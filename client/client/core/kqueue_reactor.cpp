//
//  kqueue_reactor.cpp
//  client
//
//  Created by Antony Searle on 22/11/2025.
//

#include <fcntl.h>
#include <sys/event.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <cstring>

#include <mutex>
#include <optional>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <thread>

#if defined(__has_feature)
#if __has_feature(thread_sanitizer)
#define WRY_TSAN 1
#include <sanitizer/tsan_interface.h>
#endif
#endif

#include "kqueue_reactor.hpp"
#include "nursery.hpp"

#include "test.hpp"

namespace wry {

    namespace {

        // ---- Reactor thread ------------------------------------------------

        constinit int g_reactor_kqueue = -1;
        std::thread g_reactor_thread;

        // The kernel's per-knote edge (see ORDER below), shown to
        // ThreadSanitizer on the knote's udata; nothing in other builds
        inline void tsan_kevent_release([[maybe_unused]] void* udata) {
#ifdef WRY_TSAN
            __tsan_release(udata);
#endif
        }
        inline void tsan_kevent_acquire([[maybe_unused]] void* udata) {
#ifdef WRY_TSAN
            __tsan_acquire(udata);
#endif
        }

        void reactor_run();

        // The shutdown doorbell is a persistent EVFILT_USER knote, ident 0.
        constexpr uint64_t DOORBELL_IDENT = 0;

        // ---- Reactor contract ----------------------------------------------
        //
        // The reactor is protocol-free.  Every knote it delivers, other than
        // the doorbell, carries as udata a heap frame whose first two words
        // are a coroutine::Header {resume, destroy}; the reactor schedules
        // the frame on the thread pool.  Filters, idents, and completion
        // semantics are the frame's business.  Nothing resumes on the reactor
        // thread.
        //
        // Knotes are one-shot and EV_UDATA_SPECIFIC, keyed by (ident, filter,
        // udata), so concurrent frames on one ident never collide and a
        // frame's EV_DELETE can only ever remove its own knote.
        //
        // ORDER: the kernel carries only the udata pointer, an edge neither
        // the C++ model nor ThreadSanitizer can see.  We treat kevent as a
        // mutex per knote -- ADD synchronizes-with delivery, and with a
        // receipt-0 DELETE -- and make that edge visible to TSan on the
        // udata itself: __tsan_release(udata) before every kevent change,
        // __tsan_acquire(udata) after it returns and before the reactor
        // schedules a harvested udata (tsan_kevent_*).  Per knote, as the
        // kernel orders it: a global witness atomic (until 2026-11-09) also
        // ordered unrelated knotes, which could hide races
        // (docs/tsan_2026-11-08.md N1).  Frame atomics are otherwise
        // relaxed except for an acquire re-load on the winning path.

        // ---- Reactor service loop ------------------------------------------

        void reactor_run() {
            pthread_setname_np("R0");
            bool stop_requested = false;
            for (;;) {
                const int NEVENTS = 16;
                struct kevent events[NEVENTS];
                struct timespec immediate = {};
                int n = ::kevent(g_reactor_kqueue,
                                 nullptr, 0, events, NEVENTS,
                                 stop_requested ? &immediate : nullptr);
                assert((-1 <= n) && (n <= NEVENTS));
                assert((n != 0) || stop_requested);
                if (n == -1) {
                    if (errno == EINTR)
                        continue;
                    perror("reactor kevent");
                    abort();
                }
                for (int i = 0; i != n; ++i) {
                    struct kevent const& event = events[i];
                    assert(!(event.flags & EV_ERROR));
                    if (event.filter == EVFILT_USER) {
                        // Only the shutdown doorbell registers EVFILT_USER
                        // today; a future user-event tenant must be
                        // dispatched here, not mistaken for shutdown.
                        assert(event.ident == DOORBELL_IDENT);
                        // Stop has been requested, but in-order delivery is
                        // not guaranteed, so we switch to polling until we
                        // have observed an empty queue
                        stop_requested = true;
                        continue;
                    }
                    // Every other knote's udata is a Header-prefixed frame
                    // (CancelableKEventFrame); the reactor knows nothing
                    // about filters or protocols.  It schedules the frame on
                    // the pool.
                    // The kernel ordered the publisher's ADD (and everything
                    // before it) before this harvest; tell TSan
                    tsan_kevent_acquire((void*)event.udata);
                    global_work_queue_schedule((void*)event.udata);
                }
                if (stop_requested) {
                    // We can break out of the loop iff we prove the queue is
                    // empty
                    if (n < NEVENTS)
                        break;
                }
            }
        }

    } // namespace

    void global_reactor_start() {
        g_reactor_kqueue = kqueue();
        if (g_reactor_kqueue == -1) {
            perror("kqueue");
            abort();
        }
        struct kevent change = {
            .ident = DOORBELL_IDENT,
            .filter = EVFILT_USER,
            .flags = EV_ADD | EV_CLEAR,
        };
        struct timespec immediate = {};
        if (::kevent(g_reactor_kqueue, &change, 1, nullptr, 0,  &immediate) != 0) {
            perror("kevent");
        }
        g_reactor_thread = std::thread(&reactor_run);
    }

    void global_reactor_stop() {
        if (!g_reactor_thread.joinable())
            return;
        struct kevent change = {
            .ident = DOORBELL_IDENT,
            .filter = EVFILT_USER,
            .fflags = NOTE_TRIGGER,
        };
        struct timespec immediate = {};
        if (::kevent(g_reactor_kqueue, &change, 1, nullptr, 0, &immediate) != 0)
            abort();
        g_reactor_thread.join();
        ::close(g_reactor_kqueue);
        g_reactor_kqueue = -1;
    }

    // Test observability: live CancelableKEventFrame count.  The tests pin
    // the load-bearing property -- the Frame dies promptly after the FIRST
    // of {timer fired, stop requested} -- which is invisible without this.
    // Kept in every build: the tests' drain loops on it are what hold a
    // test open until its frame has died, and in a Release build they are
    // the only thing that does (the asserts around them compile out).
    // One relaxed add and one release sub per frame.  Process-wide, so it
    // only speaks for a test while the reactor is the test's alone: the
    // tests check that first (FrameCensus below)
    constinit std::atomic<std::ptrdiff_t> g_cancelable_kevent_frame_count{0};

    template<typename F>
    struct CancelableKEventFrame {

        using Frame = CancelableKEventFrame;

        enum : uintptr_t {
            RESUMED = 1,
            STOPPED = 2,
            STARTED = 4,
        };

        struct Callback {
            Frame* frame;
            void operator()() {
                // We have reached here because stop was requested
                assert(frame->_stop_token.stop_requested());
                // Race to claim the continuation.  Relaxed: _state only
                // elects who acts; it publishes no data.  What this body
                // reads of the frame (_stop_token, _change) was written by
                // spawn before _stop_callback.emplace, so it happens-before
                // us by sequenced-before (run inline in emplace) or by the
                // stop_state lock (emplace's registration releases it,
                // request_stop's pop acquires it).  What other parties do
                // to the frame after us is ordered by release() below
                // (fetch_sub release; the deleter's acquire load reads the
                // end of that release sequence) and by ~stop_callback's
                // acquire wait on request_stop's __completed_ release
                // store.  No acquire re-load of _state is needed: the model
                // check (kqueue_reactor_model.cpp, variant M_C_GARNISH)
                // shows one is not load-bearing.
                intptr_t before = frame->_state.fetch_or(STOPPED, std::memory_order_relaxed);
                intptr_t resumed = before & RESUMED;
                assert(!(before & STOPPED));
                if (!resumed) {
                    switch (frame->_delete_knote()) {
                        case 0: // Won race; responsible for release
                            frame->release();
                            break;
                        case ENOENT: // We lost the race, or not yet armed
                            break;
                        default: // Unhandled errno value
                            abort();
                    }
                }
                frame->release();
                // _f may be destroyed
            }
        };

        coroutine::Header _header = { &static_resume, nullptr };
        std::stop_token _stop_token;
        std::atomic<uintptr_t> _state{0};
        std::atomic<ptrdiff_t> _reference_count_minus_one{2}; // initial condition: 3 owners
        std::optional<std::stop_callback<Callback>> _stop_callback;
        struct kevent _change;
        F _f;

        void acquire() {
            _reference_count_minus_one.fetch_add(1, std::memory_order_relaxed);
        }

        void release() {
            if (!_reference_count_minus_one.fetch_sub(1, std::memory_order_release)) {
                (void) _reference_count_minus_one.load(std::memory_order_acquire);
                delete this;
                g_cancelable_kevent_frame_count.fetch_sub(1, std::memory_order_release);
            }
        }

        static void static_resume(void* ptr) {
            // We have reached here because the timer event fired
            CancelableKEventFrame* frame = (CancelableKEventFrame*)ptr;
            // Because the timer fired, we can kill the stop_callback
            // Safety: ~_stop_callback and request_stop are mutually excluded
            frame->_stop_callback.reset();
            // Race to take the continuation
            auto before = frame->_state.fetch_or(RESUMED, std::memory_order_relaxed);
            auto resumed = before & RESUMED;
            auto stopped = before & STOPPED;
            auto started = before & STARTED;
            assert(!resumed);
            // We deliberately don't poll the stop_token because doing so
            // doesn't prevent the race, it just narrows the window
            if (!stopped) {
                // We destroyed the stop callback before it fired, and inherit
                // its ownership
                frame->release();
            }
            if (started && !stopped) {
                // frame->_continuation.load(std::memory_order_acquire);
                // global_work_queue_schedule((void*)before);
                std::invoke(frame->_f);
            }
            frame->release();
        }

        [[nodiscard]] int64_t _kevent(uint16_t flags) {
            struct kevent change = _change;
            struct kevent event = {};
            change.flags = flags;
            change.udata = this;
            struct timespec immediate = {};
            tsan_kevent_release(this);
            // EV_RECEIPT and nchange == nevent means we get only change results
            int n = kevent(g_reactor_kqueue, &change, 1, &event, 1, &immediate);
            if (n != 1)
                abort();
            // A receipt-0 DELETE won the knote from the reactor: the ADD
            // is ordered before it.  (Not after an ADD: the frame may
            // already be delivered, run and freed.)
            if ((flags & EV_DELETE) && event.data == 0)
                tsan_kevent_acquire(this);
            return event.data;
        }

        [[nodiscard]] int64_t _add_knote() {
            return _kevent(EV_ADD | EV_ONESHOT | EV_RECEIPT);
        }

        [[nodiscard]] int64_t _delete_knote() {
            return _kevent(EV_DELETE | EV_RECEIPT);
        }

        template<typename G>
        CancelableKEventFrame(std::stop_token stop_token, struct kevent change, G&& g)
        : _stop_token(std::move(stop_token))
        , _change(std::move(change))
        , _f(std::forward<G>(g))
        {
        }

        struct Unarmed {
            int error;
            F callable;
        };

        template<typename G>
        [[nodiscard]] static std::expected<void, Unarmed> spawn(std::stop_token stop_token, struct kevent change, G&& g) {
            g_cancelable_kevent_frame_count.fetch_add(1, std::memory_order_relaxed);
            auto frame = new CancelableKEventFrame<F>(std::move(stop_token),
                                                      std::move(change),
                                                      std::forward<G>(g));

            if (change.filter == EVFILT_TIMER) {
                assert(change.ident == 0);
                change.ident = (uintptr_t)frame;
            }

            frame->_change = change;

            // No early-out, it pessimizes the happy path to accelerate the rare path

            frame->_stop_callback.emplace(frame->_stop_token, Callback{frame});
            int64_t result = frame->_add_knote();

            switch (result) {
                case 0: // Did add
                    break;
                default: // Did not add
                    // Release static_resume's ownership
                    frame->release();
                    // Disarm cancellation
                    frame->_stop_callback.reset();
                    break;
            }

            // Relaxed: STARTED publishes nothing.  The parties that read it
            // take their data from other edges:
            //   - static_resume reads STARTED, then invokes _f.  _f (and the
            //     suspended coroutine) were written before the ADD, and the
            //     ADD synchronizes-with the reactor's harvest of the knote
            //     (the kernel edge, see ORDER), which is sequenced-before
            //     static_resume on the pool.  On the armed path spawn
            //     writes nothing to the frame between the ADD and here, so
            //     a release here would publish nothing new.
            //   - On the failed-ADD path no static_resume exists, and the
            //     Callback cannot read STARTED: the reset above either
            //     unlinked it (it never runs) or waited (acquire) on
            //     __completed_ for it to return.
            // Our own accesses after this RMW: if it saw RESUMED we invoke
            // _f, which static_resume left alone (it saw no STARTED); the
            // final release() orders us before the deleter.  The model
            // check (kqueue_reactor_model.cpp, variant M_S_STARTED_RELEASE)
            // shows release here is not load-bearing.
            auto before = frame->_state.fetch_or(STARTED, std::memory_order_relaxed);
            auto resumed = before & RESUMED;
            auto stopped = before & STOPPED;

            switch (result) {
                case 0: // Did add
                    if (stopped) [[unlikely]] {
                        // never invoked, destroyed with frame
                        if (!resumed) {
                            // We may have deleted before we added, leaving a doomed
                            // timer running.  Kill it ASAP.
                            switch (frame->_delete_knote()) {
                                case 0: // The event will never fire
                                    // Release static_resume's ownership
                                    frame->release();
                                    break;
                                case ENOENT: // Nothing to clean up
                                    break;
                                default: // Unhandled errno
                                    abort();
                            }
                        }
                    } else if (resumed) [[unlikely]] {
                        std::invoke(frame->_f);
                    }
                    break;
                default: // Did not add
                    assert(!resumed); // Impossible
                    // The callable goes back to the caller unconsumed inside
                    // the expected's error (below), never through g: g may be
                    // a temporary whose destructor would consume it
                    if (stopped) {
                        // Callback released its ownership
                    } else {
                        // Callback did not release its ownesrhip
                        frame->release();
                    }
                    break;
            }
            if (result == 0) {
                frame->release();
                return std::expected<void, Unarmed>();
            } else {
                F temp(std::move(frame->_f));
                frame->release();
                return std::expected<void, Unarmed>(std::unexpect, Unarmed{(int)result, std::move(temp)});
            }
        }

    }; // CancelableKEventFrame

    template<typename F>
    void spawn_after(std::stop_token stop_token,
                     std::chrono::steady_clock::duration duration,
                     F&& f) {
        auto nanoseconds = std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count();
        struct kevent change = {
            .ident = 0,
            .filter = EVFILT_TIMER,
            .flags = 0,
            .fflags = NOTE_NSECONDS,
            .data = nanoseconds,
        };
        auto e = CancelableKEventFrame<std::decay_t<F>>::spawn(stop_token,
                                                               change,
                                                               std::forward<F>(f));
        if (!e.has_value()) {
            errno = e.error().error;
            perror("cancelable_after");
            abort();
        }
    }

    // The exported form erases the callable at the API boundary
    void cancelable_after(std::stop_token stop_token,
                          std::chrono::steady_clock::duration duration,
                          move_only_function<void()> f) {
        spawn_after(std::move(stop_token), duration, std::move(f));
    }

    // ---- Continuations as callables ---------------------------------------
    //
    // The callable a CancelableKEventFrame runs when its knote fires, when the
    // awaiting coroutine's continuation is what should run: scheduled onto
    // the pool on the fire, destroyed (cancellation unwinding) if dropped
    // unrun.  release() takes the handle back when the frame was never
    // armed and the awaitable must continue the coroutine itself.
    struct ContinuationThunk {
        std::coroutine_handle<> _handle;
        explicit ContinuationThunk(std::coroutine_handle<> handle) : _handle(handle) {}
        ContinuationThunk(ContinuationThunk&& other) : _handle(take(other._handle)) {}
        ContinuationThunk& operator=(ContinuationThunk&& other) {
            assert(!_handle);
            _handle = take(other._handle);
            return *this;
        }
        void operator()() {
            assert(_handle);
            global_work_queue_schedule(take(_handle));
        };
        ~ContinuationThunk() {
            if (_handle)
                _handle.destroy();
        }
        std::coroutine_handle<> release() {
            return take(_handle);
        }
    };

    // ---- Readiness ----------------------------------------------------------
    //
    // Suspend until `fd` is ready on `filter` (EVFILT_READ or EVFILT_WRITE),
    // then resume with 0.  If the knote cannot be registered (a bad
    // descriptor, say) resume at once with the positive errno; the operation
    // built on top negates it into kernel convention.  A stop request on the
    // awaiting chain's token destroys the parked frame and withdraws the
    // knote.  The knote fires on EV_EOF too (peer close is readability, and
    // writability), so the syscall that follows sees the kernel's answer.
    //
    // At most one waiter per (fd, filter): two waiters' knotes both fire on
    // readiness, and the loser's syscall either blocks the worker (on a
    // blocking descriptor) or fails with EAGAIN.  A reader and a writer may
    // wait on one socket at the same time: those are different knotes.
    struct CancelableReadinessAwaitable {
        int _fd;
        int16_t _filter;
        int _error = 0;
        constexpr bool await_ready() const noexcept {
            return false;
        }
        template<typename OuterPromise>
        std::coroutine_handle<> await_suspend(std::coroutine_handle<OuterPromise> continuation) {
            std::stop_token stop_token = get_stop_token(continuation);
            struct kevent change = {
                .ident = (uint64_t) _fd,
                .filter = _filter,
                .flags = 0,
                .fflags = 0,
                .data = 0,
            };
            auto e = CancelableKEventFrame<ContinuationThunk>::spawn(stop_token, change,
                                                                     ContinuationThunk{take(continuation)});
            if (e.has_value()) {
                // the frame may be running or gone: no *this past here
                return std::noop_coroutine();
            }
            // Registration failed: spawn gave the thunk back, so the frame is
            // still ours; take the handle back before the thunk's destructor
            // destroys it
            _error = e.error().error;
            return e.error().callable.release();
        }
        [[nodiscard]] int await_resume() const noexcept {
            return _error;
        }
    };

    struct CancelableReadableAwaitable : CancelableReadinessAwaitable {
        explicit CancelableReadableAwaitable(int fd)
        : CancelableReadinessAwaitable{fd, EVFILT_READ} {}
    };

    struct CancelableWritableAwaitable : CancelableReadinessAwaitable {
        explicit CancelableWritableAwaitable(int fd)
        : CancelableReadinessAwaitable{fd, EVFILT_WRITE} {}
    };

    // ---- Socket operations: wait for readiness, then one syscall -----------

    [[nodiscard]] coroutine::Future<ssize_t> recv_some(int socket, void* buffer, size_t length, int flags) {
        if (int error = co_await CancelableReadableAwaitable{socket})
            co_return -error;
        ssize_t n = ::recv(socket, buffer, length, flags);
        co_return (n == -1) ? -errno : n;
    }

    [[nodiscard]] coroutine::Future<ssize_t> send_some(int socket, void const* buffer, size_t length, int flags) {
        if (int error = co_await CancelableWritableAwaitable{socket})
            co_return -error;
        ssize_t n = ::send(socket, buffer, length, flags);
        co_return (n == -1) ? -errno : n;
    }

    [[nodiscard]] coroutine::Future<int> accept_one(int socket, struct sockaddr* address, socklen_t* length) {
        if (int error = co_await CancelableReadableAwaitable{socket})
            co_return -error;
        int fd = ::accept(socket, address, length);
        co_return (fd == -1) ? -errno : fd;
    }

    [[nodiscard]] coroutine::Future<int> connect_to(int socket, struct sockaddr const* address, socklen_t length) {
        // A non-blocking connect that cannot complete at once reports
        // EINPROGRESS; completion is writability, and the result is then
        // read back with SO_ERROR.  A connect that completes (or fails) at
        // once -- a Unix domain socket, a bad address -- never suspends
        if (::connect(socket, address, length) == 0)
            co_return 0;
        if (errno != EINPROGRESS)
            co_return -errno;
        if (int error = co_await CancelableWritableAwaitable{socket})
            co_return -error;
        int so_error = 0;
        socklen_t so_length = sizeof so_error;
        if (::getsockopt(socket, SOL_SOCKET, SO_ERROR, &so_error, &so_length) == -1)
            co_return -errno;
        co_return -so_error;
    }

    // ---- Timers ---------------------------------------------------------------

    struct CancelableSleepAwaitable {
        intptr_t _nanoseconds;
        constexpr bool await_ready() const noexcept {
            return false;
        }
        template<typename OuterPromise>
        std::coroutine_handle<> await_suspend(std::coroutine_handle<OuterPromise> continuation) {
            std::stop_token stop_token = get_stop_token(continuation);
            struct kevent change = {
                .ident = 0,
                .filter = EVFILT_TIMER,
                .flags = 0,
                .fflags = NOTE_NSECONDS,
                .data = _nanoseconds,
            };
            auto e = CancelableKEventFrame<ContinuationThunk>::spawn(stop_token, change,
                                                                     ContinuationThunk{take(continuation)});
            if (!e.has_value())
                abort();  // resource-exhaustion grade, as cancelable_after
            return std::noop_coroutine();
        }
        void await_resume() const {}
    };

    [[nodiscard]] coroutine::Future<> cancelable_sleep(double seconds) {
        co_return co_await CancelableSleepAwaitable{(intptr_t)(seconds * NSEC_PER_SEC)};
    }

    // ---- Tests -------------------------------------------------------------

    namespace {

        using namespace std::chrono;

        struct SocketPair {
            int _fds[2] = {-1, -1};
            SocketPair() {
                int rc = ::socketpair(AF_UNIX, SOCK_STREAM, 0, _fds);
                assert(rc == 0);
                (void) rc;
            }
            ~SocketPair() {
                if (_fds[0] != -1) ::close(_fds[0]);
                if (_fds[1] != -1) ::close(_fds[1]);
            }
            int reader() const { return _fds[0]; }
            int writer() const { return _fds[1]; }
            void close_writer() {
                ::close(_fds[1]);
                _fds[1] = -1;
            }
        };

        struct ReactorProbe {
            std::atomic<int>* _counter;
            ~ReactorProbe() { _counter->fetch_add(1, std::memory_order_relaxed); }
        };

        coroutine::Task reactor_late_writer(int fd) {
            co_await cancelable_sleep(0.010);
            char message[] = "later";
            ssize_t n = ::send(fd, message, 5, 0);
            assert(n == 5);
            (void) n;
        }

    }
    namespace {

        // Move-nulling parameter probe: destroyed exactly once with the task
        // frame, whether the task ran to completion or was destroyed armed.
        // (A cancelled never-started frame destroys only its parameter
        // copies, so the probe must be a parameter, not a body local.)
        struct CancelableProbe {
            std::atomic<int>* _counter = nullptr;
            explicit CancelableProbe(std::atomic<int>* counter)
            : _counter(counter) {}
            CancelableProbe(CancelableProbe&& other)
            : _counter(take(other._counter)) {}
            ~CancelableProbe() {
                if (_counter)
                    _counter->fetch_add(1, std::memory_order_relaxed);
            }
        };

        // The probe rides in the callable, so it is destroyed exactly once
        // whether the callable ran or was dropped unrun
        auto cancelable_task(CancelableProbe probe,
                             std::atomic<int>* ran) {
            return [probe = std::move(probe), ran]() mutable {
                ran->fetch_add(1, std::memory_order_relaxed);
            };
        }

    }

    namespace {

        // The frame-count checks assume the reactor is the test's alone,
        // which only --test-only guarantees: in a live process the control
        // socket keeps an accept frame parked (re-armed per connection),
        // each live connection a recv frame and the idle host's wait a
        // deadline timer (gui_context.cpp), so the count never rests at
        // zero there.  The tests that read it are therefore tagged
        // test_exclusive (core/test.hpp) and the runner skips them in a
        // live process.  The census below is the belt to those braces: a
        // brief wait for the count to rest at zero (the previous test's
        // frames retiring) and, if it does not, the count checks stand down
        // and say so rather than assert on someone else's frames.  The
        // drains are bounded either way, so a regression fails the test
        // instead of hanging the suite.
        struct FrameCensus {

            bool exclusive = false;

            static coroutine::Future<FrameCensus> open(char const* test) {
                // Up to 100 ms for the count to reach zero (the previous
                // test's frames retiring), then zero throughout 20 ms: zero
                // once is not enough, since a driven app's listener frame
                // dies and re-arms at each accept, so the count visits zero
                // for microseconds there
                auto t0 = steady_clock::now();
                std::optional<steady_clock::time_point> since_zero;
                for (;;) {
                    std::ptrdiff_t live = g_cancelable_kevent_frame_count.load(std::memory_order_acquire);
                    auto now = steady_clock::now();
                    if (live != 0) {
                        if (since_zero || (now - t0 > milliseconds(100))) {
                            printf("[%s] frame-count checks skipped: the reactor is shared (%td live frames)\n",
                                   test, live);
                            co_return FrameCensus{false};
                        }
                    } else if (!since_zero) {
                        since_zero = now;
                    } else if (now - *since_zero > milliseconds(20)) {
                        co_return FrameCensus{true};
                    }
                    co_await coroutine::TransferToPoolExecutor{};
                }
            }

            // True when the count is at rest, or is not ours to read
            bool quiet() const {
                return !exclusive
                    || (g_cancelable_kevent_frame_count.load(std::memory_order_acquire) == 0);
            }

            // Hold the test open until its frames have died: promptly, so
            // a second is already a failure
            coroutine::Future<> drained() const {
                auto t0 = steady_clock::now();
                while (exclusive
                       && (g_cancelable_kevent_frame_count.load(std::memory_order_acquire) != 0)
                       && (steady_clock::now() - t0 < seconds(1)))
                    co_await coroutine::TransferToPoolExecutor{};
                assert(quiet());
            }

        };

    }

    define_test("kqueue_cancelable_after", test_exclusive) {
        FrameCensus census = co_await FrameCensus::open("kqueue_cancelable_after");

        // Fire path: at the deadline the task runs on the pool and its frame
        // is destroyed on completion
        {
            std::stop_source source;
            std::atomic<int> ran{0};
            std::atomic<int> destroyed{0};
            auto t0 = steady_clock::now();
            cancelable_after(source.get_token(), milliseconds(20),
                             cancelable_task(CancelableProbe{&destroyed}, &ran));
            while (ran.load(std::memory_order_relaxed) == 0)
                co_await coroutine::TransferToPoolExecutor{};
            require(steady_clock::now() - t0 >= milliseconds(15));
            while (destroyed.load(std::memory_order_relaxed) == 0)
                co_await coroutine::TransferToPoolExecutor{};
            // The load-bearing property: the Frame dies promptly after the
            // fire, with the token never requested -- static_resume disarms
            // the callback and inherits its reference
            co_await census.drained();
        }

        // Cancel path: cancellation is a synchronous join -- when
        // request_stop returns, the armed task is already destroyed
        {
            std::stop_source source;
            std::atomic<int> ran{0};
            std::atomic<int> destroyed{0};
            cancelable_after(source.get_token(), seconds(10),
                             cancelable_task(CancelableProbe{&destroyed}, &ran));
            // Settle past the arming window so this exercises the parked
            // cancel, not the install race
            co_await cancelable_sleep(0.005);
            source.request_stop();
            require(destroyed.load(std::memory_order_relaxed) == 1);
            require(ran.load(std::memory_order_relaxed) == 0);
            // The whole structure -- knote withdrawn, frame freed -- retired
            // synchronously inside request_stop
            require(census.quiet());
        }

        // Cancel before spawn: an already-requested token destroys the task
        // inside cancelable_after itself, synchronously
        {
            std::stop_source source;
            source.request_stop();
            std::atomic<int> ran{0};
            std::atomic<int> destroyed{0};
            cancelable_after(source.get_token(), seconds(10),
                             cancelable_task(CancelableProbe{&destroyed}, &ran));
            require(destroyed.load(std::memory_order_relaxed) == 1);
            require(ran.load(std::memory_order_relaxed) == 0);
            // The doomed-timer cleanup: the knote armed after the inline
            // cancellation is withdrawn inside spawn itself, so the frame is
            // gone before cancelable_after returns -- not at the deadline
            require(census.quiet());
        }

        // Early fire vs install: a zero deadline races delivery against
        // spawn's own installation; every interleaving must run the task
        // exactly once
        {
            std::stop_source source;
            std::atomic<int> ran{0};
            std::atomic<int> destroyed{0};
            const int N = 100;
            for (int i = 0; i != N; ++i)
                cancelable_after(source.get_token(), std::chrono::nanoseconds(0),
                                 cancelable_task(CancelableProbe{&destroyed}, &ran));
            while (ran.load(std::memory_order_relaxed) != N)
                co_await coroutine::TransferToPoolExecutor{};
            while (destroyed.load(std::memory_order_relaxed) != N)
                co_await coroutine::TransferToPoolExecutor{};
            // All N frames retire without a stop request, in every
            // interleaving the race produced
            co_await census.drained();
        }

        co_return;
    };

    // ---- recv_some, composed with with_deadline ---------------------------
    //
    //   optional<ssize_t> r = co_await with_deadline(t, recv_some(fd,buf,len,0));
    //
    // nullopt is the deadline; an engaged value is kernel convention: bytes,
    // 0 for peer close, -errno for a failed registration or recv.  Outer
    // cancellation destroys the whole tower and is observed via the nursery.


    namespace {

        coroutine::Task wd_recv_child(int fd,
                                      std::chrono::steady_clock::time_point deadline,
                                      std::atomic<int>* started,
                                      std::atomic<int>* destroyed,
                                      std::atomic<int>* resumed_past) {
            ReactorProbe probe{destroyed};
            started->fetch_add(1, std::memory_order_release);
            std::byte buffer[8];
            (void) co_await with_deadline(deadline, recv_some(fd, buffer, sizeof buffer, 0));
            resumed_past->fetch_add(1, std::memory_order_relaxed);
        }

    }

    define_test("kqueue_recv_ready") {
        SocketPair sockets;
        char message[] = "hello";
        [[maybe_unused]] ssize_t sent = ::send(sockets.writer(), message, 5, 0);
        require(sent == 5);

        std::byte buffer[16];
        auto r = co_await with_deadline(steady_clock::now() + seconds(10),
                                        recv_some(sockets.reader(), buffer, sizeof buffer, 0));
        require(r.has_value() && (*r == 5) && (std::memcmp(buffer, "hello", 5) == 0));

        // Peer close is readability; recv returns 0 (end of stream), engaged
        sockets.close_writer();
        auto r2 = co_await with_deadline(steady_clock::now() + seconds(10),
                                         recv_some(sockets.reader(), buffer, sizeof buffer, 0));
        require(r2.has_value() && (*r2 == 0));
        co_return;
    };

    define_test("kqueue_recv_late_data") {
        SocketPair sockets;
        coroutine::Nursery nursery;
        nursery.soon(reactor_late_writer(sockets.writer()));

        std::byte buffer[16];
        auto t0 = steady_clock::now();
        auto r = co_await with_deadline(steady_clock::now() + seconds(10),
                                        recv_some(sockets.reader(), buffer, sizeof buffer, 0));
        require(r.has_value() && (*r == 5) && (std::memcmp(buffer, "later", 5) == 0));
        require(steady_clock::now() - t0 < seconds(5));

        [[maybe_unused]] std::ptrdiff_t cancelled = co_await nursery.join();
        require(cancelled == 0);
        co_return;
    };

    define_test("kqueue_recv_deadline") {
        SocketPair sockets;
        std::byte buffer[16];
        auto t0 = steady_clock::now();
        auto r = co_await with_deadline(t0 + milliseconds(50),
                                        recv_some(sockets.reader(), buffer, sizeof buffer, 0));
        auto elapsed = steady_clock::now() - t0;
        // Timeout is the nullopt channel
        require(!r.has_value());
        require(elapsed >= milliseconds(45));
        require(elapsed < seconds(5));
        co_return;
    };

    define_test("kqueue_recv_bad_fd") {
        std::byte buffer[16];
        auto r = co_await with_deadline(steady_clock::now() + seconds(10),
                                        recv_some(-1, buffer, sizeof buffer, 0));
        // A synchronous registration error is a completion, NOT a timeout:
        // engaged (unlike the deadline path), carrying -errno
        require(r.has_value() && (*r == -EBADF));
        co_return;
    };

    define_test("kqueue_recv_cancel", test_exclusive) {
        FrameCensus census = co_await FrameCensus::open("kqueue_recv_cancel");
        // Outer cancellation of a parked composed wait: 10s deadline, stop
        // lands milliseconds in, the whole tower (recv leaf, timer, recv_some frame,
        // with_deadline, child) unwinds promptly via the destroy cascade.
        SocketPair sockets;
        std::atomic<int> started{0}, destroyed{0}, resumed_past{0};

        coroutine::Nursery nursery;
        nursery.soon(wd_recv_child(sockets.reader(), steady_clock::now() + seconds(10),
                                   &started, &destroyed, &resumed_past));
        while (started.load(std::memory_order_acquire) != 1)
            co_await coroutine::TransferToPoolExecutor{};
        co_await cancelable_sleep(0.010);

        auto t0 = steady_clock::now();
        nursery.request_stop();
        std::ptrdiff_t cancelled = co_await nursery.join();
        auto elapsed = steady_clock::now() - t0;

        require(cancelled == 1);
        require(destroyed.load(std::memory_order_relaxed) == 1);
        require(resumed_past.load(std::memory_order_relaxed) == 0);
        require(elapsed < seconds(2));
        // Both CancelableKEventFrames (recv leaf + deadline timer) retired
        co_await census.drained();
        co_return;
    };

    define_test("kqueue_recv_early_completion_drains", test_exclusive) {
        FrameCensus census = co_await FrameCensus::open("kqueue_recv_early_completion_drains");
        // The scope-exit law, made observable: a recv that completes far
        // before its deadline must reap the deadline timer PROMPTLY, not at
        // the deadline.  With data already waiting, both frames should drain
        // in milliseconds despite a 10 second deadline.
        SocketPair sockets;
        [[maybe_unused]] ssize_t sent = ::send(sockets.writer(), "x", 1, 0);
        require(sent == 1);

        std::byte buffer[16];
        auto r = co_await with_deadline(steady_clock::now() + seconds(10),
                                        recv_some(sockets.reader(), buffer, sizeof buffer, 0));
        require(r.has_value() && (*r == 1));

        // If the timer is only reaped when it fires, this is still 1 after
        // the census's one-second bound
        co_await census.drained();
        co_return;
    };

    namespace {

        // Stops of its own accord: awaiting a stopped Outcome destroys the
        // frame, the stopped completion
        coroutine::Future<int> wd_thrower() {
            throw std::runtime_error("boom");
            co_return 0;
        }

        coroutine::Future<int> wd_self_stopping() {
            execution::Outcome<int> outcome;
            set_stopped(outcome);
            co_return co_await outcome;
        }

    }

    // The deadline and the inner's completion coincide: an inner already
    // past its cancellation point (recv_some after the recv, accept_one
    // after the accept) completes with its value even though the deadline's
    // stop request has reached it.  This inner makes the coincidence
    // deterministic: it parks a stop callback on its token and, when the
    // deadline requests it, completes with a VALUE instead of stopping.  The
    // value must reach the caller; nullopt here is a consumed effect lost
    struct StopToValueSender {
        int _value;
    };

} // namespace wry

namespace wry::execution {
    template<>
    struct SenderTraits<wry::StopToValueSender> {
        using value_type = int;
    };
} // namespace wry::execution

namespace wry {

    template<typename R>
    struct StopToValueOperationState {
        struct Fire {
            StopToValueOperationState* _op;
            void operator()() {
                // Runs inside the deadline's request_stop.  Last touch of the
                // operation state (and of this functor, which it owns): the
                // completion may destroy both, which is allowed for a
                // stop_callback destroyed on the thread running it
                StopToValueOperationState* op = _op;
                int value = op->_value;
                set_value(std::move(op->_receiver), std::move(value));
            }
        };
        int _value;
        R _receiver;
        std::optional<std::stop_callback<Fire>> _callback;
        template<typename R2>
        StopToValueOperationState(int value, R2&& receiver)
        : _value(value), _receiver(std::forward<R2>(receiver)) {}
        StopToValueOperationState(StopToValueOperationState const&) = delete;
    };

    template<typename R>
    auto connect(StopToValueSender&& sender, R&& receiver) {
        return StopToValueOperationState<std::remove_cvref_t<R>>(sender._value, std::forward<R>(receiver));
    }

    template<typename R>
    void start(StopToValueOperationState<R>& op) {
        std::stop_token token = get_stop_token(op._receiver);
        require(!token.stop_requested());  // the test's deadline is in the future
        op._callback.emplace(std::move(token), typename StopToValueOperationState<R>::Fire{&op});
    }

    define_test("kqueue_with_deadline_value_at_deadline", test_exclusive) {
        FrameCensus census = co_await FrameCensus::open("kqueue_with_deadline_value_at_deadline");
        for (int i = 0; i != 4; ++i) {
            auto t0 = steady_clock::now();
            auto r = co_await with_deadline(t0 + milliseconds(20), StopToValueSender{42 + i});
            // Completed at the deadline, and with the inner's value
            require(steady_clock::now() - t0 >= milliseconds(15));
            require(r.has_value() && (*r == 42 + i));
        }
        co_await census.drained();
        co_return;
    };

    define_test("kqueue_with_deadline_senders", test_exclusive) {
        FrameCensus census = co_await FrameCensus::open("kqueue_with_deadline_senders");
        // with_deadline takes any sender, not only a Future; each case must
        // also withdraw its 10 second timer promptly
        auto t0 = steady_clock::now();

        // A sender that completes inside start wins at once; the timer
        // entrant starts under the already-requested interior token
        auto r = co_await with_deadline(steady_clock::now() + seconds(10), execution::just(5));
        require(r.has_value() && (*r == 5));

        // An inner that stops of its own accord is nullopt, delivered
        // without waiting for the deadline (the outer was not requested)
        auto s = co_await with_deadline(steady_clock::now() + seconds(10), wd_self_stopping());
        require(!s.has_value());

        // A void inner yields optional<monostate>
        auto v = co_await with_deadline(steady_clock::now() + seconds(10), cancelable_sleep(0.001));
        require(v.has_value());

        // An error wins like a value
        bool caught = false;
        try {
            (void) co_await with_deadline(steady_clock::now() + seconds(10), wd_thrower());
        } catch (std::runtime_error const&) {
            caught = true;
        }
        require(caught);

        // sync_wait takes it as a sender directly
        auto o = execution::sync_wait(with_deadline(steady_clock::now() + seconds(10), execution::just(7)));
        require(o.value() == std::optional<int>{7});

        require(steady_clock::now() - t0 < seconds(5));
        co_await census.drained();
        co_return;
    };

    namespace {

        // A child parked in cancelable_sleep.  The probe counts its frame's
        // destruction; resumed_past counts a resume past the sleep.
        coroutine::Task sleep_child(double seconds,
                                    std::atomic<int>* started,
                                    std::atomic<int>* destroyed,
                                    std::atomic<int>* resumed_past) {
            ReactorProbe probe{destroyed};
            started->fetch_add(1, std::memory_order_release);
            co_await cancelable_sleep(seconds);
            resumed_past->fetch_add(1, std::memory_order_relaxed);
        }

        // A child parked in a bare recv_some (no deadline)
        coroutine::Task recv_child(int fd,
                                   std::atomic<int>* started,
                                   std::atomic<int>* destroyed,
                                   std::atomic<int>* resumed_past) {
            ReactorProbe probe{destroyed};
            started->fetch_add(1, std::memory_order_release);
            std::byte buffer[16];
            (void) co_await recv_some(fd, buffer, sizeof buffer, 0);
            resumed_past->fetch_add(1, std::memory_order_relaxed);
        }

    }

    // Replaces coroutine_timed_stop_source (deleted with
    // RequestStopAfterAwaitable): a sleeping host resumes normally at the
    // deadline, and an outer stop request destroys a sleeping host promptly.
    // The watchdog half of that test -- request an interior source at a
    // deadline -- is with_deadline's timer, covered by kqueue_recv_deadline.
    define_test("kqueue_cancelable_sleep", test_exclusive) {
        FrameCensus census = co_await FrameCensus::open("kqueue_cancelable_sleep");

        // Duration path: the sleeper resumes after the deadline and its
        // frame is reaped promptly on the fire, token never requested
        {
            auto t0 = steady_clock::now();
            co_await cancelable_sleep(0.020);
            auto elapsed = steady_clock::now() - t0;
            require(elapsed >= milliseconds(15));
            require(elapsed < seconds(5));
            co_await census.drained();
        }

        // Zero-duration burst: the fire races spawn's own installation of
        // the continuation (the awaitable form of the early-fire path).
        // Every sleep must resume exactly once and every frame must retire.
        {
            for (int i = 0; i != 100; ++i)
                co_await cancelable_sleep(0.0);
            co_await census.drained();
        }

        // Parked cancel: a stop request against a child asleep for 10s
        // destroys it promptly through the destroy cascade; the timer knote
        // is withdrawn and its frame retired
        {
            std::atomic<int> started{0}, destroyed{0}, resumed_past{0};
            coroutine::Nursery nursery;
            nursery.soon(sleep_child(10.0, &started, &destroyed, &resumed_past));
            while (started.load(std::memory_order_acquire) != 1)
                co_await coroutine::TransferToPoolExecutor{};
            // Settle past the arming window so this exercises the parked
            // cancel, not the install race
            co_await cancelable_sleep(0.010);

            auto t0 = steady_clock::now();
            nursery.request_stop();
            std::ptrdiff_t cancelled = co_await nursery.join();
            auto elapsed = steady_clock::now() - t0;

            require(cancelled == 1);
            require(destroyed.load(std::memory_order_relaxed) == 1);
            require(resumed_past.load(std::memory_order_relaxed) == 0);
            require(elapsed < seconds(2));
            co_await census.drained();
        }

        // Cancel before park: the child's token is already requested when it
        // reaches the sleep, so the stop callback fires synchronously inside
        // spawn, which destroys the child right there -- inside the
        // awaitable's own await_suspend.  The awaitable must not touch its
        // frame after spawn returns.
        {
            std::atomic<int> started{0}, destroyed{0}, resumed_past{0};
            coroutine::Nursery nursery;
            nursery.request_stop();  // interior source requested before the fork
            nursery.soon(sleep_child(10.0, &started, &destroyed, &resumed_past));
            std::ptrdiff_t cancelled = co_await nursery.join();
            require(cancelled == 1);
            require(started.load(std::memory_order_relaxed) == 1);
            require(destroyed.load(std::memory_order_relaxed) == 1);
            require(resumed_past.load(std::memory_order_relaxed) == 0);
            // The nursery retires inside the child's destroy, which spawn
            // runs before it releases the Frame: spin rather than assert
            co_await census.drained();
        }

        co_return;
    };

    // ---- send_some, accept, connect ------------------------------------------

    namespace {

        void make_nonblocking(int fd) {
            int flags = ::fcntl(fd, F_GETFL, 0);
            assert(flags != -1);
            int rc = ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
            assert(rc != -1);
            (void) rc;
        }

        void set_nosigpipe(int fd) {
            int one = 1;
            int rc = ::setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
            assert(rc == 0);
            (void) rc;
        }

        // Fill a non-blocking socket's send buffer; returns the byte count
        size_t fill_send_buffer(int fd) {
            std::byte junk[4096] = {};
            size_t total = 0;
            for (;;) {
                ssize_t n = ::send(fd, junk, sizeof junk, 0);
                if (n == -1) {
                    assert(errno == EAGAIN);
                    return total;
                }
                total += (size_t)n;
            }
        }

        // Drains a non-blocking socket after a delay until `expected` bytes
        // have been seen, polling across the gaps (it is the other end of a
        // parked send_some, so it must never block a worker)
        coroutine::Task reactor_late_drain(int fd, size_t expected) {
            co_await cancelable_sleep(0.010);
            std::byte sink[65536];
            size_t total = 0;
            while (total < expected) {
                ssize_t n = ::recv(fd, sink, sizeof sink, 0);
                if (n > 0) {
                    total += (size_t)n;
                } else {
                    assert((n == -1) && (errno == EAGAIN));
                    co_await cancelable_sleep(0.001);
                }
            }
        }

        // A child parked in send_some on a full socket
        coroutine::Task send_child(int fd,
                                   std::atomic<int>* started,
                                   std::atomic<int>* destroyed,
                                   std::atomic<int>* resumed_past) {
            ReactorProbe probe{destroyed};
            started->fetch_add(1, std::memory_order_release);
            std::byte buffer[16] = {};
            (void) co_await send_some(fd, buffer, sizeof buffer, 0);
            resumed_past->fetch_add(1, std::memory_order_relaxed);
        }

        struct ListeningSocket {
            std::string _path;
            sockaddr_un _address = {};
            int _fd = -1;
            ListeningSocket() {
                _path = "/tmp/wry-kqueue-accept-" + std::to_string(::getpid()) + ".sock";
                _address.sun_family = AF_UNIX;
                std::memcpy(_address.sun_path, _path.data(), _path.size());
                (void) ::unlink(_path.c_str());
                _fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
                assert(_fd != -1);
                int rc = ::bind(_fd, (sockaddr const*)&_address, sizeof _address);
                assert(rc == 0);
                rc = ::listen(_fd, 4);
                assert(rc == 0);
                (void) rc;
                make_nonblocking(_fd);
            }
            ~ListeningSocket() {
                ::close(_fd);
                (void) ::unlink(_path.c_str());
            }
        };

        // A child parked in accept on a listening socket nobody connects to
        coroutine::Task accept_child(int fd,
                                     std::atomic<int>* started,
                                     std::atomic<int>* destroyed,
                                     std::atomic<int>* resumed_past) {
            ReactorProbe probe{destroyed};
            started->fetch_add(1, std::memory_order_release);
            (void) co_await accept_one(fd, nullptr, nullptr);
            resumed_past->fetch_add(1, std::memory_order_relaxed);
        }

        // Connects to the listener after a delay; the connect itself is the
        // reactor's, on a non-blocking socket
        coroutine::Task late_connector(sockaddr_un const* address, std::atomic<int>* result, int* fd_out) {
            co_await cancelable_sleep(0.010);
            int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
            assert(fd != -1);
            make_nonblocking(fd);
            int r = co_await connect_to(fd, (sockaddr const*)address, sizeof *address);
            *fd_out = fd;
            result->store(r, std::memory_order_release);
        }

    }

    define_test("kqueue_send_some", test_exclusive) {
        FrameCensus census = co_await FrameCensus::open("kqueue_send_some");

        // Ready path: an empty socket is writable at once
        {
            SocketPair sockets;
            char message[] = "hello";
            auto r = co_await with_deadline(steady_clock::now() + seconds(10),
                                            send_some(sockets.writer(), message, 5, 0));
            require(r.has_value() && (*r == 5));
            std::byte buffer[16];
            ssize_t n = ::recv(sockets.reader(), buffer, sizeof buffer, 0);
            require((n == 5) && (std::memcmp(buffer, "hello", 5) == 0));
            (void) n;
        }

        // Parked path: the send buffer is full, so the send parks until a
        // reader drains the other end
        {
            SocketPair sockets;
            make_nonblocking(sockets.writer());
            make_nonblocking(sockets.reader());
            size_t filled = fill_send_buffer(sockets.writer());
            require(filled > 0);
            coroutine::Nursery nursery;
            nursery.soon(reactor_late_drain(sockets.reader(), filled + 5));
            char message[] = "later";
            auto t0 = steady_clock::now();
            auto r = co_await with_deadline(steady_clock::now() + seconds(10),
                                            send_some(sockets.writer(), message, 5, 0));
            require(r.has_value() && (*r == 5));
            require(steady_clock::now() - t0 < seconds(5));
            [[maybe_unused]] std::ptrdiff_t cancelled = co_await nursery.join();
            require(cancelled == 0);
        }

        // Peer close: writability with EV_EOF, and send reports EPIPE in
        // kernel convention (SO_NOSIGPIPE keeps the signal away)
        {
            SocketPair sockets;
            set_nosigpipe(sockets.writer());
            ::close(sockets._fds[0]);
            sockets._fds[0] = -1;
            char message[] = "x";
            auto r = co_await with_deadline(steady_clock::now() + seconds(10),
                                            send_some(sockets.writer(), message, 1, 0));
            require(r.has_value() && (*r == -EPIPE));
        }

        // Bad descriptor: the registration fails synchronously, -EBADF
        {
            char message[] = "x";
            auto r = co_await with_deadline(steady_clock::now() + seconds(10),
                                            send_some(-1, message, 1, 0));
            require(r.has_value() && (*r == -EBADF));
        }

        // Parked cancel: a child blocked on a full socket unwinds promptly
        // and the EVFILT_WRITE knote is withdrawn
        {
            SocketPair sockets;
            make_nonblocking(sockets.writer());
            (void) fill_send_buffer(sockets.writer());
            std::atomic<int> started{0}, destroyed{0}, resumed_past{0};
            coroutine::Nursery nursery;
            nursery.soon(send_child(sockets.writer(), &started, &destroyed, &resumed_past));
            while (started.load(std::memory_order_acquire) != 1)
                co_await coroutine::TransferToPoolExecutor{};
            co_await cancelable_sleep(0.010);
            auto t0 = steady_clock::now();
            nursery.request_stop();
            std::ptrdiff_t cancelled = co_await nursery.join();
            require(cancelled == 1);
            require(destroyed.load(std::memory_order_relaxed) == 1);
            require(resumed_past.load(std::memory_order_relaxed) == 0);
            require(steady_clock::now() - t0 < seconds(2));
            co_await census.drained();
        }

        co_return;
    };

    define_test("kqueue_accept_connect", test_exclusive) {
        FrameCensus census = co_await FrameCensus::open("kqueue_accept_connect");

        // A parked accept completes when a connector arrives; the connect is
        // itself asynchronous; the pair then talks through send_some and
        // recv_some in both directions
        {
            ListeningSocket listener;
            std::atomic<int> connect_result{-1};
            int client = -1;
            coroutine::Nursery nursery;
            nursery.soon(late_connector(&listener._address, &connect_result, &client));
            auto t0 = steady_clock::now();
            auto r = co_await with_deadline(steady_clock::now() + seconds(10),
                                            accept_one(listener._fd, nullptr, nullptr));
            require(r.has_value() && (*r >= 0));
            require(steady_clock::now() - t0 < seconds(5));
            int server = *r;
            [[maybe_unused]] std::ptrdiff_t cancelled = co_await nursery.join();
            require(cancelled == 0);
            require(connect_result.load(std::memory_order_acquire) == 0);
            require(client != -1);

            char ping[] = "ping";
            auto s = co_await with_deadline(steady_clock::now() + seconds(10),
                                            send_some(client, ping, 4, 0));
            require(s.has_value() && (*s == 4));
            std::byte buffer[16];
            auto v = co_await with_deadline(steady_clock::now() + seconds(10),
                                            recv_some(server, buffer, sizeof buffer, 0));
            require(v.has_value() && (*v == 4) && (std::memcmp(buffer, "ping", 4) == 0));
            char pong[] = "pong";
            s = co_await with_deadline(steady_clock::now() + seconds(10),
                                       send_some(server, pong, 4, 0));
            require(s.has_value() && (*s == 4));
            v = co_await with_deadline(steady_clock::now() + seconds(10),
                                       recv_some(client, buffer, sizeof buffer, 0));
            require(v.has_value() && (*v == 4) && (std::memcmp(buffer, "pong", 4) == 0));
            ::close(client);
            ::close(server);
        }

        // Connect to nothing: a Unix domain connect fails synchronously, in
        // kernel convention, without suspending
        {
            sockaddr_un address = {};
            address.sun_family = AF_UNIX;
            std::string path = "/tmp/wry-kqueue-nobody-" + std::to_string(::getpid()) + ".sock";
            std::memcpy(address.sun_path, path.data(), path.size());
            int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
            require(fd != -1);
            make_nonblocking(fd);
            int r = co_await connect_to(fd, (sockaddr const*)&address, sizeof address);
            require(r == -ENOENT);
            ::close(fd);
        }

        // Accept with a deadline and nobody connecting: the timeout
        {
            ListeningSocket listener;
            auto t0 = steady_clock::now();
            auto r = co_await with_deadline(t0 + milliseconds(50),
                                            accept_one(listener._fd, nullptr, nullptr));
            require(!r.has_value());
            auto elapsed = steady_clock::now() - t0;
            require((elapsed >= milliseconds(45)) && (elapsed < seconds(5)));
        }

        // Parked cancel: a child blocked in accept unwinds promptly
        {
            ListeningSocket listener;
            std::atomic<int> started{0}, destroyed{0}, resumed_past{0};
            coroutine::Nursery nursery;
            nursery.soon(accept_child(listener._fd, &started, &destroyed, &resumed_past));
            while (started.load(std::memory_order_acquire) != 1)
                co_await coroutine::TransferToPoolExecutor{};
            co_await cancelable_sleep(0.010);
            auto t0 = steady_clock::now();
            nursery.request_stop();
            std::ptrdiff_t cancelled = co_await nursery.join();
            require(cancelled == 1);
            require(destroyed.load(std::memory_order_relaxed) == 1);
            require(resumed_past.load(std::memory_order_relaxed) == 0);
            require(steady_clock::now() - t0 < seconds(2));
            co_await census.drained();
        }

        co_return;
    };

    // The recv awaitable's copy of the cancel-before-park path, bare and
    // composed with with_deadline (whose bridge callback fires synchronously
    // against a pre-requested outer token, cancelling both arms at arm time)
    define_test("kqueue_recv_cancel_before_park", test_exclusive) {
        FrameCensus census = co_await FrameCensus::open("kqueue_recv_cancel_before_park");

        {
            SocketPair sockets;
            std::atomic<int> started{0}, destroyed{0}, resumed_past{0};
            coroutine::Nursery nursery;
            nursery.request_stop();
            nursery.soon(recv_child(sockets.reader(), &started, &destroyed, &resumed_past));
            std::ptrdiff_t cancelled = co_await nursery.join();
            require(cancelled == 1);
            require(started.load(std::memory_order_relaxed) == 1);
            require(destroyed.load(std::memory_order_relaxed) == 1);
            require(resumed_past.load(std::memory_order_relaxed) == 0);
            co_await census.drained();
        }

        {
            SocketPair sockets;
            std::atomic<int> started{0}, destroyed{0}, resumed_past{0};
            coroutine::Nursery nursery;
            nursery.request_stop();
            nursery.soon(wd_recv_child(sockets.reader(), steady_clock::now() + seconds(10),
                                       &started, &destroyed, &resumed_past));
            std::ptrdiff_t cancelled = co_await nursery.join();
            require(cancelled == 1);
            require(started.load(std::memory_order_relaxed) == 1);
            require(destroyed.load(std::memory_order_relaxed) == 1);
            require(resumed_past.load(std::memory_order_relaxed) == 0);
            co_await census.drained();
        }

        co_return;
    };


} // namespace wry
