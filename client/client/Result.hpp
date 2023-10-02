//
//  Result.hpp
//  client
//
//  Created by Antony Searle on 30/9/2023.
//

#ifndef Result_hpp
#define Result_hpp

#include "type_traits.hpp"
#include "utility.hpp"

namespace rust {
    
    template<typename> struct Ok;
    template<typename> struct Err;
    template<typename, typename> struct Result;
    
} // namespace rust

namespace std {
    
    template<typename T, typename E>
    struct common_type<rust::Ok<T>, rust::Err<E>> {
        using type = rust::Result<T, E>;
    };
    
} // namespace std

namespace rust {
    
    template<typename T>
    struct Ok {
        T _value;
    };
    
    template<typename T>
    Ok(T&&) -> Ok<std::decay_t<T>>;
    
    template<typename E>
    struct Err {
        E _value;
    };
    
    template<typename E>
    Err(E&&) -> Err<std::decay_t<E>>;
        
    template<typename T, typename E>
    struct Result {
        
        bool _is_err;
        union {
            Ok<T> _ok;
            Err<E> _err;
        };
        
        Result()
        : _is_err(false)
        , _ok() {
        }
        
        template<typename U, typename F>
        Result(const Result<U, F>& other)
        : _is_err(other._is_err) {
            if (!_is_err)
                new ((void*) &_ok) Ok<T>(other._ok);
            else
                new ((void*) &_err) Err<E>(other._err);
        }

        template<typename U, typename F>
        Result(Result<U, F>&& other)
        : _is_err(other._is_err) {
            if (!_is_err)
                new ((void*) &_ok) Ok<T>(std::move(other._ok));
            else
                new ((void*) &_err) Err<E>(std::move(other._err));
        }

        template<typename U>
        Result(const Ok<U>& ok)
        : _is_err(false)
        , _ok(ok) {
        }

        template<typename U>
        Result(Ok<U>&& ok)
        : _is_err(false)
        , _ok(std::move(ok)) {
        }
        
        template<typename F>
        Result(const Err<F>& err)
        : _is_err(true)
        , _err(err) {
        }

        template<typename F>
        Result(Err<F>&& err)
        : _is_err(true)
        , _err(std::move(err)) {
        }
        
        ~Result() {
            if (is_ok())
                _ok.~Ok();
            else
                _err.~Err();
        }
        
        template<typename U, typename F>
        Result& operator=(const Result<U, F>& other) {
            if (is_ok()) {
                if (other.is_ok()) {
                    _ok = other._ok;
                } else {
                    _ok.~Ok();
                    new ((void*) &_err) Err(other._err);
                }
            } else {
                if (other.is_ok()) {
                    _err.~T();
                    new ((void*) &_ok) Ok(other._ok);
                } else {
                    _err = other._err;
                }
            }
            _is_err = other._is_err;
        }
        
        template<typename U, typename F>
        Result& operator=(Result<U, F>&& other) {
            if (is_ok()) {
                if (other.is_ok()) {
                    _ok = std::move(other._ok);
                } else {
                    _ok.~Ok();
                    new ((void*) &_err) Err(std::move(other._err));
                }
            } else {
                if (other.is_ok()) {
                    _err.~T();
                    new ((void*) &_ok) Ok(std::move(other._ok));
                } else {
                    _err = other._err;
                }
            }
            _is_err = other._is_err;
        }
        
        bool is_ok() const {
            return !_is_err;
        }
        
        bool is_ok_and(auto&& predicate) const {
            return is_ok() && FORWARD(predicate)(_ok);
        }
        
        bool is_err() const {
            return _is_err;
        }
        
        bool is_err_and(auto&& predicate) const {
            return is_err() && FORWARD(predicate)(_err);
        }
        
        auto map(auto&& function) const {
            if (is_ok())
                return Ok(FORWARD(function)(_ok._value));
            else
                return _err;
        }
        
        auto map_or(auto&& otherwise, auto&& function) {
            if (is_ok())
                return FORWARD(function)(_ok._value);
            else
                return FORWARD(otherwise);
        }
        
        auto map_or_else(auto&& otherwise, auto&& function) {
            if (is_ok())
                return FORWARD(function)(_ok._value);
            else
                return FORWARD(function)(_err._value);
        }
        
        auto map_err(auto&& function) const {
            if (is_ok())
                return _ok;
            else
                return Err(FORWARD(function)(_err._value));
        }
        
        auto unwrap_or_default() const {
            if (is_ok())
                return _ok._value;
            else
                return T{};
        }
        
        auto and_(auto&& result) const {
            if (is_ok())
                return FORWARD(result);
            else
                return _err;
        }
        
        auto and_then(auto&& function) const {
            if (is_ok())
                return FORWARD(function)(_ok._value);
            else
                return _err;
        }
        
        auto or_(auto&& result) const {
            if (is_ok())
                return _ok;
            else
                return FORWARD(result);
        }
        
        auto or_else(auto&& function) const {
            if (is_ok())
                return _ok;
            else
                return FORWARD(function)(_err._value);
        }
        
        auto unwrap_or(auto&& otherwise) const {
            if (is_ok())
                return _ok._value;
            else
                return FORWARD(otherwise);
        }
        
        auto unwrap_or_else(auto&& factory) const {
            if (is_ok())
                return _ok._value;
            else
                return FORWARD(factory());
        }
        
        auto visit(auto&& visitor) const {
            if (is_ok())
                FORWARD(visitor)(_ok);
            else
                FORWARD(visitor)(_err);
        }
        
    }; // struct Result
    
} // namespace rust

#endif /* Result_hpp */
