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

#include "atomic.hpp"
#include "utility.hpp"

#include "epoch_allocator.hpp"
#include "mutex.hpp"

namespace wry::coroutine {

    void schedule_coroutine_handle_from_address(void* address);
    void schedule_coroutine_handle(std::coroutine_handle<>);

    // Work until canceled
    void worker_thread_loop();    
    void cancel_global_work_queue();


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
    
    
    // Windows-style Events are a good fit for coroutines
    // Credit: Lewis Baker's cppcoro
    
    // A manual reset event supporting a single waiter is perhaps the most
    // simple useful coroutine primitive
    //
    // Stores NONSIGNALED, SIGNALED, or the address of the awaiting coroutine
    
    struct SingleConsumerEvent {
        
        enum : intptr_t {
            NONSIGNALED = 0,
            SIGNALED = 1,
        };
        Atomic<intptr_t> _state{NONSIGNALED};
                
        // atomically set the event and schedule any waiting coroutine
        
        void set() {
            intptr_t was = _state.exchange(SIGNALED, Ordering::ACQ_REL);
            switch (was) {
                case NONSIGNALED:
                    return;
                case SIGNALED:
                    // Don't allow this unless a compelling use case is
                    // discovered
                    abort();
                    return;
                default:
                    // The state encodes a coroutine
                    schedule_coroutine_handle_from_address((void*)was);
                    return;
            }
        }
               
        // reset the event
        void reset() {
            intptr_t expected = SIGNALED;
            (void) _state.compare_exchange_strong(expected, NONSIGNALED, Ordering::RELAXED, Ordering::RELAXED);
            // Don't allow this unless a compelling use case is
            // discovered
            assert(expected == SIGNALED);
        }
        
        struct Awaitable {
            Atomic<intptr_t>& _state;
            intptr_t _expected;
            
            bool await_ready() noexcept {
                _expected = _state.load(Ordering::ACQUIRE);
                // If the event is already set, just continue without suspending
                return _expected == SIGNALED;
            }
            
            bool await_suspend(std::coroutine_handle<> handle) noexcept {
                intptr_t desired = (intptr_t)handle.address();
                assert((desired != NONSIGNALED) && (desired != SIGNALED));
                for (;;) switch (_expected) {
                    case NONSIGNALED:
                        // Atomically install the current coroutine as the awaiter
                        if (_state.compare_exchange_weak(_expected,
                                                         desired,
                                                         Ordering::RELEASE,
                                                         Ordering::ACQUIRE))
                            return true;
                        break;
                    case SIGNALED:
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
        Awaitable operator co_await() {
            return Awaitable{_state};
        }
        
    };
    
    struct SingleConsumerLatch {
        
        enum : intptr_t {
            NONSIGNALED = 0,
            SIGNALED = 1,
        };
        
        Atomic<ptrdiff_t> _count;
        Atomic<intptr_t> _continuation;
                
        explicit SingleConsumerLatch(ptrdiff_t initial_count)
        : _count(initial_count)
        , _continuation(NONSIGNALED) {
        }
        
        ~SingleConsumerLatch() {
            assert(_count.load(Ordering::RELAXED) == 0);
        }
        
        void decrement() {
            subtract(1);
        }
        
        void subtract(ptrdiff_t count) {
            assert(count > 0);
            ptrdiff_t n = _count.sub_fetch((ptrdiff_t)count, Ordering::RELEASE);
            if (n != 0)
                return;
            (void) _count.load(Ordering::ACQUIRE);
            intptr_t observed = _continuation.exchange(SIGNALED, Ordering::RELEASE);
            assert(observed != SIGNALED);
            if (observed == NONSIGNALED)
                return;
            (void) _continuation.load(Ordering::ACQUIRE);
            schedule_coroutine_handle_from_address((void*)observed);
        }
        
        bool _signalling_coroutine_decrement() {
            ptrdiff_t n = _count.sub_fetch((ptrdiff_t)1, Ordering::RELEASE);
            bool result = (n == 0);
            if (result)
                (void) _count.load(Ordering::ACQUIRE);
            return result;
        }
        
        std::coroutine_handle<> _signal_and_get_continuation() {
            intptr_t observed = _continuation.exchange(SIGNALED, Ordering::RELEASE);
            if (observed != NONSIGNALED) {
                (void) _continuation.load(Ordering::ACQUIRE);
                return std::coroutine_handle<>::from_address((void*)observed);
            } else {
                return std::noop_coroutine();
            }
        }

        // as Awaitable
        
        bool await_ready() noexcept {
            ptrdiff_t n = _count.load(Ordering::RELAXED);
            // TODO: verify this is actually an optimization
            if (n == 0)
                // all the jobs already finished
                (void) _count.load(Ordering::ACQUIRE);
            // some jobs were not yet finished
            return n == 0;
        }
        
        bool await_suspend(std::coroutine_handle<> handle) noexcept {
            intptr_t expected = NONSIGNALED;
            // install the handler; failure means jobs completed and we resume immediately
            return _continuation.compare_exchange_strong(expected,
                                                         (intptr_t)handle.address(),
                                                         Ordering::RELEASE,
                                                         Ordering::ACQUIRE);
        }
        
        void await_resume() {
            // this_thread::local_gc_log_node = _services->_log_nodes[this_thread::id];
        }
        
        // usage: Latch::WillDecrement my_coroutine(&my_latch, my_arguments...) { ... }
        //
        // on completion of the coroutine, it will signal the latch and, if
        // complete, transfer control to the latch's continuation
        struct WillDecrement {
            
            struct promise_type {
                
                // match all arguments
                static void* operator new(std::size_t count, SingleConsumerLatch&, auto&&...) {
                    return bump::this_thread_state.allocate(count);
                }
                
                static void operator delete(void* ptr) {
                }
                
                SingleConsumerLatch* _latch;
                promise_type() = delete;
                explicit promise_type(SingleConsumerLatch* p, auto&&...)
                : _latch(p) {
                }
                
                constexpr WillDecrement get_return_object() const noexcept {
                    return {};
                }
                
                auto initial_suspend() noexcept {
                    struct Awaitable {
                        constexpr bool await_ready() noexcept {
                            return false;
                        }
                        void await_suspend(std::coroutine_handle<promise_type> handle) noexcept {
                            schedule_coroutine_handle(handle);
                        }
                        void await_resume() noexcept {}
                    }; // struct Awaitable
                    return Awaitable{};
                }
                
                auto final_suspend() noexcept {
                    struct Awaitable {
                        SingleConsumerLatch* _latch;
                        bool await_ready() noexcept {
                            return !(_latch->_signalling_coroutine_decrement());
                        }
                        std::coroutine_handle<> await_suspend(std::coroutine_handle<promise_type> handle) noexcept {
                            // The latch has counted down to zero
                            // Copy from the coroutine frame to the stack frame
                            SingleConsumerLatch& target = *_latch;
                            // Destroy the coroutine frame
                            handle.destroy();
                            // Signal the latch and resume a waiting coroutine
                            return target._signal_and_get_continuation();
                        }
                        void await_resume() noexcept {
                            // Latch is not ready
                        }
                    };
                    return Awaitable{_latch};
                }
                
                constexpr void return_void() noexcept {
                    // normal return
                }
                
                void unhandled_exception() noexcept {
                    __builtin_trap();
                }
                
                decltype(auto) await_transform(auto&& awaitable) {
                    if constexpr (!std::is_same_v<std::decay_t<decltype(awaitable)>, _self_promise_t>) {
                        return FORWARD(awaitable);
                    } else {
                        struct Awaitable {
                            promise_type* _promise;
                            constexpr bool await_ready() const noexcept {
                                return true;
                            }
                            constexpr void await_suspend(std::coroutine_handle<>) const noexcept {
                                __builtin_trap();
                            };
                            promise_type& await_resume() const noexcept {
                                return *_promise;
                            }
                        };
                        return Awaitable{this};
                    }
                }
                
            }; // struct SingleConsumerLatch::WillDecrement::promise_type
            
        }; // struct SingleConsumerLatch::WillDecrement
        
    }; // struct SingleConsumerLatch
    
    
    
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
            intptr_t _expected;
            std::coroutine_handle<> _handle;

            bool await_ready() noexcept {
                _expected = UNLOCKED;
                return _context->_state.compare_exchange_weak(_expected,
                                                              LOCKED,
                                                              Ordering::ACQUIRE,
                                                              Ordering::RELAXED);
            }
            
            bool await_suspend(std::coroutine_handle<> handle) noexcept {
                _handle = handle;
                for (;;) {
                    switch (_expected) {
                        case UNLOCKED:
                            if (_context->_state.compare_exchange_weak(_expected,
                                                                       LOCKED,
                                                                       Ordering::ACQUIRE,
                                                                       Ordering::RELAXED))
                                return false;
                            break;
                        default:
                            if (_context->_state.compare_exchange_weak(_expected,
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
            for (intptr_t expected = _state.load(Ordering::RELAXED); !_awaiters;) {
                switch (expected) {
                    case UNLOCKED:
                        abort();
                    case LOCKED:
                        if (_state.compare_exchange_strong(expected, UNLOCKED, Ordering::RELEASE, Ordering::RELAXED))
                            return;
                        break;
                    default:
                        if (_state.compare_exchange_strong(expected, LOCKED, Ordering::ACQUIRE, Ordering::RELAXED)) {
                            // We could reverse the list here for fairness
                            _awaiters = (Awaitable*)expected;
                        }
                        break;
                }
            }
            assert(_awaiters);
            Awaitable* head = _awaiters;
            _awaiters = (Awaitable*)(_awaiters->_expected);
            // SAFETY: Coroutine scheduling here establishes happens-before?
            schedule_coroutine_handle(head->_handle);
            return;
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
            std::unique_lock guard(_mutex);
            if (_waiting.empty()) {
                _deque.push_back(std::move(item));
            } else {
                assert(_deque.empty());
                Awaitable* awaitable = _waiting.front();
                _waiting.pop_front();
                *(awaitable->_victim) = std::move(item);
                awaitable->_result = true;
                schedule_coroutine_handle(awaitable->_coroutine_handle);
            }
        }

        void push_front(T item) {
            std::unique_lock guard(_mutex);
            if (_waiting.empty()) {
                _deque.push_front(std::move(item));
            } else {
                assert(_deque.empty());
                Awaitable* awaitable = _waiting.front();
                _waiting.pop_front();
                *(awaitable->_victim) = std::move(item);
                awaitable->_result = true;
                schedule_coroutine_handle(awaitable->_coroutine_handle);
            }
        }
        
        bool try_pop_front(T& victim) {
            std::unique_lock guard(_mutex);
            bool result = !_deque.empty();
            if (result) {
                victim = std::move(_deque.front());
                _deque.pop_front();
            }
            return result;
        }

        bool try_pop_back(T& victim) {
            std::unique_lock guard(_mutex);
            bool result = !_deque.empty();
            if (result) {
                victim = std::move(_deque.back());
                _deque.pop_back();
            }
            return result;
        }

        void cancel() {
            std::deque<Awaitable*> waiting;
            {
                std::unique_lock guard(_mutex);
                _is_canceled = true;
                using std::swap;
                swap(waiting, _waiting);
                assert(_waiting.empty());
            }
            for (Awaitable* awaitable : waiting)
                schedule_coroutine_handle(awaitable->_coroutine_handle);
        }
        
        struct Awaitable {
            CoroutineBlockingDeque* _context;
            T* _victim;
            bool _result;
            std::coroutine_handle<> _coroutine_handle;
            
            void await_suspend(std::coroutine_handle<> handle) noexcept {
                // We still hold the mutex
                // Therefore the queue is still empty
                _coroutine_handle = handle;
                assert(!_context->_is_canceled);
                _context->_waiting.push_back(this);
                _context->_mutex.unlock();
            }
            
            bool await_resume() const noexcept {
                return _result;
            }
            
        };
        
        auto pop_front_wait(T& victim) {
            struct PopFrontAwaitable : Awaitable {
                
                bool await_ready() noexcept {
                    this->_context->_mutex.lock();
                    if (!this->_context->_deque.empty()) {
                        if (!this->_context->_is_canceled)
                            return false;
                        assert(this->_result == false);
                    } else {
                        *this->_victim = std::move(this->_context->_deque.front());
                        this->_context->_deque.pop_front();
                        this->_result = true;
                    }
                    this->_context->_mutex.unlock();
                    return true;
                }
                
            };
            return PopFrontAwaitable{{this, &victim, false}};
        };
        
        auto pop_back_wait(T& victim) {
            struct PopBackAwaitable : Awaitable {
                
                bool await_ready() noexcept {
                    this->_context->_mutex.lock();
                    if (!this->_context->_deque.empty()) {
                        if (!this->_context->_is_canceled)
                            return false;
                        assert(this->_result == false);
                    } else {
                        *this->_victim = std::move(this->_context->_deque.back());
                        this->_context->_deque.pop_back();
                        this->_result = true;
                    }
                    this->_context->_mutex.unlock();
                    return true;
                }
                
            };
            return PopBackAwaitable{{this, &victim, false}};
        };
        
    }; // CoroutineBlockingDeque
    
    
    
    
} // namespace wry::coroutine

#endif /* coroutine_hpp */
