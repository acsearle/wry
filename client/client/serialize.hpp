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
#include "Result.hpp"


namespace wry {
    
    using namespace rust;
    using namespace rust::result;
    using namespace rust::option;
    
    // Serialize primitives
    
#define X(T) template<typename S> Result<typename std::decay_t<S>::\
    Ok, typename std::decay_t<S>::Error> serialize(const T& x, S&&\
    serializer) { return std::forward<S>(serializer).serialize_##T(x); }
    
    WRY_X_OF_T_FOR_T_IN_FIXED_WIDTH_INTEGER_TYPES
    WRY_X_OF_T_FOR_T_IN_FIXED_WIDTH_FLOAT_TYPES
    X(string)
#undef X

    // Serializer sequences
    
    template<typename T, typename S>
    Result<typename std::decay_t<S>::Ok,
                  typename std::decay_t<S>::Error>
    serialize(const array_view<T>& x, S&& serializer) {
        using E = typename std::decay_t<S>::Error;
        auto seq = std::forward<S>(serializer).serialize_seq(Some(x.size()));
        if (seq.is_err())
            return Err(std::move(seq).unwrap_err());
        auto seq2 = std::move(seq).unwrap();
        for (const auto& e : x) {
            auto f = seq2.serialize_element(e);
            if (f.is_err())
                return Err(E());
        }
        return seq2.end();
    }
    
    
    
    template<typename ByteSink>
    struct binary_serializer {
        
        using value_type = std::monostate;
        using error_type = int;
        
        array<unsigned char> _buffer;
        ByteSink _sink;
        
        void _maybe_sink() {
            if (_buffer.size() >= 4096)
                _sink.set_bytes(_buffer);
        }
        
#define X(T) \
        Result<value_type, error_type> \
        serialize_##T ( T x) {\
            memcpy(_buffer.will_write_back(sizeof( T )), &x, sizeof( T ));\
            _maybe_sink();\
            return Ok(value_type{});\
        }
        
        WRY_X_OF_T_FOR_T_IN_FIXED_WIDTH_INTEGER_TYPES
        WRY_X_OF_T_FOR_T_IN_FIXED_WIDTH_FLOAT_TYPES
#undef X
        
        Result<value_type, error_type>
        serialize_bool(bool x) {
            return serialize_int8_t(x);
        }
        
        struct tuple_serializer {
            
            binary_serializer* _context;
            size_t _remaining;
            
            template<typename T>
            Result<std::monostate, error_type>
            serialize_element(const T& x) {
                if (!_remaining--)
                    return Err(ERANGE);
                return serialize(x, *_context);
            }
            
            Result<value_type, error_type>
            end() {
                if (_remaining)
                    return Err(ERANGE);
                return {};
            }
            
        };
        
        Result<tuple_serializer, error_type> serialize_tuple(size_t count) {
            return tuple_serializer{this, count};
        }
        
        using seq_serializer = tuple_serializer;
        
        Result<tuple_serializer, error_type> serialize_seq(Option<size_type> count) {
            if (count.is_none())
                return Err(ERANGE);
            auto n = std::move(count).unwrap();
            serialize_uint64_t(n);
            return serialize_tuple(n);
        }
        
        struct map_serializer {
            
            binary_serializer* _context;
            size_t _remaining;
            bool _expecting_value = false;
            
            template<typename K>
            Result<std::monostate, error_type> serialize_key(K&& key) {
                if (!_remaining || _expecting_value)
                    return Err(ERANGE);
                return serialize(std::forward<K>(key), *_context).and_then([this](auto&&) {
                    _expecting_value = true;
                    return std::monostate{};
                });
            }
            
            template<typename V>
            Result<std::monostate, error_type> serialize_value(V&& value) {
                if (!_remaining || !_expecting_value)
                    return Err(ERANGE);
                return serialize(std::forward<V>(value), *_context).and_then([this](auto&&) {
                    --_remaining;
                    _expecting_value = false;
                    return std::monostate{};
                });
            }
            
            Result<value_type, error_type> end() {
                if (_remaining || _expecting_value)
                    return Err(ERANGE);
                return Ok(value_type{});
            }
            
        };
        
        Result<map_serializer, error_type> serialize_map(Option<size_t> count) {
            if (count.is_none())
                return Err(ERANGE);
            size_type n = std::move(count).unwrap();
            serialize_uint64_t(n);
            return map_serializer{this, n};
        }
        
        struct struct_serializer {
            
            binary_serializer* _context;
            size_t _remaining;
            
            template<typename V>
            Result<std::monostate, error_type>
            serialize_field(string_view key, V&& value) {
                if (!_remaining)
                    return Err(ERANGE);
                return serialize(key, *_context)
                    .and_then([this, value=std::forward<V>(value)](auto&&) mutable {
                        return serialize(std::forward<V>(value), *_context)
                            .and_then([this](auto&&) {
                                --_remaining;
                                return std::monostate{};
                            });
                    });
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
