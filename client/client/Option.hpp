//
//  Option.hpp
//  client
//
//  Created by Antony Searle on 30/9/2023.
//

#ifndef Option_hpp
#define Option_hpp

#include <cassert>

#include "rust.hpp"

// Rust-like enums

// They store wrapped types, allowing cases to be distinguished by type as
// well as by discriminant, even when the inner type appears several times

namespace rust {
    
    struct None;
    template<typename> struct Some;
    template<typename> struct Option;
    
}


namespace std {
    
    template<typename T>
    struct common_type<rust::None, rust::Some<T>> {
        using type = rust::Option<T>;
    };
    
} // namespace std


namespace rust {
    
    struct None {
    };
    
    template<typename T>
    struct Some {
        
        T value;
        
        Some() = default;
        
        template<typename U>
        Some(const Some<U>& other)
        : value(other.value) {
        }

        template<typename U>
        Some(Some<U>&& other)
        : value(other.value) {
        }
        
        template<typename U>
        explicit Some(const U& x) 
        : value(x) {
        }

        template<typename U>
        explicit Some(U&& x)
        : value(std::forward<U>(x)) {
        }
        
        ~Some() = default;
        
    };

    template<typename T>
    Some(T&&) -> Some<std::decay_t<T>>;
    
    template<typename T>
    struct Option {
        
        bool _is_some;
        union {
            Some<T> _some;
        };
        
        Option() : _is_some(false) {}
        
        Option(None) : _is_some(false) {}
        
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
            if (_is_some)
                _some.~Some();
        }
        
        void _emplace(auto&&... x) {
            assert(!_is_some);
            new (static_cast<void*>(std::addressof(_some))) Some(FORWARD(x)...);
            _is_some = true;
        }
        
        void _assign(auto&& x) {
            assert(_is_some);
            _some = FORWARD(x);
        }
        
        void _emplace_or_assign(auto&& x) {
            if (_is_some)
                _assign(FORWARD(x));
            else
                _emplace(FORWARD(x));
        }
        
        void _unchecked_clear() {
            assert(_is_some);
            _some.~Some();
            _is_some = false;
        }

        void _checked_clear() {
            if (_is_some)
                _unchecked_clear();
        }

        template<typename U>
        Option& operator=(const Option<U>& other) {
            if (other.is_some()) {
                _emplace_or_assign(other._some);
            } else {
                _checked_clear();
            }
            return *this;
        }
        
        template<typename U>
        Option& operator=(const Option<U>&& other) {
            if (other.is_some()) {
                _emplace_or_assign(std::move(other._some));
            } else {
                _checked_clear();
            }
            return *this;
        }
        
        bool is_some() const {
            return _is_some;
        }
        
        bool is_some_and(auto&& predicate) const {
            return is_some() && FORWARD(predicate)(_some);
        }
        
        bool is_none() const {
            return !_is_some;
        }
        
        auto unwrap_or(auto&& otherwise) const& {
            if (is_some())
                return _some.value;
            else
                return FORWARD(otherwise);
        }
        
        auto unwrap_or(auto&& otherwise) && {
            if (is_some())
                return std::move(_some.value);
            else
                return FORWARD(otherwise);
        }

        auto unwrap_or_else(auto&& factory) const& {
            if (is_some())
                return _some.value;
            else
                return FORWARD(factory)();
        }

        auto unwrap_or_else(auto&& factory) && {
            if (is_some())
                return std::move(_some.value);
            else
                return FORWARD(factory)();
        }
        
        auto unwrap_or_default() const& {
            if (is_some())
                return _some.value;
            else
                return T{};
        }

        auto unwrap_or_default() && {
            if (is_some())
                return std::move(_some.value);
            else
                return T{};
        }
        
        auto map(auto&& function) const& {
            if (is_some())
                return FORWARD(function)(_some.value);
            else
                return None{};
        }
        
        auto map_or(auto&& otherwise, auto&& function) const {
            if (is_some())
                return FORWARD(function)(_some.value);
            else
                return FORWARD(otherwise);
        }

        auto map_or_else(auto&& otherwise, auto&& function) const {
            if (is_some())
                return FORWARD(function)(_some.value);
            else
                return FORWARD(otherwise)();
        }
        
        auto and_(auto&& other) const {
            if (is_some())
                return FORWARD(other);
            else
                return None{};
        }
        
        auto and_then(auto&& function) const {
            if (is_some())
                return Some(FORWARD(function)(_some.value));
            else
                return None{};
        }
        
        Option<T> filter(auto&& predicate) && {
            if (is_some() && predicate(_some.value))
                return std::move(*this);
            else
                return None{};
        }
        
        auto or_(auto&& other) && {
            if (is_some())
                return std::move(*this);
            else
                return FORWARD(other);
        }
        
        auto or_else(auto&& function) && {
            if (is_some())
                return std::move(*this);
            else
                return FORWARD(function)();
        }
        
        auto xor_(auto&& other) && {
            if (is_some()) {
                if (!other.is_some()) {
                    return std::move(_some);
                }
            } else {
                if (other.is_some())
                    return std::move(other._some);
            }
            return None{};
        }
        
        T& insert(auto&& value) & {
            if (is_some())
                _some.value = FORWARD(value);
            else
                new (&_some) Some(FORWARD(value));
            _is_some = true;
            return _some.value;
        }
        
        T& get_or_insert(auto&& value) & {
            if (is_none()) {
                new (&_some) Some(FORWARD(value));
            }
            _is_some = true;
            return _some.value;
        }
        
        T& get_or_insert_with(auto&& factory) & {
            if (is_none())
                new (&_some) Some(FORWARD(factory)());
            _is_some = true;
            return _some.value;
        }
        
        Option<T> take() & {
            Option<T> result(std::move(*this));
            if (_is_some) {
                _some.~Some();
                _is_some = false;
            }
            return result;
        }
        
        Option<T> replace(auto&& value) & {
            Option<T> result(std::move(*this));
            if (_is_some)
                _some = FORWARD(value);
            else
                new (&_some) T(FORWARD(value));
            return result;
        }
        
        template<typename U>
        auto zip(Option<U>&& other) && {
            if (is_some() && other.is_some())
                return Some(std::make_pair(std::move(_some), std::move(other._some)));
            else
                return None{};
        }

        // requires T itself be an Option<U>
        auto flatten() && {
            return is_some() ? std::move(_some.value) : None{};
        }
        
        const T* as_ptr() const {
            return is_some() ? &_some.value : nullptr;
        }
        
        T* as_mut_ptr() {
            return is_some() ? &_some.value : nullptr;
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
    
    
    
}

#endif /* Option_hpp */
