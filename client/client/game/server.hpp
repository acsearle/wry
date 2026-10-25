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
    // A client SUBMITs the local player's actions and TAKEs back the
    // finalized steps, each the authoritative, ordered commands of one
    // step across all clients and the server; `World::step(commands)`
    // applies exactly one.  The server is who orders them: the Relay
    // (game/relay.hpp), in this process over a socket pair for single
    // player and a host, or across the network for a joined game.  The
    // implementation of this seam is the client side of that connection
    // (RemoteServer, game/host.hpp) in every case: the multiplayer change
    // is off the simulation.

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

    // A finalized step: its number (the World's time after applying it)
    // and its commands in the server's order
    struct Step {
        uint64_t number = 0;
        std::vector<Command> commands;
    };

    struct Server {
        virtual ~Server() = default;

        // Local client -> server: the local player wants to do this.
        // False if it cannot travel: a Term with no literal form, or a
        // line over the wire's cap (an action held until our JOIN has
        // ridden the stream counts as submitted)
        virtual bool submit(Player::Action action) = 0;

        // Local client -> server: this client wants a Player (it has
        // subscribed, or has just installed a World).  Answered by a JOIN
        // in a later step, which the World applies idempotently: a client
        // the World already maps keeps its Player.
        virtual void request_join() = 0;

        // Local client -> server: the World's content hash after applying
        // `step` (multiplayer.md section 5).  The relay compares it with
        // the reference client's report for the same step and kicks a
        // client that disagrees; it is also the client's liveness and lag.
        virtual void report_hash(uint64_t step, uint64_t hash) = 0;

        // Server -> client: the finalized steps that have arrived since the
        // last call, in order: zero or more.  Main thread.
        virtual std::vector<Step> take_steps() = 0;

        // Whether take_steps would find anything (steps, or the
        // connection's own news): the headless host runs a frame when it
        // would
        virtual bool has_input() = 0;
    };

} // namespace wry

#endif /* server_hpp */
