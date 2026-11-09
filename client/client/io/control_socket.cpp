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
#include <variant>
#include <vector>

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
#include "test_support.hpp"

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

    std::unique_ptr<ControlSocket> ControlSocket::make() {
        std::unique_ptr<ControlSocket> s(new ControlSocket);
        s->_transport = Transport::make(control_codec(), control_limits());
        return s;
    }

    int ControlSocket::adopt(int fd) {
        return _transport->adopt(fd);
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
    // Tests (io/test_support.hpp for the rules).  The codec alone; the
    // transport semantics over a socket pair, the test driving its end
    // through the reactor; and the Unix path binding in a test of its
    // own, the only one that touches the filesystem.

    namespace {

        using testing::drain;
        using testing::eventually;
        using testing::send_all;
        using testing::soon;
        using testing::SocketPair;
        using testing::StreamReader;

        std::string this_pid() {
            return std::to_string(::getpid());
        }

    } // namespace

    define_test("control_socket_codec") {
        Transport::Codec const& codec = control_codec();

        // Lines arrive whole, split at newlines, CR stripped, a partial
        // line held for the next receive
        ContiguousDeque<char> pending;
        for (char c : std::string_view("first\nsec"))
            pending.push_back(c);
        std::vector<std::string> out;
        CHECK(codec.split(pending, out).empty());
        assert(out.size() == 1 && out[0] == "first");
        for (char c : std::string_view("ond\r\n"))
            pending.push_back(c);
        CHECK(codec.split(pending, out).empty());
        assert(out.size() == 2 && out[1] == "second");
        assert(pending.empty());

        // A reply is its line and a newline
        assert(codec.frame("reply") == "reply\n");

        // The farewell is one error reply with no id, the reason escaped
        // for JSON: quotes and backslashes escaped, control characters
        // blanked
        assert(codec.farewell("a \"quoted\" reason")
               == "{\"status\": \"error\", \"text\": \"kicked: a \\\"quoted\\\" reason\"}");
        assert(codec.farewell("back\\slash\tand\nnewline")
               == "{\"status\": \"error\", \"text\": \"kicked: back\\\\slash and newline\"}");
        co_return;
    };

    define_test("control_socket_transport") {
        auto server = ControlSocket::make();
        // A scoped socket: served under this test's nursery
        Nursery nursery;
        nursery.soon(server->serve());

        SocketPair pair;
        const int client_id = server->adopt(pair.release_server());
        assert(client_id > 0);
        StreamReader client{pair.client()};

        bool sent = co_await send_all(pair.client(), "first\nsec");
        assert(sent);
        sent = co_await send_all(pair.client(), "ond\r\n");
        assert(sent);

        // Lines arrive whole, split at newlines, CR stripped; the
        // connection's Opened came first, labelled with its peer, which
        // over a socket pair is this process
        std::vector<ControlSocket::Line> got;
        std::vector<ControlSocket::Closed> closed;
        std::vector<ControlSocket::Opened> opened;
        bool arrived = co_await eventually([&] {
            drain(server->events(), got, closed, &opened);
            return got.size() >= 2;
        });
        assert(arrived && got.size() == 2 && closed.empty());
        assert(opened.size() == 1 && opened[0].connection == (uint64_t)client_id);
        assert(opened[0].peer.ends_with(" " + this_pid()));
        assert(got[0].bytes == "first" && got[1].bytes == "second");
        assert(got[0].connection == (uint64_t)client_id && got[1].connection == (uint64_t)client_id);

        // Replies go back to the connection they are addressed to, in order
        server->send((uint64_t)client_id, "reply one");
        server->send((uint64_t)client_id, "reply two");
        auto lines = co_await client.lines(2);
        assert(lines.size() == 2 && lines[0] == "reply one" && lines[1] == "reply two");

        // A reply far bigger than the socket buffers arrives whole: the
        // writer waits for writability while the client drains it
        {
            std::string big(4 << 20, 'x');
            server->send((uint64_t)client_id, big);
            auto big_lines = co_await client.lines(1);
            assert(big_lines.size() == 1 && big_lines[0] == big);
        }

        // Closing is reported, after the connection's last line; sends to
        // the dead connection are dropped
        sent = co_await send_all(pair.client(), "last\n");
        assert(sent);
        pair.close_client();
        bool reported = co_await eventually([&] {
            drain(server->events(), got, closed);
            return !closed.empty();
        });
        assert(reported && closed.size() == 1 && closed[0].connection == (uint64_t)client_id);
        assert(closed[0].reason == "end of stream");
        assert(got.size() == 3 && got[2].bytes == "last");
        server->send((uint64_t)client_id, "nobody hears this");

        // A kick answers with one error line, then the end of stream
        {
            SocketPair misbehaving;
            const int kicked_id = server->adopt(misbehaving.release_server());
            assert(kicked_id > 0);
            StreamReader kicked{misbehaving.client()};
            sent = co_await send_all(misbehaving.client(), "misbehave\n");
            assert(sent);
            std::vector<ControlSocket::Line> misbehaved;
            bool heard = co_await eventually([&] {
                drain(server->events(), misbehaved, closed);
                return !misbehaved.empty();
            });
            assert(heard && misbehaved[0].bytes == "misbehave");
            assert(misbehaved[0].connection == (uint64_t)kicked_id);
            server->kick((uint64_t)kicked_id, "a \"quoted\" reason");
            auto farewell = co_await kicked.lines(1);
            assert(farewell.size() == 1);
            assert(farewell[0] == "{\"status\": \"error\", \"text\": \"kicked: a \\\"quoted\\\" reason\"}");
            bool ended = co_await kicked.end_of_stream();
            assert(ended && kicked.pending.empty());
            bool gone = co_await eventually([&] {
                drain(server->events(), misbehaved, closed);
                return closed.size() == 2;
            });
            assert(gone && closed[1].connection == (uint64_t)kicked_id);
            assert(closed[1].reason == "kicked: a \"quoted\" reason");
        }

        // Waiting for input: a take under a deadline with nothing arriving
        // is the nullopt (nothing else can produce into this inbox), and a
        // take parks until something does
        {
            auto nothing = co_await with_deadline(soon(std::chrono::milliseconds(20)),
                                                  server->events().take());
            assert(!nothing.has_value());
            SocketPair talking;
            const int talker_id = server->adopt(talking.release_server());
            assert(talker_id > 0);
            sent = co_await send_all(talking.client(), "knock\n");
            assert(sent);
            auto batch = co_await with_deadline(soon(), server->events().take());
            assert(batch.has_value() && !batch->empty());
            // The batch opens with the talker's Opened; its line follows,
            // in this batch or a later one
            assert(std::holds_alternative<ControlSocket::Opened>(batch->front()));
            assert(std::get<ControlSocket::Opened>(batch->front()).connection == (uint64_t)talker_id);
            std::deque<ControlSocket::Event> events = std::move(*batch);
            ControlSocket::Line const* knock = nullptr;
            bool knocked = co_await eventually([&] {
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
            assert(knock->connection == (uint64_t)talker_id);
            talking.close_client();
            bool talker_closed = co_await eventually([&] {
                drain(server->events(), got, closed);
                return closed.size() == 3;
            });
            assert(talker_closed && closed[2].connection == (uint64_t)talker_id);
        }

        // Shutdown flushes what is queued, then closes: a client's queued
        // reply arrives before its end of stream, and an idle connection
        // is closed cleanly too
        SocketPair second_pair;
        const int second_id = server->adopt(second_pair.release_server());
        SocketPair idle_pair;
        const int idle_id = server->adopt(idle_pair.release_server());
        assert(second_id > 0 && idle_id > 0);
        StreamReader second{second_pair.client()};
        StreamReader idle{idle_pair.client()};
        sent = co_await send_all(second_pair.client(), "hello\n");
        assert(sent);
        std::vector<ControlSocket::Line> hello;
        bool greeted = co_await eventually([&] {
            drain(server->events(), hello, closed);
            return !hello.empty();
        });
        assert(greeted && hello[0].bytes == "hello" && hello[0].connection == (uint64_t)second_id);
        server->send((uint64_t)second_id, "goodbye");
        co_await server->shutdown(std::chrono::seconds(1));
        // shutdown does not wait for the loop; the join does, and the
        // destructor requires it
        (void) co_await nursery.join();
        assert(server->finished());
        server.reset();
        auto last = co_await second.lines(1);
        assert(last.size() == 1 && last[0] == "goodbye");
        bool after_goodbye = co_await second.end_of_stream();
        assert(after_goodbye && second.pending.empty());
        bool idle_ended = co_await idle.end_of_stream();
        assert(idle_ended && idle.pending.empty());

        // Brutal close: a connection with a reply queued behind full socket
        // buffers is cut off and the loop ends without waiting for the
        // reply to drain.  The structural fact, not the clock: the victim
        // reads what reached the kernel, which is nothing like the reply,
        // then the end of stream
        {
            auto brutal = ControlSocket::make();
            Nursery scope;
            scope.soon(brutal->serve());
            SocketPair victim_pair;
            const int victim_id = brutal->adopt(victim_pair.release_server());
            assert(victim_id > 0);
            StreamReader victim{victim_pair.client()};
            sent = co_await send_all(victim_pair.client(), "hi\n");
            assert(sent);
            std::vector<ControlSocket::Line> hi;
            std::vector<ControlSocket::Closed> gone;
            bool heard = co_await eventually([&] {
                drain(brutal->events(), hi, gone);
                return !hi.empty();
            });
            assert(heard);
            const std::string reply(4 << 20, 'y');
            brutal->send((uint64_t)victim_id, reply);       // nobody reads it
            brutal->close();
            (void) co_await scope.join();
            assert(brutal->finished());
            brutal.reset();
            (void) co_await victim.end_of_stream();
            assert(victim.ended);
            assert(victim.pending.size() < reply.size());
        }
        co_return;
    };

    define_test("control_socket_path") {
        // The Unix binding (Transport::listen_unix): the file appears
        // owner-only and listening, a connector is served and labelled, a
        // second opener finds the path live and leaves it alone, a dead
        // socket file is replaced, and the path is unlinked at close
        // unless a successor has taken it
        const std::string path = testing::socket_path("control-socket");
        std::string error;
        auto server = ControlSocket::open(path, error);
        assert(server && error.empty());
        assert(server->path() == path);
        struct stat st = {};
        const int stat_result = ::stat(path.c_str(), &st);
        assert(stat_result == 0 && S_ISSOCK(st.st_mode) && (st.st_mode & 0777) == 0600);
        (void) stat_result;
        assert(::access((path + ".tmp").c_str(), F_OK) != 0);    // nothing left beside it
        Nursery nursery;
        nursery.soon(server->serve());

        // A connector through the path, served and labelled with its
        // process (this one: its command line's first word, and its pid)
        const int client_fd = co_await testing::connect_unix(path);
        assert(client_fd >= 0);
        StreamReader client{client_fd};
        bool sent = co_await send_all(client_fd, "ping\n");
        assert(sent);
        std::vector<ControlSocket::Line> got;
        std::vector<ControlSocket::Closed> closed;
        std::vector<ControlSocket::Opened> opened;
        bool pinged = co_await eventually([&] {
            drain(server->events(), got, closed, &opened);
            return !got.empty();
        });
        assert(pinged && got[0].bytes == "ping");
        assert(opened.size() == 1 && opened[0].connection == got[0].connection);
        assert(opened[0].peer.ends_with(" " + this_pid()) && opened[0].peer.size() > this_pid().size() + 1);
        server->send(got[0].connection, "pong");
        auto pong = co_await client.lines(1);
        assert(pong.size() == 1 && pong[0] == "pong");

        // A second opener is refused while we are live, and leaves our file
        // in place.  Its probe is a connection that opens and closes at
        // once, which the server sees
        {
            std::string refused_error;
            auto refused = ControlSocket::open(path, refused_error);
            assert(!refused);
            assert(refused_error.find("another process is listening") != std::string::npos);
            assert(::access(path.c_str(), F_OK) == 0);
            bool probed = co_await eventually([&] {
                drain(server->events(), got, closed, &opened);
                return opened.size() == 2 && closed.size() == 1;
            });
            assert(probed && closed[0].connection == opened[1].connection);
            assert(closed[0].reason == "end of stream");
            assert(got.size() == 1);
        }

        // Close: the client sees the end of stream and the path is gone
        co_await server->shutdown(std::chrono::seconds(1));
        (void) co_await nursery.join();
        server.reset();
        bool ended = co_await client.end_of_stream();
        assert(ended);
        ::close(client_fd);
        assert(::access(path.c_str(), F_OK) != 0);

        // A dead socket file (a process that died without unlinking) is
        // replaced; a regular file is not
        {
            int dead = ::socket(AF_UNIX, SOCK_STREAM, 0);
            sockaddr_un address = {};
            address.sun_family = AF_UNIX;
            std::memcpy(address.sun_path, path.data(), path.size());
            const int bound = ::bind(dead, (sockaddr const*)&address, sizeof(address));
            assert(bound == 0);
            (void) bound;
            ::close(dead);                                  // the file stays
            assert(::access(path.c_str(), F_OK) == 0);
            auto replacing = ControlSocket::open(path, error);
            assert(replacing && error.empty());
            replacing.reset();                              // never served: closes here
            assert(::access(path.c_str(), F_OK) != 0);

            FILE* plain = ::fopen(path.c_str(), "w");
            assert(plain);
            ::fclose(plain);
            auto blocked = ControlSocket::open(path, error);
            assert(!blocked && error.find("not a socket") != std::string::npos);
            assert(::access(path.c_str(), F_OK) == 0);
            ::unlink(path.c_str());
        }

        // The unlink at close is of our own file only: a successor at the
        // path (here, bound by hand after ours was removed) survives our
        // destructor
        {
            error.clear();                      // left set by the refused open above
            auto ours = ControlSocket::open(path, error);
            assert(ours && error.empty());
            ::unlink(path.c_str());
            int successor = ::socket(AF_UNIX, SOCK_STREAM, 0);
            sockaddr_un address = {};
            address.sun_family = AF_UNIX;
            std::memcpy(address.sun_path, path.data(), path.size());
            const int bound = ::bind(successor, (sockaddr const*)&address, sizeof(address));
            assert(bound == 0);
            (void) bound;
            ours.reset();
            assert(::access(path.c_str(), F_OK) == 0);     // the successor's file is still there
            ::close(successor);
            ::unlink(path.c_str());
        }
        co_return;
    };

} // namespace wry
