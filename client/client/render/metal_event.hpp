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

} // namespace wry

#endif /* metal_event_hpp */
