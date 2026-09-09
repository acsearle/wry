//
//  coroutine.hpp
//  client
//
//  Created by Antony Searle on 13/8/2025.
//

#ifndef coroutine_hpp
#define coroutine_hpp

#include <coroutine>

#include <chrono>
#include <deque>
#include <exception>
#include <semaphore>
#include <thread>
#include <queue>
#include <stop_token>
#include <variant>

#include "assert.hpp"
#include "atomic.hpp"
#include "utility.hpp"
#include "mutex.hpp"
#include "variant.hpp"
#include "memory.hpp"
#include "stdint.hpp"

#include "epoch_allocator.hpp"
#include "global_work_queue.hpp"

namespace wry {

    void global_work_queue_schedule(std::coroutine_handle<>);

    void collector_register_cycle_callback(uint64_t k,
                                           void* callback) noexcept;

} // namespace wry

namespace wry::Coroutine {

#pragma mark Explicit frame header

    struct Header {
        void (*resume )(void*);
        void (*destroy)(void*);
    };

    inline void resume_by_address(void* ptr) {
        [[clang::musttail]] return ((Header*)ptr)->resume(ptr);
    }

    inline void destroy_by_address(void* ptr) {
        [[clang::musttail]] return ((Header*)ptr)->destroy(ptr);
    }

    inline void global_work_queue_schedule_destroy(void* ptr) {
        struct DestroyThunk {
            Header _header = { &_static_destroy, &_static_destroy };
            void* continuation = nullptr;
            static void _static_destroy(void* ptr) {
                auto self = (DestroyThunk*)ptr;
                void* continuation = self->continuation;
                delete self;
                [[clang::musttail]] return destroy_by_address(continuation);
            }
        };
        global_work_queue_schedule(new DestroyThunk{ .continuation = ptr });
    }

#pragma mark Helper functions

    inline std::coroutine_handle<> null_to_noop(std::coroutine_handle<> handle) {
        return handle ? handle : std::noop_coroutine();
    }

    void global_work_queue_schedule_after(std::chrono::steady_clock::time_point when, void* address);

#pragma mark Basic Awaitables

    struct ResumeNever : std::suspend_always {
        void await_resume() const noexcept {
            std::unreachable();
        }
    };
    
    struct SuspendAndDestroy : ResumeNever {
        void await_suspend(std::coroutine_handle<> handle) const noexcept {
            handle.destroy();
        }
    };
    
    struct DebugSuspendAndLeak : ResumeNever {
        void await_suspend(std::coroutine_handle<> handle) const noexcept {            
        }
    };
    
    struct TransferToPoolExecutor : std::suspend_always {
        void await_suspend(std::coroutine_handle<> handle) const noexcept {
            global_work_queue_schedule(handle);
        }
    };

    struct TransferToBlockableExecutor : std::suspend_always {
        void await_suspend(std::coroutine_handle<>) const noexcept;
    };

    struct DebugWaitForCollectionCycles {
        uint64_t cycles;
        bool await_ready() const noexcept { return cycles == 0; }
        void await_suspend(std::coroutine_handle<> handle) const noexcept {
            collector_register_cycle_callback(cycles, handle.address());
        }
        void await_resume() const noexcept {}
    };



    template<typename T>
    struct Promise;

    template<typename T = void>
    struct Outcome {

        using stored_type = std::conditional_t<std::is_void_v<T>, std::monostate, T>;
        std::variant<std::monostate, stored_type, std::exception_ptr, std::monostate> _variant;


        bool is_empty() const { return _variant.index() == 0; }
        bool has_value() const { return _variant.index() == 1; }
        bool has_error() const { return _variant.index() == 2; }
        bool is_stopped() const { return _variant.index() == 3; }

        // Awaitable
        // TODO: Use await_transform_helper instead of polluting the interface

        bool await_ready() const noexcept {
            switch (_variant.index()) {
                case 1: // value
                    return true;
                case 2: // error
                    return true;
                case 3: // stopped
                    return false;
                default:
                    // empty or valueless-by-exception
                    abort();
            }
        }

        template<typename U>
        void await_suspend(std::coroutine_handle<Promise<U>> handle) const {
            assert(_variant.index() == 3);
            handle.destroy();
        }

        // Taking the result consumes it: the outcome returns to empty, so a
        // second take aborts instead of yielding a moved-from value or a
        // null exception
        auto await_resume() {
            switch (_variant.index()) {
                case 1:
                    if constexpr (std::is_void_v<T>) {
                        _variant.template emplace<0>();
                        return;
                    } else {
                        T value = std::move(std::get<1>(_variant));
                        _variant.template emplace<0>();
                        return value;
                    }
                case 2: {
                    std::exception_ptr error = std::move(std::get<2>(_variant));
                    _variant.template emplace<0>();
                    std::rethrow_exception(std::move(error));
                }
                default:
                    // empty or valueless-by-exception
                    abort();
            }
        }


        auto value() {
            return await_resume();
        }

        auto value_or(auto&& x) {
            switch (_variant.index()) {
                case 1:
                    if constexpr (std::is_void_v<T>) {
                        _variant.template emplace<0>();
                        return;
                    } else {
                        T value = std::move(std::get<1>(_variant));
                        _variant.template emplace<0>();
                        return value;
                    }
                case 2: {
                    std::exception_ptr error = std::move(std::get<2>(_variant));
                    _variant.template emplace<0>();
                    std::rethrow_exception(std::move(error));
                }
                case 3: {
                    _variant.template emplace<0>();
                    return std::forward<decltype(x)>(x);
                }
                default:
                    // empty or valueless-by-exception
                    abort();
            }
        }

        std::optional<stored_type> stopped_as_optional() {
            switch (_variant.index()) {
                case 1: {
                    std::optional<stored_type> value{std::move(std::get<1>(_variant))};
                    _variant.template emplace<0>();
                    return value;
                }
                case 2: {
                    std::exception_ptr error = std::move(std::get<2>(_variant));
                    _variant.template emplace<0>();
                    std::rethrow_exception(std::move(error));
                }
                case 3:
                    _variant.template emplace<0>();
                    return std::nullopt;
                default:
                    abort();
            }
        }

    };

    template<typename T, typename... Args>
    void set_value(Outcome<T>& outcome, Args&&... args) {
        switch (outcome._variant.index()) {
            case 0:
                outcome._variant.template emplace<1>(std::forward<Args>(args)...);
                break;
            default:
                abort();
        }
    }

    inline void set_value(Outcome<void>& outcome) {
        switch (outcome._variant.index()) {
            case 0:
                outcome._variant.template emplace<1>();
                break;
            default:
                abort();
        }
    }

    template<typename T, typename X>
    void set_error(Outcome<T>& outcome, X&& x) {
        switch (outcome._variant.index()) {
            case 0:
                outcome._variant.template emplace<2>(std::forward<X>(x));
                break;
            default:
                abort();
        }
    }

    template<typename T>
    void set_stopped(Outcome<T>& outcome) {
        switch (outcome._variant.index()) {
            case 0:
                outcome._variant.template emplace<3>();
                break;
            default:
                abort();
        }
    }



#pragma mark Senders: concepts

    // A sender S declares SenderTraits<S>::value_type and provides
    //
    //   connect(S&&, R&&) -> operation state, a prvalue; MOVABLE UNTIL STARTED
    //   start(op&)        -> begins the operation; op must not move afterwards
    //
    // The operation completes by calling exactly one of
    //
    //   set_value(R&&, T&&)   (set_value(R&&) when T is void)
    //   set_error(R&&, std::exception_ptr&&)
    //   set_stopped(R&&)
    //
    // on its receiver, on a pinned mutator thread.  That call is the
    // operation's LAST touch of itself: the receiver may destroy the
    // operation state before the call returns.  get_stop_token(R const&) is
    // the receiver's environment; a coroutine started as a sender inherits
    // it.  Anything immovable an operation needs (a stop_callback, a
    // self-pointer) is constructed inside start, never in connect.
    //
    // co_await accepts a sender: Promise::await_transform wraps it in a
    // SenderAwaitable whose receiver resumes (value, error) or destroys
    // (stopped) the awaiting frame.  Awaitable-ness wins over sender-ness:
    // a type with await_ready or operator co_await is awaited as itself, so
    // Future stays on its symmetric-transfer path.

    template<typename S> struct SenderTraits;

    template<typename S>
    concept Sender = requires {
        typename SenderTraits<std::remove_cvref_t<S>>::value_type;
    };

    template<typename S>
    using SenderValueType = typename SenderTraits<std::remove_cvref_t<S>>::value_type;

    template<typename A>
    concept Awaitable = requires(A& a) { a.await_ready(); }
                     || requires(A& a) { a.operator co_await(); };

    template<typename OuterPromise, Sender S> struct SenderAwaitable;

#pragma mark Future

    template<typename = void> struct Delegate;
    template<typename = void> struct Promise;
    template<typename = void> struct Future;
    using Task = Future<>;

    template<typename T>
    struct Delegate {
        virtual ~Delegate() = default;
        virtual std::coroutine_handle<> final_await_suspend() noexcept = 0;
        virtual void return_value(T value) noexcept { /* discard */ };
        virtual void unhandled_exception() noexcept { abort(); }
        virtual void unhandled_stopped() noexcept { abort(); }
        virtual std::stop_token get_stop_token() noexcept { return std::stop_token{}; }
    };

    template<>
    struct Delegate<void> {
        virtual ~Delegate() = default;
        virtual std::coroutine_handle<> final_await_suspend() noexcept = 0;
        virtual void return_void() noexcept {};
        virtual void unhandled_exception() noexcept { abort(); }
        virtual void unhandled_stopped() noexcept { abort(); }
        virtual std::stop_token get_stop_token() noexcept { return std::stop_token{}; }
    };

    template<typename T>
    struct BasicPromise {

        Delegate<T>* _delegate;

        void set_delegate(Delegate<T>* delegate) {
            _delegate = delegate;
        }

        constexpr std::suspend_always initial_suspend() const noexcept {
            return std::suspend_always{};
        }

        // TODO: pass this to all delegate calls?
        // TODO: do the exchange inside the delegate call?

        void unhandled_exception() {
            Delegate<T>* delegate = std::exchange(_delegate, nullptr);
            delegate->unhandled_exception();
        }

        void unhandled_stopped() {
            Delegate<T>* delegate = std::exchange(_delegate, nullptr);
            delegate->unhandled_exception();
        }

        struct FinalAwaitable : ResumeNever {
            template<typename DerivedPromise>
            std::coroutine_handle<> await_suspend(std::coroutine_handle<DerivedPromise> handle) const noexcept {
                Delegate<T>* delegate = std::exchange(handle.promise()._delegate, nullptr);
                // TODO: do the destroy inside the delegate call?
                handle.destroy();
                return delegate->final_await_suspend();
            }
        };

        constexpr FinalAwaitable final_suspend() const noexcept {
            return FinalAwaitable{};
        }

        ~BasicPromise() {
            if (_delegate)
                std::exchange(_delegate, nullptr)->unhandled_stopped();
        }

        std::stop_token get_stop_token() const noexcept {
            return _delegate->get_stop_token();
        }

    };

    template<typename T>
    struct MixinPromise : BasicPromise<T> {
        void return_value(T value) {
            this->_delegate->return_value(std::move(value));
        }
    };

    template<>
    struct MixinPromise<void> : BasicPromise<void> {
        void return_void() {
            this->_delegate->return_void();
        }
    };


    template<typename T>
    struct Promise : MixinPromise<T> {

        Future<T> get_return_object();

        template<typename A>
        decltype(auto) await_transform(A&& a) {
            return await_transform_helper(this, std::forward<A>(a));
        }

    };


    // Identity for awaitables; a sender is wrapped in a SenderAwaitable
    // (see Senders below)
    template<typename T, typename A>
    decltype(auto) await_transform_helper(Promise<T>* promise, A&& a) {
        using D = std::remove_cvref_t<A>;
        if constexpr (Awaitable<D>) {
            return std::forward<A>(a);
        } else if constexpr (Sender<D>) {
            static_assert(std::is_rvalue_reference_v<A&&>,
                          "co_await a sender as an rvalue");
            // TODO: SenderAwaitable can profit from early access to the promise
            return SenderAwaitable<Promise<T>, D>(promise, std::move(a));
        } else {
            return std::forward<A>(a);  // the co_await will diagnose
        }
    }


    template<typename T>
    [[nodiscard]] std::stop_token get_stop_token(Promise<T> const& promise) {
        return promise._delegate->get_stop_token();
    }

    template<typename T>
    [[nodiscard]] std::stop_token get_stop_token(std::coroutine_handle<Promise<T>> handle) {
        return get_stop_token(handle.promise());
    }

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

    template<typename T>
    struct JustAwaitable {
        T _value;
        constexpr bool await_ready() const noexcept {
            return true;
        }
        void await_suspend(std::coroutine_handle<>) const noexcept {
            std::unreachable();
        }
        T await_resume() noexcept {
            return std::move(_value);
        }
    };

    struct GetPromise {};

    template<typename T>
    decltype(auto) await_transform_helper(Promise<T>* promise, GetPromise) {
        return JustAwaitable<Promise<T>*>{ promise };
    }
    struct GetStopToken {};

    template<typename T>
    decltype(auto) await_transform_helper(Promise<T>* promise, GetStopToken) {
        return JustAwaitable<std::stop_token>{ get_stop_token(*promise) };
    }

    // TODO: `co_await get_stop_token` where get_stop_token is a Niebloid used
    // as a tag.


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

    };

    template<typename T>
    Future<T> Promise<T>::get_return_object() {
        return Future<T>{this};
    }

    template<typename T>
    std::coroutine_handle<typename Future<T>::promise_type> handle_from_future(Future<T>&& future) {
        return handle_from_promise(std::exchange(future._promise, nullptr));
    }

    template<typename OuterPromise, typename T>
    struct BasicFutureAwaitable : Delegate<T> {

        using InnerPromise = Promise<T>;

        OuterPromise* _outer_promise;
        Future<T> _future;
        Outcome<T> _outcome;

        BasicFutureAwaitable(OuterPromise* outer_promise, Future<T>&& future)
        : _outer_promise(outer_promise)
        , _future(std::move(future)) {
        }

        virtual void unhandled_exception() noexcept override {
            set_error(_outcome, std::current_exception());
        }
        virtual void unhandled_stopped() noexcept override {
            set_stopped(_outcome);
            handle_from_promise(std::exchange(_outer_promise, nullptr)).destroy();
        }
        virtual std::coroutine_handle<> final_await_suspend() noexcept override {
            return handle_from_promise(_outer_promise);
        }
        virtual std::stop_token get_stop_token() noexcept override {
            using Coroutine::get_stop_token;
            return get_stop_token(*_outer_promise);
        }

        constexpr bool await_ready() const noexcept {
            return false;
        }

        std::coroutine_handle<InnerPromise> await_suspend(std::coroutine_handle<OuterPromise> continuation) noexcept {
            assert(_outer_promise == &continuation.promise());
            _future._promise->set_delegate(this);
            return handle_from_future(std::move(_future));
        }

        auto await_resume() {
            return _outcome.await_resume();
        }

    };

    template<typename OuterPromise, typename T>
    struct FutureAwaitable : BasicFutureAwaitable<OuterPromise, T> {
        using BasicFutureAwaitable<OuterPromise, T>::BasicFutureAwaitable;
        virtual void return_value(T value) noexcept override {
            set_value(this->_outcome, std::move(value));
        }
    };

    template<typename OuterPromise>
    struct FutureAwaitable<OuterPromise, void> : BasicFutureAwaitable<OuterPromise, void> {
        using BasicFutureAwaitable<OuterPromise, void>::BasicFutureAwaitable;
        virtual void return_void() noexcept override {
            set_value(this->_outcome);
        }
    };

    template<typename U, typename T>
    auto await_transform_helper(Promise<U>* promise, Future<T>&& future) {
        return FutureAwaitable<Promise<U>, T>(promise, std::move(future));
    }



#pragma mark Senders

    // ---- Just: the synchronous sender -------------------------------------

    template<typename T>
    struct Just {
        T _value;
    };

    template<typename T>
    struct SenderTraits<Just<T>> {
        using value_type = T;
    };

    template<typename T, typename R>
    struct JustOperationState {
        T _value;
        R _receiver;
    };

    template<typename T, typename R>
    auto connect(Just<T>&& sender, R&& receiver) {
        return JustOperationState<T, std::remove_cvref_t<R>>{
            std::move(sender._value), std::forward<R>(receiver)
        };
    }

    template<typename T, typename R>
    void start(JustOperationState<T, R>& op) {
        // last touch of op
        set_value(std::move(op._receiver), std::move(op._value));
    }

    // ---- Future as a sender: the operation state is a fake frame ----------
    //
    // The coroutine's continuation is the operation state itself.  Its
    // resume word is reached by final_suspend and splits value / error on the
    // exception slot; its destroy word is reached by ~Promise during
    // cancellation unwinding and is the stopped channel.

    template<typename T>
    struct SenderTraits<Future<T>> {
        using value_type = T;
        using error_type = std::exception_ptr;
    };

    template<typename T, typename R>
    struct BasicFutureOperationState : Delegate<T> {

        struct Frame {
            Header _header = { &_static_resume, &_static_destroy };
            Outcome<T> _outcome;
            Future<T> _future;
            R _receiver;
            static void _static_resume(void* ptr) {
                auto self = (BasicFutureOperationState*)ptr;
                // last touch of self on every path
                switch (self->_outcome._variant.index()) {
                    case 1:
                        if constexpr (std::is_void_v<T>) {
                            set_value(std::move(self->_receiver));
                        } else {
                            set_value(std::move(self->_receiver), std::move(std::get<1>(self->_outcome._variant)));
                        }
                        break;
                    case 2:
                        set_error(std::move(self->_receiver), std::move(std::get<2>(self->_outcome._variant)));
                        break;
                    default:
                        abort();
                }
            }

            static void _static_destroy(void* ptr) {
                auto self = (BasicFutureOperationState*)ptr;
                // last touch of self on every path
                switch (self->_outcome._variant.index()) {
                    case 3:
                        set_stopped(std::move(self->_receiver));
                        break;
                    default:
                        abort();
                }
            }

        };

        Frame _frame;
        virtual void unhandled_exception() noexcept override {
            set_error(_frame._outcome, std::current_exception());
        }
        virtual void unhandled_stopped() noexcept override {
            set_stopped(_frame._outcome);
        }
        virtual std::coroutine_handle<> final_await_suspend() noexcept override {
            return std::coroutine_handle<>::from_address(&_frame);
        }
        virtual std::stop_token get_stop_token() noexcept override {
            return get_stop_token(_frame._receiver);
        }

        template<typename R2>
        BasicFutureOperationState(Future<T>&& future, R2&& receiver)
        : _frame{Frame{std::move(future), std::forward<R>(receiver)}} {
        }

    };

    template<typename T, typename R>
    struct FutureOperationState : BasicFutureOperationState<T, R> {
        using BasicFutureOperationState<T, R>::BasicFutureOperationState;
        virtual void return_value(T value) noexcept override {
            set_value(this->_frame._outcome, std::move(value));
        }
    };

    template<typename R>
    struct FutureOperationState<void, R> : BasicFutureOperationState<void, R> {
        using BasicFutureOperationState<void, R>::BasicFutureOperationState;
        virtual void return_void() noexcept override {
            set_value(this->_frame._outcome);
        }
    };

    template<typename T, typename R>
    auto connect(Future<T>&& future, R&& receiver) {
        return FutureOperationState<T, std::remove_cvref_t<R>>(
            std::move(future),
            std::forward<R>(receiver)
        );
    }

    template<typename T, typename R>
    void start(FutureOperationState<T, R>& op) {
        Promise<T>* promise = std::exchange(op._future._promise, nullptr);
        assert(promise);
        promise->set_delegate(&op);
        // last touch of op: the coroutine may run to completion inline
        handle_from_promise(promise).resume();
    }

    // ---- Sender as an awaitable -------------------------------------------
    //
    // AwaitableBase holds everything the receiver touches and nothing that
    // depends on the operation state type, which breaks the awaitable ->
    // operation state -> receiver -> awaitable type cycle without virtuals.
    // Completions are delivered on a pinned mutator thread and continue
    // inline, like every other leaf.

    template<typename OuterPromise, typename T>
    struct AwaitableBase {
        OuterPromise* _outer_promise;
        Outcome<T> _outcome;
    };

    template<typename OuterPromise, typename T>
    struct AwaitableReceiver {
        AwaitableBase<OuterPromise, T>* _base;
    };

    template<typename OuterPromise, typename T>
    void set_value(AwaitableReceiver<OuterPromise, T>&& receiver, T&& value) {
        set_value(receiver._base->_outcome, std::move(value));
        handle_from_promise(*std::exchange(receiver._base->_outer_promise, nullptr)).resume();

    }
    template<typename OuterPromise>
    void set_value(AwaitableReceiver<OuterPromise, void>&& receiver) {
        set_value(receiver._base->_outcome);
        handle_from_promise(*std::exchange(receiver._base->_outer_promise, nullptr)).resume();
    }

    template<typename OuterPromise, typename T>
    void set_error(AwaitableReceiver<OuterPromise, T>&& receiver, std::exception_ptr&& error) {
        set_error(receiver._base->_outcome, std::move(error));
        handle_from_promise(*std::exchange(receiver._base->_outer_promise, nullptr)).resume();
    }

    template<typename OuterPromise, typename T>
    void set_stopped(AwaitableReceiver<OuterPromise, T>&& receiver) {
        set_stopped(receiver._base->_outcome);
        handle_from_promise(*std::exchange(receiver._base->_outer_promise, nullptr)).destroy();
    }

    template<typename OuterPromise, typename T>
    std::stop_token get_stop_token(AwaitableReceiver<OuterPromise, T> const& receiver) {
        return get_stop_token(*(receiver._base->_outer_promise));
    }

    template<typename OuterPromise, Sender S>
    struct SenderAwaitable : AwaitableBase<OuterPromise, SenderValueType<S>> {

        using T = SenderValueType<S>;
        using R = AwaitableReceiver<OuterPromise, T>;
        using OperationState = decltype(connect(std::declval<S&&>(), std::declval<R&&>()));

        // The sender waits here until await_suspend, where this object has
        // its final address and the receiver may point at it
        std::variant<S, OperationState> _state;

        explicit SenderAwaitable(OuterPromise* promise, S&& sender)
        : AwaitableBase<OuterPromise, SenderValueType<S>>{promise}
        , _state(std::in_place_index<0>, std::move(sender)) {
        }

        SenderAwaitable(SenderAwaitable const&) = delete;

        constexpr bool await_ready() const noexcept { return false; }

        void await_suspend(std::coroutine_handle<OuterPromise> continuation) {
            assert(this->_outer_promise == &continuation.promise());
            S sender = std::move(std::get<0>(_state));
            OperationState& op = _state.template emplace<1>(connect(std::move(sender), R{this}));
            start(op);
            // No *this past here: the operation may have completed inline,
            // resuming (and perhaps finishing) the coroutine that owns *this
        }

        auto await_resume() {
            assert(!this->_outer_promise);
            return this->_outcome.await_resume();
        }

    };

#pragma mark Nursery / counted_scope

    struct Nursery {

        Header _header = { &_static_resume, &_static_destroy };
        Atomic<std::ptrdiff_t> _counter{0};
        Atomic<std::ptrdiff_t> _cancelled_count{0};
        std::ptrdiff_t _children = 0;
        std::coroutine_handle<> _continuation;
        std::stop_token _outer_stop_token;
        std::stop_source _inner_stop_source;

        struct Inner : Delegate<> {

            Nursery* _self;

            virtual void return_void() noexcept override {}
            virtual void unhandled_exception() noexcept override { abort(); }
            virtual void unhandled_stopped() noexcept override { _static_destroy(_self); }
            virtual std::stop_token get_stop_token() noexcept override { return _self->_inner_stop_source.get_token(); }
            virtual std::coroutine_handle<> final_await_suspend() noexcept override {
                return std::coroutine_handle<>::from_address(_self);
            }

        };
        Inner _inner;


        struct Callback {
            std::stop_source _inner_stop_source;
            void operator()() {
                // A child parked in a cancelable leaf (recv_some) unwinds
                // synchronously inside this request_stop; the last child's
                // retire can destroy the joiner frame -- this Nursery, this
                // functor, and _inner_stop_source with it -- and free the
                // inner stop state under the iteration.  Pin it on the
                // native stack for the call (cf. WithDeadlineAwaitable).
                std::stop_source keepalive = _inner_stop_source;
                keepalive.request_stop();
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
                    [[clang::musttail]] return resume_by_address(ptr);
                } else {
                    [[clang::musttail]] return destroy_by_address(ptr);
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

        template<typename T>
        [[nodiscard]] auto fork(Future<>&& future) {

        };


        // co_await nursery.fork(foo(x)) immediately starts foo on the current
        // thread and schedules the caller to execute soon.  No target: foo's
        // value, if any, is discarded and an unhandled exception aborts (see
        // BasicReturnChannel)
        [[nodiscard]] auto fork(Future<>&& future) {
            struct Awaitable : std::suspend_always {
                Nursery* _nursery;
                Future<>::promise_type* _promise;
                std::coroutine_handle<Future<>::promise_type> await_suspend(std::coroutine_handle<> continuation) noexcept {
                    ++(_nursery->_children);
                    _promise->set_delegate(&_nursery->_inner);
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
        [[nodiscard]] auto fork(Outcome<T>& target, Future<T>&& future) {
//            struct Awaitable : std::suspend_always {
//                Nursery* _nursery;
//                Outcome<T>* _target;
//                Future<T>::promise_type* _promise;
//                std::coroutine_handle<typename Future<T>::promise_type> await_suspend(std::coroutine_handle<> continuation) noexcept {
//                    ++(_nursery->_children);
//                    _promise->set_delegate(&_nursery->_inner);
//                    auto handle = handle_from_promise(std::exchange(_promise, nullptr));
//                    global_work_queue_schedule(std::move(continuation));
//                    return handle;
//                }
//                ~Awaitable() {
//                    assert(!_promise);
//                }
//            };
//            return Awaitable{{}, this, &target, std::exchange(future._promise, nullptr)};
            return fork([](Outcome<T>& target, Future<T>&& future) -> Future<> {
                // target = co_await std::move(future);
                set_value(target, co_await std::move(future));
            } (target, std::move(future)));
        }

        // nursery.soon(foo(x)) schedules foo to execute soon and continues
        // the calling context normally.  The calling context does not have to
        // be a coroutine.  No target: see fork(Future<>&&)
        void soon(Future<>&& future) {
            ++_children;
            // future._promise->set_continuation(this);
            // future._promise->set_stop_token(_inner_stop_source.get_token());
            future._promise->set_delegate(&_inner);
            global_work_queue_schedule(handle_from_future(std::move(future)));
        }

        // nursery.soon(y, bar(x)) schedules bar to execute soon and
        // continues the calling context normally.  The calling context does
        // not have to be a coroutine.  When bar completes, it assigns to y;
        // it is racy to access y until the nursery has been joined
        template<typename T>
        void soon(Outcome<T>& target, Future<T>&& future) {
            //_children++;
            //future._promise->set_continuation(this);
            //future._promise->set_target(&target);
            //future._promise->set_stop_token(_inner_stop_source.get_token());
            //global_work_queue_schedule(handle_from_future(std::move(future)));
            return soon([](Outcome<T>& target, Future<T>&& future) -> Future<> {
                // target = co_await std::move(future);
                set_value(target, co_await std::move(future));
            } (target, std::move(future)));
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
            auto continuation = std::exchange(_pop_continuation, nullptr);
            if (continuation)
                global_work_queue_schedule(continuation);
        }

        struct PopAwaitable {

            struct Callback {
                SingleProducerSingleConsumerQueue* _context;
                void operator()() {
                    std::coroutine_handle<> continuation;
                    {
                        std::unique_lock guard{_context->_mutex};
                        continuation = std::exchange(_context->_pop_continuation, nullptr);
                    }
                    if (continuation)
                        continuation.destroy();
                }
            };

            SingleProducerSingleConsumerQueue* _context;
            std::optional<std::stop_callback<Callback>> _stop_callback;

            constexpr bool await_ready() const noexcept {
                return false;
            }

            template<typename OuterPromise>
            std::coroutine_handle<> await_suspend(std::coroutine_handle<OuterPromise> continuation) {

                auto stop_token = get_stop_token(continuation);
                _stop_callback.emplace(stop_token, Callback{_context});
                {
                    std::unique_lock guard{_context->_mutex};
                    assert(!_context->_pop_continuation); // single consumer violated
                    if (!stop_token.stop_requested()) {
                        if (_context->_queue.empty()) {
                            _context->_pop_continuation = continuation;
                            return std::noop_coroutine();
                        }
                        return continuation;
                    }
                }
                continuation.destroy();
                return std::noop_coroutine();
            }

            T await_resume() {
                assert(_context);
                std::unique_lock guard{_context->_mutex};
                assert(!_context->_queue.empty());
                T result{std::move(_context->_queue.front())};
                _context->_queue.pop();
                return result;
            }
        };

        [[nodiscard]] auto pop() {
            return PopAwaitable{this};
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



    template<typename T>
    struct BasicFromStopped {

        void (*_resume)(void*) = &_static_resume;
        void (*_destroy)(void*) = &_static_destroy;
        Promise<T>* _inner_promise;
        std::coroutine_handle<> _continuation = nullptr;
        Outcome<T> _outcome;

        static void _static_resume(void* ptr) {
            auto self = (BasicFromStopped*)ptr;
            // Normal flow ==> tail call the continuation
            [[clang::musttail]] return resume_by_address(std::exchange(self->_continuation, nullptr).address());
        }

        static void _static_destroy(void* ptr) {
            auto self = (BasicFromStopped*)ptr;
            // The inner promise recorded STOPPED in _outcome before destroying
            // us (its continuation); this word only continues
            assert(self->_outcome.is_stopped());
            // Stack unwinding, we are in nested destructors ==> schedule the continuation
            global_work_queue_schedule(std::exchange(self->_continuation, nullptr));
        }

        explicit BasicFromStopped(Future<T>&& future)
        : _inner_promise{std::exchange(future._promise, nullptr)} {
        }

        BasicFromStopped(BasicFromStopped const&) = delete;
        BasicFromStopped(BasicFromStopped&&) = delete;

        ~BasicFromStopped() {
            if (_inner_promise) {
                abort();
            }
        }

        constexpr bool await_ready() const noexcept {
            return false;
        }

        template<typename OuterPromise>
        std::coroutine_handle<Promise<T>> _await_suspend(std::coroutine_handle<OuterPromise> continuation) {
            _continuation = continuation;
            _inner_promise->set_continuation(this);
            _inner_promise->set_target(&_outcome);
            return std::coroutine_handle<Promise<T>>::from_promise(*std::exchange(_inner_promise, nullptr));
        }

        std::optional<T> await_resume() {
            return _outcome.await_resume_stopped_as_optional();
        }

    }; // OptionalFromStopped

    template<typename T>
    struct OptionalFromStopped : BasicFromStopped<T> {

        explicit OptionalFromStopped(Future<T>&& future) : BasicFromStopped<T>(std::move(future)) {}

        template<typename OuterPromise>
        std::coroutine_handle<Promise<T>> await_suspend(std::coroutine_handle<OuterPromise> continuation) {
            this->_inner_promise->set_stop_token(get_stop_token(continuation));
            return this->_await_suspend(std::move(continuation));
        }

    };

    template<typename T>
    struct Shield : BasicFromStopped<T> {

        std::stop_source _stop_source;

        explicit Shield(Future<T>&& future) : BasicFromStopped<T>(std::move(future)) {}

        template<typename OuterPromise>
        std::coroutine_handle<Promise<T>> await_suspend(std::coroutine_handle<OuterPromise> continuation) {
            this->_inner_promise->set_stop_token(_stop_source.get_token());
            return this->_await_suspend(std::move(continuation));
        }
    };


    template<typename T>
    auto stopped_as_optional(Future<T>&& future) {
        return OptionalFromStopped<T>{std::move(future)};
    }

    template<typename T>
    auto shield(Future<T>&& future) {
        return Shield<T>{std::move(future)};
    }



    struct Race : Nursery {

        Atomic<std::ptrdiff_t> _winner{-1};

        bool claim(std::ptrdiff_t index) {
            std::ptrdiff_t expected = -1;
            if (!_winner.compare_exchange_strong_release_relaxed(expected, index))
                return false;
            this->request_stop();            // hasten the losers
            return true;
        }

    };


    template<typename T>
    Coroutine::Task race_entry(Race* race, std::ptrdiff_t index,
                               std::optional<T>* target, Future<T> inner) {
        T value = co_await std::move(inner);    // cancelled while parked -> this
                                                // frame unwinds, nursery tallies
        if (race->claim(index))
            *target = std::move(value);
        // a tie-loser falls through: its value is destroyed right here
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
