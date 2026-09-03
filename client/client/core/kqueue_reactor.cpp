//
//  kqueue_reactor.cpp
//  client
//
//  Created by Antony Searle on 22/11/2025.
//

#include <sys/event.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <cstring>

#include <mutex>
#include <optional>
#include <stop_token>
#include <thread>

#include "kqueue_reactor.hpp"
#include "test.hpp"

namespace wry {

    namespace {

        // ---- Reactor thread ------------------------------------------------

        constinit int g_reactor_kqueue = -1;
        std::once_flag g_reactor_once;
        std::thread g_reactor_thread;

        void reactor_run();

        // The shutdown doorbell is a persistent EVFILT_USER knote, ident 0.
        constexpr uint64_t DOORBELL_IDENT = 0;

        // All kevent64 calls made off the reactor thread pass
        // KEVENT_FLAG_ERROR_EVENTS so an immediate call can never retrieve
        // (and thereby consume) another operation's one-shot event into its
        // own eventlist; errors and receipts come back, deliveries do not.
        constexpr unsigned IMMEDIATE =
            KEVENT_FLAG_IMMEDIATE | KEVENT_FLAG_ERROR_EVENTS;

        // ---- Wait record ---------------------------------------------------
        //
        // One heap record per recv_some wait, shared by four parties: the
        // read knote, the timer knote, the waiting frame, and the in-progress
        // registration in await_suspend.  The record outlives the frame: a
        // cancelled frame is destroyed while a knote may still be in flight
        // to the reactor, whose udata must keep pointing at live memory.
        //
        // State machine (one atomic word):
        //
        //   ARMING -> PENDING            registration handover (await_suspend)
        //   ARMING -> STOP_PENDING       stop arrived mid-registration; the
        //                                registering thread owns cancellation
        //                                (the frame must not be destroyed
        //                                under a running await_suspend)
        //   ARMING -> ERRORED            read registration failed; completes
        //                                synchronously with the errno
        //   ARMING | PENDING -> READABLE reactor: fd event won
        //   ARMING | PENDING -> TIMED_OUT reactor: timer won
        //   PENDING -> STOPPED           stop callback won; the frame is
        //                                destroyed on a worker (unwinding)
        //
        // Terminal states absorb the losers.  The winner owns the waiter:
        // READABLE/TIMED_OUT schedule a resume, STOPPED schedules a destroy,
        // STOP_PENDING/ERRORED are handled inline by the registering thread.
        //
        // ORDER: the record's plain fields (_handle, _fd) are written before
        // the store_release of ARMING; every subsequent transition is an
        // acq_rel RMW continuing that release sequence, so whichever thread
        // wins a transition also acquires the fields.  (The kernel transports
        // only the udata pointer value; all cross-thread memory access is
        // ordered by _state, which ThreadSanitizer follows -- no annotations
        // needed, cf. the fence-annotation rule.)
        //
        // References: each knote holds one (retired by whoever provably
        // removes it: an EV_DELETE receipt of 0, or delivery), the frame
        // holds one (dropped by the awaitable's guard), and the registration
        // holds one (dropped when await_suspend finishes).  The _arms bitmask
        // makes knote retirement idempotent, so a lying delete receipt can
        // only leak, never double-free.

        enum : uintptr_t {
            ARMING,
            PENDING,
            STOP_PENDING,
            READABLE,
            TIMED_OUT,
            ERRORED,
            STOPPED,
        };

        enum : uintptr_t {
            ARM_READ = 1,
            ARM_TIMER = 2,
        };

        struct RecvWait {

            Atomic<uintptr_t> _state{};
            Atomic<uintptr_t> _arms{};
            Atomic<std::ptrdiff_t> _references{};
            std::coroutine_handle<> _handle;
            int _fd = -1;
            int _errno = 0;

            void unref() {
                if (_references.sub_fetch_release(1) == 0) {
                    (void) _references.load_acquire();
                    delete this;
                }
            }

            // Retire a knote's reference exactly once, no matter how many
            // parties observe its removal.
            void retire_arm(uintptr_t bit) {
                if (_arms.fetch_and_relaxed(~bit) & bit)
                    unref();
            }

            // EV_DELETE with EV_RECEIPT, callable from any thread.  A receipt
            // of 0 means we removed the knote before delivery and retire its
            // reference; ENOENT means it was already delivered (the reactor
            // retires it) or never armed (already retired at arm time).
            void delete_arm(int16_t filter, uint64_t ident, uintptr_t bit) {
                kevent64_s change = {
                    .ident = ident,
                    .filter = filter,
                    .flags = EV_DELETE | EV_RECEIPT,
                };
                kevent64_s receipt = {};
                int rc = ::kevent64(g_reactor_kqueue, &change, 1, &receipt, 1,
                                    IMMEDIATE, nullptr);
                if (rc != 1)
                    abort();
                assert(receipt.flags & EV_ERROR);
                switch (receipt.data) {
                    case 0:
                        retire_arm(bit);
                        break;
                    case ENOENT:
                        break;
                    default:
                        abort();
                }
            }

            void delete_read_arm() {
                delete_arm(EVFILT_READ, (uint64_t)_fd, ARM_READ);
            }

            void delete_timer_arm() {
                delete_arm(EVFILT_TIMER, (uint64_t)this, ARM_TIMER);
            }

        }; // struct RecvWait

        // Cancellation destroys the parked frame, which must happen on a
        // mutator-pinned worker (frame destructors shade Roots), never on the
        // canceller's thread or the reactor.  A two-word fake frame carries
        // the destroy through the work queue's resume ABI.
        struct DestroyThunk {
            void (*_resume)(void*) = &_run;
            std::coroutine_handle<> _handle;
            static void _run(void* ptr) {
                auto* self = (DestroyThunk*)ptr;
                std::coroutine_handle<> handle = self->_handle;
                delete self;
                handle.destroy();
            }
        };

        void schedule_destroy(std::coroutine_handle<> handle) {
            global_work_queue_schedule(new DestroyThunk{._handle = handle});
        }

        // The stop callback: one CAS plus knote withdrawal, nothing else.  It
        // runs on the requesting thread at an arbitrary moment; the record is
        // kept alive under it by the frame's reference, because the awaitable
        // destroys its stop_callback member (whose destructor waits out an
        // in-flight invocation) before its record guard drops that reference.
        struct RecvWaitOnStop {
            RecvWait* _record;
            void operator()() const noexcept {
                uintptr_t expected = _record->_state.load_relaxed();
                for (;;) {
                    if (expected == ARMING) {
                        // Registration is still running on the awaiting
                        // thread; hand it the cancellation.
                        if (_record->_state.compare_exchange_weak_acq_rel_relaxed(
                                expected, STOP_PENDING))
                            return;
                    } else if (expected == PENDING) {
                        if (_record->_state.compare_exchange_weak_acq_rel_relaxed(
                                expected, STOPPED)) {
                            _record->delete_read_arm();
                            _record->delete_timer_arm();
                            schedule_destroy(_record->_handle);
                            return;
                        }
                    } else {
                        // A completion already owns the waiter
                        return;
                    }
                }
            }
        };

        // ---- Reactor service loop ------------------------------------------

        std::atomic_int_fast8_t g_reactor_tsan_helper{0};

        void reactor_run() {
            pthread_setname_np("R0");
            for (;;) {
                const int NEVENTS = 16;
                kevent64_s events[NEVENTS];
                int n = ::kevent64(g_reactor_kqueue, nullptr, 0,
                                   events, NEVENTS, 0, nullptr);
                // AFTER the harvest, before touching any udata: pairs with
                // the publishers' release pokes before their kevent ADDs.
                // (Before the blocking call it reads a pre-park value and the
                // edge forms one iteration late -- covering every arm except
                // the ones delivered by this very harvest.)
                g_reactor_tsan_helper.load(std::memory_order_acquire);
                if (n == -1) {
                    if (errno == EINTR)
                        continue;
                    perror("reactor kevent64");
                    abort();
                }
                assert((0 < n) && (n <= NEVENTS));
                for (int i = 0; i != n; ++i) {
                    kevent64_s const& event = events[i];
                    assert(!(event.flags & EV_ERROR));
                    if (event.filter == EVFILT_USER) {
                        // Only the shutdown doorbell registers EVFILT_USER
                        // today; a future user-event tenant must be
                        // dispatched here, not mistaken for shutdown.
                        assert(event.ident == DOORBELL_IDENT);
                        // Returning drops any later events in this batch,
                        // which the stop contract makes unreachable: all
                        // waits have drained, so nothing else can be armed.
                        assert(i == n - 1);
                        return;
                    }
                    // TODO: This is too much intelligence in the reactor.
                    // Propose that except for doorbell, we always resume
                    // udata; this is the single-pointer way of doing an
                    // arbitrary thing.
                    if ((event.filter != EVFILT_READ) && (event.ident == 0)) {
                        // The witness acquire above ordered the publisher's
                        // construction before this dispatch; no per-frame
                        // pairing needed
                        global_work_queue_schedule((void*)event.udata);
                        // The event is consumed; without this, the RecvWait
                        // path below would type-pun the frame's Header (kept
                        // benign only by pointer alignment)
                        continue;
                    }
                    assert((event.filter == EVFILT_READ) ||
                           (event.filter == EVFILT_TIMER));
                    bool is_read = (event.filter == EVFILT_READ);
                    auto* record = (RecvWait*)event.udata;
                    // The kernel carried only the pointer; this acquire is
                    // the reactor's first access and pairs with the armer's
                    // store_release(ARMING), covering the record's
                    // initialization (a relaxed first load would race with
                    // it -- TSan-caught 2026-08-29).
                    uintptr_t expected = record->_state.load_acquire();
                    for (;;) {
                        if ((expected != ARMING) && (expected != PENDING))
                            break;  // lost to the other arm or to cancellation
                        if (record->_state.compare_exchange_weak_acq_rel_relaxed(
                                expected, is_read ? READABLE : TIMED_OUT)) {
                            // Won: withdraw the losing arm, wake the waiter
                            if (is_read)
                                record->delete_timer_arm();
                            else
                                record->delete_read_arm();
                            global_work_queue_schedule(record->_handle.address());
                            break;
                        }
                    }
                    record->retire_arm(is_read ? ARM_READ : ARM_TIMER);
                }
            }
        }

        // ---- The awaitable -------------------------------------------------

        struct RecvSomeAwaitable {

            int _fd;
            std::span<std::byte> _buffer;
            std::chrono::steady_clock::time_point _deadline;

            // Declaration order is load-bearing: _guard must outlive _on_stop
            // so that when the frame is destroyed, the stop_callback (whose
            // destructor waits out an in-flight invocation) dies while the
            // record it touches is still referenced.
            struct Guard {
                RecvWait* _record = nullptr;
                ~Guard() {
                    if (_record)
                        _record->unref();
                }
            };
            Guard _guard;
            std::optional<std::stop_callback<RecvWaitOnStop>> _on_stop;

            constexpr bool await_ready() const noexcept { return false; }

            template<typename U>
            bool await_suspend(std::coroutine_handle<Coroutine::Promise<U>> handle) {
                std::stop_token token = get_stop_token(handle);

                // Early-out keeps the already-cancelled path synchronous: we
                // must not let the callback constructor run the cancellation
                // from inside our own registration.
                if (token.stop_requested()) {
                    handle.destroy();  // cancellation unwinding, on this thread
                    return true;       // the frame is gone; never resume
                }

                global_reactor_start();

                auto* record = new RecvWait;
                record->_handle = handle;
                record->_fd = _fd;
                record->_references.store_relaxed(4);  // read, timer, frame, us
                record->_arms.store_relaxed(ARM_READ | ARM_TIMER);
                record->_state.store_release(ARMING);  // publish the fields
                _guard._record = record;

                // From here a concurrent request_stop can fire the callback:
                // below we touch only locals and the (reference-held) record,
                // never the frame -- it may be resumed, or on STOP_PENDING
                // destroyed by us, before this function returns.
                _on_stop.emplace(token, RecvWaitOnStop{record});

                // Arm the read knote.  EV_RECEIPT reports registration
                // failure (e.g. a dead fd) without consuming deliveries.
                {
                    kevent64_s change = {
                        .ident = (uint64_t)_fd,
                        .filter = EVFILT_READ,
                        .flags = EV_ADD | EV_ONESHOT | EV_RECEIPT,
                        .udata = (uint64_t)record,
                    };
                    kevent64_s receipt = {};
                    int rc = ::kevent64(g_reactor_kqueue, &change, 1,
                                        &receipt, 1, IMMEDIATE, nullptr);
                    if (rc != 1)
                        abort();
                    assert(receipt.flags & EV_ERROR);
                    if (receipt.data != 0) {
                        // Nothing armed; complete synchronously.
                        record->_errno = (int)receipt.data;
                        record->retire_arm(ARM_READ);
                        record->retire_arm(ARM_TIMER);
                        uintptr_t expected = ARMING;
                        if (record->_state.compare_exchange_strong_release_acquire(
                                expected, ERRORED)) {
                            record->unref();  // registration reference
                            return false;     // resume now; await_resume reports
                        }
                        // Cancellation arrived mid-registration and deferred
                        // to us
                        assert(expected == STOP_PENDING);
                        record->unref();
                        handle.destroy();
                        return true;
                    }
                }

                // Arm the timer knote, unless the wait has already resolved
                // (best-effort check; an orphan timer would fire, lose, and
                // retire itself) or there is no deadline.
                if ((_deadline != std::chrono::steady_clock::time_point::max()) &&
                    (record->_state.load_relaxed() == ARMING)) {
                    auto now = std::chrono::steady_clock::now();
                    int64_t ns = (_deadline > now)
                        ? std::chrono::duration_cast<std::chrono::nanoseconds>(
                              _deadline - now).count()
                        : 0;
                    kevent64_s change = {
                        .ident = (uint64_t)record,
                        .filter = EVFILT_TIMER,
                        .flags = EV_ADD | EV_ONESHOT | EV_RECEIPT,
                        .fflags = NOTE_NSECONDS,
                        .data = ns,
                        .udata = (uint64_t)record,
                    };
                    kevent64_s receipt = {};
                    int rc = ::kevent64(g_reactor_kqueue, &change, 1,
                                        &receipt, 1, IMMEDIATE, nullptr);
                    if ((rc != 1) || (receipt.data != 0))
                        abort();  // timers fail only on resource exhaustion
                } else {
                    record->retire_arm(ARM_TIMER);
                }

                // Hand over.  On failure the wait already resolved (the
                // resume is scheduled) or stop arrived mid-arming and we own
                // the cancellation.
                uintptr_t expected = ARMING;
                if (!record->_state.compare_exchange_strong_release_acquire(
                        expected, PENDING)) {
                    if (expected == STOP_PENDING) {
                        record->delete_read_arm();
                        record->delete_timer_arm();
                        record->unref();  // registration reference
                        handle.destroy();
                        return true;
                    }
                    assert((expected == READABLE) || (expected == TIMED_OUT));
                }
                record->unref();  // registration reference
                return true;
            }

            std::expected<size_t, int> await_resume() {
                // Quiesce the callback before reading anything it may touch;
                // a concurrent invocation that lost its CAS is waited out
                // here.
                _on_stop.reset();
                switch (_guard._record->_state.load_acquire()) {
                    case READABLE: {
                        ssize_t n = ::recv(_fd, _buffer.data(), _buffer.size(), 0);
                        if (n >= 0)
                            return (size_t)n;
                        return std::unexpected(errno);
                    }
                    case TIMED_OUT:
                        return std::unexpected(ETIMEDOUT);
                    case ERRORED:
                        return std::unexpected(_guard._record->_errno);
                    default:
                        abort();
                }
            }

        }; // struct RecvSomeAwaitable

    } // namespace

    void global_reactor_start() {
        std::call_once(g_reactor_once, [] {
            g_reactor_kqueue = kqueue();
            if (g_reactor_kqueue == -1) {
                perror("kqueue");
                abort();
            }
            kevent64_s change = {
                .ident = DOORBELL_IDENT,
                .filter = EVFILT_USER,
                .flags = EV_ADD | EV_CLEAR,
            };
            if (::kevent64(g_reactor_kqueue, &change, 1, nullptr, 0,
                           IMMEDIATE, nullptr) != 0)
                abort();
            g_reactor_thread = std::thread(&reactor_run);
        });
    }

    void global_reactor_stop() {
        if (!g_reactor_thread.joinable())
            return;
        kevent64_s change = {
            .ident = DOORBELL_IDENT,
            .filter = EVFILT_USER,
            .fflags = NOTE_TRIGGER,
        };
        if (::kevent64(g_reactor_kqueue, &change, 1, nullptr, 0,
                       IMMEDIATE, nullptr) != 0)
            abort();
        g_reactor_thread.join();
        ::close(g_reactor_kqueue);
        g_reactor_kqueue = -1;
    }

    Coroutine::Future<std::expected<size_t, int>>
    recv_some(int file_descriptor,
              std::span<std::byte> buffer,
              std::chrono::steady_clock::time_point deadline) {
        co_return co_await RecvSomeAwaitable{file_descriptor, buffer, deadline};
    }







    // Test observability: live CancelableTimerFrame count.  The tests pin
    // the load-bearing property -- the Frame dies promptly after the FIRST
    // of {timer fired, stop requested} -- which is invisible without this.
    constinit std::atomic<std::ptrdiff_t> g_cancelable_timer_frame_count{0};

    struct CancelableKEventFrame {

        using Frame = CancelableKEventFrame;

        enum : uintptr_t {
            RESUMED = 1,
            STOPPED = 2,
        };

        struct Callback {
            Frame* frame;
            void operator()() {
                // We have reached here because stop was requested
                assert(frame->_stop_token.stop_requested());
                // Race to claim the continuation
                intptr_t before = frame->_continuation.fetch_or(STOPPED, std::memory_order_relaxed);
                intptr_t resumed = before & RESUMED;
                intptr_t stopped = before & STOPPED;
                intptr_t continuation = before & ~(STOPPED | RESUMED);
                assert(!stopped);
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
                if (continuation && !resumed) {
                    frame->_continuation.load(std::memory_order_acquire);
                    // Inline destruction keeps us ordered with respect to
                    // request_stop returning
                    Coroutine::destroy_by_address((void*)before);
                }
                frame->release();
            }
        };

        Coroutine::Header _header = { &static_resume, nullptr };
        std::stop_token _stop_token;
        std::atomic<uintptr_t> _continuation{0};
        std::atomic<ptrdiff_t> _reference_count_minus_one{2}; // initial condition: 3 owners
        std::optional<std::stop_callback<Callback>> _stop_callback;
        kevent64_s _change;

        void acquire() {
            _reference_count_minus_one.fetch_add(1, std::memory_order_relaxed);
        }

        void release() {
            if (!_reference_count_minus_one.fetch_sub(1, std::memory_order_release)) {
                (void) _reference_count_minus_one.load(std::memory_order_acquire);
                delete this;
                g_cancelable_timer_frame_count.fetch_sub(1, std::memory_order_release);
            }
        }

        static void static_resume(void* ptr) {
            // We have reached here because the timer event fired
            CancelableKEventFrame* frame = (CancelableKEventFrame*)ptr;
            // Because the timer fired, we can kill the stop_callback
            // Safety: ~_stop_callback and request_stop are mutually excluded
            frame->_stop_callback.reset();
            // Race to take the continuation
            intptr_t before = frame->_continuation.fetch_or(RESUMED, std::memory_order_relaxed);
            intptr_t resumed = before & RESUMED;
            intptr_t stopped = before & STOPPED;
            intptr_t continuation = before & ~(STOPPED | RESUMED);
            assert(!resumed);
            // We deliberately don't poll the stop_token because doing so
            // doesn't prevent the race, it just narrows the window
            if (!stopped) {
                // We destroyed the stop callback before it fired, and inherit
                // its ownership
                frame->release();
            }
            if (continuation && !stopped) {
                frame->_continuation.load(std::memory_order_acquire);
                global_work_queue_schedule((void*)before);
            }
            frame->release();
        }

        [[nodiscard]] int64_t _kevent64(uint16_t flags) {
            kevent64_s change = _change;
            kevent64_s event = {};
            change.flags = flags;
            change.udata =  (uint64_t)this;
            g_reactor_tsan_helper.fetch_or(0, std::memory_order_release);
            int n = kevent64(g_reactor_kqueue, &change, 1, &event, 1, IMMEDIATE, nullptr);
            if (n != 1)
                abort();
            return event.data;
        }

        [[nodiscard]] int64_t _add_knote() {
            return _kevent64(EV_ADD | EV_ONESHOT | EV_RECEIPT | EV_UDATA_SPECIFIC);
        }

        [[nodiscard]] int64_t _delete_knote() {
            return _kevent64(EV_DELETE | EV_RECEIPT | EV_UDATA_SPECIFIC);
        }


        [[nodiscard]] static int64_t spawn(std::stop_token stop_token, kevent64_s change, uintptr_t continuation) {
            assert(continuation);
            g_cancelable_timer_frame_count.fetch_add(1, std::memory_order_relaxed);
            auto frame = new Frame;
            frame->_stop_token = stop_token;
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

            // Check our pointer-tagging is ok
            assert(!(continuation & STOPPED));
            assert(!(continuation & RESUMED));
            auto before = frame->_continuation.fetch_or(continuation, std::memory_order_release);
            auto resumed = before & RESUMED;
            auto stopped = before & STOPPED;

            switch (result) {
                case 0: // Did add
                    if (stopped) [[unlikely]] {
                        Coroutine::destroy_by_address((void*)continuation);
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
                        Coroutine::resume_by_address((void*)continuation);
                    }
                    break;
                default: // Did not add
                    assert(!resumed); // Impossible
                    if (stopped) {
                        // Callback released its ownership
                    } else {
                        // Callback did not release its ownesrhip
                        frame->release();
                    }
                    break;
            }
            frame->release();
            return result;
        }

    };

    void cancelable_after(std::stop_token stop_token,
                          std::chrono::steady_clock::duration duration,
                          Coroutine::Future<>&& future) {
        // TODO: start the reactor eagerly in main, not lazily everywhere
        global_reactor_start();
        // Set the stop_token before we erase the necessary type information
        future._promise->set_stop_token(stop_token);
        // TODO: continue with wait-group accounting instead, so shutdown can
        // observe pending arms (see the cancellable_after spec discussion).
        future._promise->set_continuation(std::noop_coroutine());
        auto nanoseconds = std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count();
        kevent64_s change = {
            .ident = 0,
            .filter = EVFILT_TIMER,
            .flags = 0,
            .fflags = NOTE_NSECONDS,
            .data = nanoseconds,
        };
        auto continuation = Coroutine::handle_from_promise(std::exchange(future._promise, nullptr));
        int64_t result = CancelableKEventFrame::spawn(stop_token,
                                                      change,
                                                      (uintptr_t)continuation.address());
        if (result != 0) {
            // kevent registration failed unexpectedly
            // continuation has been neither resumed nor destroyed
            perror("cancelable_after");
            abort();
        }
    }

    struct CancelableRecvAwaitable {
        int _socket;
        void* _buffer;
        size_t _length;
        int _flags;
        int64_t _result = 0;
        constexpr bool await_ready() const noexcept {
            return false;
        }
        template<typename OuterPromise>
        std::coroutine_handle<> await_suspend(std::coroutine_handle<OuterPromise> continuation) {
            std::stop_token stop_token = get_stop_token(continuation);

            kevent64_s change = {
                .ident = (uint64_t) _socket,
                .filter = EVFILT_READ,
                .flags = 0,
                .fflags = 0,
                .data = 0,
            };
            _result = CancelableKEventFrame::spawn(stop_token,
                                                   change,
                                                   (uintptr_t)continuation.address());
            switch (_result) {
                case 0: // success
                    return std::noop_coroutine();
                default: // kevent returned an error
                    // we still own the continuation
                    return continuation;
            }
        }
        [[nodiscard]] ssize_t await_resume() {
            switch (_result) {
                case 0: // _socket is readable
                    _result = ::recv(_socket, _buffer, _length, _flags);
                    break;
                default: // kevent add returned an error
                    // negate it to match ::recv convention
                    _result = -_result;
                    break;
            }
            return (ssize_t)_result;
        }
    };

    // NOTE: The reactor will not prevent or detect multiple readers waiting
    // on a socket, so don't do that
    [[nodiscard]] Coroutine::Future<ssize_t> recv_some(int socket, void* buffer, size_t length, int flags) {
        co_return co_await CancelableRecvAwaitable{socket, buffer, length, flags};
    }


    template<typename T>
    struct WithDeadlineAwaitable {

        struct Callback {
            std::stop_source stop_source;
            void operator()() {
                stop_source.request_stop();
            }
        };

        Coroutine::Header _header = { &_static_resume, &_static_destroy };

        std::chrono::steady_clock::time_point _deadline;
        Coroutine::Future<T> _future;
        std::coroutine_handle<> _continuation;
        std::stop_token _outer_stop_token;
        std::stop_source _inner_stop_source;
        std::optional<std::stop_callback<Callback>> _stop_callback;

        T _value;
        std::exception_ptr _error;
        bool _stopped = false;

        WithDeadlineAwaitable(std::chrono::steady_clock::time_point deadline,
                              Coroutine::Future<T> future)
        : _deadline(std::move(deadline))
        , _future(std::move(future)) {
        }

        constexpr bool await_ready() const noexcept {
            return false;
        }

        template<typename Promise>
        std::coroutine_handle<> await_suspend(std::coroutine_handle<Promise> handle) noexcept {
            _continuation = handle;
            using Coroutine::get_stop_token;
            _outer_stop_token = get_stop_token(handle);
            _stop_callback.emplace(_outer_stop_token, Callback{_inner_stop_source});
            cancelable_after(_inner_stop_source.get_token(),
                             _deadline - std::chrono::steady_clock::now(),
                             [](std::stop_source stop_source) mutable -> Coroutine::Future<> {
                stop_source.request_stop();
                co_return;  // co_ keyword makes this a coroutine, not a plain fn
            } (_inner_stop_source));
            _future._promise->set_stop_token(_inner_stop_source.get_token());
            _future._promise->set_target(&_value);
            _future._promise->set_exception_target(&_error);
            _future._promise->set_continuation(std::coroutine_handle<>::from_address(this));
            return handle_from_future(std::move(_future));
        }

#define CONTINUE\
        if (self->_outer_stop_token.stop_requested()) {\
            [[clang::musttail]] return Coroutine::destroy_by_address(self->_continuation.address());\
        } else {\
            [[clang::musttail]] return Coroutine::resume_by_address(self->_continuation.address());\
        }

        static void _static_resume(void* ptr) {
            auto self = (WithDeadlineAwaitable*)ptr;
            CONTINUE
        }

        static void _static_destroy(void* ptr) {
            auto self = (WithDeadlineAwaitable*)ptr;
            self->_stopped = true;
            CONTINUE
        }

#undef CONTINUE

        std::optional<T> await_resume() {
            _inner_stop_source.request_stop();
            if (_stopped)
                return {};
            if (_error)
                std::rethrow_exception(std::move(_error));
            return std::move(_value);
        }

    };

    template<typename T>
    Coroutine::Future<std::optional<T>> with_deadline(std::chrono::steady_clock::time_point deadline, Coroutine::Future<T>&& future) {
        co_return co_await WithDeadlineAwaitable<T>(std::move(deadline), std::move(future));
    };











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

        Coroutine::Task reactor_late_writer(int fd) {
            co_await Coroutine::Until{steady_clock::now() + milliseconds(10)};
            char message[] = "later";
            ssize_t n = ::send(fd, message, 5, 0);
            assert(n == 5);
            (void) n;
        }

        Coroutine::Task reactor_recv_child(int fd,
                                           std::atomic<int>* started,
                                           std::atomic<int>* destroyed,
                                           std::atomic<int>* resumed_past) {
            ReactorProbe probe{destroyed};
            started->fetch_add(1, std::memory_order_release);
            std::byte buffer[8];
            (void) co_await recv_some(fd, buffer,
                                      steady_clock::now() + seconds(10));
            resumed_past->fetch_add(1, std::memory_order_relaxed);
        }

    }

    define_test("kqueue_recv_ready") {
        SocketPair sockets;
        char message[] = "hello";
        ssize_t sent = ::send(sockets.writer(), message, 5, 0);
        assert(sent == 5);

        std::byte buffer[16];
        auto r = co_await recv_some(sockets.reader(), buffer,
                                    steady_clock::now() + seconds(10));
        assert(r && (*r == 5) && (std::memcmp(buffer, "hello", 5) == 0));

        // Peer closure delivers EV_EOF as readability; recv reports end of
        // stream
        sockets.close_writer();
        auto r2 = co_await recv_some(sockets.reader(), buffer,
                                     steady_clock::now() + seconds(10));
        assert(r2 && (*r2 == 0));
        co_return;
    };

    define_test("kqueue_recv_late_data") {
        SocketPair sockets;
        Coroutine::Nursery nursery;
        nursery.soon(reactor_late_writer(sockets.writer()));

        std::byte buffer[16];
        auto t0 = steady_clock::now();
        auto r = co_await recv_some(sockets.reader(), buffer,
                                    steady_clock::now() + seconds(10));
        assert(r && (*r == 5) && (std::memcmp(buffer, "later", 5) == 0));
        assert(steady_clock::now() - t0 < seconds(5));

        std::ptrdiff_t cancelled = co_await nursery.join();
        assert(cancelled == 0);
        co_return;
    };

    define_test("kqueue_recv_deadline") {
        SocketPair sockets;
        std::byte buffer[16];
        auto t0 = steady_clock::now();
        auto r = co_await recv_some(sockets.reader(), buffer,
                                    t0 + milliseconds(50));
        auto elapsed = steady_clock::now() - t0;
        assert(!r && (r.error() == ETIMEDOUT));
        assert(elapsed >= milliseconds(45));
        assert(elapsed < seconds(5));
        co_return;
    };

    define_test("kqueue_recv_bad_fd") {
        // Registration against a dead fd completes synchronously with the
        // errno from the receipt
        std::byte buffer[16];
        auto r = co_await recv_some(-1, buffer,
                                    steady_clock::now() + seconds(10));
        assert(!r && (r.error() == EBADF));
        co_return;
    };

    define_test("kqueue_recv_cancel") {
        // Prompt cancellation of a parked wait: the deadline is 10 seconds,
        // the stop request lands milliseconds in, and the whole structure --
        // reactor record, recv_some frame, child frame, nursery tally --
        // resolves promptly by unwinding.
        SocketPair sockets;
        std::atomic<int> started{0};
        std::atomic<int> destroyed{0};
        std::atomic<int> resumed_past{0};

        Coroutine::Nursery nursery;
        nursery.soon(reactor_recv_child(sockets.reader(),
                                        &started, &destroyed, &resumed_past));
        while (started.load(std::memory_order_acquire) != 1)
            co_await Coroutine::TransferToPoolExecutor{};
        // Give the child a beat to reach the parked state; either way --
        // parked, mid-registration, or not yet at the await -- cancellation
        // must resolve the wait promptly.
        co_await Coroutine::Until{steady_clock::now() + milliseconds(10)};

        auto t0 = steady_clock::now();
        nursery.request_stop();
        std::ptrdiff_t cancelled = co_await nursery.join();
        auto elapsed = steady_clock::now() - t0;

        assert(cancelled == 1);
        assert(destroyed.load(std::memory_order_relaxed) == 1);
        assert(resumed_past.load(std::memory_order_relaxed) == 0);
        assert(elapsed < seconds(2));  // nowhere near the 10 second deadline
        co_return;
    };

    define_test("kqueue_recv_cancel_early") {
        // Stop requested before the wait even starts: the early-out destroys
        // the frame without arming anything
        SocketPair sockets;
        std::atomic<int> started{0};
        std::atomic<int> destroyed{0};
        std::atomic<int> resumed_past{0};

        Coroutine::Nursery nursery;
        nursery.request_stop();
        nursery.soon(reactor_recv_child(sockets.reader(),
                                        &started, &destroyed, &resumed_past));
        auto t0 = steady_clock::now();
        std::ptrdiff_t cancelled = co_await nursery.join();
        auto elapsed = steady_clock::now() - t0;

        assert(cancelled == 1);
        assert(started.load(std::memory_order_relaxed) == 1);
        assert(destroyed.load(std::memory_order_relaxed) == 1);
        assert(resumed_past.load(std::memory_order_relaxed) == 0);
        assert(elapsed < seconds(2));
        co_return;
    };

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
            : _counter(std::exchange(other._counter, nullptr)) {}
            ~CancelableProbe() {
                if (_counter)
                    _counter->fetch_add(1, std::memory_order_relaxed);
            }
        };

        Coroutine::Task cancelable_task(CancelableProbe probe,
                                        std::atomic<int>* ran) {
            ran->fetch_add(1, std::memory_order_relaxed);
            co_return;
        }

    }

    define_test("kqueue_cancelable_after") {

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
                co_await Coroutine::TransferToPoolExecutor{};
            assert(steady_clock::now() - t0 >= milliseconds(15));
            while (destroyed.load(std::memory_order_relaxed) == 0)
                co_await Coroutine::TransferToPoolExecutor{};
            // The load-bearing property: the Frame dies promptly after the
            // fire, with the token never requested -- static_resume disarms
            // the callback and inherits its reference
            while (g_cancelable_timer_frame_count.load(std::memory_order_acquire) != 0)
                co_await Coroutine::TransferToPoolExecutor{};
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
            co_await Coroutine::Until{steady_clock::now() + milliseconds(5)};
            source.request_stop();
            assert(destroyed.load(std::memory_order_relaxed) == 1);
            assert(ran.load(std::memory_order_relaxed) == 0);
            // The whole structure -- knote withdrawn, frame freed -- retired
            // synchronously inside request_stop
            assert(g_cancelable_timer_frame_count.load(std::memory_order_acquire) == 0);
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
            assert(destroyed.load(std::memory_order_relaxed) == 1);
            assert(ran.load(std::memory_order_relaxed) == 0);
            // The doomed-timer cleanup: the knote armed after the inline
            // cancellation is withdrawn inside spawn itself, so the frame is
            // gone before cancelable_after returns -- not at the deadline
            assert(g_cancelable_timer_frame_count.load(std::memory_order_acquire) == 0);
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
                co_await Coroutine::TransferToPoolExecutor{};
            while (destroyed.load(std::memory_order_relaxed) != N)
                co_await Coroutine::TransferToPoolExecutor{};
            // All N frames retire without a stop request, in every
            // interleaving the race produced
            while (g_cancelable_timer_frame_count.load(std::memory_order_acquire) != 0)
                co_await Coroutine::TransferToPoolExecutor{};
        }

        co_return;
    };

    // ---- with_deadline over the new recv_some -----------------------------
    //
    // These port the six kqueue_recv_* tests onto the composed stack:
    //   optional<ssize_t> = co_await with_deadline(t, recv_task(fd,buf,len))
    // where a timeout is nullopt, a peer close is 0, and a byte count is n.
    // The old RecvWait recv_some(span, deadline) and its tests remain until
    // this stack is proven, then both are deleted.

    namespace {

        // Interpose a Future so with_deadline has a promise seam to drive; the
        // bare awaitable is hardwired to whoever co_awaits it.
        Coroutine::Future<ssize_t> recv_task(int fd, void* buffer, size_t length) {
            co_return co_await recv_some(fd, buffer, length, 0);
        }

        Coroutine::Task wd_recv_child(int fd,
                                      std::chrono::steady_clock::time_point deadline,
                                      std::atomic<int>* started,
                                      std::atomic<int>* destroyed,
                                      std::atomic<int>* resumed_past) {
            ReactorProbe probe{destroyed};
            started->fetch_add(1, std::memory_order_release);
            std::byte buffer[8];
            (void) co_await with_deadline(deadline, recv_task(fd, buffer, sizeof buffer));
            resumed_past->fetch_add(1, std::memory_order_relaxed);
        }

    }

    define_test("kqueue_wd_ready") {
        SocketPair sockets;
        char message[] = "hello";
        assert(::send(sockets.writer(), message, 5, 0) == 5);

        std::byte buffer[16];
        auto r = co_await with_deadline(steady_clock::now() + seconds(10),
                                        recv_task(sockets.reader(), buffer, sizeof buffer));
        assert(r.has_value() && (*r == 5) && (std::memcmp(buffer, "hello", 5) == 0));

        // Peer close is readability; recv returns 0 (end of stream), engaged
        sockets.close_writer();
        auto r2 = co_await with_deadline(steady_clock::now() + seconds(10),
                                         recv_task(sockets.reader(), buffer, sizeof buffer));
        assert(r2.has_value() && (*r2 == 0));
        co_return;
    };

    define_test("kqueue_wd_late_data") {
        SocketPair sockets;
        Coroutine::Nursery nursery;
        nursery.soon(reactor_late_writer(sockets.writer()));

        std::byte buffer[16];
        auto t0 = steady_clock::now();
        auto r = co_await with_deadline(steady_clock::now() + seconds(10),
                                        recv_task(sockets.reader(), buffer, sizeof buffer));
        assert(r.has_value() && (*r == 5) && (std::memcmp(buffer, "later", 5) == 0));
        assert(steady_clock::now() - t0 < seconds(5));

        assert((co_await nursery.join()) == 0);
        co_return;
    };

    define_test("kqueue_wd_deadline") {
        SocketPair sockets;
        std::byte buffer[16];
        auto t0 = steady_clock::now();
        auto r = co_await with_deadline(t0 + milliseconds(50),
                                        recv_task(sockets.reader(), buffer, sizeof buffer));
        auto elapsed = steady_clock::now() - t0;
        // Timeout is the nullopt channel
        assert(!r.has_value());
        assert(elapsed >= milliseconds(45));
        assert(elapsed < seconds(5));
        co_return;
    };

    define_test("kqueue_wd_bad_fd") {
        std::byte buffer[16];
        auto r = co_await with_deadline(steady_clock::now() + seconds(10),
                                        recv_task(-1, buffer, sizeof buffer));
        // A synchronous registration error is a completion, NOT a timeout:
        // the optional is engaged (distinguishing it from the deadline path).
        // NOTE: the value carries the errno in the same ssize_t as a byte
        // count -- the triune-encoding ambiguity still open on recv_some.
        assert(r.has_value());
        co_return;
    };

    define_test("kqueue_wd_cancel") {
        // Outer cancellation of a parked composed wait: 10s deadline, stop
        // lands milliseconds in, the whole tower (recv leaf, timer, recv_task,
        // with_deadline, child) unwinds promptly via the destroy cascade.
        SocketPair sockets;
        std::atomic<int> started{0}, destroyed{0}, resumed_past{0};

        Coroutine::Nursery nursery;
        nursery.soon(wd_recv_child(sockets.reader(), steady_clock::now() + seconds(10),
                                   &started, &destroyed, &resumed_past));
        while (started.load(std::memory_order_acquire) != 1)
            co_await Coroutine::TransferToPoolExecutor{};
        co_await Coroutine::Until{steady_clock::now() + milliseconds(10)};

        auto t0 = steady_clock::now();
        nursery.request_stop();
        std::ptrdiff_t cancelled = co_await nursery.join();
        auto elapsed = steady_clock::now() - t0;

        assert(cancelled == 1);
        assert(destroyed.load(std::memory_order_relaxed) == 1);
        assert(resumed_past.load(std::memory_order_relaxed) == 0);
        assert(elapsed < seconds(2));
        // Both CancelableKEventFrames (recv leaf + deadline timer) retired
        while (g_cancelable_timer_frame_count.load(std::memory_order_acquire) != 0)
            co_await Coroutine::TransferToPoolExecutor{};
        co_return;
    };

    define_test("kqueue_wd_early_completion_drains") {
        // The scope-exit law, made observable: a recv that completes far
        // before its deadline must reap the deadline timer PROMPTLY, not at
        // the deadline.  With data already waiting, both frames should drain
        // in milliseconds despite a 10 second deadline.
        SocketPair sockets;
        assert(::send(sockets.writer(), "x", 1, 0) == 1);

        std::byte buffer[16];
        auto r = co_await with_deadline(steady_clock::now() + seconds(10),
                                        recv_task(sockets.reader(), buffer, sizeof buffer));
        assert(r.has_value() && (*r == 1));

        auto t0 = steady_clock::now();
        while ((g_cancelable_timer_frame_count.load(std::memory_order_acquire) != 0)
               && (steady_clock::now() - t0 < seconds(1)))
            co_await Coroutine::TransferToPoolExecutor{};
        // If the timer is only reaped when it fires, this is still 1 here
        assert(g_cancelable_timer_frame_count.load(std::memory_order_acquire) == 0);
        co_return;
    };

} // namespace wry
