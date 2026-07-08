//
//  thread_public.cpp
//  client
//
//  Created by Antony Searle on 7/7/2026.
//

#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>

#include "thread_public.hpp"

#include "test.hpp"

namespace wry {

    constinit thread_local Root<ThreadPublic*> _thread_local_thread_public{};

    static int64_t _thread_public_now() {
        return std::chrono::steady_clock::now().time_since_epoch().count();
    }

    ThreadPublic::ThreadPublic(const char* name) {
        // operator new is calloc and the Atomic members zero-initialize;
        // only the name needs stamping.  The object is unpublished until
        // the registration CAS links it into the ring.
        snprintf(_name, sizeof _name, "%s", name);
    }

    void ThreadPublic::_garbage_collected_scan() const {
        // The ring successor is a strong edge: linked nodes keep their
        // (possibly retired) successors alive until unlinked.
        garbage_collected_scan(_next);
    }

    void ThreadPublic::_garbage_collected_debug() const {
        printf("ThreadPublic \"%s\"\n", _name);
    }

    ThreadPublic* thread_public_ring_sentinel() {
        // First caller must be pinned: we allocate.  The function-local
        // static Root keeps the sentinel rooted for the life of the
        // process; it is never marked, so never unlinked.
        assert(epoch::local_state.is_pinned);
        static Root<ThreadPublic*> _sentinel{[] {
            ThreadPublic* s = new ThreadPublic("(ring)");
            s->_next.nonatomic_store(s, false); // self-loop, pre-publication
            return s;
        }()};
        return _sentinel._ptr;
    }

    ThreadPublic* thread_public_this_thread() {
        return _thread_local_thread_public._ptr;
    }

    void thread_public_register(const char* name) {
        assert(epoch::local_state.is_pinned);
        assert(!_thread_local_thread_public);
        ThreadPublic* s = thread_public_ring_sentinel();
        ThreadPublic* node = new ThreadPublic(name);
        node->_is_pinned.store_relaxed(true);
        node->_pinned_epoch.store_relaxed(epoch::local_state.known.raw);
        node->_last_pin_time.store_relaxed(_thread_public_now());
        // Insert after the sentinel.  The node is unpublished until the
        // CAS succeeds, so the plain store of its link is race-free; the
        // sentinel is never marked, so the CAS fails only on a competing
        // insert (or an unlink of the current head).
        auto expected = s->_next.load_acquire();
        for (;;) {
            node->_next.nonatomic_store(expected.ptr, false);
            if (s->_next.compare_exchange_strong(expected, {node, false}))
                break;
        }
        _thread_local_thread_public = node;
    }

    void thread_public_deregister() {
        assert(epoch::local_state.is_pinned);
        ThreadPublic* node = _thread_local_thread_public._ptr;
        assert(node);
        // Final stamps: after this call the pin/unpin hooks can no longer
        // see the node, so leave it showing unpinned rather than stuck
        // showing pinned forever.
        node->_is_pinned.store_relaxed(false);
        node->_last_unpin_time.store_relaxed(_thread_public_now());
        // Mark our own link (Harris: the link freezes; iterators unlink
        // us).  Only the owner marks, but the link can concurrently change
        // as iterators unlink our retired successor, so CAS in a loop.
        auto expected = node->_next.load_acquire();
        for (;;) {
            assert(!expected.marked); // double-deregister
            if (node->_next.compare_exchange_strong(expected,
                                                    {expected.ptr, true}))
                break;
        }
        // Drop the root; the ring holds the node until it is unlinked.
        _thread_local_thread_public = nullptr;
    }

    void thread_public_debug_dump() {
        int64_t now = _thread_public_now();
        printf("TP: ---------------- thread ring ----------------\n");
        thread_public_for_each([now](ThreadPublic const& tp) {
            bool pinned = tp._is_pinned.load_relaxed();
            int64_t since = pinned ? tp._last_pin_time.load_relaxed()
                                   : tp._last_unpin_time.load_relaxed();
            printf("TP: %-15s %s epoch=%04x for %.3fs"
                   " pins=%" PRIu64 " repins=%" PRIu64
                   " allocated=%" PRIu64 "B/%" PRIu64 "\n",
                   tp._name,
                   pinned ? "PINNED  " : "unpinned",
                   (unsigned)tp._pinned_epoch.load_relaxed(),
                   (now - since) * 1e-9,
                   tp._pin_count.load_relaxed(),
                   tp._repin_count.load_relaxed(),
                   tp._allocated_bytes.load_relaxed(),
                   tp._allocated_objects.load_relaxed());
        });
    }

    // Telemetry hooks, called from mutator_pin/repin/unpin.  Relaxed
    // stores by the owning thread; anyone may read.  All run while the
    // thread is pinned (unpin's hook runs before the epoch is released).

    static void _thread_public_mirror_allocation(ThreadPublic* tp) {
        tp->_allocated_bytes.store_relaxed(_thread_local_gc_allocated_bytes);
        tp->_allocated_objects.store_relaxed(_thread_local_gc_allocated_objects);
    }

    void _thread_public_note_pin() {
        if (ThreadPublic* tp = thread_public_this_thread()) {
            tp->_pin_count.fetch_add_relaxed(1);
            tp->_pinned_epoch.store_relaxed(epoch::local_state.known.raw);
            tp->_last_pin_time.store_relaxed(_thread_public_now());
            tp->_is_pinned.store_relaxed(true);
            _thread_public_mirror_allocation(tp);
        }
    }

    void _thread_public_note_repin() {
        if (ThreadPublic* tp = thread_public_this_thread()) {
            tp->_repin_count.fetch_add_relaxed(1);
            tp->_pinned_epoch.store_relaxed(epoch::local_state.known.raw);
            tp->_last_pin_time.store_relaxed(_thread_public_now());
            _thread_public_mirror_allocation(tp);
        }
    }

    void _thread_public_note_unpin() {
        if (ThreadPublic* tp = thread_public_this_thread()) {
            tp->_unpin_count.fetch_add_relaxed(1);
            tp->_last_unpin_time.store_relaxed(_thread_public_now());
            tp->_is_pinned.store_relaxed(false);
            _thread_public_mirror_allocation(tp);
        }
    }


    // Runs on a worker thread, which registered at service start and is
    // pinned by the drain loop.
    define_test("thread_public") {

        assert(epoch::local_state.is_pinned);
        ThreadPublic* self = thread_public_this_thread();
        assert(self);

        // We can find ourself, live and pinned.
        {
            int live = 0;
            bool found_self = false;
            thread_public_for_each([&](ThreadPublic const& tp) {
                ++live;
                if (&tp == self) {
                    found_self = true;
                    assert(tp._is_pinned.load_relaxed());
                }
            });
            assert(found_self);
            printf("thread_public: %d live threads\n", live);
            thread_public_debug_dump();
        }

        // Churn: threads register, see themselves, deregister.  After the
        // joins, no churn node may be visited (join gives happens-before,
        // so every mark is visible), and iteration unlinks them in passing.
        {
            // Deliberately hot: ~60ms of 8 threads pinning/unpinning as
            // fast as possible drives the epoch at full rate while other
            // tests (notably the parallel rebuilds) run concurrently.
            // This is the regression tripwire that catches pin-boundary
            // bugs like the drain-loop cadence use-after-poison; see the
            // comment in global_work_queue_service.
            constexpr int N = 8;
            constexpr int LAPS = 2500;
            std::vector<std::thread> threads;
            for (int i = 0; i != N; ++i) {
                threads.emplace_back([i] {
                    char name[16];
                    snprintf(name, sizeof name, "tp-churn-%d", i);
                    for (int lap = 0; lap != LAPS; ++lap) {
                        mutator_pin();
                        thread_public_register(name);
                        ThreadPublic* me = thread_public_this_thread();
                        assert(me);
                        bool found = false;
                        thread_public_for_each([&](ThreadPublic const& tp) {
                            if (&tp == me)
                                found = true;
                        });
                        assert(found);
                        mutator_repin();
                        thread_public_deregister();
                        mutator_unpin();
                    }
                });
            }
            for (auto& t : threads)
                t.join();
            int stragglers = 0;
            thread_public_for_each([&](ThreadPublic const& tp) {
                if (!strncmp(tp._name, "tp-churn-", 9))
                    ++stragglers;
            });
            assert(stragglers == 0);
        }

        co_return;
    };

} // namespace wry
