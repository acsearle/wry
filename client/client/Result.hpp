//
//  Result.hpp
//  client
//
//  Created by Antony Searle on 30/9/2023.
//

#ifndef Result_hpp
#define Result_hpp

#include <cstdio>

#include "type_traits.hpp"
#include "utility.hpp"

namespace rust::result {
    
    // Sum types in C++
    //
    // std::variant does not permit customization of discriminant
    // Rust does not expose discriminant at all
    // Explicit discriminants are helpful for serialization and C++ switch
    // statements
    //
    // std::variant can hold the same type more than once, but visit cannot
    // distinguish between these occurances; visit does not get access to the
    // discriminant
    //
    // std::variant cannot hold void or reference types or array types
    //
    // Rust alternatives look like structs but can't be accessed outside of
    // pattern matching; note the duplication here:
    //
    // pub enum Entry<'a, K: 'a, V: 'a> {
    //     Occupied(OccupiedEntry<'a, K, V>),
    //     Vacant(VacantEntry<'a, K, V>),
    // }
    //
    // Occupied is a Variant, OccupiedEntry is an ordinary struct that is its
    // payload
    //
    // In Rust, Variants such as Ok and Err act as constructors for enums such
    // as Result.  In C++, there is not enough information present (E or T) to
    // do so, and they must construct an intermediate that is convertible to
    // a specified common type.
    //
    // The discriminant should not increase the alignment of the type; if it is
    // not of the same alignment as the union, there will be unused space in
    // the type (though, there will be in general whenever it holds not-the
    // largest type)
    //
    // In C++ we can have the empty-by-exception state if constructors are
    // yesexcept
    //
    // The absence of a .? operator in C++ makes sum-type-based error handling
    // unwieldy compared to exception-based
    //
    // enum discriminant_t { EMPTY = -1, OK = 0, ERR = 1 };
    //
    // the discriminant is a property of Result, not of Ok<T>
    //
    // is every member type, like OK, a higher-order tuple?
    //
    // template<int D, typename T>
    // struct VariantFactory {
    //     static constexpr discriminant_t key = D;
    //     T value;
    // };
    //
    // template<typename T>
    // using Ok = VariantFactory<OK, T>;
    //
    // template<typename T>
    // struct Ok {
    //     static constexpr discriminant_t discriminant = OK;
    //     T value;
    // };
    //
    // template<typename T, typename E>
    // struct Result {
    //     discriminant_t discriminant;
    //     alignas(Ok<T>, Err<E>) unsigned char value[sizeof(Ok<T>, Err<E>)];
    //     template<typename U> Result(Ok<U>);
    //     template<typename F> Result(Err<F>);
    // };

    struct Error {
    };

    template<typename> struct Ok;
    template<typename> struct Err;
    template<typename, typename> struct Result;
    
    // where do these live?
    
    enum {
        ERR = -1,
        EMPTY = 0,
        OK = 1,
    };
    
    template<template<typename> typename>
    struct discrimnator_mapping {
    };

    template<>
    struct discrimnator_mapping<Ok> {
        static constexpr int value = OK;
    };

    template<>
    struct discrimnator_mapping<Err> {
        static constexpr int value = ERR;
    };

            
    template<typename T>
    struct Ok {
        
        T value;
        
        Ok() = default;
        Ok(const Ok& x) = default;
        Ok(Ok&& x) = default;
        ~Ok() = default;
        Ok& operator=(const Ok&) = default;
        Ok& operator=(Ok&&) = default;

        template<typename U>
        Ok(const Ok<U>& other)
        : value(other.value) {
        }

        template<typename U>
        Ok(Ok<U>&& other)
        : value(std::move(other.value)) {
        }

        template<typename U>
        Ok(const U& other) 
        : value(other) {            
        }
        
        template<typename U>
        Ok(U&& other)
        : value(std::move(other)) {
        }
        
        const T& unwrap() const& { return value; }
        T& unwrap() & { return value; }
        const T&& unwrap() && { return std::move(value); }
        T&& unwrap() const&& { return std::move(value); }
        
    };
    
    template<typename T> Ok(T&&) -> Ok<T>;
    
    template<typename E>
    struct Err {
        E value;
        
        Err() = delete;
        Err(const Err& x) = default;
        Err(Err&& x) = default;
        ~Err() = default;
        Err& operator=(const Err&) = default;
        Err& operator=(Err&&) = default;
        
        Err(const E& other) : value(other) {}
        
        Err(E&& other) : value(std::move(other)) {}
        
        const E& unwrap() const& { return value; }
        E& unwrap() & { return value; }
        const E&& unwrap() && { return std::move(value); }
        E&& unwrap() const&& { return std::move(value); }
        
    };
    
    template<typename E> Err(E&&) -> Err<E>;
            
    template<typename T, typename E = Error>
    struct Result {
        
        enum {
            EMPTY = -1, // <-- empty by exception, inaccessible state
            OK = 0,
            ERR = 1,
        } _discriminant;
        
        union {
            Ok<T> _ok;
            Err<E> _err;
        };
        
        bool _invariant() const& {
            return (_discriminant == OK) || (_discriminant == EMPTY);
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
                case OK:
                    _emplace_ok(std::move(other._ok));
                    break;
                case ERR:
                    _emplace_err(std::move(other._err));
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
        
        void _destruct_ok() {
            assert(_discriminant == OK);
            std::destroy_at(&_ok);
            _discriminant = EMPTY;
        }
        
        void _destruct_err() {
            assert(_discriminant == ERR);
            std::destroy_at(&_err);
            _discriminant = EMPTY;
        }
        
        void _destruct() {
            switch (_discriminant) {
                case OK:
                    _destruct_ok();
                    break;
                case ERR:
                    _destruct_err();
                    break;
                default:
                    break;
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
        
        bool is_ok() const& {
            return _discriminant == OK;
        }
        
        bool is_err() const& {
            return _discriminant == ERR;
        }

        T&& expect(const char* msg) && {
            if (is_ok()) {
                return std::move(_ok.value);
            } else {
                fputs(msg, stderr);
                abort();
            }
        }
        
        T unwrap() && {
            if (is_ok()) {
                return std::move(_ok.value);
            } else {
                abort();
            }
        }
        
        T unwrap_or_default() && {
            if (is_ok())
                return std::move(_ok._value);
            else
                return T{};
        }
        
        T unwrap_or(T&& fallback) && {
            if (is_ok())
                return std::move(_ok._value);
            else
                return std::move(fallback);
        }
        
        template<typename F>
        T unwrap_or_else(F&& factory) && {
            if (is_ok())
                return std::move(_ok._value);
            else
                return FORWARD(factory)();
        }
        
        E expect_err(const char* msg) && {
            if (is_err()) {
                return std::move(_err.value);
            } else {
                fputs(msg, stderr);
                abort();
            }
        }
        
        E unwrap_err() && {
            if (is_err()) {
                return std::move(_err.value);
            } else {
                abort();
            }
        }
        
        template<typename F>
        Result<decltype((std::declval<F&&>()(std::declval<T&&>()))), E>
        map(F&& f) && {
            switch (_discriminant) {
                case OK:
                    return Ok(std::forward<F>(f)(std::move(_ok.value)));
                case ERR:
                    return std::move(_err);
                default:
                    abort();
            }
        }
        
        template<typename F>
        Result<T, decltype((std::declval<F&&>()(std::declval<E&&>())))>
        map_err(F&& f) && {
            switch (_discriminant) {
                case OK:
                    return std::move(_ok);
                case ERR:
                    return Err(std::forward<F>(f)(std::move(_err._value)));
                default:
                    abort();
            }
        }
        
        template<typename F, typename U>
        U map_or(F&& f, U&& fallback) && {
            switch (_discriminant) {
                case OK:
                    return std::forward<F>(f)(std::move(_ok.value));
                default:
                    return std::move(fallback);
            }
        }
        
        template<typename F, typename G>
        auto map_or_else(F&& f, G&& g) && {
            switch (_discriminant) {
                case OK:
                    return std::forward<F>(f)(std::move(_ok.value));
                case ERR:
                    return std::forward<G>(g)(std::move(_err.value));
                default:
                    abort();
            }
        }
        
        template<typename U>
        Result<U, E> and_(Result<U, E>&& other) && {
            switch (_discriminant) {
                case OK:
                    return std::move(other);
                case ERR:
                    return std::move(_err);
                default:
                    abort();
            }
        }
        
        template<typename F>
        Result<T, F> or_(Result<T, F>&& other) && {
            switch (_discriminant) {
                case OK:
                    return std::move(other._ok);
                case ERR:
                    return std::move(other);
                default:
                    abort();
            }
        }
        
        template<typename F>
        auto and_then(F&& f) && {
            switch (_discriminant) {
                case OK:
                    return std::forward<F>(f)(std::move(_ok.value));
                case ERR:
                    return std::move(_err);
                default:
                    abort();
            }
        }
        
        template<typename F>
        auto or_else(F&& f) && {
            switch (_discriminant) {
                case OK:
                    return std::move(_ok);
                case ERR:
                    return std::forward<F>(f)(std::move(_err.value));
                default:
                    abort();
            }
        }
        
        bool is_ok_and(auto&& predicate) const& {
            return is_ok() && FORWARD(predicate)(_ok);
        }
                
        bool is_err_and(auto&& predicate) const& {
            return is_err() && FORWARD(predicate)(_err);
        }
        
        T unwrap_unchecked() && {
            assert(_discriminant == OK);
            return std::move(_ok.value);
        }

        E unwrap_err_unchecked() && {
            assert(_discriminant == OK);
            return std::move(_err.value);
        }

        auto visit(auto&& visitor) && {
            switch (_discriminant) {
                case OK:
                    return std::forward<decltype(visitor)>(visitor)(std::move(_ok.value));
                case ERR:
                    return std::forward<decltype(visitor)>(visitor)(std::move(_err.value));
                default:
                    abort();
            }
        }
        
        
    }; // struct Result
    
} // namespace rust::result

#endif /* Result_hpp */
