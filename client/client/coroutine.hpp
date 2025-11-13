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


    // Global work queue
    
    void global_work_queue_schedule(std::coroutine_handle<>);
    
    void global_work_queue_service();
    void global_work_queue_cancel();
    
    // Basic functions
    
    inline std::coroutine_handle<> null_to_noop(std::coroutine_handle<> handle) {
        return handle ? handle : std::noop_coroutine();
    }

    // Basic awaitables
    
    using std::suspend_always;
    using std::suspend_never;
    
    struct suspend_and_schedule : suspend_always {
        void await_suspend(std::coroutine_handle<> handle) const noexcept {
            global_work_queue_schedule(handle);
        }
    };
    
    struct suspend_and_destroy : suspend_always {
        void await_suspend(std::coroutine_handle<> handle) const noexcept {
            handle.destroy();
        }
        void await_resume() const noexcept {
            abort();
        }
    };
    
    struct debug_suspend_and_leak : suspend_always {
        void await_suspend(std::coroutine_handle<> handle) const noexcept {}
    };

    struct co_task {
        
        struct promise_type {
            
            promise_type* _parent = nullptr;
            ptrdiff_t _children = 0;
            Atomic<ptrdiff_t> _countdown{0};
            
            ~promise_type() {
                assert(_children == 0);
            }
                        
            co_task get_return_object() {
                return co_task{this};
            }
            
            constexpr suspend_always initial_suspend() const noexcept {
                return suspend_always{};
            }
            
            void unhandled_exception() const noexcept { abort(); }
            void return_void() const noexcept {}
            
            auto final_suspend() const noexcept {
                struct awaitable : suspend_and_destroy {
                    std::coroutine_handle<> await_suspend(std::coroutine_handle<promise_type> handle) const noexcept {
                        promise_type* child = &handle.promise();
                        promise_type* parent = child->_parent;
                        if (parent) {
                            handle.destroy();
                            ptrdiff_t count = parent->_countdown.sub_fetch(1, Ordering::RELEASE);
                            if (count == 0) {
                                parent->_countdown.load(Ordering::ACQUIRE);
                                return std::coroutine_handle<promise_type>::from_promise(*parent);
                            }
                        } else {
                            ptrdiff_t count = child->_countdown.load(Ordering::RELAXED);
                            assert(count == 0);
                            child->_countdown.notify_one();
                        }
                        return std::noop_coroutine();
                    }
                };
                return awaitable{};
            }
                        
        };
        
        promise_type* _promise;
        
        co_task() = delete;
        explicit co_task(promise_type* p) : _promise(p) {}
        co_task(co_task const&) = delete;
        ~co_task() { if (_promise) abort(); }
        co_task& operator=(co_task const&) = delete;
                
        auto operator co_await() {
            struct awaitable : suspend_always {
                promise_type* _child;
                std::coroutine_handle<> await_suspend(std::coroutine_handle<promise_type> handle) noexcept {
                    promise_type* child = _child;
                    child->_parent = &(handle.promise());
                    ++(child->_parent->_children);
                    global_work_queue_schedule(handle);
                    // we can no longer use *this
                    return std::coroutine_handle<promise_type>::from_promise(*child);
                }
            };
            return awaitable{{}, std::exchange(_promise, nullptr)};
        }
        
        struct join_awaitable : suspend_always {
            std::coroutine_handle<> await_suspend(std::coroutine_handle<promise_type> handle) noexcept {
                promise_type* self = &handle.promise();
                ptrdiff_t count = self->_countdown.add_fetch(std::exchange(self->_children, 0), Ordering::ACQ_REL);
                if (count > 0) {
                    return std::noop_coroutine();
                } else if (count == 0) {
                    (void) self->_countdown.load(Ordering::ACQUIRE);
                    return handle;
                } else {
                    abort();
                }
            }
        };
        
        
        void start() {
            global_work_queue_schedule(std::coroutine_handle<promise_type>::from_promise(*_promise));
        };
        
        void join() {
            ptrdiff_t expected = _promise->_countdown.load(Ordering::RELAXED);
            while (expected) {
                _promise->_countdown.wait(expected, Ordering::RELAXED);
            }
            (void) _promise->_countdown.load(Ordering::ACQUIRE);
            _promise = nullptr;
        }
                
    };
        
    // Questionable
    
#define co_fork co_await
#define co_join co_await ::wry::coroutine::co_task::join_awaitable{};

    
    
    
    
    
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
                
                suspend_and_schedule initial_suspend() noexcept { return {}; }
                
                auto final_suspend() noexcept {
                    
                    struct Awaitable {
                        
                        SingleConsumerLatch* _context;
                        constexpr bool await_ready() const noexcept {
                            return false;
                        }
                        std::coroutine_handle<> await_suspend(std::coroutine_handle<promise_type> handle) noexcept {
                            // Save the context
                            SingleConsumerLatch* context = _context;
                            handle.destroy();
                            return null_to_noop(context->count_down_and_return_continuation(1));
                        }
                        void await_resume() noexcept {
                            abort();
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
                
                /*
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
                 */
                
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
    
        
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    

    
    
    
    // TODO: Are tasks actually useful?
    // vs write multiple results into an array and count down a latch?
    
    // Eager task returning T
    template<typename T>
    struct co_future_eager {
        
        struct promise_type {
            
            enum {
                INITIAL = 0,
                READY,
                ABANDONED,
            };
            
            Atomic<intptr_t> _state;
            union {
                char _initial;
                T _ready;
            };
            
            constexpr co_future_eager get_return_object() const noexcept {
                return co_future_eager{this};
            }
            
            constexpr std::suspend_never initial_suspend() const noexcept {
                return {};
            }
            
            void unhandled_exception() const noexcept { abort(); }
            
            void return_value(auto&& x) noexcept {
                new(&_ready) T(FORWARD(x));
            }
            
            constexpr auto final_suspend() const noexcept {
                struct awaitable {
                    constexpr bool await_ready() const noexcept {
                        return false;
                    }
                    std::coroutine_handle<> await_suspend(std::coroutine_handle<promise_type> handle) {
                        intptr_t was = handle.promise()._state.exchange(READY, Ordering::RELEASE);
                        switch (was) {
                            case INITIAL:
                                return std::noop_coroutine();
                            case READY:
                                abort();
                            case ABANDONED:
                                std::destroy(handle.promise()._ready);
                                handle.destroy();
                                return std::noop_coroutine();
                            default: // AWAITED
                                handle.promise().load(Ordering::ACQUIRE);
                                return std::coroutine_handle<>::from_address((void*)was);
                        }
                    }
                };
            }
            
        };
        
        promise_type* _promise;
        
        co_future_eager() = delete;

        co_future_eager(co_future_eager const& other) = delete;

        co_future_eager(co_future_eager&& other)
        : _promise(std::exchange(other._promise, nullptr)) {
        }
        
        ~co_future_eager() {
            if (_promise) {
                intptr_t was = _promise->_state.exchange(promise_type::ABANDONED, Ordering::RELEASE);
                switch (was) {
                    case promise_type::INITIAL:
                        // running; will destroy itself
                        break;
                    case promise_type::READY:
                        // finished; we must destroy it
                        (void) _promise->_state.load(Ordering::ACQUIRE);
                        std::destroy(_promise->_ready);
                        std::coroutine_handle<promise_type>::from_promise(*(_promise)).destroy();
                        break;
                    case promise_type::ABANDONED:
                        // disallowed; already abandoned
                        abort();
                    default:
                        // disallowed; awaited
                        abort();
                }
            }
        }
        
        co_future_eager& operator=(co_future_eager const&) = delete;
        co_future_eager& operator=(co_future_eager&& other) {
            co_future_eager local(std::move(other));
            using std::swap;
            swap(_promise, local._promise);
            return *this;
        }
        
        
        auto operator co_await() {
            struct awaitable {
                co_future_eager* _context;
                constexpr bool await_ready() const noexcept {
                    return false;
                }
                std::coroutine_handle<> await_suspend(std::coroutine_handle<> handle) const noexcept {
                    intptr_t was = _context->_promise->_state.exchange((intptr_t)handle.address(), Ordering::RELEASE);
                    switch (was) {
                        case promise_type::INITIAL:
                            return std::noop_coroutine();
                        case promise_type::READY:
                            (void) _context->_promise->_state.load(Ordering::ACQUIRE);
                            return handle;
                        case promise_type::ABANDONED:
                            // disallowed: already abandoned
                            abort();
                        default:
                            //disallowed: already awaited
                            abort();
                    }
                }
                T await_resume() const noexcept {
                    T result{std::move(_context->_promise->_ready)};
                    std::destroy(_context->_promise->_ready);
                    std::coroutine_handle<promise_type>::from_promise(*(_context->_promise)).destroy();
                    _context->_promise = nullptr;
                    return result;
                }
            };
            return awaitable{this};
        }
        
    };
    
    
    

    
    
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
            global_work_queue_schedule(head->_handle);
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

    
    
    
} // namespace wry::coroutine

#endif /* coroutine_hpp */
