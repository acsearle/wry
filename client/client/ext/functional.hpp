//
//  functional.hpp
//  client
//
//  Created by Antony Searle on 11/9/2026.
//

#ifndef functional_hpp
#define functional_hpp

#include <cstddef>
#include <cstring>
#include <functional>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>

#include "assert.hpp"
#include "utility.hpp"

namespace wry {

    // A move-only type-erased callable with a small buffer, shaped like the
    // shipping std::move_only_function implementations: one pointer to a
    // static per-type table of operations plus an in-place buffer of two
    // words, 24 bytes in all.  A callable is stored in place when it fits,
    // is not over-aligned and is nothrow move-constructible; otherwise it
    // lives on the heap behind a box that is itself stored in place.
    // Moving relocates the stored object with its own move constructor
    // (memcpy only for trivially copyable types); there is no notion of
    // trivial relocatability.
    //
    // Implements the subset of std::move_only_function we use: the plain
    // R(Args...) signature only (no cv/ref/noexcept-qualified forms) and no
    // in_place_type constructor.  Calling an empty one is undefined in the
    // standard; here it asserts.  Retire this in favor of the standard type
    // when libc++ ships it (its <version> entry is still commented out).

    template<typename Signature>
    class move_only_function;

    template<typename R, typename... Args>
    class move_only_function<R(Args...)> {

        struct Ops {
            R (*invoke)(void*, Args&&...);
            void (*relocate)(void* to, void* from) noexcept;  // move-construct into to, destroy from
            void (*destroy)(void*) noexcept;
        };

        static constexpr std::size_t buffer_size = 2 * sizeof(void*);
        static constexpr std::size_t buffer_align = alignof(void*);

        // X is what actually lives in the buffer: the callable, or a Boxed<G>
        template<typename X>
        struct Storage {
            static X* at(void* p) noexcept {
                return std::launder(reinterpret_cast<X*>(p));
            }
            static R invoke(void* p, Args&&... args) {
                return std::invoke_r<R>(*at(p), std::forward<Args>(args)...);
            }
            static void relocate(void* to, void* from) noexcept {
                if constexpr (std::is_trivially_copyable_v<X>) {
                    std::memcpy(to, from, sizeof(X));
                } else {
                    std::construct_at(reinterpret_cast<X*>(to), std::move(*at(from)));
                    std::destroy_at(at(from));
                }
            }
            static void destroy(void* p) noexcept {
                std::destroy_at(at(p));
            }
            static constexpr Ops ops = { &invoke, &relocate, &destroy };
        };

        // Large, over-aligned, or throwing-move callables live on the heap;
        // the box is what sits in the buffer
        template<typename G>
        struct Boxed {
            G* _p;
            explicit Boxed(G* p) noexcept : _p(p) {}
            Boxed(Boxed const&) = delete;
            Boxed(Boxed&& other) noexcept : _p(take(other._p)) {}
            ~Boxed() { delete _p; }
            template<typename... A>
            decltype(auto) operator()(A&&... a) {
                return std::invoke(*_p, std::forward<A>(a)...);
            }
        };

        template<typename G>
        static constexpr bool stored_in_place
            = (sizeof(G) <= buffer_size)
            && (alignof(G) <= buffer_align)
            && std::is_nothrow_move_constructible_v<G>;

        template<typename X, typename... A>
        void _emplace(A&&... a) {
            static_assert(stored_in_place<X>);
            std::construct_at(reinterpret_cast<X*>(_storage), std::forward<A>(a)...);
            _ops = &Storage<X>::ops;
        }

        void _reset() noexcept {
            if (Ops const* ops = take(_ops))
                ops->destroy(_storage);
        }

        void _take(move_only_function& other) noexcept {
            if (other._ops) {
                other._ops->relocate(_storage, other._storage);
                _ops = take(other._ops);
            }
        }

        Ops const* _ops = nullptr;  // null is empty
        alignas(buffer_align) std::byte _storage[buffer_size];

    public:

        using result_type = R;

        move_only_function() noexcept = default;
        move_only_function(std::nullptr_t) noexcept {}

        template<typename F, typename G = std::decay_t<F>>
        requires (!std::is_same_v<G, move_only_function>)
              && std::is_constructible_v<G, F>
              && std::is_invocable_r_v<R, G, Args...>
              && std::is_invocable_r_v<R, G&, Args...>
        move_only_function(F&& f) {
            if constexpr (stored_in_place<G>)
                _emplace<G>(std::forward<F>(f));
            else
                _emplace<Boxed<G>>(new G(std::forward<F>(f)));
        }

        move_only_function(move_only_function const&) = delete;
        move_only_function& operator=(move_only_function const&) = delete;

        move_only_function(move_only_function&& other) noexcept {
            _take(other);
        }

        move_only_function& operator=(move_only_function&& other) noexcept {
            if (this != &other) {
                _reset();
                _take(other);
            }
            return *this;
        }

        template<typename F, typename G = std::decay_t<F>>
        requires (!std::is_same_v<G, move_only_function>)
              && std::is_constructible_v<G, F>
              && std::is_invocable_r_v<R, G, Args...>
              && std::is_invocable_r_v<R, G&, Args...>
        move_only_function& operator=(F&& f) {
            move_only_function(std::forward<F>(f)).swap(*this);
            return *this;
        }

        move_only_function& operator=(std::nullptr_t) noexcept {
            _reset();
            return *this;
        }

        ~move_only_function() {
            _reset();
        }

        void swap(move_only_function& other) noexcept {
            move_only_function temp(std::move(other));
            other = std::move(*this);
            *this = std::move(temp);
        }

        friend void swap(move_only_function& a, move_only_function& b) noexcept {
            a.swap(b);
        }

        explicit operator bool() const noexcept {
            return _ops != nullptr;
        }

        friend bool operator==(move_only_function const& f, std::nullptr_t) noexcept {
            return !f;
        }

        R operator()(Args... args) {
            assert(_ops && "calling an empty move_only_function");
            return _ops->invoke(_storage, std::forward<Args>(args)...);
        }

    };

} // namespace wry

#endif /* functional_hpp */
