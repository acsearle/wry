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

    // Idempotent; called lazily by the operations below.
    void global_reactor_start();

    // Trigger the shutdown doorbell and join the reactor thread.  No-op if
    // the reactor was never started.  Call after all waits have completed
    // (the wait group has drained) and before the worker pool is cancelled.
    void global_reactor_stop();

    // Suspend until file_descriptor is readable or the deadline passes, then
    // perform one recv into buffer.
    //
    //   - value: bytes received; 0 is end-of-stream (the peer closed; kqueue
    //     delivers EV_EOF as readability and recv returns 0)
    //   - error: ETIMEDOUT if the deadline won, otherwise the errno from
    //     registration (e.g. EBADF) or from recv itself
    //
    // A stop request on the awaiting chain's token cancels the wait promptly:
    // the parked frame is destroyed (cancellation unwinding), the knotes are
    // withdrawn, and the operation completes on no channel.  At most one
    // recv_some may wait on a given fd at a time (the knote is keyed by fd).
    //
    // Pass steady_clock::time_point::max() for no deadline.
    Coroutine::Future<std::expected<size_t, int>>
    recv_some(int file_descriptor,
              std::span<std::byte> buffer,
              std::chrono::steady_clock::time_point deadline);

} // namespace wry

#endif /* kqueue_reactor_hpp */
