//
//  control_socket.cpp
//  client
//
//  Created by Antony Searle on 2026-10-12.
//

#include <cerrno>
#include <chrono>
#include <cstring>
#include <deque>
#include <string>
#include <thread>
#include <variant>
#include <vector>

#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include "assert.hpp"
#include "control_socket.hpp"
#include "coroutine.hpp"
#include "kqueue_reactor.hpp"
#include "nursery.hpp"
#include "test.hpp"

namespace wry {

    using coroutine::Nursery;

    namespace {

        // The line codec with the control socket's farewell: one reply
        // line, in the shape of every other error reply, with no id since
        // it answers no request
        struct ControlCodec final : Transport::LineCodec {
            ControlCodec() : LineCodec(ControlSocket::max_line) {}
            std::string farewell(std::string_view reason) const override {
                std::string line = "{\"status\": \"error\", \"text\": \"kicked: ";
                for (char c : reason) {
                    if (c == '"' || c == '\\')
                        line.push_back('\\');
                    if ((unsigned char)c < 0x20)
                        c = ' ';
                    line.push_back(c);
                }
                line += "\"}";
                return line;
            }
        };

        ControlCodec const& control_codec() {
            static ControlCodec const codec;
            return codec;
        }

        constexpr Transport::Limits control_limits() {
            Transport::Limits limits;
            limits.max_connections = 16;
            limits.max_unconsumed = 256;
            limits.max_queued_bytes = 16 << 20;
            limits.kick_grace = std::chrono::milliseconds(1000);
            return limits;
        }

    } // namespace

    std::unique_ptr<ControlSocket> ControlSocket::open(std::string const& path,
                                                       std::string& error) {
        std::unique_ptr<Transport> transport =
            Transport::listen_unix(path, control_codec(), control_limits(), error);
        if (!transport)
            return nullptr;
        std::unique_ptr<ControlSocket> s(new ControlSocket);
        s->_transport = std::move(transport);
        return s;
    }

    coroutine::Task ControlSocket::serve() {
        return _transport->serve();
    }

    coroutine::Task ControlSocket::shutdown(std::chrono::milliseconds grace) {
        return _transport->shutdown(grace);
    }

    void ControlSocket::close() {
        _transport->close();
    }

    bool ControlSocket::finished() {
        return _transport->finished();
    }

    coroutine::MultiProducerSingleConsumerQueue<ControlSocket::Event>& ControlSocket::events() {
        return _transport->events();
    }

    void ControlSocket::send(uint64_t connection, std::string_view line) {
        _transport->send(connection, line);
    }

    void ControlSocket::kick(uint64_t connection, std::string_view reason) {
        _transport->kick(connection, reason);
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

        // Drains the event queue: lines into `lines`, closings into
        // `closed`, and openings into `opened`, whose order against the
        // lines is checked by the caller
        void drain(ControlSocket& server,
                   std::vector<ControlSocket::Line>& lines,
                   std::vector<ControlSocket::Closed>& closed,
                   std::vector<ControlSocket::Opened>* opened = nullptr) {
            for (auto& event : server.events().try_take()) {
                if (auto* line = std::get_if<ControlSocket::Line>(&event))
                    lines.push_back(std::move(*line));
                else if (auto* open = std::get_if<ControlSocket::Opened>(&event)) {
                    assert(lines.empty() || (lines.back().connection != open->connection));
                    if (opened)
                        opened->push_back(std::move(*open));
                } else if (auto* close = std::get_if<ControlSocket::Closed>(&event))
                    closed.push_back(std::move(*close));
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

        // Lines arrive whole, split at newlines, CR stripped; the
        // connection's Opened came first, labelled with this process
        std::vector<ControlSocket::Line> got;
        std::vector<ControlSocket::Closed> closed;
        std::vector<ControlSocket::Opened> opened;
        bool arrived = eventually([&] {
            drain(*server, got, closed, &opened);
            return got.size() >= 2;
        });
        assert(arrived && got.size() == 2 && closed.empty());
        assert(opened.size() == 1 && opened[0].connection == got[0].connection);
        assert(opened[0].peer == "client " + std::to_string(::getpid()));
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
        assert(reported && closed.size() == 1 && closed[0].connection == got[0].connection);
        assert(closed[0].reason == "end of stream");
        assert(got.size() == 3 && got[2].bytes == "last");
        (void) reported;
        server->send(got[0].connection, "nobody hears this");

        // A kick answers with one error line, then the end of stream
        {
            int kicked = connect_client(path);
            const ssize_t w_k = ::write(kicked, "misbehave\n", 10);
            assert(w_k == 10);
            (void) w_k;
            std::vector<ControlSocket::Line> misbehaved;
            bool heard = eventually([&] {
                drain(*server, misbehaved, closed);
                return !misbehaved.empty();
            });
            assert(heard && misbehaved[0].bytes == "misbehave");
            (void) heard;
            server->kick(misbehaved[0].connection, "a \"quoted\" reason");
            auto farewell = read_lines(kicked, 1);
            assert(farewell.size() == 1);
            assert(farewell[0] == "{\"status\": \"error\", \"text\": \"kicked: a \\\"quoted\\\" reason\"}");
            char byte;
            const ssize_t after = ::read(kicked, &byte, 1);
            assert(after == 0);
            (void) after;
            bool gone = eventually([&] {
                drain(*server, misbehaved, closed);
                return closed.size() == 2;
            });
            assert(gone && closed[1].reason == "kicked: a \"quoted\" reason");
            (void) gone;
            ::close(kicked);
        }

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
            // The batch opens with the talker's Opened; its line follows,
            // in this batch or a later one
            ControlSocket::Line const* knock = nullptr;
            std::deque<ControlSocket::Event> events = std::move(*batch);
            bool knocked = eventually([&] {
                for (auto& event : events)
                    if (auto* line = std::get_if<ControlSocket::Line>(&event))
                        knock = line;
                if (knock)
                    return true;
                for (auto& event : server->events().try_take())
                    events.push_back(std::move(event));
                return false;
            });
            assert(knocked && knock && knock->bytes == "knock");
            (void) knocked;
            const uint64_t talker_id = knock->connection;
            ::close(talker);
            bool talker_closed = eventually([&] {
                drain(*server, got, closed);
                return closed.size() == 3;
            });
            assert(talker_closed && closed[2].connection == talker_id);
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
            std::vector<ControlSocket::Closed> gone;
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
