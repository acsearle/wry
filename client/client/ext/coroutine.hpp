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
#include <exception>
#include <optional>
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
#include "execution.hpp"

#include "epoch_allocator.hpp"
#include "global_work_queue.hpp"
#include "garbage_collected.hpp"


namespace wry {

    void global_work_queue_schedule(std::coroutine_handle<>);
    void global_work_queue_schedule_destroy_by_address(void* ptr);

} // namespace wry

namespace wry::coroutine {

    template<typename = void> struct Delegate;
    template<typename = void> struct Promise;
    template<typename = void> struct Future;
    using Task = Future<>;

#pragma mark Coroutine punning

    inline void resume_by_address(void* ptr) {
        [[clang::musttail]] return __builtin_coro_resume(ptr);
    }

    inline void destroy_by_address(void* ptr) {
        [[clang::musttail]] return __builtin_coro_destroy(ptr);
    }

    struct Header {
        void (*resume )(void*);
        void (*destroy)(void*);
    };

#pragma mark Basic Awaitables

    // Awaitable as the language sees it, for A's own value category:
    // await_ready and friends, or a member or non-member operator co_await
    template<typename A>
    concept Awaitable = requires(A&& a) { std::forward<A>(a).await_ready(); }
    || requires(A&& a) { std::forward<A>(a).operator co_await(); }
    || requires(A&& a) { operator co_await(std::forward<A>(a)); };

    // TODO: Is a type independently Awaitable or only in conjunction with a
    // context?

    template<typename T>
    struct just_awaitable_t {
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

    template<typename OuterPromise, execution::Sender S> struct SenderAwaitable;


    // co_await outcome: a value returns, an error rethrows, stopped destroys
    // the awaiting frame (the cancellation continues through it).  An
    // Outcome is not an awaitable in its own right; a Promise reaches this
    // through transform_await, for an lvalue or an rvalue outcome
    template<typename T>
    struct OutcomeAwaitable {
        execution::Outcome<T>* _outcome;
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
    OutcomeAwaitable<T> transform_await(Promise<U>*, execution::Outcome<T>& outcome) {
        return OutcomeAwaitable<T>{&outcome};
    }

    template<typename U, typename T>
    OutcomeAwaitable<T> transform_await(Promise<U>*, execution::Outcome<T>&& outcome) {
        return OutcomeAwaitable<T>{&outcome};
    }

#pragma mark Promise

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

    struct GetPromise {};

    template<typename T>
    decltype(auto) transform_await(Promise<T>* promise, GetPromise) {
        return just_awaitable_t<Promise<T>*>{ promise };
    }
    struct GetStopToken {};

    template<typename T>
    decltype(auto) transform_await(Promise<T>* promise, GetStopToken) {
        return just_awaitable_t<std::stop_token>{ get_stop_token(*promise) };
    }





#pragma mark Await transform customization point

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
        } else if constexpr (execution::Sender<D>) {
            static_assert(std::is_rvalue_reference_v<A&&>,
                          "co_await a sender as an rvalue");
            // TODO: SenderAwaitable can profit from early access to the promise
            return SenderAwaitable<Promise<T>, D>(promise, std::move(a));
        } else {
            static_assert(execution::Sender<D>,
                          "co_await of something that is not transformable, awaitable or a sender");
        }
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
        execution::Outcome<T>& _outcome_of_derived() noexcept {
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


    template<typename OuterPromise, typename T>
    struct FutureAwaitable : OutcomeDelegate<FutureAwaitable<OuterPromise, T>, T> {

        using InnerPromise = Promise<T>;

        OuterPromise* _outer_promise;
        Future<T> _future;
        execution::Outcome<T> _outcome;

        FutureAwaitable(OuterPromise* outer_promise, Future<T>&& future)
        : _outer_promise(outer_promise)
        , _future(std::move(future)) {
        }

        execution::Outcome<T>& outcome() noexcept {
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
            using coroutine::get_stop_token;
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



    // ---- Future as a sender: the operation state is a fake frame ----------
    //
    // The coroutine's continuation is the operation state itself.  Its
    // resume word is reached by final_suspend and splits value / error on the
    // exception slot; its destroy word is reached by ~Promise during
    // cancellation unwinding and is the stopped channel.

} // namespace wry::coroutine

namespace wry::execution {
    template<typename T>
    struct SenderTraits<wry::coroutine::Future<T>> {
        using value_type = T;
        using error_type = std::exception_ptr;
    };
}

namespace wry::coroutine {

    template<typename T, typename R>
    struct FutureOperationState : OutcomeDelegate<FutureOperationState<T, R>, T> {

        struct Frame {
            Header _header = { &_static_resume, &_static_destroy };
            execution::Outcome<T> _outcome;
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

        execution::Outcome<T>& outcome() noexcept {
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
            using coroutine::get_stop_token;
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
        execution::Outcome<T> _outcome;
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

    template<typename OuterPromise, execution::Sender S>
    struct SenderAwaitable : AwaitableBase<OuterPromise, execution::SenderValueType<S>> {

        using T = execution::SenderValueType<S>;
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



    // ---- Lifting an awaitable into a Future ----------------------------------
    //
    // What co_await x yields inside a Promise, with x in its own value
    // category: through transform_await for the types that customize it,
    // else x's own await_resume, else a SenderAwaitable's
    template<typename A, typename T=void>
    using AwaitResultType = decltype(await_transform_helper(static_cast<Promise<T>*>(nullptr),
                                                            std::declval<A>()).await_resume());

    // A Future that awaits the caller's object in place, through a pointer:
    // the object must outlive the await (a temporary in the awaiting
    // expression does; so does a local the caller blocks on).  The value
    // category A says how it is awaited
    template<typename A>
    Future<AwaitResultType<A>> lift_awaitable(std::remove_reference_t<A>* a) {
        co_return co_await static_cast<A&&>(*a);
    }



    // Anything else a coroutine could co_await (a nursery's join, a queue's
    // take, a with_deadline) is lifted into one, which awaits the caller's
    // object in place through a pointer: the argument outlives the call,
    // which blocks until the lifted frame is gone, and a copy would be a
    // second awaiter of a single-use awaitable (a join's copy asserts when
    // it dies unawaited).  The value category the caller supplied is the
    // one awaited
    template<typename A>
    requires (!execution::Sender<A>)
    auto sync_wait(A&& awaitable, std::stop_token stop_token = {}) {
        return execution::sync_wait(lift_awaitable<A>(&awaitable), std::move(stop_token));
    }

} // namespace wry::coroutine

namespace wry {

    // Process-lifetime work anchor (a Go-style wait group).  Background tasks
    // spawned onto the work queue register here so that main can block until
    // they finish before cancelling the thread pool -- otherwise a task that has
    // yielded mid-flight (e.g. a chunked background save) would be abandoned at
    // shutdown, leaking its frame and leaving a half-written file.
    //
    // It is a nursery without an owner: spawns come from any thread, so the
    // count is a sentinel-held atomic rather than a fold at join, and the
    // join is a blocking wait or a callback rather than a coroutine.  Like a
    // nursery it has a stop source, the ROOT token: every task spawned here
    // runs under it (GetStopToken sees it; nurseries beneath bridge it), and
    // main requests it when shutdown begins so that servers parked in the
    // reactor unwind rather than hold the drain open.  Work that never
    // reaches a cancellation point is unaffected and still drains.
    //
    // It is inherently a single process-global instance (blocking; no other use
    // case), so it is expressed as static methods over hidden state

    // must call before wait is called
    void wait_group_spawn(coroutine::Task task);

    // the root token, for code outside any task (bridge it with a
    // stop_callback or a Nursery)
    std::stop_token wait_group_stop_token();

    // shutdown has begun: request the root token.  Idempotent
    bool wait_group_request_stop();

    // blocking; consumes main's sentinel, so call exactly once
    void wait_group_wait();

    // consumes main's sentinel, so call exactly once
    void wait_group_set_callback(void* callback);

} // namespace wry

#endif /* coroutine_hpp */
