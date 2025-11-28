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

#include "atomic.hpp"
#include "utility.hpp"

#include "epoch_allocator.hpp"
#include "mutex.hpp"

#include "global_work_queue.hpp"

namespace wry {

    void global_work_queue_schedule(std::coroutine_handle<>);

}

namespace wry::coroutine {
    
    
    
    template<typename... Args>
    struct receiver_of {
        virtual void set_value(Args...) = 0;
    };
    
    
    template<typename T>
    struct sender_traits {
    };
    
    template<typename T>
    using sender_traits_t = typename sender_traits<T>::type;


    
    template<typename...>
    struct _coroutine_handle_receiver;
    
    template<>
    struct _coroutine_handle_receiver<> {
        std::coroutine_handle<> _handle;
        void set_value() {
            _handle.resume();
        }
    };
    
    template<typename T>
    struct _coroutine_handle_receiver<T> {
        std::coroutine_handle<> _handle;
        T* _value;
        void set_value(T value) {
            *_value = value;
            _handle.resume();
        }
    };
    
    template<typename...>
    struct _co_sender_promise;
    
    template<typename... Args>
    struct co_sender {
        using promise_type = _co_sender_promise<Args...>;
        promise_type* _promise;
        template<typename Receiver> auto connect(Receiver receiver);
    };
    
    
    template<typename Sender>
    auto _common_await_transform(Sender sender) {
        using T = sender_traits_t<Sender>;
        using U = decltype(std::move(sender).connect());
        struct awaitable {
            Sender _sender;
            T _value;
            U _operation;
            constexpr bool await_ready() const noexcept { return false; }
            void await_suspend(std::coroutine_handle<> handle) {
                _operation = std::move(_sender).connect(_coroutine_handle_receiver<T>{std::move(handle), &_value});
                _operation.start();
            }
            T await_resume() {
                return std::move(_value);
            }
        };
        return awaitable{std::move(sender)};
    }
    
    auto _common_await_transform(co_sender<> sender);
    
    template<typename T>
    auto _common_await_transform(co_sender<T> sender) {
        struct awaitable : receiver_of<T> {
            std::coroutine_handle<> _handle;
            T _value;
            explicit awaitable(std::coroutine_handle<> handle)
            : _handle(std::move(handle)) {
            }
            constexpr bool await_ready() const noexcept { return false; }
            std::coroutine_handle<> await_suspend(std::coroutine_handle<> handle) noexcept {
                std::coroutine_handle<typename co_sender<T>::promise_type>::from_address(_handle.address()).promise()._receiver = this;
                return std::exchange(_handle, handle);
            }
            T await_resume() {
                return std::move(_value);
            }
            virtual void set_value(T value) override {
                _value = std::move(value);
            }
        };
        return awaitable{std::coroutine_handle<typename co_sender<T>::promise_type>::from_promise(*sender._promise)};

    }
    
    
    template<>
    struct _co_sender_promise<> {
        receiver_of<>* _receiver = nullptr;
        co_sender<> get_return_object() { return co_sender<>{this}; }
        auto initial_suspend() noexcept { return std::suspend_always{}; }
        void return_void() noexcept {}
        void unhandled_exception() noexcept { abort(); }
        
        auto final_suspend() noexcept {
            struct awaitable {
                constexpr bool await_ready() const noexcept { return false; }
                void await_suspend(std::coroutine_handle<_co_sender_promise<>> handle) noexcept {
                    receiver_of<>* receiver = handle.promise()._receiver;
                    handle.destroy();
                    receiver->set_value();
                }
                void await_resume() const noexcept { abort(); }
            };
            return awaitable{};
        }
        template<typename Sender>
        auto await_transform(Sender sender) {
            return _common_await_transform(std::move(sender));
        }
    };
    
    
    template<typename T>
    struct _co_sender_promise<T> {
        receiver_of<T>* _receiver = nullptr;
        T _value;
        co_sender<T> get_return_object() { return co_sender<T>{this}; }
        auto initial_suspend() noexcept { return std::suspend_always{}; }
        void return_value(auto&& expr) noexcept {
            _value = std::forward<decltype(expr)>(expr);
        }
        void unhandled_exception() noexcept { abort(); }
        auto final_suspend() noexcept {
            struct awaitable {
                constexpr bool await_ready() const noexcept { return false; }
                void await_suspend(std::coroutine_handle<_co_sender_promise> handle) noexcept {
                    T value = std::move(handle.promise()._value);
                    receiver_of<T>* receiver = handle.promise()._receiver;
                    handle.destroy();
                    receiver->set_value(std::move(value));
                }
                void await_resume() const noexcept { abort(); }
            };
            return awaitable{};
        }
        template<typename Sender>
        auto await_transform(Sender sender) {
            return _common_await_transform(std::move(sender));
        }
    };
    
    
    
    template<typename...>
    struct _co_sender_operation;
    
    template<typename T, typename Receiver>
    struct _co_sender_operation<T, Receiver> : receiver_of<T> {
        co_sender<T> _sender;
        Receiver _receiver;
        _co_sender_operation(co_sender<T> sender, Receiver receiver)
        : _sender(std::move(sender))
        , _receiver(std::move(receiver)) {
        }
        void start() {
            _sender._promise->_receiver = this;
            std::coroutine_handle<typename co_sender<T>::promise_type>::from_promise(*_sender._promise).resume();
        }
        virtual void set_value(T value) override {
            _receiver.set_value(std::move(value));
        }
    };
    
    template<typename Receiver>
    struct _co_sender_operation<Receiver> : receiver_of<> {
        co_sender<> _sender;
        Receiver _receiver;
        _co_sender_operation(co_sender<> sender, Receiver receiver)
        : _sender(std::move(sender))
        , _receiver(std::move(receiver)) {
        }
        void start() {
            _sender._promise->_receiver = this;
            std::coroutine_handle<typename co_sender<>::promise_type>::from_promise(*_sender._promise).resume();
        }
        virtual void set_value() override {
            _receiver.set_value();
        }
    };
    
    
    template<typename... Args> template<typename Receiver>
    auto co_sender<Args...>::connect(Receiver receiver) {
        return _co_sender_operation<Args..., Receiver>{
            std::move(*this),
            std::move(receiver)
        };
    }
    
    
   
    inline auto _common_await_transform(co_sender<> sender) {
        struct awaitable : receiver_of<> {
            std::coroutine_handle<> _handle;
            explicit awaitable(std::coroutine_handle<> handle) : _handle(handle) {}
            constexpr bool await_ready() const noexcept { return false; }
            std::coroutine_handle<> await_suspend(std::coroutine_handle<> handle) noexcept {
                std::coroutine_handle<typename co_sender<>::promise_type>::from_address(_handle.address()).promise()._receiver = this;
                return std::exchange(_handle, handle);
            }
            void await_resume() const noexcept {}
            virtual void set_value() {
                _handle.resume();
            }
        };
        return awaitable{std::coroutine_handle<co_sender<>::promise_type>::from_promise(*sender._promise)};
    }
    
    
    
    
    
    
    
    
        
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
    
    // we want to:
    //
    // co_fork foo();
    // ...
    // co_join;
    //
    // x = co_await foo();
    //
    // x = sync_wait foo();
    //
    // in the first case:
    // - the caller suspends and reschedules itself, and informs callee of
    //   something before starting it
    // - the callee must count down something and signal the join
    // - the caller must eventually wait at the join
    //
    // in the second case:
    // - the caller suspends and installs itself in the callee as a continuation
    //
    // in the third case:
    // - the callee schedules itself and the caller thread blocks on a future-type thing
    //
    // from the callee perspective, all of these can be accomplished by resuming
    // a continuation given before starting
    //
    // when directly awaiting, we can manufacture this coroutine easily enough
    //
    // when forking, this coroutine needs to be faked somehow, and its state
    // needs to live somewhere
    //
    // in an object:
    // for (_fork_state_t _fork_state; _fork_state.is_finished(); co_await _fork_state.join()) {
    //    co_await _fork_state_ % stuff();
    // }
    //
    // in the body, a fauxroutine of:
    //     _resume
    //     _destroy = null
    //     _atomic_count
    //     _self_continuation
    //     _forked_count
    //
    //
    // co_fork boo() = co_await _fork % boo()
    // _fork % co_task -> {
    //     await_ready() -> false
    //     std::coroutine_handle<> await_suspend(std::coroutine_handle<promise_type> handle) {
    //         context = handle.promise()
    //
    //     }
    // }
    
    // a coroutine that is a sender:
    //
    //
    
    struct co_task {
        
        struct promise_type {
            // void (*_co_resume)(void*);
            // std::coroutine_handle<> _final_continuation;
            // std::coroutine_handle<> _self_continution;

            promise_type* _parent = nullptr;
            Atomic<ptrdiff_t> _countdown{0};
            ptrdiff_t _children = 0;

            
            
            
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

        explicit co_task(promise_type* p) : _promise(p) {}

        co_task() = delete;
        co_task(co_task const&) = delete;
        co_task(co_task&& other) : _promise(exchange(other._promise, nullptr)) {}
        ~co_task() { if (_promise) abort(); }
        co_task& operator=(co_task const&) = delete;
        co_task& operator=(co_task&& other) {
            co_task a(std::move(other));
            using std::swap;
            swap(_promise, a._promise);
            return *this;
        }

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
        
        
        decltype(auto) start(this auto&& self) {
            global_work_queue_schedule(std::coroutine_handle<promise_type>::from_promise(*self._promise));
            return FORWARD(self);
        };
        
        decltype(auto) join(this auto&& self) {
            ptrdiff_t expected = self._promise->_countdown.load(Ordering::RELAXED);
            while (expected) {
                self._promise->_countdown.wait(expected, Ordering::RELAXED);
            }
            (void) self._promise->_countdown.load(Ordering::ACQUIRE);
            self._promise = nullptr;
            return FORWARD(self);
        }
                
    };
        
    // Questionable
    
#define co_fork co_await
#define co_join co_await ::wry::coroutine::co_task::join_awaitable{};

    
    struct flow;
    template<typename T> struct co_future;
    
    // Another attempt:
    
    struct Flow {
        
        void (*_resume)(void*);
        ptrdiff_t _forks;
        Atomic<ptrdiff_t> _count;
        Atomic<uintptr_t> _continuation;
        
        static void _fork_action(void* address) {
            Flow* self = (Flow*)address;
            if (self->_count.sub_fetch(1, Ordering::RELEASE) == 0) {
                (void) self->_count.load(Ordering::ACQUIRE);
                uintptr_t was = self->_continuation.exchange(0, Ordering::ACQUIRE);
                if (was) {
                    auto p = (void (**)(void*))(was);
                    [[clang::musttail]] return (*p)(p);
                }
                return;
            }
        }
        
        Flow()
        : _resume{&_fork_action}
        , _forks{0}
        , _count{0}
        , _continuation{0}
        {}
        
        ~Flow() {
            assert(_forks == 0);
        }
        
        // co_await flow.fork(foo())
        
        
        template<typename T>
        struct PendingFork {
            Flow* _flow;
            co_future<T> _future;
        };
        
        template<typename T>
        PendingFork<T> fork(co_future<T>&& x) {
            return PendingFork<T>{ this, std::move(x) };
        }
        
        
        
        
        //
        //        // when joining
        //
        //        bool await_ready() noexcept {
        //            return false;
        //        }
        //
        //        bool await_suspend(std::coroutine_handle<> continuation) noexcept {
        //            _continuation.store((uintptr_t)(continuation.address()), Ordering::RELAXED);
        //            auto count = _count.add_fetch(exchange(_forks, 0), Ordering::RELEASE);
        //            if (count == 0) {
        //                // forks have already finished, unpublish the continuation and
        //                // abort the suspension
        //                _continuation.store(0, Ordering::RELAXED);
        //                return false; // abort suspension and continue
        //            } else {
        //                return true; // suspend
        //            }
        //        }
        //
        //        void await_resume() {
        //        }
        //
        //        // when forking something
        //
        //        // co_await my_flow << foo();
        //        // co_fork(my_flow) foo();
        //
        //        auto operator<<(auto&& other) {
        //            struct pending_fork {
        //                flow* _context;
        //                decltype(other.operator co_await()) _inner;
        //                bool await_ready() noexcept {
        //                    assert(!_inner.await_ready());
        //                    return false;
        //                }
        //                std::coroutine_handle<> await_suspend(std::coroutine_handle<> outer) noexcept {
        //                    ++(_context->_forks);
        //                    // set the countdown as the continuation of the fork
        //                    // send the outer coroutine to the work queue to be continued soon
        //                    // start the fork on this thread
        //                    std::coroutine_handle handle = _inner.await_suspend(std::coroutine_handle<>::from_address(_context));
        //                    global_work_queue_schedule(outer);
        //                    return handle;
        //                }
        //            };
        //            return pending_fork{this, FORWARD(other)};
        //        }
        
        
        
    };
    
    
    
    
    template<typename T>
    struct co_future {
        struct promise_type {
            std::coroutine_handle<> _continuation;
            std::variant<std::monostate, T, std::exception_ptr> _value;
            co_future get_return_object() { return co_future{this}; }
            std::coroutine_handle<promise_type> get_handle() {
                return std::coroutine_handle<promise_type>::from_promise(*this);
                
            }
            void set_continuation(std::coroutine_handle<> continuation) noexcept {
                _continuation = std::move(continuation);
            }
            T get_result() {
                using std::get;
                switch (_value.index()) {
                    case 1:
                        return std::move(get<1>(_value));
                    case 2:
                        std::rethrow_exception(std::move(get<2>(_value)));
                    default:
                        abort();
                }
            }
            auto initial_suspend() noexcept { return suspend_always{}; }
            void return_value(auto&& expr) { _value.template emplace<1>(FORWARD(expr)); }
            void unhandled_exception() noexcept { _value.template emplace<2>(std::current_exception()); }
            auto final_suspend() noexcept {
                struct awaitable : suspend_always {
                    std::coroutine_handle<> await_suspend(std::coroutine_handle<promise_type> self) noexcept {
                        return self.promise()._continuation;
                    }
                };
                return awaitable{};
            }
            
            template<typename U>
            auto await_transform(co_future<U>&& right) {
                struct awaitable : suspend_always {
                    co_future<U>::promise_type* _right;
                    auto await_suspend(std::coroutine_handle<promise_type> handle) noexcept {
                        _right->set_continuation(std::move(handle));
                        return _right->get_handle();
                    }
                    T await_resume() const { return _right->get_result(); }
                    // the co_future destroys the handle
                };
                return awaitable{{}, exchange(right._promise, nullptr)};
            }
            
            template<typename U>
            auto await_transform(Flow::PendingFork<U>&& right) {
                struct awaitable : suspend_always {
                    Flow::PendingFork<U> _right;
                    promise_type* _left;
                    auto await_suspend(std::coroutine_handle<promise_type> handle) noexcept {
                        ++(_right._flow->_forks);
                        _right._future._promise->set_continuation(std::coroutine_handle<>::from_address(_right._flow));
                        auto result = _right._future._promise->get_handle();
                        _right._future = nullptr;
                        global_work_queue_schedule(handle);
                        return result;
                    }
                    // resumption
                };
                return awaitable{{}, std::move(right), this};
            }
            
        };
        promise_type* _promise;
        co_future() : _promise(nullptr) {}
        explicit co_future(promise_type* promise) : _promise(promise) {}
        co_future(co_future const&) = delete;
        co_future(co_future&& other) : _promise(std::exchange(other._promise, nullptr)) {}
        ~co_future() {
            if (_promise)
                _promise->get_handle().destroy();
        }
        co_future& operator=(co_future const&) = delete;
        co_future& operator=(co_future&& other) {
            co_future temporary{std::move(other)};
            using std::swap;
            swap(_promise, temporary._promise);
            return *this;
        }
    };
        





    
    
    
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
