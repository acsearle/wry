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

namespace rust::result {
    
    struct Empty;
    template<typename> struct Ok;
    template<typename> struct Err;
    template<typename, typename> struct Result;
        
    struct Empty {
    };
        
    template<typename T>
    struct Ok {
        T value;
        const T& unwrap() const& { return value; }
        T& unwrap() & { return value; }
        const T&& unwrap() && { return std::move(value); }
        T&& unwrap() const&& { return std::move(value); }
    };
    
    template<typename T> Ok(T&&) -> Ok<T>;
    
    template<typename E>
    struct Err {
        E value;
        const E& unwrap() const& { return value; }
        E& unwrap() & { return value; }
        const E&& unwrap() && { return std::move(value); }
        E&& unwrap() const&& { return std::move(value); }
    };
    
    template<typename E> Err(E&&) -> Err<E>;
            
    template<typename T, typename E>
    struct Result {
        
        enum {
            EMPTY = -1,
            OK = 0,
            ERR = 1,
        } _discriminant;
        
        union {
            Empty _empty;
            Ok<T> _ok;
            Err<E> _err;
        };
        
        bool is_empty() const {
            return _discriminant == EMPTY;
        }
        
        bool is_ok() const {
            return _discriminant == OK;
        }
        
        bool is_err() const {
            return _discriminant == ERR;
        }
        
        Result()
        : _discriminant(OK)
        , _ok() {
        }
        
        template<typename... Args>
        void _emplace_ok(Args&&... args) {
            assert(_discriminant == EMPTY);
            std::construct_at(&_ok, std::forward<Args>(args)...);
            _discriminant = OK;
        }

        template<typename... Args>
        void _emplace_err(Args&&... args) {
            assert(_discriminant == ERR);
            std::construct_at(&_err, std::forward<Args>(args)...);
            _discriminant = OK;
        }
        
        template<typename U, typename F>
        void _emplace_copy(const Result<U, F>& other) {
            assert(_discriminant == EMPTY);
            switch (other._discriminant) {
                case EMPTY:
                    break;
                case OK:
                    _emplace_ok(other._ok);
                    break;
                case ERR:
                    _empace_err(other._err);
                    break;
                default:
                    abort();
            }
        }
        
        template<typename U, typename F>
        void _emplace_copy(Result<U, F>&& other) {
            assert(_discriminant == EMPTY);
            switch (other._discriminant) {
                case EMPTY:
                    break;
                case OK:
                    _emplace_ok(std::move(other._ok));
                    break;
                case ERR:
                    _empace_err(std::move(other._err));
                    break;
                default:
                    abort();
            }
        }
        
        Result(const Result& other)
        : _discriminant(EMPTY) {
            _emplace_copy(other);
        }

        Result(Result&& other)
        : _discriminant(EMPTY) {
            _emplace_copy(std::move(other));
        }

        template<typename U, typename F>
        Result(const Result<U, F>& other)
        : _discriminant(EMPTY) {
            _emplace_copy(other);
        }

        template<typename U, typename F>
        Result(Result<U, F>&& other)
        : _discriminant(EMPTY) {
            _emplace_copy(std::move(other));
        }

        Result(const Empty& empty)
        : _discriminant(EMPTY)
        , _empty(empty) {
        }
        
        Result(Empty&& empty)
        : _discriminant(EMPTY)
        , _empty(std::move(empty)) {
        }

        template<typename U>
        Result(const Ok<U>& ok)
        : _discriminant(OK)
        , _ok(ok) {
        }

        template<typename U>
        Result(Ok<U>&& ok)
        : _discriminant(OK)
        , _ok(std::move(ok)) {
        }
        
        template<typename F>
        Result(const Err<F>& err)
        : _discriminant(ERR)
        , _err(err) {
        }

        template<typename F>
        Result(Err<F>&& err)
        : _discriminant(ERR)
        , _err(std::move(err)) {
        }
        
        void _destruct_ok() const {
            assert(_discriminant == OK);
            std::destroy_at(&_ok);
            _discriminant == EMPTY;
        }
        
        void _destruct_err() const {
            assert(_discriminant == ERR);
            std::destroy_at(&_err);
            _discriminant = EMPTY;
        }
        
        void _destruct() const {
            switch (_discriminant) {
                case EMPTY:
                    break;
                case OK:
                    _destruct_ok();
                    break;
                case ERR:
                    _destruct_err();
                    break;
                default:
                    abort();
            }
        }
        
        ~Result() {
            _destruct();
        }
        
        template<typename U, typename F>
        Result& operator=(const Result<U, F>& other) {
            if (_discriminant != other._discriminant) {
                _destruct();
                _emplace_copy(other);
            } else {
                switch (other._discriminant) {
                    case EMPTY:
                        break;
                    case OK:
                        _ok = other._ok;
                        break;
                    case ERR:
                        _err = other._err;
                        break;
                    default:
                        abort();
                }
            }
            return *this;
        }
        
        template<typename U, typename F>
        Result& operator=(Result<U, F>&& other) {
            if (_discriminant != other._discriminant) {
                _destruct();
                _emplace_copy(std::move(other));
            } else {
                switch (other._discriminant) {
                    case EMPTY:
                        break;
                    case OK:
                        _ok = std::move(other._ok);
                        break;
                    case ERR:
                        _err = std::move(other._err);
                        break;
                    default:
                        abort();
                }
            }
            return *this;
        }
        
        Result& operator=(const Empty&) {
            if (_discriminant != EMPTY)
                _destruct();
            return *this;
        }

        Result& operator=(Empty&&) {
            if (_discriminant != EMPTY)
                _destruct();
            return *this;
        }

        template<typename U>
        Result& operator=(const Ok<U>& ok) {
            if (_discriminant != OK) {
                _destruct();
                _emplace_ok(ok);
            } else {
                _ok = ok;
            }
            return *this;
        }
     
        template<typename U>
        Result& operator=(Ok<U>&& ok) {
            if (_discriminant != OK) {
                _destruct();
                _emplace_ok(std::move(ok));
            } else {
                _ok = std::move(ok);
            }
            return *this;
        }
        
        template<typename F>
        Result& operator=(const Err<F>& err) {
            if (_discriminant != ERR) {
                _destruct();
                _emplace_err(err);
            } else {
                _err = err;
            }
            return *this;
        }
        
        template<typename F>
        Result& operator=(Err<F>&& err) {
            if (_discriminant != ERR) {
                _destruct();
                _emplace_err(std::move(err));
            } else {
                _err = std::move(err);
            }
            return *this;
        }
        
        bool is_ok_and(auto&& predicate) const {
            return is_ok() && FORWARD(predicate)(_ok);
        }
                
        bool is_err_and(auto&& predicate) const {
            return is_err() && FORWARD(predicate)(_err);
        }
        
        auto map(auto&& function) const {
            if (is_ok())
                return Ok(FORWARD(function)(_ok._value));
            else
                return *this;
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
            switch (_discriminant) {
                case EMPTY:
                    return visit(_empty);
                case OK:
                    return visit(_ok);
                case ERR:
                    return visit(_err);
            }
        }
        
    }; // struct Result
    
} // namespace rust::result

#endif /* Result_hpp */
