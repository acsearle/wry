//
//  render_clock.hpp
//  client
//
//  Created by Antony Searle on 2026-11-20.
//
//  The render clock (docs/step_pump.md section 7, multiplayer.md open
//  item 11.6): which moment of the world a frame draws.  An audio-style
//  jitter buffer: steps become drawable at uneven wall times (the
//  network, the relay's timer, the pump's own step times), and the
//  frame does not draw the newest of them, but a steady clock running
//  at the step rate a smoothed delay behind the earliest each could
//  have arrived.  The delay adapts to the jitter observed, rising at
//  once and falling slowly; the clock corrects by running slightly fast
//  or slow, never by jumping, except across a big gap.
//
//  Pure arithmetic on wall times and step numbers, no World: the main
//  thread feeds it each step's arrival as it adopts the pump's
//  snapshot, and asks it once per frame for the position to draw.
//  Positions are fractional step numbers r: World ceil(r) drawn at time
//  r, a fraction of a step before its own.  Every move a machine is in
//  in World m was decided at m - 1 or before (a step from m - 1 runs at
//  now() == m - 1), so World m knows where everything is over
//  (m - 1, m], and consecutive Worlds agree where they meet.
//

#ifndef render_clock_hpp
#define render_clock_hpp

#include <cstdint>
#include <deque>

namespace wry {

    class RenderClock {
    public:

        struct Limits {
            double quantum = 1.0 / 60.0;    // seconds per step: the relay's
            size_t window = 120;            // arrivals the estimates span (2 s)
            double margin = 1.0;            // steps of delay beyond the jitter seen:
                                            // a whole step, since the newest step
                                            // arrived is drawable only up to its time
            double initial_delay = 2.0;     // steps, before any spread is seen
            double max_delay = 30.0;        // steps
            double delay_decay = 1.0;       // steps per second the delay may fall
            double gap = 0.25;              // seconds late beyond which an arrival
                                            // is a discontinuity, not jitter
            double slew = 0.1;              // the most the clock runs fast or slow
            double slew_time = 0.5;         // seconds: a correction's time constant
            double snap = 8.0;              // steps behind its target at which
                                            // the clock jumps (a big gap: an
                                            // outage, a catch-up), rather
                                            // than take 10 x as long to slew
        };

        RenderClock() { reset(); }
        explicit RenderClock(Limits limits) : _limits(limits) { reset(); }

        // Forget everything: a new lineage (a World installed).  The next
        // position is the newest step
        void reset();

        // Step `n` became drawable at wall time `t` (seconds, any steady
        // epoch).  `paused`: the stream's clock had stopped after it (a
        // PAUSED step, or one of a `step n` burst while paused), so it was
        // not on the relay's schedule.  In order; a step not after the
        // last one taken (a World edited in place, `corrupt`) is ignored
        void arrived(uint64_t n, double t, bool paused);

        // The position to draw at wall time `t`, given the kept steps
        // [oldest, newest]: World ceil(r) drawn at r, so r is at most
        // `newest`, and never decreases between resets.  Exactly
        // `newest` while the stream is paused.  Once per frame
        double advance(double t, uint64_t oldest, uint64_t newest);

        // The position the clock steers for at `t`: the delay behind the
        // newest step the schedule says has arrived
        double target(double t) const;

        double position() const { return _position; }
        double delay() const { return _delay; }        // steps
        double spread() const { return _spread; }      // steps: the jitter seen
        uint64_t underruns() const { return _underruns; }
        uint64_t snaps() const { return _snaps; }
        Limits const& limits() const { return _limits; }
        void set_quantum(double seconds);

    private:
        void _rebase();
        void _learn(double offset);

        Limits _limits;
        // The window's arrival offsets, t - n * quantum: when the step
        // arrived relative to the relay's schedule plus an unknown
        // constant.  Their minimum is the schedule (_base): the earliest
        // any step arrived.  Their spread is the jitter
        std::deque<double> _offsets;
        double _base = 0.0;
        bool _have_base = false;
        double _spread = 0.0;           // steps: max - min offset, in the window
        double _delay = 0.0;            // steps
        bool _paused = false;           // the newest arrival's
        bool _have_last = false;
        uint64_t _last = 0;             // the last step taken
        bool _recovering = false;       // since a gap, not yet back on schedule
        double _recovering_since = 0.0; // the gap's arrival time
        bool _have_position = false;
        double _position = 0.0;
        double _last_t = 0.0;
        uint64_t _underruns = 0;        // frames the newest step was not enough
        uint64_t _snaps = 0;            // jumps forward
    };

} // namespace wry

#endif /* render_clock_hpp */
