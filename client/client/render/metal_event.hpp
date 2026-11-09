//
//  metal_event.hpp
//  client
//
//  Coroutines that wait on the GPU: suspend until an MTLSharedEvent reaches
//  a value, then continue on the worker pool.  Objective-C++ only.
//

#ifndef metal_event_hpp
#define metal_event_hpp

#include <cstdint>

#import <Metal/Metal.h>

#include "coroutine.hpp"

namespace wry {

    // co_await shared_event_wait(event, value) completes once
    // event.signaledValue >= value, whoever signals it: a command buffer's
    // encodeSignalEvent:value: (the GPU passed that point; for a command
    // buffer's completion, encode the signal last, before commit) or the
    // CPU's setSignaledValue:.  The coroutine continues on the worker pool,
    // never on the Metal notification thread.  A value already reached
    // completes without suspending.
    //
    // A stop request on the awaiting chain's token while it waits destroys
    // the parked frame (cancellation unwinding), synchronously on the
    // requesting thread, as the reactor's leaves do.  Metal cannot withdraw
    // a listener, so the notification stays registered after a stop: a
    // small heap record (not the coroutine) lives until the event reaches
    // the value or Metal drops the notification, and then dies without
    // resuming anything.  Completion and stop race through one claim, so
    // exactly one of them disposes of the continuation (see the .mm).
    //
    // Visibility: once this resumes, writes to shared-storage resources by
    // GPU work ordered before the signal are visible to the CPU (Metal's
    // event contract, which the awaitable does not add to).  Time it out
    // with with_deadline; the Future is also a sender.
    //
    // The event is retained by the waiting frame, not by the notification.
    [[nodiscard]] coroutine::Future<>
    shared_event_wait(id<MTLSharedEvent> event, uint64_t value);

    // An owner that abandons an event (drops it, or stops signaling it)
    // while stopped waits may still be registered on it calls this first:
    // it signals the terminal value UINT64_MAX from the CPU, which fires
    // and so retires every leftover notification (each resumes nothing).
    // Metal keeps a notification as long as its event never reaches the
    // value, even after the last reference to the event goes (measured),
    // so without this a stopped wait's small state leaks.  An event whose
    // values only ever advance, signaled to the end, needs nothing.
    // Waits after abandonment complete at once.
    void shared_event_abandon(id<MTLSharedEvent> event);

    // Adds a completion handler that aborts the process if the command
    // buffer fails (status MTLCommandBufferStatusError: a GPU fault,
    // timeout or page fault).  Call before commit.  Whether a failed
    // command buffer still signals its encoded events is unverified, so a
    // wait on one could hang and its state leak; we treat a failed command
    // buffer as fatal rather than recover.
    void abort_on_command_buffer_error(id<MTLCommandBuffer> command_buffer);

} // namespace wry

#endif /* metal_event_hpp */
