//
//  xml.cpp
//  client
//
//  Created by Antony Searle on 8/3/2026.
//

#include "xml.hpp"

#include <cassert>
#include <cstdio>

#include "test.hpp"

namespace wry::xml {

    std::string const* Element::attribute(std::string_view key) const {
        for (auto const& [k, v] : attributes)
            if (k == key)
                return &v;
        return nullptr;
    }

    namespace {

        bool is_space(char c) {
            return c == ' ' || c == '\t' || c == '\n' || c == '\r';
        }

        // ASCII approximation of NameStartChar / NameChar; any byte >= 0x80
        // is accepted so that non-ASCII UTF-8 names pass through.
        bool is_name_start(char c) {
            unsigned char u = (unsigned char)c;
            return (u >= 'a' && u <= 'z') || (u >= 'A' && u <= 'Z')
                || u == '_' || u == ':' || u >= 0x80;
        }

        bool is_name_char(char c) {
            unsigned char u = (unsigned char)c;
            return is_name_start(c) || (u >= '0' && u <= '9')
                || u == '-' || u == '.';
        }

        void append_utf8(std::string& s, unsigned long cp) {
            if (cp < 0x80) {
                s.push_back((char)cp);
            } else if (cp < 0x800) {
                s.push_back((char)(0xC0 | (cp >> 6)));
                s.push_back((char)(0x80 | (cp & 0x3F)));
            } else if (cp < 0x10000) {
                s.push_back((char)(0xE0 | (cp >> 12)));
                s.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
                s.push_back((char)(0x80 | (cp & 0x3F)));
            } else {
                s.push_back((char)(0xF0 | (cp >> 18)));
                s.push_back((char)(0x80 | ((cp >> 12) & 0x3F)));
                s.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
                s.push_back((char)(0x80 | (cp & 0x3F)));
            }
        }

        struct Parser {

            std::string_view src;
            std::size_t pos = 0;
            Document doc;

            bool at_end() const { return pos >= src.size(); }
            char peek(std::size_t k = 0) const {
                return pos + k < src.size() ? src[pos + k] : '\0';
            }
            bool starts_with(std::string_view s) const {
                return src.substr(pos, s.size()) == s;
            }

            bool fail(std::string what) {
                if (doc.error.empty()) {
                    doc.error = std::move(what);
                    doc.error_offset = pos;
                }
                return false;
            }

            void skip_space() {
                while (!at_end() && is_space(src[pos]))
                    ++pos;
            }

            bool skip_past(std::string_view terminator, char const* what) {
                std::size_t k = src.find(terminator, pos);
                if (k == std::string_view::npos) {
                    pos = src.size();
                    return fail(std::string("unterminated ") + what);
                }
                pos = k + terminator.size();
                return true;
            }

            bool name(std::string_view& out) {
                if (at_end() || !is_name_start(src[pos]))
                    return fail("expected a name");
                std::size_t first = pos;
                while (!at_end() && is_name_char(src[pos]))
                    ++pos;
                out = src.substr(first, pos - first);
                return true;
            }

            // Decode character data / attribute text up to (not including)
            // the first byte in `stops`.
            bool decode_until(std::string& out, std::string_view stops) {
                while (!at_end()) {
                    char c = src[pos];
                    if (stops.find(c) != std::string_view::npos)
                        return true;
                    if (c != '&') {
                        out.push_back(c);
                        ++pos;
                        continue;
                    }
                    std::size_t semi = src.find(';', pos);
                    if (semi == std::string_view::npos || semi - pos > 12)
                        return fail("malformed entity reference");
                    std::string_view e = src.substr(pos + 1, semi - pos - 1);
                    if (e == "lt") out.push_back('<');
                    else if (e == "gt") out.push_back('>');
                    else if (e == "amp") out.push_back('&');
                    else if (e == "quot") out.push_back('"');
                    else if (e == "apos") out.push_back('\'');
                    else if (e.size() >= 2 && e[0] == '#') {
                        bool hex = e[1] == 'x' || e[1] == 'X';
                        std::string digits(e.substr(hex ? 2 : 1));
                        char* end = nullptr;
                        unsigned long cp = std::strtoul(digits.c_str(), &end, hex ? 16 : 10);
                        if (digits.empty() || *end || cp > 0x10FFFF)
                            return fail("malformed character reference");
                        append_utf8(out, cp);
                    } else {
                        return fail("unknown entity (DTDs are not supported)");
                    }
                    pos = semi + 1;
                }
                return true;
            }

            // Misc: comments, processing instructions, doctype, whitespace.
            // Returns true if it consumed something.
            bool misc(bool& ok) {
                if (starts_with("<!--")) {
                    pos += 4;
                    ok = skip_past("-->", "comment");
                    return true;
                }
                if (starts_with("<?")) {
                    pos += 2;
                    ok = skip_past("?>", "processing instruction");
                    return true;
                }
                if (starts_with("<!DOCTYPE")) {
                    // Skip, including any bracketed internal subset.
                    pos += 9;
                    int depth = 0;
                    while (!at_end()) {
                        char c = src[pos++];
                        if (c == '[') ++depth;
                        else if (c == ']') --depth;
                        else if (c == '>' && depth <= 0) { ok = true; return true; }
                    }
                    ok = fail("unterminated DOCTYPE");
                    return true;
                }
                return false;
            }

            bool element(Element& e, int depth) {
                if (depth > 256)
                    return fail("elements nested too deeply");
                assert(peek() == '<');
                ++pos;
                if (!name(e.name))
                    return false;
                for (;;) {
                    std::size_t before = pos;
                    skip_space();
                    if (starts_with("/>")) {
                        pos += 2;
                        return true;
                    }
                    if (peek() == '>') {
                        ++pos;
                        break;
                    }
                    if (pos == before)
                        return fail("expected whitespace before attribute");
                    std::string_view key;
                    if (!name(key))
                        return false;
                    skip_space();
                    if (peek() != '=')
                        return fail("expected '=' after attribute name");
                    ++pos;
                    skip_space();
                    char q = peek();
                    if (q != '"' && q != '\'')
                        return fail("expected quoted attribute value");
                    ++pos;
                    std::string value;
                    if (!decode_until(value, q == '"' ? "\"<" : "'<"))
                        return false;
                    if (peek() != q)
                        return fail("unterminated attribute value");
                    ++pos;
                    for (auto const& [k, v] : e.attributes)
                        if (k == key)
                            return fail("duplicate attribute");
                    e.attributes.emplace_back(key, std::move(value));
                }
                // Content
                for (;;) {
                    if (at_end())
                        return fail("unterminated element");
                    if (starts_with("</")) {
                        pos += 2;
                        std::string_view end_name;
                        if (!name(end_name))
                            return false;
                        if (end_name != e.name)
                            return fail("mismatched end tag");
                        skip_space();
                        if (peek() != '>')
                            return fail("expected '>' to close end tag");
                        ++pos;
                        return true;
                    }
                    if (starts_with("<![CDATA[")) {
                        pos += 9;
                        std::size_t k = src.find("]]>", pos);
                        if (k == std::string_view::npos)
                            return fail("unterminated CDATA section");
                        e.text.append(src.substr(pos, k - pos));
                        pos = k + 3;
                        continue;
                    }
                    bool ok = true;
                    if (misc(ok)) {
                        if (!ok)
                            return false;
                        continue;
                    }
                    if (peek() == '<') {
                        Element child;
                        bool ok2 = element(child, depth + 1);
                        e.children.push_back(std::move(child));
                        if (!ok2)
                            return false;
                        continue;
                    }
                    if (!decode_until(e.text, "<"))
                        return false;
                }
            }

            void document() {
                // A UTF-8 byte order mark is permitted.
                if (starts_with("\xEF\xBB\xBF"))
                    pos += 3;
                for (;;) {
                    skip_space();
                    if (at_end())
                        break;
                    bool ok = true;
                    if (misc(ok)) {
                        if (!ok)
                            return;
                        continue;
                    }
                    if (peek() != '<') {
                        fail("character data outside the root element");
                        return;
                    }
                    Element e;
                    bool ok2 = element(e, 0);
                    doc.root.children.push_back(std::move(e));
                    if (!ok2)
                        return;
                }
                if (doc.ok() && doc.root.children.empty())
                    fail("no root element");
            }

        };

    } // namespace

    Document parse(std::string_view source) {
        Parser p{source};
        p.document();
        return std::move(p.doc);
    }

    void print(Element const& a, int depth) {
        printf("%*s<%.*s", 2 * depth, "", (int)a.name.size(), a.name.data());
        for (auto const& [k, v] : a.attributes)
            printf(" %.*s=\"%s\"", (int)k.size(), k.data(), v.c_str());
        if (a.children.empty() && a.text.empty()) {
            printf("/>\n");
            return;
        }
        printf(">%s\n", a.text.c_str());
        for (auto const& b : a.children)
            print(b, depth + 1);
        printf("%*s</%.*s>\n", 2 * depth, "", (int)a.name.size(), a.name.data());
    }

    namespace {

        define_test("xml") {

            // Declaration, comments, nesting, empty elements, both quote
            // styles, entities, hyphenated and prefixed names.
            {
                auto d = parse(R"(<?xml version="1.0" encoding="UTF-8"?>
<!-- a comment -->
<!DOCTYPE svg PUBLIC "-//W3C//DTD SVG 1.1//EN" "x.dtd" [ <!ENTITY e "x"> ]>
<svg xmlns:xlink='http://www.w3.org/1999/xlink' viewBox="0 0 10 10">
  <!-- inner -- comment -->
  <g fill-rule="evenodd"><path d="M0 0"/><rect width = "1" /></g>
  <text>a &lt;b&gt; &amp; &#65;&#x42;<![CDATA[<raw>]]></text>
</svg>
<!-- trailing -->)");
                assert(d.ok());
                assert(d.root.children.size() == 1);
                auto const& svg = d.root.children[0];
                assert(svg.name == "svg");
                assert(svg.attribute("xmlns:xlink"));
                assert(*svg.attribute("viewBox") == "0 0 10 10");
                assert(!svg.attribute("width"));
                assert(svg.children.size() == 2);
                auto const& g = svg.children[0];
                assert(*g.attribute("fill-rule") == "evenodd");
                assert(g.children.size() == 2);
                assert(g.children[0].name == "path");
                assert(*g.children[1].attribute("width") == "1");
                auto const& t = svg.children[1];
                assert(t.text == "a <b> & AB<raw>");
            }

            // Errors are reported, not thrown, and keep the partial tree.
            {
                auto d = parse("<a><b></a>");
                assert(!d.ok());
                assert(d.error == "mismatched end tag");
                d = parse("<a x=1/>");
                assert(!d.ok());
                d = parse("<a x='1' x='2'/>");
                assert(!d.ok());
                d = parse("<a>&nbsp;</a>");
                assert(!d.ok());
                d = parse("<a><!-- open");
                assert(!d.ok());
                d = parse("");
                assert(!d.ok());
            }

            co_return;
        };

    } // namespace

} // namespace wry::xml
