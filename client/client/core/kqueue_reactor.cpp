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
                std::stop_token token = handle.promise().get_stop_token();

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

    struct CancelableTimerFrame {

        using Frame = CancelableTimerFrame;

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
                    switch (frame->_delete_event()) {
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

        Coroutine::Header _header = { &static_resume, &static_destroy };
        std::stop_token _stop_token;
        std::atomic<uintptr_t> _continuation{0};
        std::atomic<ptrdiff_t> _reference_count_minus_one{2}; // initial condition: 3 owners
        std::optional<std::stop_callback<Callback>> _stop_callback;

        void acquire() {
            _reference_count_minus_one.fetch_add(1, std::memory_order_relaxed);
        }

        void release() {
            if (!_reference_count_minus_one.fetch_sub(1, std::memory_order_release)) {
                (void) _reference_count_minus_one.load(std::memory_order_acquire);
                assert(_continuation.load(std::memory_order_relaxed) & (STOPPED | RESUMED));
                delete this;
                g_cancelable_timer_frame_count.fetch_sub(1, std::memory_order_release);
            }
        }

        static void static_resume(void* ptr) {
            // We have reached here because the timer event fired
            CancelableTimerFrame* frame = (CancelableTimerFrame*)ptr;
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


        static void static_destroy(void*) {
            std::unreachable();
        }

        [[nodiscard]] int64_t _add_event(int64_t nanoseconds) {
            kevent64_s change = {
                .ident = 0,
                .filter = EVFILT_TIMER,
                .flags = EV_ADD | EV_ONESHOT | EV_RECEIPT | EV_UDATA_SPECIFIC,
                .fflags = NOTE_NSECONDS,
                .data = nanoseconds,
                .udata = (uint64_t)this,
            };
            kevent64_s event = {};
            g_reactor_tsan_helper.fetch_or(0, std::memory_order_release);
            int n = kevent64(g_reactor_kqueue, &change, 1, &event, 1, IMMEDIATE, nullptr);
            if (n != 1)
                abort();
            return event.data;
        }

        [[nodiscard]] int64_t _delete_event() {
            kevent64_s change = {
                .ident = 0,
                .filter = EVFILT_TIMER,
                .flags = EV_DELETE | EV_RECEIPT | EV_UDATA_SPECIFIC,
                .fflags = 0,
                .data = 0,
                .udata = (uint64_t)this,
            };
            kevent64_s event = {};
            g_reactor_tsan_helper.fetch_or(0, std::memory_order_release);
            int n = kevent64(g_reactor_kqueue, &change, 1, &event, 1, IMMEDIATE, nullptr);
            if (n != 1)
                abort();
            return event.data;
        }


        static void spawn(std::stop_token stop_token, int64_t nanoseconds, uintptr_t continuation) {
            assert(continuation);
            g_cancelable_timer_frame_count.fetch_add(1, std::memory_order_relaxed);
            auto frame = new Frame;
            frame->_stop_token = stop_token;

            // No early-out, it pessimizes the happy path to accelerate the rare path

            frame->_stop_callback.emplace(frame->_stop_token, Callback{frame});
            switch (frame->_add_event(nanoseconds)) {
                case 0: // Success
                    break;
                default: // Unhandled errno value
                    abort();
            }

            // Check our pointer-tagging is ok
            assert(!(continuation & STOPPED));
            assert(!(continuation & RESUMED));
            auto before = frame->_continuation.fetch_or(continuation, std::memory_order_release);
            auto resumed = before & RESUMED;
            auto stopped = before & STOPPED;
            if (stopped) [[unlikely]] {
                Coroutine::destroy_by_address((void*)continuation);
                if (!resumed) {
                    // We may have deleted before we added, leaving a doomed
                    // timer running.  Kill it ASAP.
                    switch (frame->_delete_event()) {
                        case 0: // The event will never fire
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
            frame->release();
        }

    };

    void cancelable_after(std::stop_token stop_token, std::chrono::steady_clock::duration duration, Coroutine::Future<>&& future) {
        global_reactor_start();
        // Set the stop_token before we erase the necessary type information
        future._promise->set_stop_token(stop_token);
        // The fired task completes into nothing: FinalAwaitable transfers to
        // (and ~Promise, on the cancel path, harmlessly destroys) the noop
        // coroutine.  TODO: wait-group accounting instead, so shutdown can
        // observe pending arms (see the cancellable_after spec discussion).
        future._promise->set_continuation(std::noop_coroutine());
        auto continuation = Coroutine::handle_from_promise(std::exchange(future._promise, nullptr)).address();
        auto nanoseconds = std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count();
        CancelableTimerFrame::spawn(stop_token,
                                    nanoseconds,
                                    (uintptr_t)continuation);
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

} // namespace wry
