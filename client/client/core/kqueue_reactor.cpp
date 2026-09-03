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
        std::thread g_reactor_thread;
        std::atomic_int_fast8_t g_reactor_tsan_helper{0};

        void reactor_run();

        // The shutdown doorbell is a persistent EVFILT_USER knote, ident 0.
        constexpr uint64_t DOORBELL_IDENT = 0;

        // All kevent64 calls made off the reactor thread pass
        // KEVENT_FLAG_ERROR_EVENTS so an immediate call can never retrieve
        // (and thereby consume) another operation's one-shot event into its
        // own eventlist; errors and receipts come back, deliveries do not.
        constexpr unsigned IMMEDIATE =
            KEVENT_FLAG_IMMEDIATE | KEVENT_FLAG_ERROR_EVENTS;

        // ---- Reactor contract ----------------------------------------------
        //
        // The reactor is protocol-free.  Every knote it delivers, other than
        // the doorbell, carries as udata a heap frame whose first two words
        // are a Coroutine::Header {resume, destroy}; the reactor schedules
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
        // mutex -- ADD synchronizes-with delivery, and with a receipt-0
        // DELETE -- and make that edge visible with g_reactor_tsan_helper: a
        // release poke by every publisher before its kevent, an acquire poke
        // by the reactor after each harvest.  Frame atomics are otherwise
        // relaxed except for an acquire re-load on the winning path.

        // ---- Reactor service loop ------------------------------------------

        void reactor_run() {
            pthread_setname_np("R0");
            bool stop_requested = false;
            for (;;) {
                const int NEVENTS = 16;
                kevent64_s events[NEVENTS];
                int n = ::kevent64(g_reactor_kqueue,
                                   nullptr, 0, events, NEVENTS,
                                   (stop_requested ? KEVENT_FLAG_IMMEDIATE : 0),
                                   nullptr);
                // AFTER the harvest, before touching any udata: pairs with
                // the publishers' release pokes before their kevent ADDs.
                g_reactor_tsan_helper.load(std::memory_order_acquire);

                assert((-1 <= n) && (n <= NEVENTS));
                assert((n != 0) || stop_requested);
                if (n == -1) {
                    if (errno == EINTR)
                        continue;
                    perror("reactor kevent64");
                    abort();
                }
                for (int i = 0; i != n; ++i) {
                    kevent64_s const& event = events[i];
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
                    // The witness acquire above ordered the publisher's
                    // construction before this dispatch.
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
        kevent64_s change = {
            .ident = DOORBELL_IDENT,
            .filter = EVFILT_USER,
            .flags = EV_ADD | EV_CLEAR,
        };
        if (::kevent64(g_reactor_kqueue, &change, 1, nullptr, 0,
                       IMMEDIATE, nullptr) != 0) {
            perror("kevent64");
        }
        g_reactor_thread = std::thread(&reactor_run);
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

#ifndef NDEBUG
    // Test observability: live CancelableKEventFrame count.  The tests pin
    // the load-bearing property -- the Frame dies promptly after the FIRST
    // of {timer fired, stop requested} -- which is invisible without this.
    constinit std::atomic<std::ptrdiff_t> g_cancelable_kevent_frame_count{0};
#endif // !NDEBUG

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
#ifndef NDEBUG
                g_cancelable_kevent_frame_count.fetch_sub(1, std::memory_order_release);
#endif
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
#ifndef NDEBUG
            g_cancelable_kevent_frame_count.fetch_add(1, std::memory_order_relaxed);
#endif
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

    }; // CancelableKEventFrame

    void cancelable_after(std::stop_token stop_token,
                          std::chrono::steady_clock::duration duration,
                          Coroutine::Future<>&& future) {
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

    // TODO: send, accept, etc. also follow this pattern
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
            std::stop_source _stop_source;
            void operator()() {
                // Local copy to survive possible self-destruct
                std::stop_source stop_source{std::move(_stop_source)};
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
                // request_stop returns bool; a non-void co_return would route
                // to return_value, which Promise<void> lacks
                (void) stop_source.request_stop();
                co_return;
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
            while (g_cancelable_kevent_frame_count.load(std::memory_order_acquire) != 0)
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
            assert(g_cancelable_kevent_frame_count.load(std::memory_order_acquire) == 0);
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
            assert(g_cancelable_kevent_frame_count.load(std::memory_order_acquire) == 0);
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
            while (g_cancelable_kevent_frame_count.load(std::memory_order_acquire) != 0)
                co_await Coroutine::TransferToPoolExecutor{};
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

        Coroutine::Task wd_recv_child(int fd,
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
        assert(::send(sockets.writer(), message, 5, 0) == 5);

        std::byte buffer[16];
        auto r = co_await with_deadline(steady_clock::now() + seconds(10),
                                        recv_some(sockets.reader(), buffer, sizeof buffer, 0));
        assert(r.has_value() && (*r == 5) && (std::memcmp(buffer, "hello", 5) == 0));

        // Peer close is readability; recv returns 0 (end of stream), engaged
        sockets.close_writer();
        auto r2 = co_await with_deadline(steady_clock::now() + seconds(10),
                                         recv_some(sockets.reader(), buffer, sizeof buffer, 0));
        assert(r2.has_value() && (*r2 == 0));
        co_return;
    };

    define_test("kqueue_recv_late_data") {
        SocketPair sockets;
        Coroutine::Nursery nursery;
        nursery.soon(reactor_late_writer(sockets.writer()));

        std::byte buffer[16];
        auto t0 = steady_clock::now();
        auto r = co_await with_deadline(steady_clock::now() + seconds(10),
                                        recv_some(sockets.reader(), buffer, sizeof buffer, 0));
        assert(r.has_value() && (*r == 5) && (std::memcmp(buffer, "later", 5) == 0));
        assert(steady_clock::now() - t0 < seconds(5));

        assert((co_await nursery.join()) == 0);
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
        assert(!r.has_value());
        assert(elapsed >= milliseconds(45));
        assert(elapsed < seconds(5));
        co_return;
    };

    define_test("kqueue_recv_bad_fd") {
        std::byte buffer[16];
        auto r = co_await with_deadline(steady_clock::now() + seconds(10),
                                        recv_some(-1, buffer, sizeof buffer, 0));
        // A synchronous registration error is a completion, NOT a timeout:
        // engaged (unlike the deadline path), carrying -errno
        assert(r.has_value() && (*r == -EBADF));
        co_return;
    };

    define_test("kqueue_recv_cancel") {
        // Outer cancellation of a parked composed wait: 10s deadline, stop
        // lands milliseconds in, the whole tower (recv leaf, timer, recv_some frame,
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
        while (g_cancelable_kevent_frame_count.load(std::memory_order_acquire) != 0)
            co_await Coroutine::TransferToPoolExecutor{};
        co_return;
    };

    define_test("kqueue_recv_early_completion_drains") {
        // The scope-exit law, made observable: a recv that completes far
        // before its deadline must reap the deadline timer PROMPTLY, not at
        // the deadline.  With data already waiting, both frames should drain
        // in milliseconds despite a 10 second deadline.
        SocketPair sockets;
        assert(::send(sockets.writer(), "x", 1, 0) == 1);

        std::byte buffer[16];
        auto r = co_await with_deadline(steady_clock::now() + seconds(10),
                                        recv_some(sockets.reader(), buffer, sizeof buffer, 0));
        assert(r.has_value() && (*r == 1));

        auto t0 = steady_clock::now();
        while ((g_cancelable_kevent_frame_count.load(std::memory_order_acquire) != 0)
               && (steady_clock::now() - t0 < seconds(1)))
            co_await Coroutine::TransferToPoolExecutor{};
        // If the timer is only reaped when it fires, this is still 1 here
        assert(g_cancelable_kevent_frame_count.load(std::memory_order_acquire) == 0);
        co_return;
    };

} // namespace wry
