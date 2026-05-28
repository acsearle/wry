//
//  string.hpp
//  client
//
//  Created by Antony Searle on 26/6/2023.
//

#ifndef string_hpp
#define string_hpp

#include <atomic>
#include <cstdint>
#include <stdexcept>

#include "contiguous_deque.hpp"
#include "hash.hpp"
#include "string_view.hpp"

namespace wry {

    // <string>

    // A String presents a UTF-8 ContiguousDeque<char> as a sequence of UTF-32
    // scalars.  String, StringView, and ContiguousView<const char> (when used
    // as a UTF-8 view) all maintain the invariant that their bytes form a
    // valid UTF-8 sequence -- the invariant lives on the container, not on
    // the element type.
    //
    // std::string::c_str() is not worth the complication it induces; we make
    // a copy where we are forced to deal with a zero-terminated String API
    // such as libpng, rather than maintaining a one-past-the-end zero
    // character.
    //
    // Practically speaking, we can probably just blanket ban zeros from
    // appearing in the strings.

    struct String;

    template<> struct Rank<String> : std::integral_constant<std::size_t, 1> {};



    struct String {

        ContiguousDeque<char> chars;

        using const_iterator = utf8::iterator;
        using iterator = const_iterator;
        using value_type = char32_t;

        String() = default;
        String(const String& other) = default;
        String(String&&) = default;
        ~String() = default;
        String& operator=(const String& other) = default;
        String& operator=(String&&) = default;

        explicit String(StringView other)
        : chars(other.chars) {
        }

        String& operator=(StringView other) {
            chars = other.chars;
            return *this;
        }

        explicit String(const_iterator a, const_iterator b) {
            chars.reserve(b.base - a.base + 1);
            chars.assign(a.base, b.base);
        }

        // Take ownership of a byte buffer and validate.  Bytes are moved in
        // before checking so the caller's buffer is consumed regardless of
        // outcome (matches the std::move contract callers expect).
        explicit String(ContiguousDeque<char>&& bytes)
        : chars(std::move(bytes)) {
            if (!utf8::isvalid(ContiguousView<const char>(chars.begin(), chars.end())))
                throw std::invalid_argument("String: invalid UTF-8 in deque");
        }

        // Construct from a UTF-8 byte source.  Validates at the boundary
        // and throws std::invalid_argument on invalid UTF-8.  Owned-string
        // construction is the sanctioned entry point for runtime bytes
        // entering the validated-text world.  Compile-time-known literals
        // can use StringView's consteval ctor and an explicit
        // String(StringView) round-trip if zero-cost is needed.

        explicit String(const char* zstr) {
            if (zstr) {
                const char* p = zstr;
                while (*p) ++p;
                chars.append(zstr, p);
                if (!utf8::isvalid(ContiguousView<const char>(chars.begin(), chars.end())))
                    throw std::invalid_argument("String: invalid UTF-8");
            }
        }

        explicit String(const char* p, size_type n) {
            chars.append(p, p + n);
            if (!utf8::isvalid(ContiguousView<const char>(chars.begin(), chars.end())))
                throw std::invalid_argument("String: invalid UTF-8");
        }

        // Trusted-tag ctors: take ownership of bytes the caller asserts are
        // already valid UTF-8.  No check.  Use only when validation has
        // happened elsewhere (e.g., reading from an interned table, or
        // accepting bytes from a source documented to be UTF-8).

        String(trusted_t, const char* p, size_type n) {
            chars.append(p, p + n);
            assert(utf8::isvalid(ContiguousView<const char>(chars.begin(), chars.end())));
        }

        String(trusted_t, ContiguousDeque<char>&& bytes)
        : chars(std::move(bytes)) {
            assert(utf8::isvalid(ContiguousView<const char>(chars.begin(), chars.end())));
        }

        operator ContiguousView<const char>() const {
            return ContiguousView<const char>(chars.begin(), chars.end());
        }

        operator StringView() const {
            return StringView(begin(), end());
        }

        const_iterator begin() const {
            return iterator{ chars.begin() };
        }

        const_iterator end() const {
            return iterator{ chars.end() }; }

        const char* data() const {
            return chars.data();
        }

        ContiguousView<const byte> as_bytes() const {
            return ContiguousView<const byte>(reinterpret_cast<const byte*>(chars.begin()),
                                          reinterpret_cast<const byte*>(chars.end()));
        }

        char32_t front() {
            assert(!empty());
            return *begin();
        }

        char32_t back() {
            assert(!empty());
            auto a = end();
            return *--a;
        }

        // push_back(char) accepts an ASCII byte only.  Pushing arbitrary
        // bytes through this overload would let callers corrupt the UTF-8
        // invariant one byte at a time; that's reserved for code that has
        // direct access to `chars` and takes responsibility for what it
        // does.  Most code should prefer push_back(char32_t).
        void push_back(char ch) {
            assert(!(ch & 0x80));
            chars.push_back(ch);
        }

        void push_back(char16_t ch) {
            // we can only push back an isolated char16_t if it is not a
            // surrogate
            assert(!utf16::issurrogate(ch));
            push_back(static_cast<char32_t>(ch));
        }

        void push_back(char32_t ch) {
            // todo: fixme / use common code
            if (ch < 0x80) {
                chars.push_back((char)ch);
            } else if (ch < 0x800) {
                chars.push_back((char)(0xC0 | ((ch >>  6)       )));
                chars.push_back((char)(0x80 | ((ch      ) & 0x3F)));
            } else if (ch < 0x10000) {
                chars.push_back((char)(0xE0 | ((ch >> 12)       )));
                chars.push_back((char)(0x80 | ((ch >>  6) & 0x3F)));
                chars.push_back((char)(0x80 | ((ch      ) & 0x3F)));
            } else {
                assert(ch <= 0x10FFFF);
                chars.push_back((char)(0xF0 | ((ch >> 18)       )));
                chars.push_back((char)(0x80 | ((ch >> 12) & 0x3F)));
                chars.push_back((char)(0x80 | ((ch >>  6) & 0x3F)));
                chars.push_back((char)(0x80 | ((ch      ) & 0x3F)));
            }
        }

        // push_front(char) accepts an ASCII byte only; same rationale as
        // push_back(char).
        void push_front(char ch) {
            assert(!(ch & 0x80));
            chars.push_front(ch);
        }

        void push_front(char16_t ch) {
            assert(!utf16::issurrogate(ch));
            push_front(static_cast<char32_t>(ch));
        }

        void push_front(char32_t ch) {
            // fixme: use common / better code
            if (ch < 0x80) {
                chars.push_front((char)ch);
            } else if (ch < 0x800) {
                chars.push_front((char)(0x80 | ((ch      ) & 0x3F)));
                chars.push_front((char)(0xC0 | ((ch >>  6)       )));
            } else if (ch < 0x10000) {
                chars.push_front((char)(0x80 | ((ch      ) & 0x3F)));
                chars.push_front((char)(0x80 | ((ch >>  6) & 0x3F)));
                chars.push_front((char)(0xE0 | ((ch >> 12)       )));
            } else {
                assert (ch <= 0x10FFFF);
                chars.push_front((char)(0x80 | ((ch      ) & 0x3F)));
                chars.push_front((char)(0x80 | ((ch >>  6) & 0x3F)));
                chars.push_front((char)(0x80 | ((ch >> 12) & 0x3F)));
                chars.push_front((char)(0xF0 | ((ch >> 18)       )));
            }
        }

        void pop_back() {
            assert(!empty());
            --reinterpret_cast<iterator&>(chars._end);
        }

        char32_t back_and_pop_back() {
            assert(!empty());
            return *--reinterpret_cast<iterator&>(chars._end);
        }

        void pop_front() {
            assert(!empty());
            ++reinterpret_cast<iterator&>(chars._begin);
        }

        char32_t front_and_pop_front() {
            assert(!empty());
            return  *reinterpret_cast<iterator&>(chars._begin)++;
        }

        bool empty() const {
            return chars.empty();
        }

        void clear() {
            chars.clear();
        }

        // Append a zero-terminated UTF-8 byte sequence (validated by the
        // caller).  The bytes are pushed onto the underlying deque verbatim;
        // we don't go through push_back(char) because that overload asserts
        // ASCII-only.
        void append(const char* zstr) {
            if (zstr)
                for (char ch; (ch = *zstr); ++zstr)
                    chars.push_back(ch);
        }


        void append(StringView v) {
            chars.append(v.chars.begin(), v.chars.end());
        }

        void append(ContiguousView<const char> v) {
            chars.append(v);
        }


         bool operator==(const String& other) const {
             return std::equal(begin(), end(), other.begin(), other.end());
         }

        bool operator==(const StringView& other) const {
            return std::equal(begin(), end(), other.begin(), other.end());
        }

        auto operator<=>(const String& other) const {
            return std::lexicographical_compare_three_way(begin(), end(), other.begin(), other.end());
        }

    };

    inline uint64_t hash(const String& x) {
        return hash_combine(x.chars.data(), x.chars.size());
    }

    inline std::ostream& operator<<(std::ostream& a, String const& b) {
        a.write(b.chars.begin(), b.chars.size());
        return a;
    }

} // namespace wry




#endif /* string_hpp */
