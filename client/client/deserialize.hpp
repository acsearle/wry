//
//  deserialize.hpp
//  client
//
//  Created by Antony Searle on 30/9/2023.
//

#ifndef deserialize_hpp
#define deserialize_hpp

#include <expected>

#include "stdint.hpp"
#include "stdfloat.hpp"
#include "utility.hpp"
#include "array.hpp"
#include "string.hpp"

namespace wry {
    
    // serde-rs deserialization
    
    template<typename T, typename D>
    std::expected<T, typename std::decay_t<D>::error_type> deserialize(D&& deserializer) {
        // tag dispatch to actual implementations
        return deserialize(std::in_place_type<T>, std::forward<D>(deserializer));
    }
    
    
    
    // define visitors
            
    template<typename T, typename E>
    struct basic_visitor {
        
        using value_type = T;
        
        // default implementations
        
#define X(T)\
        std::expected<T, E> visit_##T (T x) { return std::unexpected<E>(ENOTSUP); }

        WRY_X_OF_T_FOR_T_IN_FIXED_WIDTH_INTEGER_TYPES
        WRY_X_OF_T_FOR_T_IN_FIXED_WIDTH_FLOAT_TYPES

        X(string)
        
#undef X

    };
    
    
    // define deserialization

#define X(T)\
    template<typename D>\
    std::expected<T, typename std::decay_t<D>::error_type>\
    deserialize(std::in_place_type_t<T>, D&& deserializer) {\
        using E = typename std::decay_t<D>::error_type;\
        struct visitor : basic_visitor<T, E> {\
            std::expected<T, E> visit_##T(T x) { return x; }\
        };\
        return std::forward<D>(deserializer).deserialize_##T(visitor{});\
    }
    
    WRY_X_OF_T_FOR_T_IN_FIXED_WIDTH_INTEGER_TYPES
    WRY_X_OF_T_FOR_T_IN_FIXED_WIDTH_FLOAT_TYPES
    
#undef X
    
    template<typename D>
    std::expected<bool, typename std::decay_t<D>::error_type>
    deserialize(std::in_place_type_t<bool>, D&& deserializer) {
        using E = typename std::decay_t<D>::error_type;
        struct visitor : basic_visitor<bool, E> {
#define X(T)\
            std::expected<bool, E> visit_##T (T value) const {\
                return static_cast<bool>(value);\
            }
            WRY_X_OF_T_FOR_T_IN_FIXED_WIDTH_INTEGER_TYPES
            WRY_X_OF_T_FOR_T_IN_FIXED_WIDTH_FLOAT_TYPES
#undef X
        };
        return std::forward<D>(deserializer).deserialize_string(visitor{});
    }
    
    
    template<typename D>
    std::expected<string, typename std::decay_t<D>::error_type>
    deserialize(std::in_place_type_t<string>, D&& deserializer) {
        using E = typename std::decay_t<D>::error_type;
        struct visitor : basic_visitor<string, E> {
            using value_type = string;
            std::expected<value_type, E> visit_string(string s) const {
                return std::move(s);
            }
        };
        return std::forward<D>(deserializer).deserialize_string(visitor{});
    }
        
    
    
    // simple little-endian binary deserializer
    
    template<typename ByteSource>
    struct binary_deserializer {
        
        using error_type = int;
        
        ByteSource _byte_source;
        array<unsigned char> _buffer;
        
        bool _ensure_available(size_t n) {
            for (;;) {
                size_t m = _buffer.size();
                if (!(n < m))
                    return true;
                size_t k = max(m - n, 4096);
                _buffer.may_write_back(k);
                _byte_source.get_bytes(_buffer);
                if (k == m)
                    return false;
            }
        }

#define X(T)\
        template<typename V>\
        std::expected<typename std::decay_t<V>::value_type, error_type>\
        deserialize_##T (V&& visitor) {\
            _ensure_available(sizeof( T ));\
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
            if (!_ensure_available(sizeof(uint64_t)))
                return std::unexpected(ERANGE);
            uint64_t count{};
            memcpy(&count, _buffer.will_read_first(sizeof(uint64_t)), count);
            if (!_ensure_available(count))
                return std::unexpected(ERANGE);
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
        using value_type = array<T>;
        
        template<typename A>
        std::expected<array<T>, typename std::decay_t<A>::error_type>
        visit_seq(A&& access) {
            array<T> result;
            for (;;) {
                printf("hello\n");
                std::optional<T> x = access.template next_element<T>();
                if (x)
                    result.emplace_back(*std::move(x));
                else
                    return std::move(result);
            }
        }
    };
    
    template<typename T, typename D>
    std::expected<array<T>, typename std::decay_t<D>::error_type> deserialize(std::in_place_type_t<array<T>>, D&& deserializer) {
        return std::forward<D>(deserializer).deserialize_seq(_deserialize_array_visitor<T>{});
    }
    
    
    
    
#define WRY_X_FOR_ALL_RUST_INTEGERS\
X(f32) X(f64)\
X(i8) X(i16) X(i32) X(i64) X(i128)\
X(u8) X(u16) X(u32) X(u64) X(u128)
    

    
}


#endif /* deserialize_hpp */
