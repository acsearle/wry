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

#include "atomic.hpp"
#include "utility.hpp"

namespace wry::coroutine {
    
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
    
    // We can manually construct objects with the appropriate callbacks to
    // be consumed by the thread pool as-if they are coroutines
        
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
    
    std::coroutine_handle<> coroutine_handle_from(Header* header) {
        return std::coroutine_handle<>::from_address(header);
    }
    
    void resume_by_address(void* address) {
        __builtin_coro_resume(address);
    }

    void destroy_by_address(void* address) {
        __builtin_coro_destroy(address);
    }
    
    bool is_done_by_address(void* address) {
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

    

    
    
    
    
    void schedule_coroutine_handle_from_address(void* address);
    void schedule_coroutine_handle(std::coroutine_handle<>);

    constexpr inline struct _self_promise_t {} self_promise;
    
    
    struct Latch {
        
        enum : intptr_t {
            NONSIGNALED = 0,
            SIGNALED = 1,
        };
        
        Atomic<ptrdiff_t> _count;
        Atomic<intptr_t> _continuation;
        
        // TODO: This is a bad(fragile? incorrect?) way of organizing an
        // unknown-in-advance number of jobs
        ptrdiff_t _dependents;
                
        Latch()
        : _count(0)
        , _continuation(NONSIGNALED)
        , _dependents(0) {
        }
        
        ~Latch() {
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
            if (_dependents == 0)
                // there were never any jobs to wait for
                return true;
            ptrdiff_t n = _count.add_fetch(_dependents, Ordering::RELAXED);
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
                static void* operator new(std::size_t count, Latch&, auto&&...) {
                    // TODO: call latch._service etc
                    // return arena_allocate(count);
                    return calloc(count, 1);
                }
                
                static void operator delete(void* ptr) {
                    // no-op
                    free(ptr);
                }
                
                Latch& _latch;
                promise_type() = delete;
                explicit promise_type(Latch& p, auto&&...)
                : _latch(p) {
                    // this is invoked immediately on the spawning thread and
                    // thus does not need to be atomic **for the intended use case**
                    ++(p._dependents);
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
                        Latch& _latch;
                        bool await_ready() noexcept {
                            return !(_latch._signalling_coroutine_decrement());
                        }
                        std::coroutine_handle<> await_suspend(std::coroutine_handle<promise_type> handle) noexcept {
                            // The latch has counted down to zero
                            // Copy from the coroutine frame to the stack frame
                            Latch& target = _latch;
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
                
            }; // struct Latch::WillDecrement::promise_type
            
        }; // struct Latch::WillDecrement
        
    }; // struct Latch
    
} // namespace wry::coroutine

#endif /* coroutine_hpp */
