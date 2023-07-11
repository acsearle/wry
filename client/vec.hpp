//
//  vec.hpp
//  client
//
//  Created by Antony Searle on 26/6/2023.
//

#ifndef vec_hpp
#define vec_hpp

#include <cassert>
#include <cmath>
#include <iostream>
#include <utility>

#include "common.hpp"
#include "hash.hpp"
#include "serialize.hpp"

namespace wry {
    
    // todo: map these to simd on Apple
    
    template<typename T, std::size_t N>
    class vec;
    
    template<typename T>
    class vec<T, 0> {
    public:
        
    };
    
    template<typename T, std::size_t N> struct vec_storage;
    
    template<typename T>
    struct vec_storage<T, 1> {
        union {
            T _[1];
            struct { T x; };
            struct { T r; };
            struct { T s; };
        };
    };
    
    template<typename T>
    struct vec_storage<T, 2> {
        union {
            T _[2];
            struct { T x, y; };
            struct { T r, g; };
            struct { T s, t; };
            struct { T width, height; };
        };
    };
    
    template<typename T>
    struct vec_storage<T, 3> {
        union {
            T _[3];
            struct { T x, y, z; };
            struct { T r, g, b; };
            struct { T s, t, p; };
            // Named contiguous subvectors
            struct { vec<T, 2> xy; };
            struct { T _x_yz; vec<T, 2> yz; };
            struct { vec<T, 2> rg; };
            struct { T _r_gb; vec<T, 2> gb; };
            struct { vec<T, 2> st; };
            struct { T _s_tp; vec<T, 2> tp; };
        };
    };
    
    template<typename T, std::size_t N>
    struct vec_storage {
        union {
            T _[N];
            struct { T x, y, z, w; };
            struct { T r, g, b, a; };
            struct { T s, t, p, q; };
            // Named contiguous subvectors for lowest 4 components
            struct { vec<T, 2> xy, zw; };
            struct { T _x_yz; vec<T, 2> yz; };
            struct { vec<T, 3> xyz; };
            struct { T _x_yzw; vec<T, 3> yzw; };
            struct { vec<T, 2> rg, ba; };
            struct { T _r_gb; vec<T, 2> gb; };
            struct { vec<T, 3> rgb; };
            struct { T _r_gba; vec<T, 3> gba; };
            struct { vec<T, 2> st, pq; };
            struct { T _s_tp; vec<T, 2> tp; };
            struct { vec<T, 3> stp; };
            struct { T _s_tpq; vec<T, 3> tpq; };
        };
    };
    
    template<typename T, std::size_t N>
    class vec : public vec_storage<T, N> {
        
        // TriviallyCopyable allows us to move around by clobbering data
        static_assert(std::is_trivially_copyable<T>::value, "T must be TriviallyCopyable");
        
        // T _[N];
        
    public:
        
        using size_type = std::size_t;
        using value_type = T;
        using reference = value_type&;
        using const_reference = const value_type&;
        using pointer = value_type*;
        using const_pointer = const value_type*;
        using iterator = pointer;
        using const_iterator = const_pointer;
        
        constexpr vec() = default;
        constexpr vec(const vec&) = default;
        constexpr vec(vec&&) = default;
        ~vec() = default;
        constexpr vec& operator=(const vec&) = default;
        constexpr vec& operator=(vec&&) = default;
        
        template<typename U, typename = decltype(std::declval<T&>() = std::declval<const U&>())>
        constexpr explicit vec(const U& r) {
            for (size_type i = 0; i != N; ++i)
                this->_[i] = r;
        }
        
        template<typename U, typename = decltype(std::declval<T&>() = std::declval<const U&>())>
        constexpr vec(const vec<U, N>& r) {
            for (size_type i = 0; i != N; ++i)
                this->_[i] = r[i];
        }
        
        template<typename U, typename... Args, typename = decltype(std::declval<T&>() = std::declval<const U&>())>
        constexpr vec(const U& r, const Args&... args) {
            this->_[0] = r;
            new (this->_ + 1) vec<T, N - 1>(args...);
        }
        
        template<typename U, std::size_t M, typename... Args, typename = decltype(std::declval<T&>() = std::declval<const U&>())>
        constexpr vec(const vec<U, M>& r, const Args&... args) {
            static_assert(M < N, "too many initializers");
            new (this) vec<T, M>(r);
            new (this->_ + M) vec<T, N - M>(args...);
        }
        
        template<typename U, typename = decltype(std::declval<T&>() = std::declval<const U&>())>
        constexpr vec& operator=(const U& r) {
            for (size_type i = 0; i != N; ++i)
                this->_[i] = r;
            return *this;
        }
        
        template<typename U, typename = decltype(std::declval<T&>() = std::declval<const U&>())>
        constexpr vec& operator=(const vec<U, N>& r) {
            for (size_type i = 0; i != N; ++i)
                this->_[i] = r[i];
            return *this;
        }
        
        constexpr reference operator[](size_type i) { assert(i < N); return this->_[i]; }
        constexpr const_reference operator[](size_type i) const { assert(i < N); return this->_[i]; }
        
        constexpr reference front() { return this->_[0]; }
        constexpr const_reference front() const { return this->_[0]; }
        
        constexpr reference back() { return this->_[N - 1]; }
        constexpr const_reference back() const { return this->_[N - 1]; }
        
        constexpr pointer data() { return this->_; }
        constexpr const_pointer data() const { return this->_; }
        
        constexpr iterator begin() { return this->_; }
        constexpr const_iterator begin() const { return this->_; }
        constexpr const_iterator cbegin() const { return this->_; }
        
        constexpr iterator end() { return this->_ + N; }
        constexpr const_iterator end() const { return this->_ + N; }
        constexpr const_iterator cend() const { return this->_ + N; }
        
        constexpr bool empty() const { return !N; }
        constexpr size_type size() const { return N; }
        
    };
    
    template<typename T, std::size_t N>
    void swap(vec<T, N>& a, vec<T, N>& b) {
        using std::swap;
        for (size_t i = 0; i != N; ++i)
            swap(a[i], b[i]);
    }
    
    template<std::size_t I, typename T, std::size_t N>
    T& get(vec<T, N>& x) { return x[I]; }
    
    template<std::size_t I, typename T, std::size_t N>
    const T& get(const vec<T, N>& x) { return x[I]; }
    
    template<std::size_t I, typename T, std::size_t N>
    T&& get(vec<T, N>&& x) { return x[I]; }
    
    template<std::size_t I, typename T, std::size_t N>
    const T&& get(const vec<T, N>&& x) { return x[I]; }
    
    template<typename T>
    class tuple_size;
    
    template<typename T, std::size_t N>
    class tuple_size<vec<T, N>> : public std::integral_constant<std::size_t, N> {};
    
    
    // elementwise operations
    
#define UNARY(OP)\
template<typename T, std::size_t N> auto operator OP (const vec<T, N>& a) {\
vec<decltype( OP std::declval<const T&>()), N> c;\
for (std::size_t i = 0; i != N; ++i) c[i] = OP a[i];\
return c;\
}\

    UNARY(+)
    UNARY(-)
    UNARY(~)
    UNARY(!)
    
#undef UNARY
    
#define BINARY(OP)\
template<typename T, std::size_t N, typename U, typename R = decltype(std::declval<const T&>() OP std::declval<const U&>())>\
vec<R, N> operator OP (const vec<T, N>& a, const vec<U, N>& b) {\
vec<R, N> c;\
for (std::size_t i = 0; i != N; ++i) c[i] = a[i] OP b[i];\
return c;\
}\
template<typename T, std::size_t N, typename U, typename R = decltype(std::declval<const T&>() OP std::declval<const U&>())>\
vec<R, N> operator OP (const vec<T, N>& a, const U& b) {\
vec<R, N> c;\
for (std::size_t i = 0; i != N; ++i) c[i] = a[i] OP b;\
return c;\
}\
template<typename T, std::size_t N, typename U, typename R = decltype(std::declval<const T&>() OP std::declval<const U&>())>\
vec<R, N> operator OP (const T& a, const vec<U, N>& b) {\
vec<R, N> c;\
for (std::size_t i = 0; i != N; ++i) c[i] = a OP b[i];\
return c;\
}\
template<typename T, std::size_t N, typename U>\
vec<T, N>& operator OP##=(vec<T, N>& a, const vec<U, N>& b) {\
for (std::size_t i = 0; i != N; ++i) a[i] OP##= b[i];\
return a;\
}\
template<typename T, std::size_t N, typename U>\
vec<T, N>& operator OP##=(vec<T, N>& a, const U& b) {\
for (std::size_t i = 0; i != N; ++i) a[i] OP##= b;\
return a;\
}
    
    BINARY(+)
    BINARY(-)
    BINARY(*)
    BINARY(/)
    BINARY(%)
    BINARY(^)
    BINARY(&)
    BINARY(|)
    BINARY(<<)
    BINARY(>>)
    
#undef BINARY
    
    template<typename T, std::size_t N>
    std::ostream& operator<<(std::ostream& a, const vec<T, N>& b) {
        a << "vec(";
        for (std::size_t i = 0; i != N;) {
            a << b[i];
            ++i;
            if (i != N)
                a << ",";
        }
        return a << ")";
    }
    
    // short-circuit comparison
    
    template<typename T, std::size_t N, typename U>
    bool operator==(const vec<T, N>& a, const vec<U, N>& b) {
        for (std::size_t i = 0; i != N; ++i)
            if (a[i] != b[i])
                return false;
        return true;
    }
    
    template<typename T, std::size_t N, typename U>
    bool operator!=(const vec<T, N>& a, const vec<U, N>& b) {
        return !(a == b);
    }
    
    // lexicograhical ordering
    
    template<typename T, std::size_t N, typename U>
    bool operator<(const vec<T, N>& a, const vec<U, N>& b) {
        for (std::size_t i = 0; i != N; ++i) {
            if (a[i] < b[i])
                return true;
            else if (b[i] < a[i])
                return false;
        }
        return false;
    }
    
    template<typename T, std::size_t N, typename U>
    bool operator>(const vec<T, N>& a, const vec<U, N>& b) {
        return b < a;
    }
    
    template<typename T, std::size_t N, typename U>
    bool operator<=(const vec<T, N>& a, const vec<U, N>& b) {
        return !(b < a);
    }
    
    template<typename T, std::size_t N, typename U>
    bool operator>=(const vec<T, N>& a, const vec<U, N>& b) {
        return !(a < b);
    }
    
    
    
    // non-elementwise operations
    
    template<typename T, std::size_t N, typename U>
    auto dot(const vec<T, N>& a, const vec<U, N>& b) {
        static_assert(N, "");
        auto c = a[0] * b[0];
        for (std::size_t i = 1; i != N; ++i)
            c += a[i] * b[i];
        return c;
    }
    
    template<typename T, std::size_t N>
    auto sqr(const vec<T, N>& a) {
        static_assert(N, "");
        auto c = sqr(a[0]);
        for (std::size_t i = 1; i != N; ++i)
            c += sqr(a[i]);
        return c;
    }
    
    template<typename T, std::size_t N>
    auto length(const vec<T, N>& a) {
        using std::sqrt;
        return sqrt(dot(a, a));
    }
    
    template<typename T>
    auto length(const vec<T, 1>& a) {
        using std::abs;
        return abs(a[0]); // use abs for 1d
    }
    
    template<typename T>
    auto length(const vec<T, 2>& a) {
        using std::hypot;
        return hypot(a[0], a[1]); // use hypot for 2d
    }
    
    template<typename T>
    auto length(const vec<T, 3>& a) {
        using std::hypot;
        return hypot(a[0], a[1], a[2]); // use hypot for 3d
    }
    
    template<typename T, std::size_t N>
    auto distance(const vec<T, N>& a, const vec<T, N>& b) {
        return length(a - b);
    }
    
    template<typename T, std::size_t N>
    auto normalize(const vec<T, N>& a) {
        auto b = length(a);
        assert(b);
        return a / b;
    }
    
    template<typename T, std::size_t N>
    auto product(const vec<T, N>& a) {
        auto b = a[0];
        for (int i = 1; i != N; ++i)
            b *= a[i];
        return b;
    }
    
    
    
    // cross product for 3-vectors
    
    template<typename T, typename U>
    auto cross(const vec<T, 3>& a, const vec<U, 3>& b) {
        using R = decltype(std::declval<const T&>() * std::declval<const U&>());
        return vec<R, 3>(a[1] * b[2] - a[2] * b[1],
                         a[2] * b[0] - a[0] * b[2],
                         a[0] * b[1] - a[1] * b[0]);
    }
    
    // cross product for 2-vectors by analogy
    
    template<typename T, typename U>
    auto cross(const vec<T, 2>& a, const vec<U, 2>& b) {
        return a[0] * b[1] - a[1] * b[0];
    }
    
    // perpendicular vector to a 2-vector by ccw rotation
    
    template<typename T>
    vec<T, 2> perp(const vec<T, 2>& a) {
        return vec<T, 2>(-a[1], a[0]);
    }
    
    static_assert(sizeof(int) == 4, "int is not 32 bit");
    using uint = uint32_t;
    
    using vec2 = vec<float, 2>;
    using vec3 = vec<float, 3>;
    using vec4 = vec<float, 4>;
    using dvec2 = vec<double, 2>;
    using dvec3 = vec<double, 3>;
    using dvec4 = vec<double, 4>;
    using bvec2 = vec<bool, 2>;
    using bvec3 = vec<bool, 3>;
    using bvec4 = vec<bool, 4>;
    using ivec2 = vec<int, 2>;
    using ivec3 = vec<int, 3>;
    using ivec4 = vec<int, 4>;
    using uvec2 = vec<uint, 2>;
    using uvec3 = vec<uint, 3>;
    using uvec4 = vec<uint, 4>;
    
    template<typename T, usize N>
    u64 hash(const vec<T, N>& x) {
        return hash_combine(&x, sizeof(x));
    }
    
    template<typename T, usize N, typename Serializer>
    void serialize(vec<T, N> const& x, Serializer& s) {
        for (usize i = 0; i != N; ++i)
            serialize(x[i], s);
    }
    
    template<typename T, usize N, typename Deserializer>
    void deserialize(placeholder<vec<T, N>>, Deserializer& s) {
        vec<T, N> x;
        for (usize i = 0; i != N; ++i)
            x[i] = deserialize<T>(s);
        return x;
    }
    
    
}

#endif /* vec_hpp */
