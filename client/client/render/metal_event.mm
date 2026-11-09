//
//  metal_event.mm
//  client
//
//  Coroutines that wait on an MTLSharedEvent; see metal_event.hpp.
//

#include <atomic>
#include <cstdio>
#include <cstring>
#include <memory>
#include <optional>
#include <stop_token>

#include <dispatch/dispatch.h>

#if defined(__has_feature)
#if __has_feature(thread_sanitizer)
#define WRY_TSAN 1
#include <sanitizer/tsan_interface.h>
#endif
#endif

#include "metal_event.hpp"

#include "kqueue_reactor.hpp"
#include "nursery.hpp"
#include "test.hpp"

namespace wry {

    // ---- Design -------------------------------------------------------------
    //
    // Two ways to learn that an event reached a value: Metal calls a block
    // (notifyListener:atValue:block:, on the dispatch queue of an
    // MTLSharedEventListener), or someone polls signaledValue.  Polling needs
    // a poller: the frame loop (not running headless, nor during a long
    // load) or a reactor timer (latency = the period, and work at idle).  The
    // block has neither problem; its cost is that a registered notification
    // cannot be withdrawn, so a stopped wait leaves its block registered.
    // We take the block, and keep what it holds small: a WaitState, never
    // the coroutine.
    //
    // The kqueue reactor cannot help: an MTLSharedEvent exposes no kernel
    // object (no Mach port or descriptor) to put a knote on.
    //
    // ---- Who resumes --------------------------------------------------------
    //
    // A WaitState is shared (std::shared_ptr) by await_suspend and the
    // notification block; it holds the continuation, a stop_callback on the
    // awaiting chain's token, and a claim word.  Exactly one of two parties
    // disposes of the continuation:
    //
    //   - stop: the stop_callback (on the requesting thread, inside
    //     request_stop, or inline in emplace when the token is already
    //     requested) sets STOPPED and destroys the continuation there --
    //     cancellation as destruction, as the reactor's leaves do.
    //
    //   - fire: the block (on the listener's queue) FIRST resets the
    //     stop_callback, THEN sets FIRED; if STOPPED was not set it schedules
    //     the continuation on the pool.  Never resumes or destroys a frame
    //     itself: the listener queue is a foreign thread like the reactor.
    //
    // ORDER.  ~stop_callback either deregisters a callback that has not
    // started (it then never runs) or, if the callback is running on another
    // thread, blocks until it returns, and that return synchronizes-with the
    // destructor's return (libc++: the invoking thread's release store of
    // the callback's completion flag, the destructor's acquire wait on it).
    // So after the reset the callback has either run to completion -- its
    // STOPPED RMW, and its take of the continuation, happen-before the
    // block's fetch_or -- or will never run.  The claim word's RMWs could
    // be relaxed on that edge alone; they are acq_rel as a second witness
    // that ThreadSanitizer can see.  Consequently the callback can never
    // observe FIRED (asserted).
    //
    // The continuation is written before the notification is registered;
    // libdispatch orders the registration before the block (the notifier
    // dispatches it to the listener's queue), so the block reads it safely.
    // await_suspend registers only after emplacing the stop_callback, so
    // the block's reset never races the emplace.
    //
    // After the emplace, await_suspend touches only locals and the
    // WaitState: a stop may already have destroyed the awaiting frame, and
    // the awaitable (and its strong reference to the event) with it.  If it
    // sees STOPPED it skips the registration (nothing left to notify).
    //
    // LIFETIME.  The block captures a shared_ptr<WaitState>, so the state
    // dies when Metal releases the block, whether or not it ran.  ~WaitState
    // touches no GC object and resumes nothing, so it may run on the
    // listener's queue.  Not cancelled, the parked frame retains the event
    // through the awaitable, so the block runs (or the event outlives the
    // wait forever); ~WaitState holding the continuation is therefore
    // unreachable, and asserts.  Release builds hand it to the pool to
    // destroy rather than strand it.
    //
    // MEASURED (macOS 27, M1 Max, 2026-11-08): releasing every reference to
    // an event with a notification still pending does NOT release the
    // block (its WaitState was still live a second later; presumably the
    // listener keeps the event and its notifications).  So a stopped wait
    // on an event that never reaches the value leaks its WaitState, and
    // perhaps the event.  An owner that abandons an event with stopped
    // waiters can retire them by signaling a terminal value (UINT64_MAX)
    // first; a timeline that only ever advances needs nothing.

    // (shared_event_abandon is that, in the header's contract.)

    // Test observability: live WaitStates (one relaxed add, one release sub)
    constinit std::atomic<std::ptrdiff_t> g_shared_event_wait_state_count{0};

    namespace {

        MTLSharedEventListener* shared_event_listener() {
            // A serial queue: the blocks are short (a deregistration and a
            // schedule), and an order among them costs nothing.  A block can
            // wait inside ~stop_callback for a requester that is destroying
            // a coroutine tower, which delays the blocks queued behind it;
            // tolerable for now (see docs/todo.md)
            static MTLSharedEventListener* listener = [] {
                dispatch_queue_t queue = dispatch_queue_create("wry.metal-event",
                                                               DISPATCH_QUEUE_SERIAL);
                return [[MTLSharedEventListener alloc] initWithDispatchQueue:queue];
            }();
            return listener;
        }

        struct WaitState {

            enum : uint32_t {
                STOPPED = 1,
                FIRED = 2,
            };

            struct Callback {
                WaitState* _state;
                void operator()() noexcept {
                    uint32_t before = _state->_claim.fetch_or(STOPPED, std::memory_order_acq_rel);
                    // The block resets this callback before it claims
                    assert(!(before & FIRED));
                    assert(!(before & STOPPED));
                    (void) before;
                    // Cancellation unwinding, on the requesting thread.  The
                    // stop_callback (inside *_state) outlives this call: the
                    // block's reset waits for it, and await_suspend's
                    // shared_ptr is not dropped by anything this destroys
                    take(_state->_continuation).destroy();
                }
            };

            std::coroutine_handle<> _continuation;
            std::atomic<uint32_t> _claim{0};
            std::optional<std::stop_callback<Callback>> _stop_callback;

            explicit WaitState(std::coroutine_handle<> continuation)
            : _continuation(continuation) {
                g_shared_event_wait_state_count.fetch_add(1, std::memory_order_relaxed);
            }

            WaitState(WaitState const&) = delete;

            ~WaitState() {
                // Waits for a callback still running on another thread
                _stop_callback.reset();
                if (_continuation) {
                    // Unreachable (see LIFETIME above)
                    assert(false && "shared_event_wait: notification dropped unrun");
                    global_work_queue_schedule_destroy_by_address(take(_continuation).address());
                }
                // Pairs with the tests' acquire loads: the destruction
                // happens-before a test that sees the count drop
                g_shared_event_wait_state_count.fetch_sub(1, std::memory_order_release);
            }

            // The notification block's body: the value was reached
            void fire() noexcept {
                _stop_callback.reset();
                uint32_t before = _claim.fetch_or(FIRED, std::memory_order_acq_rel);
                assert(!(before & FIRED));
                if (!(before & STOPPED))
                    global_work_queue_schedule(take(_continuation));
            }

        };

        struct SharedEventAwaitable {

            id<MTLSharedEvent> _event;
            uint64_t _value;

            bool await_ready() const noexcept {
                return _event.signaledValue >= _value;
            }

            template<typename OuterPromise>
            void await_suspend(std::coroutine_handle<OuterPromise> continuation) {
                std::stop_token stop_token = get_stop_token(continuation);
                // Locals: past the emplace, *this may be gone
                id<MTLSharedEvent> event = _event;
                uint64_t value = _value;
                auto state = std::make_shared<WaitState>(continuation);
                // Runs the callback inline if the token is already requested
                state->_stop_callback.emplace(std::move(stop_token), WaitState::Callback{state.get()});
                if (state->_claim.load(std::memory_order_relaxed) & WaitState::STOPPED)
                    return;
                // A value reached between await_ready and here is fine: Metal
                // calls a block registered at or below signaledValue at once.
                //
                // ThreadSanitizer cannot see the registration-to-block edge
                // (Metal reaches the listener queue through an IOSurface
                // notification callback), so it is annotated on the state's
                // address, as the reactor annotates its knotes: the block is
                // copied to the heap (ARC, on assignment) before the
                // release, and reads the key, a plain captured pointer,
                // before anything else.  Nothing in other builds.
                [[maybe_unused]] void* key = state.get();
                void (^block)(id<MTLSharedEvent>, uint64_t) = ^(id<MTLSharedEvent>, uint64_t) {
#ifdef WRY_TSAN
                    __tsan_acquire(key);
#endif
                    state->fire();
                };
#ifdef WRY_TSAN
                __tsan_release(key);
#endif
                [event notifyListener:shared_event_listener()
                              atValue:value
                                block:block];
            }

            void await_resume() const noexcept {}

        };

    } // namespace

    coroutine::Future<> shared_event_wait(id<MTLSharedEvent> event, uint64_t value) {
        co_await SharedEventAwaitable{event, value};
    }

    void shared_event_abandon(id<MTLSharedEvent> event) {
        event.signaledValue = UINT64_MAX;
    }

    void abort_on_command_buffer_error(id<MTLCommandBuffer> command_buffer) {
        [command_buffer addCompletedHandler:^(id<MTLCommandBuffer> buffer) {
            if (buffer.status == MTLCommandBufferStatusError) {
                fprintf(stderr, "command buffer \"%s\" failed: %s\n",
                        buffer.label ? buffer.label.UTF8String : "",
                        buffer.error ? buffer.error.localizedDescription.UTF8String : "(no error)");
                abort();
            }
        }];
    }

    // ---- Tests ----------------------------------------------------------------

    namespace {

        using namespace std::chrono;

        struct MetalEventFixture {
            id<MTLDevice> device = MTLCreateSystemDefaultDevice();
            id<MTLCommandQueue> queue = [device newCommandQueue];
            id<MTLSharedEvent> event = [device newSharedEvent];
        };

        // Signal `value` from the GPU: an otherwise empty command buffer
        void gpu_signal(MetalEventFixture const& f, uint64_t value) {
            id<MTLCommandBuffer> command_buffer = [f.queue commandBuffer];
            [command_buffer encodeSignalEvent:f.event value:value];
            abort_on_command_buffer_error(command_buffer);
            [command_buffer commit];
        }

        bool on_listener_queue() {
            char const* label = dispatch_queue_get_label(DISPATCH_CURRENT_QUEUE_LABEL);
            return label && !std::strcmp(label, "wry.metal-event");
        }

        // The wait states of this file's tests are the only ones in the
        // process (shared_event_wait has no other caller yet), and the suite
        // is serial, so the count is ours.  Bounded: a second is a failure
        coroutine::Future<> wait_states_drained() {
            auto t0 = steady_clock::now();
            while ((g_shared_event_wait_state_count.load(std::memory_order_acquire) != 0)
                   && (steady_clock::now() - t0 < seconds(1)))
                co_await coroutine::TransferToPoolExecutor{};
            assert(g_shared_event_wait_state_count.load(std::memory_order_acquire) == 0);
        }

        struct EventProbe {
            std::atomic<int>* _counter;
            ~EventProbe() { _counter->fetch_add(1, std::memory_order_relaxed); }
        };

        coroutine::Task event_child(id<MTLSharedEvent> event,
                                    uint64_t value,
                                    std::atomic<int>* started,
                                    std::atomic<int>* destroyed,
                                    std::atomic<int>* resumed_past) {
            EventProbe probe{destroyed};
            started->fetch_add(1, std::memory_order_release);
            co_await shared_event_wait(event, value);
            assert(!on_listener_queue());
            resumed_past->fetch_add(1, std::memory_order_relaxed);
        }

    } // namespace

    define_test("metal_event_cpu_signal") {
        MetalEventFixture f;
        // Already reached: no suspension, no state
        f.event.signaledValue = 5;
        co_await shared_event_wait(f.event, 3);
        co_await shared_event_wait(f.event, 5);
        assert(g_shared_event_wait_state_count.load(std::memory_order_acquire) == 0);

        // Reached later, from the CPU on another coroutine
        std::atomic<int> started{0}, destroyed{0}, resumed_past{0};
        coroutine::Nursery nursery;
        nursery.soon(event_child(f.event, 6, &started, &destroyed, &resumed_past));
        while (started.load(std::memory_order_acquire) != 1)
            co_await coroutine::TransferToPoolExecutor{};
        co_await cancelable_sleep(0.005);
        assert(resumed_past.load(std::memory_order_relaxed) == 0);
        f.event.signaledValue = 6;
        std::ptrdiff_t cancelled = co_await nursery.join();
        assert(cancelled == 0);
        assert(resumed_past.load(std::memory_order_relaxed) == 1);
        assert(destroyed.load(std::memory_order_relaxed) == 1);
        co_await wait_states_drained();
    };

    define_test("metal_event_gpu_signal") {
        MetalEventFixture f;
        // A readback: the GPU fills a shared buffer, then signals; once the
        // wait resumes the bytes are visible here
        id<MTLBuffer> buffer = [f.device newBufferWithLength:256
                                                     options:MTLResourceStorageModeShared];
        std::memset(buffer.contents, 0, 256);
        id<MTLCommandBuffer> command_buffer = [f.queue commandBuffer];
        id<MTLBlitCommandEncoder> blit = [command_buffer blitCommandEncoder];
        [blit fillBuffer:buffer range:NSMakeRange(0, 256) value:0xAB];
        [blit endEncoding];
        [command_buffer encodeSignalEvent:f.event value:1];
        abort_on_command_buffer_error(command_buffer);
        [command_buffer commit];
        auto r = co_await with_deadline(steady_clock::now() + seconds(30),
                                        shared_event_wait(f.event, 1));
        assert(r.has_value());
        assert(!on_listener_queue());
        unsigned char const* p = (unsigned char const*)buffer.contents;
        for (int i = 0; i != 256; ++i)
            assert(p[i] == 0xAB);
        (void) p;

        // A run of waits, each on the next GPU signal
        for (uint64_t value = 2; value != 34; ++value) {
            gpu_signal(f, value);
            auto s = co_await with_deadline(steady_clock::now() + seconds(30),
                                            shared_event_wait(f.event, value));
            assert(s.has_value());
        }
        co_await wait_states_drained();
    };

    define_test("metal_event_deadline") {
        MetalEventFixture f;
        // Never signaled: the deadline stops the wait, the outer resumes
        // with nullopt, and the registered notification is left behind
        auto t0 = steady_clock::now();
        auto r = co_await with_deadline(steady_clock::now() + milliseconds(20),
                                        shared_event_wait(f.event, 1));
        assert(!r.has_value());
        assert(steady_clock::now() - t0 < seconds(10));
        assert(g_shared_event_wait_state_count.load(std::memory_order_acquire) == 1);
        // A late signal fires the leftover block, which resumes nothing
        f.event.signaledValue = 1;
        co_await wait_states_drained();
    };

    define_test("metal_event_abandon") {
        MetalEventFixture f;
        // Stopped waits on values the event will never reach leave their
        // notifications behind; abandoning the event retires them all
        for (uint64_t value : {10ull, 1000ull, UINT64_MAX - 1}) {
            auto r = co_await with_deadline(steady_clock::now() + milliseconds(5),
                                            shared_event_wait(f.event, value));
            assert(!r.has_value());
        }
        assert(g_shared_event_wait_state_count.load(std::memory_order_acquire) == 3);
        shared_event_abandon(f.event);
        co_await wait_states_drained();
        // and a wait after abandonment does not suspend
        co_await shared_event_wait(f.event, UINT64_MAX);
        assert(g_shared_event_wait_state_count.load(std::memory_order_acquire) == 0);
    };

    define_test("metal_event_cancel_before_park") {
        MetalEventFixture f;
        std::atomic<int> started{0}, destroyed{0}, resumed_past{0};
        coroutine::Nursery nursery;
        nursery.request_stop();
        nursery.soon(event_child(f.event, 1, &started, &destroyed, &resumed_past));
        std::ptrdiff_t cancelled = co_await nursery.join();
        assert(cancelled == 1);
        assert(started.load(std::memory_order_relaxed) == 1);
        assert(destroyed.load(std::memory_order_relaxed) == 1);
        assert(resumed_past.load(std::memory_order_relaxed) == 0);
        // Stopped before registering: nothing was left with Metal
        co_await wait_states_drained();
    };

    define_test("metal_event_cancel_parked") {
        MetalEventFixture f;
        std::atomic<int> started{0}, destroyed{0}, resumed_past{0};
        coroutine::Nursery nursery;
        nursery.soon(event_child(f.event, 1, &started, &destroyed, &resumed_past));
        while (started.load(std::memory_order_acquire) != 1)
            co_await coroutine::TransferToPoolExecutor{};
        co_await cancelable_sleep(0.005);
        nursery.request_stop();
        std::ptrdiff_t cancelled = co_await nursery.join();
        assert(cancelled == 1);
        assert(destroyed.load(std::memory_order_relaxed) == 1);
        assert(resumed_past.load(std::memory_order_relaxed) == 0);
        // The notification outlives the stop; the signal retires it
        gpu_signal(f, 1);
        co_await wait_states_drained();
        assert(resumed_past.load(std::memory_order_relaxed) == 0);
    };

    define_test("metal_event_fire_stop_race") {
        // GPU signal against stop request, landing close together: exactly
        // one of resume or destroy per wait, and every state retires
        MetalEventFixture f;
        int completed = 0;
        int stopped = 0;
        for (uint64_t value = 1; value != 201; ++value) {
            std::atomic<int> started{0}, destroyed{0}, resumed_past{0};
            coroutine::Nursery nursery;
            nursery.soon(event_child(f.event, value, &started, &destroyed, &resumed_past));
            while (started.load(std::memory_order_acquire) != 1)
                co_await coroutine::TransferToPoolExecutor{};
            // Sweep the stop across the signal's latency (GPU ~1 ms here,
            // CPU tens of microseconds), alternating the signaller
            if (value & 1)
                gpu_signal(f, value);
            else
                f.event.signaledValue = value;
            if (value % 3)
                co_await cancelable_sleep((double)(value % 50) * 20e-6);
            nursery.request_stop();
            std::ptrdiff_t cancelled = co_await nursery.join();
            int resumed = resumed_past.load(std::memory_order_relaxed);
            assert(destroyed.load(std::memory_order_relaxed) == 1);
            assert(resumed + cancelled == 1);
            completed += resumed;
            stopped += (int) cancelled;
        }
        co_await wait_states_drained();
        printf("[metal_event_fire_stop_race] %d completed, %d stopped\n", completed, stopped);
    };

} // namespace wry
