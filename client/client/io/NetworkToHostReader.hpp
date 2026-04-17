//
//  NetworkToHostReader.hpp
//  client
//
//  Created by Antony Searle on 26/3/2026.
//

#ifndef NetworkToHostReader_hpp
#define NetworkToHostReader_hpp

#include <arpa/inet.h>
#include <cstdio>

#include <cstring>
#include <ranges>

#include "span.hpp"

namespace wry {
        
    using byte = unsigned char;

//    template<typename T>
//    T sgn(T x) {
//        return (T{0} < x) - (x < T{0});
//    }
    
    // Binary file assist
        
    // overload network-to-host
    
    template<std::integral T>
    T ntoh(T x) {
        if constexpr (sizeof(T) == 1) {
            return x;
        } else if constexpr (sizeof(T) == 2) {
            return ntohs(x);
        } else if constexpr (sizeof(T) == 4) {
            return ntohl(x);
        } else if constexpr (sizeof(T) == 8) {
            return ntohll(x);
        }
    }
    
    template<std::integral T>
    void parse_ntoh(span<byte const>& s, T& x) {
        std::memcpy(&x, s.data(), sizeof(T));
        s.drop_front(sizeof(T));
        x = ntoh(x);
    }

    template<std::integral... Ts>
    void parse_ntoh(span<byte const>& s, Ts&... xs) {
        (parse_ntoh(s, xs), ...);
    }
    
    template<std::ranges::range T>
    void parse_ntoh(span<byte const>&s, T& t) {
        for (auto& a : t)
            parse_ntoh(s, a);
    }
    

    // TODO: Clean up; probably not needed at all

    using Bytes = span<byte const>;
    
    struct NetworkToHostReader {
        
        span<byte const> s;
        
        void skip(std::size_t n) {
            s.drop_front(n);
        }
        
        template<typename T>
        void read(T& x) {
            std::memcpy(&x, s.data(), sizeof(T));
            s.drop_front(sizeof(T));
            if constexpr (std::is_integral_v<T>) {
                x = ntoh(x);
            }
        }
        
        template<typename T, typename T2, typename... Ts>
        void read(T& x, T2& x2, Ts&... xs) {
            read(x);
            read(x2, xs...);
        }
        
        template<typename T>
        T read() {
            T x{};
            read(x);
            return x;
        }
        
    };
    
    using Reader = NetworkToHostReader;
    
    template<std::integral T>
    bool read(span<byte const>& s, T& x) {
        if (s.size() < sizeof(T))
            return false;
        std::memcpy(&x, s.data(), sizeof(T));
        s.drop_front(sizeof(T));
        x = ntoh(x);
        return true;
    }
    
    template<typename T>
    struct NetworkByteOrder {
        unsigned char raw[sizeof(T)];
        operator T() const {
            T x = {};
            std::memcpy(&x, raw, sizeof(T));
            x = ntoh(x);
            return x;
        };
    };
    
    template<typename T, typename F, F k>
    struct FixedPoint {
        T raw;
        operator F() const {
            return raw * k;
        }
    };
        
} // namespace wry



#endif /* NetworkToHostReader_hpp */
