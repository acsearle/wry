//
//  coroutine.hpp
//  client
//
//  Created by Antony Searle on 13/8/2025.
//

#ifndef coroutine_hpp
#define coroutine_hpp

#include <cassert>
#include <chrono>
#include <coroutine>
#include <cstdint>
#include <deque>
#include <exception>
#include <memory>
#include <semaphore>
#include <thread>
#include <mutex>
#include <queue>
#include <variant>
#include <stop_token>

#include "atomic.hpp"
#include "utility.hpp"

#include "epoch_allocator.hpp"
#include "mutex.hpp"

#include "global_work_queue.hpp"

namespace wry {

    void global_work_queue_schedule(std::coroutine_handle<>);

    void collector_register_cycle_callback(uint64_t k,
                                           void* callback) noexcept;

}

namespace wry::Coroutine {
    
    // Basic functions
    
    inline std::coroutine_handle<> null_to_noop(std::coroutine_handle<> handle) {
        return handle ? handle : std::noop_coroutine();
    }

    // Basic awaitables
    
    struct ResumeNever : std::suspend_always {
        void await_resume() const noexcept {
            abort();
        }
    };
    
    struct SuspendAndDestroy : ResumeNever {
        void await_suspend(std::coroutine_handle<> handle) const noexcept {
            handle.destroy();
        }
    };
    
    struct DebugSuspendAndLeak : ResumeNever {
        void await_suspend(std::coroutine_handle<> handle) const noexcept {}
    };
    
    struct SuspendAndSchedule : std::suspend_always {
        void await_suspend(std::coroutine_handle<> handle) const noexcept {
            global_work_queue_schedule(handle);
        }
    };

    struct SuspendAndScheduleOnTemporaryThread : std::suspend_always {
        void await_suspend(std::coroutine_handle<> handle) const noexcept {
            std::thread{handle}.detach();
        }
    };

    struct ScheduleOnBlockableThread : std::suspend_always {
        void await_suspend(std::coroutine_handle<>) const noexcept;
    };

    struct Until {
        std::chrono::steady_clock::time_point _when;
        bool await_ready() const noexcept {
            return std::chrono::steady_clock::now() >= _when;
        }
        void await_suspend(std::coroutine_handle<>) const noexcept;
        void await_resume() const noexcept {}
    };

    struct DebugWaitForCollectionCycles {
        uint64_t cycles;
        bool await_ready() const noexcept { return cycles == 0; }
        void await_suspend(std::coroutine_handle<> handle) const noexcept {
            collector_register_cycle_callback(cycles, handle.address());
        }
        void await_resume() const noexcept {}
    };

    // Basic coroutine

    template<typename T>
    struct ReturnChannel {
        using stored_type = T;
        T* _target = nullptr;
        void set_target(T* target) noexcept {
            assert(!_target);
            _target = target;
        }
        template<typename U>
        void return_value(U&& u) const noexcept(std::is_nothrow_assignable_v<T&, U&&>) {
            *_target = std::forward<U>(u);
        }
    };

    template<>
    struct ReturnChannel<void> {
        using stored_type = std::monostate;
        void return_void() const noexcept {}
    };

    template<typename = void> struct Future;
    using Task = Future<>;

    template<typename T>
    struct Promise : ReturnChannel<T> {

        std::coroutine_handle<> _continuation = nullptr;
        std::stop_token _stop_token;
        std::exception_ptr* _exception = nullptr;

        constexpr std::suspend_always initial_suspend() const noexcept {
            return std::suspend_always{};
        }

        Future<T> get_return_object();

        void unhandled_exception() noexcept {
            if (!_exception) // Awaiter does not accept exceptions
                abort();
            if (*_exception) // An exception was already set
                abort();
            *_exception = std::current_exception();
        }

        struct FinalAwaitable : ResumeNever {
            template<typename DerivedPromise>
            std::coroutine_handle<> await_suspend(std::coroutine_handle<DerivedPromise> handle) const noexcept {
                std::coroutine_handle<> continuation = std::exchange(handle.promise()._continuation, nullptr);
                handle.destroy();
                assert(continuation);
                return continuation;
            }
        };

        auto final_suspend() const noexcept {
            return FinalAwaitable{};
        }

        ~Promise() {
            if (_continuation) {
                // We did not resume our continuation, therefore we are
                // participants in a cancellation unwinding
                _continuation.destroy();
            }
        }

        void set_continuation(std::coroutine_handle<> continuation) {
            assert(!_continuation);
            _continuation = std::move(continuation);
        }

        void set_continuation(void* ptr) {
            assert(!_continuation);
            _continuation = std::coroutine_handle<>::from_address(ptr);
        }

        void set_stop_token(std::stop_token t) {
            _stop_token = t;
        }

        std::stop_token get_stop_token() {
            return _stop_token;
        }

        void set_exception_target(std::exception_ptr* xpp) {
            assert(!_exception && xpp);
            _exception = xpp;
        }

    }; // Promise<T>

    template<typename T>
    std::coroutine_handle<Promise<T>> handle_from_promise(Promise<T>* promise) {
        assert(promise);
        return std::coroutine_handle<typename Future<T>::promise_type>::from_promise(*promise);
    }

    // SuspendAndDestroy, but restricted to Promise<T> that perform unwinding
    struct SuspendAndCancel : ResumeNever {
        template<typename T>
        void await_suspend(std::coroutine_handle<Promise<T>> handle) {
            handle.destroy();
        }
    };

    // TODO: We could imagine returning a common base class of Promise with all
    // the T-invariant features available.
    struct GetStopToken {
        std::stop_token _stop_token;
        constexpr bool await_ready() const noexcept { return false; }
        template<typename T>
        std::coroutine_handle<Promise<T>> await_suspend(std::coroutine_handle<Promise<T>> continuation) {
            _stop_token = continuation.promise().get_stop_token();
            return continuation;
        }
        std::stop_token await_resume() {
            return _stop_token;
        }
    };

    template<typename T>
    struct Future {

        using promise_type = Promise<T>;
        promise_type* _promise = nullptr;
        explicit Future(promise_type* promise) : _promise(promise) {}

        Future() = delete;
        Future(Future const&) = delete;
        Future(Future&& other) : _promise(exchange(other._promise, nullptr)) {}
        ~Future() {
            // This could happen if we create a future, hold it across a
            // suspension point, and then resume it.  But it's more likely to
            // just have been forgotten.  See if we ever have a compelling need
            // to do the former.
            if (_promise)
                abort();
        }
        Future& operator=(Future const&) = delete;
        Future& operator=(Future&& other) {
            Future a{std::move(other)};
            using std::swap;
            swap(_promise, a._promise);
            return *this;
        }

        struct Awaitable {
            promise_type* _promise = nullptr;
            [[no_unique_address]] ReturnChannel<T>::stored_type _target;
            std::exception_ptr _exception = nullptr;
            constexpr bool await_ready() const noexcept { return false; }
            template<typename OuterPromise>
            std::coroutine_handle<promise_type> await_suspend(std::coroutine_handle<OuterPromise> continuation) noexcept {
                if constexpr (!std::is_void_v<T>)
                    _promise->set_target(&_target);
                _promise->set_exception_target(&_exception);
                _promise->set_stop_token(continuation.promise().get_stop_token());
                _promise->set_continuation(std::move(continuation));
                return handle_from_promise(std::exchange(_promise, nullptr));
            }
            auto await_resume() {
                if (_exception)
                    std::rethrow_exception(_exception);
                if constexpr (!std::is_void_v<T>)
                    return std::move(_target);
            }
        };

        auto operator co_await() {
            return Awaitable{std::exchange(this->_promise, nullptr)};
        }

    };

    template<typename T>
    Future<T> Promise<T>::get_return_object() {
        return Future<T>{this};
    }

    template<typename T>
    std::coroutine_handle<typename Future<T>::promise_type> handle_from_future(Future<T>&& future) {
        return handle_from_promise(std::exchange(future._promise, nullptr));
    }

    template<typename T> constexpr bool is_coroutine(T const&) { return false; }
    template<typename T> constexpr bool is_coroutine(Future<T> const&) { return true; }

    struct Nursery {
        
        void (*_resume)(void*) = &_static_resume;
        void (*_destroy)(void*) = &_static_destroy;
        Atomic<std::ptrdiff_t> _counter{0};
        Atomic<std::ptrdiff_t> _cancelled_count{0};
        std::ptrdiff_t _children = 0;
        std::coroutine_handle<> _continuation;
        std::stop_token _outer_stop_token;
        std::stop_source _inner_stop_source;

        struct Callback {
            std::stop_source _inner_stop_source;
            void operator()() {
                _inner_stop_source.request_stop();
            }
        };

        std::stop_callback<Callback> _outer_stop_callback;

        static void _static_resume(void* ptr) {
            auto self = (Nursery*)ptr;
            auto count = self->_counter.sub_fetch_release(1);
            if (count == 0) {
                (void) self->_counter.load_acquire();
                ptr = std::exchange(self->_continuation, nullptr).address();
                if (!self->_outer_stop_token.stop_requested()) {
                    [[clang::musttail]] return (*(void(**)(void*))ptr)(ptr);
                } else {
                    [[clang::musttail]] return (*((void(**)(void*))ptr + 1))(ptr);
                }
            }
        }

        static void _static_destroy(void* ptr) {
            auto self = (Nursery*)ptr;
            self->_cancelled_count.fetch_add_relaxed(1);
            _static_resume(ptr);
        }

        bool request_stop() {
            return _inner_stop_source.request_stop();
        }

        Nursery() : Nursery(std::stop_token{}) {}
        explicit Nursery(std::stop_token token)
        : _outer_stop_token(token)
        , _outer_stop_callback(_outer_stop_token, Callback{_inner_stop_source}) {
        }

        ~Nursery() {
            // Detect destruction of a coroutine containing a running nursery
            assert(!_children && !_counter.load_relaxed());
        }

        struct Factory {
            std::stop_token _stop_token;
            constexpr bool await_ready() const noexcept {
                return false;
            }
            template<typename T>
            bool await_suspend(std::coroutine_handle<Promise<T>> continuation) noexcept {
                _stop_token = continuation.promise()._stop_token;
                return false;
            }
            [[nodiscard]] Nursery await_resume() noexcept {
                return Nursery(std::move(_stop_token));
            }
        };

        // co_await nursery.fork(foo(x)) immediately starts foo on the current thread
        // and schedules the caller to execute soon
        [[nodiscard]] auto fork(Future<>&& future) {
            struct Awaitable : std::suspend_always {
                Nursery* _nursery;
                Future<>::promise_type* _promise;
                std::coroutine_handle<Future<>::promise_type> await_suspend(std::coroutine_handle<> continuation) noexcept {
                    ++(_nursery->_children);
                    _promise->set_continuation(_nursery);
                    _promise->set_stop_token(_nursery->_inner_stop_source.get_token());
                    // set_exception_target deliberately omitted
                    auto handle = handle_from_promise(std::exchange(_promise, nullptr));
                    global_work_queue_schedule(std::move(continuation));
                    return handle;
                }
                ~Awaitable() {
                    assert(!_promise);
                }
            };
            return Awaitable{{}, this, std::exchange(future._promise, nullptr)};
        }
        
        // co_await nursery.fork(y, bar(x)) immediately starts bar on the current
        // thread and schedules the caller to execute soon.  When bar completes
        // it assigns to y; it is racy to access y until the nursery has been
        // joined
        template<typename T>
        [[nodiscard]] auto fork(T& target, Future<T>&& future) {
            struct Awaitable : std::suspend_always {
                Nursery* _nursery;
                Future<T>::promise_type* _promise;
                T* _target;
                std::coroutine_handle<typename Future<T>::promise_type> await_suspend(std::coroutine_handle<> continuation) noexcept {
                    ++(_nursery->_children);
                    _promise->set_continuation(_nursery);
                    _promise->set_stop_token(_nursery->_inner_stop_source.get_token());
                    _promise->set_target(_target);
                    // set_exception_target deliberately omitted
                    auto handle = handle_from_promise(std::exchange(_promise, nullptr));
                    global_work_queue_schedule(std::move(continuation));
                    return handle;
                }
                ~Awaitable() {
                    assert(!_promise);
                }
            };
            return Awaitable{{}, this, std::exchange(future._promise, nullptr), &target};
        }

        // nursery.soon(foo(x)) schedules foo to execute soon and continues
        // the calling context normally.  The calling context does not have to
        // be a coroutine
        void soon(Future<>&& future) {
            ++_children;
            future._promise->set_continuation(this);
            future._promise->set_stop_token(_inner_stop_source.get_token());
            // set_exception_target deliberately omitted
            global_work_queue_schedule(handle_from_future(std::move(future)));
        }

        // nursery.soon(y, bar(x)) schedules bar to execute soon and
        // continues the calling context normally.  The calling context does
        // not have to be a coroutine.  When bar completes, it assigns to y;
        // it is racy to access y until the nursery has been joined
        template<typename T>
        void soon(T& target, Future<T>&& future) {
            _children++;
            future._promise->set_continuation(this);
            future._promise->set_target(&target);
            // set_exception_target deliberately omitted
            future._promise->set_stop_token(_inner_stop_source.get_token());
            global_work_queue_schedule(handle_from_future(std::move(future)));
        }

        // co await nursery.join() suspends the caller and resumes it after
        // all forks are complete.  The nursery may then be resumed.
        [[nodiscard]] auto join() {
            struct Awaitable {
                Nursery* _nursery;
                bool await_ready() noexcept {
                    auto count = _nursery->_counter.load_relaxed();
                    bool result = (count == -_nursery->_children) && !_nursery->_outer_stop_token.stop_requested();
                    if (result) {
                        _nursery->_counter.exchange_acquire(0);
                        _nursery->_children = 0;
                    }
                    return result;
                }
                std::coroutine_handle<> await_suspend(std::coroutine_handle<> continuation) noexcept {
                    _nursery->_continuation = std::move(continuation);
                    auto count = _nursery->_counter.add_fetch_release(std::exchange(_nursery->_children, 0));
                    if (count == 0) {
                        (void) _nursery->_counter.load_acquire();
                        if (_nursery->_outer_stop_token.stop_requested()) {
                            continuation.destroy();
                            return std::noop_coroutine();
                        }
                        return _nursery->_continuation;
                    } else {
                        return std::noop_coroutine();
                    }
                }
                std::ptrdiff_t await_resume() noexcept {
                    return std::exchange(_nursery, nullptr)->_cancelled_count.nonatomic_exchange(0);
                }
                ~Awaitable() {
                    assert(!_nursery || _nursery->_outer_stop_token.stop_requested());
                }
            };
            return Awaitable{this};
        }

    }; // struct Nursery


    // Block the current thread until the awaitable completes.
    
    template<typename T>
    auto sync_wait(T&& awaitable) {
        struct Frame {
            void (*_resume)(void*) = &_static_resume;
            void (*_destroy)(void*) = &_static_resume;
            std::binary_semaphore _semaphore{0}; // start in unavailable state
            static void _static_resume(void* ptr) {
                auto self = (Frame*)ptr;
                self->_semaphore.release();
            }
            // TODO: We have no way to communicate cancellation into a generic awaitable
        };
        Frame frame;
        if (!awaitable.await_ready()) {
            global_work_queue_schedule(awaitable.await_suspend(std::coroutine_handle<>::from_address(&frame)));
            frame._semaphore.acquire();
        }
        return awaitable.await_resume();
    }





    template<typename T>
    struct SingleProducerSingleConsumerQueue {

        std::mutex _mutex;
        std::queue<T> _queue;
        std::coroutine_handle<> _pop_continuation;

        void emplace(auto&&... args) {
            std::unique_lock guard{_mutex};
            _queue.emplace(FORWARD(args)...);
            if (_pop_continuation)
                global_work_queue_schedule(std::exchange(_pop_continuation, nullptr));
        }

        struct _pop_awaitable : std::suspend_always {
            SingleProducerSingleConsumerQueue* _context;
            std::coroutine_handle<> await_suspend(std::coroutine_handle<> continuation) {
                assert(_context);
                std::unique_lock guard{_context->_mutex};
                if (_context->_queue.empty()) {
                    assert(!_context->_pop_continuation); // single consumer violated
                    _context->_pop_continuation = continuation;
                    return std::noop_coroutine();
                } else {
                    return continuation;
                }
            }
            T await_resume() {
                assert(_context);
                std::unique_lock guard{_context->_mutex};
                assert(!_context->_queue.empty());
                T result{std::move(_context->_queue.front())};
                _context->_queue.pop();
                _context = nullptr;
                return result;
            }
        };

        [[nodiscard]] auto pop() {
            return _pop_awaitable{this};
        };

        bool try_pop(T& victim) {
            std::unique_lock guard{_mutex};
            assert(!_pop_continuation); // single consumer violated
            bool result = !_queue.empty();
            if (result) {
                victim = std::move(_queue.front());
                _queue.pop();
            }
            return result;
        }

    };




    // TODO: OneShotEvent's single usage circumvents structured concurrency
    // and leaves the non-timed-out branch detached (hence the shared_ptr).
    // Since the use case is a unit test, maybe OK

    // ---- OneShotEvent: timed one-shot completion / cancellable timer -------
    //
    // One cell, two ends.  co_await cell->wait_until(t) is simultaneously
    //   - a completion wait that signal() resolves early (returns true), and
    //   - a timer that signal() cancels (the same race, seen from the other
    //     end); if the deadline arrives first the wait resolves false.
    // signal() may be called from any thread, before or after the wait
    // begins, and repeatedly (idempotent).  Single waiter, single use.
    //
    // The waiter resumes on the GC pool via global_work_queue_schedule (a
    // pinned mutator worker) -- unlike Until, which resumes on a dispatch
    // thread.
    //
    // Lifetime: the cell is shared_ptr-managed (construct via make()) so the
    // coordination state outlives whichever of {signaler, deadline fire,
    // waiter} finishes last; it must not live in the winner's scope.  A
    // timed-out wait leaves a detached signaler (e.g. a still-running
    // background save) holding its own reference harmlessly.
    //
    // The deadline is dispatch_after_f, which has no cancellation:
    // "cancelling the timer" is losing the race -- the fire still happens at
    // the deadline, no-ops against the decided state, and drops its
    // reference.  Upgrade the backend to a dispatch source if early physical
    // release ever matters.
    //
    // State (one atomic word): EMPTY, SIGNALED, TIMED_OUT, or the waiter's
    // coroutine handle address (frame allocations are aligned, so 1 and 2
    // cannot alias a real handle).
    //
    //   EMPTY -> SIGNALED       signal() before the wait; wait is ready
    //   EMPTY -> <handle>       await_suspend installs the waiter
    //   <handle> -> SIGNALED    signal() wins and schedules the waiter
    //   <handle> -> TIMED_OUT   deadline wins and schedules the waiter
    //
    // Terminal states absorb the loser's attempt.  (EMPTY -> TIMED_OUT is
    // unreachable single-use: the timer is armed only after the install.)
    //
    // ORDER: await_suspend publishes the suspended frame with a release CAS;
    // the deciding CAS in _decide is acq_rel (acquire: take ownership of
    // that frame before scheduling it; release: publish the signaler's
    // preceding writes -- the payload -- into the state word).  await_ready
    // and await_resume load with acquire, closing the payload edge when the
    // waiter observes SIGNALED.

    struct OneShotEvent : std::enable_shared_from_this<OneShotEvent> {

        static constexpr uintptr_t EMPTY = 0;
        static constexpr uintptr_t SIGNALED = 1;
        static constexpr uintptr_t TIMED_OUT = 2;

        Atomic<uintptr_t> _state{EMPTY};

        static std::shared_ptr<OneShotEvent> make() {
            return std::make_shared<OneShotEvent>();
        }

        void _decide(uintptr_t terminal) {
            uintptr_t expected = _state.load_relaxed();
            for (;;) {
                if ((expected == SIGNALED) || (expected == TIMED_OUT))
                    return;  // already decided; late or duplicate, a no-op
                if (_state.compare_exchange_weak_acq_rel_relaxed(expected,
                                                                 terminal)) {
                    if (expected != EMPTY)
                        global_work_queue_schedule((void*)expected);
                    return;
                }
            }
        }

        void signal() { _decide(SIGNALED); }

        struct WaitUntil {
            std::shared_ptr<OneShotEvent> _cell;
            std::chrono::steady_clock::time_point _when;

            bool await_ready() const noexcept {
                return _cell->_state.load_acquire() == SIGNALED;
            }
            // Defined in coroutine.cpp (libdispatch).  Returns false --
            // resume immediately -- when signal() beat the install.
            bool await_suspend(std::coroutine_handle<> handle) noexcept;
            bool await_resume() const noexcept {
                return _cell->_state.load_acquire() == SIGNALED;
            }
        };

        [[nodiscard]] WaitUntil wait_until(std::chrono::steady_clock::time_point when) {
            return WaitUntil{shared_from_this(), when};
        }

        [[nodiscard]] WaitUntil wait_for(std::chrono::nanoseconds duration) {
            return wait_until(std::chrono::steady_clock::now() + duration);
        }

    }; // struct OneShotEvent




} // namespace wry::Coroutine

namespace wry {

    // Process-lifetime work anchor (a Go-style wait group).  Background tasks
    // spawned onto the work queue register here so that main can block until
    // they finish before cancelling the thread pool -- otherwise a task that has
    // yielded mid-flight (e.g. a chunked background save) would be abandoned at
    // shutdown, leaking its frame and leaving a half-written file.
    //
    // It is inherently a single process-global instance (blocking; no other use
    // case), so it is expressed as static methods over hidden state

    // must call before wait is called
    void wait_group_spawn(Coroutine::Task task);

    // blocking; consumes main's sentinel, so call exactly once
    void wait_group_wait();

    // consumes main's sentinel, so call exactly once
    void wait_group_set_callback(void* callback);

} // namespace wry

#endif /* coroutine_hpp */
