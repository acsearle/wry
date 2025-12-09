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
#include <variant>
#include <semaphore>

#include "atomic.hpp"
#include "utility.hpp"

#include "epoch_allocator.hpp"
#include "mutex.hpp"

#include "global_work_queue.hpp"

namespace wry {

    void global_work_queue_schedule(std::coroutine_handle<>);

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
    
    
    
    // Basic coroutine
    
    template<typename T>
    struct Future;
    
    using Task = Future<void>;
    
    template<>
    struct Future<void> {
        
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
                promise_type* _promise;
                explicit Awaitable(Promise* promise) : _promise(promise) {}
                ~Awaitable() { if (_promise) abort(); }
                std::coroutine_handle<Promise> await_suspend(std::coroutine_handle<> continuation) noexcept {
                    _promise->set_continuation(std::move(continuation));
                    return std::exchange(_promise, nullptr)->get_handle();
                }
            };
            return Awaitable{std::exchange(_promise, nullptr)};
        }
        
    };
        
    
    
    
    template<typename T>
    struct Future {
        
        struct Promise {
            
            std::coroutine_handle<> _continuation;
            T* _target;
            
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
                promise_type* _promise;
                T _result;
                explicit Awaitable(Promise* promise) : _promise(promise) {}
                ~Awaitable() { if (_promise) abort(); }
                std::coroutine_handle<Promise> await_suspend(std::coroutine_handle<> continuation) noexcept {
                    _promise->set_continuation(std::move(continuation));
                    _promise->_target = &_result;
                    return std::exchange(_promise, nullptr)->get_handle();
                }
                T await_resume() {
                    return std::move(_result);
                }
            };
            return Awaitable{std::exchange(_promise, nullptr)};
        }
        
    };

    struct Nursery {
        
        void (*_resume)(void*) = &static_resume;
        Atomic<std::ptrdiff_t> _counter{0};
        std::ptrdiff_t _children = 0;
        std::coroutine_handle<> _continuation;
        
        static void static_resume(void* ptr) {
            auto self = (Nursery*)ptr;
            auto count = self->_counter.sub_fetch(1, Ordering::RELEASE);
            if (count == 0) {
                (void) self->_counter.load(Ordering::ACQUIRE);
                ptr = self->_continuation.address();
                [[clang::musttail]] return (*(void(**)(void*))ptr)(ptr);
                // if we don't have tail call, we can use the global work queue
                // as a trampoline
            }
        }
                
        auto fork(Task&& task) {
            struct Awaitable : std::suspend_always {
                Nursery* _nursery;
                Task _task;
                std::coroutine_handle<Task::Promise> await_suspend(std::coroutine_handle<> continuation) noexcept {
                    _nursery->_children++;
                    _task._promise->set_continuation(_nursery);
                    auto result = std::exchange(_task._promise, nullptr)->get_handle();
                    global_work_queue_schedule(std::move(continuation));
                    return result;
                }
            };
            return Awaitable{{}, this, std::move(task)};
        }
        
        template<typename T>
        auto fork(T& target, Future<T>&& future) {
            struct Awaitable : std::suspend_always {
                Nursery* _nursery;
                Future<T> _future;
                T* _target;
                std::coroutine_handle<typename Future<T>::Promise> await_suspend(std::coroutine_handle<> continuation) noexcept {
                    _nursery->_children++;
                    _future._promise->set_continuation(_nursery);
                    _future._promise->_target = _target;
                    auto result = std::exchange(_future._promise, nullptr)->get_handle();
                    global_work_queue_schedule(std::move(continuation));
                    return result;
                }
            };
            return Awaitable{{}, this, std::move(future), &target};
        }
        
        auto join() {
            struct Awaitable {
                Nursery* _nursery;
                bool await_ready() const noexcept {
                    auto count = _nursery->_counter.load(Ordering::RELAXED);
                    bool result = (count == -_nursery->_children);
                    if (result) {
                        _nursery->_counter.exchange(0, Ordering::ACQUIRE);
                        _nursery->_children = 0;
                    }
                    return result;
                }
                std::coroutine_handle<> await_suspend(std::coroutine_handle<> continuation) const noexcept {
                    _nursery->_continuation = std::move(continuation);
                    auto count = _nursery->_counter.add_fetch(std::exchange(_nursery->_children, 0), Ordering::RELEASE);
                    if (count == 0) {
                        (void) _nursery->_counter.load(Ordering::ACQUIRE);
                        return _nursery->_continuation;
                    } else {
                        return std::noop_coroutine();
                    }
                }
                void await_resume() const noexcept {}
            };
            return Awaitable{this};
        }
        
        void spawn(Task&& task) {
            _children++;
            task._promise->set_continuation(this);
            global_work_queue_schedule(std::exchange(task._promise, nullptr)->get_handle());
        }
        
        template<typename T>
        void spawn(T& victim, Future<T>&& future) {
            _children++;
            future._promise->set_continuation(this);
            future._promise->_target = &victim;
            global_work_queue_schedule(std::exchange(future._promise, nullptr)->get_handle());
        }
        
        void sync_join() {
            struct Frame {
                void (*_resume)(void*) = &static_resume;
                std::binary_semaphore _semaphore{0}; // start unavailable
                static void static_resume(void* ptr) {
                    auto self = (Frame*)ptr;
                    self->_semaphore.release();
                }
            };
            Frame frame;
            _continuation = std::coroutine_handle<void>::from_address(&frame);
            auto count = _counter.add_fetch(std::exchange(_children, 0), Ordering::RELEASE);
            if (count == 0) {
                (void) _counter.load(Ordering::ACQUIRE);
            } else {
                frame._semaphore.acquire();
            }
        }
        
    }; // struct Nursery

    
    
   
    
    
    
    



    
    
    
    // Credit: Lewis Baker's cppcoro
    
    
    struct SingleConsumerEvent {
        
        Atomic<intptr_t> _state{0};
        
        // atomically set the event and schedule any waiting coroutine
        
        void set_and_schedule_continuation() {
            intptr_t was = _state.exchange(1, Ordering::ACQ_REL);
            switch (was) {
                case 0:
                    break;
                case 1:
                    // Don't allow this unless a compelling use case is
                    // discovered
                    abort();
                default:
                    // The state encodes a coroutine
                    global_work_queue_schedule(std::coroutine_handle<>::from_address((void*)was));
                    break;
            }
        }
        
        [[nodiscard]] std::coroutine_handle<> /* Nullable */ set_and_return_continuation() {
            intptr_t was = _state.exchange(1, Ordering::ACQ_REL);
            switch (was) {
                case 0:
                    return nullptr;
                case 1:
                    abort();
                default:
                    return std::coroutine_handle<>::from_address((void*)was);
            }
        }
        
        // reset the event
        void reset() {
            intptr_t expected = 1;
            (void) _state.compare_exchange_strong(expected, 0, Ordering::RELAXED, Ordering::RELAXED);
            switch (expected) {
                case 0:
                    // The event was not signaled anyway
                case 1:
                    // The event was signaled
                default:
                    // The even was not signaled anyway, and had a continuation
                    break;
            }
        }
        
        struct awaitable_type {
            
            SingleConsumerEvent* _context = nullptr;
            intptr_t _expected = 0;
            
            bool await_ready() noexcept {
                _expected = _context->_state.load(Ordering::ACQUIRE);
                // If the event is already set, just continue without suspending
                return _expected == 1;
            }
            
            bool await_suspend(std::coroutine_handle<> handle) noexcept {
                intptr_t desired = (intptr_t)handle.address();
                assert((desired != 0) && (desired != 1));
                for (;;) switch (_expected) {
                    case 0:
                        // Atomically install the current coroutine as the awaiter
                        if (_context->_state.compare_exchange_weak(_expected,
                                                                   desired,
                                                                   Ordering::RELEASE,
                                                                   Ordering::ACQUIRE))
                            return true;
                        break;
                    case 1:
                        // (rare) The event was signaled before we could install
                        // ourself, resume immediately
                        return false;
                    default:
                        // (forbidden) The event is already awaited by another coroutine
                        abort();
                }
            }
            
            void await_resume() noexcept {
            };
            
        };
        
        // atomically wait until the event is set
        awaitable_type operator co_await() {
            return awaitable_type{this};
        }
        
    };
    
    
    
    struct SingleConsumerLatch {
        
        // We can't store both a continuation address and an arbitrary count
        // in a single atomic
        
        Atomic<ptrdiff_t> _counter;
        SingleConsumerEvent _event;
        
        explicit SingleConsumerLatch(ptrdiff_t initial_count)
        : _counter(initial_count) {
        }
        
        ~SingleConsumerLatch() {
            assert(_counter.load(Ordering::RELAXED) == 0);
        }
                
        bool _count_down_common(ptrdiff_t n) {
            assert(n > 0);
            ptrdiff_t count = _counter.sub_fetch(n, Ordering::RELEASE);
            bool result = (count == 0);
            if (result)
                (void) _counter.load(Ordering::ACQUIRE);
            return result;
        }
        
        void count_down(ptrdiff_t n = 1) {
            if (_count_down_common(n))
                _event.set_and_schedule_continuation();
        }
        
        [[nodiscard]] std::coroutine_handle<> count_down_and_return_continuation(ptrdiff_t n = 1) {
            if (_count_down_common(n))
                return _event.set_and_return_continuation();
            else
                return nullptr;
        }
        
        using awaitable_type = SingleConsumerEvent::awaitable_type;
        
        awaitable_type operator co_await() {
            return _event.operator co_await();
        }
        
        bool try_wait() const {
            return _counter.load(Ordering::ACQUIRE) == 0;
        }
        
        
        
        
        
        // TODO: Rename this co_notify or something, and call notify on a
        // generic first argument
        
        // usage: Latch::WillDecrement my_coroutine(&my_latch, my_arguments...) { ... }
        //
        // on completion of the coroutine, it will signal the latch and, if
        // complete, transfer control to the latch's continuation
        struct WillDecrement {
            
            struct promise_type {
                
                //                // match all arguments
                //                static void* operator new(std::size_t count, SingleConsumerLatch*, auto&&...) {
                //                    return bump::this_thread_state.allocate(count);
                //                }
                //
                //                static void operator delete(void* ptr) {
                //                }
                
                SingleConsumerLatch* _latch;
                promise_type() = delete;
                explicit promise_type(SingleConsumerLatch* p, auto&&...)
                : _latch(p) {
                }
                
                ~promise_type() {
                    printf("WillDecrement::promise_type was deleted\n");
                }
                
                constexpr WillDecrement get_return_object() const noexcept {
                    return {};
                }
                
                SuspendAndSchedule initial_suspend() noexcept { return {}; }
                
                auto final_suspend() noexcept {
                    
                    struct Awaitable : ResumeNever {
                        
                        SingleConsumerLatch* _context;
                        std::coroutine_handle<> await_suspend(std::coroutine_handle<promise_type> handle) noexcept {
                            // Save the context
                            SingleConsumerLatch* context = _context;
                            handle.destroy();
                            return null_to_noop(context->count_down_and_return_continuation(1));
                        }
                    };
                    return Awaitable{{}, _latch};
                }
                
                constexpr void return_void() noexcept {
                    // normal return
                }
                
                void unhandled_exception() noexcept {
                    __builtin_trap();
                }
                
            }; // struct SingleConsumerLatch::WillDecrement::promise_type
            
        }; // struct SingleConsumerLatch::WillDecrement
        
    }; // struct SingleConsumerLatch
    
    
    
    
    
    
    struct MultipleConsumerEvent {

        enum {
            SET_NO  = 0,
            SET_YES = 1,
        };
        Atomic<intptr_t> _state{SET_NO};
        
        struct awaitable_type {
            MultipleConsumerEvent* context;
            intptr_t _next;
            std::coroutine_handle<> _continuation;
            
            bool await_ready() noexcept {
                _next = context->_state.load(Ordering::ACQUIRE);
                return _next == SET_YES;
            }
            
            std::coroutine_handle<> await_suspend(std::coroutine_handle<> handle) noexcept {
                _continuation = handle;
                for (;;) switch (_next) {
                    case SET_YES:
                        return handle;
                    case SET_NO:
                    default:
                        if (context-> _state.compare_exchange_weak(_next,
                                                                   (intptr_t)this,
                                                                   Ordering::RELEASE,
                                                                   Ordering::ACQUIRE))
                            return std::noop_coroutine();
                }
            }
            
            void await_resume() const noexcept {
            }
            
        };
        
        awaitable_type operator co_await() {
            return awaitable_type{this};
        }

        void set() {
            intptr_t was = _state.exchange(SET_YES, Ordering::ACQUIRE);
            switch (was) {
                case SET_YES:
                    break;
                case SET_NO:
                default: {
                    auto p = (awaitable_type*)was;
                    while (p) {
                        // Thundering herd
                        global_work_queue_schedule(p->_continuation);
                        p = (awaitable_type*)(p->_next);
                    }
                    break;
                }
            }
        }
        
        void reset() {
            intptr_t expected = _state.load(Ordering::RELAXED);
            for (;;) switch (expected) {
                case SET_YES:
                    if (_state.compare_exchange_weak(expected,
                                                     SET_NO,
                                                     Ordering::RELAXED,
                                                     Ordering::RELAXED))
                        return;
                    break;
                default:
                    return;
            }
        }
        
    }; // MultipleConsumerEvent
    
    
    
    
    struct Barrier {
        
        Atomic<ptrdiff_t> _counter;
        MultipleConsumerEvent _event;
        
        explicit Barrier(ptrdiff_t n) : _counter(n) {}
        
        MultipleConsumerEvent::awaitable_type operator co_await() {
            // TODO: Can we rely on the MultipleConsumerEvent to enforce memory ordering?
            ptrdiff_t n = _counter.sub_fetch(1, Ordering::RELEASE);
            if (n < 0)
                abort();
            if (n == 0) {
                (void) _counter.load(Ordering::ACQUIRE);
                _event.set();
            }
            return _event.operator co_await();
        }
        
    };
    
    
    
    


    // The variables held by a coroutine are inaccessible to us and thus cannot
    // be traced by the garbage collector.
    //
    // TODO: Coroutines can be desugared mechanically down to ordinary
    // structures.
    //
    // Coroutines use the epoch allocator for their own storage, and rely on
    // the garbage collector epoch to keep persistent objects alive while
    // working with them.
    //
    // TODO: The bump allocator epoch and the garbage collector epoch are
    // essentially the same concept, and we can probably unify them fruitfully.
    //
    // TODO: Advancing the epoch is a per-frame operation, we might as well
    // do so explicitly.
        
    /*
     
     C++ coroutine_handle support:
     
     void  __builtin_coro_resume(void *addr);
     void  __builtin_coro_destroy(void *addr);
     bool  __builtin_coro_done(void *addr);
     void *__builtin_coro_promise(void *addr, int alignment, bool from_promise)
     
     https://clang.llvm.org/docs/LanguageExtensions.html#c-coroutines-support-builtins
          
     Coroutine implementation support:
     
     size_t __builtin_coro_size()
     void  *__builtin_coro_frame()
     void  *__builtin_coro_free(void *coro_frame)
     
     void  *__builtin_coro_id(int align, void *promise, void *fnaddr, void *parts)
     bool   __builtin_coro_alloc()
     void  *__builtin_coro_begin(void *memory)
     void   __builtin_coro_end(void *coro_frame, bool unwind)
     char   __builtin_coro_suspend(bool final)
     
     https://llvm.org/docs/Coroutines.html#intrinsics

     Coroutine frame layout pseudocode:
     
     struct __coroutine_frame_type {
         void (*__resume)(void* addr);
         void (*__destroy)(void* addr);
         __coroutine_promise_type __promise;
         byte __state[0];
     };
     
     LLVM will instantiate a single __resume function that switches over an
     explicit __index to resume from different suspend points.  Likewise for
     __destroy.

     */
    
    // We can manually construct objects with the appropriate header to
    // be consumed by an executor as-if they are coroutines
        
    struct Header {
        void (*resume)(void*);
        void (*destroy)(void*);
    };
    
    template<typename Promise>
    struct Frame {
        Header header;
        Promise promise;
        /* copies-of-arguments */
        /* suspend-point-index */
        /* variables-spanning-suspend-point */
    };
    
    inline std::coroutine_handle<> coroutine_handle_from(Header* header) {
        return std::coroutine_handle<>::from_address((void*)header);
    }
    
    inline void resume_by_address(void* address) {
        __builtin_coro_resume(address);
    }

    inline void destroy_by_address(void* address) {
        __builtin_coro_destroy(address);
    }
    
    inline bool is_done_by_address(void* address) {
        return __builtin_coro_done(address);
    }
    
    template<typename Promise>
    Promise* promise_from_address(void* address) {
        return __builtin_coro_promise(address,
                                      (int) alignof(Promise),
                                      /*from-promise=*/false);
    }

    template<typename Promise>
    void* address_from_promise(Promise* address) {
        return __builtin_coro_promise(address,
                                      (int) alignof(Promise),
                                      /*from-promise=*/true);
    }

    constexpr inline struct _self_promise_t {} self_promise;
    
        
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    

    
    

    
    
    struct Mutex {
        
        struct Awaitable;

        enum : intptr_t {
            LOCKED = 0,
            UNLOCKED = 1,
        };
                
        Atomic<intptr_t> _state{UNLOCKED};
        Awaitable* _awaiters = nullptr;
        
        struct Awaitable {
            Mutex* _context;
            intptr_t _state;
            std::coroutine_handle<> _handle;

            bool await_ready() noexcept {
                _state = UNLOCKED;
                return _context->_state.compare_exchange_weak(_state,
                                                              LOCKED,
                                                              Ordering::ACQUIRE,
                                                              Ordering::RELAXED);
            }
            
            bool await_suspend(std::coroutine_handle<> handle) noexcept {
                _handle = handle;
                for (;;) {
                    switch (_state) {
                        case UNLOCKED:
                            if (_context->_state.compare_exchange_weak(_state,
                                                                       LOCKED,
                                                                       Ordering::ACQUIRE,
                                                                       Ordering::RELAXED))
                                return false;
                            break;
                        default:
                            if (_context->_state.compare_exchange_weak(_state,
                                                                       (intptr_t)this,
                                                                       Ordering::RELEASE,
                                                                       Ordering::RELAXED))
                                return true;
                            break;
                    }
                }
            }
            
            [[nodiscard]] std::unique_lock<Mutex> await_resume() noexcept {
                // We wake up owning the mutex
                return std::unique_lock<Mutex>(*_context, std::adopt_lock);
            }
        };
        
        Awaitable operator co_await() {
            return Awaitable{this};
        }
                              
        void unlock() {
            intptr_t expected = {};
            if (_awaiters)
                goto ALPHA;
            expected = _state.load(Ordering::RELAXED);
            for (;;) switch (expected) {
                case UNLOCKED:
                    abort();
                case LOCKED:
                    if (_state.compare_exchange_strong(expected, UNLOCKED, Ordering::RELEASE, Ordering::RELAXED))
                        return;
                    break;
                default:
                    if (_state.compare_exchange_strong(expected, LOCKED, Ordering::ACQUIRE, Ordering::RELAXED)) {
                        _awaiters = (Awaitable*)expected;
                    ALPHA:
                        global_work_queue_schedule(std::exchange(_awaiters, (Awaitable*)_awaiters->_state)->_handle);
                        return;
                    }
                    break;
            }
        }
        
        
    };
    
    
    

    template<typename T, template<typename> typename A = EpochAllocator>
    struct CoroutineBlockingDeque {
        
        struct Awaitable;
        
        mutable std::mutex _mutex;
        std::deque<T, A<T>> _deque;
        bool _is_canceled;
        std::deque<Awaitable*, A<Awaitable*>> _waiting;
        
        void push_back(T item) {
            WITH(std::unique_lock lock{_mutex}) {
                if (_waiting.empty()) {
                    _deque.push_back(std::move(item));
                } else {
                    assert(_deque.empty());
                    Awaitable* awaitable = _waiting.front();
                    _waiting.pop_front();
                    *(awaitable->_victim) = std::move(item);
                    awaitable->_result = true;
                    global_work_queue_schedule(awaitable->_coroutine_handle);
                }
            }
        }

        void push_front(T item) {
            WITH(std::unique_lock lock{_mutex}) {
                if (_waiting.empty()) {
                    _deque.push_front(std::move(item));
                } else {
                    assert(_deque.empty());
                    Awaitable* awaitable = _waiting.front();
                    _waiting.pop_front();
                    *(awaitable->_victim) = std::move(item);
                    awaitable->_result = true;
                    global_work_queue_schedule(awaitable->_coroutine_handle);
                }
            }
        }
        
        bool try_pop_front(T& victim) {
            WITH(std::unique_lock lock{_mutex}) {
                bool result = !_deque.empty();
                if (result) {
                    victim = std::move(_deque.front());
                    _deque.pop_front();
                }
                return result;
            }
        }

        bool try_pop_back(T& victim) {
            WITH(std::unique_lock lock{_mutex}) {
                bool result = !_deque.empty();
                if (result) {
                    victim = std::move(_deque.back());
                    _deque.pop_back();
                }
                return result;
            }
        }

        void cancel() {
            std::deque<Awaitable*> waiting;
            WITH(std::unique_lock lock{_mutex}) {
                _is_canceled = true;
                using std::swap;
                swap(waiting, _waiting);
                assert(_waiting.empty());
            }
            for (Awaitable* awaitable : waiting)
                global_work_queue_schedule(awaitable->_coroutine_handle);
        }
        
        struct Awaitable {
            CoroutineBlockingDeque* _context;
            T* _victim;
            bool _result;
            std::coroutine_handle<> _coroutine_handle;
            
            void await_suspend(std::coroutine_handle<> handle) noexcept {
                // Lock transfers from await_ready() -> false
                WITH(std::unique_lock guard(_context->_mutex, std::adopt_lock)) {
                    _coroutine_handle = handle;
                    assert(!_context->_is_canceled);
                    _context->_waiting.push_back(this);
                }
            }
            
            bool await_resume() const noexcept {
                return _result;
            }
            
        };
        
        auto pop_front_wait(T& victim) {
            struct PopFrontAwaitable : Awaitable {
                
                bool await_ready() noexcept {
                    WITH(std::unique_lock guard(this->_context->_mutex)) {
                        if (!this->_context->_deque.empty()) {
                            if (!this->_context->_is_canceled) {
                                // Lock transfers into await_suspend(...)
                                guard.release();
                                return false;
                            }
                            assert(this->_result == false);
                        } else {
                            *this->_victim = std::move(this->_context->_deque.front());
                            this->_context->_deque.pop_front();
                            this->_result = true;
                        }
                        return true;
                    }
                }
                
            };
            return PopFrontAwaitable{{this, &victim, false}};
        };
        
        auto pop_back_wait(T& victim) {
            struct PopBackAwaitable : Awaitable {
                
                bool await_ready() noexcept {
                    WITH(std::unique_lock guard{this->_context->_mutex}) {
                        if (!this->_context->_deque.empty()) {
                            if (!this->_context->_is_canceled) {
                                // Lock transfers into await_suspend(...)
                                guard.release();
                                return false;
                            }
                            assert(this->_result == false);
                        } else {
                            *this->_victim = std::move(this->_context->_deque.back());
                            this->_context->_deque.pop_back();
                            this->_result = true;
                        }
                        return true;
                    }
                }
                
            };
            return PopBackAwaitable{{this, &victim, false}};
        };
        
    }; // CoroutineBlockingDeque
    
    
    
    
    // Worked example of a manual coroutine mostly compatible with the Promise
    // interface.
    
    // Manual coroutines at least have the potential to be garbage collected
    
    template<typename Promise>
    struct Example {
        
        using ReturnObjectType = decltype(std::declval<Promise&>().get_return_object());
        using AwaitableTypeInitial = decltype(std::declval<Promise&>().initial_suspend());
        using AwaitableTypeFinal = decltype(std::declval<Promise&>().initial_suspend());

        Header _header;
        Promise _promise;
        
        enum StateTag {
            INITIAL,
            FINAL,
        };
        
        StateTag _state_tag;

        // Tagged union of the current awaitable
        enum AwaitableTag {
            AWAITABLE_TAG_NONE,
            AWAITABLE_TAG_INITIAL,
            AWAITABLE_TAG_FINAL,
        } _awaitable_tag;
        union {
            char _awaitable_none;
            AwaitableTypeInitial _awaitable_initial;
            AwaitableTypeFinal _awaitable_final;
        };
        
        Example(/* args */)
        : _header{
            .resume = [](void* a) -> void { ((Example*)a)->_resume(); },
            .destroy = [](void* a) -> void { ((Example*)a)->_destroy(); },
        }, _promise(/* args */)
        , _state_tag{INITIAL} {
        }
                    
        static ReturnObjectType execute() {
            Example* self = new Example;
            return self->execute();
        }
        
        ReturnObjectType _execute() {
            ReturnObjectType return_object = _promise.get_return_object();
            _initial_suspend();
            return return_object;
        }
        
        void _initial_suspend() {
            assert(_awaitable_tag == AWAITABLE_TAG_NONE);
            new(&_awaitable_initial) AwaitableTypeInitial(_promise.initial_suspend());
            _awaitable_tag = AWAITABLE_TAG_INITIAL;
            if (_awaitable_initial.await_ready()) {
                _resume();
            } else {
                std::coroutine_handle<> continuation
                = _awaitable_initial.await_suspend(std::coroutine_handle<Promise>::from_address(&(_header)));
                // TODO: tail call vs structured code
                if (continuation)
                    continuation.resume();
            }
        }
        
        auto _initial_resume() {
            assert(_awaitable_tag == AWAITABLE_TAG_INITIAL);
            auto result = _awaitable_initial.await_resume();
            std::destroy_at(&_awaitable_initial);
            _awaitable_tag = AWAITABLE_TAG_NONE;
            return result;
        }
        
        void _final_suspend() {
            _header.resume = nullptr; // mark coroutine as done
            assert(_awaitable_tag == AWAITABLE_TAG_NONE);
            new (&_awaitable_final) AwaitableTypeFinal(_promise.final_suspend());
            _awaitable_tag = AWAITABLE_TAG_FINAL;
            if (!_awaitable_final.await_ready()) {
                std::coroutine_handle<> continuation
                = _awaitable_final.await_suspend(std::coroutine_handle<Promise>::from_address(&(_header)));
                // TODO: tail call vs structured code
                if (continuation)
                    continuation.resume();
            }
        }

        void _resume() {
            switch (_state_tag) {
                case INITIAL: {
                    (void) _initial_resume();
                    
                    // do some work
                    
                    _promise.return_void();
                    _final_suspend();
                    return;
                }
                case FINAL:
                    abort(); // resumed a done coroutine
                default:
                    abort(); // invalid state tag
            }
        }
        
        void _destroy() {
            switch (_awaitable_tag) {
                case AWAITABLE_TAG_NONE:
                    break;
                case AWAITABLE_TAG_INITIAL:
                    std::destroy_at(_awaitable_initial);
                    break;
                case AWAITABLE_TAG_FINAL:
                    std::destroy_at(_awaitable_final);
                    break;
                default:
                    abort(); // invalid awaitable_tag
            }
            switch (_state_tag) {
                default:
                    break;
            }
            delete this;
        }
        
    };
    
    
    // Simple manual coroutine example that doesn't use the Promise machinery
    
    struct Example2 {
        
        void (*_resume)(void*);
        void (*_destroy)(void*);
        
        Example2()
        : _resume(&_static_resume)
        , _destroy(&_static_destroy) {
        }
        
        // Tagged union holds necessary state that crosses suspension points
        enum Tag {
            INITIAL,
            FINAL,
        } _tag;
        union {
            char _dummy;
        };
        
        
        
        static void _static_resume(void* context) {
            Example2* self = (Example2*)context;
            switch (self->_tag) {
                case INITIAL:
                    // some work
                    
                    // [[clang::musttail]] continuation->resume(continuation);
                    
                    self->_tag = FINAL;
                    self->_resume = nullptr;
                    return;
                case FINAL:
                default:
                    abort();
            }
        };

        static void _static_destroy(void* context) {
            Example2* self = (Example2*)context;
            switch (self->_tag) {
                case INITIAL:
                    break;
                case FINAL:
                    break;
            }
            delete self;
        };
        
    };

    
    
    
} // namespace wry::Coroutine

#endif /* coroutine_hpp */
