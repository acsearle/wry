//
//  Option.hpp
//  client
//
//  Created by Antony Searle on 30/9/2023.
//

#ifndef Option_hpp
#define Option_hpp

#include <cassert>

#include "type_traits.hpp"
#include "utility.hpp"

namespace rust::option {
    
    struct None;
    template<typename> struct Some;
    template<typename> struct Option;
        
    struct None {};
    
    template<typename T>
    struct Some {
        
        T value;
        
        Some() = default;
        Some(const Some&) = default;
        Some(Some&&) = default;
        
        template<typename U> explicit Some(const Some<U>& other) : value(other.value) {}
        template<typename U> explicit Some(Some<U>& other) : value(other.value) {}
        template<typename U> explicit Some(const Some<U>&& other) : value(std::move(other.value)) {}
        template<typename U> explicit Some(Some<U>&& other) : value(std::move(other.value)) {}
        template<typename U> explicit Some(U&& x) : value(std::forward<U>(x)) {}
        
        template<typename... Args>
        explicit Some(std::in_place_t, Args&&... args)
        : value(std::forward<Args>(args)...) {
        }

        ~Some() = default;

        Some& operator=(const Some&) = default;
        Some& operator=(Some&&) = default;

        template<typename U>
        Some& operator=(const Some<U>& other) {
            value = other.value;
            return *this;
        }
        
        template<typename U>
        Some& operator=(Some<U>&& other) {
            value = std::move(other.value);
            return *this;
        }
        
        template<typename U>
        Some& operator=(U&& other) {
            value = std::move(other);
            return *this;
        }
        
    };

    template<typename T> Some(T&&) -> Some<T>;
    
    template<typename T>
    struct Option {
    
    
        bool _is_some;
        union {
            Some<T> _some;
        };
        
        template<typename... Args>
        void _emplace_some(Args&&... args) {
            assert(!_is_some);
            std::construct_at(&_some, std::forward<Args>(args)...);
            _is_some = true;
        }
        
        template<typename U>
        void _emplace_option(const Option<U>& other) {
            assert(!_is_some);
            if (other._is_some)
                _emplace_some(other._some);
        }

        template<typename U>
        void _emplace_option(Option<U>&& other) {
            assert(!_is_some);
            if (other._is_some)
                _emplace_some(std::move(other._some));
        }
        
        void _destruct_some() {
            assert(_is_some);
            std::destroy_at(&_some);
            _is_some = false;
        }
        
        void _destruct_option() {
            if (_is_some)
                _destruct_some();
        }
        
        template<typename U>
        Option& _assign_option(const Option<U>& other) {
            if (_is_some != other._is_some) {
                _destruct_option();
                _emplace_option(other);
            } else {
                if (_is_some) {
                    assert(other._is_some);
                    _some = other._some;
                }
            }
            return *this;
        }
        
        template<typename U>
        Option& _assign_option(Option<U>&& other) {
            if (_is_some != other._is_some) {
                _destruct_option();
                _emplace_option(std::move(other));
            } else {
                if (_is_some) {
                    assert(other._is_some);
                    _some = std::move(other._some);
                }
            }
            return *this;
        }
        
        Option() : _is_some(false) {}
        
        Option(const Option& other) 
        : _is_some(false) {
            _emplace_option(other);
        }
        
        Option(Option&& other) 
        : _is_some(false) {
            _emplace_option(std::move(other));
        }

        template<typename U>
        Option(const Option<U>& other) 
        : _is_some(false) {
            _emplace_option(other);
        }
        
        template<typename U>
        Option(Option<U>&& other) 
        : _is_some(false) {
            _emplace_option(std::move(other));
        }
        
        Option(const None&)
        : _is_some(false) {
        }
        
        Option(None&&)
        : _is_some(false) {
        }
        
        template<typename U>
        Option(const Some<U>& some)
        : _is_some(true)
        , _some(some) {
        }
        
        template<typename U>
        Option(Some<U>&& some)
        : _is_some(true)
        , _some(std::move(some)) {
        }
        
        ~Option() {
            _destruct_option();
        }
        
        Option& operator=(const Option& other) {
            return _assign_option(other);
        }

        Option& operator=(Option&& other) {
            return _assign_option(std::move(other));
        }
        
        template<typename U>
        Option& operator=(const Option<U>& other) {
            return _assign_option(other);
        }
        
        template<typename U>
        Option& operator=(Option<U>&& other) {
            return _assign_option(std::move(other));
        }
        
        bool is_some() const {
            return _is_some;
        }
        
        bool is_none() const {
            return !_is_some;
        }
        
        T expect(const char* msg) && {
            if (_is_some) {
                return std::move(_some.value);
            } else {
                fputs(msg, stderr);
                abort();
            }
        }
    
        T unwrap() && {
            if (_is_some) {
                return std::move(_some.value);
            } else {
                abort();
            }
        }
        
        T unwrap_or(T&& fallback) && {
            if (_is_some) {
                return std::move(_some.value);
            } else {
                return std::move(fallback);
            }
        }
        
        T unwrap_or_default() && {
            if (_is_some) {
                return std::move(_some.value);
            } else {
                return T{};
            }
        }
        
        template<typename F>
        T unwrap_or_else(F&& factory) && {
            if (_is_some) {
                return std::move(_some.value);
            } else {
                return std::forward<F>(factory)();
            }
        }
        
        template<typename P>
        Option filter(P&& predicate) && {
            if (_is_some && std::forward<P>(predicate)(_some.value))
                return std::move(_some);
            else
                return None{};
        }
        
        
        template<typename F>
        auto map(F&& f) && {
            if (is_some())
                return Some(std::forward<F>(f)(std::move(_some.value)));
            else
                return None{};
        }
           
        template<typename F, typename U>
        U map_or(U&& fallback, F&& function) && {
            if (is_some())
                return std::forward<F>(function)(std::move(_some.value));
            else
                return std::forward<U>(fallback);
        }
        
        template<typename G, typename F>
        auto map_or_else(G&& fallback, F&& function) && {
            if (is_some())
                return std::forward<F>(function)(std::move(_some.value));
            else
                return std::forward<G>(fallback)();
        }
        
        template<typename U>
        Option<U> and_(Option<U>&& other) && {
            if (is_some())
                return std::move(other);
            else
                return None();
        }
        
        Option or_(Option&& other) && {
            if (is_some())
                return std::move(*this);
            else
                return std::move(other);
        }
        
        Option xor_(Option&& other) && {
            if (is_none())
                return std::move(other);
            else if (other.is_none())
                return std::move(*this);
            else
                return None{};
        }
        
        template<typename F>
        auto and_then(F&& f) && {
            if (is_some())
                return std::forward<F>(f)(std::move(_some.value));
            else
                return None{};
        }
        
        template<typename F>
        Option or_else(F&& factory) {
            if (is_some())
                return std::move(*this);
            else
                return std::forward<F>(factory)();
        }
        
        
        template<typename P>
        bool is_some_and(P&& predicate) const {
            return is_some() && std::forward<P>(predicate)(_some.value);
        }
        
        T unwrap_unchecked() && {
            assert(_is_some);
            return std::move(_some.value);
        }
        
        T& insert(T x) & {
            if (is_some()) {
                _some.value = std::move(x);
            } else {
                _emplace_some(std::move(x));
            }
            return _some.value;
        }
        
        T& get_or_insert(T x) & {
            if (is_none())
                _emplace_some(std::move(x));
            return _some.value;
        }
        
        template<typename F>
        T& get_or_insert_with(F&& f) & {
            if (is_none())
                _emplace_some(std::forward<F>(f)());
            return _some.value;
        }
        
        Option take() & {
            Option y(std::move(*this));
            _destruct_option();
            return y;
        }
        
        Option replace(T x) & {
            Option y(std::move(*this));
            *this = Some(std::move(x));
            return y;
        }

        decltype(auto) visit(auto&& visitor) const& {
            return is_none() ? FORWARD(visitor)(None{}) : FORWARD(visitor)(_some);
        }

        decltype(auto) visit(auto&& visitor) & {
            return is_none() ? FORWARD(visitor)(None{}) : FORWARD(visitor)(_some);
        }

        decltype(auto) visit(auto&& visitor) const && {
            return is_none() ? FORWARD(visitor)(None{}) : FORWARD(visitor)(std::move(_some));
        }

        decltype(auto) visit(auto&& visitor) && {
            return is_none() ? FORWARD(visitor)(None{}) : FORWARD(visitor)(std::move(_some));
        }

    };
    
    
} // namespace rust

#endif /* Option_hpp */
