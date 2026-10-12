//
//  control_socket.cpp
//  client
//
//  Created by Antony Searle on 2026-10-12.
//

#include <cerrno>
#include <chrono>
#include <cstring>

#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include "assert.hpp"
#include "control_socket.hpp"
#include "test.hpp"

namespace wry {

    namespace {

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

        std::unique_ptr<ControlSocket> s(new ControlSocket);
        s->_path = path;
        s->_listen_fd = socket(AF_UNIX, SOCK_STREAM, 0);
        if (s->_listen_fd == -1) {
            error = errno_message("socket");
            return nullptr;
        }
        // A socket file left by a previous run would make bind fail.
        (void) ::unlink(path.c_str());
        // Owner-only from creation: bind makes the file under the umask.
        // The umask is process-wide, so this is done once, at startup.
        mode_t old_mask = ::umask(0177);
        int bound = ::bind(s->_listen_fd, (sockaddr const*)&address, sizeof(address));
        ::umask(old_mask);
        if (bound == -1) {
            error = errno_message("bind");
            return nullptr;
        }
        if (::listen(s->_listen_fd, 4) == -1 || !set_nonblocking(s->_listen_fd)) {
            error = errno_message("listen");
            return nullptr;
        }
        int pipe_fds[2];
        if (::pipe(pipe_fds) == -1) {
            error = errno_message("pipe");
            return nullptr;
        }
        s->_wake_read = pipe_fds[0];
        s->_wake_write = pipe_fds[1];
        (void) set_nonblocking(s->_wake_read);
        (void) set_nonblocking(s->_wake_write);
        s->_thread = std::thread([p = s.get()] { p->_run(); });
        return s;
    }

    ControlSocket::~ControlSocket() {
        if (_thread.joinable()) {
            {
                std::scoped_lock lock{_mutex};
                _stopping = true;
            }
            _wake();
            _thread.join();
        }
        for (auto& [id, c] : _connections)
            ::close(c.fd);
        for (int fd : {_listen_fd, _wake_read, _wake_write})
            if (fd != -1)
                ::close(fd);
        if (_listen_fd != -1)
            (void) ::unlink(_path.c_str());
    }

    void ControlSocket::_wake() {
        char byte = 0;
        // Full pipe: a wake is already pending, which is all we need.
        (void) ::write(_wake_write, &byte, 1);
    }

    std::vector<ControlSocket::Line> ControlSocket::take_lines() {
        std::scoped_lock lock{_mutex};
        return std::exchange(_inbox, {});
    }

    std::vector<uint64_t> ControlSocket::take_closed() {
        std::scoped_lock lock{_mutex};
        return std::exchange(_closed, {});
    }

    void ControlSocket::send(uint64_t connection, std::string_view line) {
        {
            std::scoped_lock lock{_mutex};
            auto it = _connections.find(connection);
            if (it == _connections.end())
                return;
            it->second.out.append(line);
            it->second.out.push_back('\n');
        }
        _wake();
    }

    // The thread.  Each pass builds the poll set under the lock, polls
    // without it, then does the I/O the poll reported, taking the lock
    // only to touch the shared buffers.  Descriptors are owned by this
    // thread alone; the main thread only touches the buffers.
    void ControlSocket::_run() {
        std::vector<pollfd> fds;
        std::vector<uint64_t> ids;      // parallel to fds[2...]
        char buffer[65536];
        for (;;) {
            fds.clear();
            ids.clear();
            fds.push_back({_wake_read, POLLIN, 0});
            {
                std::scoped_lock lock{_mutex};
                if (_stopping)
                    return;
                fds.push_back({_listen_fd,
                               (short)(_connections.size() < max_connections ? POLLIN : 0), 0});
                for (auto& [id, c] : _connections) {
                    fds.push_back({c.fd, (short)(POLLIN | (c.out.empty() ? 0 : POLLOUT)), 0});
                    ids.push_back(id);
                }
            }
            if (::poll(fds.data(), (nfds_t)fds.size(), -1) == -1) {
                if (errno == EINTR)
                    continue;
                return;
            }
            if (fds[0].revents & POLLIN)
                while (::read(_wake_read, buffer, sizeof(buffer)) > 0) {}

            if (fds[1].revents & POLLIN) {
                int fd = ::accept(_listen_fd, nullptr, nullptr);
                if (fd != -1) {
                    int one = 1;
                    (void) ::setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
                    if (set_nonblocking(fd)) {
                        std::scoped_lock lock{_mutex};
                        _connections[_next_connection++].fd = fd;
                    } else {
                        ::close(fd);
                    }
                }
            }

            for (size_t k = 0; k != ids.size(); ++k) {
                pollfd const& p = fds[k + 2];
                const uint64_t id = ids[k];
                bool dead = (p.revents & (POLLERR | POLLNVAL)) != 0;

                if (!dead && (p.revents & (POLLIN | POLLHUP))) {
                    ssize_t n = ::read(p.fd, buffer, sizeof(buffer));
                    if (n > 0) {
                        std::scoped_lock lock{_mutex};
                        Connection& c = _connections[id];
                        c.in.append(buffer, (size_t)n);
                        size_t start = 0;
                        for (size_t nl; (nl = c.in.find('\n', start)) != std::string::npos; start = nl + 1) {
                            std::string line = c.in.substr(start, nl - start);
                            if (!line.empty() && line.back() == '\r')
                                line.pop_back();
                            _inbox.push_back(Line{id, std::move(line)});
                        }
                        c.in.erase(0, start);
                        if (c.in.size() > max_line)
                            dead = true;
                    } else if (n == 0 || (errno != EAGAIN && errno != EINTR)) {
                        dead = true;
                    }
                }

                if (!dead && (p.revents & POLLOUT)) {
                    std::scoped_lock lock{_mutex};
                    Connection& c = _connections[id];
                    ssize_t n = ::write(p.fd, c.out.data(), c.out.size());
                    if (n > 0)
                        c.out.erase(0, (size_t)n);
                    else if (n == -1 && errno != EAGAIN && errno != EINTR)
                        dead = true;
                }

                if (dead) {
                    std::scoped_lock lock{_mutex};
                    ::close(p.fd);
                    _connections.erase(id);
                    _closed.push_back(id);
                }
            }
        }
    }

    // ----------------------------------------------------------------
    // Tests.

    namespace {

        int connect_to(std::string const& path) {
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

        int client = connect_to(path);
        const char request[] = "first\nsec";
        const ssize_t w1 = ::write(client, request, sizeof(request) - 1);
        const ssize_t w2 = ::write(client, "ond\r\n", 5);
        assert(w1 == (ssize_t)(sizeof(request) - 1) && w2 == 5);
        (void) w1;
        (void) w2;

        // Lines arrive whole, split at newlines, CR stripped.
        std::vector<ControlSocket::Line> got;
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (got.size() < 2 && std::chrono::steady_clock::now() < deadline) {
            for (auto& line : server->take_lines())
                got.push_back(std::move(line));
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        assert(got.size() == 2);
        assert(got[0].bytes == "first" && got[1].bytes == "second");
        assert(got[0].connection == got[1].connection);

        // Replies go back to the connection they are addressed to.
        server->send(got[0].connection, "reply one");
        server->send(got[0].connection, "reply two");
        auto lines = read_lines(client, 2);
        assert(lines.size() == 2 && lines[0] == "reply one" && lines[1] == "reply two");

        // Closing is reported; sends to the dead connection are dropped.
        ::close(client);
        std::vector<uint64_t> closed;
        deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (closed.empty() && std::chrono::steady_clock::now() < deadline) {
            closed = server->take_closed();
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        assert(closed.size() == 1 && closed[0] == got[0].connection);
        server->send(got[0].connection, "nobody hears this");

        server.reset();
        assert(::access(path.c_str(), F_OK) != 0);   // unlinked
        co_return;
    };

} // namespace wry
