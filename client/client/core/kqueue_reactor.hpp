//
//  kqueue_reactor.hpp
//  client
//
//  Created by Antony Searle on 22/11/2025.
//

#ifndef kqueue_reactor_hpp
#define kqueue_reactor_hpp

#include <chrono>
#include <expected>
#include <span>

#include "coroutine.hpp"

namespace wry {

    // A dedicated reactor thread blocked in kevent64 waits on kernel events
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

    // At `duration` from now, start `future` on the pool with `token` as its
    // stop token -- unless `token` is requested first, in which case the
    // future never runs.  After request_stop(token) returns, the timer
    // provably will not fire and the future provably will not run:
    // cancellation is a synchronous join.  A failed timer registration
    // aborts (it is resource-exhaustion grade).
    void cancelable_after(std::stop_token token,
                          std::chrono::steady_clock::duration duration,
                          Coroutine::Future<>&& future);

    // Suspend until `socket` is readable, then perform one recv.  Returns the
    // recv result in kernel convention: bytes received, 0 for end-of-stream
    // (peer close arrives as EV_EOF readability), or -errno for a failed
    // registration (e.g. -EBADF) or a failed recv.
    //
    // No deadline here: compose one with with_deadline (kqueue_reactor.cpp),
    // which times out the wait by requesting an interior stop source.  A stop
    // request on the awaiting chain's token destroys the parked frame
    // (cancellation unwinding); the knote is withdrawn promptly.  At most one
    // recv_some may wait on a given socket at a time: a second waiter's knote
    // also fires on readability, and on a blocking socket the loser blocks
    // its worker in recv.
    [[nodiscard]] Coroutine::Future<ssize_t>
    recv_some(int socket, void* buffer, size_t length, int flags);

} // namespace wry

#endif /* kqueue_reactor_hpp */
