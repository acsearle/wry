//
//  functional.cpp
//  client
//
//  Created by Antony Searle on 11/9/2026.
//

#include <array>
#include <atomic>

#include "functional.hpp"
#include "test.hpp"
#include "utility.hpp"

namespace wry {

    namespace {

        // Move-nulling probe: its destructor counts exactly once per object
        struct Probe {
            std::atomic<int>* _destroyed;
            explicit Probe(std::atomic<int>* destroyed) : _destroyed(destroyed) {}
            Probe(Probe const&) = delete;
            Probe(Probe&& other) noexcept : _destroyed(take(other._destroyed)) {}
            ~Probe() {
                if (_destroyed)
                    _destroyed->fetch_add(1, std::memory_order_relaxed);
            }
        };

        struct alignas(64) OverAligned {
            int value;
        };

    }

    define_test("move_only_function") {

        static_assert(sizeof(move_only_function<void()>) == 3 * sizeof(void*));

        // empty
        {
            move_only_function<void()> f;
            assert(!f);
            assert(f == nullptr);
            move_only_function<int(int)> g = nullptr;
            assert(!g);
        }

        // small callable, in place: result and argument
        {
            int base = 40;
            move_only_function<int(int)> f = [base](int x) { return base + x; };
            assert(f);
            assert(f(2) == 42);
        }

        // large callable, boxed on the heap: callable, destroyed exactly once
        {
            std::atomic<int> destroyed{0};
            {
                move_only_function<int()> f = [probe = Probe{&destroyed},
                                               pad = std::array<char, 64>{}]() mutable {
                    return (int)pad.size();
                };
                assert(f() == 64);
                assert(destroyed.load() == 0);
            }
            assert(destroyed.load() == 1);
        }

        // over-aligned callable: boxed
        {
            move_only_function<int()> f = [v = OverAligned{7}]() { return v.value; };
            assert(f() == 7);
        }

        // move: the destination is callable, the source is empty, the probe
        // survives the relocation and dies once, on reset
        {
            std::atomic<int> destroyed{0};
            std::atomic<int> ran{0};
            move_only_function<void()> a = [probe = Probe{&destroyed}, &ran]() mutable {
                ran.fetch_add(1, std::memory_order_relaxed);
            };
            move_only_function<void()> b = std::move(a);
            assert(!a);
            assert(b);
            b();
            assert(ran.load() == 1);
            assert(destroyed.load() == 0);
            b = nullptr;
            assert(!b);
            assert(destroyed.load() == 1);
        }

        // move assignment over a live callable destroys the old one
        {
            std::atomic<int> d1{0};
            std::atomic<int> d2{0};
            move_only_function<void()> a = [p = Probe{&d1}]() {};
            move_only_function<void()> b = [p = Probe{&d2}]() {};
            a = std::move(b);
            assert(d1.load() == 1);
            assert(d2.load() == 0);
            assert(!b);
            assert(a);
        }

        // assignment from a callable; swap
        {
            move_only_function<int()> a = []() { return 1; };
            a = []() { return 2; };
            assert(a() == 2);
            move_only_function<int()> b = []() { return 3; };
            swap(a, b);
            assert(a() == 3);
            assert(b() == 2);
        }

        // void result discards the callable's; reference argument
        {
            int n = 0;
            move_only_function<void(int&)> f = [](int& x) { x += 5; return 99; };
            f(n);
            assert(n == 5);
        }

        // a move-only callable dropped unrun still destroys exactly once
        {
            std::atomic<int> destroyed{0};
            {
                move_only_function<void()> f = [p = Probe{&destroyed}]() {};
                (void) f;
            }
            assert(destroyed.load() == 1);
        }

        co_return;
    };

} // namespace wry
