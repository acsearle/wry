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

#include "span.hpp"

namespace wry {
    
    // TODO: Clean up; probably not needed at all
    
    using byte = unsigned char;

    template<typename T>
    T sgn(T x) {
        return (T{0} < x) - (x < T{0});
    }
    
    // Binary file assist
        
    // overload network-to-host
    
    inline char ntoh(char x) {
        return x;
    }

    inline signed char ntoh(signed char x) {
        return x;
    }

    inline unsigned char ntoh(unsigned char x) {
        return x;
    }

    inline short ntoh(short x) {
        return ntohs(x);
    }
    
    inline unsigned short ntoh(unsigned short x) {
        return ntohs(x);
    }

    inline int ntoh(int x) {
        return ntohl(x);
    }
    
    inline unsigned int ntoh(unsigned int x) {
        return ntohl(x);
    }
    
    inline long ntoh(long x) {
        return ntohl(x);
    }
    
    inline unsigned long ntoh(unsigned long x) {
        return ntohl(x);
    }
    
    inline long long ntoh(long long x) {
        return ntohll(x);
    }
    
    inline unsigned long long ntoh(unsigned long long x) {
        return ntohll(x);
    }

    
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
    void parse_ntoh(span<byte const>& s, T& x) {
        x = {};
        printf("%p\n", s._begin);
        std::memcpy(&x, s.data(), sizeof(T));
        printf("%d\n", (unsigned int)x);
        s.drop_front(sizeof(T));
        printf("%p\n", s._begin);
        x = ntoh(x);
        printf("read %x, %zd\n", (int) x, sizeof(T));
    }
}

#endif /* NetworkToHostReader_hpp */
