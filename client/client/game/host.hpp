//
//  host.hpp
//  client
//
//  Created by Antony Searle on 2026-10-21.
//
//  The client side of the seam (game/server.hpp) and the host that
//  pairs it with a relay (game/docs/multiplayer.md sections 2 and 8).
//
//  RemoteServer is this process's connection to a Relay: over a socket
//  pair to the relay in this process (single player, or the host's own
//  client), or over TCP to another process (a joined game).  Its
//  transport's loop runs on the pool; the main thread takes the inbox
//  once per frame (take_steps), runs the client's state machine on it --
//  the handshake, the JOIN, the finalized steps -- and decodes STEPs into
//  Steps for the World.
//
//  Host is a Relay plus a RemoteServer over a socket pair: what a process
//  is when it plays alone or hosts others.  There is no process that is
//  a relay and nothing else.
//

#ifndef host_hpp
#define host_hpp

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "coroutine.hpp"
#include "nursery.hpp"
#include "relay.hpp"
#include "server.hpp"
#include "transport.hpp"

namespace wry {

    class RemoteServer final : public Server {
    public:

        enum class State : uint8_t {
            CONNECTING,     // no connection yet (dialing)
            HANDSHAKE,      // HELLO sent, WELCOME awaited
            SUBSCRIBED,     // receiving the stream; no Player yet
            JOINED,         // our JOIN has ridden the stream: actions go
            DISCONNECTED,   // over: reason() says why
        };

        // Its transport's loop is spawned into `scope` (the process scope)
        static std::unique_ptr<RemoteServer> make(coroutine::Nursery& scope,
                                                  Transport::Limits limits = {});

        // Connect: one end of a socket pair, or a TCP peer (dialed on the
        // pool; the outcome shows in state()).  One connection per
        // RemoteServer
        int adopt(int fd);
        void dial(std::string address, uint16_t port);

        // The seam
        void submit(Player::Action action) override;
        void request_join() override;
        std::vector<Step> take_steps() override;
        bool has_input() override;

        State state() const { return _state; }
        ClientID client_id() const { return _client_id; }
        uint64_t head() const { return _next_step - 1; }   // the last step taken
        std::string const& reason() const { return _reason; }

        // Tell the relay we are leaving.  Its end of stream follows
        void leave();

        // Give up on the connection for a reason of our own (the stream
        // does not continue our World)
        void disconnect(std::string reason);

        // Graceful end of the connection and its loop; resolves once the
        // loop has ended.  At most one waiter
        [[nodiscard]] coroutine::Task shutdown(std::chrono::milliseconds grace);
        void close();
        bool finished();

        // Asserts finished()
        ~RemoteServer();

        RemoteServer(RemoteServer const&) = delete;
        RemoteServer& operator=(RemoteServer const&) = delete;

    private:
        RemoteServer() = default;

        void _send(std::string message);
        void _disconnect(std::string reason);
        void _on_message(std::string const& bytes);

        coroutine::Nursery* _scope = nullptr;
        std::shared_ptr<Transport> _transport;  // shared with a dial in flight
        std::stop_source _dial_stop;
        uint64_t _connection = 0;
        State _state = State::CONNECTING;
        ClientID _client_id = SERVER_CLIENT_ID;
        uint64_t _quantum_ns = 0;
        uint64_t _next_step = 1;                // the step we expect next
        uint64_t _seq = 0;                      // our last action's
        bool _join_requested = false;
        bool _join_sent = false;
        std::vector<Player::Action> _queued;    // submitted before JOINED
        std::vector<Step> _steps;
        std::string _reason;
    };

    class Host {
    public:

        // A relay and its local client over a socket pair, their loops
        // spawned into `scope`; the relay's stream starts after `head`
        // (Relay::make).  Null, with `error` set, on failure
        static std::unique_ptr<Host> make(coroutine::Nursery& scope, Relay::Limits limits,
                                          std::string& error, uint64_t head = 0);

        Relay& relay() { return *_relay; }
        RemoteServer& client() { return *_client; }

        // Both, gracefully, in order: the relay (SHUTDOWN to everyone,
        // the client's connection included), then the client
        [[nodiscard]] coroutine::Task shutdown(std::chrono::milliseconds grace);
        bool finished();

    private:
        Host() = default;
        std::unique_ptr<Relay> _relay;
        std::unique_ptr<RemoteServer> _client;
    };

} // namespace wry

#endif /* host_hpp */
