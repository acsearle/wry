//
//  deserialize.hpp
//  client
//
//  Created by Antony Searle on 30/9/2023.
//

#ifndef deserialize_hpp
#define deserialize_hpp

#include "stdint.hpp"
#include "stdfloat.hpp"
#include "utility.hpp"
#include "array.hpp"
#include "string.hpp"

#include "Option.hpp"

namespace wry {
    
    using namespace rust::option;
    
    // serde-rs deserialization
    
    template<typename T, typename D>
    T deserialize(D&& deserializer) {
        // tag dispatch to actual implementations
        return deserialize(std::in_place_type<T>, std::forward<D>(deserializer));
    }
    
    
    
    // define visitors
            
    template<typename T>
    struct basic_visitor {
        
        using Value = T;
        
        // default implementations
        
#define X(T)\
        void visit_##T(T x) { throw ENOTSUP; }

        WRY_X_OF_T_FOR_T_IN_FIXED_WIDTH_INTEGER_TYPES
        WRY_X_OF_T_FOR_T_IN_FIXED_WIDTH_FLOAT_TYPES

        X(string)
        
#undef X

    };
    
    
    // define deserialization

#define X(T)\
    struct visitor_##T : basic_visitor<T> {\
        T visit_##T(T x) { return x; }\
    };\
    \
    template<typename D>\
    T deserialize(std::in_place_type_t<T>, D&& deserializer) {\
        return std::forward<D>(deserializer).deserialize_##T(visitor_##T{});\
    }
    
    WRY_X_OF_T_FOR_T_IN_FIXED_WIDTH_INTEGER_TYPES
    WRY_X_OF_T_FOR_T_IN_FIXED_WIDTH_FLOAT_TYPES
    
#undef X
    
    struct visitor_bool  {
        
#define X(T)\
        bool visit_##T (T value) const {\
            return static_cast<bool>(value);\
        }
        
        WRY_X_OF_T_FOR_T_IN_FIXED_WIDTH_INTEGER_TYPES
        WRY_X_OF_T_FOR_T_IN_FIXED_WIDTH_FLOAT_TYPES
        
#undef X
        
    };

    template<typename D>
    bool deserialize(std::in_place_type_t<bool>, D&& deserializer) {
        return std::forward<D>(deserializer).deserialize_string(visitor_bool{});
    }
    
    struct visitor_string : basic_visitor<string> {
        using Value = string;
        Value visit_string(string s) const {
            return s;
        }
        
    };
    
    template<typename D>
    string deserialize(std::in_place_type_t<string>, D&& deserializer) {
        return std::forward<D>(deserializer).deserialize_string(visitor_string{});
    }
        
    
    
    // simple little-endian binary deserializer
    
    template<typename ByteSource>
    struct binary_deserializer {
                
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
        typename std::decay_t<V>::Value deserialize_##T (V&& visitor) {\
            _ensure_available(sizeof( T ));\
            T x{};\
            memcpy(&x, _buffer.will_read_first(sizeof( T )),  sizeof( T ));\
            return std::forward<V>(visitor).visit_##T (x);\
        }
             
        WRY_X_OF_T_FOR_T_IN_FIXED_WIDTH_INTEGER_TYPES
        WRY_X_OF_T_FOR_T_IN_FIXED_WIDTH_FLOAT_TYPES
#undef X
        
        template<typename V>
        typename std::decay_t<V>::Value deserialize_any(V&& visitor) {
            throw ENOTSUP;
        };
        
        template<typename V>
        typename std::decay_t<V>::Value deserialize_bool(V&& visitor) {
            return deserialize_int8_t(std::forward<V>(visitor));
        };

        template<typename V>
        typename std::decay_t<V>::Value deserialize_bytes(V&& visitor) {
            if (!_ensure_available(sizeof(uint64_t)))
                throw ERANGE;
            uint64_t count = {};
            memcpy(&count, _buffer.will_read_first(sizeof(uint64_t)), count);
            if (!_ensure_available(count))
                throw ERANGE;
            array_view<const unsigned char> result(_buffer.will_read_first(count), count);
            return std::forward<V>(visitor).visit_bytes(result);
        };
        
        template<typename V>
        typename std::decay_t<V>::Value
        deserialize_string(V&& visitor) {
            return deserialize_bytes(std::forward<V>(visitor));
        };
        
        struct SeqAccess {
            binary_deserializer* _context;
            uint64_t _count;
            
            template<typename T>
            Option<T> next_element() {
                if (!_count--)
                    return None();
                return Some(deserialize<T>(*_context));
            }
            
            Option<size_type> size_hint() {
                return Some(_count);
            }
            
        };
        
        struct map_accessor {
            binary_deserializer* _context;
            uint64_t _count;
            bool _expect_value = false;
            
            template<typename K>
            Option<K> next_key() {
                if (!_count--)
                    return None();
                _expect_value = true;
                return deserialize<K>(*_context);
            }
            
            template<typename V>
            V next_value() {
                if (!_expect_value)
                    throw ERANGE;
                _expect_value = false;
                return deserialize<V>(*_context);
            }
            
            
        };
        
        template<typename V>
        typename std::decay_t<V>::V deserialize_sequence(V&& visitor) {
            while (_buffer.size() < sizeof(uint64_t)) {
                _buffer.may_write_back(sizeof(uint64_t));
                _byte_source.get_bytes(_buffer);
            }
            uint64_t count = {};
            memcpy(&count, _buffer.will_read_first(sizeof(uint64_t)), count);
            return std::forward<V>(visitor).visit_sequence(sequence_accessor(this, count));
        }
        
        template<typename V>
        typename std::decay_t<V>::Value
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
        
        using Value = array<T>;
        
        template<typename A>
        array<T> visit_seq(A&& access) {
            array<T> x;
            for (;;) {
                Option<T> y = access.template next_element<T>();
                if (y.is_some())
                    x.emplace_back(std::move(y).unwrap());
                else
                    return std::move(x);
            }
        }
    };
    
    template<typename T, typename D>
    array<T> deserialize(std::in_place_type_t<array<T>>, D&& deserializer) {
        return std::forward<D>(deserializer).deserialize_seq(_deserialize_array_visitor<T>{});
    }
    
    
    
    
#define WRY_X_FOR_ALL_RUST_INTEGERS\
X(f32) X(f64)\
X(i8) X(i16) X(i32) X(i64) X(i128)\
X(u8) X(u16) X(u32) X(u64) X(u128)
    

    
}


#endif /* deserialize_hpp */
