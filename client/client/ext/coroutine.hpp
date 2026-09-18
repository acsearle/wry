//
//  coroutine.hpp
//  client
//
//  Created by Antony Searle on 13/8/2025.
//

#ifndef coroutine_hpp
#define coroutine_hpp

#include <cstdio>

#include <chrono>
#include <coroutine>
#include <deque>
#include <exception>
#include <optional>
#include <queue>
#include <semaphore>
#include <stop_token>
#include <thread>
#include <tuple>
#include <typeinfo>

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

#pragma mark Senders: concepts

    // A sender S declares SenderTraits<S>::value_type and provides
    //
    //   connect(S&&, R&&) -> operation state, a prvalue elided into its home
    //   start(op&)        -> begins the operation
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
    // it.  Operation states are IMMOVABLE: one is constructed where it will
    // live (an awaitable, a sync_wait local, a slot in a parent operation
    // state), so connect may build self-referential structure at once, such
    // as child operation states whose receivers point at the parent.
    // Registering a stop callback is a side effect (an already-requested
    // token fires it immediately) and belongs in start.
    //
    // co_await accepts a sender: Promise::await_transform wraps it in a
    // SenderAwaitable whose receiver resumes (value, error) or destroys
    // (stopped) the awaiting frame.  transform_await (see Promise) is
    // consulted first, which is how Future keeps its symmetric-transfer path.

    template<typename S> struct SenderTraits;

    template<typename S>
    concept Sender = requires {
        typename SenderTraits<std::remove_cvref_t<S>>::value_type;
    };

    template<typename S>
    using SenderValueType = typename SenderTraits<std::remove_cvref_t<S>>::value_type;

    // Awaitable as the language sees it, for A's own value category:
    // await_ready and friends, or a member or non-member operator co_await
    template<typename A>
    concept Awaitable = requires(A&& a) { std::forward<A>(a).await_ready(); }
                     || requires(A&& a) { std::forward<A>(a).operator co_await(); }
                     || requires(A&& a) { operator co_await(std::forward<A>(a)); };

    template<typename OuterPromise, Sender S> struct SenderAwaitable;


#pragma mark Coroutine punning

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


#pragma mark Type-erased awaitables

    template<typename = void> struct Delegate;
    template<typename = void> struct Promise;
    template<typename = void> struct Future;
    using Task = Future<>;


#pragma mark Reified operation result

    template<typename T = void>
    struct Outcome {

        using stored_type = std::conditional_t<std::is_void_v<T>, std::monostate, T>;
        std::variant<std::monostate, stored_type, std::exception_ptr, std::monostate> _variant;

        bool is_empty() const { return _variant.index() == 0; }
        bool has_value() const { return _variant.index() == 1; }
        bool has_error() const { return _variant.index() == 2; }
        bool is_stopped() const { return _variant.index() == 3; }

        // Taking the result consumes it: the outcome returns to empty, so a
        // second take aborts instead of yielding a moved-from value or a
        // null exception
        auto value() {
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

        auto value_or(auto&& x) requires (!std::is_void_v<T>) {
            switch (_variant.index()) {
                case 1: {
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

    // co_await outcome: a value returns, an error rethrows, stopped destroys
    // the awaiting frame (the cancellation continues through it).  An
    // Outcome is not an awaitable in its own right; a Promise reaches this
    // through transform_await, for an lvalue or an rvalue outcome
    template<typename T>
    struct OutcomeAwaitable {
        Outcome<T>* _outcome;
        bool await_ready() const noexcept {
            switch (_outcome->_variant.index()) {
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
        void await_suspend(std::coroutine_handle<> handle) const noexcept {
            assert(_outcome->_variant.index() == 3);
            handle.destroy();
        }
        auto await_resume() {
            return _outcome->value();
        }
    };

    template<typename U, typename T>
    OutcomeAwaitable<T> transform_await(Promise<U>*, Outcome<T>& outcome) {
        return OutcomeAwaitable<T>{&outcome};
    }

    template<typename U, typename T>
    OutcomeAwaitable<T> transform_await(Promise<U>*, Outcome<T>&& outcome) {
        return OutcomeAwaitable<T>{&outcome};
    }

    // As a sink.  These are the verbs a receiver that targets an Outcome
    // calls (SyncWaitReceiver, a nursery child's delegate); an Outcome is
    // not itself a receiver, since completion consumes a receiver and an
    // Outcome must outlive completion to be read

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

#pragma mark Future

    // A delegate is never owned through this interface -- it lives in an
    // awaitable, an operation state, a static, or a promise's parking space
    // -- so there is deliberately no virtual destructor: a parked delegate
    // must be trivially destructible (see BasicPromise::emplace_delegate)
    template<typename T>
    struct Delegate {
        virtual std::coroutine_handle<> final_await_suspend() noexcept = 0;
        virtual void return_value(T value) noexcept { /* discard */ };
        virtual void unhandled_exception() noexcept { abort(); }
        virtual void unhandled_stopped() noexcept { abort(); }
        virtual std::stop_token get_stop_token() noexcept { return std::stop_token{}; }
    };

    template<>
    struct Delegate<void> {
        virtual std::coroutine_handle<> final_await_suspend() noexcept = 0;
        virtual void return_void() noexcept {};
        virtual void unhandled_exception() noexcept { abort(); }
        virtual void unhandled_stopped() noexcept { abort(); }
        virtual std::stop_token get_stop_token() noexcept { return std::stop_token{}; }
    };

    // TODO: Should Delegate take Promise<T>* as an argument?  Is that enough
    // to allow us to not hold a Promise<T>* in the delegate?

    // The value and error channels of a delegate that completes into an
    // Outcome<T>.  Such delegates differ between T and void only in the name
    // of the value hook, return_value(T) or return_void(); this mixin spells
    // both as set_value on the outcome that Derived names through outcome()
    // -- owned, held in a frame, or pointed at, the mixin does not care, and
    // adds no state.  The stopped hook stays with Derived: it is the destroy
    // word, and what it must continue or forward to differs per delegate.
    //
    // Derived is the class that provides outcome(); it is incomplete while
    // this base is instantiated, and complete by the time the virtuals are
    template<typename Derived, typename T>
    struct BasicOutcomeDelegate : Delegate<T> {
        Outcome<T>& _outcome_of_derived() noexcept {
            return static_cast<Derived*>(this)->outcome();
        }
        virtual void unhandled_exception() noexcept override {
            set_error(_outcome_of_derived(), std::current_exception());
        }
    };

    template<typename Derived, typename T>
    struct OutcomeDelegate : BasicOutcomeDelegate<Derived, T> {
        virtual void return_value(T value) noexcept override {
            set_value(this->_outcome_of_derived(), std::move(value));
        }
    };

    template<typename Derived>
    struct OutcomeDelegate<Derived, void> : BasicOutcomeDelegate<Derived, void> {
        virtual void return_void() noexcept override {
            set_value(this->_outcome_of_derived());
        }
    };

    template<typename T>
    struct BasicPromise {

        Delegate<T>* _delegate = nullptr;

        // Parking space for a delegate with no other home of the child's
        // lifetime (a nursery child's, see NurseryChildDelegate)
        alignas(void*) std::byte _delegate_storage[3 * sizeof(void*)];

        void set_delegate(Delegate<T>* delegate) {
#ifndef NDEBUG
            if (_delegate) {
                // A promise is connected exactly once; name the collision
                fprintf(stderr, "set_delegate: promise %p already delegated to %s; new delegate %s\n",
                        (void*)this, typeid(*_delegate).name(), typeid(*delegate).name());
                abort();
            }
#endif
            _delegate = delegate;
        }

        template<typename D, typename... Args>
        D* emplace_delegate(Args&&... args) {
            static_assert(sizeof(D) <= sizeof(_delegate_storage));
            static_assert(alignof(D) <= alignof(void*));
            static_assert(std::is_trivially_destructible_v<D>);
            D* delegate = std::construct_at((D*)_delegate_storage, std::forward<Args>(args)...);
            set_delegate(delegate);
            return delegate;
        }

        constexpr std::suspend_always initial_suspend() const noexcept {
            return std::suspend_always{};
        }

        // Records the exception only: final_suspend still runs afterwards
        // and needs the delegate, so it is not released here
        void unhandled_exception() noexcept {
            _delegate->unhandled_exception();
        }

        struct FinalAwaitable : ResumeNever {
            template<typename DerivedPromise>
            std::coroutine_handle<> await_suspend(std::coroutine_handle<DerivedPromise> handle) const noexcept {
                // Release the delegate first so ~BasicPromise (run by the
                // destroy) does not report a cancellation.  Ask it for the
                // continuation BEFORE the destroy: the delegate may be parked
                // inside this very frame (a nursery child's).  The hook only
                // names the continuation; completion's side effects (a
                // nursery's retire, the wait group's) belong to that
                // continuation's resume, which runs after the frame is gone
                // TODO: should handle.destroy be moved inside delegate::final_await_suspend?
                Delegate<T>* delegate = take(handle.promise()._delegate);
                std::coroutine_handle<> continuation = delegate->final_await_suspend();
                handle.destroy();
                return continuation;
            }
        };

        constexpr FinalAwaitable final_suspend() const noexcept {
            return FinalAwaitable{};
        }

        ~BasicPromise() {
            if (_delegate)
                take(_delegate)->unhandled_stopped();
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

    // co_await x: the promise offers x to transform_await first, a
    // customization point that receives the promise and x in x's own value
    // category (Future, Outcome, the GetPromise and GetStopToken tags and
    // WithDeadlineSender customize it; an overload that takes only an
    // rvalue leaves lvalues to fall through).  Failing that an awaitable is
    // awaited as itself, and a sender is wrapped in a SenderAwaitable (see
    // Senders below).  Anything else is diagnosed here
    template<typename T, typename A>
    decltype(auto) await_transform_helper(Promise<T>* promise, A&& a) {
        using D = std::remove_cvref_t<A>;
        if constexpr (requires { transform_await(promise, std::forward<A>(a)); }) {
            return transform_await(promise, std::forward<A>(a));
        } else if constexpr (Awaitable<A>) {
            return std::forward<A>(a);
        } else if constexpr (Sender<D>) {
            static_assert(std::is_rvalue_reference_v<A&&>,
                          "co_await a sender as an rvalue");
            // TODO: SenderAwaitable can profit from early access to the promise
            return SenderAwaitable<Promise<T>, D>(promise, std::move(a));
        } else {
            static_assert(Sender<D>,
                          "co_await of something that is not transformable, awaitable or a sender");
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
    decltype(auto) transform_await(Promise<T>* promise, GetPromise) {
        return JustAwaitable<Promise<T>*>{ promise };
    }
    struct GetStopToken {};

    template<typename T>
    decltype(auto) transform_await(Promise<T>* promise, GetStopToken) {
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
        Future(Future&& other) : _promise(take(other._promise)) {}
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
        return handle_from_promise(take(future._promise));
    }

    template<typename OuterPromise, typename T>
    struct FutureAwaitable : OutcomeDelegate<FutureAwaitable<OuterPromise, T>, T> {

        using InnerPromise = Promise<T>;

        OuterPromise* _outer_promise;
        Future<T> _future;
        Outcome<T> _outcome;

        FutureAwaitable(OuterPromise* outer_promise, Future<T>&& future)
        : _outer_promise(outer_promise)
        , _future(std::move(future)) {
        }

        Outcome<T>& outcome() noexcept {
            return _outcome;
        }

        virtual void unhandled_stopped() noexcept override {
            set_stopped(_outcome);
            handle_from_promise(take(_outer_promise)).destroy();
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
            return _outcome.value();
        }

    };

    // Rvalues only: a Future is consumed by the await.  An lvalue falls
    // through to the Sender branch, whose static_assert says so
    template<typename U, typename T>
    auto transform_await(Promise<U>* promise, Future<T>&& future) {
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
        template<typename R2>
        JustOperationState(T&& value, R2&& receiver)
        : _value(std::move(value))
        , _receiver(std::forward<R2>(receiver)) {
        }
        JustOperationState(JustOperationState const&) = delete;
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

    // This is an example of a type with a very simple specialized Awaitable
    // (JustAwaitable) above, that gets shunted into the generic SenderAwaitable
    // by the current resolution


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
    struct FutureOperationState : OutcomeDelegate<FutureOperationState<T, R>, T> {

        struct Frame {
            Header _header = { &_static_resume, &_static_destroy };
            Outcome<T> _outcome;
            Future<T> _future;
            R _receiver;
            static void _static_resume(void* ptr) {
                auto self = (Frame*)ptr;
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
                auto self = (Frame*)ptr;
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

        Outcome<T>& outcome() noexcept {
            return _frame._outcome;
        }

        virtual void unhandled_stopped() noexcept override {
            set_stopped(_frame._outcome);
            // This hook is the destroy word: forward stopped to the receiver,
            // which may destroy this operation state (last touch)
            Frame::_static_destroy(&_frame);
        }
        virtual std::coroutine_handle<> final_await_suspend() noexcept override {
            return std::coroutine_handle<>::from_address(&_frame);
        }
        virtual std::stop_token get_stop_token() noexcept override {
            using Coroutine::get_stop_token;
            return get_stop_token(_frame._receiver);
        }

        template<typename R2>
        FutureOperationState(Future<T>&& future, R2&& receiver)
        : _frame{._future = std::move(future), ._receiver = std::forward<R2>(receiver)} {
        }

        FutureOperationState(FutureOperationState const&) = delete;

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
        Promise<T>* promise = take(op._frame._future._promise);
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
        handle_from_promise(take(receiver._base->_outer_promise)).resume();

    }
    template<typename OuterPromise>
    void set_value(AwaitableReceiver<OuterPromise, void>&& receiver) {
        set_value(receiver._base->_outcome);
        handle_from_promise(take(receiver._base->_outer_promise)).resume();
    }

    template<typename OuterPromise, typename T>
    void set_error(AwaitableReceiver<OuterPromise, T>&& receiver, std::exception_ptr&& error) {
        set_error(receiver._base->_outcome, std::move(error));
        handle_from_promise(take(receiver._base->_outer_promise)).resume();
    }

    template<typename OuterPromise, typename T>
    void set_stopped(AwaitableReceiver<OuterPromise, T>&& receiver) {
        set_stopped(receiver._base->_outcome);
        handle_from_promise(take(receiver._base->_outer_promise)).destroy();
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

        // Connected here: this object is a prvalue all the way through
        // await_transform, materialized in the awaiting frame, so its
        // address is final and the receiver may point at it
        OperationState _operation_state;

        explicit SenderAwaitable(OuterPromise* promise, S&& sender)
        : AwaitableBase<OuterPromise, T>{promise}
        , _operation_state(connect(std::move(sender), R{this})) {
        }

        SenderAwaitable(SenderAwaitable const&) = delete;

        constexpr bool await_ready() const noexcept { return false; }

        void await_suspend(std::coroutine_handle<OuterPromise> continuation) {
            assert(this->_outer_promise == &continuation.promise());
            start(_operation_state);
            // No *this past here: the operation may have completed inline,
            // resuming (and perhaps finishing) the coroutine that owns *this
        }

        auto await_resume() {
            assert(!this->_outer_promise);
            return this->_outcome.value();
        }

    };

#pragma mark Nursery / counted_scope

    template<typename T> struct NurseryChildDelegate;

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

            explicit Inner(Nursery* self) : _self(self) {}

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
                ptr = take(self->_continuation).address();
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
        , _inner(this)
        , _outer_stop_callback(_outer_stop_token, Callback{_inner_stop_source}) {
        }

        ~Nursery() {
            // Detect destruction of a coroutine containing a running nursery
            assert(!_children && !_counter.load_relaxed());
        }

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
                    auto handle = handle_from_promise(take(_promise));
                    global_work_queue_schedule(std::move(continuation));
                    return handle;
                }
                ~Awaitable() {
                    assert(!_promise);
                }
            };
            return Awaitable{{}, this, take(future._promise)};
        }

        // co_await nursery.fork(y, bar(x)) immediately starts bar on the current
        // thread and schedules the caller to execute soon.  When bar completes
        // it assigns to y; it is racy to access y until the nursery has been
        // joined
        template<typename T>
        [[nodiscard]] auto fork(Outcome<T>& target, Future<T>&& future) {
            struct Awaitable : std::suspend_always {
                Nursery* _nursery;
                Outcome<T>* _target;
                Future<T>::promise_type* _promise;
                std::coroutine_handle<typename Future<T>::promise_type> await_suspend(std::coroutine_handle<> continuation) noexcept {
                    ++(_nursery->_children);
                    _promise->template emplace_delegate<NurseryChildDelegate<T>>(_nursery, _target);
                    auto handle = handle_from_promise(take(_promise));
                    global_work_queue_schedule(std::move(continuation));
                    return handle;
                }
                ~Awaitable() {
                    assert(!_promise);
                }
            };
            return Awaitable{{}, this, &target, take(future._promise)};
        }

        // nursery.soon(foo(x)) schedules foo to execute soon and continues
        // the calling context normally.  The calling context does not have to
        // be a coroutine.  No target: see fork(Future<>&&)
        void soon(Future<>&& future) {
            ++_children;
            future._promise->set_delegate(&_inner);
            global_work_queue_schedule(handle_from_future(std::move(future)));
        }

        // nursery.soon(y, bar(x)) schedules bar to execute soon and
        // continues the calling context normally.  The calling context does
        // not have to be a coroutine.  When bar completes, it assigns to y;
        // it is racy to access y until the nursery has been joined
        template<typename T>
        void soon(Outcome<T>& target, Future<T>&& future) {
            ++_children;
            future._promise->template emplace_delegate<NurseryChildDelegate<T>>(this, &target);
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
                    auto count = _nursery->_counter.add_fetch_release(take(_nursery->_children));
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
                    return take(_nursery)->_cancelled_count.nonatomic_exchange(0);
                }
                ~Awaitable() {
                    assert(!_nursery || _nursery->_outer_stop_token.stop_requested());
                }
            };
            return Awaitable{this};
        }

        // TODO: How to we fork / spawn Senders?  We have to put the
        // OperationState on the heap, is using a coroutine thunk therefore as
        // good as a bespoke solution?

    }; // struct Nursery

    // Per-child delegate for a targeted fork or soon.  It lives in the
    // child's promise (emplace_delegate): the only object with the child's
    // lifetime, since the fork awaitable is gone once the child is running.
    template<typename T>
    struct NurseryChildDelegate : OutcomeDelegate<NurseryChildDelegate<T>, T> {
        Nursery* _nursery;
        Outcome<T>* _target;
        NurseryChildDelegate(Nursery* nursery, Outcome<T>* target)
        : _nursery(nursery)
        , _target(target) {
        }
        Outcome<T>& outcome() noexcept {
            return *_target;
        }
        virtual void unhandled_stopped() noexcept override {
            set_stopped(*_target);
            // TODO: Is this type-info-discarding static call a wart or a
            // virtuous sign of same-interface everywhere.  Is _static_destroy
            // called exclusively via this path?
            Nursery::_static_destroy(_nursery);
        }
        virtual std::stop_token get_stop_token() noexcept override {
            return _nursery->_inner_stop_source.get_token();
        }
        virtual std::coroutine_handle<> final_await_suspend() noexcept override {
            return std::coroutine_handle<>::from_address(_nursery);
        }
    };

    // ---- sync_wait: block the calling thread on a sender ------------------
    //
    // The receiver records the outcome and releases a semaphore; the
    // release / acquire pair is the publishing edge for a completion that
    // arrives on a worker.  start is handed to the pool through a two-word
    // thunk rather than called here, so the work runs on a pinned worker
    // whatever the caller is, and a worker that blocks here cannot recurse
    // into its own queue.  The caller's token, if any, is the receiver's
    // environment.  Returns the Outcome, never empty: take value(),
    // value_or(x) or stopped_as_optional() from it as the call site prefers.

    template<typename T>
    struct SyncWaitReceiver {
        Outcome<T>* _outcome;
        std::binary_semaphore* _semaphore;
        std::stop_token _stop_token;
    };

    template<typename T>
    void set_value(SyncWaitReceiver<T>&& receiver, T&& value) {
        set_value(*receiver._outcome, std::move(value));
        receiver._semaphore->release();
    }

    inline void set_value(SyncWaitReceiver<void>&& receiver) {
        set_value(*receiver._outcome);
        receiver._semaphore->release();
    }

    template<typename T>
    void set_error(SyncWaitReceiver<T>&& receiver, std::exception_ptr&& error) {
        set_error(*receiver._outcome, std::move(error));
        receiver._semaphore->release();
    }

    template<typename T>
    void set_stopped(SyncWaitReceiver<T>&& receiver) {
        set_stopped(*receiver._outcome);
        receiver._semaphore->release();
    }

    template<typename T>
    std::stop_token get_stop_token(SyncWaitReceiver<T> const& receiver) {
        return receiver._stop_token;
    }

    template<Sender S>
    Outcome<SenderValueType<S>> sync_wait(S&& sender, std::stop_token stop_token = {}) {
        using T = SenderValueType<S>;
        using OperationState = decltype(connect(std::declval<S&&>(),
                                                std::declval<SyncWaitReceiver<T>&&>()));
        struct Starter {
            Header _header = { &_static_resume, &_static_destroy };
            OperationState* _operation_state;
            static void _static_resume(void* ptr) {
                // last touch of the thunk and of the operation state: the
                // completion may release the caller before start returns
                start(*((Starter*)ptr)->_operation_state);
            }
            static void _static_destroy(void*) {
                abort();
            }
        };
        Outcome<T> outcome;
        std::binary_semaphore semaphore{0};
        OperationState operation_state = connect(std::forward<S>(sender),
                                                 SyncWaitReceiver<T>{&outcome, &semaphore, std::move(stop_token)});
        Starter starter{ ._operation_state = &operation_state };
        global_work_queue_schedule((void*)&starter);
        semaphore.acquire();
        return outcome;
    }

    // An awaitable that is not a sender (a nursery's join) is lifted into a
    // coroutine, which awaits the caller's object in place through a
    // pointer: the argument outlives the call, which blocks until the lifted
    // frame is gone, and a copy would be a second awaiter of a single-use
    // awaitable (a join's copy asserts when it dies unawaited).  The value
    // category the caller supplied is the one awaited
    template<typename A>
        requires (!Sender<A>) && Awaitable<A>
    auto sync_wait(A&& awaitable, std::stop_token stop_token = {}) {
        using D = std::remove_cvref_t<A>;
        using V = decltype(std::declval<D&>().await_resume());
        auto lift = [](D* a) -> Future<V> {
            co_return co_await std::forward<A>(*a);
        };
        return sync_wait(lift(&awaitable), std::move(stop_token));
    }



    template<typename T>
    struct SingleProducerSingleConsumerQueue {

        std::mutex _mutex;
        std::queue<T> _queue;
        std::coroutine_handle<> _pop_continuation;

        void emplace(auto&&... args) {
            std::unique_lock guard{_mutex};
            _queue.emplace(FORWARD(args)...);
            auto continuation = take(_pop_continuation);
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
                        continuation = take(_context->_pop_continuation);
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

        // TODO: Sender/receiver-level interface?

    };



    // ---- stopped_as_optional, shield: receiver adaptors -------------------
    //
    // The stopped channel becomes a value, nullopt, at the receiver; there is
    // nothing to connect or start beyond the inner's own operation state.
    // The inner runs under the token the adaptor puts in the receiver's
    // environment: the outer's (stopped_as_optional), or one the caller
    // supplies (shield; by default a detached token, which nobody can
    // request, so the inner cannot be cancelled from outside at all).  A
    // shielded inner can still stop of its own accord, hence optional either
    // way.  A shield with a budget is shield(f, s.get_token()) plus a
    // cancelable_after that requests s.

    template<Sender S>
    struct StoppedAsOptional {
        S _sender;
        std::optional<std::stop_token> _stop_token;  // engaged: replaces the outer's
    };

    template<Sender S>
    struct SenderTraits<StoppedAsOptional<S>> {
        using value_type = std::optional<typename Outcome<SenderValueType<S>>::stored_type>;
    };

    template<typename T, typename R>
    struct StoppedAsOptionalReceiver {
        R _receiver;
        std::optional<std::stop_token> _stop_token;
    };

    template<typename T, typename R>
    void set_value(StoppedAsOptionalReceiver<T, R>&& receiver, T&& value) {
        set_value(std::move(receiver._receiver), std::optional<T>{std::move(value)});
    }

    template<typename R>
    void set_value(StoppedAsOptionalReceiver<void, R>&& receiver) {
        set_value(std::move(receiver._receiver), std::optional<std::monostate>{std::in_place});
    }

    template<typename T, typename R>
    void set_error(StoppedAsOptionalReceiver<T, R>&& receiver, std::exception_ptr&& error) {
        set_error(std::move(receiver._receiver), std::move(error));
    }

    template<typename T, typename R>
    void set_stopped(StoppedAsOptionalReceiver<T, R>&& receiver) {
        set_value(std::move(receiver._receiver), std::optional<typename Outcome<T>::stored_type>{});
    }

    template<typename T, typename R>
    std::stop_token get_stop_token(StoppedAsOptionalReceiver<T, R> const& receiver) {
        return receiver._stop_token ? *receiver._stop_token : get_stop_token(receiver._receiver);
    }

    template<Sender S, typename R>
    auto connect(StoppedAsOptional<S>&& sender, R&& receiver) {
        return connect(std::move(sender._sender),
                       StoppedAsOptionalReceiver<SenderValueType<S>, std::remove_cvref_t<R>>{
                           std::forward<R>(receiver), std::move(sender._stop_token)});
    }

    template<Sender S>
    auto stopped_as_optional(S&& sender) {
        return StoppedAsOptional<std::remove_cvref_t<S>>{std::forward<S>(sender), std::nullopt};
    }

    template<Sender S>
    auto shield(S&& sender, std::stop_token stop_token = {}) {
        return StoppedAsOptional<std::remove_cvref_t<S>>{std::forward<S>(sender), std::move(stop_token)};
    }

    // ---- race: the first value or error wins ------------------------------
    //
    // Entrants are senders of one value type.  They run under an interior
    // stop source that the winner requests to hasten the losers, and that
    // the outer's token is bridged into.  A stopped entrant is a retiree,
    // not a winner: a race whose entrants all stop completes stopped, and a
    // tie-loser's value or error is dropped.  The winner's outcome, tagged
    // with its index, is delivered when the LAST entrant retires, on that
    // retiree's thread; until then this state is alive for every entrant.
    //
    // The entrant operation states live here by value, connected in the
    // constructor with receivers that point back here: like every operation
    // state this one is constructed where it lives.  The bridge is
    // registered in start.

    template<typename T>
    struct RaceWinner {
        std::size_t index;
        T value;
    };

    template<>
    struct RaceWinner<void> {
        std::size_t index;
    };

    template<Sender... Ss>
    struct Race {
        std::tuple<Ss...> _senders;
    };

    template<Sender S, Sender... Ss>
    struct SenderTraits<Race<S, Ss...>> {
        static_assert((std::is_same_v<SenderValueType<S>, SenderValueType<Ss>> && ...),
                      "race entrants must have one value type");
        using value_type = RaceWinner<SenderValueType<S>>;
    };

    template<typename T, typename R>
    struct RaceBase {

        struct Bridge {
            std::stop_source _stop_source;
            void operator()() {
                // Local copy: the entrants this unwinds may retire the race
                // and free this functor with it
                std::stop_source keepalive = std::move(_stop_source);
                keepalive.request_stop();
            }
        };

        R _receiver;
        std::stop_source _stop_source;                       // interior
        std::optional<std::stop_callback<Bridge>> _bridge;   // outer -> interior
        Atomic<std::ptrdiff_t> _winner{-1};
        Atomic<std::ptrdiff_t> _live;
        Outcome<T> _outcome;                                 // the winner's

        template<typename R2>
        RaceBase(R2&& receiver, std::ptrdiff_t entrants)
        : _receiver(std::forward<R2>(receiver))
        , _live(entrants) {
        }

        RaceBase(RaceBase const&) = delete;

        bool claim(std::ptrdiff_t index) {
            // Mutual exclusion only; the winner's outcome is published by
            // the retire below, not by this exchange
            std::ptrdiff_t expected = -1;
            return _winner.compare_exchange_strong_relaxed_relaxed(expected, index);
        }

        void hasten() {
            // Losers parked in cancelable leaves unwind and retire inside
            // this call; none is the last, the caller has not retired yet
            _stop_source.request_stop();
        }

        void retire() {
            // Every retire is a release decrement of one word; only the
            // last, seeing zero, takes an acquire.  That load reads its own
            // decrement, an RMW that lies in the release sequence headed by
            // each earlier retire (RMWs extend a release sequence), so it
            // synchronizes with all of them: the winner's outcome and the
            // losers' unwinding happen before the delivery
            if (_live.sub_fetch_release(1) == 0) {
                (void) _live.load_acquire();
                deliver();
            }
        }

        void deliver() {
            // last touch of this on every path
            std::size_t index = (std::size_t) _winner.load_relaxed();
            switch (_outcome._variant.index()) {
                case 1:
                    if constexpr (std::is_void_v<T>) {
                        set_value(std::move(_receiver), RaceWinner<void>{index});
                    } else {
                        set_value(std::move(_receiver),
                                  RaceWinner<T>{index, std::move(std::get<1>(_outcome._variant))});
                    }
                    break;
                case 2:
                    set_error(std::move(_receiver), std::move(std::get<2>(_outcome._variant)));
                    break;
                case 0:
                    // no claim: every entrant stopped
                    set_stopped(std::move(_receiver));
                    break;
                default:
                    abort();
            }
        }

    };

    template<typename T, typename R, std::size_t I>
    struct RaceReceiver {
        RaceBase<T, R>* _base;
    };

    template<typename T, typename R, std::size_t I>
    void set_value(RaceReceiver<T, R, I>&& receiver, T&& value) {
        RaceBase<T, R>* base = receiver._base;
        if (base->claim(I)) {
            set_value(base->_outcome, std::move(value));
            base->hasten();
        }
        base->retire();  // last touch
    }

    template<typename R, std::size_t I>
    void set_value(RaceReceiver<void, R, I>&& receiver) {
        RaceBase<void, R>* base = receiver._base;
        if (base->claim(I)) {
            set_value(base->_outcome);
            base->hasten();
        }
        base->retire();  // last touch
    }

    template<typename T, typename R, std::size_t I>
    void set_error(RaceReceiver<T, R, I>&& receiver, std::exception_ptr&& error) {
        RaceBase<T, R>* base = receiver._base;
        if (base->claim(I)) {
            set_error(base->_outcome, std::move(error));
            base->hasten();
        }
        base->retire();  // last touch
    }

    template<typename T, typename R, std::size_t I>
    void set_stopped(RaceReceiver<T, R, I>&& receiver) {
        receiver._base->retire();  // last touch
    }

    template<typename T, typename R, std::size_t I>
    std::stop_token get_stop_token(RaceReceiver<T, R, I> const& receiver) {
        return receiver._base->_stop_source.get_token();
    }

    // A tuple whose elements are constructed in place, each from its own
    // factory: a connect prvalue returned by the factory is elided into the
    // slot, where std::tuple would have to move it.  The slots are bases so
    // that a pack of them is initialized in one expansion
    template<std::size_t I, typename V>
    struct InPlaceSlot {
        V _value;
        template<typename F>
        explicit InPlaceSlot(F&& factory) : _value(factory()) {}
    };

    template<typename Indices, typename... Vs> struct InPlaceTuple;

    template<std::size_t... Is, typename... Vs>
    struct InPlaceTuple<std::index_sequence<Is...>, Vs...> : InPlaceSlot<Is, Vs>... {
        template<typename... Fs>
        explicit InPlaceTuple(Fs&&... factories) : InPlaceSlot<Is, Vs>(factories)... {}
        InPlaceTuple(InPlaceTuple const&) = delete;
        template<std::size_t I>
        auto& get() {
            return static_cast<InPlaceSlot<I, std::tuple_element_t<I, std::tuple<Vs...>>>&>(*this)._value;
        }
    };

    template<typename T, typename R, typename Indices, Sender... Ss>
    struct RaceOperationStates;

    template<typename T, typename R, std::size_t... Is, Sender... Ss>
    struct RaceOperationStates<T, R, std::index_sequence<Is...>, Ss...> {
        using type = InPlaceTuple<std::index_sequence<Is...>,
                                  decltype(connect(std::declval<Ss&&>(),
                                                   std::declval<RaceReceiver<T, R, Is>&&>()))...>;
    };

    // TODO: Unlike Nursery, this formulation prevents runtime determination
    // of participants.  Not sure if that is a good thing.  In a "download
    // from multiple remotes" race, the remotes would be a runtime list.
    // But, in a with_deadline race, we have a small number of known
    // participants.

    template<typename T, typename R, Sender... Ss>
    struct RaceOperationState : RaceBase<T, R> {

        using Indices = std::index_sequence_for<Ss...>;
        using OperationStates = typename RaceOperationStates<T, R, Indices, Ss...>::type;

        // The entrants, each connected to a receiver that points at the base
        OperationStates _operation_states;

        template<typename R2>
        RaceOperationState(std::tuple<Ss...>&& senders, R2&& receiver)
        : RaceOperationState(std::move(senders), std::forward<R2>(receiver), Indices{}) {
        }

        template<typename R2, std::size_t... Is>
        RaceOperationState(std::tuple<Ss...>&& senders, R2&& receiver, std::index_sequence<Is...>)
        : RaceBase<T, R>(std::forward<R2>(receiver), sizeof...(Ss))
        , _operation_states([&] {
            return connect(std::move(std::get<Is>(senders)), RaceReceiver<T, R, Is>{this});
        }...) {
        }

    };

    template<typename T, typename R, Sender... Ss>
    void start(RaceOperationState<T, R, Ss...>& op) {
        using Bridge = typename RaceBase<T, R>::Bridge;
        // The bridge fires here if the outer's token is already requested:
        // the entrants then start under a requested interior token
        op._bridge.emplace(get_stop_token(op._receiver), Bridge{op._stop_source});
        [&]<std::size_t... Is>(std::index_sequence<Is...>) {
            // Any entrant may complete inside its start and retire; the last
            // retire delivers and may destroy op, but only once every entrant
            // has started, so the last start is the last touch of op
            (start(op._operation_states.template get<Is>()), ...);
        }(std::index_sequence_for<Ss...>{});
    }

    template<Sender... Ss, typename R>
    auto connect(Race<Ss...>&& race, R&& receiver) {
        using T = SenderValueType<std::tuple_element_t<0, std::tuple<Ss...>>>;
        return RaceOperationState<T, std::remove_cvref_t<R>, Ss...>(std::move(race._senders),
                                                                    std::forward<R>(receiver));
    }

    template<Sender... Ss>
    auto race(Ss&&... senders) {
        return Race<std::remove_cvref_t<Ss>...>{
            std::tuple<std::remove_cvref_t<Ss>...>{std::forward<Ss>(senders)...}
        };
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
