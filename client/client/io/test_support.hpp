//
//  test_support.hpp
//  client
//
//  Created by Antony Searle on 2026-11-02.
//
//  Helpers for tests of the transports and of what runs over them, written
//  for a suite that shares the worker pool and the reactor with a live
//  game (core/main.mm spawns the suite beside the GUI, the offscreen and
//  headless hosts included).  The rules they embody:
//
//    - A test never blocks a pool worker.  A worker that sleeps, polls or
//      reads a blocking descriptor holds its mutator pin for the duration
//      (global_work_queue.cpp pins around each drain), which stalls the
//      collector for the whole process; and the pool is four wide.  So
//      every wait here is a co_await on the reactor: the test's own end of
//      a socket is driven through recv_some and send_some, and polling a
//      predicate sleeps on a reactor timer between polls.
//
//    - A deadline is a hang detector, not a performance claim.  Under a
//      sanitizer, beside a stepping world, the clock proves nothing about
//      promptness, so the deadlines here are generous (`patience`) and a
//      test that wants to say "promptly" asserts a structural fact
//      instead (the bytes were never drained, the table is empty).  A
//      wait with no deadline hangs the suite rather than failing it, so
//      there are none.
//
//    - A test owns every observable it reads and every path it touches.
//      The transport semantics are tested over a socket pair (adopt), with
//      no path on the filesystem at all; the one test of the Unix path
//      binding uses a name unique to the process.
//

#ifndef test_support_hpp
#define test_support_hpp

#include <cerrno>
#include <chrono>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "assert.hpp"
#include "coroutine.hpp"
#include "kqueue_reactor.hpp"
#include "mpsc_queue.hpp"
#include "transport.hpp"

namespace wry::testing {

    // How long a test waits for something that should happen promptly
    // before it gives up and fails: long enough that a loaded, sanitized
    // process never trips it, short enough that a hang is reported
    inline constexpr std::chrono::milliseconds patience{30000};

    inline std::chrono::steady_clock::time_point soon(std::chrono::milliseconds within = patience) {
        return std::chrono::steady_clock::now() + within;
    }

    // Poll `f` until it holds, sleeping on the reactor between polls, for
    // up to `within`.  False if it never held.  `f` may have side effects
    // (draining an inbox, stepping a clock)
    template<typename F>
    coroutine::Future<bool> eventually(F f, std::chrono::milliseconds within = patience) {
        const auto deadline = soon(within);
        while (!f()) {
            if (std::chrono::steady_clock::now() >= deadline)
                co_return false;
            co_await cancelable_sleep(0.002);
        }
        co_return true;
    }

    // Sleep on the reactor (never std::this_thread::sleep_for on a worker)
    inline coroutine::Future<> pause(std::chrono::milliseconds duration) {
        co_await cancelable_sleep((double)duration.count() / 1000.0);
    }

    inline bool make_nonblocking(int fd) {
        int flags = ::fcntl(fd, F_GETFL, 0);
        if (flags == -1)
            return false;
        int one = 1;
        (void) ::setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
        return ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) != -1;
    }

    // Both ends of a connected Unix stream pair, non-blocking with
    // SO_NOSIGPIPE: the server end for a transport to adopt, the client end
    // for the test to drive through the reactor.  Ends still held are
    // closed with the pair
    struct SocketPair {
        int fds[2] = {-1, -1};

        SocketPair() {
            const int paired = ::socketpair(AF_UNIX, SOCK_STREAM, 0, fds);
            assert(paired == 0);
            (void) paired;
            for (int fd : fds) {
                const bool ok = make_nonblocking(fd);
                assert(ok);
                (void) ok;
            }
        }

        ~SocketPair() {
            close_client();
            close_server();
        }

        SocketPair(SocketPair const&) = delete;
        SocketPair& operator=(SocketPair const&) = delete;

        // Hand the server end to Transport::adopt, which owns it from then
        int release_server() { return std::exchange(fds[0], -1); }
        int client() const { return fds[1]; }

        // The peer hangs up
        void close_client() {
            if (fds[1] != -1)
                ::close(std::exchange(fds[1], -1));
        }
        void close_server() {
            if (fds[0] != -1)
                ::close(std::exchange(fds[0], -1));
        }
    };

    // Send all of `bytes` through the reactor.  False if the deadline
    // passed first (the peer is not reading: its buffers are full) or the
    // send failed (the peer has gone)
    inline coroutine::Future<bool> send_all(int fd, std::string bytes,
                                            std::chrono::steady_clock::time_point deadline = soon()) {
        size_t sent = 0;
        while (sent < bytes.size()) {
            std::optional<ssize_t> n =
                co_await with_deadline(deadline, send_some(fd, bytes.data() + sent, bytes.size() - sent, 0));
            if (!n)
                co_return false;
            if (*n == -EAGAIN)
                continue;
            if (*n < 0)
                co_return false;
            sent += (size_t)*n;
        }
        co_return true;
    }

    // The test's end of a stream, read through the reactor a chunk at a
    // time, keeping any partial line between calls
    struct StreamReader {
        int fd;
        std::string pending;
        bool ended = false;         // the peer closed (recv returned 0)
        int error = 0;              // or failed, with this errno

        explicit StreamReader(int descriptor) : fd(descriptor) {}

        // One receive: the bytes appended to `pending` (0 is the end of
        // stream, recorded in `ended`), or nullopt at the deadline
        coroutine::Future<std::optional<ssize_t>> receive(std::chrono::steady_clock::time_point deadline = soon()) {
            char buffer[65536];
            for (;;) {
                std::optional<ssize_t> n = co_await with_deadline(deadline, recv_some(fd, buffer, sizeof buffer, 0));
                if (!n)
                    co_return std::nullopt;
                if (*n == -EAGAIN)
                    continue;
                if (*n < 0) {
                    error = (int)-*n;
                    ended = true;
                    co_return *n;
                }
                if (*n == 0)
                    ended = true;
                else
                    pending.append(buffer, (size_t)*n);
                co_return *n;
            }
        }

        // Up to `want` newline-terminated lines (CR not stripped: that is
        // the codec's job, and this is the peer), fewer if the stream
        // ended or the deadline passed; the caller asserts on the count
        coroutine::Future<std::vector<std::string>> lines(size_t want,
                                                          std::chrono::steady_clock::time_point deadline = soon()) {
            std::vector<std::string> result;
            auto split = [&] {
                for (size_t nl; (result.size() < want) && ((nl = pending.find('\n')) != std::string::npos); ) {
                    result.push_back(pending.substr(0, nl));
                    pending.erase(0, nl + 1);
                }
            };
            split();
            while ((result.size() < want) && !ended) {
                std::optional<ssize_t> n = co_await receive(deadline);
                if (!n)
                    break;
                split();
            }
            co_return result;
        }

        // Up to `want` bytes, as for lines
        coroutine::Future<std::string> bytes(size_t want,
                                             std::chrono::steady_clock::time_point deadline = soon()) {
            while ((pending.size() < want) && !ended) {
                std::optional<ssize_t> n = co_await receive(deadline);
                if (!n)
                    break;
            }
            const size_t take = std::min(want, pending.size());
            std::string result = pending.substr(0, take);
            pending.erase(0, take);
            co_return result;
        }

        // Wait for the peer to close, with nothing more arriving first
        // (anything that does is left in `pending`).  False at the deadline
        coroutine::Future<bool> end_of_stream(std::chrono::steady_clock::time_point deadline = soon()) {
            while (!ended) {
                std::optional<ssize_t> n = co_await receive(deadline);
                if (!n)
                    co_return false;
            }
            co_return error == 0;
        }
    };

    // Connect to a Unix socket path through the reactor: the connected,
    // non-blocking descriptor, or -errno
    inline coroutine::Future<int> connect_unix(std::string path) {
        sockaddr_un address = {};
        address.sun_family = AF_UNIX;
        if (path.size() >= sizeof(address.sun_path))
            co_return -ENAMETOOLONG;
        std::memcpy(address.sun_path, path.data(), path.size());
        int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
        if (fd == -1)
            co_return -errno;
        if (!make_nonblocking(fd)) {
            int error = errno;
            ::close(fd);
            co_return -error;
        }
        int connected = co_await connect_to(fd, (sockaddr const*)&address, sizeof(address));
        if (connected < 0) {
            ::close(fd);
            co_return connected;
        }
        co_return fd;
    }

    // A socket path unique to this process, for the one test that binds one
    inline std::string socket_path(const char* name) {
        return std::string("/tmp/wry-") + name + '-' + std::to_string(::getpid()) + ".sock";
    }

    // Take what is in a transport's inbox: messages, closings and, if
    // asked for, openings.  Checks the arrival-order contract within the
    // batch as it goes: a connection's Opened never follows one of its
    // messages.  (Within the batch only: a test may pour two transports'
    // inboxes, whose connection ids both count from one, into one vector)
    inline void drain(coroutine::MultiProducerSingleConsumerQueue<Transport::Event>& events,
                      std::vector<Transport::Message>& messages,
                      std::vector<Transport::Closed>& closed,
                      std::vector<Transport::Opened>* opened = nullptr) {
        const size_t first_of_batch = messages.size();
        for (auto& event : events.try_take()) {
            if (auto* message = std::get_if<Transport::Message>(&event)) {
                messages.push_back(std::move(*message));
            } else if (auto* open = std::get_if<Transport::Opened>(&event)) {
                for (size_t i = first_of_batch; i != messages.size(); ++i)
                    assert(messages[i].connection != open->connection);
                if (opened)
                    opened->push_back(std::move(*open));
            } else if (auto* close = std::get_if<Transport::Closed>(&event)) {
                closed.push_back(std::move(*close));
            }
        }
    }

} // namespace wry::testing

#endif /* test_support_hpp */
