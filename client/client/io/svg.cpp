//
//  svg.cpp
//  client
//
//  Created by Antony Searle on 8/3/2026.
//

#include "svg.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <unordered_map>

#include "xml.hpp"

namespace wry::svg {

    namespace {

        using double2 = simd_double2;

        double2 P(double x, double y) { return double2{x, y}; }
        double cross(double2 u, double2 v) { return u.x * v.y - u.y * v.x; }
        double2 perp(double2 u) { return double2{-u.y, u.x}; }

        bool is_space(char c) {
            return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
        }

        std::string_view trim(std::string_view s) {
            while (!s.empty() && is_space(s.front())) s.remove_prefix(1);
            while (!s.empty() && is_space(s.back())) s.remove_suffix(1);
            return s;
        }

        bool iequals(std::string_view a, std::string_view b) {
            if (a.size() != b.size())
                return false;
            for (std::size_t i = 0; i != a.size(); ++i) {
                char x = a[i], y = b[i];
                if (x >= 'A' && x <= 'Z') x += 'a' - 'A';
                if (y >= 'A' && y <= 'Z') y += 'a' - 'A';
                if (x != y)
                    return false;
            }
            return true;
        }

        // ---- Number grammar -----------------------------------------------
        //
        // SVG path grammar numbers:
        //
        //   number   ::= sign? (digits ('.' digits?)? | '.' digits) exponent?
        //   exponent ::= ('e' | 'E') sign? digits
        //
        // Numbers may be packed without separators wherever the grammar is
        // unambiguous: "1-2" is 1, -2 and "0.5.5" is 0.5, 0.5.

        struct Scanner {

            std::string_view s;
            std::size_t i = 0;

            bool at_end() const { return i >= s.size(); }
            char peek() const { return i < s.size() ? s[i] : '\0'; }

            void ws() {
                while (!at_end() && is_space(s[i]))
                    ++i;
            }

            // Whitespace, at most one comma, whitespace.
            void comma_ws() {
                ws();
                if (peek() == ',') {
                    ++i;
                    ws();
                }
            }

            bool number_starts() const {
                char c = peek();
                return (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '+';
            }

            bool number(double& out) {
                std::size_t j = i;
                auto digit = [&](std::size_t k) {
                    return k < s.size() && s[k] >= '0' && s[k] <= '9';
                };
                if (j < s.size() && (s[j] == '+' || s[j] == '-'))
                    ++j;
                bool any = false;
                while (digit(j)) { ++j; any = true; }
                if (j < s.size() && s[j] == '.') {
                    ++j;
                    while (digit(j)) { ++j; any = true; }
                }
                if (!any)
                    return false;
                // An 'e' not followed by an exponent is not consumed.
                if (j < s.size() && (s[j] == 'e' || s[j] == 'E')) {
                    std::size_t k = j + 1;
                    if (k < s.size() && (s[k] == '+' || s[k] == '-'))
                        ++k;
                    if (digit(k)) {
                        while (digit(k)) ++k;
                        j = k;
                    }
                }
                char buf[64];
                std::size_t n = j - i;
                if (n >= sizeof(buf))
                    return false;
                std::memcpy(buf, s.data() + i, n);
                buf[n] = '\0';
                out = std::strtod(buf, nullptr);
                i = j;
                return true;
            }

            // An arc flag is a single '0' or '1', and may be packed.
            bool flag(bool& out) {
                char c = peek();
                if (c != '0' && c != '1')
                    return false;
                out = (c == '1');
                ++i;
                return true;
            }

        };

        // A list of numbers separated by whitespace and/or commas.
        std::vector<double> number_list(std::string_view s, bool* ok = nullptr) {
            std::vector<double> v;
            Scanner sc{s};
            sc.ws();
            double x;
            while (sc.number(x)) {
                v.push_back(x);
                sc.comma_ws();
            }
            if (ok)
                *ok = sc.at_end();
            return v;
        }

        // A length with optional unit.  `reference` resolves percentages.
        std::optional<double> parse_length(std::string_view s, double reference) {
            s = trim(s);
            Scanner sc{s};
            double x;
            if (!sc.number(x))
                return std::nullopt;
            std::string_view unit = trim(s.substr(sc.i));
            if (unit.empty() || unit == "px") return x;
            if (unit == "%") return x * reference / 100.0;
            if (unit == "pt") return x * 4.0 / 3.0;
            if (unit == "pc") return x * 16.0;
            if (unit == "mm") return x * 96.0 / 25.4;
            if (unit == "cm") return x * 96.0 / 2.54;
            if (unit == "in") return x * 96.0;
            if (unit == "em") return x * 16.0;
            if (unit == "ex") return x * 8.0;
            return std::nullopt;
        }

        // ---- Curve helpers ------------------------------------------------

        Cubic make_line(double2 a, double2 b) {
            return Cubic{{a, a + (b - a) / 3.0, a + (b - a) * (2.0 / 3.0), b}, true};
        }

        Cubic make_quad(double2 a, double2 q, double2 b) {
            return Cubic{{a, a + (q - a) * (2.0 / 3.0), b + (q - b) * (2.0 / 3.0), b}, false};
        }

        Cubic reversed(Cubic c) {
            return Cubic{{c.p[3], c.p[2], c.p[1], c.p[0]}, c.line};
        }

        double2 evaluate(Cubic const& c, double t) {
            double s = 1.0 - t;
            return (s * s * s) * c.p[0] + (3 * s * s * t) * c.p[1]
                + (3 * s * t * t) * c.p[2] + (t * t * t) * c.p[3];
        }

        std::pair<Cubic, Cubic> split(Cubic const& c, double t) {
            double2 ab = simd_mix(c.p[0], c.p[1], t);
            double2 bc = simd_mix(c.p[1], c.p[2], t);
            double2 cd = simd_mix(c.p[2], c.p[3], t);
            double2 abc = simd_mix(ab, bc, t);
            double2 bcd = simd_mix(bc, cd, t);
            double2 m = simd_mix(abc, bcd, t);
            return {Cubic{{c.p[0], ab, abc, m}, c.line},
                    Cubic{{m, bcd, cd, c.p[3]}, c.line}};
        }

        double polygon_length(Cubic const& c) {
            return simd_distance(c.p[0], c.p[1]) + simd_distance(c.p[1], c.p[2])
                + simd_distance(c.p[2], c.p[3]);
        }

        // Unit tangents at the ends, skipping coincident control points.
        double2 start_tangent(Cubic const& c) {
            for (int k = 1; k != 4; ++k) {
                double2 d = c.p[k] - c.p[0];
                if (simd_length(d) > 1e-12)
                    return simd_normalize(d);
            }
            return P(1, 0);
        }

        double2 end_tangent(Cubic const& c) {
            for (int k = 2; k >= 0; --k) {
                double2 d = c.p[3] - c.p[k];
                if (simd_length(d) > 1e-12)
                    return simd_normalize(d);
            }
            return P(1, 0);
        }

        // The unit-circle arc from angle t0 sweeping dt (|dt| <= pi/2),
        // mapped by u -> center + mx u.x + my u.y, as one cubic.  The
        // handle length 4/3 tan(dt/4) makes the cubic meet the circle at
        // the ends and the middle; the radial error is below 2.8e-4 of the
        // radius for a quarter circle.
        Cubic arc_piece(double2 center, double2 mx, double2 my, double t0, double dt) {
            double k = 4.0 / 3.0 * std::tan(dt / 4.0);
            double t1 = t0 + dt;
            double2 u0 = P(std::cos(t0), std::sin(t0));
            double2 u1 = P(std::cos(t1), std::sin(t1));
            double2 q[4] = {u0, u0 + k * perp(u0), u1 - k * perp(u1), u1};
            Cubic c;
            for (int i = 0; i != 4; ++i)
                c.p[i] = center + mx * q[i].x + my * q[i].y;
            c.line = false;
            return c;
        }

        // Elliptical arc around center with conjugate axes mx, my, from t0
        // sweeping dt, split into pieces of at most 90 degrees.
        void append_arc_pieces(std::vector<Cubic>& out, double2 center,
                               double2 mx, double2 my, double t0, double dt) {
            int n = std::max(1, (int)std::ceil(std::abs(dt) / (M_PI / 2) - 1e-9));
            double step = dt / n;
            for (int i = 0; i != n; ++i)
                out.push_back(arc_piece(center, mx, my, t0 + i * step, step));
        }

        void append_circle_arc(std::vector<Cubic>& out, double2 center,
                               double r, double t0, double dt) {
            append_arc_pieces(out, center, P(r, 0), P(0, r), t0, dt);
        }

    } // namespace

    void append_arc(Subpath& sp, double2 from, double rx, double ry,
                    double phi_degrees, bool large_arc, bool sweep, double2 to) {
        // SVG 1.1 F.6.2: out-of-range parameters.
        if (simd_equal(from, to))
            return;
        rx = std::abs(rx);
        ry = std::abs(ry);
        if (rx == 0.0 || ry == 0.0) {
            sp.segments.push_back(make_line(from, to));
            return;
        }
        double phi = phi_degrees * (M_PI / 180.0);
        double cp = std::cos(phi), sn = std::sin(phi);
        // F.6.5 step 1
        double2 h = (from - to) * 0.5;
        double x1 = cp * h.x + sn * h.y;
        double y1 = -sn * h.x + cp * h.y;
        // F.6.6 radii correction
        double lambda = (x1 * x1) / (rx * rx) + (y1 * y1) / (ry * ry);
        if (lambda > 1.0) {
            double s = std::sqrt(lambda);
            rx *= s;
            ry *= s;
        }
        // step 2
        double num = rx * rx * ry * ry - rx * rx * y1 * y1 - ry * ry * x1 * x1;
        double den = rx * rx * y1 * y1 + ry * ry * x1 * x1;
        double coef = std::sqrt(std::max(0.0, num / den));
        if (large_arc == sweep)
            coef = -coef;
        double cx1 = coef * rx * y1 / ry;
        double cy1 = -coef * ry * x1 / rx;
        // step 3
        double2 mid = (from + to) * 0.5;
        double2 center = P(cp * cx1 - sn * cy1 + mid.x, sn * cx1 + cp * cy1 + mid.y);
        // step 4
        double2 u = P((x1 - cx1) / rx, (y1 - cy1) / ry);
        double2 v = P((-x1 - cx1) / rx, (-y1 - cy1) / ry);
        double t0 = std::atan2(u.y, u.x);
        double dt = std::atan2(cross(u, v), simd_dot(u, v));
        if (!sweep && dt > 0) dt -= 2 * M_PI;
        if (sweep && dt < 0) dt += 2 * M_PI;
        std::size_t first = sp.segments.size();
        append_arc_pieces(sp.segments, center,
                          P(cp, sn) * rx, P(-sn, cp) * ry, t0, dt);
        // Pin the ends exactly.
        sp.segments[first].p[0] = from;
        sp.segments.back().p[3] = to;
    }

    bool parse_path_data(std::string_view d, Path& out) {
        Scanner sc{d};
        double2 cur = P(0, 0);
        double2 start = P(0, 0);
        double2 last_c2 = P(0, 0); // second control point of the last C/S
        double2 last_q = P(0, 0);  // control point of the last Q/T
        char prev = 0;             // previous command (after implicit repeats)
        char cmd = 0;
        bool after_close = false;
        Subpath* sp = nullptr;

        auto begin_subpath = [&](double2 p) {
            out.push_back(Subpath{});
            sp = &out.back();
            sp->start = p;
        };
        // Drawing commands after a Z without an M start a new subpath at
        // the closed subpath's start.
        auto ensure_subpath = [&]() {
            if (after_close) {
                begin_subpath(start);
                after_close = false;
            }
            sp->drawn = true;
        };

        for (;;) {
            sc.ws();
            if (sc.at_end())
                return true;
            char c = sc.peek();
            if (c && std::strchr("MmLlHhVvCcSsQqTtAaZz", c)) {
                cmd = c;
                ++sc.i;
                sc.ws();
            } else if (sc.number_starts() && cmd && cmd != 'Z' && cmd != 'z') {
                // Implicit repeat; after a moveto the repeat is a lineto.
                if (cmd == 'M') cmd = 'L';
                else if (cmd == 'm') cmd = 'l';
            } else {
                return false;
            }
            if (!sp && cmd != 'M' && cmd != 'm')
                return false; // must begin with a moveto

            bool rel = (cmd >= 'a' && cmd <= 'z');
            double2 base = rel ? cur : P(0, 0);
            auto pt = [&](double2& p) {
                double x, y;
                if (!sc.number(x)) return false;
                sc.comma_ws();
                if (!sc.number(y)) return false;
                sc.comma_ws();
                p = base + P(x, y);
                return true;
            };
            auto num = [&](double& x) {
                if (!sc.number(x)) return false;
                sc.comma_ws();
                return true;
            };

            switch (cmd) {
                case 'M': case 'm': {
                    double2 p;
                    if (!pt(p)) return false;
                    begin_subpath(p);
                    after_close = false;
                    cur = start = p;
                    break;
                }
                case 'Z': case 'z': {
                    if (!after_close) {
                        if (!simd_equal(cur, start))
                            sp->segments.push_back(make_line(cur, start));
                        sp->closed = true;
                        sp->drawn = true;
                    }
                    cur = start;
                    after_close = true;
                    break;
                }
                case 'L': case 'l': {
                    double2 p;
                    if (!pt(p)) return false;
                    ensure_subpath();
                    sp->segments.push_back(make_line(cur, p));
                    cur = p;
                    break;
                }
                case 'H': case 'h': {
                    double x;
                    if (!num(x)) return false;
                    ensure_subpath();
                    double2 p = P(rel ? cur.x + x : x, cur.y);
                    sp->segments.push_back(make_line(cur, p));
                    cur = p;
                    break;
                }
                case 'V': case 'v': {
                    double y;
                    if (!num(y)) return false;
                    ensure_subpath();
                    double2 p = P(cur.x, rel ? cur.y + y : y);
                    sp->segments.push_back(make_line(cur, p));
                    cur = p;
                    break;
                }
                case 'C': case 'c': {
                    double2 p1, p2, p3;
                    if (!pt(p1) || !pt(p2) || !pt(p3)) return false;
                    ensure_subpath();
                    sp->segments.push_back(Cubic{{cur, p1, p2, p3}, false});
                    last_c2 = p2;
                    cur = p3;
                    break;
                }
                case 'S': case 's': {
                    double2 p2, p3;
                    if (!pt(p2) || !pt(p3)) return false;
                    ensure_subpath();
                    bool smooth = prev && std::strchr("CcSs", prev);
                    double2 p1 = smooth ? 2.0 * cur - last_c2 : cur;
                    sp->segments.push_back(Cubic{{cur, p1, p2, p3}, false});
                    last_c2 = p2;
                    cur = p3;
                    break;
                }
                case 'Q': case 'q': {
                    double2 q, p;
                    if (!pt(q) || !pt(p)) return false;
                    ensure_subpath();
                    sp->segments.push_back(make_quad(cur, q, p));
                    last_q = q;
                    cur = p;
                    break;
                }
                case 'T': case 't': {
                    double2 p;
                    if (!pt(p)) return false;
                    ensure_subpath();
                    bool smooth = prev && std::strchr("QqTt", prev);
                    double2 q = smooth ? 2.0 * cur - last_q : cur;
                    sp->segments.push_back(make_quad(cur, q, p));
                    last_q = q;
                    cur = p;
                    break;
                }
                case 'A': case 'a': {
                    double rx, ry, phi;
                    bool large, sweep;
                    double2 p;
                    if (!num(rx) || !num(ry) || !num(phi)) return false;
                    if (!sc.flag(large)) return false;
                    sc.comma_ws();
                    if (!sc.flag(sweep)) return false;
                    sc.comma_ws();
                    if (!pt(p)) return false;
                    ensure_subpath();
                    append_arc(*sp, cur, rx, ry, phi, large, sweep, p);
                    cur = p;
                    break;
                }
                default:
                    return false;
            }
            prev = cmd;
        }
    }

    bool parse_transform(std::string_view s, Affine& out) {
        out = Affine{};
        Affine m{};
        Scanner sc{s};
        for (;;) {
            sc.comma_ws();
            if (sc.at_end())
                break;
            std::size_t first = sc.i;
            while (!sc.at_end() && ((sc.peek() >= 'a' && sc.peek() <= 'z')
                                    || (sc.peek() >= 'A' && sc.peek() <= 'Z')))
                ++sc.i;
            std::string_view name = s.substr(first, sc.i - first);
            sc.ws();
            if (sc.peek() != '(')
                return false;
            ++sc.i;
            sc.ws();
            double v[6];
            int n = 0;
            while (n < 6 && sc.number(v[n])) {
                ++n;
                sc.comma_ws();
            }
            if (sc.peek() != ')')
                return false;
            ++sc.i;
            Affine t{};
            auto rad = [](double deg) { return deg * (M_PI / 180.0); };
            if (name == "matrix" && n == 6) {
                t = Affine{v[0], v[1], v[2], v[3], v[4], v[5]};
            } else if (name == "translate" && (n == 1 || n == 2)) {
                t.e = v[0];
                t.f = n == 2 ? v[1] : 0.0;
            } else if (name == "scale" && (n == 1 || n == 2)) {
                t.a = v[0];
                t.d = n == 2 ? v[1] : v[0];
            } else if (name == "rotate" && (n == 1 || n == 3)) {
                double c = std::cos(rad(v[0])), sn = std::sin(rad(v[0]));
                t = Affine{c, sn, -sn, c, 0, 0};
                if (n == 3) {
                    Affine to{1, 0, 0, 1, v[1], v[2]};
                    Affine back{1, 0, 0, 1, -v[1], -v[2]};
                    t = to * t * back;
                }
            } else if (name == "skewX" && n == 1) {
                t.c = std::tan(rad(v[0]));
            } else if (name == "skewY" && n == 1) {
                t.b = std::tan(rad(v[0]));
            } else {
                return false;
            }
            m = m * t;
        }
        out = m;
        return true;
    }

    namespace {

        struct NamedColor { char const* name; unsigned rgb; };

        // The CSS basic colours plus a few common extended names.
        constexpr NamedColor named_colors[] = {
            {"black", 0x000000}, {"silver", 0xc0c0c0}, {"gray", 0x808080},
            {"grey", 0x808080}, {"white", 0xffffff}, {"maroon", 0x800000},
            {"red", 0xff0000}, {"purple", 0x800080}, {"fuchsia", 0xff00ff},
            {"magenta", 0xff00ff}, {"green", 0x008000}, {"lime", 0x00ff00},
            {"olive", 0x808000}, {"yellow", 0xffff00}, {"navy", 0x000080},
            {"blue", 0x0000ff}, {"teal", 0x008080}, {"aqua", 0x00ffff},
            {"cyan", 0x00ffff}, {"orange", 0xffa500}, {"darkgray", 0xa9a9a9},
            {"darkgrey", 0xa9a9a9}, {"lightgray", 0xd3d3d3},
            {"lightgrey", 0xd3d3d3}, {"brown", 0xa52a2a}, {"pink", 0xffc0cb},
            {"gold", 0xffd700},
        };

        int hex_digit(char c) {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return -1;
        }

    } // namespace

    bool parse_color(std::string_view s, simd_float4& out) {
        s = trim(s);
        if (s.empty())
            return false;
        if (s[0] == '#') {
            std::string_view h = s.substr(1);
            int v[8] = {};
            if (h.size() > 8)
                return false;
            for (std::size_t i = 0; i != h.size(); ++i)
                if ((v[i] = hex_digit(h[i])) < 0)
                    return false;
            if (h.size() == 3 || h.size() == 4) {
                for (std::size_t i = 0; i != 4; ++i)
                    out[i] = i < h.size() ? (float)(v[i] * 17) / 255.0f : 1.0f;
                return true;
            }
            if (h.size() == 6 || h.size() == 8) {
                for (std::size_t i = 0; i != 4; ++i)
                    out[i] = 2 * i < h.size() ? (float)(v[2 * i] * 16 + v[2 * i + 1]) / 255.0f : 1.0f;
                return true;
            }
            return false;
        }
        if ((s.starts_with("rgb(") || s.starts_with("rgba(")) && s.back() == ')') {
            std::string_view body = s.substr(s.find('(') + 1);
            body.remove_suffix(1);
            Scanner sc{body};
            float c[4] = {0, 0, 0, 1};
            int n = 0;
            sc.ws();
            while (n < 4) {
                double x;
                if (!sc.number(x))
                    break;
                bool percent = sc.peek() == '%';
                if (percent)
                    ++sc.i;
                if (n < 3)
                    c[n] = (float)(percent ? x / 100.0 : x / 255.0);
                else
                    c[n] = (float)(percent ? x / 100.0 : x);
                ++n;
                sc.ws();
                if (sc.peek() == ',' || sc.peek() == '/')
                    ++sc.i;
                sc.ws();
            }
            if (n < 3 || !sc.at_end())
                return false;
            for (int i = 0; i != 4; ++i)
                out[i] = std::clamp(c[i], 0.0f, 1.0f);
            return true;
        }
        if (iequals(s, "transparent")) {
            out = simd_float4{0, 0, 0, 0};
            return true;
        }
        for (auto const& nc : named_colors) {
            if (iequals(s, nc.name)) {
                out = simd_float4{
                    (float)((nc.rgb >> 16) & 0xff) / 255.0f,
                    (float)((nc.rgb >> 8) & 0xff) / 255.0f,
                    (float)(nc.rgb & 0xff) / 255.0f,
                    1.0f};
                return true;
            }
        }
        return false;
    }

    namespace {

        // ---- Stroking -----------------------------------------------------
        //
        // A stroke is emitted as a set of closed pieces, each to be oriented
        // positive: a body per (flattish piece of a) segment, a wedge per
        // join on its outer side, a cap per open end.  Pieces that abut do
        // so along shared edges, which cancel in the winding sum; where they
        // overlap (the inner side of a join) the rasterizer clamps the
        // winding sum to one, so the union is free.  The outline is built
        // in user space and then transformed, which is exact for any affine
        // CTM, non-uniform scale included.

        using Contour = std::vector<Cubic>;

        enum class Cap { butt, round, square };
        enum class Join { miter, round, bevel };

        struct StrokeStyle {
            double half_width;
            Cap cap;
            Join join;
            double miter_limit;
        };

        Contour polygon(std::initializer_list<double2> ps) {
            Contour c;
            std::vector<double2> v(ps);
            for (std::size_t i = 0; i != v.size(); ++i)
                c.push_back(make_line(v[i], v[(i + 1) % v.size()]));
            return c;
        }

        // Total turning, in radians, between successive control legs.
        double polygon_turn(Cubic const& c) {
            double total = 0.0;
            double2 prev = P(0, 0);
            bool have = false;
            for (int k = 0; k != 3; ++k) {
                double2 d = c.p[k + 1] - c.p[k];
                if (simd_length(d) < 1e-12)
                    continue;
                if (have)
                    total += std::abs(std::atan2(cross(prev, d), simd_dot(prev, d)));
                prev = d;
                have = true;
            }
            return total;
        }

        void stroke_body(Cubic const& c, double hw, std::vector<Contour>& out, int depth = 0) {
            if (c.line) {
                double2 n = perp(start_tangent(c)) * hw;
                out.push_back(polygon({c.p[0] + n, c.p[3] + n, c.p[3] - n, c.p[0] - n}));
                return;
            }
            if (depth < 10 && polygon_turn(c) > 20.0 * (M_PI / 180.0)) {
                auto [l, r] = split(c, 0.5);
                stroke_body(l, hw, out, depth + 1);
                stroke_body(r, hw, out, depth + 1);
                return;
            }
            double2 n0 = perp(start_tangent(c)) * hw;
            double2 n3 = perp(end_tangent(c)) * hw;
            double chord = simd_distance(c.p[0], c.p[3]);
            auto offset = [&](double s) {
                double2 q0 = c.p[0] + s * n0;
                double2 q3 = c.p[3] + s * n3;
                // Scale the handles with the chord, which is exact for
                // circular arcs (radius r +/- hw over r).
                double k = chord > 1e-12 ? simd_distance(q0, q3) / chord : 1.0;
                return Cubic{{q0, q0 + (c.p[1] - c.p[0]) * k,
                              q3 + (c.p[2] - c.p[3]) * k, q3}, false};
            };
            Cubic outer = offset(+1.0);
            Cubic inner = offset(-1.0);
            Contour k;
            k.push_back(outer);
            k.push_back(make_line(outer.p[3], inner.p[3]));
            k.push_back(reversed(inner));
            k.push_back(make_line(inner.p[0], outer.p[0]));
            out.push_back(std::move(k));
        }

        void stroke_join(double2 v, double2 ta, double2 tb, StrokeStyle const& st,
                         std::vector<Contour>& out) {
            double cr = cross(ta, tb);
            if (std::abs(cr) < 1e-9 && simd_dot(ta, tb) > 0)
                return; // smooth
            double hw = st.half_width;
            // The outer side is opposite the turn.
            double s = cr > 0 ? -1.0 : 1.0;
            double2 na = perp(ta) * (s * hw);
            double2 nb = perp(tb) * (s * hw);
            double2 a = v + na, b = v + nb;
            switch (st.join) {
                case Join::round: {
                    Contour k;
                    k.push_back(make_line(v, a));
                    double t0 = std::atan2(na.y, na.x);
                    double sweep = std::atan2(cross(na, nb), simd_dot(na, nb));
                    append_circle_arc(k, v, hw, t0, sweep);
                    k.back().p[3] = b;
                    k.push_back(make_line(b, v));
                    out.push_back(std::move(k));
                    return;
                }
                case Join::miter: {
                    // Miter length over stroke width is 1 / sin(theta / 2),
                    // theta the angle between the segments.
                    double theta = std::atan2(std::abs(cross(-ta, tb)), simd_dot(-ta, tb));
                    double ratio = 1.0 / std::max(1e-12, std::sin(theta / 2));
                    if (ratio <= st.miter_limit && std::abs(cr) > 1e-12) {
                        double u = cross(b - a, tb) / cr;
                        double2 m = a + ta * u;
                        out.push_back(polygon({v, a, m, b}));
                        return;
                    }
                    [[fallthrough]];
                }
                case Join::bevel:
                    out.push_back(polygon({v, a, b}));
                    return;
            }
        }

        // Cap at p, with d the unit direction pointing out of the stroke.
        void stroke_cap(double2 p, double2 d, StrokeStyle const& st, std::vector<Contour>& out) {
            double hw = st.half_width;
            double2 n = perp(d) * hw;
            switch (st.cap) {
                case Cap::butt:
                    return;
                case Cap::square:
                    out.push_back(polygon({p + n, p - n, p - n + d * hw, p + n + d * hw}));
                    return;
                case Cap::round: {
                    // Half disc from +n through d to -n (d is n turned by
                    // -90 degrees), closed by the diameter.
                    Contour k;
                    append_circle_arc(k, p, hw, std::atan2(n.y, n.x), -M_PI);
                    k.front().p[0] = p + n;
                    k.back().p[3] = p - n;
                    k.push_back(make_line(p - n, p + n));
                    out.push_back(std::move(k));
                    return;
                }
            }
        }

        void stroke_subpath(Subpath const& sp, StrokeStyle const& st, std::vector<Contour>& out) {
            std::vector<Cubic> segs;
            for (auto const& c : sp.segments)
                if (polygon_length(c) > 1e-9)
                    segs.push_back(c);
            double hw = st.half_width;
            if (segs.empty()) {
                // A zero-length subpath: round and square caps make a dot.
                if (!sp.drawn)
                    return;
                double2 p = sp.segments.empty() ? sp.start : sp.segments.front().p[0];
                if (st.cap == Cap::round) {
                    Contour k;
                    append_circle_arc(k, p, hw, 0.0, 2 * M_PI);
                    out.push_back(std::move(k));
                } else if (st.cap == Cap::square) {
                    out.push_back(polygon({p + P(-hw, -hw), p + P(hw, -hw),
                                           p + P(hw, hw), p + P(-hw, hw)}));
                }
                return;
            }
            for (auto const& c : segs)
                stroke_body(c, hw, out);
            for (std::size_t i = 0; i + 1 < segs.size(); ++i)
                stroke_join(segs[i].p[3], end_tangent(segs[i]), start_tangent(segs[i + 1]), st, out);
            if (sp.closed) {
                stroke_join(segs.front().p[0], end_tangent(segs.back()), start_tangent(segs.front()), st, out);
            } else {
                stroke_cap(segs.front().p[0], -start_tangent(segs.front()), st, out);
                stroke_cap(segs.back().p[3], end_tangent(segs.back()), st, out);
            }
        }

        // ---- Orientation for the clamped winding sum ----------------------

        using FContour = std::vector<bezier4>;

        bezier4 to_bezier4(Cubic const& c, Affine const& m) {
            bezier4 b;
            for (int i = 0; i != 4; ++i)
                b.columns[i] = simd_float(m(c.p[i]));
            return b;
        }

        Cubic to_cubic(bezier4 const& b) {
            return Cubic{{simd_double(b.columns[0]), simd_double(b.columns[1]),
                          simd_double(b.columns[2]), simd_double(b.columns[3])}, false};
        }

        void reverse(FContour& k) {
            std::reverse(k.begin(), k.end());
            for (auto& b : k) {
                std::swap(b.columns[0], b.columns[3]);
                std::swap(b.columns[1], b.columns[2]);
            }
        }

        // Winding number of the flattened contours around p.
        int winding(std::vector<std::vector<double2>> const& polys, double2 p) {
            int w = 0;
            for (auto const& poly : polys) {
                for (std::size_t i = 0; i + 1 < poly.size(); ++i) {
                    double2 a = poly[i], b = poly[i + 1];
                    if (a.y <= p.y) {
                        if (b.y > p.y && cross(b - a, p - a) > 0) ++w;
                    } else {
                        if (b.y <= p.y && cross(b - a, p - a) < 0) --w;
                    }
                }
            }
            return w;
        }

        enum class FillRule { nonzero, evenodd };

        // Reorient the contours so that the rasterizer's clamped signed sum
        // gives the fill rule's coverage: every contour that bounds the fill
        // is oriented with the fill on its left (positive integral of x dy),
        // and contours with fill on both sides or neither are dropped.  Each
        // contour's curves vote, by testing the fill rule just either side
        // of their midpoints.  Exact when contours do not cross one another;
        // where they do (overlapping contours under evenodd, or opposed
        // overlapping contours under nonzero) the majority wins.
        std::vector<bezier4> orient_fill(std::vector<FContour> ks, FillRule rule) {
            std::vector<std::vector<double2>> polys;
            double2 lo = P(INFINITY, INFINITY), hi = P(-INFINITY, -INFINITY);
            for (auto const& k : ks) {
                std::vector<double2> poly;
                for (auto const& b : k) {
                    Cubic c = to_cubic(b);
                    // Includes t = 0.5 exactly, the vote point.
                    for (int j = 0; j != 16; ++j)
                        poly.push_back(evaluate(c, j / 16.0));
                    for (int j = 0; j != 4; ++j) {
                        lo = simd_min(lo, c.p[j]);
                        hi = simd_max(hi, c.p[j]);
                    }
                }
                if (!poly.empty())
                    poly.push_back(poly.front());
                polys.push_back(std::move(poly));
            }
            double eps = 1e-6 * std::max(1e-9, simd_length(hi - lo));
            auto filled = [&](int w) {
                return rule == FillRule::nonzero ? w != 0 : (w & 1) != 0;
            };
            std::vector<bezier4> result;
            for (auto& k : ks) {
                int left = 0, right = 0;
                for (auto const& b : k) {
                    Cubic c = to_cubic(b);
                    double2 tan = evaluate(c, 0.5 + 1.0 / 64) - evaluate(c, 0.5 - 1.0 / 64);
                    if (simd_length(tan) < 1e-12)
                        continue;
                    double2 n = perp(simd_normalize(tan)) * eps;
                    double2 p = evaluate(c, 0.5);
                    bool fl = filled(winding(polys, p + n));
                    bool fr = filled(winding(polys, p - n));
                    if (fl && !fr) ++left;
                    if (fr && !fl) ++right;
                }
                if (left == 0 && right == 0)
                    continue;
                if (right > left)
                    reverse(k);
                result.insert(result.end(), k.begin(), k.end());
            }
            return result;
        }

        std::vector<bezier4> orient_union(std::vector<FContour> ks) {
            std::vector<bezier4> result;
            for (auto& k : ks) {
                double a = bezier4_signed_area(k);
                if (std::abs(a) < 1e-12)
                    continue;
                if (a < 0.0)
                    reverse(k);
                result.insert(result.end(), k.begin(), k.end());
            }
            return result;
        }

        // ---- Styles -------------------------------------------------------

        struct Paint {
            enum Kind { NONE, COLOR, CURRENT_COLOR } kind = NONE;
            simd_float4 color = {0, 0, 0, 1};
        };

        struct Style {
            Paint fill{Paint::COLOR, {0, 0, 0, 1}};
            Paint stroke{};
            FillRule fill_rule = FillRule::nonzero;
            double fill_opacity = 1.0;
            double stroke_opacity = 1.0;
            double group_opacity = 1.0; // product of ancestors' opacity
            double stroke_width = 1.0;
            Cap cap = Cap::butt;
            Join join = Join::miter;
            double miter_limit = 4.0;
            simd_float4 color = {0, 0, 0, 1};
            bool visible = true;
            bool display = true;
        };

        template<typename F>
        void for_each_declaration(std::string_view s, F&& f) {
            while (!s.empty()) {
                auto semi = s.find(';');
                std::string_view decl = s.substr(0, semi);
                s = semi == std::string_view::npos ? std::string_view{} : s.substr(semi + 1);
                auto colon = decl.find(':');
                if (colon == std::string_view::npos)
                    continue;
                f(trim(decl.substr(0, colon)), trim(decl.substr(colon + 1)));
            }
        }

        double parse_unit_number(std::string_view v, double fallback) {
            v = trim(v);
            Scanner sc{v};
            double x;
            if (!sc.number(x))
                return fallback;
            if (sc.peek() == '%')
                x /= 100.0;
            return x;
        }

        std::string_view local_name(std::string_view n) {
            // Drop a namespace prefix such as "svg:".
            auto k = n.find(':');
            return k == std::string_view::npos ? n : n.substr(k + 1);
        }

        struct Renderer {

            Document& doc;
            std::unordered_map<std::string_view, xml::Element const*> ids;
            Affine output; // user space of the root viewport -> output
            double viewport_w = 0, viewport_h = 0;
            int use_depth = 0;

            void skip(std::string what) { ++doc.skipped[std::move(what)]; }

            void index(xml::Element const& e) {
                ++doc.elements[std::string(e.name)];
                if (auto id = e.attribute("id"))
                    ids.emplace(std::string_view(*id), &e);
                for (auto const& c : e.children)
                    index(c);
            }

            xml::Element const* lookup_url(std::string_view v, std::string_view* rest) {
                v = trim(v);
                if (!v.starts_with("url("))
                    return nullptr;
                auto close = v.find(')');
                if (close == std::string_view::npos)
                    return nullptr;
                std::string_view ref = trim(v.substr(4, close - 4));
                if (ref.size() >= 2 && (ref.front() == '"' || ref.front() == '\'')) {
                    ref.remove_prefix(1);
                    ref.remove_suffix(1);
                }
                *rest = trim(v.substr(close + 1));
                if (!ref.starts_with("#"))
                    return nullptr;
                auto it = ids.find(ref.substr(1));
                return it == ids.end() ? nullptr : it->second;
            }

            xml::Element const* lookup_href(xml::Element const& e) {
                std::string const* h = e.attribute("href");
                if (!h)
                    h = e.attribute("xlink:href");
                if (!h || !h->starts_with("#"))
                    return nullptr;
                auto it = ids.find(std::string_view(*h).substr(1));
                return it == ids.end() ? nullptr : it->second;
            }

            // Mean stop colour of a gradient, following href for stops.
            std::optional<simd_float4> gradient_mean(xml::Element const& g, int depth = 0) {
                simd_float4 sum = {0, 0, 0, 0};
                int n = 0;
                for (auto const& s : g.children) {
                    if (local_name(s.name) != "stop")
                        continue;
                    simd_float4 c = {0, 0, 0, 1};
                    double opacity = 1.0;
                    auto take = [&](std::string_view k, std::string_view v) {
                        if (k == "stop-color") parse_color(v, c);
                        else if (k == "stop-opacity") opacity = parse_unit_number(v, 1.0);
                    };
                    for (auto const& [k, v] : s.attributes)
                        take(k, v);
                    if (auto st = s.attribute("style"))
                        for_each_declaration(*st, take);
                    c.a *= (float)std::clamp(opacity, 0.0, 1.0);
                    sum += c;
                    ++n;
                }
                if (n)
                    return sum / (float)n;
                if (depth < 8)
                    if (auto h = lookup_href(g))
                        return gradient_mean(*h, depth + 1);
                return std::nullopt;
            }

            void parse_paint(std::string_view v, Paint& p) {
                v = trim(v);
                simd_float4 c;
                if (v == "none") {
                    p.kind = Paint::NONE;
                } else if (v == "currentColor") {
                    p.kind = Paint::CURRENT_COLOR;
                } else if (v.starts_with("url(")) {
                    std::string_view rest;
                    xml::Element const* e = lookup_url(v, &rest);
                    std::string_view n = e ? local_name(e->name) : std::string_view{};
                    if (e && (n == "linearGradient" || n == "radialGradient")) {
                        if (auto m = gradient_mean(*e)) {
                            skip("gradient paint (flat mean stop colour)");
                            p.kind = Paint::COLOR;
                            p.color = *m;
                            return;
                        }
                    }
                    skip(e ? "paint server <" + std::string(n) + ">" : "unresolved url() paint");
                    if (!rest.empty())
                        parse_paint(rest, p); // the fallback
                    else
                        p.kind = Paint::NONE;
                } else if (parse_color(v, c)) {
                    p.kind = Paint::COLOR;
                    p.color = c;
                } else {
                    skip("unparsed paint '" + std::string(v) + "'");
                }
            }

            void apply(Style& s, Style const& parent, std::string_view k, std::string_view v) {
                v = trim(v);
                if (v == "inherit")
                    return; // already inherited from the parent
                if (k == "fill") parse_paint(v, s.fill);
                else if (k == "stroke") parse_paint(v, s.stroke);
                else if (k == "fill-rule")
                    s.fill_rule = (v == "evenodd") ? FillRule::evenodd : FillRule::nonzero;
                else if (k == "fill-opacity")
                    s.fill_opacity = std::clamp(parse_unit_number(v, 1.0), 0.0, 1.0);
                else if (k == "stroke-opacity")
                    s.stroke_opacity = std::clamp(parse_unit_number(v, 1.0), 0.0, 1.0);
                else if (k == "opacity")
                    s.group_opacity = parent.group_opacity
                        * std::clamp(parse_unit_number(v, 1.0), 0.0, 1.0);
                else if (k == "stroke-width") {
                    if (auto w = parse_length(v, std::hypot(viewport_w, viewport_h) / M_SQRT2))
                        s.stroke_width = std::max(0.0, *w);
                } else if (k == "stroke-linecap") {
                    s.cap = v == "round" ? Cap::round : v == "square" ? Cap::square : Cap::butt;
                } else if (k == "stroke-linejoin") {
                    s.join = v == "round" ? Join::round : v == "bevel" ? Join::bevel : Join::miter;
                    if (v == "arcs" || v == "miter-clip")
                        skip("stroke-linejoin " + std::string(v) + " (as miter)");
                } else if (k == "stroke-miterlimit") {
                    s.miter_limit = std::max(1.0, parse_unit_number(v, 4.0));
                } else if (k == "color") {
                    simd_float4 c;
                    if (parse_color(v, c)) s.color = c;
                } else if (k == "display") {
                    s.display = v != "none";
                } else if (k == "visibility") {
                    s.visible = v == "visible";
                } else if (k == "stroke-dasharray") {
                    if (v != "none") skip("stroke-dasharray (drawn solid)");
                } else if (k == "filter" || k == "mask" || k == "clip-path") {
                    if (v != "none") skip(std::string(k) + " (ignored)");
                } else if (k == "marker-start" || k == "marker-mid" || k == "marker-end" || k == "marker") {
                    if (v != "none") skip("markers");
                } else if (k == "paint-order") {
                    if (v != "normal" && v != "fill" && !v.starts_with("fill stroke"))
                        skip("paint-order");
                }
            }

            // Presentation attributes, then style="..." declarations, over
            // the inherited style.
            Style style_for(xml::Element const& e, Style const& parent) {
                Style s = parent;
                s.display = true; // not inherited
                for (auto const& [k, v] : e.attributes)
                    if (k != "style")
                        apply(s, parent, k, v);
                if (auto st = e.attribute("style"))
                    for_each_declaration(*st, [&](std::string_view k, std::string_view v) {
                        apply(s, parent, k, v);
                    });
                if (e.attribute("class"))
                    skip("class attribute (CSS selectors)");
                return s;
            }

            double length_attr(xml::Element const& e, char const* key, double reference, double fallback = 0.0) {
                if (auto v = e.attribute(key))
                    if (auto x = parse_length(*v, reference))
                        return *x;
                return fallback;
            }

            // viewBox + preserveAspectRatio mapping into a w x h viewport.
            Affine viewbox_transform(xml::Element const& e, double w, double h) {
                std::string const* vb = e.attribute("viewBox");
                if (!vb)
                    return Affine{};
                bool ok = false;
                auto v = number_list(*vb, &ok);
                if (!ok || v.size() != 4 || v[2] <= 0 || v[3] <= 0) {
                    skip("malformed viewBox");
                    return Affine{};
                }
                double sx = w / v[2], sy = h / v[3];
                double tx = 0, ty = 0;
                std::string const* pa = e.attribute("preserveAspectRatio");
                std::string_view par = trim(pa ? std::string_view(*pa) : "xMidYMid");
                if (par != "none") {
                    if (!par.starts_with("xMidYMid") || par.ends_with("slice"))
                        skip("preserveAspectRatio " + std::string(par) + " (as xMidYMid meet)");
                    double s = std::min(sx, sy);
                    tx = (w - v[2] * s) / 2;
                    ty = (h - v[3] * s) / 2;
                    sx = sy = s;
                }
                return Affine{sx, 0, 0, sy, tx - v[0] * sx, ty - v[1] * sy};
            }

            void emit(Path const& path, Affine const& ctm, Style const& st) {
                if (!st.visible || path.empty())
                    return;
                Affine m = output * ctm;
                auto resolve = [&](Paint const& p) {
                    return p.kind == Paint::CURRENT_COLOR ? st.color : p.color;
                };
                if (st.fill.kind != Paint::NONE) {
                    std::vector<FContour> ks;
                    for (auto const& sp : path) {
                        if (sp.segments.empty())
                            continue;
                        FContour k;
                        for (auto const& c : sp.segments)
                            k.push_back(to_bezier4(c, m));
                        // Filling closes open subpaths.
                        double2 a = sp.segments.back().p[3], b = sp.segments.front().p[0];
                        if (!simd_equal(a, b))
                            k.push_back(to_bezier4(make_line(a, b), m));
                        ks.push_back(std::move(k));
                    }
                    simd_float4 c = resolve(st.fill);
                    c.a *= (float)(st.fill_opacity * st.group_opacity);
                    auto curves = orient_fill(std::move(ks), st.fill_rule);
                    if (!curves.empty() && c.a > 0)
                        doc.layers.push_back(Layer{c, std::move(curves), false});
                }
                if (st.stroke.kind != Paint::NONE && st.stroke_width > 0) {
                    StrokeStyle ss{st.stroke_width / 2, st.cap, st.join, st.miter_limit};
                    std::vector<Contour> pieces;
                    for (auto const& sp : path)
                        stroke_subpath(sp, ss, pieces);
                    std::vector<FContour> ks;
                    for (auto const& p : pieces) {
                        FContour k;
                        for (auto const& c : p)
                            k.push_back(to_bezier4(c, m));
                        ks.push_back(std::move(k));
                    }
                    simd_float4 c = resolve(st.stroke);
                    c.a *= (float)(st.stroke_opacity * st.group_opacity);
                    auto curves = orient_union(std::move(ks));
                    if (!curves.empty() && c.a > 0)
                        doc.layers.push_back(Layer{c, std::move(curves), true});
                }
            }

            void path_error(xml::Element const& e) {
                if (doc.error.empty()) {
                    doc.error = "path data error in <" + std::string(e.name) + ">";
                    if (auto id = e.attribute("id"))
                        doc.error += " id=\"" + *id + "\"";
                }
            }

            void shape(xml::Element const& e, std::string_view n, Affine const& ctm, Style const& st) {
                double vw = viewport_w, vh = viewport_h;
                double vd = std::hypot(vw, vh) / M_SQRT2;
                Path path;
                if (n == "path") {
                    if (auto d = e.attribute("d"))
                        if (!parse_path_data(*d, path))
                            path_error(e); // render what came before
                } else if (n == "rect") {
                    double x = length_attr(e, "x", vw), y = length_attr(e, "y", vh);
                    double w = length_attr(e, "width", vw), h = length_attr(e, "height", vh);
                    if (w <= 0 || h <= 0)
                        return;
                    double rx = -1, ry = -1;
                    if (auto v = e.attribute("rx")) if (auto r = parse_length(*v, vw)) rx = *r;
                    if (auto v = e.attribute("ry")) if (auto r = parse_length(*v, vh)) ry = *r;
                    if (rx < 0) rx = ry;
                    if (ry < 0) ry = rx;
                    rx = std::clamp(rx, 0.0, w / 2);
                    ry = std::clamp(ry, 0.0, h / 2);
                    Subpath sp;
                    sp.drawn = sp.closed = true;
                    auto L = [&](double2 a, double2 b) { sp.segments.push_back(make_line(a, b)); };
                    // Quarter arcs; in y-down user space +angle is clockwise
                    // on screen.
                    auto A = [&](double2 c, double t0) {
                        append_arc_pieces(sp.segments, c, P(rx, 0), P(0, ry), t0, M_PI / 2);
                    };
                    if (rx > 0 && ry > 0) {
                        sp.start = P(x + rx, y);
                        L(P(x + rx, y), P(x + w - rx, y));
                        A(P(x + w - rx, y + ry), -M_PI / 2);
                        L(P(x + w, y + ry), P(x + w, y + h - ry));
                        A(P(x + w - rx, y + h - ry), 0.0);
                        L(P(x + w - rx, y + h), P(x + rx, y + h));
                        A(P(x + rx, y + h - ry), M_PI / 2);
                        L(P(x, y + h - ry), P(x, y + ry));
                        A(P(x + rx, y + ry), M_PI);
                        // Drop zero-length edges (rx == w / 2 and so on).
                        std::erase_if(sp.segments, [](Cubic const& c) { return polygon_length(c) < 1e-12; });
                    } else {
                        sp.start = P(x, y);
                        L(P(x, y), P(x + w, y));
                        L(P(x + w, y), P(x + w, y + h));
                        L(P(x + w, y + h), P(x, y + h));
                        L(P(x, y + h), P(x, y));
                    }
                    path.push_back(std::move(sp));
                } else if (n == "circle" || n == "ellipse") {
                    double cx = length_attr(e, "cx", vw), cy = length_attr(e, "cy", vh);
                    double rx, ry;
                    if (n == "circle") {
                        rx = ry = length_attr(e, "r", vd);
                    } else {
                        rx = length_attr(e, "rx", vw, -1);
                        ry = length_attr(e, "ry", vh, -1);
                        if (rx < 0) rx = ry;
                        if (ry < 0) ry = rx;
                    }
                    if (rx <= 0 || ry <= 0)
                        return;
                    Subpath sp;
                    sp.drawn = sp.closed = true;
                    sp.start = P(cx + rx, cy);
                    append_arc_pieces(sp.segments, P(cx, cy), P(rx, 0), P(0, ry), 0.0, 2 * M_PI);
                    path.push_back(std::move(sp));
                } else if (n == "line") {
                    double2 a = P(length_attr(e, "x1", vw), length_attr(e, "y1", vh));
                    double2 b = P(length_attr(e, "x2", vw), length_attr(e, "y2", vh));
                    Subpath sp;
                    sp.drawn = true;
                    sp.start = a;
                    sp.segments.push_back(make_line(a, b));
                    path.push_back(std::move(sp));
                    // A line has no interior.
                    Style s2 = st;
                    s2.fill.kind = Paint::NONE;
                    emit(path, ctm, s2);
                    return;
                } else if (n == "polyline" || n == "polygon") {
                    bool ok = true;
                    std::string const* pts = e.attribute("points");
                    auto v = number_list(pts ? std::string_view(*pts) : "", &ok);
                    if (!ok || (v.size() & 1))
                        path_error(e); // render the pairs before the error
                    v.resize(v.size() & ~std::size_t(1));
                    if (v.size() < 2)
                        return;
                    Subpath sp;
                    sp.drawn = true;
                    sp.start = P(v[0], v[1]);
                    for (std::size_t i = 2; i + 1 < v.size(); i += 2)
                        sp.segments.push_back(make_line(P(v[i - 2], v[i - 1]), P(v[i], v[i + 1])));
                    if (n == "polygon") {
                        double2 last = P(v[v.size() - 2], v[v.size() - 1]);
                        if (!simd_equal(last, sp.start))
                            sp.segments.push_back(make_line(last, sp.start));
                        sp.closed = true;
                    }
                    path.push_back(std::move(sp));
                }
                emit(path, ctm, st);
            }

            void children(xml::Element const& e, Affine const& ctm, Style const& st) {
                for (auto const& c : e.children)
                    walk(c, ctm, st);
            }

            void walk(xml::Element const& e, Affine ctm, Style const& parent) {
                std::string_view n = local_name(e.name);
                // Never rendered directly (referenced, or metadata).
                if (n == "defs" || n == "symbol" || n == "linearGradient" || n == "radialGradient"
                    || n == "clipPath" || n == "mask" || n == "pattern" || n == "marker"
                    || n == "filter" || n == "title" || n == "desc" || n == "metadata"
                    || n == "script")
                    return;
                if (n == "style") {
                    skip("<style> (CSS stylesheet)");
                    return;
                }
                Style st = style_for(e, parent);
                if (!st.display)
                    return;
                if (auto t = e.attribute("transform")) {
                    Affine m;
                    if (parse_transform(*t, m))
                        ctm = ctm * m;
                    else
                        skip("malformed transform");
                }
                if (n == "g" || n == "a") {
                    children(e, ctm, st);
                } else if (n == "switch") {
                    skip("<switch> (rendered as <g>)");
                    children(e, ctm, st);
                } else if (n == "svg") {
                    // A nested viewport (not clipped).
                    double x = length_attr(e, "x", viewport_w), y = length_attr(e, "y", viewport_h);
                    double w = length_attr(e, "width", viewport_w, viewport_w);
                    double h = length_attr(e, "height", viewport_h, viewport_h);
                    children(e, ctm * Affine{1, 0, 0, 1, x, y} * viewbox_transform(e, w, h), st);
                } else if (n == "use") {
                    xml::Element const* target = lookup_href(e);
                    if (!target) {
                        skip("<use> with unresolved href");
                        return;
                    }
                    if (use_depth > 16) {
                        skip("<use> nested too deeply (or cyclic)");
                        return;
                    }
                    ctm = ctm * Affine{1, 0, 0, 1, length_attr(e, "x", viewport_w), length_attr(e, "y", viewport_h)};
                    ++use_depth;
                    if (local_name(target->name) == "symbol") {
                        Style ts = style_for(*target, st);
                        double w = length_attr(e, "width", viewport_w, length_attr(*target, "width", viewport_w, viewport_w));
                        double h = length_attr(e, "height", viewport_h, length_attr(*target, "height", viewport_h, viewport_h));
                        children(*target, ctm * viewbox_transform(*target, w, h), ts);
                    } else {
                        walk(*target, ctm, st);
                    }
                    --use_depth;
                } else if (n == "path" || n == "rect" || n == "circle" || n == "ellipse"
                           || n == "line" || n == "polyline" || n == "polygon") {
                    shape(e, n, ctm, st);
                } else {
                    skip("<" + std::string(n) + ">");
                }
            }

        };

        xml::Element const* find_root(xml::Document const& tree) {
            for (auto const& c : tree.root.children)
                if (local_name(c.name) == "svg")
                    return &c;
            return nullptr;
        }

        void begin(Document& doc, Renderer& r, xml::Document const& tree) {
            if (!tree.ok())
                doc.error = "XML: " + tree.error + " at byte " + std::to_string(tree.error_offset);
            for (auto const& c : tree.root.children)
                r.index(c);
        }

    } // namespace

    Document parse(std::string_view source) {
        Document doc;
        xml::Document tree = xml::parse(source);
        Renderer r{doc};
        begin(doc, r, tree);
        xml::Element const* root = find_root(tree);
        if (!root) {
            if (doc.error.empty())
                doc.error = "no <svg> root element";
            return doc;
        }
        // The root viewport: width / height, defaulting to the viewBox.
        double vbw = 0, vbh = 0;
        if (auto vb = root->attribute("viewBox")) {
            auto v = number_list(*vb);
            if (v.size() == 4) {
                vbw = v[2];
                vbh = v[3];
            }
        }
        double w = vbw, h = vbh;
        if (auto v = root->attribute("width"))
            if (!std::string_view(*v).ends_with("%"))
                if (auto x = parse_length(*v, 0)) w = *x;
        if (auto v = root->attribute("height"))
            if (!std::string_view(*v).ends_with("%"))
                if (auto x = parse_length(*v, 0)) h = *x;
        if (w <= 0 || h <= 0) {
            // The CSS default replaced-element size.
            w = vbw > 0 ? vbw : 300;
            h = vbh > 0 ? vbh : 150;
        }
        doc.size = simd_float2{(float)w, (float)h};
        // Percentages inside resolve against the viewBox, if any.
        r.viewport_w = vbw > 0 ? vbw : w;
        r.viewport_h = vbh > 0 ? vbh : h;
        // y-up output: (x, h - y)
        r.output = Affine{1, 0, 0, -1, 0, h} * r.viewbox_transform(*root, w, h);
        Style rs = r.style_for(*root, Style{});
        Affine ctm{};
        if (auto t = root->attribute("transform"))
            if (!parse_transform(*t, ctm))
                ctm = Affine{};
        if (rs.display)
            r.children(*root, ctm, rs);
        return doc;
    }

    Document parse_otf_glyph(std::string_view source, int glyph_index) {
        Document doc;
        if (source.size() >= 2 && (unsigned char)source[0] == 0x1F && (unsigned char)source[1] == 0x8B) {
            doc.error = "gzip-compressed SVG document (not supported)";
            return doc;
        }
        xml::Document tree = xml::parse(source);
        Renderer r{doc};
        begin(doc, r, tree);
        xml::Element const* root = find_root(tree);
        std::string id = "glyph" + std::to_string(glyph_index);
        auto it = r.ids.find(std::string_view(id));
        if (!root || it == r.ids.end()) {
            if (doc.error.empty())
                doc.error = "no element with id=\"" + id + "\"";
            return doc;
        }
        // Font design units, y-up.
        r.output = Affine{1, 0, 0, -1, 0, 0};
        // Walk from the root to the glyph element, accumulating ancestors'
        // style and transform, then render only the glyph's subtree.
        std::vector<xml::Element const*> chain;
        auto find_chain = [&](auto& self, xml::Element const& e) -> bool {
            chain.push_back(&e);
            if (&e == it->second)
                return true;
            for (auto const& c : e.children)
                if (self(self, c))
                    return true;
            chain.pop_back();
            return false;
        };
        if (!find_chain(find_chain, *root)) {
            doc.error = "glyph element is outside the root <svg>";
            return doc;
        }
        Style st;
        Affine ctm{};
        for (std::size_t i = 0; i + 1 < chain.size(); ++i) {
            st = r.style_for(*chain[i], st);
            if (auto t = chain[i]->attribute("transform")) {
                Affine m;
                if (parse_transform(*t, m))
                    ctm = ctm * m;
            }
        }
        r.walk(*it->second, ctm, st);
        return doc;
    }

    matrix<RGBA8Unorm_sRGB> rasterize(Document const& doc, float ppu,
                                      simd_float2 lo, simd_float2 hi,
                                      simd_float3 background) {
        float px = 1.0f / ppu;
        int x0 = (int)std::floor(lo.x * ppu);
        int x1 = (int)std::ceil(hi.x * ppu);
        int y0 = (int)std::floor(lo.y * ppu);
        int y1 = (int)std::ceil(hi.y * ppu);
        int width = std::max(0, x1 - x0), height = std::max(0, y1 - y0);
        // Linear, opaque accumulation.
        std::vector<simd_float3> acc((std::size_t)width * height, background);
        for (auto const& layer : doc.layers) {
            simd_float2 blo = {INFINITY, INFINITY}, bhi = {-INFINITY, -INFINITY};
            for (auto const& b : layer.curves)
                for (int i = 0; i != 4; ++i) {
                    blo = simd_min(blo, b.columns[i]);
                    bhi = simd_max(bhi, b.columns[i]);
                }
            // The prefilter spills ~3 pixels past the control points.
            int c0 = std::max(x0, (int)std::floor(blo.x * ppu) - 3) - x0;
            int c1 = std::min(x1, (int)std::ceil(bhi.x * ppu) + 3) - x0;
            int r0 = y1 - std::min(y1, (int)std::ceil(bhi.y * ppu) + 3);
            int r1 = y1 - std::max(y0, (int)std::floor(blo.y * ppu) - 3);
            simd_float3 color = {from_sRGB(layer.color.r), from_sRGB(layer.color.g), from_sRGB(layer.color.b)};
            for (int row = r0; row < r1; ++row) {
                float y = (y1 - row - 0.5f) * px;
                for (int col = c0; col < c1; ++col) {
                    float x = (x0 + col + 0.5f) * px;
                    float cov = std::clamp(bezier4_coverage(layer.curves, simd_float2{x, y}, px), 0.0f, 1.0f);
                    float a = cov * layer.color.a;
                    auto& d = acc[(std::size_t)row * width + col];
                    d += (color - d) * a;
                }
            }
        }
        matrix<RGBA8Unorm_sRGB> image((std::size_t)height, (std::size_t)width);
        for (int row = 0; row != height; ++row)
            for (int col = 0; col != width; ++col) {
                auto c = acc[(std::size_t)row * width + col];
                image[row, col] = RGBA8Unorm_sRGB(c.r, c.g, c.b, 1.0f);
            }
        return image;
    }

} // namespace wry::svg

// ---- Tests ----------------------------------------------------------------

#include <fstream>
#include <sstream>

#include "image.hpp"
#include "otf.hpp"
#include "test.hpp"
#include "assert.hpp"

namespace wry::svg {

    namespace {

        bool near(double a, double b, double tol) { return std::abs(a - b) <= tol; }
        bool near(double2 a, double2 b, double tol) { return simd_distance(a, b) <= tol; }

        double layers_area(Document const& d) {
            double a = 0;
            for (auto const& l : d.layers)
                a += bezier4_signed_area(l.curves);
            return a;
        }

        // Rasterized coverage of white-on-black, integrated: the painted
        // area (each layer opaque white).
        double raster_area(Document const& d, float ppu) {
            auto img = rasterize(d, ppu, simd_float2{0, 0}, d.size, simd_float3{0, 0, 0});
            double sum = 0;
            for (std::size_t i = 0; i != img.minor(); ++i)
                for (std::size_t j = 0; j != img.major(); ++j)
                    sum += (float)img[i, j].r;
            return sum / (ppu * ppu);
        }

        std::string wrap(char const* body, char const* size = "100") {
            return std::string("<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"") + size
                + "\" height=\"" + size + "\">" + body + "</svg>";
        }

        define_test("svg_path_grammar") {
            // Packed signs and decimals.
            {
                Path p;
                require(parse_path_data("M10-20L.5.5", p));
                require(p.size() == 1 && p[0].segments.size() == 1);
                require(near(p[0].start, P(10, -20), 0));
                require(near(p[0].segments[0].p[3], P(0.5, 0.5), 0));
                require(p[0].segments[0].line);
            }
            // Exponents; an 'e' with no digits is not an exponent.
            {
                Path p;
                require(parse_path_data("M1e2,1E-1l1.5e+1-2", p));
                require(near(p[0].start, P(100, 0.1), 1e-12));
                require(near(p[0].segments[0].p[3], P(115, -1.9), 1e-12));
            }
            // Implicit repeats: lineto after moveto, relative after m.
            {
                Path p;
                require(parse_path_data("M0 0 10 0 10 10z", p));
                require(p.size() == 1 && p[0].closed && p[0].segments.size() == 3);
                Path q;
                require(parse_path_data("m1 1 2 0 0 2", q));
                require(near(q[0].segments[0].p[3], P(3, 1), 0));
                require(near(q[0].segments[1].p[3], P(3, 3), 0));
            }
            // H V h v
            {
                Path p;
                require(parse_path_data("M0 0H10V5h-5v-5", p));
                auto const& s = p[0].segments;
                require(s.size() == 4);
                require(near(s[0].p[3], P(10, 0), 0) && near(s[1].p[3], P(10, 5), 0));
                require(near(s[2].p[3], P(5, 5), 0) && near(s[3].p[3], P(5, 0), 0));
            }
            // Smooth cubic reflects the previous second control point;
            // smooth after a non-cubic uses the current point.
            {
                Path p;
                require(parse_path_data("M0 0C1 1 2 1 3 0S5 -1 6 0", p));
                require(near(p[0].segments[1].p[1], P(4, -1), 1e-12));
                Path q;
                require(parse_path_data("M0 0L3 0S5 -1 6 0", q));
                require(near(q[0].segments[1].p[1], P(3, 0), 1e-12));
            }
            // Quadratics become exact cubics; T reflects, including chains.
            {
                Path p;
                require(parse_path_data("M0 0Q1 1 2 0T4 0t2 0", p));
                auto const& s = p[0].segments;
                require(s.size() == 3);
                require(near(s[0].p[1], P(2.0 / 3, 2.0 / 3), 1e-12));
                // Second control point reflected to (3, -1).
                require(near(s[1].p[1], P(2, 0) + (P(3, -1) - P(2, 0)) * (2.0 / 3), 1e-12));
                // Third reflected back to (5, 1).
                require(near(s[2].p[1], P(4, 0) + (P(5, 1) - P(4, 0)) * (2.0 / 3), 1e-12));
            }
            // After Z the next subpath starts at the closed one's start, and
            // relative commands are relative to it.
            {
                Path p;
                require(parse_path_data("M5 5 L6 5 L6 6 z l1 1", p));
                require(p.size() == 2);
                require(p[0].closed && !p[1].closed);
                require(near(p[1].segments[0].p[0], P(5, 5), 0));
                require(near(p[1].segments[0].p[3], P(6, 6), 0));
            }
            // Packed arc flags: large=1 sweep=0 then the endpoint.
            {
                Path p;
                require(parse_path_data("M0 0a5 5 0 1010 0", p));
                auto const& s = p[0].segments;
                require(s.size() == 2); // a semicircle is two quarter arcs
                require(near(s.back().p[3], P(10, 0), 0));
                for (auto const& c : s)
                    for (int i = 0; i <= 8; ++i)
                        require(near(simd_distance(evaluate(c, i / 8.0), P(5, 0)), 5.0, 5 * 3e-4));
            }
            // Errors keep the prefix; a path must start with a moveto.
            {
                Path p;
                require(!parse_path_data("M0 0 L1 1 L", p));
                require(p.size() == 1 && p[0].segments.size() == 1);
                Path q;
                require(!parse_path_data("L1 1", q));
                require(q.empty());
                Path r;
                require(!parse_path_data("M0 0 1", r));
                Path s;
                require(!parse_path_data("M0 0 z 1 1", s)); // no implicit repeat of Z
                Path t;
                require(!parse_path_data("M0 0 a1 1 0 2 0 1 1", t)); // flag must be 0/1
                Path u;
                require(parse_path_data("  M 0,0 , 1,1  ", u)); // comma-wsp
                require(u[0].segments.size() == 1);
            }
            co_return;
        };

        define_test("svg_arc") {
            // An ellipse with known center, radii and rotation; the arc
            // between two parameter angles in each of the four flag
            // combinations.  Every sample of every cubic lies on the
            // analytic ellipse to within the cubic approximation error.
            double2 c = P(2, 1);
            double rx = 5, ry = 3, phi = 30.0;
            double cp = std::cos(phi * M_PI / 180), sn = std::sin(phi * M_PI / 180);
            auto on = [&](double t) {
                double2 u = P(rx * std::cos(t), ry * std::sin(t));
                return c + P(cp * u.x - sn * u.y, sn * u.x + cp * u.y);
            };
            double2 a = on(10 * M_PI / 180), b = on(250 * M_PI / 180);
            for (int large = 0; large != 2; ++large) {
                for (int sweep = 0; sweep != 2; ++sweep) {
                    Subpath sp;
                    append_arc(sp, a, rx, ry, phi, large, sweep, b);
                    // Sweeping positive from 10 to 250 degrees is the large
                    // arc (240); negative is the small one (120).  The other
                    // two combinations use the mirror-image center.
                    bool same_center = (large == sweep);
                    int expected = large ? 3 : 2;
                    require((int)sp.segments.size() == expected);
                    require(near(sp.segments.front().p[0], a, 0));
                    require(near(sp.segments.back().p[3], b, 0));
                    double worst = 0;
                    double2 center = same_center ? c : a + b - c;
                    for (auto const& k : sp.segments) {
                        for (int i = 0; i <= 32; ++i) {
                            double2 d = evaluate(k, i / 32.0) - center;
                            double2 q = P(cp * d.x + sn * d.y, -sn * d.x + cp * d.y);
                            double r = std::hypot(q.x / rx, q.y / ry);
                            worst = std::max(worst, std::abs(r - 1));
                        }
                    }
                    require(worst < 3e-4);
                }
            }
            // Radii too small are scaled up: a unit-radius request between
            // points 10 apart becomes a radius-5 semicircle.
            {
                Subpath sp;
                append_arc(sp, P(0, 0), 1, 1, 0, false, true, P(10, 0));
                for (auto const& k : sp.segments)
                    for (int i = 0; i <= 8; ++i)
                        require(near(simd_distance(evaluate(k, i / 8.0), P(5, 0)), 5.0, 5 * 3e-4));
                // sweep=1 is the positive-angle direction, clockwise on a
                // y-down screen: left to right over the top, through y < 0.
                require(evaluate(sp.segments[0], 1.0).y < -4.9);
            }
            // Zero radius is a line; coincident endpoints are nothing.
            {
                Subpath sp;
                append_arc(sp, P(0, 0), 0, 1, 0, false, true, P(10, 0));
                require(sp.segments.size() == 1 && sp.segments[0].line);
                Subpath sq;
                append_arc(sq, P(1, 1), 3, 3, 0, false, true, P(1, 1));
                require(sq.segments.empty());
            }
            co_return;
        };

        define_test("svg_transform") {
            Affine m;
            require(parse_transform("translate(10,20) scale(2) rotate(90)", m));
            require(near(m(P(1, 0)), P(10, 22), 1e-12));
            require(parse_transform("rotate(90 5 5)", m));
            require(near(m(P(6, 5)), P(5, 6), 1e-12));
            require(parse_transform("skewX(45)", m));
            require(near(m(P(0, 1)), P(1, 1), 1e-12));
            require(parse_transform("skewY(45)", m));
            require(near(m(P(1, 0)), P(1, 1), 1e-12));
            require(parse_transform(" matrix(1 2 3 4 5 6) ", m));
            require(near(m(P(1, 1)), P(9, 12), 1e-12));
            require(parse_transform("scale(2,3)translate(1)", m));
            require(near(m(P(0, 0)), P(2, 0), 1e-12));
            require(parse_transform("", m));
            require(!parse_transform("translate(1", m));
            require(!parse_transform("rotate(1 2)", m));
            require(!parse_transform("wobble(1)", m));

            // Composed down the tree, then the root viewBox and the y-up
            // output flip.
            auto d = parse(R"~(<svg xmlns="http://www.w3.org/2000/svg" width="200" height="200" viewBox="0 0 100 100">
                <g transform="translate(10 0)"><g transform="scale(2)">
                <rect width="1" height="1"/></g></g></svg>)~");
            require(d.ok() && d.layers.size() == 1);
            require(near(d.size.x, 200, 0) && near(d.size.y, 200, 0));
            simd_float2 lo = {INFINITY, INFINITY}, hi = {-INFINITY, -INFINITY};
            for (auto const& b : d.layers[0].curves)
                for (int i = 0; i != 4; ++i) {
                    lo = simd_min(lo, b.columns[i]);
                    hi = simd_max(hi, b.columns[i]);
                }
            // user x in [10, 12], y in [0, 2]; viewBox scale 2; flip.
            require(near(lo.x, 20, 1e-4) && near(hi.x, 24, 1e-4));
            require(near(lo.y, 196, 1e-4) && near(hi.y, 200, 1e-4));
            co_return;
        };

        define_test("svg_color") {
            simd_float4 c;
            require(parse_color("#f00", c) && c.r == 1 && c.g == 0 && c.a == 1);
            require(parse_color("#00ff0080", c) && c.g == 1 && near(c.a, 128 / 255.0, 1e-6));
            require(parse_color("rgb(255, 0, 0)", c) && c.r == 1 && c.b == 0);
            require(parse_color("rgb(100%,50%,0%)", c) && c.r == 1 && near(c.g, 0.5, 1e-6));
            require(parse_color("rgba(0,0,255,0.5)", c) && c.b == 1 && near(c.a, 0.5, 1e-6));
            require(parse_color("RED", c) && c.r == 1);
            require(parse_color("transparent", c) && c.a == 0);
            require(!parse_color("none", c));
            require(!parse_color("#12345", c));
            require(!parse_color("#ggg", c));

            // Inheritance, style= over attributes, opacity, currentColor.
            auto d = parse(wrap(R"(<g fill="red" color="#00f" opacity="0.5">
                <rect width="1" height="1" style="fill-opacity: 0.5"/>
                <rect width="1" height="1" fill="#0f0" style="fill:currentColor"/>
                <rect width="1" height="1" fill="none"/></g>)").c_str());
            require(d.ok() && d.layers.size() == 2);
            require(d.layers[0].color.r == 1 && near(d.layers[0].color.a, 0.25, 1e-6));
            require(d.layers[1].color.b == 1 && d.layers[1].color.g == 0);
            require(near(d.layers[1].color.a, 0.5, 1e-6));
            co_return;
        };

        define_test("svg_shapes") {
            auto area = [](char const* body) {
                auto d = parse(wrap(body).c_str());
                require(d.ok());
                return layers_area(d);
            };
            require(near(area(R"(<rect x="5" y="5" width="10" height="20"/>)"), 200, 1e-3));
            require(near(area(R"(<rect width="10" height="20" rx="2" ry="3"/>)"), 200 - (4 - M_PI) * 6, 1e-2));
            // rx alone sets ry; each clamps to half its side, independently
            // (so this is a 10 x 4 ellipse).
            require(near(area(R"(<rect width="10" height="4" rx="9"/>)"), M_PI * 10, 1e-2));
            require(near(area(R"(<rect width="10" height="4" rx="2"/>)"), 40 - (4 - M_PI) * 4, 1e-2));
            require(near(area(R"(<circle cx="50" cy="50" r="5"/>)"), M_PI * 25, 25 * 1e-3));
            require(near(area(R"(<ellipse cx="50" cy="50" rx="4" ry="2"/>)"), M_PI * 8, 8 * 1e-3));
            require(near(area(R"(<polygon points="0,0 4,0 0,3"/>)"), 6, 1e-4));
            // Fill closes an open polyline.
            require(near(area(R"(<polyline points="0 0 4 0 0 3"/>)"), 6, 1e-4));
            // Not rendered: zero size, no fill, a line's interior.
            require(area(R"(<rect width="0" height="10"/>)") == 0);
            require(area(R"(<circle r="5" fill="none"/>)") == 0);
            require(area(R"(<line x1="0" y1="0" x2="10" y2="10"/>)") == 0);
            require(area(R"(<rect width="10" height="10" display="none"/>)") == 0);
            require(area(R"(<g display="none"><rect width="10" height="10"/></g>)") == 0);
            // <use> of <defs>, with x / y and style inheritance.
            {
                auto d = parse(wrap(R"(<defs><circle id="c" r="2"/></defs>
                    <use href="#c" x="10" y="10" fill="#00f"/>
                    <use xlink:href="#c" x="20" y="10"/>)").c_str());
                require(d.ok() && d.layers.size() == 2);
                require(d.layers[0].color.b == 1 && d.layers[1].color.b == 0);
                require(near(layers_area(d), 2 * M_PI * 4, 1e-2));
            }
            co_return;
        };

        define_test("svg_fill_rule") {
            // Two concentric circles drawn the same way round.
            char const* rings = R"(<path fill-rule="%s" d="M 40 50 A 10 10 0 1 1 60 50 A 10 10 0 1 1 40 50 Z M 45 50 A 5 5 0 1 1 55 50 A 5 5 0 1 1 45 50 Z"/>)";
            char buf[512];
            std::snprintf(buf, sizeof buf, rings, "evenodd");
            auto d = parse(wrap(buf).c_str());
            require(d.ok());
            require(near(layers_area(d), M_PI * 75, 0.1));
            std::snprintf(buf, sizeof buf, rings, "nonzero");
            d = parse(wrap(buf).c_str());
            require(near(layers_area(d), M_PI * 100, 0.1));
            // Inner drawn the other way round: a hole under either rule.
            char const* opposed = R"(<path fill-rule="%s" d="M 40 50 A 10 10 0 1 1 60 50 A 10 10 0 1 1 40 50 Z M 45 50 A 5 5 0 1 0 55 50 A 5 5 0 1 0 45 50 Z"/>)";
            std::snprintf(buf, sizeof buf, opposed, "nonzero");
            d = parse(wrap(buf).c_str());
            require(near(layers_area(d), M_PI * 75, 0.1));
            // A single contour drawn clockwise on screen still fills.
            d = parse(wrap(R"(<path d="M0 0 H10 V10 H0 Z"/>)").c_str());
            require(near(layers_area(d), 100, 1e-3));
            d = parse(wrap(R"(<path d="M0 0 V10 H10 V0 Z"/>)").c_str());
            require(near(layers_area(d), 100, 1e-3));
            co_return;
        };

        define_test("svg_rasterize") {
            // End to end through the shared rasterizer: a 10 x 10 square and
            // an r = 6 circle, white on black.  The prefilter preserves the
            // integral, so the summed coverage is the painted area.
            auto d = parse(R"(<?xml version="1.0"?>
<!-- end to end -->
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 40 40" width="40" height="40">
  <rect x="5" y="5" width="10" height="10" fill="white"/>
  <circle cx="28" cy="28" r="6" style="fill: #fff"/>
</svg>)");
            require(d.ok() && d.layers.size() == 2);
            double expected = 100 + M_PI * 36;
            require(near(raster_area(d, 4), expected, expected * 0.01));
            auto img = rasterize(d, 4, simd_float2{0, 0}, d.size, simd_float3{0, 0, 0});
            require(img.minor() == 160 && img.major() == 160);
            // Square center (10, 10) in y-down user space is row 40.
            require(((float)img[40, 40].r > 0.99f));
            require(((float)img[112, 112].r > 0.99f)); // circle center
            require(((float)img[2, 2].r < 0.01f));
            require(((float)img[40, 112].r < 0.01f));

            // Strokes: a butt-capped line is a w x L rectangle; round caps
            // add a disc; square caps add w^2.
            auto line = [](char const* cap) {
                char buf[256];
                std::snprintf(buf, sizeof buf, R"(<path d="M 20 50 H 80" stroke="#fff" stroke-width="4" stroke-linecap="%s" fill="none"/>)", cap);
                return parse(wrap(buf).c_str());
            };
            require(near(layers_area(line("butt")), 240, 1e-3));
            require(near(layers_area(line("round")), 240 + M_PI * 4, 1e-2));
            require(near(layers_area(line("square")), 240 + 16, 1e-3));
            // A closed stroked square with miter joins is the difference of
            // two squares; its pieces overlap, so measure by rasterizing.
            auto sq = parse(wrap(R"(<rect x="20" y="20" width="40" height="40" fill="none" stroke="#fff" stroke-width="4"/>)").c_str());
            double ring = 44 * 44 - 36 * 36;
            require(near(raster_area(sq, 4), ring, ring * 0.01));
            // A stroked circle is an annulus (offset arcs are exact radii).
            auto ci = parse(wrap(R"(<circle cx="50" cy="50" r="20" fill="none" stroke="#fff" stroke-width="4"/>)").c_str());
            double annulus = M_PI * (22 * 22 - 18 * 18);
            require(near(raster_area(ci, 4), annulus, annulus * 0.01));
            // Round joins on a polyline: two butt bodies, a quarter disc on
            // the outer corner, the inner corner square counted once.
            auto pl = parse(wrap(R"(<polyline points="20,20 60,20 60,60" fill="none" stroke="#fff" stroke-width="4" stroke-linejoin="round"/>)").c_str());
            double bent = 2 * 40 * 4 - 4 + M_PI * 4 / 4;
            require(near(raster_area(pl, 4), bent, bent * 0.01));
            co_return;
        };

        define_test("svg_otf") {
            // An OpenType 'SVG ' document with two glyphs and shared defs.
            char const* doc = R"~(<svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink">
                <defs><rect id="r" width="10" height="10"/></defs>
                <g id="glyph1"><rect width="1" height="1"/></g>
                <g id="glyph2" transform="translate(5 0)"><use xlink:href="#r" y="-10"/></g>
                </svg>)~";
            auto d = parse_otf_glyph(doc, 2);
            require(d.ok() && d.layers.size() == 1);
            simd_float2 lo = {INFINITY, INFINITY}, hi = {-INFINITY, -INFINITY};
            for (auto const& b : d.layers[0].curves)
                for (int i = 0; i != 4; ++i) {
                    lo = simd_min(lo, b.columns[i]);
                    hi = simd_max(hi, b.columns[i]);
                }
            // y-down glyph space above the baseline becomes y-up.
            require(near(lo.x, 5, 1e-5) && near(hi.x, 15, 1e-5));
            require(near(lo.y, 0, 1e-5) && near(hi.y, 10, 1e-5));
            require(near(layers_area(d), 100, 1e-3));
            require(!parse_otf_glyph(doc, 3).ok());
            require(!parse_otf_glyph("\x1F\x8B...", 1).ok());

            // The table lookup, over a hand-built 'SVG ' table: header,
            // document list of two records (glyphs 1-3, glyph 5), documents.
            std::string a = "<svg>A</svg>", b = "<svg>B</svg>";
            std::vector<unsigned char> t;
            auto u16 = [&](unsigned v) { t.push_back((v >> 8) & 0xff); t.push_back(v & 0xff); };
            auto u32 = [&](unsigned v) { u16(v >> 16); u16(v & 0xffff); };
            u16(0); u32(10); u32(0);
            std::size_t list_size = 2 + 12 * 2;
            u16(2);
            u16(1); u16(3); u32((unsigned)list_size); u32((unsigned)a.size());
            u16(5); u16(5); u32((unsigned)(list_size + a.size())); u32((unsigned)b.size());
            t.insert(t.end(), a.begin(), a.end());
            t.insert(t.end(), b.begin(), b.end());
            span<byte const> s((byte const*)t.data(), t.size());
            require(otf::svg_document_in_table(s, 1) == a);
            require(otf::svg_document_in_table(s, 3) == a);
            require(otf::svg_document_in_table(s, 4).empty());
            require(otf::svg_document_in_table(s, 5) == b);
            require(otf::svg_document_in_table(s, 0).empty());
            require(otf::svg_document_in_table(span<byte const>((byte const*)t.data(), 20), 1).empty());
            co_return;
        };

        // Opt-in report on a real file: WRY_SVG_FILE names an SVG to parse;
        // WRY_SVG_PNG_DIR, if also set, receives renderings: the whole
        // document at 1024 px, and any cells, in y-down output units (the
        // root's px), given as WRY_SVG_CELLS="x,y,w,h;x,y,w,h", at
        // WRY_SVG_CELL_PPU (default 2) pixels per unit.
        define_test("svg_file_report") {
            char const* path = std::getenv("WRY_SVG_FILE");
            if (!path)
                co_return;
            std::ifstream f(path, std::ios::binary);
            require(f);
            std::stringstream ss;
            ss << f.rdbuf();
            std::string text = ss.str();
            auto d = parse(text);
            printf("svg report: %s\n", path);
            printf("  error: %s\n", d.ok() ? "(none)" : d.error.c_str());
            printf("  size: %g x %g\n", d.size.x, d.size.y);
            int strokes = 0;
            std::size_t curves = 0;
            for (auto const& l : d.layers) {
                strokes += l.is_stroke;
                curves += l.curves.size();
            }
            printf("  layers: %zu (%d fill, %d stroke), %zu cubic curves\n",
                   d.layers.size(), (int)d.layers.size() - strokes, strokes, curves);
            for (auto const& [k, v] : d.elements)
                printf("  element %-20s %d\n", k.c_str(), v);
            for (auto const& [k, v] : d.skipped)
                printf("  skipped %-40s %d\n", k.c_str(), v);
            require(d.ok());
            if (char const* dir = std::getenv("WRY_SVG_PNG_DIR")) {
                auto write = [&](std::string name, float ppu, simd_float2 lo, simd_float2 hi) {
                    auto img = rasterize(d, ppu, lo, hi, simd_float3{0.05f, 0.05f, 0.06f});
                    std::string out = std::string(dir) + "/" + name;
                    to_png(img, StringView(out.c_str()));
                    printf("  wrote %s (%zu x %zu)\n", out.c_str(), img.major(), img.minor());
                };
                write("svg_sheet.png", 1024.0f / std::max(d.size.x, d.size.y),
                      simd_float2{0, 0}, d.size);
                char const* ppu_env = std::getenv("WRY_SVG_CELL_PPU");
                float ppu = ppu_env ? (float)std::atof(ppu_env) : 2.0f;
                if (char const* cells = std::getenv("WRY_SVG_CELLS")) {
                    int k = 0;
                    for (std::string_view cs = cells; !cs.empty(); ++k) {
                        auto semi = cs.find(';');
                        auto v = number_list(cs.substr(0, semi));
                        cs = semi == std::string_view::npos ? std::string_view{} : cs.substr(semi + 1);
                        if (v.size() != 4)
                            continue;
                        // y-down output cell -> y-up output box.
                        simd_float2 lo = {(float)v[0], d.size.y - (float)(v[1] + v[3])};
                        simd_float2 hi = {(float)(v[0] + v[2]), d.size.y - (float)v[1]};
                        write("svg_cell_" + std::to_string(k) + ".png", ppu, lo, hi);
                    }
                }
            }
            co_return;
        };

    } // namespace

} // namespace wry::svg
