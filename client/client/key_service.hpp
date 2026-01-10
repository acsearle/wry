//
//  key_service.hpp
//  client
//
//  Created by Antony Searle on 18/12/2025.
//

#ifndef key_service_hpp
#define key_service_hpp

#include <tuple>

#include "concepts.hpp"
#include "stdint.hpp"
#include "type_traits.hpp"
#include "utility.hpp"

namespace wry {
        
    template<typename>
    struct DefaultKeyService;
    
    template<std::integral T>
    struct DefaultKeyService<T> {
        using key_type = T;
        using hash_type = std::make_unsigned_t<T>;
        constexpr hash_type hash(key_type key) const { return key; }
        constexpr key_type unhash(hash_type z) const { return z; }
        constexpr bool compare(key_type a, key_type b) const {
            return a < b;
        }
    };
    
    // TODO: This should use a DefaultKeyService for A and B individually
    template<typename A, typename B>
    struct DefaultKeyService<std::pair<A, B>> {
        using key_type = std::pair<A, B>;
        using hash_type = unsigned_integer_of_byte_width_t<sizeof(A) + sizeof(B)>;
        constexpr hash_type hash(key_type key) const {
            hash_type z = {};
            // Assemble respecting little-endianness
            __builtin_memcpy(&z, &key.second, sizeof(B));
            __builtin_memcpy((char*)&z + sizeof(B), &key.first, sizeof(A));
            return z;
        }
        constexpr hash_type mask_first() {
            hash_type z{};
            __builtin_memset((char*)&z + sizeof(B), -1, sizeof(A));
            return z;
        }
        constexpr hash_type mask_second() {
            hash_type z{};
            __builtin_memset(&z, -1, sizeof(B));
            return z;
        }
        constexpr key_type unhash(hash_type z) const {
            key_type key{};
            // Assemble respecting little-endianness
            __builtin_memcpy(&key.second, &z, sizeof(B));
            __builtin_memcpy(&key.first, (char*)&z + sizeof(B), sizeof(A));
            return key;
        }
        constexpr bool compare(key_type a, key_type b) const {
            return hash(a) < hash(b);
        }
    };
    
}

#endif /* key_service_hpp */
