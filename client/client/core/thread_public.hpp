//
//  thread_public.hpp
//  client
//
//  Created by Antony Searle on 7/7/2026.
//

#ifndef thread_public_hpp
#define thread_public_hpp

#include <cinttypes>

#include "garbage_collected.hpp"
#include "epoch_allocator.hpp"

namespace wry {

    // ThreadPublic -- per-thread heap object publishing debugging telemetry
    // to any other thread.
    //
    // Each participating thread owns one via a thread_local
    // Root<ThreadPublic*>, and the nodes form a concurrent circular
    // intrusive singly linked list through AtomicMarkedScanSlot links,
    // anchored by an immortal sentinel:
    //
    //   - a thread explicitly registers itself on creation (insert after
    //     the sentinel);
    //   - it explicitly marks its own node's link before it ends (Harris:
    //     mark == owner logically deleted; the link freezes);
    //   - iterators cooperatively unlink marked nodes, whose unlink CAS
    //     shades the displaced node (Yuasa), so concurrent iterators
    //     remain safe and the node is collected once unreachable.
    //
    // The ring is strongly traced from the sentinel (each node scans its
    // successor), so linked nodes are always live; unlinked nodes die a
    // cycle later.
    //
    // Field discipline: _name is written before publication and immutable
    // after.  The Atomic fields are written by the owning thread (relaxed,
    // at pin/repin/unpin) and read by anyone (relaxed); they are telemetry,
    // not synchronization.

    struct ThreadPublic final : GarbageCollected {

        // Ring link; mark means this node's thread has deregistered.
        mutable AtomicMarkedScanSlot<ThreadPublic*> _next;

        char _name[16];

        Atomic<bool>     _is_pinned;
        Atomic<uint16_t> _pinned_epoch;       // valid while _is_pinned
        Atomic<uint64_t> _pin_count;          // cumulative pins
        Atomic<uint64_t> _repin_count;        // cumulative repins
        Atomic<uint64_t> _unpin_count;        // cumulative unpins
        Atomic<int64_t>  _last_pin_time;      // steady_clock ns
        Atomic<int64_t>  _last_unpin_time;    // steady_clock ns
        Atomic<uint64_t> _allocated_bytes;    // via GarbageCollected::operator new
        Atomic<uint64_t> _allocated_objects;

        explicit ThreadPublic(const char* _Nonnull name);

        virtual void _garbage_collected_scan() const override;
        virtual void _garbage_collected_debug() const override;

    }; // struct ThreadPublic

    // The immortal ring anchor; created on first use (caller must be
    // pinned).  Never marked, never unlinked, all-zero telemetry.
    ThreadPublic* _Nonnull thread_public_ring_sentinel();

    // This thread's node, or nullptr if not (yet, still) registered.
    ThreadPublic* _Nullable thread_public_this_thread();

    // Both require the calling thread to be pinned (they allocate and
    // retire garbage collected objects).  Register once per thread, on
    // creation; deregister before the thread ends.
    void thread_public_register(const char* _Nonnull name);
    void thread_public_deregister();

    // Visit every live (unmarked) node once, cooperatively unlinking any
    // marked nodes encountered.  Requires the calling thread to be pinned.
    // Weakly consistent: concurrent registrations may be missed.  The
    // sentinel is not visited.
    template<typename F>
    void thread_public_for_each(F&& f) {
        assert(epoch::local_state.is_pinned);
        ThreadPublic* start = thread_public_ring_sentinel();
        ThreadPublic* pred = start;
        // The sentinel is never marked, so its link's mark bit is ignored.
        ThreadPublic* cur = pred->_next.load_acquire().ptr;
        while (cur != start) {
            auto next = cur->_next.load_acquire();
            if (next.marked) {
                // cur is retired.  Try to unlink it; the CAS shades the
                // displaced cur, keeping it (and its frozen _next) valid
                // for any concurrent iterator this cycle.
                typename AtomicMarkedScanSlot<ThreadPublic*>::MarkedPointer
                    expected{cur, false};
                if (pred->_next.compare_exchange_strong(expected,
                                                        {next.ptr, false})) {
                    cur = next.ptr;
                    continue;
                }
                // pred is itself retired, or changed under us; step through
                // cur without unlinking (reading a frozen marked link is
                // fine) and leave the unlink to an iterator with a live
                // predecessor.
            } else {
                f(*static_cast<ThreadPublic const*>(cur));
            }
            pred = cur;
            cur = next.ptr;
        }
    }

    // printf one line per live node: name, pinnedness, epoch, ages,
    // counters.  For the collector's stall diagnostics and tests.
    void thread_public_debug_dump();

    // Telemetry hooks for mutator_pin/mutator_repin/mutator_unpin.  No-ops
    // for unregistered threads.
    void _thread_public_note_pin();
    void _thread_public_note_repin();
    void _thread_public_note_unpin();

} // namespace wry

#endif /* thread_public_hpp */
