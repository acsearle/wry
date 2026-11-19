//
//  render_clock.cpp
//  client
//
//  Created by Antony Searle on 2026-11-20.
//

#include "render_clock.hpp"

#include <algorithm>
#include <cmath>
#include <random>
#include <vector>

#include "test.hpp"

namespace wry {

    void RenderClock::reset() {
        _offsets.clear();
        _have_base = false;
        _base = 0.0;
        _spread = 0.0;
        _delay = _limits.initial_delay;
        _paused = false;
        _have_last = false;
        _last = 0;
        _recovering = false;
        _have_position = false;
        _position = 0.0;
        _last_t = 0.0;
    }

    void RenderClock::set_quantum(double seconds) {
        if (seconds > 0.0 && seconds != _limits.quantum) {
            _limits.quantum = seconds;
            _rebase();
        }
    }

    // The schedule is not what the window says: start learning it again.
    // The delay is kept, and falls only at its own rate
    void RenderClock::_rebase() {
        _offsets.clear();
        _have_base = false;
        _recovering = false;
    }

    void RenderClock::_learn(double offset) {
        _offsets.push_back(offset);
        while (_offsets.size() > _limits.window)
            _offsets.pop_front();
        auto [lo, hi] = std::minmax_element(_offsets.begin(), _offsets.end());
        _base = *lo;
        _have_base = true;
        _spread = (*hi - *lo) / _limits.quantum;
        // Rise at once
        _delay = std::max(_delay, std::min(_spread + _limits.margin, _limits.max_delay));
    }

    void RenderClock::arrived(uint64_t n, double t, bool paused) {
        if (_have_last && n <= _last)
            return;
        _have_last = true;
        _last = n;
        const double q = _limits.quantum;
        const double offset = t - (double)n * q;
        if (paused) {
            // Off the schedule: a pause's last step, or one stepped while
            // paused.  Drawn at once (advance), and not learned
            _paused = true;
            return;
        }
        if (_paused) {
            // Resumed: the relay's timer restarted, so the schedule moved
            _paused = false;
            _rebase();
        }
        if (!_have_base) {
            _learn(offset);
            return;
        }
        const double late = offset - _base;
        if (late < -0.5 * q) {
            // Earlier than any step in the window: the window was late,
            // not jittery -- a backlog draining (a join or load catching
            // up), or a faster path.  Start again from this one
            _rebase();
            _learn(offset);
            return;
        }
        if (late > _limits.gap || (_recovering && late > (_spread + 0.125) * q)) {
            // A gap (an outage, a stall), and the burst that ends it, are
            // not jitter: the clock fast-forwards over them (advance), and
            // learns again once arrivals are back within the spread it
            // has learned (the burst's in-order tail is the gap's too).  A
            // gap that has not closed in a quarter of the window is a
            // schedule that moved: rebase
            if (!_recovering) {
                _recovering = true;
                _recovering_since = t;
            }
            if (t - _recovering_since < 0.25 * (double)_limits.window * q)
                return;
            _rebase();
            _learn(offset);
            return;
        }
        _recovering = false;
        _learn(offset);
    }

    double RenderClock::target(double t) const {
        return (t - _base) / _limits.quantum - _delay;
    }

    double RenderClock::advance(double t, uint64_t oldest, uint64_t newest) {
        const double q = _limits.quantum;
        const double dt = _have_position ? std::max(0.0, t - _last_t) : 0.0;
        _last_t = t;

        // Fall slowly, to what the spread asks
        const double wanted = std::min(_spread + _limits.margin, _limits.max_delay);
        if (_delay > wanted)
            _delay = std::max(wanted, _delay - _limits.delay_decay * dt);

        double r;
        bool snapped = false;
        if (!_have_base || _paused) {
            // Nothing on the schedule to follow (the first step of a
            // lineage, or the stream paused): the newest, at rest, so a
            // paused world stands still and a stepped one shows the step
            r = (double)newest;
        } else if (!_have_position) {
            r = target(t);
        } else {
            // How far the target is from where the clock would be now at
            // the step rate
            const double error = target(t) - (_position + dt / q);
            if (error > _limits.snap) {
                // A big gap: fast-forward
                r = target(t);
                snapped = true;
            } else {
                // Rubber-band: run slightly fast or slow
                const double rate = 1.0 + std::clamp(error * q / _limits.slew_time,
                                                     -_limits.slew, _limits.slew);
                r = _position + dt / q * rate;
            }
            if (r > (double)newest)
                ++_underruns;
        }
        if (_have_position)
            r = std::max(r, _position);      // never backwards
        r = std::min(r, (double)newest);     // only what has arrived
        r = std::max(r, (double)oldest - 1.0 + 1.0 / 1024.0);  // only what is kept
        // A jump only if it went further than the fastest slew would
        if (snapped && r > _position + dt / q * (1.0 + _limits.slew))
            ++_snaps;
        _position = r;
        _have_position = true;
        return r;
    }

    // ---- Tests ----------------------------------------------------------

    namespace {

        struct Arrival {
            uint64_t n;
            double t;
            bool paused = false;
        };

        struct Frame {
            double t;
            double r;
            uint64_t newest;
            double delay;
            uint64_t underruns;
            double target;
        };

        // Feed `arrivals` (in order) to `clock` as frames at `frames` come
        // round, each drawing from the newest 64 arrived
        std::vector<Frame> simulate(RenderClock& clock,
                                    std::vector<Arrival> const& arrivals,
                                    std::vector<double> const& frames) {
            std::vector<Frame> out;
            size_t next = 0;
            uint64_t newest = 0;
            bool any = false;
            for (double t : frames) {
                while (next < arrivals.size() && arrivals[next].t <= t) {
                    clock.arrived(arrivals[next].n, arrivals[next].t, arrivals[next].paused);
                    newest = arrivals[next].n;
                    any = true;
                    ++next;
                }
                if (!any)
                    continue;
                const uint64_t oldest = newest > 63 ? newest - 63 : 0;
                const double r = clock.advance(t, oldest, newest);
                out.push_back({t, r, newest, clock.delay(), clock.underruns(), clock.target(t)});
            }
            return out;
        }

        // In-order delivery: a step cannot overtake the one before it
        void deliver(std::vector<Arrival>& a, uint64_t n, double t, bool paused = false) {
            if (!a.empty())
                t = std::max(t, a.back().t);
            a.push_back({n, t, paused});
        }

        std::vector<double> frames_at(double hz, double from, double to, std::mt19937_64& rng,
                                      double jitter = 0.0005) {
            std::uniform_real_distribution<double> u(-jitter, jitter);
            std::vector<double> f;
            for (double t = from; t < to; t += 1.0 / hz)
                f.push_back(t + u(rng));
            std::sort(f.begin(), f.end());
            return f;
        }

        // Between consecutive frames: the clock's rate as a multiple of
        // the step rate
        double rate(Frame const& a, Frame const& b, double q) {
            return (b.r - a.r) / ((b.t - a.t) / q);
        }

    } // namespace

    // Jittered arrivals drawn at 120 Hz: once the delay has learned the
    // jitter, the clock never runs dry, runs within its slew of the step
    // rate, and holds a delay just over the worst lateness seen; when the
    // jitter stops the delay falls, slowly, to its margin
    define_test("render_clock_jitter") {
        const double q = 1.0 / 60.0;
        RenderClock clock;
        clock.reset();
        std::mt19937_64 rng(7);
        std::uniform_real_distribution<double> late(0.0, 0.005);
        std::vector<Arrival> a;
        const uint64_t jittery = 20 * 60;          // 20 s jittery
        const uint64_t calm = 40 * 60;             // then 20 s calm
        for (uint64_t n = 1; n <= calm; ++n) {
            double d = (n <= jittery) ? late(rng) : 0.0;
            if (n <= jittery && n % 50 == 0)
                d = 0.015;
            deliver(a, n, (double)n * q + 0.002 + d);
        }
        std::vector<Frame> f = simulate(clock, a, frames_at(120.0, 0.0, (double)calm * q, rng));
        require(f.size() > 1000);
        double worst_lag = 0.0;
        uint64_t underruns_at_warmup = 0;
        for (size_t i = 1; i < f.size(); ++i) {
            require(f[i].r >= f[i - 1].r);
            require(f[i].r <= (double)f[i].newest);
            if (f[i].t < 3.0) {
                underruns_at_warmup = f[i].underruns;
                continue;
            }
            const double k = rate(f[i - 1], f[i], q);
            require(k >= 0.9 - 1e-9 && k <= 1.1 + 1e-9);
            // drawn no more than the delay plus a step behind the newest
            worst_lag = std::max(worst_lag, (double)f[i].newest - f[i].r);
            if (f[i].t < 20.0) {
                // the 15 ms spikes are learned, and nothing much beyond
                require(f[i].delay >= 0.015 / q
                        && f[i].delay <= 0.017 / q + clock.limits().margin + 0.01);
            }
        }
        require(worst_lag < 3.0);
        // no frame after warm-up wanted a step that had not arrived
        require(f.back().underruns == underruns_at_warmup);
        require(clock.snaps() == 0);
        // calm for 20 s: back to the margin
        require(std::abs(f.back().delay - clock.limits().margin) < 1e-9);
        co_return;
    };

    // A one-second outage, then the burst that ends it: the clock holds
    // the last step through it (underruns), fast-forwards over the gap,
    // and does not learn the outage as jitter
    define_test("render_clock_gap") {
        const double q = 1.0 / 60.0;
        RenderClock clock;
        clock.reset();
        std::mt19937_64 rng(11);
        std::vector<Arrival> a;
        const uint64_t stall_from = 5 * 60, stall_to = 6 * 60;
        for (uint64_t n = 1; n <= 12 * 60; ++n) {
            double t = (double)n * q + 0.002;
            if (n > stall_from && n <= stall_to)
                t = (double)stall_to * q + 0.002 + 0.001 * (double)(n - stall_from);   // the burst
            deliver(a, n, t);
        }
        std::vector<Frame> f = simulate(clock, a, frames_at(60.0, 0.0, 12.0, rng));
        uint64_t underruns_before = 0, underruns_after = 0;
        for (size_t i = 1; i < f.size(); ++i) {
            require(f[i].r >= f[i - 1].r);
            require(f[i].r <= (double)f[i].newest);
            if (f[i].t < (double)stall_from * q)
                underruns_before = f[i].underruns;
            if (f[i].t < (double)stall_to * q + 0.5)
                underruns_after = f[i].underruns;
            if (f[i].t > (double)stall_to * q + 0.5) {
                require(f[i].delay < clock.limits().margin + 0.5);  // the outage was not learned
                require((double)f[i].newest - f[i].r < 2.0);
            }
        }
        // ran dry only in the outage; jumped over it, more than once
        // only while its burst was still landing
        require(f.back().underruns == underruns_after);
        require(underruns_after > underruns_before);
        require(clock.snaps() >= 1 && clock.snaps() <= 5);
        co_return;
    };

    // Pause, a stepped burst while paused, resume: paused, the newest step
    // is drawn exactly (a still frame, and `step n` shows its step); after
    // the resume the clock follows the new schedule without a jump or a
    // learned delay
    define_test("render_clock_pause") {
        const double q = 1.0 / 60.0;
        RenderClock clock;
        clock.reset();
        std::mt19937_64 rng(13);
        std::vector<Arrival> a;
        const uint64_t P = 3 * 60;
        for (uint64_t n = 1; n <= P; ++n)
            deliver(a, n, (double)n * q + 0.002, n == P);
        // `step 3` while paused, at 4 s, at once
        for (uint64_t n = P + 1; n <= P + 3; ++n)
            deliver(a, n, 4.0 + 0.001 * (double)(n - P), true);
        // resume at 6 s
        for (uint64_t n = P + 4; n <= P + 4 + 4 * 60; ++n)
            deliver(a, n, 6.0 + (double)(n - P - 3) * q + 0.002);
        std::vector<Frame> f = simulate(clock, a, frames_at(60.0, 0.0, 10.0, rng));
        for (size_t i = 1; i < f.size(); ++i) {
            require(f[i].r >= f[i - 1].r);
            if (f[i].t > (double)P * q + 0.003 && f[i].t < 4.0)
                require(f[i].r == (double)P);
            if (f[i].t > 4.01 && f[i].t < 6.0)
                require(f[i].r == (double)(P + 3));
            if (f[i].t > 6.5)
                require(f[i].delay < 2.01 && (double)f[i].newest - f[i].r < 3.0 && f[i].r <= (double)f[i].newest);
        }
        require(clock.snaps() == 0);
        co_return;
    };

    // A joiner's catch-up: a backlog applied faster than real time, then
    // the stream on schedule.  Arriving early is not jitter: the delay
    // never rises, and the clock settles a delay behind the head
    define_test("render_clock_catch_up") {
        const double q = 1.0 / 60.0;
        RenderClock clock;
        clock.reset();
        std::mt19937_64 rng(17);
        std::vector<Arrival> a;
        for (uint64_t n = 1001; n <= 1300; ++n)
            deliver(a, n, 0.001 * (double)(n - 1000));
        for (uint64_t n = 1301; n <= 1300 + 5 * 60; ++n)
            deliver(a, n, 0.3 + (double)(n - 1300) * q + 0.002);
        std::vector<Frame> f = simulate(clock, a, frames_at(60.0, 0.0, 5.0, rng));
        for (size_t i = 1; i < f.size(); ++i) {
            require(f[i].r >= f[i - 1].r);
            require(f[i].delay <= clock.limits().initial_delay + 1e-9);
            if (f[i].t > 2.5)
                require((double)f[i].newest - f[i].r <= f[i].delay + 1.0);
        }
        co_return;
    };

} // namespace wry
