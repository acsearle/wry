//
//  host.cpp
//  client
//
//  Created by Antony Searle on 2026-10-21.
//

#include <cerrno>
#include <cstring>
#include <thread>
#include <variant>

#include <sys/socket.h>
#include <unistd.h>

#include "assert.hpp"
#include "command_types.hpp"
#include "execution.hpp"
#include "host.hpp"
#include "test.hpp"
#include "unicode.hpp"
#include "wire.hpp"
#include "world.hpp"

namespace wry {

    using coroutine::Nursery;

    namespace {

        enum Posted : uint64_t {
            DIAL_FAILED = 1,    // bytes: u64 errno
        };

        std::string u64_bytes(uint64_t x) {
            std::string bytes(sizeof x, '\0');
            std::memcpy(bytes.data(), &x, sizeof x);
            return bytes;
        }

        uint64_t u64_of(std::string const& bytes) {
            uint64_t x = 0;
            if (bytes.size() == sizeof x)
                std::memcpy(&x, bytes.data(), sizeof x);
            return x;
        }

        // The dial, on the pool, under its own stop source so a shutdown
        // withdraws it; the transport outlives it either way
        coroutine::Task dial_task(std::shared_ptr<Transport> transport, std::stop_token stop,
                                  std::string address, uint16_t port) {
            std::optional<int> result =
                co_await execution::shield(transport->connect_tcp(std::move(address), port), stop);
            if (!result)
                co_return;                  // withdrawn
            if (*result < 0)
                transport->post(DIAL_FAILED, u64_bytes((uint64_t)-*result));
            // Success: the transport admitted the connection, whose Opened
            // is in the inbox
        }

    } // namespace

    // ----------------------------------------------------------------
    // RemoteServer.

    std::unique_ptr<RemoteServer> RemoteServer::make(coroutine::Nursery& scope, Transport::Limits limits) {
        std::unique_ptr<RemoteServer> r(new RemoteServer);
        r->_scope = &scope;
        limits.max_connections = 1;
        r->_transport = Transport::make(wire::codec(), limits);
        scope.soon(r->_transport->serve());
        return r;
    }

    int RemoteServer::adopt(int fd) {
        return _transport->adopt(fd);
    }

    void RemoteServer::dial(std::string address, uint16_t port) {
        _scope->soon(dial_task(_transport, _dial_stop.get_token(), std::move(address), port));
    }

    void RemoteServer::_send(std::string message) {
        if (_connection)
            _transport->send(_connection, message);
    }

    void RemoteServer::_disconnect(std::string reason) {
        if (_state == State::DISCONNECTED)
            return;
        _state = State::DISCONNECTED;
        _reason = std::move(reason);
        if (_connection)
            _transport->end(_connection);
    }

    void RemoteServer::submit(Player::Action action) {
        if (_state == State::DISCONNECTED)
            return;
        if (_state != State::JOINED) {
            // Until our JOIN has ridden the stream the relay would refuse
            // it; it goes once we are in
            if (_queued.size() < 1024)
                _queued.push_back(std::move(action));
            return;
        }
        std::optional<String> line = command::format_action(action);
        if (!line)
            return;                         // no literal: cannot be sent
        std::string_view body(line->data(), line->chars.size());
        if (body.size() > wire::MAX_ACTION_BYTES)
            return;
        _send(wire::encode_action(++_seq, body));
    }

    void RemoteServer::report_hash(uint64_t step, uint64_t hash) {
        if ((_state == State::SUBSCRIBED) || (_state == State::JOINED))
            _send(wire::encode_hash(step, hash));
    }

    void RemoteServer::request_join() {
        _join_requested = true;
        if ((_state == State::SUBSCRIBED) && !_join_sent) {
            _join_sent = true;
            _send(wire::encode_join_request("player"));
        }
    }

    void RemoteServer::_on_message(std::string const& bytes) {
        const uint16_t tag = wire::tag_of(bytes);
        switch (tag) {
            case wire::WELCOME: {
                if (_state != State::HANDSHAKE)
                    return _disconnect("unexpected WELCOME");
                std::optional<wire::Welcome> welcome = wire::decode_welcome(bytes);
                if (!welcome)
                    return _disconnect("malformed WELCOME");
                _client_id = welcome->client;
                _quantum_ns = welcome->quantum_ns;
                _next_step = welcome->first;
                _state = State::SUBSCRIBED;
                // The stream from genesis follows, up to welcome->head,
                // then the live steps; each is checked as it comes
                if (_join_requested && !_join_sent) {
                    _join_sent = true;
                    _send(wire::encode_join_request("player"));
                }
                return;
            }
            case wire::STEP: {
                if ((_state != State::SUBSCRIBED) && (_state != State::JOINED))
                    return _disconnect("STEP before WELCOME");
                std::optional<wire::Step> frame = wire::decode_step(bytes);
                if (!frame)
                    return _disconnect("malformed STEP");
                if (frame->number != _next_step)
                    return _disconnect("the stream skipped: step " + std::to_string(frame->number)
                                       + " after " + std::to_string(_next_step - 1));
                ++_next_step;
                Step step;
                step.number = frame->number;
                for (wire::Entry& e : frame->entries) {
                    if (e.origin == SERVER_CLIENT_ID) {
                        std::optional<wire::Event> event = wire::decode_event(e.bytes);
                        if (!event)
                            return _disconnect("malformed server event");
                        Command c;
                        c.kind = event->kind;
                        c.client = event->client;
                        step.commands.push_back(std::move(c));
                        if ((event->kind == Command::JOIN) && (event->client == _client_id)) {
                            _state = State::JOINED;
                            // What was submitted while we waited
                            std::vector<Player::Action> queued = std::move(_queued);
                            _queued.clear();
                            for (Player::Action& a : queued)
                                submit(std::move(a));
                        }
                    } else {
                        // Total: a body we cannot read is a no-op.  The
                        // bytes are untrusted: validated as UTF-8 at this
                        // boundary before the parser sees them
                        Player::Action action;
                        const char* first = e.bytes.data();
                        const char* last = first + e.bytes.size();
                        if (utf8::isvalid(first, last)) {
                            auto parsed = command::parse_action(StringView(trusted, first, last));
                            if (parsed)
                                action = std::move(*parsed);
                        }
                        step.commands.push_back(make_action_command(e.origin, e.seq, std::move(action)));
                    }
                }
                _steps.push_back(std::move(step));
                return;
            }
            case wire::KICK: {
                std::optional<std::string_view> reason = wire::decode_kick(bytes);
                return _disconnect("kicked: " + std::string(reason ? *reason : std::string_view("?")));
            }
            case wire::SHUTDOWN:
                return _disconnect("the server shut down");
            case wire::PING:
                return;
            default:
                return _disconnect("unexpected message from the server");
        }
    }

    std::vector<Step> RemoteServer::take_steps() {
        for (Transport::Event& event : _transport->events().try_take()) {
            if (auto* opened = std::get_if<Transport::Opened>(&event)) {
                assert(_connection == 0);
                _connection = opened->connection;
                if (_state == State::CONNECTING) {
                    _state = State::HANDSHAKE;
                    _send(wire::encode_hello({wire::PROTOCOL_VERSION, wire::build_id(), 0}));
                }
            } else if (auto* message = std::get_if<Transport::Message>(&event)) {
                if (_state != State::DISCONNECTED)
                    _on_message(message->bytes);
            } else if (auto* closed = std::get_if<Transport::Closed>(&event)) {
                if (_state != State::DISCONNECTED)
                    _disconnect(closed->reason == "end of stream" ? "the server closed the connection"
                                                                   : closed->reason);
                _connection = 0;
            } else if (auto* posted = std::get_if<Transport::Posted>(&event)) {
                if (posted->code == DIAL_FAILED)
                    _disconnect(std::string("connect: ") + std::strerror((int)u64_of(posted->bytes)));
            }
        }
        return take(_steps);
    }

    bool RemoteServer::has_input() {
        return _transport->events().size() != 0;
    }

    void RemoteServer::disconnect(std::string reason) {
        _disconnect(std::move(reason));
    }

    void RemoteServer::leave() {
        if (_state == State::DISCONNECTED)
            return;
        _send(wire::encode_quit());
        _disconnect("left");
    }

    coroutine::Task RemoteServer::shutdown(std::chrono::milliseconds grace) {
        _dial_stop.request_stop();
        co_await _transport->shutdown(grace);
        co_await _transport->wait_finished();
    }

    void RemoteServer::close() {
        _dial_stop.request_stop();
        _transport->close();
    }

    bool RemoteServer::finished() {
        return _transport->finished();
    }

    RemoteServer::~RemoteServer() {
        assert(finished());
    }

    // ----------------------------------------------------------------
    // Host.

    std::unique_ptr<Host> Host::make(coroutine::Nursery& scope, Relay::Limits limits, std::string& error,
                                     uint64_t head) {
        int fds[2] = {-1, -1};
        if (::socketpair(AF_UNIX, SOCK_STREAM, 0, fds) != 0) {
            error = std::string("socketpair: ") + std::strerror(errno);
            return nullptr;
        }
        std::unique_ptr<Host> h(new Host);
        h->_relay = Relay::make(limits, head);
        scope.soon(h->_relay->serve());
        h->_client = RemoteServer::make(scope);
        // The host's own client is the reference every other client's
        // hash is compared with
        int server_side = h->_relay->adopt(fds[0], true);
        int client_side = h->_client->adopt(fds[1]);
        if ((server_side < 0) || (client_side < 0)) {
            error = std::string("adopt: ") + std::strerror(-std::min(server_side, client_side));
            // The loops are spawned: the owner's scope joins them once
            // they are closed
            h->_relay->close();
            h->_client->close();
            return nullptr;
        }
        return h;
    }

    coroutine::Task Host::shutdown(std::chrono::milliseconds grace) {
        co_await _relay->shutdown(grace);
        co_await _client->shutdown(grace);
    }

    bool Host::finished() {
        return _relay->finished() && _client->finished();
    }

    // ----------------------------------------------------------------
    // Tests.

    namespace {

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

    // A host's own client: handshake, JOIN, actions, and the steps a World
    // would apply, over the socket pair
    define_test("host_round_trip") {
        Nursery scope;
        std::string error;
        Relay::Limits limits;
        limits.quantum = std::chrono::hours(1);
        auto host = Host::make(scope, limits, error);
        assert(host && error.empty());
        RemoteServer& client = host->client();
        host->relay().pause();              // step 1: PAUSED
        client.request_join();              // JOIN_REQUEST once welcomed
        std::vector<Step> steps;
        auto take = [&] {
            for (Step& s : client.take_steps())
                steps.push_back(std::move(s));
        };
        bool subscribed = eventually([&] {
            take();
            return client.state() == RemoteServer::State::SUBSCRIBED || client.state() == RemoteServer::State::JOINED;
        });
        assert(subscribed);
        (void) subscribed;
        assert(client.client_id() == 1);    // the first connection

        // Submitted before the JOIN: queued, sent once in.  (A posted step
        // goes straight to the relay's inbox while the request travels the
        // socket, so the test keeps stepping, as the clock would)
        client.submit(make_write_action(Coordinate{1, 1}, term_make_integer_with(7)));
        bool joined = eventually([&] {
            host->relay().step(1);
            take();
            return client.state() == RemoteServer::State::JOINED;
        });
        assert(joined);
        (void) joined;
        bool acted = eventually([&] {
            host->relay().step(1);
            take();
            for (Step& s : steps)
                for (Command& c : s.commands)
                    if (c.kind == Command::ACTION)
                        return true;
            return false;
        });
        assert(acted);
        (void) acted;
        // The steps are numbered from 1 and carry PAUSED, JOIN{1}, then
        // the action with its sequence number
        assert(steps.size() >= 3);
        for (size_t i = 0; i != steps.size(); ++i)
            assert(steps[i].number == i + 1);
        bool paused_seen = false, join_seen = false;
        Command const* action = nullptr;
        for (Step& s : steps)
            for (Command& c : s.commands) {
                if (c.kind == Command::PAUSED) paused_seen = true;
                if (c.kind == Command::JOIN && c.client == 1) join_seen = true;
                if (c.kind == Command::ACTION) action = &c;
            }
        assert(paused_seen && join_seen && action);
        assert(action->origin == 1 && action->seq == 1);
        assert(action->action.tag == Player::Action::WRITE_VALUE_FOR_COORDINATE);
        assert(action->action.coordinate == (Coordinate{1, 1}));
        assert(action->action.value._value._data == term_make_integer_with(7)._data);

        // Applied to a World, the steps spawn the Player and land the write
        {
            World* w = new World;
            w->hack_repair_invariant();
            Root<World*> world{w};
            for (Step& s : steps) {
                epoch::Epoch pin = pin_global_epoch();
                Root<World*> next = co_await world._ptr->step(s.commands);
                unpin_global_epoch(pin);
                world = std::move(next);
                assert(world._ptr->_time == (Time)s.number);
            }
            EntityID player{};
            assert(world._ptr->_player_for_client.try_get(1, player));
            Term t{};
            assert(world._ptr->_term_for_coordinate.try_get(Coordinate{1, 1}, t));
            assert(t._data == term_make_integer_with(7)._data);
        }

        // Leaving: the relay sees the QUIT and our LEAVE rides a step that
        // we no longer receive
        client.leave();
        assert(client.state() == RemoteServer::State::DISCONNECTED && client.reason() == "left");
        co_await host->shutdown(std::chrono::milliseconds(200));
        assert(host->finished());
        (void) co_await scope.join();
        co_return;
    };

    // A second process's client: over TCP to a listening relay
    define_test("host_tcp_join") {
        Nursery scope;
        std::string error;
        Relay::Limits limits;
        limits.quantum = std::chrono::hours(1);
        auto host = Host::make(scope, limits, error);
        assert(host && error.empty());
        host->relay().pause();
        auto port = host->relay().listen("127.0.0.1", 0);
        assert(port && *port != 0);

        auto joiner = RemoteServer::make(scope);
        joiner->dial("127.0.0.1", *port);
        joiner->request_join();
        host->relay().step(2);
        bool joined = eventually([&] {
            (void) host->client().take_steps();
            (void) joiner->take_steps();
            host->relay().step(1);
            return joiner->state() == RemoteServer::State::JOINED;
        });
        assert(joined);
        (void) joined;
        assert(joiner->client_id() == 2);
        assert(joiner->head() >= 2);

        // Hash reports: agreement with the host's client is silent, and a
        // disagreement at any step the reference has reported is a kick
        host->client().report_hash(1, 0x1111);
        joiner->report_hash(1, 0x1111);
        joiner->report_hash(2, 0x2222);
        host->client().report_hash(2, 0x2222);
        host->client().report_hash(3, 0x3333);
        joiner->report_hash(3, 0xbad);
        bool desynced = eventually([&] {
            (void) joiner->take_steps();
            return joiner->state() == RemoteServer::State::DISCONNECTED;
        });
        assert(desynced && joiner->reason() == "kicked: desync at step 3");
        (void) desynced;

        // A dial that is refused reports its error
        auto refused = RemoteServer::make(scope);
        {
            auto spare = Transport::listen_tcp("127.0.0.1", 0, wire::codec(), Transport::Limits{}, error);
            uint16_t dead = spare->port();
            spare.reset();
            refused->dial("127.0.0.1", dead);
        }
        bool failed = eventually([&] {
            (void) refused->take_steps();
            return refused->state() == RemoteServer::State::DISCONNECTED;
        });
        assert(failed && refused->reason().starts_with("connect: "));
        (void) failed;

        // A second joiner, agreeing, is told of the shutdown
        auto second = RemoteServer::make(scope);
        second->dial("127.0.0.1", *port);
        bool subscribed = eventually([&] {
            (void) second->take_steps();
            return second->state() == RemoteServer::State::SUBSCRIBED;
        });
        assert(subscribed);
        (void) subscribed;
        second->report_hash(3, 0x3333);
        co_await host->shutdown(std::chrono::milliseconds(200));
        bool told = eventually([&] {
            (void) second->take_steps();
            return second->state() == RemoteServer::State::DISCONNECTED;
        });
        assert(told && second->reason() == "the server shut down");
        (void) told;
        co_await joiner->shutdown(std::chrono::milliseconds(100));
        co_await second->shutdown(std::chrono::milliseconds(100));
        co_await refused->shutdown(std::chrono::milliseconds(100));
        (void) co_await scope.join();
        co_return;
    };

} // namespace wry
