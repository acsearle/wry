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
#include "save.hpp"
#include "test.hpp"
#include "unicode.hpp"
#include "wire.hpp"
#include "world.hpp"

namespace wry {

    using coroutine::GetStopToken;
    using coroutine::Nursery;

    namespace {

        // Requests a stop source from a stop callback (a token bridge)
        struct Bridge {
            std::stop_source _stop;
            void operator()() {
                std::stop_source keepalive = _stop;
                keepalive.request_stop();
            }
        };

        enum Posted : uint64_t {
            DIAL_FAILED = 1,    // bytes: u64 errno
            SAVE_READY = 2,     // bytes: u64 transfer, u64 boundary, the save
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
        // withdraws it; the spawner's stop (the process scope's, at quit)
        // reaches it through the bridge, so a connect to an address that
        // never answers does not hold the quit for the kernel's timeout.
        // The transport outlives it either way
        coroutine::Task dial_task(std::shared_ptr<Transport> transport, std::stop_source stop,
                                  std::string address, uint16_t port) {
            std::stop_callback<Bridge> outer_to_stop{co_await GetStopToken{}, Bridge{stop}};
            std::optional<int> result =
                co_await execution::shield(transport->connect_tcp(std::move(address), port), stop.get_token());
            if (!result)
                co_return;                  // withdrawn
            if (*result < 0)
                transport->post(DIAL_FAILED, u64_bytes((uint64_t)-*result));
            // Success: the transport admitted the connection, whose Opened
            // is in the inbox
        }

        // The donation, on the pool: the walk over the rooted snapshot,
        // then the bytes handed to the main thread through the inbox,
        // which sends them.  The Root dies with this frame, on a worker
        coroutine::Task donate_task(std::shared_ptr<Transport> transport, RemoteServer::Donation donation,
                                    Root<World const*> snapshot) {
            std::vector<uint8_t> bytes = serialize_world(&*snapshot);
            co_await coroutine::TransferToPoolExecutor{};   // yield after the walk
            std::string posted = u64_bytes(donation.transfer) + u64_bytes(donation.boundary);
            posted.append((const char*)bytes.data(), bytes.size());
            transport->post(SAVE_READY, std::move(posted));
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
        _scope->soon(dial_task(_transport, _dial_stop, std::move(address), port));
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

    bool RemoteServer::submit(Player::Action action) {
        if (_state == State::DISCONNECTED)
            return false;
        // What can travel is decided before the queue, so a held action is
        // one that will go
        std::optional<String> line = command::format_action(action);
        if (!line)
            return false;                   // no literal: cannot be sent
        std::string_view body(line->data(), line->chars.size());
        if (body.size() > wire::MAX_ACTION_BYTES)
            return false;
        if (_state != State::JOINED) {
            // Until our JOIN has ridden the stream the relay would refuse
            // it; it goes once we are in
            if (_queued.size() < 1024)
                _queued.push_back(std::move(action));
            return true;
        }
        _send(wire::encode_action(++_seq, body));
        return true;
    }

    std::vector<RemoteServer::Donation> RemoteServer::take_donations() {
        return take(_donations);
    }

    void RemoteServer::donate(Donation const& donation, Root<World const*> snapshot, coroutine::Nursery& scope) {
        scope.soon(donate_task(_transport, donation, std::move(snapshot)));
    }

    void RemoteServer::_send_save(uint64_t transfer, uint64_t boundary, std::string_view bytes) {
        _send(wire::encode_save_begin(transfer, boundary, bytes.size()));
        uint32_t index = 0;
        for (size_t at = 0; at < bytes.size(); at += wire::MAX_SAVE_CHUNK)
            _send(wire::encode_save_data(transfer, index++, bytes.substr(at, wire::MAX_SAVE_CHUNK)));
        _send(wire::encode_save_end(transfer));
    }

    std::optional<RemoteServer::DonatedSave> RemoteServer::take_save() {
        return std::exchange(_save, std::nullopt);
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

    std::expected<Step, std::string> step_from_wire(wire::Step const& frame) {
        Step step;
        step.number = frame.number;
        for (wire::Entry const& e : frame.entries) {
            if (e.origin == SERVER_CLIENT_ID) {
                std::optional<wire::Event> event = wire::decode_event(e.bytes);
                if (!event)
                    return std::unexpected(std::string("malformed server event"));
                Command c;
                c.kind = event->kind;
                c.client = event->client;
                step.commands.push_back(std::move(c));
            } else {
                // The relay refuses a body over the cap at its intake, so
                // one here is a hostile relay or a crafted log; the parser
                // recurses per nesting level and gets nothing that long
                if (e.bytes.size() > wire::MAX_ACTION_BYTES)
                    return std::unexpected(std::string("action body over the cap"));
                // Total: a body we cannot read is a no-op.  The bytes are
                // untrusted: validated as UTF-8 at this boundary before
                // the parser sees them
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
        return step;
    }

    std::string step_to_wire(Step const& step) {
        std::vector<wire::Entry> entries;
        entries.reserve(step.commands.size());
        for (Command const& c : step.commands) {
            wire::Entry e;
            if (c.kind == Command::ACTION) {
                e.origin = c.origin;
                e.seq = c.seq;
                std::optional<String> line = command::format_action(c.action);
                e.bytes = line ? std::string(line->data(), line->chars.size()) : std::string("nop");
            } else {
                e.origin = SERVER_CLIENT_ID;
                e.seq = 0;
                e.bytes = wire::encode_event(c.kind, c.client);
            }
            entries.push_back(std::move(e));
        }
        return wire::encode_step(step.number, entries);
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
                std::expected<Step, std::string> step = step_from_wire(*frame);
                if (!step)
                    return _disconnect(step.error());
                for (Command const& c : step->commands) {
                    if ((c.kind == Command::JOIN) && (c.client == _client_id) && (_state != State::JOINED)) {
                        _state = State::JOINED;
                        // What was submitted while we waited
                        std::vector<Player::Action> queued = std::move(_queued);
                        _queued.clear();
                        for (Player::Action& a : queued)
                            (void) submit(std::move(a));
                    }
                }
                _steps.push_back(std::move(*step));
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
            case wire::REQUEST_SAVE: {
                std::optional<wire::SaveRequest> request = wire::decode_save_request(bytes);
                if (!request)
                    return _disconnect("malformed REQUEST_SAVE");
                _donations.push_back({request->transfer, request->boundary});
                return;
            }
            case wire::SAVE_BEGIN: {
                std::optional<wire::SaveBegin> begin = wire::decode_save_begin(bytes);
                if (!begin)
                    return _disconnect("malformed SAVE_BEGIN");
                if (_incoming)
                    return _disconnect("a second save before the first was whole");
                if (begin->total > wire::MAX_SAVE_BYTES)
                    return _disconnect("the donated save is too large");
                _incoming = Incoming{begin->transfer, begin->boundary, begin->total, 0, {}};
                _incoming->bytes.reserve((size_t)begin->total);
                return;
            }
            case wire::SAVE_DATA: {
                std::optional<wire::SaveData> data = wire::decode_save_data(bytes);
                if (!data)
                    return _disconnect("malformed SAVE_DATA");
                if (!_incoming || (data->transfer != _incoming->transfer))
                    return _disconnect("save data for no transfer");
                if (data->index != _incoming->next_index)
                    return _disconnect("save data out of order");
                if (_incoming->bytes.size() + data->bytes.size() > _incoming->total)
                    return _disconnect("save data beyond its total");
                ++_incoming->next_index;
                _incoming->bytes.insert(_incoming->bytes.end(), data->bytes.begin(), data->bytes.end());
                return;
            }
            case wire::SAVE_END: {
                std::optional<uint64_t> transfer = wire::decode_save_end(bytes);
                if (!transfer)
                    return _disconnect("malformed SAVE_END");
                if (!_incoming || (*transfer != _incoming->transfer))
                    return _disconnect("save end for no transfer");
                if (_incoming->bytes.size() != _incoming->total)
                    return _disconnect("save ended short");
                _save = DonatedSave{_incoming->boundary, std::move(_incoming->bytes)};
                _incoming.reset();
                return;
            }
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
                if (posted->code == DIAL_FAILED) {
                    _disconnect(std::string("connect: ") + std::strerror((int)u64_of(posted->bytes)));
                } else if ((posted->code == SAVE_READY) && (posted->bytes.size() >= 16)) {
                    if (_state != State::DISCONNECTED) {
                        uint64_t transfer = u64_of(posted->bytes.substr(0, 8));
                        uint64_t boundary = u64_of(posted->bytes.substr(8, 8));
                        _send_save(transfer, boundary, std::string_view(posted->bytes).substr(16));
                    }
                }
            }
        }
        return take(_steps);
    }

    bool RemoteServer::has_input() {
        return _transport->events().size() != 0;
    }

    coroutine::MultiProducerSingleConsumerQueue<Transport::Event>& RemoteServer::inbox() {
        return _transport->events();
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
            // The loops are spawned; they must have ended before the
            // facades go (their destructors assert so)
            (void) execution::sync_wait(h->_relay->shutdown(std::chrono::milliseconds(0)));
            (void) execution::sync_wait(h->_client->shutdown(std::chrono::milliseconds(0)));
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
            bool mapped = world._ptr->_player_for_client.try_get(1, player);
            assert(mapped);
            (void) mapped;
            Term t{};
            bool has_term = world._ptr->_term_for_coordinate.try_get(Coordinate{1, 1}, t);
            assert(has_term);
            (void) has_term;
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

    // A STEP entry's body is bounded as the relay bounds an ACTION's: a
    // longer one is a malformation, not a parse
    define_test("host_step_body_cap") {
        wire::Step frame;
        frame.number = 1;
        frame.entries.push_back({1, 1, std::string("write Coordinate(1, 1) 7")});
        frame.entries.push_back({1, 2, std::string(wire::MAX_ACTION_BYTES, 'x')});
        auto ok = step_from_wire(frame);
        assert(ok && ok->commands.size() == 2);
        assert(ok->commands[0].action.tag == Player::Action::WRITE_VALUE_FOR_COORDINATE);
        assert(ok->commands[1].action.tag == Player::Action::NONE);     // unparseable: a no-op
        frame.entries.push_back({1, 3, std::string(wire::MAX_ACTION_BYTES + 1, '(')});
        auto over = step_from_wire(frame);
        assert(!over);
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

        host->relay().step(2);
        bool stepped = eventually([&] { return host->relay().clock().head >= 3; });
        assert(stepped);
        (void) stepped;
        auto joiner = RemoteServer::make(scope);
        joiner->dial("127.0.0.1", *port);
        joiner->request_join();

        // The joiner's HELLO has the relay ask the host's client for a
        // save at the head; the test plays the host's World
        std::vector<RemoteServer::Donation> donations;
        bool asked = eventually([&] {
            (void) joiner->take_steps();
            (void) host->client().take_steps();
            for (auto& d : host->client().take_donations())
                donations.push_back(d);
            return !donations.empty();
        });
        assert(asked && donations.size() == 1);
        (void) asked;
        const uint64_t boundary = donations[0].boundary;
        assert(boundary == host->relay().clock().head);
        World* w = new World;
        w->_time = (Time)boundary;
        Player* p = new Player;
        p->_entity_id = w->generate_entity_id();
        p->seal();
        w->_entity_for_entity_id.set(p->_entity_id, p);
        w->_waiting_on_time.set({(Time)boundary, p->_entity_id});
        w->_player_for_client.set(ClientID{1}, p->_entity_id);
        w->_term_for_coordinate.set(Coordinate{2, 3}, term_make_string_with("donated"));
        w->hack_repair_invariant();
        Root<World const*> snapshot{w};
        host->client().donate(donations[0], snapshot, scope);

        // The joiner assembles the save, loads it, and proves receipt with
        // its hash; the JOIN waits for that proof, then rides a step
        std::optional<RemoteServer::DonatedSave> save;
        bool received = eventually([&] {
            (void) host->client().take_steps();     // sends the serialized save
            (void) joiner->take_steps();
            save = joiner->take_save();
            return save.has_value();
        });
        assert(received && save->boundary == boundary);
        (void) received;
        World* loaded = load_world_from_bytes(save->bytes);
        assert(loaded && loaded->_time == (Time)boundary);
        assert(loaded->content_hash() == w->content_hash());
        {
            Term t{};
            bool has_term = loaded->_term_for_coordinate.try_get(Coordinate{2, 3}, t);
            assert(has_term);
            (void) has_term;
            assert(term_eq(t, term_make_string_with("donated")));
        }
        // The JOIN is not yet admitted: no report
        host->relay().step(1);
        bool pending = eventually([&] {
            (void) joiner->take_steps();
            return joiner->head() >= boundary + 1;
        });
        assert(pending && joiner->state() == RemoteServer::State::SUBSCRIBED);
        (void) pending;
        host->client().report_hash(boundary, w->content_hash());
        joiner->report_hash(boundary, loaded->content_hash());
        bool joined = eventually([&] {
            host->relay().step(1);
            (void) host->client().take_steps();
            (void) joiner->take_steps();
            return joiner->state() == RemoteServer::State::JOINED;
        });
        assert(joined);
        (void) joined;
        assert(joiner->client_id() == 2);

        // Hash reports: agreement with the host's client is silent, and a
        // disagreement at any step the reference has reported is a kick
        const uint64_t later = joiner->head();
        host->client().report_hash(later - 1, 0x1111);
        joiner->report_hash(later - 1, 0x1111);
        joiner->report_hash(later, 0x2222);
        host->client().report_hash(later, 0x2222);
        bool agreed = eventually([&] {
            (void) joiner->take_steps();
            return joiner->state() == RemoteServer::State::JOINED;
        }, std::chrono::seconds(1));
        assert(agreed);
        (void) agreed;
        host->relay().step(1);
        bool one_more = eventually([&] { return host->relay().clock().head > later; });
        assert(one_more);
        (void) one_more;
        host->client().report_hash(later + 1, 0x3333);
        joiner->report_hash(later + 1, 0xbad);
        bool desynced = eventually([&] {
            (void) joiner->take_steps();
            return joiner->state() == RemoteServer::State::DISCONNECTED;
        });
        assert(desynced && joiner->reason() == "kicked: desync at step " + std::to_string(later + 1));
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
        second->report_hash(later + 1, 0x3333);
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
