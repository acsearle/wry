//
//  transport.cpp
//  client
//
//  Created by Antony Searle on 2026-10-21.
//

#include <algorithm>
#include <bit>
#include <cerrno>
#include <climits>
#include <chrono>
#include <cstring>
#include <map>
#include <mutex>
#include <optional>
#include <thread>
#include <variant>
#include <vector>

#include <arpa/inet.h>
#include <fcntl.h>
#include <libproc.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <sys/un.h>
#include <unistd.h>

#include "assert.hpp"
#include "coroutine.hpp"
#include "execution.hpp"
#include "kqueue_reactor.hpp"
#include "nursery.hpp"
#include "random.hpp"
#include "single_consumer_event.hpp"
#include "test.hpp"
#include "transport.hpp"
#include "utility.hpp"

namespace wry {

    using coroutine::Future;
    using coroutine::GetStopToken;
    using coroutine::Nursery;

    static_assert(std::endian::native == std::endian::little,
                  "the frame codec reads and writes its prefix in host order");

    // ----------------------------------------------------------------
    // Codecs.

    std::string_view Transport::LineCodec::split(std::string& pending,
                                                 std::vector<std::string>& out) const {
        size_t start = 0;
        for (size_t nl; (nl = pending.find('\n', start)) != std::string::npos; start = nl + 1) {
            if (nl - start > max_line) {
                pending.erase(0, start);
                return "line too long";
            }
            std::string line = pending.substr(start, nl - start);
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            out.push_back(std::move(line));
        }
        pending.erase(0, start);
        return (pending.size() > max_line) ? "line too long" : "";
    }

    std::string Transport::LineCodec::frame(std::string_view message) const {
        std::string bytes(message);
        bytes.push_back('\n');
        return bytes;
    }

    std::string Transport::LineCodec::farewell(std::string_view reason) const {
        return std::string(reason);
    }

    std::string_view Transport::FrameCodec::split(std::string& pending,
                                                  std::vector<std::string>& out) const {
        for (;;) {
            if (pending.size() < 4)
                break;
            uint32_t length = 0;
            std::memcpy(&length, pending.data(), sizeof length);
            if (length < 2)
                return "frame too short";
            if (pending.size() < 6)
                break;
            uint16_t tag = 0;
            std::memcpy(&tag, pending.data() + 4, sizeof tag);
            size_t cap = cap_for_tag(tag);
            if (cap == 0)
                return "unknown tag";
            if (length > cap)
                return "frame too long";
            if (pending.size() < 4 + (size_t)length)
                break;
            out.push_back(pending.substr(4, length));
            pending.erase(0, 4 + (size_t)length);
        }
        return {};
    }

    std::string Transport::FrameCodec::frame(std::string_view message) const {
        assert(message.size() >= 2);
        assert(message.size() <= UINT32_MAX);
        uint32_t length = (uint32_t)message.size();
        std::string bytes(sizeof length, '\0');
        std::memcpy(bytes.data(), &length, sizeof length);
        bytes.append(message);
        return bytes;
    }

    std::string Transport::FrameCodec::farewell(std::string_view reason) const {
        std::string message(sizeof kick_tag, '\0');
        std::memcpy(message.data(), &kick_tag, sizeof kick_tag);
        message.append(reason);
        return message;
    }

    uint16_t Transport::FrameCodec::tag_of(std::string_view message) {
        assert(message.size() >= 2);
        uint16_t tag = 0;
        std::memcpy(&tag, message.data(), sizeof tag);
        return tag;
    }

    // ----------------------------------------------------------------
    // Shared state.
    //
    // Everything the consumer's thread and the coroutines both touch.  The
    // table is under the Shared mutex; each connection's bounds are under
    // its own.  A connection's outbound queue is a single-producer (the
    // consumer) single-consumer (its writer) queue whose pop is a
    // cancelable await.

    namespace {

        // One outbound item: bytes to send, or the end of the stream (the
        // writer sends everything before it, then ends the connection).
        struct Outgoing {
            std::string bytes;
            bool end = false;
        };

        using OutQueue = coroutine::SingleProducerSingleConsumerQueue<Outgoing>;

    } // namespace

    struct Transport::Shared {
        Codec const* codec;
        Limits limits;
        std::mutex mutex;
        uint64_t next_connection = 1;
        std::map<uint64_t, std::shared_ptr<Connection>> connections;
        bool admitting = true;                  // until the loop has ended
        bool finished = false;                  // the loop has ended
        // Its own lock: the connections' readers produce, the consumer
        // consumes
        coroutine::MultiProducerSingleConsumerQueue<Event> events;
        // What the loop serves: admitted connections, and listening
        // descriptors to accept on.  Its own lock: the acceptors, dialers
        // and the owner produce, the loop consumes
        struct Arrival {
            std::shared_ptr<Connection> connection;
            int listen_fd = -1;
        };
        coroutine::MultiProducerSingleConsumerQueue<Arrival> arrivals;
        // shutdown()'s event, published here under the mutex for as long as
        // that frame is waiting on it and cleared by that frame on every
        // exit; the retirement that empties the table sets it
        coroutine::SingleConsumerEvent* drained = nullptr;
        // Set when the loop has ended: the last touch of the loop's frame
        coroutine::SingleConsumerEvent finished_event;
    };

    struct Transport::Connection {
        std::shared_ptr<Shared> shared;
        uint64_t id = 0;
        int fd = -1;                            // the serving frame's, once it runs
        std::string peer;

        std::mutex mutex;                       // the fields to the next blank line
        size_t outstanding = 0;                 // Messages delivered and alive
        coroutine::SingleConsumerEvent* credit_waiter = nullptr;    // the parked reader's
        size_t queued_bytes = 0;                // framed and not yet sent
        bool kicked = false;
        std::string reason;                     // the Closed's; first cause wins

        OutQueue out;
        // Everything of this connection: its halves run under it, its end
        // requests it (withdrawing a kick timer), a kick's deadline
        // requests it, and the loop's token is bridged into it
        std::stop_source stop;
        // The reader alone: a kick requests it, so the writer lives on to
        // deliver the farewell
        std::stop_source reader_stop;
    };

    namespace {

        using Shared = Transport::Shared;
        using Connection = Transport::Connection;

        bool set_nonblocking(int fd) {
            int flags = fcntl(fd, F_GETFL, 0);
            return flags != -1 && fcntl(fd, F_SETFL, flags | O_NONBLOCK) != -1;
        }

        std::string_view basename_of(std::string_view path) {
            size_t slash = path.rfind('/');
            return (slash == std::string_view::npos) ? path : path.substr(slash + 1);
        }

        // Who is on the other end of a Unix domain connection: the pid the
        // kernel recorded at connect (LOCAL_PEERPID), and a name.  The
        // tools are scripts, whose process name is only their interpreter,
        // so the name is the first word or two of the peer's command line
        // (KERN_PROCARGS2, basenames, a second word only if it is not an
        // option), with the process name (proc_name) as the fallback
        std::string unix_peer_label(int fd) {
            pid_t pid = 0;
            socklen_t length = sizeof pid;
            if ((::getsockopt(fd, SOL_LOCAL, LOCAL_PEERPID, &pid, &length) != 0) || (pid <= 0))
                return "unknown peer";
            std::string name;
            int mib[3] = {CTL_KERN, KERN_PROCARGS2, (int)pid};
            size_t size = 0;
            if ((::sysctl(mib, 3, nullptr, &size, nullptr, 0) == 0) && (size > sizeof(int))) {
                std::vector<char> buffer(size);
                if (::sysctl(mib, 3, buffer.data(), &size, nullptr, 0) == 0) {
                    // Layout: int argc; the executable path; NUL padding;
                    // then argv[0], argv[1], ... each NUL-terminated
                    int argc = 0;
                    std::memcpy(&argc, buffer.data(), sizeof argc);
                    const char* p = buffer.data() + sizeof(int);
                    const char* end = buffer.data() + size;
                    while ((p < end) && *p) ++p;        // the executable path
                    while ((p < end) && !*p) ++p;       // its padding
                    for (int word = 0; (word < 2) && (word < argc) && (p < end); ++word) {
                        std::string_view arg(p, ::strnlen(p, (size_t)(end - p)));
                        if ((word == 1) && (arg.empty() || (arg[0] == '-')))
                            break;
                        if (!name.empty())
                            name += ' ';
                        name += basename_of(arg);
                        p += arg.size() + 1;
                    }
                }
            }
            if (name.empty()) {
                char comm[2 * MAXCOMLEN + 1] = {};
                if (::proc_name(pid, comm, sizeof comm) > 0)
                    name = comm;
            }
            if (name.empty())
                name = "pid";
            return name + ' ' + std::to_string(pid);
        }

        // "address:port" for IPv4, "[address]:port" for IPv6
        std::string address_label(sockaddr const* address) {
            char text[INET6_ADDRSTRLEN] = {};
            if (address->sa_family == AF_INET) {
                auto* in = (sockaddr_in const*)address;
                if (!::inet_ntop(AF_INET, &in->sin_addr, text, sizeof text))
                    return "unknown peer";
                return std::string(text) + ':' + std::to_string(ntohs(in->sin_port));
            }
            if (address->sa_family == AF_INET6) {
                auto* in6 = (sockaddr_in6 const*)address;
                if (!::inet_ntop(AF_INET6, &in6->sin6_addr, text, sizeof text))
                    return "unknown peer";
                return '[' + std::string(text) + "]:" + std::to_string(ntohs(in6->sin6_port));
            }
            return "unknown peer";
        }

        std::string peer_label(int fd) {
            sockaddr_storage storage = {};
            socklen_t length = sizeof storage;
            if (::getpeername(fd, (sockaddr*)&storage, &length) != 0)
                return "unknown peer";
            if (storage.ss_family == AF_UNIX)
                return unix_peer_label(fd);
            return address_label((sockaddr const*)&storage);
        }

        // A numeric IPv4 or IPv6 address and a port, as a socket address
        bool parse_address(std::string const& address, uint16_t port,
                           sockaddr_storage& storage, socklen_t& length) {
            storage = {};
            sockaddr_in in = {};
            if (::inet_pton(AF_INET, address.c_str(), &in.sin_addr) == 1) {
                in.sin_len = sizeof in;
                in.sin_family = AF_INET;
                in.sin_port = htons(port);
                std::memcpy(&storage, &in, sizeof in);
                length = sizeof in;
                return true;
            }
            sockaddr_in6 in6 = {};
            if (::inet_pton(AF_INET6, address.c_str(), &in6.sin6_addr) == 1) {
                in6.sin6_len = sizeof in6;
                in6.sin6_family = AF_INET6;
                in6.sin6_port = htons(port);
                std::memcpy(&storage, &in6, sizeof in6);
                length = sizeof in6;
                return true;
            }
            return false;
        }

        std::string errno_message(const char* what, int error) {
            std::string message(what);
            message += ": ";
            message += std::strerror(error);
            return message;
        }

        // Owns a descriptor for the life of a coroutine frame, unless
        // released.  Frames are destroyed only after the waits on their
        // descriptor have finished (the nursery joins them first), so
        // closing here never strands an armed knote: close silently drops
        // a registered knote, and the frame waiting on it would never be
        // resumed.
        struct Descriptor {
            int fd;
            explicit Descriptor(int f) : fd(f) {}
            Descriptor(Descriptor const&) = delete;
            int release() { return std::exchange(fd, -1); }
            ~Descriptor() {
                if (fd != -1)
                    ::close(fd);
            }
        };

        struct Bridge {
            std::stop_source _stop;
            void operator()() {
                std::stop_source keepalive = _stop;     // see Nursery::Callback
                keepalive.request_stop();
            }
        };

        // Record the first cause of a connection's end, and end it: its
        // halves unwind, and a kick timer is withdrawn
        void finish(std::shared_ptr<Connection> const& connection, std::string_view reason) {
            {
                std::scoped_lock lock{connection->mutex};
                if (connection->reason.empty())
                    connection->reason = reason;
            }
            connection->stop.request_stop();
        }

        // Take the connection out of the table, report it closed, and make
        // sure nothing of it is still armed.  On whatever path the serving
        // frame ends, after its halves are joined, so after every Message
        // the reader delivered; or from the loop's drain for a connection
        // it never served
        void retire(std::shared_ptr<Connection> const& connection) {
            Shared& shared = *connection->shared;
            std::string reason;
            {
                std::scoped_lock lock{connection->mutex};
                reason = connection->reason.empty() ? std::string("closed") : connection->reason;
            }
            // Idempotent on most paths; here it withdraws a kick timer
            // that has not fired (deleted synchronously by its callback)
            connection->stop.request_stop();
            {
                std::scoped_lock lock{shared.mutex};
                shared.connections.erase(connection->id);
                if (shared.connections.empty() && shared.drained)
                    shared.drained->set();      // last touch of the event
            }
            shared.events.emplace(Transport::Closed{connection->id, std::move(reason)});
        }

        struct Registration {
            std::shared_ptr<Connection> connection;
            explicit Registration(std::shared_ptr<Connection> c) : connection(std::move(c)) {}
            Registration(Registration const&) = delete;
            ~Registration() {
                retire(connection);
            }
        };

        // Stop reading, queue the farewell and the end past the outbound
        // bound, and give the writer the grace to deliver them.  Idempotent:
        // the first kick wins, and a connection already ending is left to
        // end
        void kick_connection(std::shared_ptr<Connection> const& connection, std::string_view reason) {
            {
                std::scoped_lock lock{connection->mutex};
                if (connection->kicked)
                    return;
                connection->kicked = true;
                if (connection->reason.empty()) {
                    connection->reason = "kicked: ";
                    connection->reason += reason;
                }
            }
            Shared& shared = *connection->shared;
            // Nothing further from the peer is processed: a reader parked
            // in its recv unwinds here, on this thread
            connection->reader_stop.request_stop();
            Transport::Codec const& codec = *shared.codec;
            connection->out.emplace(Outgoing{codec.frame(codec.farewell(reason)), false});
            connection->out.emplace(Outgoing{std::string(), true});
            // The deadline: a peer that does not read never gets the
            // farewell, and the connection closes anyway.  The arm is
            // withdrawn by the connection's own stop request at its end,
            // whichever comes first; a late fire finds nothing to stop
            cancelable_after(connection->stop.get_token(), shared.limits.kick_grace,
                             [connection]() mutable {
                std::stop_source keepalive = connection->stop;
                keepalive.request_stop();
            });
        }

        // Make `fd` a connection: a record in the table, its Opened in the
        // inbox, and an arrival for the loop to serve.  The id, or -errno
        // with the descriptor closed: the loop has ended, or the table is
        // full
        int admit(std::shared_ptr<Shared> const& shared, int fd) {
            int one = 1;
            (void) ::setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
            (void) ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);    // fails harmlessly on a Unix socket
            if (!set_nonblocking(fd)) {
                int error = errno;
                ::close(fd);
                return -error;
            }
            auto connection = std::make_shared<Connection>();
            connection->shared = shared;
            connection->fd = fd;
            connection->peer = peer_label(fd);
            {
                std::scoped_lock lock{shared->mutex};
                if (!shared->admitting) {
                    ::close(fd);
                    return -ECANCELED;
                }
                if (shared->connections.size() >= shared->limits.max_connections) {
                    ::close(fd);
                    return -EMFILE;
                }
                connection->id = shared->next_connection++;
                shared->connections.emplace(connection->id, connection);
            }
            assert(connection->id <= (uint64_t)INT_MAX);
            // Before its reader can deliver, so its Opened comes first
            shared->events.emplace(Transport::Opened{connection->id, connection->peer});
            int id = (int)connection->id;
            shared->arrivals.emplace(Shared::Arrival{std::move(connection), -1});
            return id;
        }

        // Make a listening descriptor from an address, bound and listening,
        // non-blocking; the bound port is read back.  -1 with `error` set
        // on failure
        int make_tcp_listener(std::string const& address, uint16_t port,
                              uint16_t& bound_port, std::string& error) {
            sockaddr_storage storage;
            socklen_t length = 0;
            if (!parse_address(address, port, storage, length)) {
                error = "not a numeric IPv4 or IPv6 address: " + address;
                return -1;
            }
            int fd = ::socket(storage.ss_family, SOCK_STREAM, 0);
            if (fd == -1) {
                error = errno_message("socket", errno);
                return -1;
            }
            int one = 1;
            (void) ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
            if (::bind(fd, (sockaddr const*)&storage, length) == -1) {
                error = errno_message("bind", errno);
                ::close(fd);
                return -1;
            }
            if (::listen(fd, 16) == -1 || !set_nonblocking(fd)) {
                error = errno_message("listen", errno);
                ::close(fd);
                return -1;
            }
            // As bound: port 0 asked for an ephemeral one
            sockaddr_storage bound = {};
            socklen_t bound_length = sizeof bound;
            bound_port = port;
            if (::getsockname(fd, (sockaddr*)&bound, &bound_length) == 0) {
                if (bound.ss_family == AF_INET)
                    bound_port = ntohs(((sockaddr_in const*)&bound)->sin_port);
                else if (bound.ss_family == AF_INET6)
                    bound_port = ntohs(((sockaddr_in6 const*)&bound)->sin6_port);
            }
            return fd;
        }

        // Unpublishes a parked reader's credit event on every exit from
        // the wait, the cancellation unwind included; the event itself is
        // destroyed after this, so a setter that took the pointer under
        // the lock and set under the lock is done with it by then
        struct Unpublish {
            Connection* connection;
            coroutine::SingleConsumerEvent* event;
            ~Unpublish() {
                std::scoped_lock lock{connection->mutex};
                if (connection->credit_waiter == event)
                    connection->credit_waiter = nullptr;
            }
        };

        enum class ReadEnd { end_of_stream, error, violation };

        struct ReadResult {
            ReadEnd end;
            int error = 0;
            std::string violation;
        };

        // Reads and delivers until the end of the stream, an error, or a
        // violation (after delivering the messages before it).  Parks for
        // credit when the consumer has `max_unconsumed` of this
        // connection's Messages alive; the kernel buffers then fill and the
        // peer's sends block
        Future<ReadResult> read_loop(std::shared_ptr<Connection> connection) {
            Shared& shared = *connection->shared;
            Transport::Codec const& codec = *shared.codec;
            const size_t cap = shared.limits.max_unconsumed;
            std::string pending;
            std::vector<std::string> ready;
            std::string_view violation;
            char buffer[65536];
            for (;;) {
                for (std::string& message : ready) {
                    for (;;) {
                        std::unique_lock lock{connection->mutex};
                        if (connection->outstanding < cap) {
                            ++connection->outstanding;
                            break;
                        }
                        coroutine::SingleConsumerEvent credit;
                        connection->credit_waiter = &credit;
                        lock.unlock();
                        Unpublish unpublish{connection.get(), &credit};
                        co_await credit;
                    }
                    shared.events.emplace(Transport::Message{connection->id, std::move(message),
                                                             Transport::Credit{connection}});
                }
                ready.clear();
                if (!violation.empty())
                    co_return ReadResult{ReadEnd::violation, 0, std::string(violation)};
                ssize_t n = co_await recv_some(connection->fd, buffer, sizeof buffer, 0);
                if (n == -EAGAIN || n == -EINTR)
                    continue;               // readiness without data
                if (n == 0)
                    co_return ReadResult{ReadEnd::end_of_stream};
                if (n < 0)
                    co_return ReadResult{ReadEnd::error, (int)-n};
                pending.append(buffer, (size_t)n);
                violation = codec.split(pending, ready);
            }
        }

        // The reader half: the loop under the connection's reader source,
        // which a kick requests alone and the connection's stop reaches
        // through the bridge; then what its end means for the connection
        Future<> read_side(std::shared_ptr<Connection> connection) {
            std::stop_callback<Bridge> bridge{co_await GetStopToken{}, Bridge{connection->reader_stop}};
            std::optional<ReadResult> result =
                co_await execution::shield(read_loop(connection), connection->reader_stop.get_token());
            if (!result)
                co_return;                  // kicked, or the connection is ending
            switch (result->end) {
                case ReadEnd::violation:
                    kick_connection(connection, result->violation);
                    break;
                case ReadEnd::end_of_stream:
                    finish(connection, "end of stream");
                    break;
                case ReadEnd::error:
                    finish(connection, errno_message("recv", result->error));
                    break;
            }
        }

        // The writer half: sends what the consumer queues until the end
        // of the stream, then ends the connection; or until an error,
        // which ends only the writer.  A send fails because the peer's
        // receive side is gone, and its last words to us (a kick's
        // farewell) may still be in our receive buffer, so the reader
        // runs on to the end of stream.  What the consumer queues from
        // here on is never sent, and the outbound bound still kicks a
        // connection that accumulates it
        Future<> write_side(std::shared_ptr<Connection> connection) {
            for (;;) {
                Outgoing out = co_await connection->out.pop();
                if (out.end)
                    break;
                size_t sent = 0;
                int error = 0;
                while (sent < out.bytes.size()) {
                    ssize_t n = co_await send_some(connection->fd, out.bytes.data() + sent,
                                                   out.bytes.size() - sent, 0);
                    if (n == -EAGAIN || n == -EINTR)
                        continue;
                    if (n < 0) {
                        error = (int)-n;
                        break;
                    }
                    sent += (size_t)n;
                }
                {
                    std::scoped_lock lock{connection->mutex};
                    connection->queued_bytes -= std::min(connection->queued_bytes, out.bytes.size());
                }
                if (error) {
                    std::scoped_lock lock{connection->mutex};
                    if (connection->reason.empty())
                        connection->reason = errno_message("send", error);
                    co_return;
                }
            }
            finish(connection, "closed");
        }

        // One connection: its reader and writer, until either ends it.
        // The halves run under the connection's own source; the loop's
        // token reaches it through the bridge.  Cancelled from outside,
        // the nursery unwinds both before this frame (and its descriptor)
        // goes
        Future<> serve_connection(std::shared_ptr<Connection> connection) {
            Descriptor descriptor{connection->fd};
            Registration registration{connection};
            std::stop_callback<Bridge> bridge{co_await GetStopToken{}, Bridge{connection->stop}};
            Nursery halves{connection->stop.get_token()};
            halves.soon(read_side(connection));
            halves.soon(write_side(connection));
            (void) co_await halves.join();
        }

        // Accepts until stopped.  The accept runs under the accepting
        // token alone (shield), which a graceful shutdown requests by
        // itself and which a full stop reaches through the bridge in
        // serve_under
        Future<> accept_loop(std::shared_ptr<Shared> shared, int listen_fd, std::stop_token accepting) {
            Descriptor listener{listen_fd};
            for (;;) {
                std::optional<int> accepted =
                    co_await execution::shield(accept_one(listen_fd, nullptr, nullptr), accepting);
                if (!accepted)
                    break;                  // stopped
                int fd = *accepted;
                if (fd == -EAGAIN || fd == -EINTR || fd == -ECONNABORTED)
                    continue;
                if (fd < 0)
                    break;                  // the listener is broken; give up
                (void) admit(shared, fd);   // refused: closed, unreported
            }
        }

        // Serves every arrival until stopped, then joins them: a connection
        // gets its reader and writer, a listener gets an accept loop.  The
        // take runs under the accepting token (shield), as the accept
        // does, so a stop is absorbed here and this frame lives on to join
        // its nursery: a graceful shutdown lets the connections finish, a
        // full stop unwinds them through the token they inherit
        Future<> serve_loop(std::shared_ptr<Shared> shared, std::stop_token accepting) {
            Nursery children{co_await GetStopToken{}};
            for (;;) {
                auto popping = shared->arrivals.pop();
                std::optional<Shared::Arrival> arrival =
                    co_await execution::shield(coroutine::lift_awaitable<decltype(popping)>(&popping),
                                               accepting);
                if (!arrival)
                    break;                  // stopped
                if (arrival->listen_fd != -1)
                    children.soon(accept_loop(shared, arrival->listen_fd, accepting));
                else
                    children.soon(serve_connection(std::move(arrival->connection)));
            }
            // No more admissions, and whatever arrived meanwhile is closed
            // unserved
            {
                std::scoped_lock lock{shared->mutex};
                shared->admitting = false;
            }
            for (auto& arrival : shared->arrivals.try_take()) {
                if (arrival.listen_fd != -1) {
                    ::close(arrival.listen_fd);
                } else {
                    ::close(arrival.connection->fd);
                    retire(arrival.connection);
                }
            }
            (void) co_await children.join();
        }

        // The root of the transport's work: the loop under three tokens,
        // bridged in a chain.  The spawner's (inherited through the
        // delegate chain: the root token, a nursery's) requests the
        // object's own (close()), which requests the accepting one
        // (shutdown()); the shield runs the loop under the object's token
        // alone and absorbs the stop at this boundary.  Reports finished
        // last: the object may be destroyed the moment it is, so nothing of
        // it is touched after
        coroutine::Task serve_under(std::shared_ptr<Shared> shared,
                                    std::stop_source stop, std::stop_source accepting) {
            std::stop_callback<Bridge> outer_to_stop{co_await GetStopToken{}, Bridge{stop}};
            std::stop_callback<Bridge> stop_to_accepting{stop.get_token(), Bridge{accepting}};
            (void) co_await execution::shield(serve_loop(shared, accepting.get_token()),
                                              stop.get_token());
            {
                std::scoped_lock lock{shared->mutex};
                shared->finished = true;
            }
            shared->finished_event.set();
        }

        std::shared_ptr<Connection> lookup(Shared& shared, uint64_t id) {
            std::scoped_lock lock{shared.mutex};
            auto it = shared.connections.find(id);
            return (it == shared.connections.end()) ? nullptr : it->second;
        }

    } // namespace

    // ----------------------------------------------------------------
    // Credit.

    Transport::Credit::~Credit() {
        if (!_connection)
            return;
        Connection& connection = *_connection;
        std::scoped_lock lock{connection.mutex};
        assert(connection.outstanding > 0);
        --connection.outstanding;
        // Under the lock: the reader's Unpublish takes the same lock
        // before its event dies, so the event outlives this set
        if ((connection.outstanding < connection.shared->limits.max_unconsumed) && connection.credit_waiter)
            take(connection.credit_waiter)->set();
    }

    Transport::Credit& Transport::Credit::operator=(Credit&& other) {
        Credit discarded(std::move(*this));
        _connection = std::move(other._connection);
        return *this;
    }

    // ----------------------------------------------------------------
    // Transport.

    std::unique_ptr<Transport> Transport::make(Codec const& codec, Limits limits) {
        std::unique_ptr<Transport> t(new Transport);
        t->_shared = std::make_shared<Shared>();
        t->_shared->codec = &codec;
        t->_shared->limits = limits;
        return t;
    }

    std::unique_ptr<Transport> Transport::listen_unix(std::string const& path,
                                                      Codec const& codec, Limits limits,
                                                      std::string& error) {
        sockaddr_un address = {};
        address.sun_family = AF_UNIX;
        if (path.empty() || path.size() >= sizeof(address.sun_path)) {
            error = "socket path is empty or too long";
            return nullptr;
        }
        std::memcpy(address.sun_path, path.data(), path.size());

        int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
        if (fd == -1) {
            error = errno_message("socket", errno);
            return nullptr;
        }
        // A socket file left by a previous run would make bind fail.
        (void) ::unlink(path.c_str());
        // Owner-only from creation: bind makes the file under the umask.
        // The umask is process-wide, so this is done once, at startup.
        mode_t old_mask = ::umask(0177);
        int bound = ::bind(fd, (sockaddr const*)&address, sizeof(address));
        ::umask(old_mask);
        if (bound == -1 || ::listen(fd, 16) == -1 || !set_nonblocking(fd)) {
            error = errno_message(bound == -1 ? "bind" : "listen", errno);
            ::close(fd);
            return nullptr;
        }

        std::unique_ptr<Transport> t = make(codec, limits);
        t->_path = path;
        t->_listen_fd = fd;
        return t;
    }

    std::unique_ptr<Transport> Transport::listen_tcp(std::string const& address, uint16_t port,
                                                     Codec const& codec, Limits limits,
                                                     std::string& error) {
        uint16_t bound_port = 0;
        int fd = make_tcp_listener(address, port, bound_port, error);
        if (fd == -1)
            return nullptr;
        std::unique_ptr<Transport> t = make(codec, limits);
        t->_port = bound_port;
        t->_listen_fd = fd;
        return t;
    }

    coroutine::Task Transport::serve() {
        assert(!_served);
        _served = true;
        // From here the loop owns the listening descriptor, if any
        if (_listen_fd != -1)
            _shared->arrivals.emplace(Shared::Arrival{nullptr, std::exchange(_listen_fd, -1)});
        return serve_under(_shared, _stop, _stop_accepting);
    }

    int Transport::adopt(int fd) {
        return admit(_shared, fd);
    }

    std::expected<uint16_t, std::string> Transport::listen_tcp(std::string const& address, uint16_t port) {
        std::string error;
        uint16_t bound_port = 0;
        int fd = make_tcp_listener(address, port, bound_port, error);
        if (fd == -1)
            return std::unexpected(std::move(error));
        {
            std::scoped_lock lock{_shared->mutex};
            if (!_shared->admitting) {
                ::close(fd);
                return std::unexpected(std::string("the transport has stopped"));
            }
        }
        _port = bound_port;
        _shared->arrivals.emplace(Shared::Arrival{nullptr, fd});
        return bound_port;
    }

    void Transport::post(uint64_t code, std::string bytes) {
        _shared->events.emplace(Posted{code, std::move(bytes)});
    }

    coroutine::Future<int> Transport::connect_tcp(std::string address, uint16_t port) {
        sockaddr_storage storage;
        socklen_t length = 0;
        if (!parse_address(address, port, storage, length))
            co_return -EINVAL;
        int fd = ::socket(storage.ss_family, SOCK_STREAM, 0);
        if (fd == -1)
            co_return -errno;
        Descriptor descriptor{fd};
        int one = 1;
        (void) ::setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
        if (!set_nonblocking(fd))
            co_return -errno;
        int connected = co_await connect_to(fd, (sockaddr const*)&storage, length);
        if (connected < 0)
            co_return connected;
        co_return admit(_shared, descriptor.release());
    }

    bool Transport::finished() {
        if (!_served)
            return true;
        std::scoped_lock lock{_shared->mutex};
        return _shared->finished;
    }

    coroutine::Future<> Transport::wait_finished() {
        if (!_served)
            co_return;
        co_await _shared->finished_event;
    }

    Transport::~Transport() {
        assert(finished());
        if (_listen_fd != -1)
            ::close(_listen_fd);
        if (!_path.empty())
            (void) ::unlink(_path.c_str());
    }

    coroutine::Task Transport::shutdown(std::chrono::milliseconds grace) {
        std::shared_ptr<Shared> shared = _shared;
        // No new connections
        _stop_accepting.request_stop();
        // Ask every connection to finish what is queued for it and close,
        // and wait for the table to empty.  The event lives in this frame
        // and is published under the same lock as the snapshot, so only
        // this snapshot's emptying sets it; the guard unpublishes it on
        // every exit, the deadline unwind included
        coroutine::SingleConsumerEvent drained;
        struct UnpublishDrained {
            std::shared_ptr<Shared> shared;
            ~UnpublishDrained() {
                std::scoped_lock lock{shared->mutex};
                shared->drained = nullptr;
            }
        } unpublish{shared};
        std::vector<std::shared_ptr<Connection>> connections;
        {
            std::scoped_lock lock{shared->mutex};
            for (auto& [id, connection] : shared->connections)
                connections.push_back(connection);
            if (!connections.empty())
                shared->drained = &drained;
        }
        for (auto& connection : connections)
            connection->out.emplace(Outgoing{std::string(), true});
        if (!connections.empty())
            (void) co_await with_deadline(std::chrono::steady_clock::now() + grace, drained);
        // Then the rest, brutally
        close();
    }

    void Transport::close() {
        // The cancellation unwinds frames parked on this thread, inside
        // request_stop; frames running on workers finish there, and the
        // spawner's join waits for them.  The accepting source follows
        // through the bridge in serve_under
        _stop.request_stop();
        if (!_path.empty())
            (void) ::unlink(_path.c_str());
    }

    coroutine::MultiProducerSingleConsumerQueue<Transport::Event>& Transport::events() {
        return _shared->events;
    }

    void Transport::send(uint64_t id, std::string_view message) {
        std::shared_ptr<Connection> connection = lookup(*_shared, id);
        if (!connection)
            return;
        std::string bytes = _shared->codec->frame(message);
        bool over = false;
        {
            std::scoped_lock lock{connection->mutex};
            if (connection->kicked)
                return;
            over = connection->queued_bytes + bytes.size() > _shared->limits.max_queued_bytes;
            if (!over) {
                connection->queued_bytes += bytes.size();
                // The emplace only schedules a parked writer, which takes
                // this lock after its send, so holding it here is harmless
                connection->out.emplace(Outgoing{std::move(bytes), false});
            }
        }
        if (over)
            kick_connection(connection, "slow consumer");
    }

    void Transport::kick(uint64_t id, std::string_view reason) {
        if (std::shared_ptr<Connection> connection = lookup(*_shared, id))
            kick_connection(connection, reason);
    }

    void Transport::end(uint64_t id) {
        if (std::shared_ptr<Connection> connection = lookup(*_shared, id))
            connection->out.emplace(Outgoing{std::string(), true});
    }

    size_t Transport::connections() {
        std::scoped_lock lock{_shared->mutex};
        return _shared->connections.size();
    }

    // ----------------------------------------------------------------
    // Tests.

    namespace {

        // The test protocol: tag 1 is small, tag 2 is large, tag 9 is the
        // kick, anything else is unknown
        size_t test_cap(uint16_t tag) {
            switch (tag) {
                case 1: return 64;
                case 2: return 1 << 20;
                case 9: return 1024;
                default: return 0;
            }
        }

        std::string tagged(uint16_t tag, std::string_view payload) {
            std::string message(sizeof tag, '\0');
            std::memcpy(message.data(), &tag, sizeof tag);
            message.append(payload);
            return message;
        }

        std::string test_socket_path(const char* name) {
            return std::string("/tmp/wry-") + name + '-' + std::to_string(::getpid()) + ".sock";
        }

        int connect_unix(std::string const& path) {
            int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
            assert(fd != -1);
            sockaddr_un address = {};
            address.sun_family = AF_UNIX;
            std::memcpy(address.sun_path, path.data(), path.size());
            int connected = ::connect(fd, (sockaddr const*)&address, sizeof(address));
            assert(connected == 0);
            (void) connected;
            int one = 1;
            (void) ::setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
            return fd;
        }

        // Read until `want` bytes have arrived, or time out
        std::string read_bytes(int fd, size_t want) {
            std::string got;
            auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            while (got.size() < want && std::chrono::steady_clock::now() < deadline) {
                pollfd p{fd, POLLIN, 0};
                if (::poll(&p, 1, 50) <= 0)
                    continue;
                char buffer[65536];
                ssize_t n = ::read(fd, buffer, std::min(sizeof buffer, want - got.size()));
                if (n <= 0)
                    break;
                got.append(buffer, (size_t)n);
            }
            return got;
        }

        // Read until `want` newline-terminated lines have arrived, or time
        // out (the caller asserts on the count)
        std::vector<std::string> read_lines(int fd, size_t want) {
            std::vector<std::string> lines;
            std::string pending;
            auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            while (lines.size() < want && std::chrono::steady_clock::now() < deadline) {
                pollfd p{fd, POLLIN, 0};
                if (::poll(&p, 1, 50) <= 0)
                    continue;
                char buffer[4096];
                ssize_t n = ::read(fd, buffer, sizeof(buffer));
                if (n <= 0)
                    break;
                pending.append(buffer, (size_t)n);
                for (size_t nl; (nl = pending.find('\n')) != std::string::npos; ) {
                    lines.push_back(pending.substr(0, nl));
                    pending.erase(0, nl + 1);
                }
            }
            return lines;
        }

        // Write all of `bytes`, blocking as needed, or time out
        bool write_all(int fd, std::string_view bytes) {
            auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            size_t sent = 0;
            while (sent < bytes.size() && std::chrono::steady_clock::now() < deadline) {
                ssize_t n = ::send(fd, bytes.data() + sent, bytes.size() - sent, 0);
                if (n > 0) {
                    sent += (size_t)n;
                    continue;
                }
                if (n < 0 && errno == EAGAIN) {
                    pollfd p{fd, POLLOUT, 0};
                    (void) ::poll(&p, 1, 50);
                    continue;
                }
                return false;
            }
            return sent == bytes.size();
        }

        // Drains the inbox: messages into `messages`, closings into
        // `closed`, openings into `opened`
        void drain(Transport& transport,
                   std::vector<Transport::Message>& messages,
                   std::vector<Transport::Closed>& closed,
                   std::vector<Transport::Opened>* opened = nullptr) {
            for (auto& event : transport.events().try_take()) {
                if (auto* message = std::get_if<Transport::Message>(&event))
                    messages.push_back(std::move(*message));
                else if (auto* open = std::get_if<Transport::Opened>(&event)) {
                    if (opened)
                        opened->push_back(std::move(*open));
                } else if (auto* close = std::get_if<Transport::Closed>(&event))
                    closed.push_back(std::move(*close));
            }
        }

        template<typename F>
        bool eventually(F&& f, std::chrono::seconds timeout = std::chrono::seconds(5)) {
            auto deadline = std::chrono::steady_clock::now() + timeout;
            while (!f()) {
                if (std::chrono::steady_clock::now() >= deadline)
                    return false;
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            return true;
        }

    } // namespace

    define_test("transport_frame_codec") {
        Transport::FrameCodec codec{test_cap, 9};
        wry::rand random{20261021};

        // Chunking independence: a stream of frames split at random points
        // yields the same messages, and nothing is left pending
        std::vector<std::string> messages;
        std::string stream;
        for (int i = 0; i != 200; ++i) {
            uint16_t tag = (random() % 4 == 0) ? 2 : 1;
            size_t length = random() % std::min(test_cap(tag) - 2, (size_t)4096);
            std::string payload(length, '\0');
            for (char& c : payload)
                c = (char)(random() & 0xff);
            std::string message = tagged(tag, payload);
            stream += codec.frame(message);
            messages.push_back(std::move(message));
        }
        for (int trial = 0; trial != 20; ++trial) {
            std::string pending;
            std::vector<std::string> out;
            size_t at = 0;
            while (at < stream.size()) {
                size_t n = std::min(stream.size() - at, (size_t)(1 + random() % 97));
                pending.append(stream, at, n);
                at += n;
                std::string_view violation = codec.split(pending, out);
                assert(violation.empty());
                (void) violation;
            }
            assert(pending.empty());
            assert(out == messages);
        }

        // Violations, each reported once the six-byte prefix is in, with
        // the messages before it delivered
        auto violation_of = [&](std::string bytes) {
            std::string pending = codec.frame(tagged(1, "first")) + bytes;
            std::vector<std::string> out;
            std::string_view violation = codec.split(pending, out);
            assert(out.size() == 1 && out[0] == tagged(1, "first"));
            return std::string(violation);
        };
        {
            std::string over = codec.frame(tagged(1, std::string(70, 'x')));
            assert(violation_of(over) == "frame too long");
            assert(violation_of(over.substr(0, 6)) == "frame too long");     // before the body
            assert(violation_of(over.substr(0, 5)).empty());                 // not yet
            std::string unknown = codec.frame(tagged(7, "?"));
            assert(violation_of(unknown) == "unknown tag");
            std::string one_byte = codec.frame(tagged(1, "")).substr(0, 4);
            one_byte[0] = 1;                                                 // length 1: no room for a tag
            assert(violation_of(one_byte) == "frame too short");
            assert(violation_of("").empty());
        }

        // The farewell carries the reason under the kick tag
        {
            std::string farewell = codec.farewell("because");
            assert(Transport::FrameCodec::tag_of(farewell) == 9);
            assert(farewell.substr(2) == "because");
        }
        co_return;
    };

    define_test("transport_line_codec") {
        Transport::LineCodec codec{8};
        std::string pending = "one\r\ntwo\nthr";
        std::vector<std::string> out;
        assert(codec.split(pending, out).empty());
        assert(out.size() == 2 && out[0] == "one" && out[1] == "two");
        assert(pending == "thr");
        pending += "ee\n";
        assert(codec.split(pending, out).empty());
        assert(out.size() == 3 && out[2] == "three" && pending.empty());
        // A partial line over the cap, and a complete one
        pending = "123456789";
        assert(codec.split(pending, out) == "line too long");
        pending = "ok\n123456789\n";
        assert(codec.split(pending, out) == "line too long");
        assert(out.size() == 4 && out[3] == "ok");
        assert(codec.frame("x") == "x\n");
        assert(codec.farewell("why") == "why");
        co_return;
    };

    define_test("transport_tcp") {
        Transport::FrameCodec codec{test_cap, 9};
        Transport::Limits limits;
        std::string error;
        auto server = Transport::listen_tcp("127.0.0.1", 0, codec, limits, error);
        assert(server && error.empty());
        assert(server->port() != 0);
        auto client = Transport::make(codec, limits);
        Nursery nursery;
        nursery.soon(server->serve());
        nursery.soon(client->serve());

        int id = co_await client->connect_tcp("127.0.0.1", server->port());
        assert(id > 0);

        // Both sides see the connection open, labelled by address
        std::vector<Transport::Message> server_messages, client_messages;
        std::vector<Transport::Closed> server_closed, client_closed;
        std::vector<Transport::Opened> server_opened, client_opened;
        client->send((uint64_t)id, tagged(1, "hello"));
        bool heard = eventually([&] {
            drain(*server, server_messages, server_closed, &server_opened);
            drain(*client, client_messages, client_closed, &client_opened);
            return !server_messages.empty();
        });
        assert(heard);
        (void) heard;
        assert(server_opened.size() == 1 && server_opened[0].peer.starts_with("127.0.0.1:"));
        assert(client_opened.size() == 1 && client_opened[0].connection == (uint64_t)id);
        assert(client_opened[0].peer == "127.0.0.1:" + std::to_string(server->port()));
        assert(server_messages[0].bytes == tagged(1, "hello"));
        const uint64_t accepted = server_messages[0].connection;
        assert(accepted == server_opened[0].connection);
        assert(server->connections() == 1 && client->connections() == 1);

        // A message bigger than the socket buffers arrives whole
        {
            std::string big = tagged(2, std::string(300000, 'b'));
            server->send(accepted, big);
            bool arrived = eventually([&] {
                drain(*client, client_messages, client_closed);
                return !client_messages.empty();
            });
            assert(arrived && client_messages[0].bytes == big);
            (void) arrived;
        }

        // A kick: the farewell reaches the peer, then the end of stream;
        // the kicker records the reason
        server->kick(accepted, "testing");
        bool farewell = eventually([&] {
            drain(*client, client_messages, client_closed);
            drain(*server, server_messages, server_closed);
            return !client_closed.empty() && !server_closed.empty();
        });
        assert(farewell);
        (void) farewell;
        assert(client_messages.size() == 2 && client_messages[1].bytes == tagged(9, "testing"));
        assert(client_closed[0].reason == "end of stream");
        assert(server_closed[0].connection == accepted && server_closed[0].reason == "kicked: testing");
        assert(server->connections() == 0 && client->connections() == 0);

        // Dialing a closed port fails cleanly
        {
            auto refused = Transport::listen_tcp("127.0.0.1", 0, codec, limits, error);
            uint16_t port = refused->port();
            refused.reset();
            int result = co_await client->connect_tcp("127.0.0.1", port);
            assert(result == -ECONNREFUSED);
            (void) result;
        }

        co_await server->shutdown(std::chrono::milliseconds(100));
        co_await client->shutdown(std::chrono::milliseconds(100));
        (void) co_await nursery.join();
        assert(server->finished() && client->finished());
        co_return;
    };

    define_test("transport_inbound_bound") {
        // A peer that sends faster than the consumer consumes is held at
        // the bound: at most max_unconsumed of its Messages alive, the
        // rest in the kernel buffers until its sends block
        Transport::LineCodec codec{1 << 20};
        Transport::Limits limits;
        limits.max_unconsumed = 4;
        std::string path = test_socket_path("transport-inbound");
        std::string error;
        auto server = Transport::listen_unix(path, codec, limits, error);
        assert(server && error.empty());
        Nursery nursery;
        nursery.soon(server->serve());

        int client = connect_unix(path);
        assert(set_nonblocking(client));
        const std::string line = std::string(65536, 'l') + '\n';
        size_t written = 0;
        // A non-blocking send of the line from wherever the last partial
        // send stopped: the bytes sent, or -1 with EAGAIN
        auto send_line = [&] {
            size_t offset = written % line.size();
            ssize_t n = ::send(client, line.data() + offset, line.size() - offset, 0);
            if (n > 0)
                written += (size_t)n;
            return n;
        };
        bool blocked = false;
        {
            auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            while (std::chrono::steady_clock::now() < deadline) {
                ssize_t n = send_line();
                if (n > 0)
                    continue;
                assert(n < 0 && errno == EAGAIN);
                // Blocked: give the reader a chance, then see if it stays
                // blocked, which is the bound holding
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                if (send_line() > 0)
                    continue;
                blocked = true;
                break;
            }
        }
        // The inbox holds no more than the bound, and the rest is in
        // flight: in the reader's hands or the kernel's
        std::vector<Transport::Message> held;
        std::vector<Transport::Closed> closed;
        drain(*server, held, closed);
        assert(blocked);
        (void) blocked;
        assert(held.size() == limits.max_unconsumed);
        assert(closed.empty());
        // Consuming (destroying the Messages) returns the credits: the
        // reader resumes, the peer can send again, and everything written
        // arrives, whole.  From here every batch is tallied and dropped
        size_t delivered_bytes = 0;
        size_t delivered_count = 0;
        auto consume = [&] {
            std::vector<Transport::Message> batch;
            drain(*server, batch, closed);
            for (auto& message : batch) {
                assert(message.bytes.size() == line.size() - 1);
                delivered_bytes += message.bytes.size() + 1;
                ++delivered_count;
            }
        };
        for (auto& message : held)
            delivered_bytes += message.bytes.size() + 1;
        delivered_count = held.size();
        held.clear();
        bool flowed = eventually([&] {
            consume();
            return delivered_count > limits.max_unconsumed;
        });
        assert(flowed);
        (void) flowed;
        bool unblocked = eventually([&] {
            ssize_t n = send_line();
            consume();
            return n > 0;
        });
        assert(unblocked);
        (void) unblocked;
        ::close(client);
        bool complete = eventually([&] {
            consume();
            return !closed.empty();
        });
        assert(complete);
        (void) complete;
        consume();
        // Whole lines arrive as messages; a final partial line is lost at
        // the close, as it should be
        assert(delivered_bytes <= written && written - delivered_bytes < line.size());
        assert(closed[0].reason == "end of stream");

        co_await server->shutdown(std::chrono::milliseconds(100));
        (void) co_await nursery.join();
        co_return;
    };

    define_test("transport_outbound_bound") {
        // A peer that does not read is kicked as a slow consumer once its
        // unsent bytes would exceed the bound, and closed after the grace
        // even though the farewell cannot be delivered
        Transport::LineCodec codec{1 << 20};
        Transport::Limits limits;
        limits.max_queued_bytes = 256 << 10;
        limits.kick_grace = std::chrono::milliseconds(200);
        std::string path = test_socket_path("transport-outbound");
        std::string error;
        auto server = Transport::listen_unix(path, codec, limits, error);
        assert(server && error.empty());
        Nursery nursery;
        nursery.soon(server->serve());

        int client = connect_unix(path);
        assert(write_all(client, "hi\n"));
        std::vector<Transport::Message> messages;
        std::vector<Transport::Closed> closed;
        bool heard = eventually([&] {
            drain(*server, messages, closed);
            return !messages.empty();
        });
        assert(heard);
        (void) heard;
        const uint64_t id = messages[0].connection;
        auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i != 64; ++i)
            server->send(id, std::string(64 << 10, 'y'));       // nobody reads it
        bool kicked = eventually([&] {
            drain(*server, messages, closed);
            return !closed.empty();
        });
        assert(kicked);
        (void) kicked;
        assert(closed[0].connection == id && closed[0].reason == "kicked: slow consumer");
        assert(std::chrono::steady_clock::now() - t0 >= std::chrono::milliseconds(150));
        assert(server->connections() == 0);
        ::close(client);

        co_await server->shutdown(std::chrono::milliseconds(100));
        (void) co_await nursery.join();
        co_return;
    };

    define_test("transport_kick") {
        // A kick stops reading at once: what the peer sent after it is
        // never delivered, and the peer reads the farewell then the end of
        // stream
        Transport::LineCodec codec{1 << 20};
        Transport::Limits limits;
        std::string path = test_socket_path("transport-kick");
        std::string error;
        auto server = Transport::listen_unix(path, codec, limits, error);
        assert(server && error.empty());
        Nursery nursery;
        nursery.soon(server->serve());

        int client = connect_unix(path);
        assert(write_all(client, "one\n"));
        std::vector<Transport::Message> messages;
        std::vector<Transport::Closed> closed;
        std::vector<Transport::Opened> opened;
        bool heard = eventually([&] {
            drain(*server, messages, closed, &opened);
            return !messages.empty();
        });
        assert(heard);
        (void) heard;
        assert(opened.size() == 1 && opened[0].peer == "client " + std::to_string(::getpid()));
        const uint64_t id = messages[0].connection;
        server->kick(id, "bye");
        server->kick(id, "again");                      // idempotent: the first wins
        (void) ::send(client, "late\n", 5, 0);        // may already be refused
        auto lines = read_lines(client, 1);
        assert(lines.size() == 1 && lines[0] == "bye");
        char byte;
        const ssize_t after = ::read(client, &byte, 1);
        assert(after == 0);                             // then end of stream
        (void) after;
        bool reported = eventually([&] {
            drain(*server, messages, closed);
            return !closed.empty();
        });
        assert(reported);
        (void) reported;
        assert(closed[0].reason == "kicked: bye");
        assert(messages.size() == 1);                   // never "late"
        server->send(id, "nobody hears this");
        ::close(client);

        co_await server->shutdown(std::chrono::milliseconds(100));
        (void) co_await nursery.join();
        server.reset();

        // A violation kicks with its reason, after the messages before it
        Transport::LineCodec strict{8};
        auto picky = Transport::listen_unix(path, strict, limits, error);
        assert(picky && error.empty());
        Nursery scope;
        scope.soon(picky->serve());
        int bad = connect_unix(path);
        assert(write_all(bad, "fine\n123456789\n"));
        bool violated = eventually([&] {
            drain(*picky, messages, closed);
            return closed.size() == 2;
        });
        assert(violated);
        (void) violated;
        assert(messages.size() == 2 && messages[1].bytes == "fine");
        assert(closed[1].reason == "kicked: line too long");
        auto farewell = read_lines(bad, 1);
        assert(farewell.size() == 1 && farewell[0] == "line too long");
        ::close(bad);
        co_await picky->shutdown(std::chrono::milliseconds(100));
        (void) co_await scope.join();
        co_return;
    };

} // namespace wry
