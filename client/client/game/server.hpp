//
//  server.hpp
//  client
//
//  Created by Antony Searle on 2026-06-27.
//

#ifndef server_hpp
#define server_hpp

#include <utility>
#include <vector>

#include "entity.hpp"   // EntityID
#include "player.hpp"   // Player::Action

namespace wry {

    // The seam between local input and the deterministic simulation.
    //
    // A client SUBMITs the local player's actions and POLLs back the
    // authoritative, ordered set of commands (across all players) to apply for
    // the next step.  `World::step()` is unchanged underneath: `Player::_queue`
    // remains the per-step command intake, and the Server is merely who FILLS
    // it -- locally for single player, or via the network for multiplayer.
    // That keeps the multiplayer change off the simulation: only the Server
    // implementation swaps.  A new game gets a LocalServer; a joined game gets a
    // networked one.
    //
    // The deterministic-lockstep details a real server owns -- which step a
    // command lands on, input delay, ordering across peers -- all hide behind
    // this interface.

    struct Command {
        EntityID player;            // who issued it (the player entity's id)
        Player::Action action;
    };

    struct Server {
        virtual ~Server() = default;

        // Local client -> server: the local player wants to do this.
        virtual void submit(Player::Action action) = 0;

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

    // Single-player: submit just buffers the local player's actions, and poll
    // hands them straight back (trivially "ordered").
    struct LocalServer final : Server {
        explicit LocalServer(EntityID localPlayer)
        : _localPlayer(localPlayer) {}

        void submit(Player::Action action) override {
            _pending.push_back(Command{_localPlayer, std::move(action)});
        }

        std::vector<Command> poll() override {
            return std::exchange(_pending, {});
        }

        EntityID _localPlayer;
        std::vector<Command> _pending;
    };

} // namespace wry

#endif /* server_hpp */
