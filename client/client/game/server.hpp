//
//  server.hpp
//  client
//
//  Created by Antony Searle on 2026-06-27.
//

#ifndef server_hpp
#define server_hpp

#include <cstdint>
#include <utility>
#include <vector>

#include "entity.hpp"   // EntityID
#include "player.hpp"   // Player::Action
#include "utility.hpp"

namespace wry {

    // The seam between local input and the deterministic simulation
    // (game/docs/multiplayer.md).
    //
    // A client SUBMITs the local player's actions and POLLs back the
    // authoritative, ordered commands of the next step, across all clients
    // and the server; `World::step(commands)` applies exactly one step's
    // worth.  The Server is who orders them: locally for single player, or
    // the networked server's finalized STEP for multiplayer.  That keeps the
    // multiplayer change off the simulation: only the Server implementation
    // swaps.
    //
    // The deterministic-lockstep details a real server owns -- which step a
    // command lands on, input delay, ordering across peers -- all hide behind
    // this interface.

    // The server's name for a connection.  0 is the server itself, the
    // origin of the server events; clients are numbered from 1.  The map
    // from client id to Player EntityID is World state
    // (World::_player_for_client), written by JOIN and LEAVE.
    using ClientID = uint64_t;
    inline constexpr ClientID SERVER_CLIENT_ID = 0;

    // One entry of a finalized step: a client's action, or a server event
    // (multiplayer.md section 3).  Everything that mutates the World or its
    // clock rides as one of these.
    struct Command {
        enum Kind : uint8_t {
            ACTION,     // `action`, from `origin`, its `seq`-th
            JOIN,       // the server spawns a Player for `client`
            LEAVE,      // `client` has left; its Player idles in place
            PAUSED,     // the step clock stopped after this step
            RESUMED,    // the step clock runs again
        };
        Kind kind = ACTION;
        ClientID origin = SERVER_CLIENT_ID;     // who sent it
        uint64_t seq = 0;                       // the origin's sequence number
        ClientID client = SERVER_CLIENT_ID;     // JOIN / LEAVE: the client concerned
        Player::Action action;                  // ACTION
    };

    inline Command make_action_command(ClientID origin, uint64_t seq, Player::Action action) {
        Command c;
        c.kind = Command::ACTION;
        c.origin = origin;
        c.seq = seq;
        c.action = std::move(action);
        return c;
    }

    inline Command make_join_command(ClientID client) {
        Command c;
        c.kind = Command::JOIN;
        c.client = client;
        return c;
    }

    inline Command make_leave_command(ClientID client) {
        Command c;
        c.kind = Command::LEAVE;
        c.client = client;
        return c;
    }

    struct Server {
        virtual ~Server() = default;

        // Local client -> server: the local player wants to do this.
        virtual void submit(Player::Action action) = 0;

        // Local client -> server: this client wants a Player (it has
        // subscribed, or has just installed a World).  Answered by a JOIN
        // in a later step, which the World applies idempotently: a client
        // the World already maps keeps its Player.
        virtual void request_join() = 0;

        // Server -> client: the ordered commands to apply for the next step.
        //
        // NOTE (future): once the Server is networked, commands trickle in
        // non-deterministically and the next step's set may not be ready yet --
        // poll() will need a "not ready" signal (e.g. optional, or a separate
        // readiness query).  That is deliberately entangled with decoupling
        // render from step-advancement and is out of scope here; LocalServer is
        // always ready, so the plain vector suffices for now.
        virtual std::vector<Command> poll() = 0;
    };

    // Single-player: the local client is client 1; submit buffers its
    // actions, numbered, and poll hands them straight back (trivially
    // "ordered").  A requested JOIN is a step of its own, before any action
    // that was pending: the Player it spawns first acts in the step after.
    struct LocalServer final : Server {

        static constexpr ClientID LOCAL_CLIENT_ID = 1;

        void submit(Player::Action action) override {
            _pending.push_back(make_action_command(LOCAL_CLIENT_ID, ++_seq, std::move(action)));
        }

        void request_join() override {
            _join_requested = true;
        }

        std::vector<Command> poll() override {
            if (std::exchange(_join_requested, false))
                return { make_join_command(LOCAL_CLIENT_ID) };
            return take(_pending);
        }

        uint64_t _seq = 0;
        bool _join_requested = false;
        std::vector<Command> _pending;
    };

} // namespace wry

#endif /* server_hpp */
