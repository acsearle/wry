//
//  binary.hpp
//  client
//
//  Created by Antony Searle on 30/9/2023.
//

#ifndef binary_hpp
#define binary_hpp

#include <optional>
#include <cstdio>

#include "array.hpp"
#include "Option.hpp"
#include "stdfloat.hpp"
#include "stdint.hpp"
#include "string.hpp"

namespace wry {

    // Notes on serializaton:
    
    // We have a couple of use cases
    // - Actions to network
    // - Game state to disk
    // - Load assets
    // - Testing and debugging
    
    // We give serious consideration to sqlite for any disk access, particularly
    // the valuable game state.  This may either be in the form of opaque binary
    // blobs, or actually expressing state in database types/tables
    
    // NOTE: Serde (and JSON) serializes trees.  For more general graphs we
    // must accept duplication, or perform some kind of processing to represent
    // pointers, or use an entirely different strategy
    
    // NOTE: Processing the graph to handle pointers is related to what sqlite
    // would have to do to build a relational database representation.

    
    // Patterned after Rust Serde
    
    // Import some Rust-like vocabulary types
    using rust::option::Option;
    using rust::option::Some;
    using rust::option::None;
    
    // trivial little-endian binary serializer
    
    template<typename ByteSink>
    struct binary_serializer {
        
        ContiguousDeque<unsigned char> _buffer;
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
            void serialize_field(StringView key, V&& value) {
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
        void set_bytes(ContiguousView<T>& buffer) {
            if (buffer.can_read_first()) {
                size_t n = fwrite(buffer.may_read_first(), sizeof(T), buffer.can_read_first(), _file_stream);
                buffer.did_read_first(n);
            }
        }
        
    };
    
    struct memory_byte_sink {
        
        ContiguousView<unsigned char> _byte_view;
        
        template<typename T>
        void set_bytes(ContiguousView<T>& buffer) {
            size_t n = _byte_view.can_overwrite_first() / sizeof(T);
            size_t m = buffer.size();
            size_t count = min(m, n);
            memcpy(_byte_view.will_overwrite_first(count * sizeof(T)),
                   buffer.will_read_first(count),
                   count * sizeof(T));
        }
        
    };
    
    
    
    
    
    
    
    // trivial little-endian binary deserializer
    
    template<typename ByteSource>
    struct binary_deserializer {
        
        ByteSource _byte_source;
        ContiguousDeque<unsigned char> _buffer;
        
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
            ContiguousView<const unsigned char> result(_buffer.will_read_first(count), count);
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
        
        file_stream_byte_source(const file_stream_byte_source&) = delete;
        
        file_stream_byte_source(file_stream_byte_source&& other)
        : _stream(std::exchange(other._stream, nullptr)) {
        }

        ~file_stream_byte_source() {
            [[maybe_unused]] int result = fclose(_stream);
            assert(result == 0);
            
        }
                  
        void swap(file_stream_byte_source& other) {
            std::swap(_stream, other._stream);
        }
        
        file_stream_byte_source& operator=(const file_stream_byte_source&) = delete;
        file_stream_byte_source& operator=(file_stream_byte_source& other) {
            file_stream_byte_source(std::move(other)).swap(*this);
            return *this;
        }
        
        void get_bytes(ContiguousDeque<byte>& buffer) {
            size_t count = fread(buffer.end(), 1, buffer.can_write_back(), _stream);
            buffer.did_write_back(count);
        }
        
        void read(ContiguousDeque<byte>& buffer) {
            return get_bytes(buffer);
        }
        
    };
    
    struct memory_byte_source {
        
        ContiguousView<const byte> _view;
        
        void get_bytes(ContiguousDeque<byte>& buffer) {
            size_t count = min(_view.can_read_first(), buffer.can_write_back());
            memcpy(buffer.will_write_back(count), _view.will_read_first(count), count);
        }
        
    };
    
    
    
    
    

} // namespace wry


#endif /* binary_hpp */
