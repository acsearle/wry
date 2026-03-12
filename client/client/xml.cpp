//
//  xml.cpp
//  client
//
//  Created by Antony Searle on 8/3/2026.
//

#include "xml.hpp"

#include "parse.hpp"
#include "test.hpp"

namespace wry::xml {
    
    namespace {
        
        constexpr int isnamehead(int ch) {
            return isalpha(ch) || __builtin_strchr("_:", ch);
        }
        
        constexpr int isnametail(int ch) {
            return isalnum(ch) || __builtin_strchr("_:.-", ch);
        }
        
        auto match_any() {
            return [](auto& v) {
                return !v.empty() && (v.pop_front(), true);
            };
        }
        
        auto match_comment() {
            return match_and(match_zstr("<!--"),
                             match_not(match_zstr("--")),
                             match_zstr("-->"));
        }
        
        auto match_name() {
            return match_identifier();
        }
        
        auto match_attribute() {
            return match_and(match_name(),
                             match_spaces(),
                             match_character('='),
                             match_spaces(),
                             match_quotation()
                             );
        }
        
        auto parse_attribute(StringView& key, StringView& value) {
            return match_and(parse_into(match_name(), key),
                             match_spaces(),
                             match_character('='),
                             match_spaces(),
                             parse(match_quotation(),
                                   [&](auto u) {
                value.reset(u);
                value.pop_front();
                value.pop_back();
            })
                             );
        }
        
        auto parse_attributes(std::vector<std::pair<StringView, StringView>>& a) {
            return [&](auto& v) {
                StringView key{}, value{};
                while (match_and(match_spaces(),
                                 parse_attribute(key, value))(v)) {
                    a.emplace_back(key, value);
                }
                return true;
            };
        }
        
        // TODO: Separate Y-combinator and curry
        auto y_combinator(auto&& f, auto&... g) {
            return [f, &g...](auto&&... args) mutable {
                return f(f, g..., std::forward<decltype(args)>(args)...);
            };
        }
        
        auto parse_content(std::vector<Content>& cs) {
            return y_combinator([](auto& self, std::vector<Content>& cs, auto& v) {
                auto u{v};
                Content c;
                if (match_not(match_character('<'))(u)) {
                    c.tag = Content::TEXT;
                    c.text.reset(u / v);
                } else if (match_and(match_character('<'),
                                     parse_into(match_name(), c.name),
                                     parse_attributes(c.attributes),
                                     match_spaces(),
                                     match_or(/* empty-tag */
                                              match_zstr("/>"),
                                              /* start-tag */
                                              match_and(match_character('>'),
                                                        match_until(y_combinator(self, c.content),
                                                                    /* end-tag */
                                                                    match_and(match_zstr("</"),
                                                                              // TODO: this is a nasty
                                                                              // capture by ref, to see
                                                                              // result of parse name
                                                                              // above
                                                                              match_string_view(c.name),
                                                                              match_character('>')))))
                                     )(u)) {
                    c.tag = Content::ELEMENT;
                } else {
                    return false;
                }
                v.reset(u);
                //print(c);
                cs.push_back(std::move(c));
                return true;
            }, cs);
        }
        
        define_test("xml") {
            char const* input_circle_svg = R"(<svg xmlns="http://www.w3.org/2000/svg" height="24px" viewBox="0 -960 960 960" width="24px" fill="#1f1f1f"><path d="M480-640 280-440l56 56 104-103v407h80v-407l104 103 56-56-200-200ZM146-260q-32-49-49-105T80-480q0-83 31.5-156T197-763q54-54 127-85.5T480-880q83 0 156 31.5T763-763q54 54 85.5 127T880-480q0 59-17 115t-49 105l-58-58q22-37 33-78t11-84q0-134-93-227t-227-93q-134 0-227 93t-93 227q0 43 11 84t33 78l-58 58Z"/></svg>)";
            
            StringView v{input_circle_svg};
            
            auto cs = parse(v);
            
            //for (auto& c : cs) {
            //    print(c);
            //}

            co_return;
        };
        
    } // namespace anonymous
    
    std::vector<Content> parse(StringView& v) {
        std::vector<Content> result;
        while (parse_content(result)(v))
            ;
        return result;
    }
    
    void print(Content const& a) {
        switch (a.tag) {
            case Content::TEXT:
                print(a.text); printf("\n");
                break;
            case Content::ELEMENT:
                printf("<");
                print(a.name);
                for (auto& [b, c] : a.attributes) {
                    printf(" ");
                    print(b);
                    printf("=\"");
                    print(c);
                    printf("\"");
                }
                if (a.content.empty()) {
                    printf("/>\n");
                } else {
                    for (auto& b : a.content) {
                        print(b);
                    }
                    printf("</");
                    print(a.name);
                    printf(">\n");
                }
                break;
            default:
                printf("Content::tag = %d\n", a.tag);
                break;
        }
    }

} // namespace wry::xml
