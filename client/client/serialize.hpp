//
//  serialize.hpp
//  client
//
//  Created by Antony Searle on 26/6/2023.
//

#ifndef serialize_hpp
#define serialize_hpp

#include <optional>
#include <expected>
#include <variant>

#include "array.hpp"
#include "stdint.hpp"
#include "type_traits.hpp"
#include "stdfloat.hpp"
#include "string.hpp"
#include "string_view.hpp"


namespace wry {
    
    template<typename S>
    std::expected<
        typename std::decay_t<S>::value_type,
        typename std::decay_t<S>::error_type>
    serialize(const int32_t& x, S&& serializer) {
        return std::forward<S>(serializer).serialize_i32(x);
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
        std::expected<value_type, error_type> \
        serialize_##T ( T x) {\
            memcpy(_buffer.will_write_back(sizeof( T )), &x, sizeof( T ));\
            _maybe_sink();\
            return value_type{};\
        }
        
        WRY_X_OF_T_FOR_T_IN_FIXED_WIDTH_INTEGER_TYPES
        WRY_X_OF_T_FOR_T_IN_FIXED_WIDTH_FLOAT_TYPES
#undef X
        
        std::expected<value_type, error_type>
        serialize_bool(bool x) {
            return serialize_int8_t(x);
        }
        
        struct tuple_serializer {
            
            binary_serializer* _context;
            size_t _remaining;
            
            template<typename T>
            std::expected<std::monostate, error_type>
            serialize_element(const T& x) {
                if (!_remaining--)
                    return std::unexpected(ERANGE);
                return serialize(x, *_context);
            }
            
            std::expected<value_type, error_type>
            end() {
                if (_remaining)
                    return std::unexpected(ERANGE);
                return {};
            }
            
        };
        
        std::expected<tuple_serializer, error_type> serialize_tuple(size_t count) {
            return tuple_serializer{this, count};
        }
        
        using seq_serializer = tuple_serializer;
        
        std::expected<tuple_serializer, error_type> serialize_seq(std::optional<size_t> count) {
            if (!count)
                return std::unexpected(ERANGE);
            serialize_uint64_t(*count);
            return serialize_tuple(*count);
        }
        
        struct map_serializer {
            
            binary_serializer* _context;
            size_t _remaining;
            bool _expecting_value = false;
            
            template<typename K>
            std::expected<std::monostate, error_type> serialize_key(K&& key) {
                if (!_remaining || _expecting_value)
                    return std::unexpected(ERANGE);
                return serialize(std::forward<K>(key), *_context).and_then([this](auto&&) {
                    _expecting_value = true;
                    return std::monostate{};
                });
            }
            
            template<typename V>
            std::expected<std::monostate, error_type> serialize_value(V&& value) {
                if (!_remaining || !_expecting_value)
                    return std::unexpected(ERANGE);
                return serialize(std::forward<V>(value), *_context).and_then([this](auto&&) {
                    --_remaining;
                    _expecting_value = false;
                    return std::monostate{};
                });
            }
            
            std::expected<value_type, error_type> end() {
                if (_remaining || _expecting_value)
                    return std::unexpected(ERANGE);
                return value_type{};
            }
            
        };
        
        std::expected<map_serializer, error_type> serialize_map(std::optional<size_t> count) {
            if (!count)
                return std::unexpected(ERANGE);
            serialize_uint64_t(*count);
            return map_serializer{this, *count};
        }
        
        struct struct_serializer {
            
            binary_serializer* _context;
            size_t _remaining;
            
            template<typename V>
            std::expected<std::monostate, error_type>
            serialize_field(string_view key, V&& value) {
                if (!_remaining)
                    return std::unexpected(ERANGE);
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
    
    
    
    
    
    /*
    
    struct file_deserializer {
        
        FILE* stream;
        
        string buffer;
        
#define X(T) auto deserialize_##T(auto&& visitor) {\
            T x = {};\
            fread(&x, sizeof(T), 1, stream);\
            return visitor.visit_##T(x);\
        }
        WRY_X_FOR_ALL_RUST_INTEGERS
#undef X
        
        auto deserialize_string(auto&& visitor) {
            u64 count = {};
            fread(&count, 8, 1, stream);
            buffer._bytes.clear();
            fread(buffer._bytes.will_write_back(count), 1, count, stream);
            return visitor.visit_string(buffer);
        }
        
        auto deserialize_bytes(auto&& visitor) {
            u64 count = {};
            fread(&count, 8, 1, stream);
            buffer._bytes.clear();
            fread(buffer._bytes.will_write_back(count), 1, count, stream);
            return visitor.visit_bytes(buffer._bytes);
        }
        
        // the accessor is essentially a deserializer but we can't provide
        // the interface for it without stomping other stuff
        
        struct sequence_access {
            
            file_deserializer* _parent;
            u64 _count;
            
            explicit sequence_access(file_deserializer* parent)
            : _parent(parent)
            , _count(0) {
                fread(&_count, 8, 1, _parent->stream);
            }
            
            sequence_access(file_deserializer* parent, u64 count) 
            : _parent(parent)
            , _count(count) {
            }
            
            ~sequence_access() {
                assert(!_count);
            }
            
            template<typename T>
            std::optional<T> next() {
                if (!_count)
                    return {};
                --_count; // <-- side effect / statefulness counting down to end of seq
                return deserialize<T>(*_parent);
            }
            
        };
                
        auto deserialize_sequence(auto&& visitor) {
            return visitor.visit_sequence(sequence_access(this));
        }
        
        auto deserialize_tuple(size_t count, auto&& visitor) {
            return visitor.visit_sequence(sequence_access(this, count));
        }
                
                
    };
        
// #define X(T) struct T##_vistor { T visit_##T(T x) { return x; } };
//    WRY_X_FOR_ALL_RUST_INTEGERS
// #undef X
    
    */
        
    /*
    
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
    
    
    
    template<typename T, typename Deserializer>
    T deserialize(Deserializer& d) {
        return deserialize(std::in_place_type<T>, d);
    }
    
    
    // Binary, native (little) endian serialization and deserialization
    
#define X(T)\
\
inline void serialize(T const& x, std::FILE*& s) {\
[[maybe_unused]] auto r = fwrite(&x, sizeof(x), 1, s);\
assert(r == 1);\
}\
\
inline auto deserialize(std::in_place_type_t< T >, std::FILE*& d) {\
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
    
    bool serialize_iterable(auto first, auto last, FILE*& s) {
        long a = ftell(s);
        if (a == -1) 
            return false;
        if (fseek(s, +8, SEEK_CUR))
            return false;
        uint64_t count = 0;
        for (; first != last; ++first, ++count)
            serialize(*first, s);
        long b = ftell(s);
        if (b == -1) 
            return false;
        if (fseek(s, a, SEEK_SET))
            return false;
        if (fwrite(&count, 8, 1, s) != 1)
            return false;
        if (fseek(s, b, SEEK_SET))
            return false;
    }
    
    
    
    // std::pair
    
    template<typename A, typename B, typename Serializer>
    void serialize(std::pair<A, B> const& x, Serializer& s) {
        serialize(x.first, s);
        serialize(x.second, s);
    }
    
    template<typename A, typename B, typename Deserializer>
    auto deserialize(std::in_place_type_t<std::pair<A, B>>, Deserializer& d) {
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
    auto deserialize(std::in_place_type_t<std::vector<T>>, Deserializer& d) {
        auto n = deserialize<std::size_t>(d);
        std::vector<T> x;
        x.reserve(n);
        while (n--)
            x.push_back(deserialize<T>(d));
        return x;
    }
    
    */
    
} // namespace wry

#endif /* serialize_hpp */
