//
//  kqueue_reactor_model.cpp
//  client
//
//  Exhaustive-interleaving model check of the CancelableKEventFrame
//  protocol (kqueue_reactor.cpp).  No kqueue, no threads: an abstract
//  model of the frame's parties, enumerated state by state.
//

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_set>
#include <vector>

#include "test.hpp"

// ==== What is modelled ======================================================
//
// One CancelableKEventFrame<ContinuationThunk>, armed by spawn, raced by a
// stop request and by its knote firing.  Four parties on three threads:
//
//   S  the spawner (thread S): spawn(), then the awaitable's tail
//   K  a canceller (thread K): stop_source::request_stop(), libc++'s loop
//      around invoking a registered callback
//   C  the Callback body: runs on thread K when request_stop pops it, or
//      inline on thread S when the token was already stopped at emplace
//   R  the fire (thread R): the reactor's harvest of the one-shot knote and
//      static_resume on the pool, merged into one thread (the reactor ->
//      work-queue -> pool edge is taken as sequenced-before)
//
// Each step is one access to shared state: an atomic operation, or a
// non-atomic access to the frame or the awaiting coroutine that matters.
// The shared state:
//
//   atomic  _state                  RMW only (all relaxed), plus, in the
//                                   variants below, spawn's STARTED as a
//                                   release and C's acquire garnish load
//   atomic  _reference_count_minus_one   RMW only, plus the deleter's
//                                   acquire load
//   kq      the knote (none / armed / harvested); every kevent call is an
//           RMW on it.  Synchronization per the reactor's ORDER contract:
//           ADD is a release, the harvest an acquire, a receipt-0 DELETE an
//           acquire; a failed ADD and an ENOENT DELETE synchronize with
//           nothing (weaker than "kevent is a mutex", so it is the
//           conservative reading)
//   ss      libc++'s stop_state word: stop bit + this callback's list
//           membership (registered / popped).  Its lock is an acquire RMW
//           and its unlock a release, so each locked section is one acq_rel
//           RMW here
//   done    stop_callback::__completed_: K's release store after the
//           invocation, the remover's acquire wait
//   non-atomic locations:  FIELDS (_stop_token, _change), OPT (the
//           optional's engaged flag), NODE (the stop_callback's payload and
//           __destroyed_), F (_f), STORAGE (the frame's memory, which every
//           atomic access to the frame reads and delete writes), CO (the
//           awaiting coroutine: suspended by S, resumed or destroyed once)
//
// The libc++ stop_callback semantics modelled are those of the shipped
// headers (__stop_token/stop_state.h): registration under the lock or the
// inline invocation if stop is already requested; request_stop pops under
// the lock, unlocks, invokes, then (unless the callback destroyed itself on
// this thread) stores __completed_ with release; __remove_callback unlinks a
// registered callback, or waits on __completed_ (acquire) for one that was
// popped, or, on the requesting thread itself, sets *__destroyed_.
//
// ==== Properties ============================================================
//
// Checked on every step:
//   - no access to the frame after it is freed (the last-touch rule), and
//     the frame is freed at most once
//   - the reference count never goes below zero owners
//   - the frame is never freed while its knote is armed (the reactor would
//     deliver a dangling udata)
//   - the callable is consumed at most once (invoked, destroyed with the
//     frame, or handed back to the caller)
//   - each of RESUMED and STOPPED is set at most once
//   - in RA mode (below): every pair of conflicting non-atomic accesses is
//     ordered by happens-before; the free is a write of every frame location
// Checked in every terminal state (no party can step):
//   - no party is stuck mid-protocol (no deadlock)
//   - if stop was requested, or the knote fired, or the ADD failed: the
//     frame is freed and the continuation was consumed exactly once (no
//     lost wakeup, no lost cancellation, no leak)
//   - otherwise the frame is parked: knote armed, frame alive, continuation
//     held (a wait that is never satisfied is legitimate)
//   - the outcome matches the race winner: the continuation was invoked iff
//     RESUMED was set before STOPPED; destroyed iff STOPPED was first;
//     handed back iff the ADD failed
//
// ==== The two modes =========================================================
//
// SC: plain interleaving semantics.  The values every step reads are those
//     of the interleaving; no happens-before bookkeeping.  This settles the
//     protocol logic: ownership, fates, the winner, the last-touch rule in
//     interleaving order.
//
// RA: the same interleavings, but happens-before is computed only from the
//     synchronization the code actually has (vector clocks: sequenced-before
//     plus release -> acquire, with C++20 release sequences: a relaxed RMW
//     continues the release sequences before it, so an acquire that reads
//     it synchronizes with every release head earlier in the chain), and
//     plain loads may read any message coherence allows, not only the
//     latest.  The data-race and use-after-free checks then use that
//     happens-before, not the interleaving order.
//
// Why interleavings suffice for RA here.  Every write to every atomic
// location in the model is an RMW (the plain loads write nothing; the done
// flag has a single store).  An RMW reads its immediate modification-order
// predecessor (atomicity), so modification order on such a location is
// contained in (reads-from)+.  Hence sb | rf | mo is contained in
// (sb | rf)+, which RC11 requires to be acyclic (its no-thin-air axiom), so
// every RC11-consistent execution has a topological order that is an
// interleaving in which each RMW reads the latest value -- exactly what the
// enumeration generates.  Plain loads are not RMWs, so they are given every
// message from their coherence floor (the latest message whose write
// happens-before them, or that they already saw) to the latest.
// Conversely every enumerated execution is RC11-consistent (hb is contained
// in the interleaving order), so a counterexample is a real execution.
//
// ==== The gap to C++ ========================================================
//
//   - C++ (unlike RC11) does not forbid sb | rf cycles for relaxed atomics
//     (load buffering, out-of-thin-air).  An execution where a relaxed RMW
//     reads from a write that depends on the RMW's own sb-successors is
//     outside this model.  No mainstream implementation produces OOTA;
//     whether load buffering through RMWs is observable on Apple silicon I
//     do not know (ARMv8 permits LB for plain accesses architecturally).
//   - The kernel is abstracted as an RMW location with the ORDER contract's
//     edges; the reactor -> pool hop as sequenced-before; libc++'s spinlock
//     as acq_rel RMWs.  The model's soundness is relative to those.
//   - One canceller and one stop request; the pool thread that runs
//     static_resume is never the requesting thread (libc++'s same-thread
//     __destroyed_ path is modelled only for the canceller deleting the
//     frame from inside its own callback, the one place it arises here).
//   - notify_all after the __completed_ store touches the atomic's address
//     after a waiter may have freed it (libc++'s, not modelled).
//   - Field granularity: FIELDS, NODE, F are each one location; finer races
//     inside them are not distinguished.  The callable is opaque: invoking
//     it is a write of F and of CO.
//   - No SC atomics or fences exist in the protocol, so RC11's SC axiom is
//     not modelled.

namespace wry {

    namespace {

        namespace kevent_model {

            constexpr int NT = 3;   // threads
            enum : uint8_t { T_S = 0, T_K = 1, T_R = 2, T_NONE = 255 };
            constexpr int NP = 4;   // parties
            enum : uint8_t { P_S = 0, P_K = 1, P_R = 2, P_C = 3 };

            enum : uint8_t { L_FIELDS, L_OPT, L_NODE, L_F, L_STORAGE, L_CO, NL };
            enum : uint8_t { A_STATE, A_RC, A_KQ, A_SS, A_DONE, NA };

            // _state bits, as CancelableKEventFrame
            enum : uint8_t { RESUMED = 1, STOPPED = 2, STARTED = 4 };
            // knote
            enum : uint8_t { KN_NONE = 0, KN_ARMED = 1, KN_HARVESTED = 2 };
            // stop_state word (this callback's view of it)
            enum : uint8_t { SS_STOP = 1, SS_REG = 2, SS_POPPED = 4 };
            // the reference count is stored biased so underflow is visible
            constexpr uint8_t RC_BIAS = 16;

            // continuation fates
            enum : uint8_t { FATE_NONE, FATE_INVOKED_R, FATE_INVOKED_S, FATE_DESTROYED, FATE_RETURNED };
            // first of RESUMED / STOPPED
            enum : uint8_t { FIRST_NONE, FIRST_R, FIRST_C };

            enum Mutation : uint8_t {
                M_NONE,
                M_RC_RELAXED,           // release(): fetch_sub relaxed, not release
                M_RC_NO_ACQUIRE,        // release(): no acquire re-load before delete
                M_ADD_RELAXED,          // the ADD publishes nothing (no tsan-helper / kernel edge)
                M_C_LEAKS_KNOTE_OWNER,  // Callback wins the DELETE but does not release fire's share
                M_R_NO_RESET,           // static_resume does not destroy the stop_callback first
                M_R_IGNORES_STOPPED,    // static_resume inherits the callback's share unconditionally
                M_S_NO_DOOMED_DELETE,   // spawn sees STOPPED after the ADD but leaves the knote armed
                // Strengthenings, not weakenings.  Both must also pass.
                M_OLD_PROTOCOL,         // the code before 2026-11-10: spawn's fetch_or(STARTED) is a release AND
                                        // the Callback re-loads _state with acquire (the garnish) before its own
                                        // release() when it saw STARTED and not RESUMED
                M_C_GARNISH,            // the garnish alone, STARTED relaxed: a negative control (the garnish
                                        // then acquires only clockless messages and synchronizes with nothing)
            };

            struct Config {
                bool canceller;   // a stop request is made (at any point)
                bool fires;       // the knote fires once armed (else: never)
                bool ra;          // RA mode (else SC)
                Mutation mutation;
            };

            constexpr int MAXM = 7;
            struct Msg { uint8_t value, wt, we, clk[NT]; };
            struct Atomic { uint8_t n; Msg m[MAXM]; uint8_t seen[NT]; };
            struct NonAtomic { uint8_t wt, we, re[NT]; };

            // party program counters
            enum : uint8_t {
                S_CTOR, S_EMPLACE, S_PAUSED, S_ADD, S_FAIL_REL_R, S_FAIL_RESET,
                S_STARTED, S_DEL, S_DEL_REL_R, S_INVOKE, S_REL_OWN,
                S_FAIL_REL_C, S_FAIL_MOVE, S_FAIL_REL_OWN, S_FAIL_RESUME, S_DONE,
            };
            enum : uint8_t { K_REQ, K_PAUSED, K_AFTER, K_DONE };
            enum : uint8_t { R_HARVEST, R_RESET, R_OR, R_REL_C, R_INVOKE, R_REL_OWN, R_DONE };
            enum : uint8_t { C_IDLE, C_START, C_DEL, C_DEL_REL_R, C_GARNISH, C_REL_OWN, C_DONE };

            // Only uint8_t members: no padding, so bytewise hash/compare is exact
            struct State {
                uint8_t pc[NP], sub[NP];
                uint8_t c_thread;
                uint8_t s_ok, s_before, c_before, r_before;
                uint8_t k_destroyed;
                uint8_t cb_engaged, cb_has_state;
                uint8_t f_holds, fate, fates, dead, frees, first, add_done, fired;
                uint8_t vc[NT][NT];
                Atomic a[NA];
                NonAtomic na[NL];
            };

            struct StateHash {
                size_t operator()(State const& s) const {
                    uint64_t h = 14695981039346656037ull;
                    auto p = (unsigned char const*)&s;
                    for (size_t i = 0; i != sizeof(State); ++i)
                        h = (h ^ p[i]) * 1099511628211ull;
                    return (size_t)h;
                }
            };
            struct StateEq {
                bool operator()(State const& a, State const& b) const {
                    return std::memcmp(&a, &b, sizeof(State)) == 0;
                }
            };

            // coverage events: the checker asserts the interesting paths
            // were all reached, so a pass is not vacuous
            enum : uint32_t {
                EV_C_INLINE = 1u << 0,
                EV_C_ON_K = 1u << 1,
                EV_C_DEL_WON = 1u << 2,
                EV_C_DEL_ENOENT = 1u << 3,
                EV_S_DEL_WON = 1u << 4,
                EV_S_DEL_ENOENT = 1u << 5,
                EV_R_INVOKE = 1u << 6,
                EV_S_INVOKE = 1u << 7,
                EV_DESTROYED = 1u << 8,
                EV_RETURNED = 1u << 9,
                EV_WAITED_COMPLETED = 1u << 10,
                EV_K_DESTROYED_FLAG = 1u << 11,
                EV_GARNISH_STALE = 1u << 12,
                EV_PARKED = 1u << 13,
                EV_ADD_FAILED = 1u << 14,
                EV_R_SAW_UNSTARTED = 1u << 15,
                EV_GARNISH = 1u << 16,
                EV_GARNISH_SYNC = 1u << 17,     // RA: the garnish's acquire advanced the Callback's view of S
                EV_GARNISH_NO_SYNC = 1u << 18,  // RA: a garnish whose acquire added no happens-before edge
            };

            struct Ctx {
                Config const* cfg;
                char const* viol = nullptr;
                char const* what = "";
                uint32_t events = 0;
                char buf[160] = {};
            };

            char const* const thread_name[NT] = { "S", "K", "R" };
            char const* const loc_name[NL] = { "FIELDS", "OPT", "NODE", "F", "STORAGE", "CO" };

            // ---- happens-before bookkeeping (RA mode only) ----------------

            void tick(State& s, Ctx& c, int t) {
                if (c.cfg->ra)
                    ++s.vc[t][t];
            }

            bool hb(State const& s, Ctx& c, int t, uint8_t wt, uint8_t we) {
                if (!c.cfg->ra || wt == T_NONE || wt == t)
                    return true;
                return s.vc[t][wt] >= we;
            }

            void violate(Ctx& c, char const* v) {
                if (!c.viol)
                    c.viol = v;
            }

            void race(Ctx& c, int loc, int t, int u, char const* kind) {
                if (c.viol)
                    return;
                std::snprintf(c.buf, sizeof c.buf, "data race (%s) on %s: thread %s vs thread %s",
                              kind, loc_name[loc], thread_name[t], thread_name[u]);
                c.viol = c.buf;
            }

            bool frame_location(int loc) {
                return loc != L_CO;
            }

            void na_read(State& s, Ctx& c, int t, int loc) {
                if (frame_location(loc) && s.dead)
                    violate(c, "use after free: frame read after delete (last-touch rule)");
                NonAtomic& x = s.na[loc];
                if (!hb(s, c, t, x.wt, x.we))
                    race(c, loc, t, x.wt, "write-read");
                if (c.cfg->ra)
                    x.re[t] = s.vc[t][t];
            }

            void na_write(State& s, Ctx& c, int t, int loc) {
                if (frame_location(loc) && s.dead)
                    violate(c, "use after free: frame written after delete (last-touch rule)");
                NonAtomic& x = s.na[loc];
                if (!hb(s, c, t, x.wt, x.we))
                    race(c, loc, t, x.wt, "write-write");
                for (int u = 0; u != NT; ++u)
                    if (u != t && x.re[u] && c.cfg->ra && s.vc[t][u] < x.re[u])
                        race(c, loc, t, u, "read-write");
                if (c.cfg->ra) {
                    x.wt = (uint8_t)t;
                    x.we = s.vc[t][t];
                    std::memset(x.re, 0, sizeof x.re);
                }
            }

            // ---- atomic locations -------------------------------------------

            void join(State& s, int t, uint8_t const* clk) {
                for (int u = 0; u != NT; ++u)
                    if (clk[u] > s.vc[t][u])
                        s.vc[t][u] = clk[u];
            }

            // Initialization of an atomic: a non-atomic write, no release
            void atomic_init(State& s, Ctx& c, int t, int a, uint8_t value) {
                Atomic& x = s.a[a];
                x = {};
                x.n = 1;
                x.m[0].value = value;
                x.m[0].wt = c.cfg->ra ? (uint8_t)t : (uint8_t)T_NONE;
                x.m[0].we = c.cfg->ra ? s.vc[t][t] : 0;
            }

            void append(State& s, Ctx& c, int t, int a, uint8_t value, uint8_t const* clk) {
                Atomic& x = s.a[a];
                if (x.n == MAXM) {
                    violate(c, "model limit: modification order longer than MAXM");
                    return;
                }
                Msg& m = x.m[x.n];
                m.value = value;
                m.wt = c.cfg->ra ? (uint8_t)t : (uint8_t)T_NONE;
                m.we = c.cfg->ra ? s.vc[t][t] : 0;
                std::memcpy(m.clk, clk, NT);
                x.seen[t] = x.n;
                ++x.n;
            }

            // An RMW reads the latest message.  Acquire joins its clock.
            // The new message carries the release-sequence clock: the old
            // one (a relaxed RMW continues every release sequence before
            // it), joined with the thread's own if this RMW is a release.
            uint8_t rmw(State& s, Ctx& c, int t, int a, bool acquire, bool release, uint8_t value_or, uint8_t value_and = 0xFF, int8_t add = 0) {
                Atomic& x = s.a[a];
                assert(x.n);
                Msg const old = x.m[x.n - 1];
                if (c.cfg->ra && acquire)
                    join(s, t, old.clk);
                uint8_t clk[NT];
                std::memcpy(clk, old.clk, NT);
                if (c.cfg->ra && release)
                    for (int u = 0; u != NT; ++u)
                        if (s.vc[t][u] > clk[u])
                            clk[u] = s.vc[t][u];
                uint8_t nv = (uint8_t)(((old.value & value_and) | value_or) + add);
                append(s, c, t, a, nv, clk);
                return old.value;
            }

            // A read with no write (a failed kevent, the inline-invoke
            // check): reads the latest, synchronizes with nothing
            uint8_t peek(State const& s, int a) {
                Atomic const& x = s.a[a];
                return x.m[x.n - 1].value;
            }

            // The messages a plain load of `a` by `t` may read: from the
            // coherence floor to the latest
            int load_floor(State const& s, Ctx& c, int t, int a) {
                Atomic const& x = s.a[a];
                if (!c.cfg->ra)
                    return x.n - 1;
                int lo = x.seen[t];
                for (int i = 0; i != x.n; ++i)
                    if (hb(s, c, t, x.m[i].wt, x.m[i].we) && i > lo)
                        lo = i;
                return lo;
            }

            int load_options(State const& s, Ctx& c, int t, int a) {
                return s.a[a].n - load_floor(s, c, t, a);
            }

            uint8_t load(State& s, Ctx& c, int t, int a, bool acquire, int choice) {
                Atomic& x = s.a[a];
                int i = load_floor(s, c, t, a) + choice;
                assert(i < x.n);
                if (c.cfg->ra && acquire)
                    join(s, t, x.m[i].clk);
                x.seen[t] = (uint8_t)i;
                return x.m[i].value;
            }

            // ---- shared procedures --------------------------------------------

            Mutation mut(Ctx& c) { return c.cfg->mutation; }

            // spawn's fetch_or(STARTED) is a release
            bool started_release(Ctx& c) { return mut(c) == M_OLD_PROTOCOL; }

            // the Callback runs the garnish (when it saw STARTED, not RESUMED)
            bool garnishes(Ctx& c) { return mut(c) == M_OLD_PROTOCOL || mut(c) == M_C_GARNISH; }

            void consume(State& s, Ctx& c, int t, uint8_t fate) {
                if (!s.f_holds)
                    violate(c, "continuation consumed twice or after being moved out");
                s.f_holds = 0;
                s.fate = fate;
                ++s.fates;
                na_write(s, c, t, L_CO);
            }

            // CancelableKEventFrame::release(), and delete this, in steps.
            // Returns the number of choices (0: disabled); sets done.
            int release_step(State& s, Ctx& c, int p, int t, int choice, bool& done) {
                done = false;
                switch (s.sub[p]) {
                    case 0: {
                        tick(s, c, t);
                        c.what = "release: _reference_count_minus_one.fetch_sub(1, release)";
                        na_read(s, c, t, L_STORAGE);
                        bool rel = mut(c) != M_RC_RELAXED;
                        int old = (int)rmw(s, c, t, A_RC, false, rel, 0, 0xFF, -1) - RC_BIAS;
                        if (old < 0)
                            violate(c, "over-release: reference count below zero owners");
                        if (old != 0) {
                            done = true;
                        } else {
                            s.sub[p] = 1;
                        }
                        return 1;
                    }
                    case 1: {
                        int n = load_options(s, c, t, A_RC);
                        tick(s, c, t);
                        c.what = "release: last owner; _reference_count_minus_one.load(acquire)";
                        na_read(s, c, t, L_STORAGE);
                        load(s, c, t, A_RC, mut(c) != M_RC_NO_ACQUIRE, choice);
                        s.sub[p] = 2;
                        return n;
                    }
                    case 2: {
                        tick(s, c, t);
                        c.what = "delete this: ~optional<stop_callback> (__remove_callback under the lock)";
                        na_read(s, c, t, L_OPT);
                        if (s.cb_engaged && s.cb_has_state) {
                            uint8_t ss = peek(s, A_SS);
                            if (ss & SS_REG) {
                                rmw(s, c, t, A_SS, true, true, 0, (uint8_t)~SS_REG);
                                s.sub[p] = 4;
                            } else if (ss & SS_POPPED) {
                                rmw(s, c, t, A_SS, true, true, 0);
                                if (t == T_K) {
                                    // the callback is deleting itself on the
                                    // requesting thread: *__destroyed_ = true
                                    na_read(s, c, t, L_NODE);
                                    s.k_destroyed = 1;
                                    c.events |= EV_K_DESTROYED_FLAG;
                                    s.sub[p] = 4;
                                } else {
                                    s.sub[p] = 3;
                                }
                            } else {
                                violate(c, "stop_callback engaged but neither registered nor popped");
                            }
                        } else {
                            s.sub[p] = 4;
                        }
                        return 1;
                    }
                    case 3: {
                        if (s.a[A_DONE].n < 2)
                            return 0;  // __completed_.wait(false)
                        tick(s, c, t);
                        c.what = "delete this: __completed_ observed true (acquire)";
                        na_read(s, c, t, L_STORAGE);
                        load(s, c, t, A_DONE, true, s.a[A_DONE].n - 1 - load_floor(s, c, t, A_DONE));
                        c.events |= EV_WAITED_COMPLETED;
                        s.sub[p] = 4;
                        return 1;
                    }
                    case 4: {
                        tick(s, c, t);
                        c.what = "delete this: free the frame";
                        if (s.dead)
                            violate(c, "double free of the frame");
                        if (peek(s, A_KQ) == KN_ARMED)
                            violate(c, "frame freed while its knote is armed (dangling udata)");
                        na_write(s, c, t, L_FIELDS);
                        na_write(s, c, t, L_OPT);
                        na_write(s, c, t, L_NODE);
                        na_write(s, c, t, L_F);
                        na_write(s, c, t, L_STORAGE);
                        if (s.f_holds) {
                            // ~ContinuationThunk destroys the coroutine
                            consume(s, c, t, FATE_DESTROYED);
                            c.events |= EV_DESTROYED;
                        }
                        s.dead = 1;
                        ++s.frees;
                        s.cb_engaged = 0;
                        s.sub[p] = 0;
                        done = true;
                        return 1;
                    }
                }
                return 0;
            }

            // _stop_callback.reset()
            int reset_step(State& s, Ctx& c, int p, int t, bool& done) {
                done = false;
                switch (s.sub[p]) {
                    case 0: {
                        tick(s, c, t);
                        c.what = "_stop_callback.reset()";
                        na_read(s, c, t, L_OPT);
                        if (!s.cb_engaged) {
                            done = true;
                            return 1;
                        }
                        if (s.cb_has_state) {
                            uint8_t ss = peek(s, A_SS);
                            if (ss & SS_REG) {
                                c.what = "_stop_callback.reset(): unlinked under the lock, never to run";
                                rmw(s, c, t, A_SS, true, true, 0, (uint8_t)~SS_REG);
                            } else if (ss & SS_POPPED) {
                                c.what = "_stop_callback.reset(): popped by request_stop; wait for __completed_";
                                rmw(s, c, t, A_SS, true, true, 0);
                                if (t == T_K)
                                    violate(c, "model: reset on the requesting thread");
                                s.sub[p] = 1;
                                return 1;
                            } else {
                                violate(c, "stop_callback engaged but neither registered nor popped");
                            }
                        }
                        na_write(s, c, t, L_NODE);
                        na_write(s, c, t, L_OPT);
                        s.cb_engaged = 0;
                        done = true;
                        return 1;
                    }
                    case 1: {
                        if (s.a[A_DONE].n < 2)
                            return 0;
                        tick(s, c, t);
                        c.what = "_stop_callback.reset(): __completed_ observed true (acquire); destroy";
                        na_read(s, c, t, L_STORAGE);
                        load(s, c, t, A_DONE, true, s.a[A_DONE].n - 1 - load_floor(s, c, t, A_DONE));
                        c.events |= EV_WAITED_COMPLETED;
                        na_write(s, c, t, L_NODE);
                        na_write(s, c, t, L_OPT);
                        s.cb_engaged = 0;
                        s.sub[p] = 0;
                        done = true;
                        return 1;
                    }
                }
                return 0;
            }

            // kevent EV_DELETE|EV_RECEIPT: 0 if the knote was armed, else ENOENT
            bool delete_knote(State& s, Ctx& c, int t) {
                na_read(s, c, t, L_FIELDS);  // _change, this
                if (peek(s, A_KQ) == KN_ARMED) {
                    rmw(s, c, t, A_KQ, true, false, 0, 0, KN_NONE);
                    return true;
                }
                rmw(s, c, t, A_KQ, false, false, 0);
                return false;
            }

            // ---- the parties --------------------------------------------------

            void start_c(State& s, uint8_t t) {
                s.c_thread = t;
                s.pc[P_C] = C_START;
            }

            int step_S(State& s, Ctx& c, int choice) {
                int const t = T_S;
                bool done = false;
                int n = 1;
                switch (s.pc[P_S]) {
                    case S_CTOR:
                        tick(s, c, t);
                        c.what = "spawn: new frame (stop_token, _change, _f); the coroutine is suspended";
                        na_write(s, c, t, L_FIELDS);
                        na_write(s, c, t, L_OPT);
                        na_write(s, c, t, L_F);
                        na_write(s, c, t, L_STORAGE);
                        na_write(s, c, t, L_CO);
                        atomic_init(s, c, t, A_STATE, 0);
                        atomic_init(s, c, t, A_RC, RC_BIAS + 2);
                        s.f_holds = 1;
                        s.pc[P_S] = S_EMPLACE;
                        return 1;
                    case S_EMPLACE:
                        tick(s, c, t);
                        na_write(s, c, t, L_NODE);
                        atomic_init(s, c, t, A_DONE, 0);
                        if (peek(s, A_SS) & SS_STOP) {
                            c.what = "spawn: _stop_callback.emplace: stop already requested, Callback runs inline";
                            s.cb_has_state = 0;
                            s.pc[P_S] = S_PAUSED;
                            start_c(s, T_S);
                            c.events |= EV_C_INLINE;
                        } else {
                            c.what = "spawn: _stop_callback.emplace: registered under the stop_state lock";
                            rmw(s, c, t, A_SS, true, true, SS_REG);
                            s.cb_has_state = 1;
                            na_write(s, c, t, L_OPT);
                            s.cb_engaged = 1;
                            s.pc[P_S] = S_ADD;
                        }
                        return 1;
                    case S_PAUSED:
                        return 0;
                    case S_ADD:
                        tick(s, c, t);
                        na_read(s, c, t, L_FIELDS);
                        s.add_done = 1;
                        if (choice == 0) {
                            c.what = "spawn: kevent EV_ADD -> 0 (armed)";
                            rmw(s, c, t, A_KQ, false, mut(c) != M_ADD_RELAXED, 0, 0, KN_ARMED);
                            s.s_ok = 1;
                            s.pc[P_S] = S_STARTED;
                        } else {
                            c.what = "spawn: kevent EV_ADD -> errno (not armed)";
                            (void)peek(s, A_KQ);
                            s.s_ok = 0;
                            c.events |= EV_ADD_FAILED;
                            s.pc[P_S] = S_FAIL_REL_R;
                        }
                        return 2;
                    case S_FAIL_REL_R:
                        n = release_step(s, c, P_S, t, choice, done);
                        if (done) s.pc[P_S] = S_FAIL_RESET;
                        return n;
                    case S_FAIL_RESET:
                        n = reset_step(s, c, P_S, t, done);
                        if (done) s.pc[P_S] = S_STARTED;
                        return n;
                    case S_STARTED: {
                        tick(s, c, t);
                        bool const rel = started_release(c);
                        c.what = rel ? "spawn: _state.fetch_or(STARTED, release)"
                                     : "spawn: _state.fetch_or(STARTED, relaxed)";
                        na_read(s, c, t, L_STORAGE);
                        uint8_t b = rmw(s, c, t, A_STATE, false, rel, STARTED);
                        s.s_before = b;
                        bool resumed = b & RESUMED, stopped = b & STOPPED;
                        if (s.s_ok) {
                            if (stopped) {
                                bool del = !resumed && mut(c) != M_S_NO_DOOMED_DELETE;
                                s.pc[P_S] = del ? S_DEL : S_REL_OWN;
                            } else if (resumed) {
                                s.pc[P_S] = S_INVOKE;
                            } else {
                                s.pc[P_S] = S_REL_OWN;
                            }
                        } else {
                            if (resumed)
                                violate(c, "RESUMED although the ADD failed");
                            s.pc[P_S] = stopped ? S_FAIL_MOVE : S_FAIL_REL_C;
                        }
                        return 1;
                    }
                    case S_DEL:
                        tick(s, c, t);
                        if (delete_knote(s, c, t)) {
                            c.what = "spawn: saw STOPPED; kevent EV_DELETE -> 0 (the doomed knote is withdrawn)";
                            c.events |= EV_S_DEL_WON;
                            s.pc[P_S] = S_DEL_REL_R;
                        } else {
                            c.what = "spawn: saw STOPPED; kevent EV_DELETE -> ENOENT (it fired)";
                            c.events |= EV_S_DEL_ENOENT;
                            s.pc[P_S] = S_REL_OWN;
                        }
                        return 1;
                    case S_DEL_REL_R:
                        n = release_step(s, c, P_S, t, choice, done);
                        if (done) s.pc[P_S] = S_REL_OWN;
                        return n;
                    case S_INVOKE:
                        tick(s, c, t);
                        c.what = "spawn: saw RESUMED before STARTED; std::invoke(_f)";
                        na_write(s, c, t, L_F);
                        consume(s, c, t, FATE_INVOKED_S);
                        c.events |= EV_S_INVOKE;
                        s.pc[P_S] = S_REL_OWN;
                        return 1;
                    case S_REL_OWN:
                        n = release_step(s, c, P_S, t, choice, done);
                        if (done) s.pc[P_S] = S_DONE;
                        return n;
                    case S_FAIL_REL_C:
                        n = release_step(s, c, P_S, t, choice, done);
                        if (done) s.pc[P_S] = S_FAIL_MOVE;
                        return n;
                    case S_FAIL_MOVE:
                        tick(s, c, t);
                        c.what = "spawn: F temp(std::move(_f))";
                        na_write(s, c, t, L_F);
                        if (!s.f_holds)
                            violate(c, "callable moved out after being consumed");
                        s.f_holds = 0;
                        s.pc[P_S] = S_FAIL_REL_OWN;
                        return 1;
                    case S_FAIL_REL_OWN:
                        n = release_step(s, c, P_S, t, choice, done);
                        if (done) s.pc[P_S] = S_FAIL_RESUME;
                        return n;
                    case S_FAIL_RESUME:
                        tick(s, c, t);
                        c.what = "await_suspend: Unarmed; the awaiter resumes its own coroutine";
                        s.fate = FATE_RETURNED;
                        ++s.fates;
                        na_write(s, c, t, L_CO);
                        c.events |= EV_RETURNED;
                        s.pc[P_S] = S_DONE;
                        return 1;
                    case S_DONE:
                        return 0;
                }
                return 0;
            }

            int step_K(State& s, Ctx& c, int /*choice*/) {
                int const t = T_K;
                switch (s.pc[P_K]) {
                    case K_REQ: {
                        tick(s, c, t);
                        uint8_t ss = peek(s, A_SS);
                        if (ss & SS_REG) {
                            c.what = "request_stop: set stop, pop the Callback under the lock; invoke";
                            rmw(s, c, t, A_SS, true, true, SS_STOP | SS_POPPED, (uint8_t)~SS_REG);
                            // after the unlock: __cb->__destroyed_ = &local; __cb->__invoke()
                            na_write(s, c, t, L_NODE);
                            s.pc[P_K] = K_PAUSED;
                            start_c(s, T_K);
                            c.events |= EV_C_ON_K;
                        } else {
                            c.what = "request_stop: set stop; no callback registered";
                            rmw(s, c, t, A_SS, true, true, SS_STOP);
                            s.pc[P_K] = K_DONE;
                        }
                        return 1;
                    }
                    case K_PAUSED:
                        return 0;
                    case K_AFTER:
                        tick(s, c, t);
                        if (!s.k_destroyed) {
                            c.what = "request_stop: __destroyed_ = nullptr; __completed_.store(true, release)";
                            na_write(s, c, t, L_NODE);
                            na_read(s, c, t, L_STORAGE);
                            append(s, c, t, A_DONE, 1, s.vc[t]);
                        } else {
                            c.what = "request_stop: the callback destroyed itself; touch nothing";
                        }
                        s.pc[P_K] = K_DONE;
                        return 1;
                    case K_DONE:
                        return 0;
                }
                return 0;
            }

            void finish_c(State& s, Ctx& c) {
                s.pc[P_C] = C_DONE;
                if (s.c_thread == T_S) {
                    // the stop_callback constructor returns; the optional is
                    // engaged (its __state_ is null: it ran inline)
                    na_write(s, c, T_S, L_OPT);
                    s.cb_engaged = 1;
                    s.pc[P_S] = S_ADD;
                } else {
                    s.pc[P_K] = K_AFTER;
                }
            }

            int step_C(State& s, Ctx& c, int choice) {
                int const t = s.c_thread;
                bool done = false;
                int n = 1;
                switch (s.pc[P_C]) {
                    case C_IDLE:
                        return 0;
                    case C_START: {
                        tick(s, c, t);
                        c.what = "Callback: _state.fetch_or(STOPPED, relaxed)";
                        na_read(s, c, t, L_NODE);    // the Callback object
                        na_read(s, c, t, L_FIELDS);  // assert(_stop_token.stop_requested())
                        na_read(s, c, t, L_STORAGE);
                        uint8_t b = rmw(s, c, t, A_STATE, false, false, STOPPED);
                        s.c_before = b;
                        if (b & STOPPED)
                            violate(c, "STOPPED set twice");
                        if (!(b & RESUMED) && s.first == FIRST_NONE)
                            s.first = FIRST_C;
                        if (!(b & RESUMED))
                            s.pc[P_C] = C_DEL;
                        else
                            s.pc[P_C] = C_REL_OWN;
                        return 1;
                    }
                    case C_DEL:
                        tick(s, c, t);
                        if (delete_knote(s, c, t)) {
                            c.what = "Callback: kevent EV_DELETE -> 0 (won: the knote will never fire)";
                            c.events |= EV_C_DEL_WON;
                            if (mut(c) == M_C_LEAKS_KNOTE_OWNER) {
                                s.pc[P_C] = C_REL_OWN;
                            } else {
                                s.pc[P_C] = C_DEL_REL_R;
                            }
                        } else {
                            c.what = "Callback: kevent EV_DELETE -> ENOENT (fired, or not yet armed)";
                            c.events |= EV_C_DEL_ENOENT;
                            bool g = (s.c_before & STARTED) && garnishes(c);
                            s.pc[P_C] = g ? C_GARNISH : C_REL_OWN;
                        }
                        return 1;
                    case C_DEL_REL_R:
                        n = release_step(s, c, P_C, t, choice, done);
                        if (done) {
                            bool g = (s.c_before & STARTED) && garnishes(c);
                            s.pc[P_C] = g ? C_GARNISH : C_REL_OWN;
                        }
                        return n;
                    case C_GARNISH: {
                        n = load_options(s, c, t, A_STATE);
                        c.events |= EV_GARNISH;
                        if (n > 1)
                            c.events |= EV_GARNISH_STALE;
                        tick(s, c, t);
                        c.what = "Callback: _state.load(acquire) (the garnish)";
                        na_read(s, c, t, L_STORAGE);
                        // Only spawn's STARTED, and only as a release, puts
                        // a nonzero clock on a _state message (every other
                        // _state write is a relaxed RMW, which carries its
                        // predecessor's release-sequence clock unchanged),
                        // so the acquire advances our view of S iff it reads
                        // a message in the release sequence headed by
                        // STARTED, and S's clock there is news to us
                        uint8_t const knew = s.vc[t][T_S];
                        load(s, c, t, A_STATE, true, choice);
                        if (c.cfg->ra)
                            c.events |= (s.vc[t][T_S] > knew) ? EV_GARNISH_SYNC : EV_GARNISH_NO_SYNC;
                        s.pc[P_C] = C_REL_OWN;
                        return n;
                    }
                    case C_REL_OWN:
                        n = release_step(s, c, P_C, t, choice, done);
                        if (done)
                            finish_c(s, c);
                        return n;
                    case C_DONE:
                        return 0;
                }
                return 0;
            }

            int step_R(State& s, Ctx& c, int choice) {
                int const t = T_R;
                bool done = false;
                int n = 1;
                switch (s.pc[P_R]) {
                    case R_HARVEST:
                        if (!c.cfg->fires || peek(s, A_KQ) != KN_ARMED)
                            return 0;
                        tick(s, c, t);
                        c.what = "reactor: harvest the one-shot knote (acquire); schedule static_resume";
                        rmw(s, c, t, A_KQ, true, false, 0, 0, KN_HARVESTED);
                        s.fired = 1;
                        s.pc[P_R] = R_RESET;
                        return 1;
                    case R_RESET:
                        if (s.sub[P_R] == 0) {
                            // the pool calls _header.resume(frame)
                            na_read(s, c, t, L_FIELDS);
                            na_read(s, c, t, L_STORAGE);
                        }
                        if (mut(c) == M_R_NO_RESET) {
                            s.pc[P_R] = R_OR;
                            return step_R(s, c, choice);
                        }
                        n = reset_step(s, c, P_R, t, done);
                        if (done) s.pc[P_R] = R_OR;
                        return n;
                    case R_OR: {
                        tick(s, c, t);
                        c.what = "static_resume: _state.fetch_or(RESUMED, relaxed)";
                        na_read(s, c, t, L_STORAGE);
                        uint8_t b = rmw(s, c, t, A_STATE, false, false, RESUMED);
                        s.r_before = b;
                        if (b & RESUMED)
                            violate(c, "RESUMED set twice");
                        if (!(b & STOPPED) && s.first == FIRST_NONE)
                            s.first = FIRST_R;
                        if (!(b & STARTED))
                            c.events |= EV_R_SAW_UNSTARTED;
                        bool inherit = !(b & STOPPED) || mut(c) == M_R_IGNORES_STOPPED;
                        s.pc[P_R] = inherit ? R_REL_C : R_REL_OWN;
                        return 1;
                    }
                    case R_REL_C:
                        n = release_step(s, c, P_R, t, choice, done);
                        if (done) {
                            bool inv = (s.r_before & STARTED) && !(s.r_before & STOPPED);
                            s.pc[P_R] = inv ? R_INVOKE : R_REL_OWN;
                        }
                        return n;
                    case R_INVOKE:
                        tick(s, c, t);
                        c.what = "static_resume: std::invoke(_f)";
                        na_write(s, c, t, L_F);
                        consume(s, c, t, FATE_INVOKED_R);
                        c.events |= EV_R_INVOKE;
                        s.pc[P_R] = R_REL_OWN;
                        return 1;
                    case R_REL_OWN:
                        n = release_step(s, c, P_R, t, choice, done);
                        if (done) s.pc[P_R] = R_DONE;
                        return n;
                    case R_DONE:
                        return 0;
                }
                return 0;
            }

            int step(State& s, Ctx& c, int p, int choice) {
                switch (p) {
                    case P_S: return step_S(s, c, choice);
                    case P_K: return step_K(s, c, choice);
                    case P_R: return step_R(s, c, choice);
                    case P_C: return step_C(s, c, choice);
                }
                return 0;
            }

            // A state in which no party can step
            char const* terminal_violation(State const& s, Ctx& c) {
                if (s.pc[P_S] != S_DONE)
                    return "deadlock: spawn cannot finish";
                if (s.pc[P_K] != K_DONE)
                    return "deadlock: request_stop cannot finish";
                if (s.pc[P_C] != C_IDLE && s.pc[P_C] != C_DONE)
                    return "deadlock: the Callback cannot finish";
                if (s.pc[P_R] != R_HARVEST && s.pc[P_R] != R_DONE)
                    return "deadlock: static_resume cannot finish";
                bool stop = peek(s, A_SS) & SS_STOP;
                bool settled = stop || s.fired || !s.s_ok;
                if (settled) {
                    if (!s.dead)
                        return "leak / lost cancellation: settled but the frame is alive";
                    if (s.fates != 1)
                        return "the continuation was not consumed exactly once";
                } else {
                    // parked: the wait is simply never satisfied
                    if (s.dead || s.fates != 0 || peek(s, A_KQ) != KN_ARMED)
                        return "unsettled, but not parked";
                    c.events |= EV_PARKED;
                    return nullptr;
                }
                if (s.dead && peek(s, A_KQ) == KN_ARMED)
                    return "frame freed with its knote armed";
                switch (s.fate) {
                    case FATE_INVOKED_R:
                    case FATE_INVOKED_S:
                        if (s.first != FIRST_R)
                            return "invoked, but STOPPED was set first";
                        break;
                    case FATE_DESTROYED:
                        if (s.first != FIRST_C)
                            return "destroyed unrun, but RESUMED was set first";
                        if (!stop)
                            return "destroyed unrun without a stop request";
                        break;
                    case FATE_RETURNED:
                        if (s.s_ok)
                            return "handed back although the ADD succeeded";
                        break;
                    default:
                        return "no fate";
                }
                return nullptr;
            }

            struct Checker {
                Config cfg;
                std::unordered_set<State, StateHash, StateEq> seen;
                std::vector<uint8_t> path;   // party * 16 + choice
                std::vector<uint8_t> trace;
                char viol[160] = {};
                bool failed = false;
                uint32_t events = 0;
                size_t transitions = 0;
                size_t terminals = 0;

                State initial() const {
                    State s;
                    std::memset(&s, 0, sizeof s);
                    for (int i = 0; i != NL; ++i)
                        s.na[i].wt = T_NONE;
                    if (cfg.ra)
                        for (int t = 0; t != NT; ++t)
                            s.vc[t][t] = 1;
                    s.a[A_KQ].n = 1;
                    s.a[A_KQ].m[0] = { KN_NONE, T_NONE, 0, {} };
                    s.a[A_SS].n = 1;
                    s.a[A_SS].m[0] = { 0, T_NONE, 0, {} };
                    s.pc[P_S] = S_CTOR;
                    s.pc[P_K] = cfg.canceller ? K_REQ : K_DONE;
                    s.pc[P_R] = R_HARVEST;
                    s.pc[P_C] = C_IDLE;
                    return s;
                }

                void fail(char const* v) {
                    failed = true;
                    std::snprintf(viol, sizeof viol, "%s", v);
                    trace = path;
                }

                void dfs(State const& s) {
                    if (failed || !seen.insert(s).second)
                        return;
                    bool any = false;
                    for (int p = 0; p != NP; ++p) {
                        for (int ch = 0, n = 1; ch < n; ++ch) {
                            State u = s;
                            Ctx c{&cfg};
                            int m = step(u, c, p, ch);
                            if (m == 0)
                                break;
                            n = m;
                            any = true;
                            ++transitions;
                            events |= c.events;
                            path.push_back((uint8_t)(p * 16 + ch));
                            if (c.viol) {
                                fail(c.viol);
                                return;
                            }
                            dfs(u);
                            path.pop_back();
                            if (failed)
                                return;
                        }
                    }
                    if (!any) {
                        ++terminals;
                        Ctx c{&cfg};
                        if (char const* v = terminal_violation(s, c)) {
                            fail(v);
                            return;
                        }
                        events |= c.events;
                    }
                }

                void run() {
                    dfs(initial());
                }

                void print_trace() const {
                    std::printf("    counterexample: %s\n", viol);
                    State s = initial();
                    Config cf = cfg;
                    for (uint8_t pc : trace) {
                        int p = pc / 16, ch = pc % 16;
                        Ctx c{&cf};
                        int thread = (p == P_C) ? s.c_thread : p;
                        step(s, c, p, ch);
                        std::printf("      %s: %s\n", thread_name[thread], c.what);
                    }
                }
            };

            struct Totals {
                size_t states = 0, transitions = 0, terminals = 0;
                uint32_t events = 0;
                bool failed = false;
                std::string first_violation;
            };

            // Run every environment (canceller or not, fires or not) in one
            // mode with one mutation
            Totals check_all(bool ra, Mutation mutation, bool print) {
                Totals totals;
                for (int canceller = 0; canceller != 2; ++canceller) {
                    for (int fires = 0; fires != 2; ++fires) {
                        Checker k;
                        k.cfg = Config{ canceller != 0, fires != 0, ra, mutation };
                        k.run();
                        totals.states += k.seen.size();
                        totals.transitions += k.transitions;
                        totals.terminals += k.terminals;
                        totals.events |= k.events;
                        if (k.failed && !totals.failed) {
                            totals.failed = true;
                            totals.first_violation = k.viol;
                            if (print) {
                                std::printf("  [%s, canceller=%d, fires=%d, mutation=%d]\n",
                                            ra ? "RA" : "SC", canceller, fires, (int)mutation);
                                k.print_trace();
                            }
                        }
                    }
                }
                return totals;
            }

            bool contains(std::string const& haystack, char const* needle) {
                return haystack.find(needle) != std::string::npos;
            }

        } // namespace kevent_model

    } // namespace

    // The protocol as written: no violation in either mode, and every path
    // of interest reached (so the pass is not vacuous)
    define_test("kevent_model") {
        using namespace kevent_model;
        bool const verbose = std::getenv("WRY_MODEL_VERBOSE") != nullptr;
        for (bool ra : { false, true }) {
            auto t0 = std::chrono::steady_clock::now();
            Totals t = check_all(ra, M_NONE, true);
            if (verbose)
                std::printf("[kevent_model] %s: %zu states, %zu transitions, %zu terminal states, %.3f s, events %#x\n",
                            ra ? "RA" : "SC", t.states, t.transitions, t.terminals,
                            std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count(),
                            t.events);
            require(!t.failed);
            uint32_t const expected =
                EV_C_INLINE | EV_C_ON_K | EV_C_DEL_WON | EV_C_DEL_ENOENT |
                EV_S_DEL_WON | EV_S_DEL_ENOENT | EV_R_INVOKE | EV_S_INVOKE |
                EV_DESTROYED | EV_RETURNED | EV_WAITED_COMPLETED |
                EV_K_DESTROYED_FLAG | EV_PARKED | EV_ADD_FAILED |
                EV_R_SAW_UNSTARTED;
            require((t.events & expected) == expected);
            // The code as written has no garnish (variant M_OLD_PROTOCOL)
            require(!(t.events & (EV_GARNISH | EV_GARNISH_SYNC | EV_GARNISH_NO_SYNC)));
            require(t.states > 100);
            (void)t;
        }
        co_return;
    };

    // The checker is not vacuous: each mutation is caught, by the mode that
    // should catch it.  Ordering mutations pass SC and fail RA; logic
    // mutations fail SC already.
    //
    // Two orderings the protocol carried until 2026-11-10 -- spawn's
    // fetch_or(STARTED) as a release, and the Callback's acquire re-load of
    // _state (the garnish) when it saw STARTED and not RESUMED -- were
    // dropped (docs/decisions_2026-11-08.md item 19).  What shows they were
    // not load-bearing is that the relaxed baseline (M_NONE, test
    // kevent_model above) passes in SC and RA: with every _state access
    // relaxed, happens-before comes only from the reference count, the
    // knote, the stop_state lock and __completed_, and that is enough.  In
    // words:
    //   - STARTED: static_resume's reads of the frame are ordered by ADD
    //     -release-> harvest (acquire); on the success path spawn writes
    //     nothing to the frame between the ADD and STARTED; on the failure
    //     path no static_resume exists, and the Callback cannot read
    //     STARTED (the reset waited for it or unlinked it).
    //   - the garnish: every later frame access by another party is ordered
    //     after the Callback by the reference count's release sequence
    //     (each fetch_sub is a release; the deleter's acquire load reads
    //     the last of the chain) or by the stop_callback's __completed_
    //     release/acquire.
    //
    // The old protocol itself is checked as variant M_OLD_PROTOCOL (both
    // halves on), which must also pass in SC and RA.  In RA its garnish
    // must really synchronize: every garnish reads (always, see below) the
    // Callback's own relaxed fetch_or(STOPPED), which continues the release
    // sequence headed by spawn's STARTED, so its acquire joins S's clock at
    // STARTED, which the Callback had from no other edge (EV_GARNISH_SYNC,
    // and never EV_GARNISH_NO_SYNC).  That is a check that the edge exists,
    // not that anything needs it; the baseline settles the latter.
    //
    // Either half alone adds no happens-before edge in RA, so it is not
    // checked as a variant of its own except as a negative control:
    // STARTED as a release with no garnish is never acquired (nothing else
    // acquires _state), and the garnish with STARTED relaxed acquires only
    // messages whose clocks are empty.  M_C_GARNISH (the garnish alone) is
    // kept to show the sync check above discriminates: there every garnish
    // is EV_GARNISH_NO_SYNC.  Beyond that it adds only one more race /
    // use-after-free probe of the frame (the garnish's own read of
    // STORAGE) over the baseline.
    //
    // Within the model.  F, NODE and FIELDS are coarse locations and the
    // callable is opaque, so a dependency inside F that spawn created after
    // the ADD would be invisible here; the code has none today (spawn does
    // not touch _f on the success path before STARTED).
    //
    // The ordering mutations below (M_RC_RELAXED, M_RC_NO_ACQUIRE,
    // M_ADD_RELAXED) are weakenings of the relaxed baseline, so the edges
    // they remove are load-bearing in the protocol as shipped.
    define_test("kevent_model_mutations") {
        using namespace kevent_model;
        struct Expectation {
            Mutation mutation;
            bool sc_fails;
            bool ra_fails;
            char const* ra_violation;  // substring of the first RA violation
        };
        Expectation const expectations[] = {
            { M_RC_RELAXED,          false, true,  "data race" },
            { M_RC_NO_ACQUIRE,       false, true,  "data race" },
            { M_ADD_RELAXED,         false, true,  "data race" },
            { M_C_LEAKS_KNOTE_OWNER, true,  true,  "leak" },
            { M_R_NO_RESET,          true,  true,  "" },
            { M_R_IGNORES_STOPPED,   true,  true,  "" },
            { M_S_NO_DOOMED_DELETE,  true,  true,  "lost cancellation" },
            { M_OLD_PROTOCOL,        false, false, nullptr },
            { M_C_GARNISH,           false, false, nullptr },
        };
        bool const verbose = std::getenv("WRY_MODEL_VERBOSE") != nullptr;
        for (Expectation const& e : expectations) {
            Totals sc = check_all(false, e.mutation, verbose);
            Totals ra = check_all(true, e.mutation, verbose);
            bool const as_expected = (sc.failed == e.sc_fails) && (ra.failed == e.ra_fails)
                && (!e.ra_violation || contains(ra.first_violation, e.ra_violation));
            if (verbose || !as_expected)
                std::printf("[kevent_model_mutations] mutation %d: SC %s, RA %s (RA events %#x)%s\n",
                            (int)e.mutation,
                            sc.failed ? sc.first_violation.c_str() : "pass",
                            ra.failed ? ra.first_violation.c_str() : "pass",
                            ra.events,
                            as_expected ? "" : "  <-- NOT AS EXPECTED");
            require(sc.failed == e.sc_fails);
            require(ra.failed == e.ra_fails);
            if (e.ra_violation)
                require(contains(ra.first_violation, e.ra_violation));
            if (e.mutation == M_OLD_PROTOCOL || e.mutation == M_C_GARNISH) {
                // The variant really runs the garnish (else its pass is
                // vacuous), and the garnish never has a choice of message:
                // after the Callback's fetch_or nothing else writes _state
                // until it has finished (spawn's STARTED is already before
                // it when the garnish runs, and static_resume's reset waits
                // on __completed_), so it always reads the Callback's own
                // RMW
                require(sc.events & EV_GARNISH);
                require(ra.events & EV_GARNISH);
                require(!(sc.events & EV_GARNISH_STALE));
                require(!(ra.events & EV_GARNISH_STALE));
                // SC keeps no clocks, so it observes neither
                require(!(sc.events & (EV_GARNISH_SYNC | EV_GARNISH_NO_SYNC)));
            }
            if (e.mutation == M_OLD_PROTOCOL) {
                // With STARTED a release, every garnish synchronizes with it
                require(ra.events & EV_GARNISH_SYNC);
                require(!(ra.events & EV_GARNISH_NO_SYNC));
            }
            if (e.mutation == M_C_GARNISH) {
                // With STARTED relaxed, no garnish synchronizes with anything
                require(ra.events & EV_GARNISH_NO_SYNC);
                require(!(ra.events & EV_GARNISH_SYNC));
            }
            (void)sc; (void)ra;
        }
        co_return;
    };

} // namespace wry
