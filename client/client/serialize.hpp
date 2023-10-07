//
//  serialize.hpp
//  client
//
//  Created by Antony Searle on 26/6/2023.
//

#ifndef serialize_hpp
#define serialize_hpp

#include <variant>

#include "array.hpp"
#include "stdint.hpp"
#include "type_traits.hpp"
#include "stdfloat.hpp"
#include "string.hpp"
#include "string_view.hpp"

#include "Option.hpp"


namespace wry {
    
    using namespace rust;
    using namespace rust::option;
    
    // Serialize primitives
    
#define X(T) template<typename S>\
    void serialize(const T& x, S&& serializer) {\
        return std::forward<S>(serializer).serialize_##T(x);\
    }
    
    WRY_X_OF_T_FOR_T_IN_FIXED_WIDTH_INTEGER_TYPES
    WRY_X_OF_T_FOR_T_IN_FIXED_WIDTH_FLOAT_TYPES
    X(string)
#undef X

    // Serialize sequences
    
    template<typename T, typename S> 
    void serialize(const array_view<T>& x, S&& serializer) {
        auto seq = std::forward<S>(serializer).serialize_seq(Some(x.size()));
        for (const auto& e : x)
            seq.serialize_element(e);
        seq.end();
    }
    
    template<typename ByteSink>
    struct binary_serializer {
        
        array<unsigned char> _buffer;
        ByteSink _sink;
        
        void _maybe_sink() {
            if (_buffer.size() >= 4096)
                _sink.set_bytes(_buffer);
        }
        
#define X(T) \
        void serialize_##T(const T& x) {\
            memcpy(_buffer.will_write_back(sizeof(T)), &x, sizeof(T));\
            _maybe_sink();\
        }
        
        WRY_X_OF_T_FOR_T_IN_FIXED_WIDTH_INTEGER_TYPES
        WRY_X_OF_T_FOR_T_IN_FIXED_WIDTH_FLOAT_TYPES
#undef X
        
        void serialize_bool(bool x) {
            return serialize_int8_t(x);
        }
        
        struct SerializeTuple {
            
            binary_serializer* _context;
            size_t _remaining;
            
            template<typename T> 
            void serialize_element(const T& x) {
                if (!_remaining--)
                    throw ERANGE;
                serialize(x, *_context);
            }
            
            void end() {
                if (_remaining)
                    throw ERANGE;
            }
            
        };
        
        SerializeTuple serialize_tuple(size_t count) {
            SerializeTuple{this, count};
        }
        
        using SerializeSeq = SerializeTuple;
        
        SerializeSeq serialize_seq(Option<size_type> count) {
            if (count.is_none())
                throw ERANGE;
            auto n = std::move(count).unwrap();
            serialize_uint64_t(n);
            return SerializeSeq(n);
        }
        
        struct SerializeMap {
            
            binary_serializer* _context;
            size_t _remaining;
            bool _expecting_value = false;
            
            template<typename K>
            void serialize_key(K&& key) {
                if (!_remaining || _expecting_value)
                    throw ERANGE;
                serialize(std::forward<K>(key), *_context);
                _expecting_value = true;
            }
            
            template<typename V>
            void serialize_value(V&& value) {
                if (!_remaining || !_expecting_value)
                    throw ERANGE;
                serialize(std::forward<V>(value), *_context);
                --_remaining;
                _expecting_value = false;
            }
            
            void end() {
                if (_remaining || _expecting_value)
                    throw ERANGE;
            }
            
        };
        
        SerializeMap serialize_map(Option<size_type> count) {
            if (count.is_none())
                throw ERANGE;
            size_type n = std::move(count).unwrap();
            serialize_uint64_t(n);
            return SerializeMap{this, n};
        }
        
        struct SerializeStruct {
            
            binary_serializer* _context;
            size_t _remaining;
            
            template<typename V>
            void serialize_field(string_view key, V&& value) {
                if (!_remaining)
                    throw ERANGE;
                serialize(key, *_context);
                serialize(std::forward<V>(value), *_context);
                --_remaining;
            }
            
        };
        
    }; // struct binary_serializer
    
    
    struct file_stream_byte_sink {

        FILE* _file_stream;

        ~file_stream_byte_sink() {
            [[maybe_unused]] int result = fclose(_file_stream);
            assert(result == 0);
        }
        
        template<typename T>
        void set_bytes(array_view<T>& buffer) {
            if (buffer.can_read_first()) {
                size_t n = fwrite(buffer.may_read_first(), sizeof(T), buffer.can_read_first(), _file_stream);
                buffer.did_read_first(n);
            }
        }
        
    };
    
    struct memory_byte_sink {
        
        array_view<unsigned char> _byte_view;
        
        template<typename T>
        void set_bytes(array_view<T>& buffer) {
            size_t n = _byte_view.can_overwrite_first() / sizeof(T);
            size_t m = buffer.size();
            size_t count = min(m, n);
            memcpy(_byte_view.will_overwrite_first(count * sizeof(T)),
                   buffer.will_read_first(count),
                   count * sizeof(T));
        }
                
    };
    
    
    
   
    
} // namespace wry

#endif /* serialize_hpp */
