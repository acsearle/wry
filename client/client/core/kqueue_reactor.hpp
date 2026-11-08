//
//  kqueue_reactor.hpp
//  client
//
//  Created by Antony Searle on 22/11/2025.
//

#ifndef kqueue_reactor_hpp
#define kqueue_reactor_hpp

#include <sys/socket.h>

#include <chrono>
#include <functional>
#include <expected>
#include <span>

#include "coroutine.hpp"
#include "functional.hpp"

namespace wry {

    // A dedicated reactor thread blocked in kevent waits on kernel events
    // (fd readiness, timers) and reschedules the interested coroutines onto
    // the global work queue.  The reactor is a foreign thread: it never runs
    // GC code and never resumes or destroys frames itself -- completions and
    // cancellations are handed to the (mutator-pinned) worker pool.
    //
    //   macOS    : kqueue
    //   Windows  : IOCP
    //   Linux    : epoll (io_uring?)
    //   Fallback : select

    // Called once by main() before any IO
    void global_reactor_start();

    // Trigger the shutdown doorbell and join the reactor thread.  No-op if
    // the reactor was never started.  Call after all waits have completed
    // (the wait group has drained) and before the worker pool is cancelled.
    void global_reactor_stop();


    // At `duration` from now, invoke `f` on the pool -- unless `token` is
    // requested first, in which case `f` is destroyed unrun.  A stop request
    // against an unfired timer withdraws it synchronously; a timer that has
    // already fired runs `f` regardless, so `f` must tolerate a late stop
    // (with_deadline's does: a second request_stop is idempotent).  A failed
    // timer registration aborts (it is resource-exhaustion grade).
    void cancelable_after(std::stop_token token,
                          std::chrono::steady_clock::duration duration,
                          move_only_function<void()> f);

    // Socket operations.  Each suspends until the descriptor is ready for
    // it (a readiness awaitable on EVFILT_READ or EVFILT_WRITE, see the
    // .cpp) and then makes one syscall, whose result is returned in kernel
    // convention: the syscall's value on success, or -errno -- for the
    // syscall, or for a failed knote registration (-EBADF for a closed
    // descriptor).  Readiness is not a guarantee: the syscall may still
    // fail with -EAGAIN on a non-blocking descriptor if another waiter took
    // the data, and will block the worker on a blocking one, so at most one
    // waiter per (descriptor, direction) at a time.  Peer close arrives as
    // readiness, and the syscall reports it (recv 0, send -EPIPE; set
    // SO_NOSIGPIPE on the socket, or SIGPIPE comes first).
    //
    // No deadlines here: compose one with with_deadline below, which times
    // the wait out by requesting an interior stop source.  A stop request on
    // the awaiting chain's token destroys the parked frame (cancellation
    // unwinding) and withdraws the knote promptly.

    // recv: bytes received, 0 for end of stream, or -errno
    [[nodiscard]] coroutine::Future<ssize_t>
    recv_some(int socket, void* buffer, size_t length, int flags);

    // send: bytes sent (possibly fewer than asked), or -errno
    [[nodiscard]] coroutine::Future<ssize_t>
    send_some(int socket, void const* buffer, size_t length, int flags);

    // accept on a listening socket: the new descriptor, or -errno.  The new
    // descriptor inherits nothing from the reactor: set O_NONBLOCK and
    // SO_NOSIGPIPE on it as the caller sees fit.  (Named accept_one, and
    // connect_to below, because the C names would be ambiguous through
    // argument-dependent lookup on sockaddr)
    [[nodiscard]] coroutine::Future<int>
    accept_one(int socket, struct sockaddr* address, socklen_t* length);

    // connect: 0, or -errno.  Suspends only when the socket is non-blocking
    // and the connect is in progress (TCP); a blocking socket connects
    // synchronously on the worker
    [[nodiscard]] coroutine::Future<int>
    connect_to(int socket, struct sockaddr const* address, socklen_t length);

    [[nodiscard]] coroutine::Future<>
    cancelable_sleep(double seconds);


    // ---- with_deadline: the inner, hastened by a timer -----------------------
    //
    //   optional<T> r = co_await with_deadline(t, sender);
    //
    // The inner runs under an interior stop source; the outer's token is
    // bridged into it, and a timer requests it at the deadline.  The timer
    // is NOT an entrant that can win: it only asks the inner to stop, and
    // the inner's own completion always decides the result.  So
    //
    //   inner value     -> engaged, even if the deadline fired meanwhile (an
    //                      inner past its cancellation point, which has
    //                      consumed bytes, an fd or an event batch, completes
    //                      with them and they reach the caller)
    //   inner error     -> the error
    //   inner stopped   -> nullopt (it honoured the deadline, or stopped of
    //                      its own accord), unless the outer's token is
    //                      requested, in which case stopped: the outer's own
    //                      request propagates instead of being absorbed
    //
    // The inner's completion requests the interior source before
    // delivering, which withdraws an unfired timer synchronously; a timer
    // that has already fired holds only its own copy of the source (shared
    // stop state), so it outlives this operation state harmlessly and its
    // late request is idempotent.

    template<execution::Sender S>
    struct WithDeadlineSender {
        std::chrono::steady_clock::time_point _deadline;
        S _sender;
    };

} // namespace wry

namespace wry::execution {
    template<Sender S>
    struct SenderTraits<wry::WithDeadlineSender<S>> {
        // optional<monostate> for a void inner, as stopped_as_optional yields
        using value_type = std::optional<typename Outcome<SenderValueType<S>>::stored_type>;
    };
} // namespace wry::execution

namespace wry {

    // Everything the inner's receiver touches; independent of the inner's
    // operation state type
    template<typename V, typename R>
    struct WithDeadlineBase {

        using U = typename execution::Outcome<V>::stored_type;

        struct Requester {
            std::stop_source _stop_source;
            void operator()() {
                // Local copy: the unwinding this request causes may destroy
                // the operation state that owns this functor
                std::stop_source keepalive = _stop_source;
                keepalive.request_stop();
            }
        };

        R _receiver;
        std::chrono::steady_clock::time_point _deadline;
        std::stop_source _stop_source;                          // interior
        std::optional<std::stop_callback<Requester>> _bridge;   // outer -> interior

        template<typename R2>
        WithDeadlineBase(R2&& receiver, std::chrono::steady_clock::time_point deadline)
        : _receiver(std::forward<R2>(receiver))
        , _deadline(deadline) {
        }

        WithDeadlineBase(WithDeadlineBase const&) = delete;

        // Each runs on the inner's completion thread, after the inner's
        // last touch of itself; each is the last touch of *this
        void _deliver_value(std::optional<U>&& value) {
            _stop_source.request_stop();  // withdraw the timer
            set_value(std::move(_receiver), std::move(value));
        }

        void _deliver_error(std::exception_ptr&& error) {
            _stop_source.request_stop();
            set_error(std::move(_receiver), std::move(error));
        }

        void _deliver_stopped() {
            _stop_source.request_stop();
            // If the outer's request is why the inner stopped, it reached
            // the inner through the bridge, a callback that request_stop
            // runs after setting the outer's stop flag; the inner observed
            // the interior request and its stopped completion is caused by
            // it, so the flag store happens before this load and it reads
            // requested.  An outer request that races an inner stopping for
            // another reason may go either way, which is the same as any
            // sender that completes before it sees the request
            if (get_stop_token(_receiver).stop_requested())
                set_stopped(std::move(_receiver));
            else
                set_value(std::move(_receiver), std::optional<U>{});
        }

    };

    template<typename V, typename R>
    struct WithDeadlineReceiver {
        WithDeadlineBase<V, R>* _base;
    };

    template<typename V, typename R>
    void set_value(WithDeadlineReceiver<V, R>&& receiver, V&& value) {
        receiver._base->_deliver_value(std::optional<V>{std::move(value)});
    }

    template<typename R>
    void set_value(WithDeadlineReceiver<void, R>&& receiver) {
        receiver._base->_deliver_value(std::optional<std::monostate>{std::in_place});
    }

    template<typename V, typename R>
    void set_error(WithDeadlineReceiver<V, R>&& receiver, std::exception_ptr&& error) {
        receiver._base->_deliver_error(std::move(error));
    }

    template<typename V, typename R>
    void set_stopped(WithDeadlineReceiver<V, R>&& receiver) {
        receiver._base->_deliver_stopped();
    }

    template<typename V, typename R>
    std::stop_token get_stop_token(WithDeadlineReceiver<V, R> const& receiver) {
        return receiver._base->_stop_source.get_token();
    }

    template<execution::Sender S, typename R>
    struct WithDeadlineOperationState : WithDeadlineBase<execution::SenderValueType<S>, R> {

        using V = execution::SenderValueType<S>;
        using Inner = decltype(connect(std::declval<S&&>(), std::declval<WithDeadlineReceiver<V, R>&&>()));

        Inner _inner;

        template<typename R2>
        WithDeadlineOperationState(S&& sender, std::chrono::steady_clock::time_point deadline, R2&& receiver)
        : WithDeadlineBase<V, R>(std::forward<R2>(receiver), deadline)
        , _inner(connect(std::move(sender), WithDeadlineReceiver<V, R>{this})) {
        }

    };

    template<execution::Sender S, typename R>
    void start(WithDeadlineOperationState<S, R>& op) {
        using Requester = typename WithDeadlineBase<execution::SenderValueType<S>, R>::Requester;
        // The bridge fires here if the outer is already requested; the timer
        // is then never armed and the inner starts under a requested token
        op._bridge.emplace(get_stop_token(op._receiver), Requester{op._stop_source});
        cancelable_after(op._stop_source.get_token(),
                         op._deadline - std::chrono::steady_clock::now(),
                         Requester{op._stop_source});
        // last touch of op: the inner may complete inside its start
        start(op._inner);
    }

    template<execution::Sender S, typename R>
    auto connect(WithDeadlineSender<S>&& sender, R&& receiver) {
        return WithDeadlineOperationState<S, std::remove_cvref_t<R>>(std::move(sender._sender),
                                                                     sender._deadline,
                                                                     std::forward<R>(receiver));
    }

    template<execution::Sender S>
    WithDeadlineSender<std::remove_cvref_t<S>> with_deadline(std::chrono::steady_clock::time_point deadline, S&& sender) {
        static_assert(!std::is_lvalue_reference_v<S>, "with_deadline consumes a sender: pass an rvalue");
        return WithDeadlineSender<std::remove_cvref_t<S>>{deadline, std::forward<S>(sender)};
    }

    // Anything else a coroutine could co_await (a queue's take, a nursery's
    // join) is lifted into a Future first, awaited in place through a
    // pointer: the argument must outlive the await, which a temporary in
    // the awaiting expression does
    template<typename A>
        requires (!execution::Sender<A>)
    auto with_deadline(std::chrono::steady_clock::time_point deadline, A&& awaitable) {
        return with_deadline(deadline, coroutine::lift_awaitable<A>(&awaitable));
    }


} // namespace wry

#endif /* kqueue_reactor_hpp */
