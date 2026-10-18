//
//  coroutine_test.hpp
//  client
//
//  Created by Antony Searle on 18/10/2026.
//
//  Test support shared by the coroutine, nursery, execution and queue
//  tests: a fence to host a task under a test's own stop token, and a
//  frame-local probe that counts destructions.
//

#ifndef coroutine_test_hpp
#define coroutine_test_hpp

#include <atomic>
#include <stop_token>

#include "coroutine.hpp"

namespace wry::coroutine {

    // Unwinding exercise.  A cancelled leaf destroys its own frame; each
    // ~Promise in the continuation chain then destroys its caller's frame in
    // turn, running that frame's local destructors, until the chain bottoms
    // out at a two-word fake frame (completion arrives through word 0,
    // cancellation through word 1).
    //
    // The fence below is such a fake frame, standing in for the scope so the
    // cascade never reaches the test's own frame.  The whole cascade runs
    // synchronously on the thread that resumed the leaf; the fence store is a
    // release paired with the polling acquire, so the probe counts written
    // during the unwind are visible to the asserts.

    // A top-level host's delegate: records whether the task completed
    // (1) or was unwound (2), and lends it the test's stop token
    struct UnwindFence : Delegate<> {
        std::atomic<int> _outcome{0};  // 0 pending, 1 completed, 2 cancelled
        std::stop_token _stop_token;
        virtual std::coroutine_handle<> final_await_suspend() noexcept override {
            _outcome.store(1, std::memory_order_release);
            return std::noop_coroutine();
        }
        virtual void unhandled_stopped() noexcept override {
            _outcome.store(2, std::memory_order_release);
        }
        virtual std::stop_token get_stop_token() noexcept override {
            return _stop_token;
        }
    };

    // Frame-local sentinel: its destructor must run exactly once whether
    // the frame completes or is unwound.
    struct UnwindProbe {
        std::atomic<int>* _counter;
        ~UnwindProbe() { _counter->fetch_add(1, std::memory_order_relaxed); }
    };

} // namespace wry::coroutine

#endif /* coroutine_test_hpp */
