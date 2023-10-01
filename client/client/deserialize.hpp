//
//  deserialize.hpp
//  client
//
//  Created by Antony Searle on 30/9/2023.
//

#ifndef deserialize_hpp
#define deserialize_hpp

#include <expected>

#include "cstdint.hpp"
#include "stdfloat.hpp"
#include "utility.hpp"
#include "array.hpp"
#include "string.hpp"


using float32_t = float;
using float64_t = double;

namespace wry {
    
    // Serde-inflected serialization and deserialization
    
    template<typename T, typename D>
    std::expected<T, typename std::decay_t<D>::Error> deserialize(D&& deserializer) {
        // tag dispatch to actual implementations
        return deserialize(std::in_place_type<T>, std::forward<D>(deserializer));
    }
            
    template<typename T, typename E>
    struct basic_visitor {
        std::expected<T, E> visit_int32_t(int32_t x) {
            return std::unexpected<E>(ENOTSUP);
        }
    };

#define X(T)\
    template<typename D>\
    std::expected<T, typename std::decay_t<D>::error_type>\
    deserialize(std::in_place_type_t<T>, D&& deserializer) {\
        using E = typename std::decay_t<D>::error_type;\
        struct visitor : basic_visitor<T, E> {\
            using value_type = T;\
            std::expected<T, E> visit_##T(T x) { return x; }\
        };\
        return std::forward<D>(deserializer).deserialize_##T(visitor{});\
    }
    
    WRY_X_OF_T_FOR_T_IN_FIXED_WIDTH_INTEGER_TYPES
    WRY_X_OF_T_FOR_T_IN_FIXED_WIDTH_FLOAT_TYPES
    
#undef X
    
    template<typename D>
    std::expected<string, typename std::decay_t<D>::error_type>
    deserialize(std::in_place_type_t<string>, D&& deserializer) {
        using E = typename std::decay_t<D>::error_type;
        struct visitor : basic_visitor<string, E> {
            using value_type = string;
            std::expected<value_type, E> visit_string(string s) const {
                return s;
            }
            std::expected<value_type, E> visit_bytes(array_view<unsigned char> b);
        };
        return std::forward<D>(deserializer).deserialize_string(visitor{});
    }
        
    
    template<typename ByteSource>
    struct binary_deserializer {
        
        using error_type = int;
        ByteSource _byte_source;
        array<unsigned char> _buffer;

#define X(T)\
        template<typename V>\
        std::expected<typename std::decay_t<V>::value_type, error_type>\
        deserialize_##T (V&& visitor) {\
            while (_buffer.size() < sizeof( T )) {\
                _buffer.may_write_back(sizeof( T ));\
                _byte_source.get_bytes(_buffer);\
            }\
            T x{};\
            memcpy(&x, _buffer.will_read_first(sizeof( T )),  sizeof( T ));\
            return std::forward<V>(visitor).visit_##T (x);\
        }
             
        WRY_X_OF_T_FOR_T_IN_FIXED_WIDTH_INTEGER_TYPES
        WRY_X_OF_T_FOR_T_IN_FIXED_WIDTH_FLOAT_TYPES

        template<typename V>
        std::expected<typename std::decay_t<V>::value_type, error_type>
        deserialize_any(V&& visitor) {
            return std::unexpected(ENOTSUP);
        };
        
        template<typename V>
        std::expected<typename std::decay_t<V>::value_type, error_type>
        deserialize_bool(V&& visitor) {
            return deserialize_int8_t(std::forward<V>(visitor));
        };

        template<typename V>
        std::expected<typename std::decay_t<V>::value_type, error_type>
        deserialize_bytes(V&& visitor) {
            while (_buffer.size() < sizeof(uint64_t)) {
                _buffer.may_write_back(sizeof(uint64_t));
                _byte_source.get_bytes(_buffer);
            }
            uint64_t count{};
            memcpy(&count, _buffer.will_read_first(sizeof(uint64_t)), count);
            while (_buffer.size() < count) {
                _buffer.may_write_back(count);
                _byte_source.get_bytes(_buffer);
            }
            array_view<const unsigned char> result(_buffer.will_read_first(count), count);
            return std::forward<V>(visitor).visit_bytes(result);
        };
        
        template<typename V>
        std::expected<typename std::decay_t<V>::value_type, error_type>
        deserialize_string(V&& visitor) {
            return deserialize_bytes(std::forward<V>(visitor));
        };
        
        struct sequence_accessor {
            binary_deserializer* _context;
            uint64_t _count;
            
            template<typename T>
            std::expected<std::optional<T>, error_type>
            next_element() {
                if (!_count--)
                    return std::optional<T>{};
                return deserialize<T>(*_context).transform([](T&& value) {
                    return std::optional<T>(std::move(value));
                });
            }
            
        };
        
        struct map_accessor {
            binary_deserializer* _context;
            uint64_t _count;
            bool _expect_value = false;
            
            template<typename K>
            std::expected<std::optional<K>, error_type>
            next_key() {
                if (!_count--)
                    return std::unexpected<error_type>(ERANGE);
                _expect_value = true;
                return deserialize<K>(*_context).transform([](K&& key) {
                    return std::optional<K>(std::move(key));
                });
            }
            
            template<typename V>
            std::expected<V, error_type>
            next_value() {
                if (!_expect_value)
                    return std::unexpected<error_type>(ERANGE);
                _expect_value = false;
                return deserialize<V>(*_context);
            }
            
            
        };
        
        template<typename V>
        std::expected<typename std::decay_t<V>::value_type, error_type>
        deserialize_sequence(V&& visitor) {
            while (_buffer.size() < sizeof(uint64_t)) {
                _buffer.may_write_back(sizeof(uint64_t));
                _byte_source.get_bytes(_buffer);
            }
            uint64_t count{};
            memcpy(&count, _buffer.will_read_first(sizeof(uint64_t)), count);
            return std::forward<V>(visitor).visit_sequence(sequence_accessor(this, count));
        }
        
        template<typename V>
        std::expected<typename std::decay_t<V>::value_type, error_type>
        deserialize_tuple(size_t count, V&& visitor) {
            return std::forward<V>(visitor).visit_sequence(sequence_accessor(this, count));
        }
        
    }; // binary_deserializer
    
    
    struct file_stream_byte_source {
        
        FILE* _stream;
        
        ~file_stream_byte_source() {
            [[maybe_unused]] int result = fclose(_stream);
            assert(result == 0);
            
        }
        
        void get_bytes(array<unsigned char>& buffer) {
            size_t count = fread(buffer.end(), 1, buffer.can_write_back(), _stream);
            buffer.did_write_back(count);
        }
        
    };
    
    struct memory_byte_source {
        
        array_view<unsigned char> _view;

        void get_bytes(array<unsigned char>& buffer) {
            size_t count = min(_view.can_read_first(), buffer.can_write_back());
            memcpy(buffer.will_write_back(count), _view.will_read_first(count), count);
        }

    };

    
    template<typename T>
    struct _deserialize_array_visitor {
        array<T> visit_sequence(auto&& access) {
            array<T> result;
            while (auto&& x = access.template next<T>())
                result.emplace_back(std::forward<decltype(x)>(x));
            return result;
        }
    };
    
    template<typename T>
    array<T> deserialize(std::in_place_type_t<array<T>>, auto&& deserializer) {
        return deserializer.deserialize_sequence(_deserialize_array_visitor<T>{});
    }
    
    
    
    
#define WRY_X_FOR_ALL_RUST_INTEGERS\
X(f32) X(f64)\
X(i8) X(i16) X(i32) X(i64) X(i128)\
X(u8) X(u16) X(u32) X(u64) X(u128)
    

    
}


#endif /* deserialize_hpp */
