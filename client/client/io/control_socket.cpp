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
        std::condition_variable input;          // notified on inbox / closed
        std::condition_variable changed;        // notified on connections / finished
        uint64_t next_connection = 1;
        std::map<uint64_t, std::shared_ptr<OutQueue>> connections;
        std::vector<Line> inbox;
        std::vector<uint64_t> closed;
        bool finished = false;                  // the accept loop has ended
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
                {
                    std::scoped_lock lock{shared->mutex};
                    shared->connections.erase(id);
                    shared->closed.push_back(id);
                }
                shared->input.notify_all();
                shared->changed.notify_all();
            }
        };

        // Split what has arrived into lines for the main thread; false if
        // the partial line has grown too long.
        bool deliver_lines(Shared& shared, uint64_t id, std::string& pending) {
            size_t start = 0;
            bool delivered = false;
            {
                std::scoped_lock lock{shared.mutex};
                for (size_t nl; (nl = pending.find('\n', start)) != std::string::npos; start = nl + 1) {
                    std::string line = pending.substr(start, nl - start);
                    if (!line.empty() && line.back() == '\r')
                        line.pop_back();
                    shared.inbox.push_back(ControlSocket::Line{id, std::move(line)});
                    delivered = true;
                }
            }
            if (delivered)
                shared.input.notify_all();
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

        // Accepts connections until stopped, then joins them: the stop is
        // absorbed at the accept (stopped_as_optional), so this frame lives
        // on to join its nursery, which unwinds every connection first.
        Future<> accept_connections(std::shared_ptr<Shared> shared, int listen_fd) {
            Descriptor listener{listen_fd};
            Nursery connections{co_await GetStopToken{}};
            for (;;) {
                std::optional<int> accepted =
                    co_await Coroutine::stopped_as_optional(accept_one(listen_fd, nullptr, nullptr));
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

        // The root: runs the accept loop under the ControlSocket's own stop
        // source (shield absorbs the stop at this boundary), then reports
        // that everything has finished.
        Coroutine::Task serve(std::shared_ptr<Shared> shared, int listen_fd, std::stop_token stop) {
            (void) co_await Coroutine::shield(accept_connections(shared, listen_fd), std::move(stop));
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
        // From here the accept loop owns the listening descriptor.
        wait_group_spawn(serve(s->_shared, fd, s->_stop.get_token()));
        return s;
    }

    ControlSocket::~ControlSocket() {
        Shared& shared = *_shared;
        // Ask every connection to finish what is queued for it and close,
        // and give them a second to do so.
        std::vector<std::shared_ptr<OutQueue>> queues;
        {
            std::scoped_lock lock{shared.mutex};
            for (auto& [id, queue] : shared.connections)
                queues.push_back(queue);
        }
        for (auto& queue : queues)
            queue->emplace(Outgoing{{}, true});
        {
            std::unique_lock lock{shared.mutex};
            shared.changed.wait_for(lock, std::chrono::seconds(1), [&] {
                return shared.connections.empty();
            });
        }
        // Cancel what remains, then wait for every frame to finish.  The
        // cancellation unwinds frames parked on this thread, inside
        // request_stop; frames running on workers finish there.
        _stop.request_stop();
        {
            std::unique_lock lock{shared.mutex};
            shared.changed.wait(lock, [&] { return shared.finished; });
        }
        (void) ::unlink(_path.c_str());
    }

    std::vector<ControlSocket::Line> ControlSocket::take_lines() {
        std::scoped_lock lock{_shared->mutex};
        return std::exchange(_shared->inbox, {});
    }

    std::vector<uint64_t> ControlSocket::take_closed() {
        std::scoped_lock lock{_shared->mutex};
        return std::exchange(_shared->closed, {});
    }

    bool ControlSocket::has_input() {
        std::scoped_lock lock{_shared->mutex};
        return !_shared->inbox.empty() || !_shared->closed.empty();
    }

    void ControlSocket::wait_for_input(std::chrono::milliseconds timeout) {
        std::unique_lock lock{_shared->mutex};
        _shared->input.wait_for(lock, timeout, [this] {
            return !_shared->inbox.empty() || !_shared->closed.empty();
        });
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
        bool arrived = eventually([&] {
            for (auto& line : server->take_lines())
                got.push_back(std::move(line));
            return got.size() >= 2;
        });
        assert(arrived && got.size() == 2);
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

        // Closing is reported; sends to the dead connection are dropped.
        ::close(client);
        std::vector<uint64_t> closed;
        bool reported = eventually([&] {
            closed = server->take_closed();
            return !closed.empty();
        });
        assert(reported && closed.size() == 1 && closed[0] == got[0].connection);
        (void) reported;
        server->send(got[0].connection, "nobody hears this");

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
            for (auto& line : server->take_lines())
                hello.push_back(std::move(line));
            return !hello.empty();
        });
        assert(greeted && hello[0].bytes == "hello");
        (void) greeted;
        server->send(hello[0].connection, "goodbye");
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
        co_return;
    };

} // namespace wry
