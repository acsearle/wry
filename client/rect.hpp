//
//  rect.hpp
//  client
//
//  Created by Antony Searle on 26/6/2023.
//

#ifndef rect_hpp
#define rect_hpp

#include <simd/simd.h>
#include "utility.hpp"

namespace wry {
    
    // There are many defensible choices for rect storage.  The most performance
    // critical use case is guessed to be constructing quad vertices.  Storing
    // top-left and bottom-right vertices means that two vertices are copies, and
    // the other two corners are component-wise copies.
    
    template<typename T, int N>
    struct simd_TN;
    
    template<typename T, int N>
    using simd_TN_t = typename simd_TN<T, N>::type;
    
    template<>
    struct simd_TN<float, 2> {
        using type = simd_float2;
    };

    template<>
    struct simd_TN<unsigned long, 2> {
        using type = simd_ulong2;
    };

    template<typename T>
    class rect {
        
    public:
        
        using T2 = typename simd_TN<T, 2>::type;
        
        T2 a, b;
        
        bool invariant() const {
            return (a.x <= b.x) && (a.y <= b.y);
        }
        
        rect() = default;
        
        rect(const rect&) = default;
        
        rect(const T2& x,
             const T2& y)
        : a(x)
        , b(y) {
        }
        
        rect(T ax, T ay, T bx, T by)
        : a{ax, ay}
        , b{bx, by} {
        }
        
        void canonicalize() {
            using std::swap;
            if (a.x > b.x)
                swap(a.x, b.x);
            if (a.y > b.y)
                swap(a.y, b.y);
        }
        
        T2 size() const { return b - a; }
        
        T width() const { return b.x - a.x; }
        T height() const { return b.y - a.y; }
        
        friend T area(const rect<T>& x) {
            auto y = x.b - x.a;
            return y.x * y.y;
        }
        
        T area() const {
            auto y = b - a;
            return y.x * y.y;
        }
        
        bool contains(T2 x) const {
            return (a.x <= x.x) && (a.y <= x.y) && (x.x < b.x) && (x.y < b.y);
        }
        
        T2 mid() {
            return (a + b) / 2.0f;
        }
        
    };
    
    struct area_cmp {
        template<typename A, typename B>
        bool operator()(A&& a, B&& b) const {
            return area(std::forward<A>(a)) < area(std::forward<B>(b));
        }
    };
    
    template<typename T>
    rect<T> operator+(const rect<T>& x) {
        return x;
    }
    
    template<typename T>
    rect<T> operator-(const rect<T>& x) {
        return rect(-x.b, -x.a);
    }
    
    // Minkowski sum
    
    template<typename T>
    rect<T> operator+(const rect<T>& a, const rect<T>& b) {
        return rect(a.a + b.a, a.b + b.b);
    }
    
    // Minkowski difference is defined so that
    //
    //     (A - B) + B == A
    //
    // but note that
    //
    //     A - B != A + (-B)
    
    template<typename T>
    rect<T> operator-(const rect<T>& a, const rect<T>& b) {
        return rect(a.a - b.a, a.b - b.b);
    }
    
    // shift
    template<typename T>
    rect<T> operator+(const rect<T>& a, const simd_TN_t<T, 2>& b) {
        return rect{a.a + b, a.b + b};
    }
    
    template<typename T>
    rect<T>& operator+=(rect<T>& a, const simd_TN_t<T, 2>& b) {
        a.a += b;
        a.b += b;
        return a;
    }
    
    template<typename T>
    rect<T> operator-(const rect<T>& a, const simd_TN_t<T, 2>& b) {
        return rect{a.a - b, a.b - b};
    }
    
    template<typename T>
    rect<T>& operator-=(rect<T>& a, const simd_TN_t<T, 2>& b) {
        a.a -= b;
        a.b -= b;
        return a;
    }
    
    
    // Scaling
    
    template<typename T>
    rect<T> operator*(const rect<T>& a, T b) {
        return rect<T>(a.a * b, a.b * b);
    }
    
    template<typename T>
    rect<T> operator*(T a, const rect<T>& b) {
        return rect<T>(a * b.a, a * b.b);
    }
    
    template<typename T>
    rect<T> operator/(const rect<T>& a, T b) {
        return rect<T>(a.a / b, a.b / b);
    }
    
    // Enclosing rect
    
    template<typename T>
    rect<T> hull(const rect<T>& a, const rect<T>& b) {
        return rect<T>(min(a.a.x, b.a.x),
                       min(a.a.y, b.a.y),
                       max(a.b.x, b.b.x),
                       max(a.b.y, b.b.y));
    }
    
    template<typename T>
    rect<T> hull(const rect<T>& a, const simd_TN_t<T, 2>& b) {
        return rect<T>(min(a.a.x, b.x),
                       min(a.a.y, b.y),
                       max(a.b.x, b.x),
                       max(a.b.y, b.y));
    }
    
    template<typename T>
    bool overlap(const rect<T>& a, const rect<T>& b) {
        return (((a.a.x < b.b.x) && (b.a.x < a.b.x)) &&
                ((a.a.x < b.b.x) && (b.a.x < a.b.x)));
    }
    
    template<typename T>
    rect<T> intersection(const rect<T>& a, const rect<T>& b) {
        return rect<T>(max(a.a.x, b.a.x),
                       max(a.a.y, b.a.y),
                       min(a.b.x, b.b.x),
                       min(a.b.y, b.b.y));
    }
    
} // namespace wry

#endif /* rect_hpp */
