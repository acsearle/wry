//
//  execution.hpp
//  client
//
//  Created by Antony Searle on 17/10/2026.
//

#ifndef execution_hpp
#define execution_hpp

#include <execution>

// Wrap <execution> and provide extended, related and future standard
// functionality

#include <exception>
#include <optional>
#include <semaphore>
#include <stop_token>

#include "utility.hpp"
#include "variant.hpp"
#include "global_work_queue.hpp"
#include "atomic.hpp"

namespace wry::execution {

#pragma mark Sender concepts

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


#pragma mark Simple senders

    // ---- Just: the synchronous sender -------------------------------------

    template<typename T>
    struct just_t {
        T _value;
    };

    template<typename T>
    auto just(T&& x) {
        return just_t<T>{std::forward<T>(x)};
    }

    template<typename T>
    struct SenderTraits<just_t<T>> {
        using value_type = T;
    };

    template<typename T, typename R>
    struct just_operation_state_t {
        T _value;
        R _receiver;
        template<typename R2>
        just_operation_state_t(T&& value, R2&& receiver)
        : _value(std::move(value))
        , _receiver(std::forward<R2>(receiver)) {
        }
        just_operation_state_t(just_operation_state_t const&) = delete;
    };

    template<typename T, typename R>
    auto connect(just_t<T>&& sender, R&& receiver) {
        return just_operation_state_t<T, std::remove_cvref_t<R>>{
            std::move(sender._value), std::forward<R>(receiver)
        };
    }

    template<typename T, typename R>
    void start(just_operation_state_t<T, R>& op) {
        // last touch of op
        set_value(std::move(op._receiver), std::move(op._value));
    }



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

    // As a sink.  These are the verbs a receiver that targets an Outcome
    // calls (SyncWaitReceiver, a nursery child's delegate); an Outcome is
    // not itself a receiver, since completion consumes a receiver and an
    // Outcome must outlive completion to be read

    template<typename T, typename... Args>
    void set_value(execution::Outcome<T>& outcome, Args&&... args) {
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
            static void _static_resume(void* ptr) {
                // last touch of the thunk and of the operation state: the
                // completion may release the caller before start returns
                start(*((Starter*)ptr)->_operation_state);
            }
            static void _static_destroy(void*) {
                abort();
            }
            void (*_resume)(void*) = &_static_resume;
            void (*_destroy)(void*) = &_static_destroy;
            OperationState* _operation_state;
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





    // ---- when_any: the first value or error wins ------------------------------
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

    template<execution::Sender... Ss>
    struct when_any_t {
        std::tuple<Ss...> _senders;
    };

    template<Sender S, Sender... Ss>
    struct SenderTraits<when_any_t<S, Ss...>> {
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
        execution::Outcome<T> _outcome;                                 // the winner's

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

    template<typename T, typename R, typename Indices, execution::Sender... Ss>
    struct RaceOperationStates;

    template<typename T, typename R, std::size_t... Is, execution::Sender... Ss>
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
    auto connect(when_any_t<Ss...>&& race, R&& receiver) {
        using T = SenderValueType<std::tuple_element_t<0, std::tuple<Ss...>>>;
        return RaceOperationState<T, std::remove_cvref_t<R>, Ss...>(std::move(race._senders),
                                                                    std::forward<R>(receiver));
    }

    template<Sender... Ss>
    auto when_any(Ss&&... senders) {
        return when_any_t<std::remove_cvref_t<Ss>...>{
            std::tuple<std::remove_cvref_t<Ss>...>{std::forward<Ss>(senders)...}
        };
    }



} // namespace wry::execution


#endif /* execution_hpp */
