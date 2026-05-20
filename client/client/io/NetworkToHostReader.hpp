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

    template<std::integral T>
    struct NetworkByteOrder {
        unsigned char raw[sizeof(T)];
        operator T() const {
            return ntoh(std::bit_cast<T>(raw));
        };
    };
    
    template<typename T, typename F, F scale_factor>
    struct FixedPoint {
        T raw;
        operator F() const {
            return raw * scale_factor;
        }
    };
        
} // namespace wry



#endif /* NetworkToHostReader_hpp */
