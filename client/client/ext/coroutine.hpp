//
//  coroutine.hpp
//  client
//
//  Created by Antony Searle on 13/8/2025.
//

#ifndef coroutine_hpp
#define coroutine_hpp

#include <cassert>
#include <coroutine>
#include <deque>
#include <exception>
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
            auto count = self->_counter.sub_fetch_relaxed(1);
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
