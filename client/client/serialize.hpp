//
//  serialize.hpp
//  client
//
//  Created by Antony Searle on 26/6/2023.
//

#ifndef serialize_hpp
#define serialize_hpp

#include <cstdio>

#include "common.hpp"

#include <vector>

namespace wry {
    
    // template<typename T, typename Serializer>
    // void serialize(T const&, Serializer&);
    
    // template<typename T, typename Deserializer>
    // T deserialize(Deserializer&);
    
    // To implement serialization for a (template) type, provide an overload of
    // serialize for the (template) type.  The implementation should call
    // serialize on its components in some fashion.  This recursion will ultimately
    // resolve to fundamental types provided by the Serializer.
    //
    // template<typename A, typename B, typename Serializer>
    // void serialize(std::pair<A, B> const& x, Serializer& s) {
    //     serialize(x.first, s);
    //     serialize(x.second, s);
    // }
    //
    // To implement a Serializer, provide overloads of serialize for the
    // Serializer and each fundamental type:
    //
    // void serialize(std::size_t, std::FILE*& s) {
    //     fwrite(&x, sizeof(x), 1, s);
    // }
    //
    // (Note that this serializer will expose both the endianness and word size
    // of the platform)
    //
    // To implement deserialization for a (template) type, provide an overload of
    // deserialize **using placeholder<T> as first argument** to overcome C++'s
    // restriction on partial specialization of template functions.  The
    // implementation shoud call deserialize on its components in some fashion.
    // This recursion will ultimately resolve to fundamental types provided by the
    // Deserializer.
    //
    // (use std::in_place_type_t as placeholder<T>?)
    //
    // template<typename A, typename B, typename Deserializer>
    // std::pair<A, B> deserialize(placeholder<std::pair<A, B>>, Deserializer& d) {
    //     auto a = deserialize<A>(d);
    //     auto b = deserialize<B>(d);
    //     // note that
    //     //     return std::pair(deserialize<A>(d), deserialize<B>(d))
    //     // would be incorrect because the order of evaluation of arguments is
    //     // unspecified
    //     return std::pair(std::move(a), std::move(b));
    // }
    //
    // To implement a Deserializer, provide an overload of deserialize for each
    // fundamental type and the Deserializer
    //
    // std::size_t deserialize(placeholder<std::size_t>, std::FILE*& d) {
    //     std::size_t x = 0;
    //     fread(&x, sizeof(x), 1, d);
    //     return x;
    // }
    
    
    
    template<typename T>
    struct placeholder {};
    
    template<typename T, typename Deserializer>
    T deserialize(Deserializer& d) {
        return deserialize(placeholder<T>{}, d);
    }
    
    
    // Binary, native (little) endian serialization and deserialization
    
#define X(T)\
\
inline void serialize(T const& x, std::FILE*& s) {\
[[maybe_unused]] auto r = fwrite(&x, sizeof(x), 1, s);\
assert(r == 1);\
}\
\
inline auto deserialize(placeholder< T >, std::FILE*& d) {\
T x;\
[[maybe_unused]] auto r = fread(&x, sizeof(x), 1, d);\
assert(r == 1);\
return x;\
}
    
    // Note that long and long long are distinct types even though they have
    // identical properties on LP64 systems
    
    X(char)
    X(signed char)
    X(unsigned char)
    
    X(short)
    X(int)
    X(long)
    X(long long)
    
    X(unsigned short)
    X(unsigned)
    X(unsigned long)
    X(unsigned long long)
    
    X(float)
    X(double)
    X(long double)
    
#undef X
    
    
    // std::pair
    
    template<typename A, typename B, typename Serializer>
    void serialize(std::pair<A, B> const& x, Serializer& s) {
        serialize(x.first, s);
        serialize(x.second, s);
    }
    
    template<typename A, typename B, typename Deserializer>
    auto deserialize(placeholder<std::pair<A, B>>, Deserializer& d) {
        auto a = deserialize<A>(d);
        auto b = deserialize<B>(d);
        // note that
        //     return std::pair(deserialize<A>(d), deserialize<B>(d))
        // would be incorrect because the order of evaluation of arguments is
        // unspecified
        return std::pair(std::move(a), std::move(b));
    }
    
    
    // std::vector
    
    template<typename T, typename Serializer>
    void serialize(std::vector<T> const& x, Serializer& s) {
        serialize(x.size(), s);
        for (auto&& y : x)
            serialize(y, s);
    }
    
    template<typename T, typename Deserializer>
    auto deserialize(placeholder<std::vector<T>>, Deserializer& d) {
        auto n = deserialize<std::size_t>(d);
        std::vector<T> x;
        x.reserve(n);
        while (n--)
            x.push_back(deserialize<T>(d));
        return x;
    }
    
    
    
} // namespace wry

#endif /* serialize_hpp */
