//
//  match.hpp
//  client
//
//  Created by Antony Searle on 10/9/2023.
//

#ifndef match_hpp
#define match_hpp

#include "chartype.hpp"
#include "string_view.hpp"

namespace wry {

    // # Matcher combinators
    //
    // Matchers look for a pattern at the start of their View argument,
    // and if found, advance the beginning of the view and return Truthy
    //
    // string_views or array_view<char8_t> views are the canonical arguments;
    // since many formats are specified in terms of ASCII characters the same
    // code can parse both, passing multibyte UTF-8 through unaltered.
    //
    // Views must support
    // .empty
    // .pop_front
    // .reset
    // CopyConstuctible
    //
    
    inline constexpr auto match_and(auto&&... matchers) {
        return [...matchers=std::forward<decltype(matchers)>(matchers)](auto& v) mutable -> bool {
            auto u(v);
            if (!(... && matchers(u)))
                return false;
            v.reset(u);
            return true;
        };
    }
    
    inline constexpr auto match_or(auto&&... matchers) {
        return [...matchers=std::forward<decltype(matchers)>(matchers)](auto& v) mutable -> bool {
            return (... || matchers(v));
        };
    }
    
    inline constexpr auto match_optional(auto&&... matchers) {
        return [...matchers=std::forward<decltype(matchers)>(matchers)](auto& v) mutable -> bool {
            (..., (void) matchers(v));
            return true;
        };
    }
            
    inline constexpr auto match_star(auto&& matcher) {
        return [matcher=std::forward<decltype(matcher)>(matcher)](auto& v) mutable -> bool {
            while (matcher(v))
                ;
            return true;
        };
    }
    
    inline auto constexpr match_plus(auto&& matcher) {
        return [matcher=std::forward<decltype(matcher)>(matcher)](auto& v) mutable -> bool {
            if (!matcher(v))
                return false;
            while (matcher(v))
                ;
            return true;
        };
    }
    
    inline auto constexpr match_count(auto&& matcher, int count) {
        assert(count >= 0);
        return [matcher=std::forward<decltype(matcher)>(matcher), count](auto& v) -> bool {
            auto u(v);
            for (int k = 0; k != count; ++k)
                if (!matcher(u))
                    return false;
            v.reset(u);
            return true;
        };
    }

    inline constexpr auto match_count_range(auto&& matcher, int min, int max) {
        assert((0 <= min) && (min <= max));
        return [matcher=std::forward<decltype(matcher)>(matcher), min, max](auto& v) -> bool {
            auto u(v);
            int k = 0;
            for ( ; k != min; ++k)
                if (!matcher(u))
                    return false;
            for ( ; (k != max) && matcher(u); ++k)
                ;
            v.reset(u);
            return true;
        };
    }
    
    // value (delimiter value)*
    inline auto match_delimited(auto&& value, auto&& delimiter) {
        return [value=std::forward<decltype(value)>(value),
                delimiter=std::forward<decltype(delimiter)>(delimiter)](auto& v) {
            int count = 0;
            auto u(v);
            for (;;) {
                if (!value(u))
                    return count;
                ++count;
                v.reset(u);
                if (!delimiter(u))
                    return count;
            }
        };
    }
    
    // the following are weird matcher/combinators that deal with EOF, match
    // negation, and other strange conditions.  Some of them are a kind of
    // tribool problem.
    //
    // They may be variously
    // - useless
    // - ill-formed
    // - misnamed
    //
    // Matchers that match empty can't advance, and when combined with
    // match_star or similar constructs, won't termimnate
    //
    // Negation can't really happen without an alternative, and when we advance
    // due to not-match, we are implicitly proposing match_any_char

    // match EOF/empty/end of view; never advances
    // Use case: some constructs can be terminated by a newline or end of file
    inline constexpr auto match_empty() {
        return [](auto& v) -> bool {
            return v.empty();
        };
    }

    // match, regardless of value, the first character of a view that has a
    // first element
    // alernative name:  match_any_character?
    inline constexpr auto match_not_empty() {
        return [](auto& v) -> bool {
            if (v.empty())
                return false;
            v.pop_front();
            return true;
        };
    }
    
    // match anything (any char) except the given
    constexpr auto match_not(auto&& matcher) {
        return [matcher=std::forward<decltype(matcher)>(matcher)](auto& v) -> bool {
            if (v.empty())
                return false;
            auto u(v);
            if (matcher(u))
                return false;
            v.pop_front();
            return true;
        };
    }
    
    // match_until
    // This is actually well-posed; we try to match _once_, then try to match
    // _many_, resulting in a priority-inverted version of
    // match_star(many) && once
    //
    // This is an example of a matcher that needs a custom parser associated
    // with it
    constexpr auto match_until(auto&& many, auto&& once) {
        return [many=std::forward<decltype(many)>(many),
                once=std::forward<decltype(once)>(once)](auto& v) mutable -> bool {
            for (auto u(v); ; ) {
                if (once(u)) {
                    v.reset(u);
                    return true;
                }
                if (!many(u))
                    return false;
            }
        };
    }
    
    
    
    
    constexpr auto match_predicate(auto&& predicate) {
        return [predicate=std::forward<decltype(predicate)>(predicate)](auto& v) -> bool {
            if (v.empty())
                return false;
            if (!predicate(v.front()))
                return false;
            v.pop_front();
            return true;
        };
    }

    

    
    // the usual unicode confusion about what actually constitutes a character
    // applies; this is an exact match with whatever the view yields, which
    // may be a char32_t code point or merely char8_t byte
    constexpr auto match_character(auto character) {
        return [character](auto& v) -> bool {
            if (v.empty())
                return false;
            if (v.front() != character)
                return false;
            v.pop_front();
            return true;
        };
    }
    
    constexpr auto match_character_case_insensitive(auto character) {
        assert(isuchar(character));
        return [character=tolower(character)](auto& v) -> bool {
            if (v.empty())
                return false;
            auto a = v.front();
            assert(isuchar(a));
            if (tolower(a) != character)
                return false;
            v.pop_front();
            return true;
        };
    }
    
    // exact match a zero-terminated string, usually a literal
    //
    // note that this is not a whole world search, and will happily match "cat"
    // when it is the just prefix of the larger word "cathederal"
    inline constexpr auto match_string(auto zstr) {
        return [zstr](auto& v) -> bool {
            auto u(v);
            auto a(zstr);
            for (;;) {
                auto cha = *a;
                if (!cha) {
                    v.reset(u);
                    return true;
                }
                if (u.empty())
                    return false;
                if (u.front() != cha)
                    return false;
                u.pop_front();
                ++a;
            }
        };
    }

    // match a single character from the string
    // match_from("abc") == match_or(match_character('a'), ...)
    inline constexpr auto match_from(auto zstr) {
        assert(zstr);
        return [zstr](auto& v) -> bool {
            if (v.empty())
                return false;
            auto ch = v.front();
            auto a(zstr);
            for (;;) {
                auto d = *a;
                if (!d)
                    return false;
                if (ch == *a) {
                    v.pop_front();
                    return true;
                }
                ++a;
            }
        };
    }
    
    // ill-posed
    constexpr auto match_not_from(auto zstr) {
        assert(zstr);
        return [zstr](auto& v) -> bool {
            if (v.empty())
                return false;
            auto ch = v.front();
            auto a(zstr);
            while (*a) {
                if (ch == *a)
                    return false;
            }
            v.pop_front();
            return true;
        };
    }
    
        
    // match one character that is drawn from a character class defined by
    // a predicate function
    
    inline constexpr auto match_cctype(int (*predicate)(int)) {
        return [predicate](auto& v) -> bool {
            if (v.empty())
                return false;
            int ch = v.front();
            if (!isuchar(ch) || !predicate(ch))
                return false;
            v.pop_front();
            return true;
        };
    }
    
    // from <cctype>
        
    inline constexpr auto match_alnum() {
        return match_cctype(&isalnum);
    }
    
    inline constexpr auto match_alpha() {
        return match_cctype(&isalpha);
    }

    inline constexpr auto match_ascii() {
        return match_cctype(&isascii);
    }

    inline constexpr auto match_blank() {
        return match_cctype(&isblank);
    }

    inline constexpr auto match_cntrl() {
        return match_cctype(&iscntrl);
    }

    inline constexpr auto match_digit() {
        return match_cctype(&isdigit);
    }

    inline constexpr auto match_graph() {
        return match_cctype(&isgraph);
    }

    inline constexpr auto match_lower() {
        return match_cctype(&islower);
    }

    inline constexpr auto match_print() {
        return match_cctype(&isprint);
    }

    inline constexpr auto match_punct() {
        return match_cctype(&ispunct);
    }

    inline constexpr auto match_space() {
        return match_cctype(&isspace);
    }

    inline constexpr auto match_xdigit() {
        return match_cctype(&isxdigit);
    }
    
    // extended character classes
    
    inline constexpr auto match_alnum_() {
        return match_cctype(&isalnum_);
    }
    
    inline constexpr auto match_alpha_() {
        return match_cctype(&isalpha_);
    }

    inline constexpr auto match_nonzero_digit() {
        return match_predicate([](auto character) {
            return (isuchar(character) && isdigit(character)) && (character != u8'0');
        });
    }

    inline constexpr auto match_hspace() {
        return match_predicate([](auto character) {
            return (character == u8' ') || (character == u8'\t');
        });
    }

    
    // multicharacter matchers
        
    inline constexpr auto match_spaces() {
        return match_star(match_space());
    }
    
    inline constexpr auto match_newline() {
        return match_and(match_optional(match_character('\r')),
                         match_character('\n'));
    }
    
    // match an identifier of the form [A-Za-z_][A-Za-z0-9_]*
    
    inline constexpr auto match_identifier() {
        return match_and(match_alpha_(),
                         match_star(match_alnum_()));
    }
    
    // match a float literal of the form (+|-)?[0-9]+(.[0-9]+)?((e|E)[0-9]+)?
    
    inline constexpr auto match_sign() {
        return match_or(match_character('-'), match_character('+'));
    }
    
    inline constexpr auto match_digits() {
        return match_plus(match_digit());
    }

    inline constexpr auto match_fractional_digits() {
        return match_and(match_character('.'),
                         match_digits());
    }
    
    inline constexpr auto match_mantissa() {
        return match_and(match_digits(),
                         match_optional(match_fractional_digits()));
    }
    
    inline constexpr auto match_exponent() {
        return match_and(match_from(u8"eE"),
                         match_optional(match_sign()),
                         match_digits());
    }
    
    inline constexpr auto match_number() {
        return match_and(match_optional(match_sign()),
                         match_mantissa(),
                         match_optional(match_exponent()));
    }
    
    // match a string literal of the fom "\""
    inline constexpr auto match_quotation() {
        return match_and(match_character(u8'"'),
                         match_until(match_or(match_string(u8"\\\""),
                                              match_not_empty()),
                                     match_character(u8'"')));
    }
    
    // match a POSIX portable filename
    // A-Z, a-z, 0-9, '.' and '_' are permitted
    // '-' is permitted
    inline constexpr auto match_posix_portable_filename() {
        return match_and(match_or(match_alnum(),
                                  match_from("._")),
                         match_star(match_or(match_alnum(),
                                             match_from("-._"))));
    }
    
    // match a POSIX portable path
    inline constexpr auto match_posix_portable_path() {
        return match_plus(match_or(match_posix_portable_filename(),
                                   match_character(u8'/')));
    }
    
    

    
} // namespace wry

#endif /* match_hpp */
