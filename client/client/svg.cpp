//
//  svg.cpp
//  client
//
//  Created by Antony Searle on 8/3/2026.
//

#include "svg.hpp"

#include "match.hpp"
#include "parse.hpp"
#include "xml.hpp"
#include "test.hpp"


namespace wry::svg {
    
    namespace {
        
        bool foo(auto& v, auto&... xs) {
            return match_and(parse_number_relaxed(xs)...)(v);
        }
        
        bool parse_path_d(auto v) {
            double x = 0.0, y = 0.0;
            double x0 = x, y0 = y;
            double x1 = 0.0, y1 = 0.0;
            double x2 = 0.0, y2 = 0.0;
            double x3 = 0.0, y3 = 0.0;
            printf("    %g,%g\n", x, y);
            for (;;) {
                match_spaces()(v);
                if (v.empty())
                    break;
                auto ch = v.front();
                v.pop_front();
                switch (ch) {
                    case 'M':
                        foo(v, x, y);
                        printf("    %g,%g\n", x, y);
                        x0 = x;
                        y0 = y;
                        [[fallthrough]];
                    case 'L':
                        while (foo(v, x, y)) {
                            printf("    %g,%g\n", x, y);
                        }
                        x1 = x2 = x;
                        y1 = y2 = y;
                        break;
                    case 'm':
                        foo(v, x1, y1);
                        x += x1;
                        y += y1;
                        printf("    %g,%g\n", x, y);
                        x0 = x;
                        y0 = y;
                        [[fallthrough]];
                    case 'l':
                        while (foo(v, x1)) {
                            foo(v, y1);
                            x += x1;
                            y += y1;
                            printf("    %g,%g\n", x, y);
                        }
                        x1 = x2 = x;
                        y1 = y2 = y;
                        break;
                    case 'Z':
                    case 'z':
                        x = x0;
                        y = y0;
                        printf("    %g,%g\n", x, y);
                        break;
                    case 'H':
                        while (foo(v, x))
                            printf("    %g,%g\n", x, y);
                        x1 = x2 = x;
                        break;
                    case 'h':
                        while (foo(v, x1)) {
                            x += x1;
                            printf("    %g,%g\n", x, y);
                        }
                        x1 = x2 = x;
                        break;
                    case 'V':
                        while (foo(v, y))
                            printf("    %g,%g\n", x, y);
                        y1 = y2 = y;
                        break;
                    case 'v':
                        while (foo(v, y1)) {
                            y += y1;
                            printf("    %g,%g\n", x, y);
                        }
                        y1 = y2 = y;
                        break;
                    case 'Q':
                        while (foo(v, x1, y1, x, y)) {
                            printf("    %g,%g\n", x1, y1);
                            printf("    %g,%g\n", x, y);
                        }
                        x2 = x;
                        y2 = y;
                        break;
                    case 'q':
                        while (foo(v, x1, y1, x2, y2)) {
                            x1 += x;
                            y1 += y;
                            x += x2;
                            y += y2;
                            printf("    %g,%g\n", x1, y1);
                            printf("    %g,%g\n", x, y);
                        }
                        x1 = 2 * x - x1;
                        y1 = 2 * y - y1;
                        x2 = x;
                        y2 = y;
                        break;
                    case 'T':
                        while (foo(v, x2, y2)) {
                            x = x2;
                            y = y2;
                            printf("    %g,%g\n", x1, y1);
                            printf("    %g,%g\n", x, y);
                            x1 = 2 * x - x1;
                            y1 = 2 * y - y1;
                        }
                        break;
                    case 't':
                        while (foo(v, x2, y2)) {
                            x += x2;
                            y += y2;
                            printf("    %g,%g\n", x1, y1);
                            printf("    %g,%g\n", x, y);
                            x1 = 2 * x - x1;
                            y1 = 2 * y - y1;
                        }
                        x2 = x;
                        y2 = y;
                        break;
                    case 'C':
                        while (foo(v, x1, y1, x2, y2, x, y)) {
                            printf("    %g,%g\n", x1, y1);
                            printf("    %g,%g\n", x2, y2);
                            printf("    %g,%g\n", x, y);
                        }
                        x1 = 2 * x - x2;
                        y1 = 2 * y - y2;
                        break;
                    case 'c':
                        while (foo(v, x1, y1, x2, y2, x3, y3)) {
                            x1 += x;
                            y1 += y;
                            x2 += x;
                            y2 += y;
                            x += x3;
                            y += y3;
                            printf("    %g,%g\n", x1, y1);
                            printf("    %g,%g\n", x2, y2);
                            printf("    %g,%g\n", x, y);
                        }
                        x1 = 2 * x - x2;
                        y1 = 2 * y - y2;
                        break;
                    case 'S':
                        while (foo(v, x2, y2, x, y)) {
                            printf("    %g,%g\n", x1, y1);
                            printf("    %g,%g\n", x2, y2);
                            printf("    %g,%g\n", x, y);
                            x1 = 2 * x - x2;
                            y1 = 2 * y - y2;
                        }
                        break;
                    case 's':
                        while (foo(v, x2, y2, x3, y3)) {
                            x2 += x;
                            y2 += y;
                            x += x3;
                            y += y3;
                            printf("    %g,%g\n", x1, y1);
                            printf("    %g,%g\n", x2, y2);
                            printf("    %g,%g\n", x, y);
                            x1 = 2 * x - x2;
                            y1 = 2 * y - y2;
                        }
                        break;
                    default:
                        printf("did not understand %c\n", ch);
                        return false;
                }
            }
            return true;
        }
        
        void parse_path(xml::Content& a) {
            assert(a.name == "path");
            for (auto [k, v] : a.attributes) {
                if (k == "d") {
                    parse_path_d(v);
                } else {
                    printf("ignored ");
                    print(k);
                    printf("=\"");
                    print(v);
                    printf("\"\n");
                }
            }
        }
        
        
        
        
        void parse_fragment(xml::Content& a) {
            assert(a.name == "svg");
            for (auto [k, v] : a.attributes) {
                //                if (k == "height") {
                //                } else if (k == "width") {
                //                } else if (k == "viewBox") {
                //                } else if (k == "x") {
                //                } else if (k == "y") {
                //                } else {
                {
                    printf("ignored ");
                    print(k);
                    printf("=\"");
                    print(v);
                    printf("\"\n");
                }
            }
            for (xml::Content& b : a.content) {
                switch(b.tag) {
                    case xml::Content::TEXT:
                        break;
                    case xml::Content::ELEMENT:
                        if (b.name == "path") {
                            parse_path(b);
                        }
                }
            }
        }
        
    }
    
    std::vector<PiecewiseCurve> parse(StringView& v) {
        std::vector<PiecewiseCurve> result;
        
        auto a{xml::parse(v)};
        
        for (xml::Content& b : a) {
            switch (b.tag) {
                case xml::Content::TEXT:
                    break;
                case xml::Content::ELEMENT:
                    if (b.name == "svg") {
                        parse_fragment(b);
                    }
            }
        }
        
        return result;

    }
    
    namespace {
        
        define_test("svg") {
            char const* input_circle_svg = R"(<svg xmlns="http://www.w3.org/2000/svg" height="24px" viewBox="0 -960 960 960" width="24px" fill="#1f1f1f"><path d="M480-640 280-440l56 56 104-103v407h80v-407l104 103 56-56-200-200ZM146-260q-32-49-49-105T80-480q0-83 31.5-156T197-763q54-54 127-85.5T480-880q83 0 156 31.5T763-763q54 54 85.5 127T880-480q0 59-17 115t-49 105l-58-58q22-37 33-78t11-84q0-134-93-227t-227-93q-134 0-227 93t-93 227q0 43 11 84t33 78l-58 58Z"/></svg>)";
            
            StringView v{input_circle_svg};

            parse(v);
            
            co_return;
        };
        
        
    }
    
} // namespace wry
