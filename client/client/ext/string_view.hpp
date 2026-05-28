//
//  string_view.hpp
//  client
//
//  Created by Antony Searle on 26/6/2023.
//


#ifndef string_view_hpp
#define string_view_hpp

#include <cstring>    // strlen
#include <ostream>    // ostream
#include <stdexcept>  // invalid_argument

#include "assert.hpp"
#include "algorithm.hpp"
#include "contiguous_view.hpp"
#include "unicode.hpp"
#include "hash.hpp"

namespace wry {

    // wry::trusted is a tag for ctors that accept raw bytes without
    // re-validating an invariant.  The caller asserts that the bytes are
    // valid for the target type's invariant (e.g. valid UTF-8 for String /
    // StringView).  Use sparingly: prefer the validating ctor (which throws
    // on invalid input) or the consteval-from-literal path (which checks at
    // compile time) when those fit.

    struct trusted_t { explicit trusted_t() = default; };
    inline constexpr trusted_t trusted{};

    // StringView presents a ContiguousView<const char> as a UTF-32 sequence.
    //
    // The UTF-8 validity invariant lives on this type (and on String), not
    // on the element type.  Construct StringViews from:
    //   - a string literal (validated at compile time, consteval ctor)
    //   - another StringView or a String (iterator-pair ctor; valid by
    //     construction, assert-only)
    //   - raw bytes you've already validated (trusted-tag ctor; no check)
    // Untrusted runtime bytes should go through a String first so the
    // validation happens once at the boundary.

    struct StringView;

    template<>
    struct Rank<StringView>
    : std::integral_constant<std::size_t, 1> {};

    struct StringView {

        using const_iterator = utf8::iterator;
        using iterator = const_iterator;
        using value_type = char32_t;

        ContiguousView<const char> chars;

        bool _invariant() const {
            return utf8::isvalid(chars);
        }

        StringView() : chars() {}

        StringView(const StringView&) = default;
        StringView(StringView&&) = default;

        ~StringView() = default;

        // views are reference-like, so assignment to const can't change the
        // data and won't change the pointers
        //
        // instead, use .reset(...) to swing the view

        StringView& operator=(const StringView&) = delete;
        StringView& operator=(StringView&&) = delete;

        // From an iterator pair returned by String/StringView.  By
        // construction the underlying bytes are already validated UTF-8,
        // so this only asserts.
        constexpr StringView(const_iterator first, const_iterator last)
        : chars(first.base, last.base) {
            assert(_invariant());
        }

        // From a zero-terminated string.  Validates and throws on invalid
        // UTF-8.  `constexpr` so that callers using a constant-evaluated
        // context (e.g. `inline constexpr StringView sv = "literal";`)
        // pay zero runtime cost and get a compile error on a bad literal;
        // otherwise runs at runtime with the validation cost.
        //
        // Treating input as a zstr (stopping at the first null) sidesteps
        // the `char buf[100]; sprintf(buf, "hi"); StringView sv(buf);`
        // hazard that an array-reference ctor would have.
        constexpr StringView(const char* zstr)
        : chars(zstr, _zstr_end(zstr)) {
            if (!utf8::isvalid_range(chars.begin(), chars.end()))
                throw std::invalid_argument("StringView: invalid UTF-8");
        }

    private:
        static constexpr const char* _zstr_end(const char* p) {
            while (*p) ++p;
            return p;
        }
    public:

        // From raw bytes the caller asserts are already valid UTF-8.  No
        // check.  Use when bytes came from a source whose invariant we
        // trust (sub-slice of a validated view, interned table, etc.).
        constexpr StringView(trusted_t, const char* p, size_type n)
        : chars(p, n) {
            assert(_invariant());  // debug-only sanity
        }

        constexpr StringView(trusted_t, const char* first, const char* last)
        : chars(first, last) {
            assert(_invariant());
        }

        bool empty() const {
            return chars.empty();
        }

        const_iterator begin() const {
            return const_iterator(chars.begin());
        }

        const_iterator end() const {
            return const_iterator(chars.end());
        }

        const_iterator cbegin() const { return begin(); }
        const_iterator cend() const { return end(); }

        char32_t front() const {
            assert(!empty());
            return *begin();
        }

        char32_t back() const {
            assert(!empty());
            iterator it = end();
            return *--it;
        }

        bool operator==(const StringView& other) const {
            return std::equal(begin(), end(), other.begin(), other.end());
        }

        auto operator<=>(const StringView& other) const {
            return std::lexicographical_compare_three_way(begin(), end(), other.begin(), other.end());
        }

        void pop_front() {
            assert(!empty());
            ++reinterpret_cast<iterator&>(chars._begin);
        }

        void pop_back() {
            --reinterpret_cast<iterator&>(chars._end);
        }

        void unsafe_unpop_front() {
            --reinterpret_cast<iterator&>(chars._begin);
        }

        void unsafe_unpop_back() {
            ++reinterpret_cast<iterator&>(chars._end);
        }


        void reset(StringView other) {
            chars.reset(other.chars);
        }

        // string concatenation is often modelled as + but it is better thought
        // of as noncommutative *

        // concatenate views, which must be consecutive
        StringView operator*(const StringView& other) const {
            assert(chars._end == other.chars._begin);
            return StringView(const_iterator(chars._begin),
                              const_iterator(other.chars._end));
        }

        // division is the complementary operation to concatenation, removing
        // a suffix so that for c = a * b we have a = c / b

        // difference of a view and its suffix
        StringView operator/(const StringView& other) const {
            assert(chars._end == other.chars._end);
            assert(chars._begin <= other.chars._begin);
            return StringView(const_iterator(chars._begin),
                              const_iterator(other.chars._begin));
        }

    }; // struct StringView

    inline void print(StringView v) {
        printf("%.*s", (int) v.chars.size(), v.chars.data());
    }

    inline std::ostream& operator<<(std::ostream& a, StringView b) {
        a.write(b.chars.begin(), b.chars.size());
        return a;
    }

    inline uint64_t hash(StringView v) {
        return hash_combine(v.chars.begin(), v.chars.size(), 0);
    }

} // namespace wry

#endif /* string_view_hpp */
