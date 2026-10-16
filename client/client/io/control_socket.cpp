//
//  control_socket.cpp
//  client
//
//  Created by Antony Searle on 2026-10-12.
//

#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <map>
#include <mutex>
#include <thread>
#include <variant>

#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include "assert.hpp"
#include "control_socket.hpp"
#include "coroutine.hpp"
#include "kqueue_reactor.hpp"
#include "test.hpp"

namespace wry {

    using Coroutine::Future;
    using Coroutine::GetStopToken;
    using Coroutine::Nursery;

    // ----------------------------------------------------------------
    // Shared state.
    //
    // Everything the main thread and the coroutines both touch, under one
    // mutex.  Each connection's outbound queue is a single-producer (the
    // main thread) single-consumer (its writer) queue whose pop is a
    // cancelable await.

    namespace {

        // One outbound item: bytes to send, or the end of the stream (the
        // writer sends everything before it, then ends the connection).
        struct Outgoing {
            std::string bytes;
            bool end = false;
        };

        using OutQueue = Coroutine::SingleProducerSingleConsumerQueue<Outgoing>;

    } // namespace

    struct ControlSocket::Shared {
        std::mutex mutex;
        std::condition_variable changed;        // notified on connections / finished
        uint64_t next_connection = 1;
        std::map<uint64_t, std::shared_ptr<OutQueue>> connections;
        bool finished = false;                  // the accept loop has ended
        // Its own lock: the connections' readers produce, the main thread
        // consumes
        Coroutine::MultiProducerSingleConsumerQueue<Event> events;
        // A signal per time the connection table empties; shutdown() is
        // its consumer
        Coroutine::MultiProducerSingleConsumerQueue<std::monostate> drained;
    };

    namespace {

        using Shared = ControlSocket::Shared;

        bool set_nonblocking(int fd) {
            int flags = fcntl(fd, F_GETFL, 0);
            return flags != -1 && fcntl(fd, F_SETFL, flags | O_NONBLOCK) != -1;
        }

        std::string errno_message(const char* what) {
            std::string message(what);
            message += ": ";
            message += std::strerror(errno);
            return message;
        }

        // Owns a descriptor for the life of a coroutine frame.  Frames are
        // destroyed only after the waits on their descriptor have finished
        // (the nursery joins them first), so closing here never strands an
        // armed knote: close silently drops a registered knote, and the
        // frame waiting on it would never be resumed.
        struct Descriptor {
            int fd;
            explicit Descriptor(int f) : fd(f) {}
            Descriptor(Descriptor const&) = delete;
            ~Descriptor() { ::close(fd); }
        };

        // A connection's registration: reports the connection closed, on
        // whatever path its frame ends, before its descriptor closes.
        struct Registration {
            std::shared_ptr<Shared> shared;
            uint64_t id;
            Registration(std::shared_ptr<Shared> s, uint64_t i) : shared(std::move(s)), id(i) {}
            Registration(Registration const&) = delete;
            ~Registration() {
                bool last;
                {
                    std::scoped_lock lock{shared->mutex};
                    shared->connections.erase(id);
                    last = shared->connections.empty();
                }
                shared->changed.notify_all();
                if (last)
                    shared->drained.emplace();
                // After every line the reader delivered: the reader is joined
                // before this runs
                shared->events.emplace(ControlSocket::Closed{id});
            }
        };

        // Split what has arrived into lines for the main thread; false if
        // the partial line has grown too long.
        bool deliver_lines(Shared& shared, uint64_t id, std::string& pending) {
            size_t start = 0;
            for (size_t nl; (nl = pending.find('\n', start)) != std::string::npos; start = nl + 1) {
                std::string line = pending.substr(start, nl - start);
                if (!line.empty() && line.back() == '\r')
                    line.pop_back();
                shared.events.emplace(ControlSocket::Line{id, std::move(line)});
            }
            pending.erase(0, start);
            return pending.size() <= ControlSocket::max_line;
        }

        // Reads requests until end of stream, an error, or an overlong
        // line; then ends the connection by stopping its writer.
        Future<> read_requests(std::shared_ptr<Shared> shared, uint64_t id, int fd, Nursery* halves) {
            std::string pending;
            char buffer[65536];
            for (;;) {
                ssize_t n = co_await recv_some(fd, buffer, sizeof(buffer), 0);
                if (n == -EAGAIN || n == -EINTR)
                    continue;               // readiness without data
                if (n <= 0)
                    break;                  // end of stream, or an error
                pending.append(buffer, (size_t)n);
                if (!deliver_lines(*shared, id, pending))
                    break;
            }
            halves->request_stop();
        }

        // Sends replies as the main thread queues them, until the end of
        // the stream or an error; then ends the connection by stopping its
        // reader.
        Future<> write_replies(std::shared_ptr<OutQueue> queue, int fd, Nursery* halves) {
            for (;;) {
                Outgoing out = co_await queue->pop();
                if (out.end)
                    break;
                size_t sent = 0;
                while (sent < out.bytes.size()) {
                    ssize_t n = co_await send_some(fd, out.bytes.data() + sent,
                                                   out.bytes.size() - sent, 0);
                    if (n == -EAGAIN || n == -EINTR)
                        continue;
                    if (n < 0) {
                        halves->request_stop();
                        co_return;
                    }
                    sent += (size_t)n;
                }
            }
            halves->request_stop();
        }

        // One connection: its reader and writer, until either ends it.
        // Cancelled from outside, the nursery unwinds both before this
        // frame (and its descriptor) goes.
        Future<> serve_connection(std::shared_ptr<Shared> shared,
                                  std::shared_ptr<OutQueue> queue,
                                  uint64_t id, int fd) {
            Descriptor descriptor{fd};
            Registration registration{shared, id};
            Nursery halves{co_await GetStopToken{}};
            halves.soon(read_requests(shared, id, fd, &halves));
            halves.soon(write_replies(std::move(queue), fd, &halves));
            (void) co_await halves.join();
        }

        // Accepts connections until stopped, then joins them.  The accept
        // runs under the accepting token alone (shield), which a graceful
        // shutdown requests by itself and which a full stop reaches through
        // the bridge in serve_under; the connections run under this frame's
        // token, so a graceful shutdown lets them finish while a full stop
        // unwinds them.  Either way the stop is absorbed at the accept and
        // this frame lives on to join its nursery.
        Future<> accept_connections(std::shared_ptr<Shared> shared, int listen_fd,
                                    std::stop_token accepting) {
            Descriptor listener{listen_fd};
            Nursery connections{co_await GetStopToken{}};
            for (;;) {
                // shield: the accept runs under `accepting` and a stop there
                // is absorbed as nullopt
                std::optional<int> accepted =
                    co_await Coroutine::shield(accept_one(listen_fd, nullptr, nullptr), accepting);
                if (!accepted)
                    break;                  // stopped
                int fd = *accepted;
                if (fd == -EAGAIN || fd == -EINTR || fd == -ECONNABORTED)
                    continue;
                if (fd < 0)
                    break;                  // the listener is broken; give up
                int one = 1;
                (void) ::setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
                auto queue = std::make_shared<OutQueue>();
                uint64_t id = 0;
                {
                    std::scoped_lock lock{shared->mutex};
                    if (shared->connections.size() < ControlSocket::max_connections) {
                        id = shared->next_connection++;
                        shared->connections.emplace(id, queue);
                    }
                }
                if (!id || !set_nonblocking(fd)) {
                    if (id) {
                        std::scoped_lock lock{shared->mutex};
                        shared->connections.erase(id);
                    }
                    ::close(fd);            // nothing is waiting on it yet
                    continue;
                }
                connections.soon(serve_connection(shared, std::move(queue), id, fd));
            }
            (void) co_await connections.join();
        }

        struct Bridge {
            std::stop_source _stop;
            void operator()() {
                std::stop_source keepalive = _stop;     // see Nursery::Callback
                keepalive.request_stop();
            }
        };

        // The root of the socket's work: the accept loop under three
        // tokens, bridged in a chain.  The spawner's (inherited through the
        // delegate chain: the wait group's root token, a nursery's) requests
        // the object's own (close()), which requests the accepting one
        // (shutdown()); the shield runs the loop under the object's token
        // alone and absorbs the stop at this boundary.  Reports finished
        // last: the object may be destroyed the moment it is, so nothing of
        // it is touched after
        Coroutine::Task serve_under(std::shared_ptr<Shared> shared, int listen_fd,
                                    std::stop_source stop, std::stop_source accepting) {
            std::stop_callback<Bridge> outer_to_stop{co_await GetStopToken{}, Bridge{stop}};
            std::stop_callback<Bridge> stop_to_accepting{stop.get_token(), Bridge{accepting}};
            (void) co_await Coroutine::shield(accept_connections(shared, listen_fd, accepting.get_token()),
                                              stop.get_token());
            {
                std::scoped_lock lock{shared->mutex};
                shared->finished = true;
            }
            shared->changed.notify_all();
        }

    } // namespace

    std::unique_ptr<ControlSocket> ControlSocket::open(std::string const& path,
                                                       std::string& error) {
        sockaddr_un address = {};
        address.sun_family = AF_UNIX;
        if (path.empty() || path.size() >= sizeof(address.sun_path)) {
            error = "control socket path is empty or too long";
            return nullptr;
        }
        std::memcpy(address.sun_path, path.data(), path.size());

        int fd = socket(AF_UNIX, SOCK_STREAM, 0);
        if (fd == -1) {
            error = errno_message("socket");
            return nullptr;
        }
        // A socket file left by a previous run would make bind fail.
        (void) ::unlink(path.c_str());
        // Owner-only from creation: bind makes the file under the umask.
        // The umask is process-wide, so this is done once, at startup.
        mode_t old_mask = ::umask(0177);
        int bound = ::bind(fd, (sockaddr const*)&address, sizeof(address));
        ::umask(old_mask);
        if (bound == -1 || ::listen(fd, 4) == -1 || !set_nonblocking(fd)) {
            error = errno_message(bound == -1 ? "bind" : "listen");
            ::close(fd);
            return nullptr;
        }

        std::unique_ptr<ControlSocket> s(new ControlSocket);
        s->_path = path;
        s->_shared = std::make_shared<Shared>();
        s->_listen_fd = fd;
        return s;
    }

    Coroutine::Task ControlSocket::serve() {
        assert(!_served && (_listen_fd != -1));
        _served = true;
        // From here the accept loop owns the listening descriptor
        return serve_under(_shared, std::exchange(_listen_fd, -1), _stop, _stop_accepting);
    }

    bool ControlSocket::finished() {
        if (!_served)
            return true;
        std::scoped_lock lock{_shared->mutex};
        return _shared->finished;
    }

    ControlSocket::~ControlSocket() {
        assert(finished());
        if (_listen_fd != -1)
            ::close(_listen_fd);
        (void) ::unlink(_path.c_str());
    }

    Coroutine::Task ControlSocket::shutdown(std::chrono::milliseconds grace) {
        std::shared_ptr<Shared> shared = _shared;
        // No new connections
        _stop_accepting.request_stop();
        // Ask every connection to finish what is queued for it and close.
        // The drained signal is cleared under the same lock as the snapshot,
        // so the one awaited below is for this snapshot emptying
        std::vector<std::shared_ptr<OutQueue>> queues;
        {
            std::scoped_lock lock{shared->mutex};
            for (auto& [id, queue] : shared->connections)
                queues.push_back(queue);
            (void) shared->drained.try_take();
        }
        for (auto& queue : queues)
            queue->emplace(Outgoing{{}, true});
        if (!queues.empty())
            (void) co_await with_deadline(std::chrono::steady_clock::now() + grace,
                                          shared->drained.pop());
        // Then the rest, brutally
        close();
    }

    void ControlSocket::close() {
        // The cancellation unwinds frames parked on this thread, inside
        // request_stop; frames running on workers finish there, and the
        // spawner's join waits for them.  The accepting source follows
        // through the bridge in serve_under
        _stop.request_stop();
        (void) ::unlink(_path.c_str());
    }

    Coroutine::MultiProducerSingleConsumerQueue<ControlSocket::Event>& ControlSocket::events() {
        return _shared->events;
    }

    void ControlSocket::send(uint64_t connection, std::string_view line) {
        std::shared_ptr<OutQueue> queue;
        {
            std::scoped_lock lock{_shared->mutex};
            auto it = _shared->connections.find(connection);
            if (it == _shared->connections.end())
                return;
            queue = it->second;
        }
        std::string bytes(line);
        bytes.push_back('\n');
        // Outside the lock: emplace schedules the writer if it is parked.
        queue->emplace(Outgoing{std::move(bytes), false});
    }

    // ----------------------------------------------------------------
    // Tests.

    namespace {

        int connect_client(std::string const& path) {
            int fd = socket(AF_UNIX, SOCK_STREAM, 0);
            assert(fd != -1);
            sockaddr_un address = {};
            address.sun_family = AF_UNIX;
            std::memcpy(address.sun_path, path.data(), path.size());
            int connected = ::connect(fd, (sockaddr const*)&address, sizeof(address));
            assert(connected == 0);
            (void) connected;
            return fd;
        }

        // Read until `want` newline-terminated lines have arrived, or time
        // out (the caller asserts on the count).
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

        // Drains the event queue: lines into `lines`, closings into `closed`
        void drain(ControlSocket& server,
                   std::vector<ControlSocket::Line>& lines,
                   std::vector<uint64_t>& closed) {
            for (auto& event : server.events().try_take()) {
                if (auto* line = std::get_if<ControlSocket::Line>(&event))
                    lines.push_back(std::move(*line));
                else
                    closed.push_back(std::get<ControlSocket::Closed>(event).connection);
            }
        }

        template<typename F>
        bool eventually(F&& f) {
            auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            while (!f()) {
                if (std::chrono::steady_clock::now() >= deadline)
                    return false;
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            return true;
        }

    } // namespace

    define_test("control_socket_transport") {
        std::string path = "/tmp/wry-control-socket-test-" + std::to_string(::getpid()) + ".sock";
        std::string error;
        auto server = ControlSocket::open(path, error);
        assert(server && error.empty());
        // A scoped socket: served under this test's nursery
        Nursery nursery;
        nursery.soon(server->serve());

        struct stat st = {};
        const int stat_result = ::stat(path.c_str(), &st);
        assert(stat_result == 0 && (st.st_mode & 0777) == 0600);
        (void) stat_result;

        int client = connect_client(path);
        const char request[] = "first\nsec";
        const ssize_t w1 = ::write(client, request, sizeof(request) - 1);
        const ssize_t w2 = ::write(client, "ond\r\n", 5);
        assert(w1 == (ssize_t)(sizeof(request) - 1) && w2 == 5);
        (void) w1;
        (void) w2;

        // Lines arrive whole, split at newlines, CR stripped.
        std::vector<ControlSocket::Line> got;
        std::vector<uint64_t> closed;
        bool arrived = eventually([&] {
            drain(*server, got, closed);
            return got.size() >= 2;
        });
        assert(arrived && got.size() == 2 && closed.empty());
        (void) arrived;
        assert(got[0].bytes == "first" && got[1].bytes == "second");
        assert(got[0].connection == got[1].connection);

        // Replies go back to the connection they are addressed to, in order.
        server->send(got[0].connection, "reply one");
        server->send(got[0].connection, "reply two");
        auto lines = read_lines(client, 2);
        assert(lines.size() == 2 && lines[0] == "reply one" && lines[1] == "reply two");

        // A reply bigger than the socket buffer arrives whole: the writer
        // waits for writability while the client drains it.
        {
            std::string big(4 << 20, 'x');
            server->send(got[0].connection, big);
            auto big_lines = read_lines(client, 1);
            assert(big_lines.size() == 1 && big_lines[0].size() == big.size());
        }

        // Closing is reported, after the connection's last line; sends to
        // the dead connection are dropped.
        const ssize_t w_last = ::write(client, "last\n", 5);
        assert(w_last == 5);
        (void) w_last;
        ::close(client);
        bool reported = eventually([&] {
            drain(*server, got, closed);
            return !closed.empty();
        });
        assert(reported && closed.size() == 1 && closed[0] == got[0].connection);
        assert(got.size() == 3 && got[2].bytes == "last");
        (void) reported;
        server->send(got[0].connection, "nobody hears this");

        // Waiting for input: a take parks until a line arrives, and a take
        // under a deadline with nothing arriving is the nullopt
        {
            auto nothing = co_await with_deadline(std::chrono::steady_clock::now() + std::chrono::milliseconds(20),
                                                  server->events().take());
            assert(!nothing.has_value());
            int talker = connect_client(path);
            const ssize_t w_talk = ::write(talker, "knock\n", 6);
            assert(w_talk == 6);
            (void) w_talk;
            auto batch = co_await with_deadline(std::chrono::steady_clock::now() + std::chrono::seconds(5),
                                                server->events().take());
            assert(batch.has_value() && !batch->empty());
            auto* knock = std::get_if<ControlSocket::Line>(&batch->front());
            assert(knock && knock->bytes == "knock");
            ::close(talker);
            bool talker_closed = eventually([&] {
                drain(*server, got, closed);
                return closed.size() == 2;
            });
            assert(talker_closed && closed[1] == knock->connection);
            (void) talker_closed;
        }

        // Shutdown flushes what is queued, then closes: a second client's
        // queued reply arrives before its end of stream, and a third, idle
        // connection is closed too (cleanly if it was accepted, by reset if
        // the accept loop had not reached it).
        int second = connect_client(path);
        int third = connect_client(path);
        const ssize_t w3 = ::write(second, "hello\n", 6);
        assert(w3 == 6);
        (void) w3;
        std::vector<ControlSocket::Line> hello;
        bool greeted = eventually([&] {
            drain(*server, hello, closed);
            return !hello.empty();
        });
        assert(greeted && hello[0].bytes == "hello");
        (void) greeted;
        server->send(hello[0].connection, "goodbye");
        co_await server->shutdown(std::chrono::seconds(1));
        // shutdown does not wait for the loop; the join does, and the
        // destructor requires it
        (void) co_await nursery.join();
        server.reset();
        auto last = read_lines(second, 1);
        assert(last.size() == 1 && last[0] == "goodbye");
        char byte;
        const ssize_t after_goodbye = ::read(second, &byte, 1);
        const ssize_t idle = ::read(third, &byte, 1);
        assert(after_goodbye == 0);                 // then end of stream
        assert(idle <= 0);                          // closed, one way or the other
        (void) after_goodbye;
        (void) idle;
        ::close(second);
        ::close(third);
        assert(::access(path.c_str(), F_OK) != 0);   // unlinked

        // Brutal close: a connection with a reply queued behind a full
        // socket is cut off, the loop ends promptly, and nothing was waited
        // for
        {
            auto brutal = ControlSocket::open(path, error);
            assert(brutal && error.empty());
            Nursery scope;
            scope.soon(brutal->serve());
            int victim = connect_client(path);
            const ssize_t w4 = ::write(victim, "hi\n", 3);
            assert(w4 == 3);
            (void) w4;
            std::vector<ControlSocket::Line> hi;
            std::vector<uint64_t> gone;
            bool heard = eventually([&] {
                drain(*brutal, hi, gone);
                return !hi.empty();
            });
            assert(heard);
            (void) heard;
            brutal->send(hi[0].connection, std::string(4 << 20, 'y'));   // nobody reads it
            auto t0 = std::chrono::steady_clock::now();
            brutal->close();
            (void) co_await scope.join();
            assert(std::chrono::steady_clock::now() - t0 < std::chrono::seconds(1));
            assert(brutal->finished());
            brutal.reset();
            ::close(victim);
            assert(::access(path.c_str(), F_OK) != 0);
        }
        co_return;
    };

} // namespace wry
