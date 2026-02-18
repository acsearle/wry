//
//  interval.hpp
//  client
//
//  Created by Antony Searle on 7/1/2024.
//

#ifndef interval_hpp
#define interval_hpp

#include <cassert>
#include <utility>

namespace wry {
    
    // basic interval arithmetic
    //
    // does not consider rounding, being intended for numerical bounding on
    // larger scales
    //
    // floats and intervals are partially ordered so careful choice of
    // comparison operators are needed
    
    template<typename T = double>
    struct interval {
        
        T a, b;
        
        bool invariant() const {
            return a <= b;
        }
    
        interval() : a(0), b(0) {}
        interval(T x) : a(x), b(x) {}
        interval(T x, T y) : a(x), b(y) { assert(invariant()); }

        interval(const interval&) = default;
        interval(interval&&) = default;
        ~interval() = default;
        interval& operator=(const interval&) = default;
        interval& operator=(interval&&) = default;
                
    }; // struct interval<T>
    
    template<typename T> interval(T&& a) -> interval<std::decay_t<T>>;
    template<typename T, typename U> interval(T&& a, U&& b) -> interval<std::common_type_t<T, U>>;
    
    template<typename T>
    interval<T> hull(T a, T b) {
        return (a <= b) ? interval(a, b) : interval(b , a);
    }
    
    template<typename T>
    interval<T> hull(interval<T> x, interval<T> y) {
        return interval<T>(min(x.a, y.a), max(x.a, y.a));
    }
    
    template<typename T, typename U, typename V = std::common_type_t<T, U>>
    interval<V> operator+(const T& x, const T& y) {
        return interval(x.a + y.a, x.b + y.b);
    }

    template<typename T, typename U, typename V = std::common_type_t<T, U>>
    interval<V> operator-(const T& x, const T& y) {
        return interval(x.a - y.b, x.b - y.a);
    }
    
    template<typename T>
    interval<T> min(const interval<T>& x, const interval<T>& y) {
        return interval(min(x.a, y.a), min(x.b, y.b));
    }

    template<typename T>
    interval<T> max(const interval<T>& x, const interval<T>& y) {
        return interval(max(x.a, y.a), max(x.b, y.b));
    }
    
    template<typename T>
    T len(const interval<T>& x) {
        return x.b - x.a;
    }
    
    template<typename T>
    T mid(const interval<T>& x) {
        return (x.a + x.b) * 0.5;
    }
    
    template<typename T>
    interval<T> sqr(const interval<T>& x) {
        if (0 <= x.a)
            return interval<T>(x.a * x.a, x.b * x.b);
        else if (x.b <= 0)
            return interval<T>(x.b * x.b, x.a * x.a);
        else
            return interval<T>(0, max(x.a * x.a, x.b * x.b));
    }

    
    template<typename T = double, typename U = T>
    struct differential {
        
        T x;
        U g;
        
        static differential constant(T a) { return differential(a, 0); }
        static differential variable(T a) { return differential(a, 1); }
        
        bool operator==(const differential& other) const {
            return x == other.x;
        }
        
        auto operator<=>(const differential& other) const {
            return x <=> other.x;
        }
        
    };
    
    template<typename T, typename U>
    differential<T, U> operator+(const differential<T, U>& a, const differential<T, U>& b) {
        return differential<T, U>(a.x + b.x, a.g + b.g);
    }

    template<typename T, typename U>
    differential<T, U> operator-(const differential<T, U>& a, const differential<T, U>& b) {
        return differential<T, U>(a.x - b.x, a.g - b.g);
    }
    
    // The Hessian matrix is the transpose of the Jacobian of the gradient of a
    // function, and the trace of the Hessian is the Laplacian
    // H(f(x)) = J(\nabla f(x))^T
    // Tr H = \nabla^2 f(x)

    
} // namespace wry

#endif /* interval_hpp */
