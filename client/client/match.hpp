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
    // Matchers look for a pattern at the start of their string_view argument,
    // and if found, advance the beginning of the view and return false
    //
    // We chain operations using short-circuiting operators using some terse
    // syntax:
    //
    //         v -> !v.empty()
    //       ++v -> ((void) v.pop_front(), true)
    //     v = u -> ((void) (v = u), true)
    //     v / u -> string_view(v.a, u.a)
    //
    // When we are transcoding between UTF-8 encoded formats (such as reading a
    // wry::string from any text-based format) it is wasteful to decode to
    // u32 and re-encode it.
    
    inline constexpr auto match_empty() {
        return [](auto& v) -> bool {
            return v.empty();
        };
    }
    
    inline constexpr auto match_not_empty() {
        return [](auto& v) -> bool {
            if (v.empty())
                return false;
            v.pop_front();
            return true;
        };
    }

    inline constexpr auto match_and(auto&&... matchers) {
        return [...matchers=std::forward<decltype(matchers)>(matchers)](auto& v) -> bool {
            auto u(v);
            if (!(... && matchers(u)))
                return false;
            v.reset(u);
            return true;
        };
    }
    
    inline constexpr auto match_or(auto&&... matchers) {
        return [...matchers=std::forward<decltype(matchers)>(matchers)](auto& v) -> bool {
            return (... || matchers(v));
        };
    }
    
    inline constexpr auto match_optional(auto&&... matchers) {
        return [...matchers=std::forward<decltype(matchers)>(matchers)](auto& v) -> bool {
            (..., (void) matchers(v));
            return true;
        };
    }
            
    inline constexpr auto match_star(auto&& matcher) {
        return [matcher=std::forward<decltype(matcher)>(matcher)](auto& v) -> bool {
            while (matcher(v))
                ;
            return true;
        };
    }
    
    inline auto constexpr match_plus(auto&& matcher) {
        return match_and(matcher, match_star(matcher));
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

    inline constexpr auto match_range(auto&& matcher, int min, int max) {
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
    
    
    auto match_not(auto&& matcher) {
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
    
    auto match_until(auto&& many, auto&& once) {
        return [many=std::forward<decltype(many)>(many),
                once=std::forward<decltype(once)>(once)](auto& v) -> bool {
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
    
    inline constexpr auto match_character(auto character) {
        return [character](auto& v) -> bool {
            if (v.empty())
                return false;
            if (v.front() != character)
                return false;
            v.pop_front();
            return true;
        };
    }
    
    inline constexpr auto match_letter(auto character) {
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
    
    inline constexpr auto match_string(auto zstr) {
        // todo: use the argument types to decide between upgrading a const
        // char8_t* to a utf8::iterator when v is character oriented, and leaving
        // it as a pointer when v is byte oriented
        return [zstr](auto& v) -> bool {
            auto u(v);
            auto a(zstr);
            for (;;) {
                char32_t cha = *a;
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
    
    inline constexpr auto match_not_from(auto zstr) {
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
    
    
    inline constexpr auto match_predicate(auto&& predicate) {
        return [predicate=std::forward<decltype(predicate)>(predicate)](auto& v) -> bool {
            if (v.empty())
                return false;
            if (!predicate(v.front()))
                return false;
            v.pop_front();
            return true;
        };
    }
    
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
    
    // character classes
        
    inline constexpr auto match_alnum() {
        return match_cctype(&isalnum);
    }

    inline constexpr auto match_alnum_() {
        return match_cctype(&isalnum_);
    }
    
    inline constexpr auto match_alpha() {
        return match_cctype(&isalpha);
    }

    inline constexpr auto match_alpha_() {
        return match_cctype(&isalpha_);
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
    
    inline constexpr auto match_nonzero_digit() {
        return match_predicate([](auto character) {
            return isuchar(character) && isdigit(character) && (character != '0');
        });
    }
    
    // multicharacter matchers
        
    inline constexpr auto match_blanks() {
        return match_star(match_space());
    }

    inline constexpr auto match_spaces() {
        return match_star(match_space());
    }
    
    inline constexpr auto match_newline() {
        return match_and(match_optional(match_character('\r')),
                         match_character('\n'));
    }
    
    inline constexpr auto match_line() {
        return match_until(match_not_empty(), match_newline());
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
        return match_and(match_letter('e'),
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
        return match_and(match_character('"'),
                         match_until(match_or(match_string("\\\""),
                                              match_not_empty()),
                                     match_character('"')));
    }
    
    // match a (posix compliant) filename
    inline constexpr auto match_filename() {
        return match_and(match_or(match_alnum(),
                                  match_from("._")),
                         match_star(match_or(match_alnum(),
                                             match_from("-._"))));
    }
    
    // match a path (crudely)
    inline constexpr auto match_path() {
        return match_until(match_not_empty(),
                           match_or(match_space(),
                                    match_empty()));
    }
    
    

    
} // namespace wry

#endif /* match_hpp */
