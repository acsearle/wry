//
//  relay.cpp
//  client
//
//  Created by Antony Searle on 2026-10-21.
//

#include <chrono>
#include <cstring>
#include <map>
#include <mutex>
#include <thread>
#include <variant>
#include <vector>

#include <sys/socket.h>
#include <unistd.h>

#include "assert.hpp"
#include "execution.hpp"
#include "kqueue_reactor.hpp"
#include "nursery.hpp"
#include "relay.hpp"
#include "single_consumer_event.hpp"
#include "test.hpp"
#include "wire.hpp"

namespace wry {

    using coroutine::Future;
    using coroutine::GetStopToken;
    using coroutine::Nursery;

    namespace {

        // The owner's events, through Transport::post
        enum Posted : uint64_t {
            TICK = 1,       // the step timer
            PAUSE = 2,
            RESUME = 3,
            STEP = 4,       // bytes: u64 n
            SHUTDOWN = 5,   // bytes: u64 grace in milliseconds
            DEADLINE = 6,   // bytes: u64 connection: the handshake deadline
            REFERENCE = 7,  // bytes: u64 client: whose hashes are the truth
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

        // How many recent steps a client's hash reports are kept for.  A
        // report older than this is not compared: a client that far
        // behind the reference, or ahead of it, is a lag problem, not a
        // desync one
        constexpr size_t HASH_WINDOW = 256;

        struct Client {
            enum State : uint8_t { HELLO_PENDING, SUBSCRIBED, JOINED };
            State state = HELLO_PENDING;
            uint64_t next_seq = 1;
            size_t pending = 0;             // actions waiting for a step
            size_t taken = 0;               // actions taken into the step being built
            bool join_requested = false;    // asked; waiting for the gate
            bool join_pending = false;      // a JOIN rides the next step
            bool kicked = false;            // nothing further from it is processed
            std::map<uint64_t, uint64_t> hashes;    // step -> reported hash, the last HASH_WINDOW
            uint64_t reported = 0;          // the last step it reported: its lag
        };

        struct PendingAction {
            ClientID client;
            uint64_t seq;
            std::string bytes;
        };

        struct PendingEvent {
            Command::Kind kind;
            ClientID client;
        };

    } // namespace

    struct Relay::Shared {
        Limits limits;
        std::unique_ptr<Transport> transport;
        std::stop_source stop;              // the facade's; the task runs under it
        coroutine::SingleConsumerEvent finished_event;

        // The consumer's state: one thread at a time touches it
        std::map<uint64_t, Client> clients;
        ClientID reference = SERVER_CLIENT_ID;  // whose hashes are the truth; 0 for none
        // Donations in flight: the reference is serializing its World at
        // `boundary` for `joiner`, and its SAVE frames are routed there
        struct Transfer {
            ClientID joiner;
            uint64_t boundary;
        };
        std::map<uint64_t, Transfer> transfers;
        uint64_t next_transfer = 1;
        std::vector<PendingAction> actions;
        std::vector<PendingEvent> events;
        std::vector<std::string> log;       // STEP messages; log[i] is step first + i
        uint64_t first = 1;                 // the number of the first step in the log
        bool timer_armed = false;
        std::chrono::steady_clock::time_point next_tick;

        // Under the mutex: the clock as the owner sees it
        std::mutex mutex;
        uint64_t head = 0;
        bool paused = false;
        bool finished = false;
    };

    namespace {

        using Shared = Relay::Shared;

        void kick(Shared& s, uint64_t id, Client& c, std::string_view reason) {
            if (c.kicked)
                return;
            c.kicked = true;
            s.transport->kick(id, reason);
        }

        void arm_timer(std::shared_ptr<Shared> const& s) {
            if (s->timer_armed)
                return;
            s->timer_armed = true;
            auto now = std::chrono::steady_clock::now();
            s->next_tick += s->limits.quantum;
            // Far behind (a long pause, a stall): resume the cadence from
            // now rather than firing a burst
            if (s->next_tick < now)
                s->next_tick = now;
            cancelable_after(s->stop.get_token(), s->next_tick - now, [s] {
                s->transport->post(TICK);
            });
        }

        // Close the step being built: the events that fell due, then the
        // actions in arrival order under the per-client quota; number it,
        // keep it, send it to every subscriber
        void finalize(std::shared_ptr<Shared> const& s) {
            std::vector<wire::Entry> entries;
            for (PendingEvent const& e : s->events)
                entries.push_back({SERVER_CLIENT_ID, 0, wire::encode_event(e.kind, e.client)});
            std::vector<PendingAction> deferred;
            for (PendingAction& a : s->actions) {
                auto it = s->clients.find(a.client);
                if (it == s->clients.end())
                    continue;               // gone: its actions go with it
                Client& c = it->second;
                if (c.taken < s->limits.actions_per_step) {
                    ++c.taken;
                    --c.pending;
                    entries.push_back({a.client, a.seq, std::move(a.bytes)});
                } else {
                    deferred.push_back(std::move(a));
                }
            }
            s->actions = std::move(deferred);
            uint64_t number = 0;
            {
                std::scoped_lock lock{s->mutex};
                number = ++s->head;
            }
            std::string message = wire::encode_step(number, entries);
            s->log.push_back(message);
            for (auto& [id, c] : s->clients) {
                c.taken = 0;
                if ((c.state != Client::HELLO_PENDING) && !c.kicked)
                    s->transport->send(id, message);
            }
            // A client whose JOIN just rode the stream may act from now
            for (PendingEvent const& e : s->events) {
                if (e.kind != Command::JOIN)
                    continue;
                auto it = s->clients.find(e.client);
                if (it != s->clients.end()) {
                    it->second.state = Client::JOINED;
                    it->second.join_pending = false;
                }
            }
            s->events.clear();
        }

        // The join gate (section 6): a client may join once it has shown
        // it holds the World, by a hash report within join_window of the
        // head.  The reference needs no proof, and a relay without one
        // has no save to have received
        bool gate_open(Shared const& s, uint64_t id, Client const& c, uint64_t head) {
            if ((s.reference == SERVER_CLIENT_ID) || (id == s.reference))
                return true;
            return !c.hashes.empty() && (c.reported + s.limits.join_window >= head);
        }

        void try_join(std::shared_ptr<Shared> const& s, uint64_t id, Client& c) {
            if (!c.join_requested || c.join_pending || (c.state == Client::JOINED) || c.kicked)
                return;
            uint64_t head = 0;
            {
                std::scoped_lock lock{s->mutex};
                head = s->head;
            }
            if (!gate_open(*s, id, c, head))
                return;
            c.join_requested = false;
            c.join_pending = true;
            s->events.push_back({Command::JOIN, id});
        }

        // A client's report against the reference's for the same step, or
        // the reference's against everyone's: a disagreement is a kick
        void on_hash(std::shared_ptr<Shared> const& s, uint64_t id, Client& c, uint64_t step, uint64_t hash) {
            uint64_t head = 0;
            {
                std::scoped_lock lock{s->mutex};
                head = s->head;
            }
            if (step > head)
                return kick(*s, id, c, "hash for a step not yet finalized");
            c.hashes[step] = hash;
            c.reported = std::max(c.reported, step);
            while (c.hashes.size() > HASH_WINDOW)
                c.hashes.erase(c.hashes.begin());
            if (s->reference != SERVER_CLIENT_ID) {
                std::string desync = "desync at step " + std::to_string(step);
                if (id == s->reference) {
                    for (auto& [other_id, other] : s->clients) {
                        if ((other_id == id) || other.kicked)
                            continue;
                        auto it = other.hashes.find(step);
                        if ((it != other.hashes.end()) && (it->second != hash))
                            kick(*s, other_id, other, desync);
                    }
                } else {
                    auto r = s->clients.find(s->reference);
                    if (r != s->clients.end()) {
                        auto it = r->second.hashes.find(step);
                        if ((it != r->second.hashes.end()) && (it->second != hash))
                            kick(*s, id, c, desync);
                    }
                }
            }
            // A report may be the proof a waiting JOIN needed
            try_join(s, id, c);
        }

        // SAVE frames from the donor go to the transfer's joiner, unread
        void on_save_frame(std::shared_ptr<Shared> const& s, uint64_t id, Client& c, uint16_t tag,
                           std::string const& bytes) {
            if (id != s->reference)
                return kick(*s, id, c, "unexpected message");
            uint64_t transfer = 0;
            if (tag == wire::SAVE_BEGIN) {
                auto b = wire::decode_save_begin(bytes);
                if (!b)
                    return kick(*s, id, c, "malformed SAVE_BEGIN");
                transfer = b->transfer;
            } else if (tag == wire::SAVE_DATA) {
                auto d = wire::decode_save_data(bytes);
                if (!d)
                    return kick(*s, id, c, "malformed SAVE_DATA");
                transfer = d->transfer;
            } else {
                auto e = wire::decode_save_end(bytes);
                if (!e)
                    return kick(*s, id, c, "malformed SAVE_END");
                transfer = *e;
            }
            auto it = s->transfers.find(transfer);
            if (it == s->transfers.end())
                return;                     // its joiner has gone: dropped
            auto j = s->clients.find(it->second.joiner);
            if ((j != s->clients.end()) && !j->second.kicked)
                s->transport->send(it->second.joiner, bytes);
            if (tag == wire::SAVE_END)
                s->transfers.erase(it);
        }

        void on_message(std::shared_ptr<Shared> const& s, uint64_t id, std::string const& bytes) {
            auto it = s->clients.find(id);
            if (it == s->clients.end())
                return;
            Client& c = it->second;
            if (c.kicked)
                return;                     // nothing further from it
            const uint16_t tag = wire::tag_of(bytes);
            if (c.state == Client::HELLO_PENDING) {
                if (tag != wire::HELLO)
                    return kick(*s, id, c, "expected HELLO first");
                std::optional<wire::Hello> hello = wire::decode_hello(bytes);
                if (!hello)
                    return kick(*s, id, c, "malformed HELLO");
                if (hello->protocol != wire::PROTOCOL_VERSION)
                    return kick(*s, id, c, "protocol version mismatch");
                if (hello->build != wire::build_id())
                    return kick(*s, id, c, "build mismatch");
                c.state = Client::SUBSCRIBED;
                uint64_t head = 0;
                {
                    std::scoped_lock lock{s->mutex};
                    head = s->head;
                }
                // A joiner gets the stream from here on and the World as of
                // here from the reference.  The reference itself holds the
                // World the stream started from, so it gets the stream from
                // its start (the clock may have ticked before its HELLO
                // arrived); so does every client of a relay without a
                // reference, a test fixture
                const bool donated = (s->reference != SERVER_CLIENT_ID) && (id != s->reference);
                const uint64_t first = donated ? head + 1 : s->first;
                s->transport->send(id, wire::encode_welcome({id, (uint64_t)s->limits.quantum.count(), head, first}));
                if (donated) {
                    uint64_t transfer = s->next_transfer++;
                    s->transfers.emplace(transfer, Shared::Transfer{id, head});
                    // Queued to the reference after STEP head, before STEP
                    // head + 1: it snapshots the World STEP head produces
                    s->transport->send(s->reference, wire::encode_save_request(transfer, head));
                } else {
                    for (std::string const& step : s->log)
                        s->transport->send(id, step);
                }
                return;
            }
            switch (tag) {
                case wire::ACTION: {
                    if (c.state != Client::JOINED)
                        return kick(*s, id, c, "action before join");
                    std::optional<wire::Action> action = wire::decode_action(bytes);
                    if (!action)
                        return kick(*s, id, c, "malformed ACTION");
                    if (action->seq != c.next_seq)
                        return kick(*s, id, c, "bad sequence number");
                    if (c.pending >= s->limits.max_pending_actions)
                        return kick(*s, id, c, "flood");
                    ++c.next_seq;
                    ++c.pending;
                    s->actions.push_back({id, action->seq, std::string(action->body)});
                    return;
                }
                case wire::JOIN_REQUEST: {
                    if (!wire::decode_join_request(bytes))
                        return kick(*s, id, c, "malformed JOIN_REQUEST");
                    if ((c.state == Client::JOINED) || c.join_pending)
                        return;             // idempotent
                    c.join_requested = true;
                    try_join(s, id, c);     // now, or at the report that opens the gate
                    return;
                }
                case wire::SAVE_BEGIN:
                case wire::SAVE_DATA:
                case wire::SAVE_END:
                    return on_save_frame(s, id, c, tag, bytes);
                case wire::QUIT:
                    s->transport->end(id);  // its Closed follows, and the LEAVE
                    return;
                case wire::HASH: {
                    std::optional<std::pair<uint64_t, uint64_t>> report = wire::decode_hash(bytes);
                    if (!report)
                        return kick(*s, id, c, "malformed HASH");
                    return on_hash(s, id, c, report->first, report->second);
                }
                default:
                    return kick(*s, id, c, "unexpected message");
            }
        }

        void on_closed(std::shared_ptr<Shared> const& s, uint64_t id) {
            auto it = s->clients.find(id);
            if (it == s->clients.end())
                return;
            Client& c = it->second;
            if ((c.state == Client::JOINED) || c.join_pending)
                s->events.push_back({Command::LEAVE, id});
            std::erase_if(s->actions, [id](PendingAction const& a) { return a.client == id; });
            // Its donation, if one was in flight, has nowhere to go; and
            // without the reference there is nothing to compare with or
            // to donate
            std::erase_if(s->transfers, [id](auto const& t) { return t.second.joiner == id; });
            if (id == s->reference) {
                s->reference = SERVER_CLIENT_ID;
                s->transfers.clear();
            }
            s->clients.erase(it);
        }

        // The owner's events.  True to go on; false once the shutdown has
        // been started
        bool on_posted(std::shared_ptr<Shared> const& s, Nursery* siblings, Transport::Posted const& posted) {
            switch (posted.code) {
                case TICK: {
                    s->timer_armed = false;
                    bool paused = false;
                    {
                        std::scoped_lock lock{s->mutex};
                        paused = s->paused;
                    }
                    if (!paused) {
                        finalize(s);
                        arm_timer(s);
                    }
                    return true;
                }
                case PAUSE: {
                    {
                        std::scoped_lock lock{s->mutex};
                        if (s->paused)
                            return true;
                    }
                    // The step that closes the stream first, the pause
                    // published after it: an owner that sees the clock
                    // paused sees the head it will stay at
                    s->events.push_back({Command::PAUSED, SERVER_CLIENT_ID});
                    finalize(s);
                    {
                        std::scoped_lock lock{s->mutex};
                        s->paused = true;
                    }
                    return true;
                }
                case RESUME: {
                    {
                        std::scoped_lock lock{s->mutex};
                        if (!s->paused)
                            return true;
                        s->paused = false;
                    }
                    s->events.push_back({Command::RESUMED, SERVER_CLIENT_ID});
                    s->next_tick = std::chrono::steady_clock::now();
                    arm_timer(s);
                    return true;
                }
                case STEP: {
                    uint64_t n = u64_of(posted.bytes);
                    for (uint64_t i = 0; i != n; ++i)
                        finalize(s);
                    return true;
                }
                case DEADLINE: {
                    uint64_t id = u64_of(posted.bytes);
                    auto it = s->clients.find(id);
                    if ((it != s->clients.end()) && (it->second.state == Client::HELLO_PENDING))
                        kick(*s, id, it->second, "handshake timeout");
                    return true;
                }
                case REFERENCE:
                    s->reference = (ClientID)u64_of(posted.bytes);
                    return true;
                case SHUTDOWN: {
                    std::string farewell = wire::encode_shutdown();
                    for (auto& [id, c] : s->clients)
                        if (!c.kicked)
                            s->transport->send(id, farewell);
                    siblings->soon(s->transport->shutdown(std::chrono::milliseconds(u64_of(posted.bytes))));
                    return false;
                }
                default:
                    return true;
            }
        }

        Future<> consume(std::shared_ptr<Shared> s, Nursery* siblings) {
            s->next_tick = std::chrono::steady_clock::now();
            arm_timer(s);
            for (;;) {
                std::deque<Transport::Event> batch = co_await s->transport->events().take();
                for (Transport::Event& event : batch) {
                    if (auto* opened = std::get_if<Transport::Opened>(&event)) {
                        s->clients.emplace(opened->connection, Client{});
                        uint64_t id = opened->connection;
                        cancelable_after(s->stop.get_token(), s->limits.handshake_deadline, [s, id] {
                            s->transport->post(DEADLINE, u64_bytes(id));
                        });
                    } else if (auto* message = std::get_if<Transport::Message>(&event)) {
                        on_message(s, message->connection, message->bytes);
                    } else if (auto* closed = std::get_if<Transport::Closed>(&event)) {
                        on_closed(s, closed->connection);
                    } else if (auto* posted = std::get_if<Transport::Posted>(&event)) {
                        if (!on_posted(s, siblings, *posted))
                            co_return;
                    }
                }
            }
        }

        struct Bridge {
            std::stop_source _stop;
            void operator()() {
                std::stop_source keepalive = _stop;
                keepalive.request_stop();
            }
        };

        // Reports the task finished on whatever path it ends: the
        // graceful one resumes the join, the brutal one destroys it.
        // Either way the relay's own stop is requested first, which
        // withdraws its armed timers (the next tick, the handshake
        // deadlines): an arm is withdrawn only by its token, and a
        // graceful end leaves it to this
        struct Finish {
            std::shared_ptr<Shared> shared;
            ~Finish() {
                shared->stop.request_stop();
                {
                    std::scoped_lock lock{shared->mutex};
                    shared->finished = true;
                }
                shared->finished_event.set();   // last touch
            }
        };

        coroutine::Task serve_under(std::shared_ptr<Shared> s) {
            Finish finish{s};
            std::stop_callback<Bridge> outer_to_stop{co_await GetStopToken{}, Bridge{s->stop}};
            Nursery children{s->stop.get_token()};
            children.soon(s->transport->serve());
            children.soon(consume(s, &children));
            (void) co_await children.join();
        }

    } // namespace

    std::unique_ptr<Relay> Relay::make(Limits limits, uint64_t head) {
        std::unique_ptr<Relay> r(new Relay);
        r->_shared = std::make_shared<Shared>();
        r->_shared->limits = limits;
        r->_shared->head = head;
        r->_shared->first = head + 1;
        r->_shared->transport = Transport::make(wire::codec(), limits.transport);
        r->_shared->stop = r->_stop;
        return r;
    }

    coroutine::Task Relay::serve() {
        assert(!_served);
        _served = true;
        return serve_under(_shared);
    }

    int Relay::adopt(int fd, bool reference) {
        int id = _shared->transport->adopt(fd);
        // The designation rides the inbox like every other control, after
        // the connection's Opened, which adopt queued first; the consumer
        // alone touches the reference
        if ((id > 0) && reference)
            _shared->transport->post(REFERENCE, u64_bytes((uint64_t)id));
        return id;
    }

    std::expected<uint16_t, std::string> Relay::listen(std::string const& address, uint16_t port) {
        return _shared->transport->listen_tcp(address, port);
    }

    void Relay::pause() {
        _shared->transport->post(PAUSE);
    }

    void Relay::resume() {
        _shared->transport->post(RESUME);
    }

    void Relay::step(uint64_t n) {
        _shared->transport->post(STEP, u64_bytes(n));
    }

    coroutine::Task Relay::shutdown(std::chrono::milliseconds grace) {
        if (!_served)
            co_return;
        _shared->transport->post(SHUTDOWN, u64_bytes((uint64_t)grace.count()));
        co_await _shared->finished_event;
    }

    void Relay::close() {
        _stop.request_stop();
    }

    bool Relay::finished() {
        if (!_served)
            return true;
        std::scoped_lock lock{_shared->mutex};
        return _shared->finished;
    }

    Relay::Clock Relay::clock() {
        std::scoped_lock lock{_shared->mutex};
        return Clock{_shared->head, _shared->paused};
    }

    Relay::~Relay() {
        assert(finished());
    }

    // ----------------------------------------------------------------
    // Tests: a relay driven by raw clients over socket pairs.

    namespace {

        // A raw client: one end of a socket pair served by its own
        // transport, speaking the wire protocol by hand
        struct RawClient {
            std::unique_ptr<Transport> transport;
            uint64_t id = 0;                    // its connection on its own transport
            uint64_t server_id = 0;             // its connection id on the relay
            std::vector<std::string> messages;
            std::vector<Transport::Closed> closed;

            explicit RawClient(Relay& relay, Nursery& scope, bool reference = false) {
                transport = Transport::make(wire::codec(), Transport::Limits{});
                scope.soon(transport->serve());
                int fds[2] = {-1, -1};
                const int paired = ::socketpair(AF_UNIX, SOCK_STREAM, 0, fds);
                assert(paired == 0);
                (void) paired;
                server_id = (uint64_t)relay.adopt(fds[0], reference);
                id = (uint64_t)transport->adopt(fds[1]);
                assert(server_id > 0 && id > 0);
            }

            void send(std::string const& message) {
                transport->send(id, message);
            }

            void drain() {
                for (auto& event : transport->events().try_take()) {
                    if (auto* m = std::get_if<Transport::Message>(&event))
                        messages.push_back(std::move(m->bytes));
                    else if (auto* c = std::get_if<Transport::Closed>(&event))
                        closed.push_back(std::move(*c));
                }
            }

            // The messages of a tag, in order
            std::vector<std::string> of(uint16_t tag) {
                std::vector<std::string> result;
                for (std::string const& m : messages)
                    if (wire::tag_of(m) == tag)
                        result.push_back(m);
                return result;
            }

            void hello() {
                send(wire::encode_hello({wire::PROTOCOL_VERSION, wire::build_id(), 0}));
            }
        };

        // The reason the relay gave a kicked client: the farewell it read
        // before its end of stream.  Loud if the farewell never came
        std::string kick_reason(RawClient& c, const char* who) {
            c.drain();
            std::vector<std::string> kicks = c.of(wire::KICK);
            if (kicks.empty()) {
                fprintf(stderr, "%s: no farewell: %zu messages, closed %zu (%s)\n", who,
                        c.messages.size(), c.closed.size(),
                        c.closed.empty() ? "" : c.closed[0].reason.c_str());
                assert(!"no farewell");
                return {};
            }
            return std::string(wire::decode_kick(kicks[0]).value());
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

        Relay::Limits paused_limits() {
            Relay::Limits limits;
            limits.quantum = std::chrono::hours(1);     // the tests step by hand
            limits.actions_per_step = 2;
            limits.max_pending_actions = 5;
            limits.handshake_deadline = std::chrono::milliseconds(200);
            return limits;
        }

        std::string action_line(int i) {
            return "write Coordinate(" + std::to_string(i) + ", 0) " + std::to_string(i);
        }

    } // namespace

    define_test("relay_handshake_and_join") {
        auto relay = Relay::make(paused_limits());
        Nursery scope;
        scope.soon(relay->serve());
        relay->pause();                     // the PAUSED step is step 1

        RawClient a{*relay, scope};
        a.hello();
        bool welcomed = eventually([&] {
            a.drain();
            return !a.of(wire::WELCOME).empty();
        });
        assert(welcomed);
        (void) welcomed;
        auto welcome = wire::decode_welcome(a.of(wire::WELCOME)[0]);
        assert(welcome && welcome->client == a.server_id);
        assert(welcome->quantum_ns == (uint64_t)std::chrono::nanoseconds(std::chrono::hours(1)).count());
        // The stream so far followed: the PAUSED step, if the pause had
        // been applied before the handshake; either way head agrees
        bool caught_up = eventually([&] {
            a.drain();
            return a.of(wire::STEP).size() >= welcome->head;
        });
        assert(caught_up);
        (void) caught_up;

        // An action before the JOIN is a violation
        RawClient b{*relay, scope};
        b.hello();
        bool b_welcomed = eventually([&] {
            b.drain();
            return !b.of(wire::WELCOME).empty();
        });
        assert(b_welcomed);
        (void) b_welcomed;
        b.send(wire::encode_action(1, action_line(1)));
        bool b_kicked = eventually([&] {
            b.drain();
            return !b.closed.empty();
        });
        assert(b_kicked);
        (void) b_kicked;
        assert(kick_reason(b, "b before join") == "action before join");

        // JOIN_REQUEST rides a step as a JOIN, after which actions are
        // accepted; a second request is idempotent.  (The request travels
        // the socket while a posted step goes straight to the inbox, so
        // the test keeps stepping until the JOIN shows, as the clock would)
        a.send(wire::encode_join_request("a"));
        a.send(wire::encode_join_request("a again"));
        auto join_steps = [&] {
            std::vector<size_t> found;
            std::vector<std::string> steps = a.of(wire::STEP);
            for (size_t i = 0; i != steps.size(); ++i) {
                auto step = wire::decode_step(steps[i]);
                assert(step);
                for (auto& e : step->entries) {
                    auto ev = (e.origin == SERVER_CLIENT_ID) ? wire::decode_event(e.bytes) : std::nullopt;
                    if (ev && ev->kind == Command::JOIN && ev->client == a.server_id)
                        found.push_back(i);
                }
            }
            return found;
        };
        bool joined = eventually([&] {
            relay->step(1);
            a.drain();
            return !join_steps().empty();
        }, std::chrono::seconds(2));
        assert(joined);
        (void) joined;
        assert(join_steps().size() == 1);
        auto join_step = wire::decode_step(a.of(wire::STEP)[join_steps()[0]]);
        assert(join_step && join_step->entries.size() == 1);
        assert(join_step->entries[0].origin == SERVER_CLIENT_ID);
        const size_t before = a.of(wire::STEP).size();

        // Actions finalize in arrival order, a prefix of what was sent:
        // five sent with a per-step cap of two take three steps
        for (int i = 1; i <= 5; ++i)
            a.send(wire::encode_action((uint64_t)i, action_line(i)));
        auto count_mine = [&] {
            size_t mine = 0;
            std::vector<std::string> steps = a.of(wire::STEP);
            for (size_t i = before; i < steps.size(); ++i)
                for (auto& e : wire::decode_step(steps[i])->entries)
                    if (e.origin == a.server_id)
                        ++mine;
            return mine;
        };
        bool queued = eventually([&] {
            relay->step(1);
            a.drain();
            return count_mine() == 5;
        }, std::chrono::seconds(2));
        assert(queued);
        (void) queued;
        std::vector<std::string> steps = a.of(wire::STEP);
        // Collect the client's entries across the steps after the join
        std::vector<uint64_t> seqs;
        for (size_t i = before; i < steps.size(); ++i) {
            auto step = wire::decode_step(steps[i]);
            assert(step);
            size_t mine = 0;
            for (auto& e : step->entries) {
                if (e.origin == a.server_id) {
                    assert(e.bytes == action_line((int)e.seq));
                    seqs.push_back(e.seq);
                    ++mine;
                }
            }
            assert(mine <= 2);
        }
        assert(seqs.size() == 5);
        for (size_t i = 0; i != 5; ++i)
            assert(seqs[i] == i + 1);

        // A bad sequence number is a violation; nothing after it is
        // processed, and the client is gone from the next step
        a.send(wire::encode_action(9, action_line(9)));
        a.send(wire::encode_action(6, action_line(6)));
        bool a_kicked = eventually([&] {
            a.drain();
            return !a.closed.empty();
        });
        assert(a_kicked);
        (void) a_kicked;
        assert(kick_reason(a, "a bad sequence") == "bad sequence number");

        co_await relay->shutdown(std::chrono::milliseconds(100));
        co_await a.transport->shutdown(std::chrono::milliseconds(100));
        co_await b.transport->shutdown(std::chrono::milliseconds(100));
        (void) co_await scope.join();
        assert(relay->finished());
        co_return;
    };

    define_test("relay_flood_leave_and_replay") {
        auto relay = Relay::make(paused_limits());
        Nursery scope;
        scope.soon(relay->serve());
        relay->pause();

        // Two clients join; one floods and is kicked, its LEAVE riding the
        // next step; the other quits, likewise
        RawClient a{*relay, scope};
        RawClient b{*relay, scope};
        a.hello();
        b.hello();
        a.send(wire::encode_join_request("a"));
        b.send(wire::encode_join_request("b"));
        bool welcomed = eventually([&] {
            a.drain();
            b.drain();
            return !a.of(wire::WELCOME).empty() && !b.of(wire::WELCOME).empty();
        });
        assert(welcomed);
        (void) welcomed;
        // The JOINs ride the stream, each once (the two requests arrive on
        // two connections, so in either order, and possibly a step apart)
        auto joins_seen = [&] {
            std::vector<ClientID> joins;
            for (std::string const& m : a.of(wire::STEP)) {
                auto step = wire::decode_step(m);
                assert(step);
                for (auto& e : step->entries)
                    if (e.origin == SERVER_CLIENT_ID) {
                        auto ev = wire::decode_event(e.bytes);
                        if (ev && ev->kind == Command::JOIN)
                            joins.push_back(ev->client);
                    }
            }
            return joins;
        };
        bool joined = eventually([&] {
            relay->step(1);
            a.drain();
            b.drain();
            return joins_seen().size() == 2;
        }, std::chrono::seconds(2));
        assert(joined);
        (void) joined;
        {
            std::vector<ClientID> joins = joins_seen();
            assert(joins.size() == 2 && joins[0] != joins[1]);
            assert(joins[0] == a.server_id || joins[0] == b.server_id);
            assert(joins[1] == a.server_id || joins[1] == b.server_id);
        }
        bool b_joined = eventually([&] {
            a.drain();
            b.drain();
            return b.of(wire::STEP).size() == a.of(wire::STEP).size();
        });
        assert(b_joined);
        (void) b_joined;

        // Actions from two clients all ride the stream, each client's in
        // its own order (across connections arrival order is the relay's
        // to see)
        const size_t steps_before_actions = a.of(wire::STEP).size();
        a.send(wire::encode_action(1, action_line(1)));
        b.send(wire::encode_action(1, action_line(2)));
        a.send(wire::encode_action(2, action_line(3)));
        auto actions_seen = [&] {
            std::vector<std::pair<ClientID, uint64_t>> actions;
            std::vector<std::string> steps = a.of(wire::STEP);
            for (size_t i = steps_before_actions; i < steps.size(); ++i) {
                auto step = wire::decode_step(steps[i]);
                assert(step);
                for (auto& e : step->entries)
                    if (e.origin != SERVER_CLIENT_ID)
                        actions.push_back({e.origin, e.seq});
            }
            return actions;
        };
        bool arrived = eventually([&] {
            relay->step(1);
            a.drain();
            return actions_seen().size() == 3;
        }, std::chrono::seconds(2));
        assert(arrived);
        (void) arrived;
        {
            std::vector<std::pair<ClientID, uint64_t>> actions = actions_seen();
            std::vector<uint64_t> a_seqs, b_seqs;
            for (auto& [client, seq] : actions)
                (client == a.server_id ? a_seqs : b_seqs).push_back(seq);
            assert((a_seqs == std::vector<uint64_t>{1, 2}));
            assert((b_seqs == std::vector<uint64_t>{1}));
        }

        // A flood: more pending actions than the limit allows
        for (int i = 2; i <= 8; ++i)
            b.send(wire::encode_action((uint64_t)i, action_line(i)));
        bool flooded = eventually([&] {
            b.drain();
            return !b.closed.empty();
        });
        assert(flooded);
        (void) flooded;
        assert(kick_reason(b, "b flood") == "flood");

        // a quits; both exits ride the next step as LEAVEs, in order
        a.send(wire::encode_quit());
        bool a_closed = eventually([&] {
            a.drain();
            return !a.closed.empty();
        });
        assert(a_closed);
        (void) a_closed;

        // A late subscriber replays the stream from genesis
        RawClient c{*relay, scope};
        c.hello();
        relay->step(1);
        bool replayed = eventually([&] {
            c.drain();
            if (c.of(wire::WELCOME).empty())
                return false;
            auto welcome = wire::decode_welcome(c.of(wire::WELCOME)[0]);
            return c.of(wire::STEP).size() >= welcome->head && relay->clock().head >= welcome->head;
        });
        assert(replayed);
        (void) replayed;
        std::vector<std::string> steps = c.of(wire::STEP);
        for (size_t i = 0; i != steps.size(); ++i) {
            auto step = wire::decode_step(steps[i]);
            assert(step && step->number == i + 1);
        }
        // The LEAVEs are in the stream: b's kick, then a's quit
        std::vector<ClientID> left;
        for (std::string const& m : steps) {
            auto step = wire::decode_step(m);
            for (auto& e : step->entries)
                if (e.origin == SERVER_CLIENT_ID) {
                    auto ev = wire::decode_event(e.bytes);
                    if (ev && ev->kind == Command::LEAVE)
                        left.push_back(ev->client);
                }
        }
        assert(left.size() == 2 && left[0] == b.server_id && left[1] == a.server_id);

        // Shutdown tells the survivors
        co_await relay->shutdown(std::chrono::milliseconds(200));
        bool told = eventually([&] {
            c.drain();
            return !c.closed.empty();
        });
        assert(told && !c.of(wire::SHUTDOWN).empty());
        (void) told;
        co_await a.transport->shutdown(std::chrono::milliseconds(100));
        co_await b.transport->shutdown(std::chrono::milliseconds(100));
        co_await c.transport->shutdown(std::chrono::milliseconds(100));
        (void) co_await scope.join();
        co_return;
    };

    define_test("relay_hash_tripwire") {
        auto relay = Relay::make(paused_limits());
        Nursery scope;
        scope.soon(relay->serve());
        relay->pause();

        RawClient r{*relay, scope, true};   // the reference
        RawClient a{*relay, scope};
        r.hello();
        a.hello();
        bool welcomed = eventually([&] {
            r.drain();
            a.drain();
            return !r.of(wire::WELCOME).empty() && !a.of(wire::WELCOME).empty();
        });
        assert(welcomed);
        (void) welcomed;
        relay->step(3);
        bool stepped = eventually([&] { return relay->clock().head >= 4; });
        assert(stepped);
        (void) stepped;

        // Agreement in either order is silent; the client is still served
        // afterwards (its JOIN rides a step)
        a.send(wire::encode_hash(1, 0x11));
        r.send(wire::encode_hash(1, 0x11));
        r.send(wire::encode_hash(2, 0x22));
        a.send(wire::encode_hash(2, 0x22));
        a.send(wire::encode_join_request("a"));
        bool joined = eventually([&] {
            relay->step(1);
            a.drain();
            for (std::string const& m : a.of(wire::STEP))
                for (auto& e : wire::decode_step(m)->entries)
                    if (e.origin == SERVER_CLIENT_ID) {
                        auto ev = wire::decode_event(e.bytes);
                        if (ev && ev->kind == Command::JOIN && ev->client == a.server_id)
                            return true;
                    }
            return false;
        }, std::chrono::seconds(2));
        assert(joined && a.closed.empty());
        (void) joined;

        // A disagreement at a step the reference has reported is a kick,
        // whichever report came second
        r.send(wire::encode_hash(3, 0x33));
        a.send(wire::encode_hash(3, 0x99));
        bool desynced = eventually([&] {
            a.drain();
            return !a.closed.empty();
        });
        assert(desynced);
        (void) desynced;
        assert(kick_reason(a, "a desync") == "desync at step 3");

        RawClient b{*relay, scope};
        b.hello();
        b.send(wire::encode_hash(4, 0x44));        // before the reference's
        bool b_welcomed = eventually([&] {
            b.drain();
            return !b.of(wire::WELCOME).empty();
        });
        assert(b_welcomed);
        (void) b_welcomed;
        r.send(wire::encode_hash(4, 0x4444));
        bool b_desynced = eventually([&] {
            b.drain();
            return !b.closed.empty();
        });
        assert(b_desynced);
        (void) b_desynced;
        assert(kick_reason(b, "b desync") == "desync at step 4");

        // A report for a step not yet finalized is a violation
        RawClient c{*relay, scope};
        c.hello();
        c.send(wire::encode_hash(1000, 0x1));
        bool c_kicked = eventually([&] {
            c.drain();
            return !c.closed.empty();
        });
        assert(c_kicked);
        (void) c_kicked;
        assert(kick_reason(c, "c future") == "hash for a step not yet finalized");

        // The reference itself is never kicked for a hash
        r.send(wire::encode_hash(2, 0x2222));      // changes its mind; nobody to disagree with it
        r.send(wire::encode_join_request("r"));
        bool r_alive = eventually([&] {
            relay->step(1);
            r.drain();
            for (std::string const& m : r.of(wire::STEP))
                for (auto& e : wire::decode_step(m)->entries)
                    if (e.origin == SERVER_CLIENT_ID) {
                        auto ev = wire::decode_event(e.bytes);
                        if (ev && ev->kind == Command::JOIN && ev->client == r.server_id)
                            return true;
                    }
            return false;
        }, std::chrono::seconds(2));
        assert(r_alive && r.closed.empty());
        (void) r_alive;

        co_await relay->shutdown(std::chrono::milliseconds(100));
        co_await r.transport->shutdown(std::chrono::milliseconds(100));
        co_await a.transport->shutdown(std::chrono::milliseconds(100));
        co_await b.transport->shutdown(std::chrono::milliseconds(100));
        co_await c.transport->shutdown(std::chrono::milliseconds(100));
        (void) co_await scope.join();

        // Without a reference, disagreement is kept, not judged
        {
            auto lone = Relay::make(paused_limits());
            Nursery lone_scope;
            lone_scope.soon(lone->serve());
            lone->pause();
            RawClient x{*lone, lone_scope};
            RawClient y{*lone, lone_scope};
            x.hello();
            y.hello();
            x.send(wire::encode_hash(1, 0x1));
            y.send(wire::encode_hash(1, 0x2));
            x.send(wire::encode_join_request("x"));
            bool x_alive = eventually([&] {
                lone->step(1);
                x.drain();
                y.drain();
                for (std::string const& m : x.of(wire::STEP))
                    for (auto& e : wire::decode_step(m)->entries)
                        if (e.origin == SERVER_CLIENT_ID) {
                            auto ev = wire::decode_event(e.bytes);
                            if (ev && ev->kind == Command::JOIN && ev->client == x.server_id)
                                return true;
                        }
                return false;
            }, std::chrono::seconds(2));
            assert(x_alive && x.closed.empty() && y.closed.empty());
            (void) x_alive;
            co_await lone->shutdown(std::chrono::milliseconds(100));
            co_await x.transport->shutdown(std::chrono::milliseconds(100));
            co_await y.transport->shutdown(std::chrono::milliseconds(100));
            (void) co_await lone_scope.join();
        }
        co_return;
    };

    define_test("relay_donation_and_gate") {
        auto relay = Relay::make(paused_limits());
        Nursery scope;
        scope.soon(relay->serve());
        relay->pause();

        RawClient r{*relay, scope, true};   // the reference, and so the donor
        r.hello();
        bool r_welcomed = eventually([&] {
            r.drain();
            return !r.of(wire::WELCOME).empty();
        });
        assert(r_welcomed);
        (void) r_welcomed;
        relay->step(2);
        bool stepped = eventually([&] { return relay->clock().head >= 3; });
        assert(stepped);
        (void) stepped;

        // A joiner's HELLO: its WELCOME starts the stream after the head,
        // nothing is replayed, and the reference is asked for a save at
        // the head
        RawClient j{*relay, scope};
        j.hello();
        bool asked = eventually([&] {
            r.drain();
            j.drain();
            return !r.of(wire::REQUEST_SAVE).empty() && !j.of(wire::WELCOME).empty();
        });
        assert(asked);
        (void) asked;
        auto request = wire::decode_save_request(r.of(wire::REQUEST_SAVE)[0]);
        auto welcome = wire::decode_welcome(j.of(wire::WELCOME)[0]);
        assert(request && welcome);
        assert(request->boundary == welcome->head && welcome->first == welcome->head + 1);
        assert(j.of(wire::STEP).empty());
        // The reference got no request for itself
        assert(r.of(wire::REQUEST_SAVE).size() == 1);

        // The donor's frames reach the joiner, in order, by transfer id
        std::string blob(150000, 'z');
        for (size_t i = 0; i != blob.size(); ++i)
            blob[i] = (char)(i * 7);
        r.send(wire::encode_save_begin(request->transfer, request->boundary, blob.size()));
        r.send(wire::encode_save_data(request->transfer, 0, std::string_view(blob).substr(0, 100000)));
        r.send(wire::encode_save_data(request->transfer, 1, std::string_view(blob).substr(100000)));
        r.send(wire::encode_save_end(request->transfer));
        bool delivered = eventually([&] {
            j.drain();
            return !j.of(wire::SAVE_END).empty();
        });
        assert(delivered);
        (void) delivered;
        {
            auto begin = wire::decode_save_begin(j.of(wire::SAVE_BEGIN).at(0));
            assert(begin && begin->transfer == request->transfer && begin->total == blob.size());
            std::vector<std::string> data = j.of(wire::SAVE_DATA);
            assert(data.size() == 2);
            std::string joined;
            for (size_t i = 0; i != 2; ++i) {
                auto d = wire::decode_save_data(data[i]);
                assert(d && d->index == i && d->transfer == request->transfer);
                joined.append(d->bytes);
            }
            assert(joined == blob);
            assert(wire::decode_save_end(j.of(wire::SAVE_END)[0]).value() == request->transfer);
        }
        // A finished transfer takes no more frames
        r.send(wire::encode_save_data(request->transfer, 2, "late"));

        // The gate: a JOIN_REQUEST before any hash report waits; a report
        // within the window of the head opens it
        j.send(wire::encode_join_request("j"));
        auto joins_for = [&](RawClient& c, ClientID who) {
            size_t n = 0;
            for (std::string const& m : c.of(wire::STEP))
                for (auto& e : wire::decode_step(m)->entries)
                    if (e.origin == SERVER_CLIENT_ID) {
                        auto ev = wire::decode_event(e.bytes);
                        if (ev && ev->kind == Command::JOIN && ev->client == who)
                            ++n;
                    }
            return n;
        };
        relay->step(2);
        bool waited = eventually([&] {
            j.drain();
            return j.of(wire::STEP).size() >= 2;
        });
        assert(waited && joins_for(j, j.server_id) == 0);
        (void) waited;
        j.send(wire::encode_hash(welcome->head, 0xabc));
        bool admitted = eventually([&] {
            relay->step(1);
            j.drain();
            return joins_for(j, j.server_id) == 1;
        }, std::chrono::seconds(2));
        assert(admitted);
        (void) admitted;
        assert(j.of(wire::SAVE_DATA).size() == 2);   // the late chunk never came

        // Only the reference may send save frames, and nobody may ask for
        // one
        RawClient x{*relay, scope};
        x.hello();
        x.send(wire::encode_save_data(request->transfer, 0, "junk"));
        RawClient y{*relay, scope};
        y.hello();
        y.send(wire::encode_save_request(1, 1));
        bool refused = eventually([&] {
            x.drain();
            y.drain();
            return !x.closed.empty() && !y.closed.empty();
        });
        assert(refused);
        (void) refused;
        assert(kick_reason(x, "x save frame") == "unexpected message");
        assert(kick_reason(y, "y request") == "unexpected message");

        co_await relay->shutdown(std::chrono::milliseconds(100));
        for (RawClient* c : {&r, &j, &x, &y})
            co_await c->transport->shutdown(std::chrono::milliseconds(100));
        (void) co_await scope.join();
        co_return;
    };

    define_test("relay_clock_and_handshake_deadline") {
        Relay::Limits limits = paused_limits();
        limits.quantum = std::chrono::milliseconds(5);
        auto relay = Relay::make(limits);
        Nursery scope;
        scope.soon(relay->serve());

        // A relay for a loaded or forked World numbers its steps from that
        // World's time
        {
            auto late = Relay::make(limits, 41);
            Nursery late_scope;
            late_scope.soon(late->serve());
            late->pause();
            RawClient d{*late, late_scope};
            d.hello();
            late->step(1);
            bool welcomed = eventually([&] {
                d.drain();
                return !d.of(wire::WELCOME).empty() && d.of(wire::STEP).size() >= 2;
            });
            assert(welcomed);
            (void) welcomed;
            auto welcome = wire::decode_welcome(d.of(wire::WELCOME)[0]);
            assert(welcome && welcome->first == 42 && welcome->head >= 42);
            assert(wire::decode_step(d.of(wire::STEP)[0])->number == 42);
            co_await late->shutdown(std::chrono::milliseconds(100));
            co_await d.transport->shutdown(std::chrono::milliseconds(100));
            (void) co_await late_scope.join();
        }

        // The clock runs from the start
        bool ticking = eventually([&] { return relay->clock().head >= 3; });
        assert(ticking);
        (void) ticking;
        relay->pause();
        bool paused = eventually([&] { return relay->clock().paused; });
        assert(paused);
        (void) paused;
        uint64_t head = relay->clock().head;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        assert(relay->clock().head == head);           // stopped
        relay->step(4);
        bool stepped = eventually([&] { return relay->clock().head == head + 4; });
        assert(stepped && relay->clock().paused);
        (void) stepped;
        relay->resume();
        bool running = eventually([&] { return !relay->clock().paused && relay->clock().head > head + 4; });
        assert(running);
        (void) running;

        // A subscriber sees PAUSED and RESUMED in the stream; a silent
        // connection is kicked at the handshake deadline
        RawClient a{*relay, scope};
        a.hello();
        RawClient silent{*relay, scope};
        bool seen = eventually([&] {
            a.drain();
            silent.drain();
            return !silent.closed.empty() && a.of(wire::STEP).size() >= head + 4;
        });
        assert(seen);
        (void) seen;
        assert(kick_reason(silent, "silent") == "handshake timeout");
        bool paused_seen = false, resumed_seen = false;
        for (std::string const& m : a.of(wire::STEP)) {
            auto step = wire::decode_step(m);
            for (auto& e : step->entries) {
                auto ev = wire::decode_event(e.bytes);
                if (ev && ev->kind == Command::PAUSED) paused_seen = true;
                if (ev && ev->kind == Command::RESUMED) resumed_seen = true;
            }
        }
        assert(paused_seen && resumed_seen);

        // Brutal close ends the task without waiting for anyone
        relay->close();
        bool ended = eventually([&] { return relay->finished(); });
        assert(ended);
        (void) ended;
        co_await a.transport->shutdown(std::chrono::milliseconds(100));
        co_await silent.transport->shutdown(std::chrono::milliseconds(100));
        (void) co_await scope.join();
        co_return;
    };

} // namespace wry
