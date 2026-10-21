//
//  relay.hpp
//  client
//
//  Created by Antony Searle on 2026-10-21.
//
//  The server (game/docs/multiplayer.md sections 2, 3, 4, 7 and 8):
//  orderer, relay and clock.  It holds no World and does not simulate.
//  It is one sequential task consuming one inbox -- its Transport's
//  events, into which its own timers and controls are posted -- so it is
//  a state machine over its connections with no locks of its own, which
//  is what keeps the trust rules checkable.
//
//  Per connection it keeps a state (HELLO pending, subscribed, joined), a
//  next-expected sequence number, and the count of its actions waiting
//  for a step.  At each finalization it emits STEP{n, commands}: the
//  server events that fell due (JOIN, LEAVE, PAUSED, RESUMED), then the
//  clients' actions in arrival order, each client limited to
//  `actions_per_step` with the rest deferred, never dropped from the
//  middle; a client's finalized actions are exactly a prefix of what it
//  submitted.  The clock is a timer at a fixed quantum; the host's clock
//  commands pause it, resume it, or finalize n steps at once.
//
//  Every violation kicks: a message out of place for the state, a bad
//  sequence number, more pending actions than `max_pending_actions`
//  (a flood), a malformed payload, a handshake that does not arrive.
//  The transport bounds and the codec caps cover the rest.  The relay
//  never reads an action's body.
//
//  The stream since the game began is kept, and a late subscriber gets
//  it from step 1: in this revision every client starts from the
//  scenario's genesis and replays (multiplayer.md 9.5 replaces this with
//  a donated save and the stream from its boundary).
//

#ifndef relay_hpp
#define relay_hpp

#include <chrono>
#include <cstdint>
#include <expected>
#include <memory>
#include <stop_token>
#include <string>

#include "coroutine.hpp"
#include "transport.hpp"

namespace wry {

    class Relay {
    public:

        struct Limits {
            std::chrono::nanoseconds quantum{1000000000 / 60};
            size_t actions_per_step = 64;                       // per client per step
            size_t max_pending_actions = 1024;                  // per client; beyond is a flood
            std::chrono::milliseconds handshake_deadline{5000};
            Transport::Limits transport{};
        };

        // The stream starts after `head`: step head + 1 is the first
        // finalized.  0 for a new game; a loaded or forked World's time
        // otherwise, since steps are numbered by the World time they
        // produce
        static std::unique_ptr<Relay> make(Limits limits, uint64_t head = 0);

        // The task: its transport's loop and its own consumer, as a task
        // for the owner to spawn into the process scope.  The clock runs
        // from the start.  Once only
        [[nodiscard]] coroutine::Task serve();

        // Serve an already connected descriptor as a client (the host's
        // own client's end of a socket pair): its connection id, which is
        // its client id, or -errno
        int adopt(int fd);

        // Listen for remote clients: the bound port, or an error
        std::expected<uint16_t, std::string> listen(std::string const& address, uint16_t port);

        // The clock, from the host (the clock commands).  pause finalizes
        // one more step carrying PAUSED and stops the timer; resume
        // restarts it, the next step carrying RESUMED; step(n) finalizes n
        // steps at once, paused or not
        void pause();
        void resume();
        void step(uint64_t n);

        // Graceful end: SHUTDOWN to every client, the transport's own
        // graceful shutdown, then the task ends.  Resolves once it has.
        // At most one waiter
        [[nodiscard]] coroutine::Task shutdown(std::chrono::milliseconds grace);

        // Brutal: cancels everything.  The spawner's join waits for it
        void close();

        // The task has ended, or was never started
        bool finished();

        struct Clock {
            uint64_t head;
            bool paused;
        };
        Clock clock();

        // Asserts finished()
        ~Relay();

        Relay(Relay const&) = delete;
        Relay& operator=(Relay const&) = delete;

        struct Shared;

    private:
        Relay() = default;

        std::shared_ptr<Shared> _shared;
        std::stop_source _stop;
        bool _served = false;
    };

} // namespace wry

#endif /* relay_hpp */
