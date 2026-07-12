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

#include "atomic.hpp"
#include "utility.hpp"

#include "epoch_allocator.hpp"
#include "mutex.hpp"

#include "global_work_queue.hpp"

namespace wry {

    void global_work_queue_schedule(std::coroutine_handle<>);

    // Defined in garbage_collected.cpp; forward-declared here so the
    // WaitForCollectionCycles awaitable below can be header-only.
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

    // co_await Until(t): suspend until steady_clock time point `t`, then resume
    // on a libdispatch global-queue thread.  Backed by dispatch_after_f (defined
    // in coroutine.cpp so this header stays libdispatch-free).  NOTE: the
    // coroutine resumes on a dispatch worker, NOT a GC-pinned mutator thread --
    // co_await SuspendAndSchedule and re-pin before touching GC state after the
    // wait.  If `t` is already past, await_ready short-circuits (no thread hop).
    struct Until {
        std::chrono::steady_clock::time_point _when;
        bool await_ready() const noexcept {
            return std::chrono::steady_clock::now() >= _when;
        }
        void await_suspend(std::coroutine_handle<>) const noexcept;  // coroutine.cpp
        void await_resume() const noexcept {}
    };

    // co_await ScheduleOnBlockableThread: hop the coroutine onto a libdispatch
    // global concurrent queue, whose threads may block (file IO, syscalls) and
    // grow unbounded on demand -- the opposite of our bounded, non-blocking GC
    // work queue.  Use for blocking work that must not occupy a mutator-pool
    // thread (cf. SuspendAndSchedule, which keeps the coroutine on the GC pool).
    // Backed by dispatch_async_f (coroutine.cpp).
    struct ScheduleOnBlockableThread {
        bool await_ready() const noexcept { return false; }
        void await_suspend(std::coroutine_handle<>) const noexcept;  // coroutine.cpp
        void await_resume() const noexcept {}
    };

    // Suspend until `cycles` full collection cycles have completed since
    // the suspend.  The collector schedules the resume via the global work
    // queue, so the coroutine resumes on a worker thread.  `cycles == 0`
    // is ready (no suspension).
    //
    // Useful for tests that need the collector to have had a chance to
    // observe and react to mutator-side changes.  Two cycles are typically
    // needed for the WAS_LOADED → READY → GONE progression of the weak
    // protocol; pass `cycles >= 3` to absorb the cycle that may have
    // already been in flight when the wait was requested.
    struct WaitForCollectionCycles {
        uint64_t cycles;
        bool await_ready() const noexcept { return cycles == 0; }
        void await_suspend(std::coroutine_handle<> handle) const noexcept {
            collector_register_cycle_callback(cycles, handle.address());
        }
        void await_resume() const noexcept {}
    };

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
                        global_work_queue_schedule(
                            std::coroutine_handle<>::from_address((void*)expected));
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



    // Basic coroutine
    
    template<typename...>
    struct Future;
    
    using Task = Future<>;
    
    template<>
    struct Future<> {
        
        struct Promise {
            
            std::coroutine_handle<> _continuation;
                                    
            Future get_return_object() {
                return Future{this};
            }
            
            constexpr std::suspend_always initial_suspend() const noexcept {
                return std::suspend_always{};
            }
            
            void unhandled_exception() const noexcept { abort(); }
            void return_void() const noexcept {}
            
            auto final_suspend() const noexcept {
                struct Awaitable : SuspendAndDestroy {
                    std::coroutine_handle<> await_suspend(std::coroutine_handle<Promise> handle) const noexcept {
                        std::coroutine_handle<> continuation = std::move(handle.promise()._continuation);
                        handle.destroy();
                        assert(continuation);
                        return continuation;
                    }
                };
                return Awaitable{};
            }
            
            std::coroutine_handle<Promise> get_handle() {
                return std::coroutine_handle<Promise>::from_promise(*this);
            }
            
            void set_continuation(std::coroutine_handle<> continuation) {
                assert(!_continuation);
                _continuation = std::move(continuation);
            }
            
            void set_continuation(void* ptr) {
                _continuation = std::coroutine_handle<>::from_address(ptr);
            }
            
            std::coroutine_handle<> take_continuation() {
                return std::exchange(_continuation, nullptr);
            }
                        
        };
        
        using promise_type = Promise;
        
        Promise* _promise;

        explicit Future(Promise* p) : _promise(p) {}

        Future() = delete;
        Future(Future const&) = delete;
        Future(Future&& other) : _promise(exchange(other._promise, nullptr)) {}
        ~Future() { if (_promise) abort(); }
        Future& operator=(Future const&) = delete;
        Future& operator=(Future&& other) {
            Future a{std::move(other)};
            using std::swap;
            swap(_promise, a._promise);
            return *this;
        }

        
        auto operator co_await() {
            struct Awaitable : std::suspend_always {
                Future _future;
                std::coroutine_handle<Promise> await_suspend(std::coroutine_handle<> continuation) noexcept {
                    _future._set_continuation(std::move(continuation));
                    return std::move(_future)._into_handle();
                }
            };
            return Awaitable{{}, std::move(*this)};
        }
        
        void _set_continuation(std::coroutine_handle<> continuation) {
            _promise->set_continuation(continuation);
        }

        void _set_continuation(void* continuation) {
            _promise->set_continuation(continuation);
        }
        
        std::coroutine_handle<Promise> _into_handle() && {
            return std::exchange(_promise, nullptr)->get_handle();
        }
        
    };
        
    
    
    
    template<typename T>
    struct Future<T> {
        
        struct Promise {
            
            std::coroutine_handle<> _continuation{};
            T* _target{};
            
            Future get_return_object() {
                return Future{this};
            }
            
            constexpr std::suspend_always initial_suspend() const noexcept {
                return std::suspend_always{};
            }
            
            void unhandled_exception() const noexcept { abort(); }
            void return_value(auto&& expression) const noexcept {
                *_target = FORWARD(expression);
            }
            
            auto final_suspend() const noexcept {
                struct Awaitable : SuspendAndDestroy {
                    std::coroutine_handle<> await_suspend(std::coroutine_handle<Promise> handle) const noexcept {
                        std::coroutine_handle<> continuation = std::move(handle.promise()._continuation);
                        handle.destroy();
                        assert(continuation);
                        return continuation;
                    }
                };
                return Awaitable{};
            }
            
            std::coroutine_handle<Promise> get_handle() {
                return std::coroutine_handle<Promise>::from_promise(*this);
            }
            
            void set_continuation(std::coroutine_handle<> continuation) {
                assert(!_continuation);
                _continuation = std::move(continuation);
            }
            
            void set_continuation(void* ptr) {
                _continuation = std::coroutine_handle<>::from_address(ptr);
            }
            
            void set_target(T* target) {
                assert(!_target);
                _target = target;
            }
            
            std::coroutine_handle<> take_continuation() {
                return std::exchange(_continuation, nullptr);
            }
            
        };
        
        using promise_type = Promise;
        
        Promise* _promise{};
        
        explicit Future(Promise* p) : _promise(p) {}
        
        Future() = delete;
        Future(Future const&) = delete;
        Future(Future&& other) : _promise(exchange(other._promise, nullptr)) {}
        ~Future() { if (_promise) abort(); }
        Future& operator=(Future const&) = delete;
        Future& operator=(Future&& other) {
            Future a{std::move(other)};
            using std::swap;
            swap(_promise, a._promise);
            return *this;
        }
        
        
        auto operator co_await() {
            struct Awaitable : std::suspend_always {
                Future _future;
                T _result;
                std::coroutine_handle<Promise> await_suspend(std::coroutine_handle<> continuation) noexcept {
                    _future._set_continuation(std::move(continuation));
                    _future._set_target(&_result);
                    return std::move(_future)._into_handle();
                }
                T await_resume() {
                    return std::move(_result);
                }
            };
            return Awaitable{{}, std::move(*this)};
        }
        
        void _set_continuation(std::coroutine_handle<> continuation) {
            _promise->set_continuation(continuation);
        }
        
        void _set_continuation(void* continuation) {
            _promise->set_continuation(continuation);
        }
        
        void _set_target(T* target) {
            _promise->set_target(target);
        }
        
        std::coroutine_handle<Promise> _into_handle() && {
            return std::exchange(_promise, nullptr)->get_handle();
        }
        
    };
    
    template<typename T> constexpr bool is_coroutine(T const&) { return false; }
    template<typename T> constexpr bool is_coroutine(Future<T> const&) { return true; }

    struct Nursery {
        
        void (*_resume)(void*) = &static_resume;
        Atomic<std::ptrdiff_t> _counter{0};
        std::ptrdiff_t _children = 0;
        std::coroutine_handle<> _continuation;
        
        static void static_resume(void* ptr) {
            auto self = (Nursery*)ptr;
            // Release: each completing child publishes its writes (e.g. the
            // result it stored into its join target) into _counter's modification
            // order.  The thread that drives the count to zero -- here, or the
            // joiner's load_acquire / exchange_acquire -- acquires the whole
            // chain of child releases (reference-count handoff).  A relaxed
            // decrement here would leave the post-join reads unsynchronized.
            auto count = self->_counter.sub_fetch_release(1);
            if (count == 0) {
                (void) self->_counter.load_acquire();
                ptr = self->_continuation.address();
                [[clang::musttail]] return (*(void(**)(void*))ptr)(ptr);
                // if we don't have tail call, we can use the global work queue
                // as a trampoline
            }
        }
         
        // co_await nursery.fork(foo(x)) immediately starts foo on the current thread
        // and schedules the caller to execute soon
        auto fork(Future<>&& future) {
            struct Awaitable : std::suspend_always {
                Nursery* _nursery;
                Future<> _future;
                std::coroutine_handle<Future<>::Promise> await_suspend(std::coroutine_handle<> continuation) noexcept {
                    ++(_nursery->_children);
                    _future._set_continuation(_nursery);
                    auto result = std::move(_future)._into_handle();
                    global_work_queue_schedule(std::move(continuation));
                    return result;
                }
            };
            return Awaitable{{}, this, std::move(future)};
        }
        
        // co_await nursery.fork(y, bar(x)) immediately starts bar on the current
        // thread and schedules the caller to execute soon.  When bar completes
        // it assigns to y; it is racy to access y until the fork has been
        // joined
        template<typename T>
        auto fork(T& target, Future<T>&& future) {
            struct Awaitable : std::suspend_always {
                Nursery* _nursery;
                Future<T> _future;
                T* _target;
                std::coroutine_handle<typename Future<T>::Promise> await_suspend(std::coroutine_handle<> continuation) noexcept {
                    ++(_nursery->_children);
                    _future._set_continuation(_nursery);
                    _future._set_target(_target);
                    auto result = std::move(_future)._into_handle();
                    global_work_queue_schedule(std::move(continuation));
                    return result;
                }
            };
            return Awaitable{{}, this, std::move(future), &target};
        }
        
        // co await nursery.join() suspends the caller and resumes it after
        // all forks are complete.  The nursery may then be resued.
        auto join() {
            struct Awaitable {
                Nursery* _nursery;
                bool await_ready() const noexcept {
                    auto count = _nursery->_counter.load_relaxed();
                    bool result = (count == -_nursery->_children);
                    if (result) {
                        _nursery->_counter.exchange_acquire(0);
                        _nursery->_children = 0;
                    }
                    return result;
                }
                std::coroutine_handle<> await_suspend(std::coroutine_handle<> continuation) const noexcept {
                    _nursery->_continuation = std::move(continuation);
                    auto count = _nursery->_counter.add_fetch_release(std::exchange(_nursery->_children, 0));
                    if (count == 0) {
                        (void) _nursery->_counter.load_acquire();
                        return _nursery->_continuation;
                    } else {
                        return std::noop_coroutine();
                    }
                }
                void await_resume() const noexcept {}
            };
            return Awaitable{this};
        }
        
        // nursery.soon(foo(x)) schedules foo to execute soon and continues
        // the calling context normally.  The calling context does not have to
        // be a coroutine
        void soon(Future<>&& future) {
            ++_children;
            future._set_continuation(this);
            global_work_queue_schedule(std::move(future)._into_handle());
        }
        
        template<typename T>
        void soon(T& target, Future<T>&& future) {
            _children++;
            future._set_continuation(this);
            future._set_target(&target);
            global_work_queue_schedule(std::move(future)._into_handle());
        }
                        
    }; // struct Nursery


    // Block the current thread until the awaitable completes.
    
    template<typename T>
    auto sync_wait(T&& awaitable) {
        struct Frame {
            void (*_resume)(void*) = &static_resume;
            std::binary_semaphore _semaphore{0}; // start in unavailable state
            static void static_resume(void* ptr) {
                auto self = (Frame*)ptr;
                self->_semaphore.release();
            }
        };
        Frame frame;
        if (!awaitable.await_ready()) {
            global_work_queue_schedule(awaitable.await_suspend(std::coroutine_handle<>::from_address(&frame)));
            frame._semaphore.acquire();
        }
        return awaitable.await_resume();
    }
    
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
