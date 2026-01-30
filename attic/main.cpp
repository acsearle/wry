//
//  main.cpp
//  aag
//
//  Created by Antony Searle on 12/1/2026.
//

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cassert>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <vector>
#include <utility>
#include <charconv>
#include <map>
#include <compare>
#include <deque>
#include <stack>
#include <span>
#include <cmath>
#include <concepts>
#include <bit>
#include <type_traits>

#include <simd/simd.h>


namespace tmp {
    
    // Quadratic equation
    
    int sgn(double x) {
        return (0.0 < x) - (x < 0.0);
    }
    

    
    struct Bezier2 {
        
        simd_double2 a;
        simd_double2 b;
        simd_double2 c;
        
        simd_double2 xy_for_t(double t) const {
            if (t < 0.0 || t > 1.0) {
                printf("warning: evaluating Bezier curve out of bounds\n");
            }
            simd_double2 ab = simd_mix(a, b, t);
            simd_double2 bc = simd_mix(b, c, t);
            simd_double2 abc = simd_mix(ab, bc, t);
            return abc;
        }

        // TODO: We want to evaluate this 4 times for xlo, xhi, ylo, yhi
        // This is perfect for SIMD
        
        static double root(double a, double b, double c, double t0, double t1) {

            // For a quadratic equation known to have exactly one root in the
            // closed interval [t0, t1], find that root
            
            // a == 0 means .b.y == mean(.a.y, .c.y); the curve, or at least y(t) is linear
            // b == 0 means .b.y == .a.y; the curve is horizontal at .a and t == 0.0
            // c == 0 means that y = .a.y and the root is at t == 0.0
            
            double d = b*b - 4.0*a*c;
            if (d < 0.0) {
                printf("warning: discrimiant suggests no root\n");
            }
            double q = -0.5 * (b + copysign(sqrt(std::max(d, 0.0)), b));
            double r0 = (a != 0.0 ? q / a : -1.0);
            double r1 = (q != 0.0 ? c / q : -1.0);
            double tmid = (t0 + t1) * 0.5;
            if (abs(r0 - tmid) <= abs(r1 - tmid)) {
                if (r0 < t0 || r0 > t1) {
                    printf("warning: clamping best root\n");
                }
                return simd_clamp(r0, t0, t1);
            } else {
                if (r1 < t0 || r1 > t1)
                    printf("warning: clamping best root\n");
                return simd_clamp(r1, t0, t1);
            }
        }

        // y = (a.y * (1.0 - t) + b.y * t) * (1.0 - t) + (b.y * (1.0 - t) + c.y * t) * t
        // 0 = ((-a.y + b.y)*t + a.y) * (1.0 - t) + ((-b.y + c.y) * t + b.y) * t - y
        //   = (a.y - 2.0*b.y + c.y)*t*t + (-2.0*a.y + 2.0*b.y)*t + (a.y - y)
        double t_for_y(double y, double t0, double t1) const {
            assert(std::min(a.y, c.y) <= b.y);
            assert(b.y <= std::max(a.y, c.y));
            assert(std::min(a.y, c.y) <= y);
            assert(y <= std::max(a.y, c.y));
            assert(0 <= t0);
            assert(t0 <= t1);
            assert(t1 <= 1.0);
            auto r = root(a.y - 2.0*b.y + c.y,
                          -2.0*a.y + 2.0*b.y,
                          a.y - y,
                          t0,
                          t1);
            return r;
        }
        
        double t_for_x(double x, double t0, double t1) const {
            assert(std::min(a.x, c.x) <= b.x);
            assert(b.x <= std::max(a.x, c.x));
            assert(std::min(a.x, c.x) <= x);
            assert(x <= std::max(a.x, c.x));
            assert(0 <= t0);
            assert(t0 <= t1);
            assert(t1 <= 1.0);
            auto r = root(a.x - 2.0*b.x + c.x,
                          -2.0*a.x + 2.0*b.x,
                          a.x - x,
                          t0,
                          t1);
            return r;
        }
        
        // TODO: In theory we should be able to eliminate t entirely and
        // operate on an implicit f(x, y) == 0 <=> y = g(x), x = h(y)
        
        double x_for_y(double y) const {
            
            simd_double2 d = (c - a) * 0.5;
            
            simd_double2 a = this->a - d;
            simd_double2 b = this->b - d;
            simd_double2 c = this->c - d;
            
            simd_double2 p{};

            double u = simd_cross(b, p).z / simd_cross(b, c).z;
            // u|a = -1.0, u|c = +1

            double v = simd_cross(p, c).z / simd_length_squared(c);
            

            return 0.0;
        }
        
        
        

        static std::vector<Bezier2> fromBezier3(simd_double2 p,
                                               simd_double2 q,
                                               simd_double2 r,
                                               simd_double2 s) {
            // We have a cubic Bezier curve
            // We want a set of quadratic Bezier curves
            // Endpoints must be exact
            // Coordinate extrema must be jointed
            //
            
            double lpq = simd_distance(p, q);
            double lrs = simd_distance(r, s);
            double t = lrs / (lrs + lpq);

            
            return {};
        }
        
                
    };
    
    
    struct Bezier3 {
        
        simd_double2 a;
        simd_double2 b;
        simd_double2 c;
        simd_double2 d;
        
        simd_double2 xy_for_t(double t) const {
            if (t < 0.0 || t > 1.0) {
                printf("warning: evaluating Bezier curve out of bounds\n");
            }
            simd_double2 ab = simd_mix(a, b, t);
            simd_double2 bc = simd_mix(b, c, t);
            simd_double2 cd = simd_mix(c, d, t);
            simd_double2 abc = simd_mix(ab, bc, t);
            simd_double2 bcd = simd_mix(bc, cd, t);
            simd_double2 abcd = simd_mix(abc, bcd, t);
            return abcd;
        }
        
        simd_double2 operator()(double t) const {
            return xy_for_t(t);
        }
        
        Bezier2 ddt() {
            // let s = (1 - t)
            // d/dt s = -1
            // d/dt a*s*s*s + 3*b*s*s*t + 3*c*s*t*t + d*t*t*t
            //   = -3*a*s*s - 3*b*(-2*s*t+s*s) + 3*c(-t*t + 2*s*t) + 3*d*t*t
            //   = (-3*a+3*b)*s*s + (-6*b+6*c)*s*t + (-3*c+3*d)*t*t
            return Bezier2(3.0*(b - a), 3.0*(c - b), 3.0*(d - c));
        }
        
    };
    
    
    
    
}




static_assert(std::is_same_v<unsigned char, std::uint8_t>);

using byte = unsigned char;

// Network to host generic byte order conversion

template<typename T>
T ntohg(T x) {
    if constexpr (sizeof(T) == 1) {
        return x;
    } else if constexpr (sizeof(T) == 2) {
        return ntohs(x);
    } else if constexpr (sizeof(T) == 4) {
        return ntohl(x);
    } else if constexpr (sizeof(T) == 8) {
        return ntohll(x);
    } else {
        static_assert(false, "Unsupported integer size\n");
    }
}

namespace wry {
    
    using byte = unsigned char;
 
    template<typename T>
    struct span {
        
        T* _begin;
        T* _end;
        
        span() : _begin{}, _end{} {}
        span(span const&) = default;
        span(T* p, T* q) : _begin(p), _end(q) { assert(p <= q); }
        span(T* p, std::size_t n) : _begin(p), _end(p + n) {}
        
        // const

        [[nodiscard]] bool empty() const { return _begin == _end; }
        [[nodiscard]] explicit operator bool() const { return _begin != _end; }

        [[nodiscard]] std::size_t size() const { return _end - _begin; }
        [[nodiscard]] T* data() const { return _begin; }
                
        [[nodiscard]] T* begin() const { return _begin; }
        [[nodiscard]] T* end() const { return _end; }
        [[nodiscard]] std::add_const_t<T>* cbegin() const { return _begin; }
        [[nodiscard]] std::add_const_t<T>* cend() const { return _end; }

        [[nodiscard]] T& operator[](std::size_t i) const { assert(i < size()); return _begin[i]; }

        [[nodiscard]] T& front() const { assert(!empty()); return *_begin; }
        [[nodiscard]] T& back() const { assert(!empty()); return *(_end - 1); }

        [[nodiscard]] span<T> before(T* p) const {
            return { _begin, p };
        }
        
        [[nodiscard]] span<T> after(T* p) const {
            return { p, _end };
        }

        [[nodiscard]] span<T> before(std::size_t n) const {
            assert(n < size());
            return { _begin, _begin + n };
        }
        
        [[nodiscard]] span<T> after(std::size_t n) const {
            assert(n < size());
            return { _begin + n, _end };
        }

        [[nodiscard]] std::pair<span<T>, span<T>> partition(T* p) const {
            return {{ _begin, p}, {p, _end}};
        }

        [[nodiscard]] span<T> first(std::size_t n) const {
            assert(n <= size());
            return { _begin, _begin + n };
        }
        
        [[nodiscard]] span<T> last(std::size_t n) const {
            assert(n <= size());
            return { _end - n, _end };
        }
                
        [[nodiscard]] std::pair<span<T>, span<T>> partition(std::size_t n) const {
            assert(n <= size());
            return split(_begin + n);
        }
        
        [[nodiscard]] span<T> between(std::size_t i, std::size_t j) const {
            assert((i <= j) && (j <= size()));
            return { _begin + i, _begin + j };
        }

        [[nodiscard]] span<T> subspan(std::size_t i, std::size_t n) const {
            return between(i, i + n);
        }

        // mutating
        
        T& pop_front() { assert(!empty()); return *_begin++; };
        T& pop_back() { assert(!empty()); return *--_end; };
        
        span<T> drop_before(T* p) {
            assert((_begin <= p) && (p <= _end));
            return {std::exchange(_begin, p), p};
        }

        span<T> drop_after(T* p) {
            assert((_begin <= p) && (p <= _end));
            return {p, std::exchange(_end, p)};
        }
        
        span<T> drop_front(std::size_t n) {
            assert(n <= size());
            return drop_before(_begin + n);
        }
        
        span<T> drop_back(std::size_t n) {
            assert(n <= size());
            return drop_after(_end - n);
        }
        
        // unsafe mutating
        
        void unpop_front() {
            --_begin;
        }
        
        void unpop_back() {
            --_end;
        }


    };
    
    struct Reader {
        
        span<byte const> s;
        
        void skip(std::size_t n) {
            s.drop_front(n);
        }
        
        template<typename T>
        void read(T& x) {
            std::memcpy(&x, s.data(), sizeof(T));
            s.drop_front(sizeof(T));
            if constexpr (std::is_integral_v<T>) {
                x = ntohg(x);
            }
        }
        
        template<typename T, typename T2, typename... Ts>
        void read(T& x, T2& x2, Ts&... xs) {
            read(x);
            read(x2, xs...);
        }

        template<typename T>
        T read() {
            T x{};
            read(x);
            return x;
        }
                
    };
    
} // namespace wry


namespace CompactFontFormat {
    
    struct Header {
        uint8_t major;
        uint8_t minor;
        uint8_t hdrSize;
        uint8_t offSize;
        
        static Header from(wry::Reader& r) {
            auto r2 = r;
            Header a{};
            r2.read(a.major, a.minor, a.hdrSize, a.offSize);
            printf("CompactFontFormat::Header { %d %d %d %d }\n", a.major, a.minor, a.hdrSize, a.offSize);
            r.skip(a.hdrSize);
            return a;
        }
        
    };
        
    struct INDEX {
        
        uint16_t count;
        uint8_t offSize;
        byte const* base;
        std::vector<uint32_t> offsets;
        
        static INDEX from(wry::Reader& s) {
            INDEX a{};
            s.read(a.count);
            if (a.count) {
                s.read(a.offSize);
                a.offsets.resize(a.count + 1);
                switch (a.offSize) {
                    case 1:
                        for (int i = 0; i != a.count + 1; ++i) {
                            a.offsets[i] = s.read<uint8_t>();
                        }
                        break;
                    case 2:
                        for (int i = 0; i != a.count + 1; ++i) {
                            a.offsets[i] = s.read<uint16_t>();
                        }
                        break;
                    case 4:
                        for (int i = 0; i != a.count + 1; ++i) {
                            a.offsets[i] = s.read<uint32_t>();
                        }
                        break;
                    default:
                        abort();
                }
                a.base = s.s.data() - 1;
                s.skip(a.offsets.back() - 1);
            }
            return a;
        }
        
        wry::span<const byte> operator[](int i) const {
            return {base + offsets[i], base + offsets[i + 1]};
        }
                
    };
    
    
    struct DICT {
        
        std::map<std::array<uint8_t, 2>, std::vector<double>> dictionary;

        // CFF encodes reals as a decimal string of nibbles
        static bool parse_real(wry::Reader& r, double& victim) {
            const size_t N = 32;
            char buffer[N] = {};
            char* dst = buffer;
            char* guard = dst + N - 3;
            constexpr static char const* table[] = {
                "0", "1", "2", "3", "4", "5", "6", "7", "8", "9",
                ".", "E", "-E", "_", "-", "_"
            };
            
            for (;;) {
                byte b0 = r.read<byte>();
                int nibble = {};
                nibble = b0 >> 4;
                // s.append(table[nibble]);
                assert(dst < guard);
                if (dst < guard)
                    dst = stpcpy(dst, table[nibble]);
                nibble = b0 & 0xf;
                assert(dst < guard);
                if (dst < guard)
                    dst = stpcpy(dst, table[nibble]);
                if (nibble == 0xf)
                    break;
            }
            std::from_chars(buffer, dst, victim);
            return true;
        }
        
        static DICT from(std::span<const byte> s) {
            DICT a{};
            std::array<uint8_t, 2> key;
            std::vector<double> value;
            const byte* first = s.data();
            const byte* last = s.data() + s.size();
            
            while (first < last) {
                byte b0 = *first++;
                if (b0 <= 21) {
                    // operator
                    key[0] = b0;
                    if (b0 == 12) {
                        byte b1 = *first++;
                        key[1] = b1;
                    }
                    // printf("key = %d %d\n", key[0], key[1]);
                    a.dictionary.emplace(key, std::move(value));
                    key[0] = 0; key[1] = 0;
                    value.clear();
                } else if (b0 <= 27) {
                    abort(); // reserved
                } else if (b0 <= 28) {
                    uint16_t x{};
                    memcpy(&x, first, 2);
                    first += 2;
                    value.push_back(ntohs(x));
                } else if (b0 <= 29) {
                    uint32_t x{};
                    memcpy(&x, first, 4);
                    first += 4;
                    value.push_back(ntohl(x));
                } else if (b0 <= 30) {
                    double x{};
                    wry::Reader r{{first, last}};
                    parse_real(r, x);
                    first = r.s.begin();
                    value.push_back(x);
                } else if (b0 <= 31) {
                    abort(); // reserved
                } else if (b0 <= 246) {
                    value.push_back((int)b0 - 139);
                } else if (b0 <= 250) {
                    byte b1 = *first++;
                    value.push_back((((int)b0 - 247) << 8) + (int)b1 + 108);
                } else if (b0 <= 254) {
                    byte b1 = *first++;
                    value.push_back((-((int)b0 - 247) << 8) - (int)b1 - 108);
                } else if (b0 <= 255) {
                    abort(); // reserved
                }
                //if (!value.empty())
                    //printf("values.append(%g),\n", value.back());
            }
            assert(value.empty()); // we completed a (key, value)
            assert(first == last); // we consumed the right number of bytes
            return a;
        }
        
        std::span<double const> operator[](int i, int j = 0) const {
            auto it = dictionary.find(std::array<uint8_t, 2>{(uint8_t)i, (uint8_t)j});
            if (it != dictionary.end()) {
                return {it->second.data(), it->second.size()};
            } else {
                return {};
            }
            
        }
        
    };
    
    
    
    struct Type2CharstringEngine {
        
        INDEX global_subroutines;
        INDEX local_subroutines;
        
        std::deque<double> stack;
        std::stack<wry::span<byte const>> cs;
        bool is_first_stack_clearing_operator = true;
        double width = 0.0;
        simd_double2 point = {};
        uint8_t mode = {};
        
        enum {
            MOVE,
            LINE,
            BEZIER,
        };
        
        std::vector<double> hstem;
        std::vector<double> vstem;
        
        std::vector<simd_double2> points;
        std::vector<uint8_t> modes;
        
        bool execute(std::span<byte const> str);
        
        void maybe_width() {
            if (is_first_stack_clearing_operator) {
                is_first_stack_clearing_operator = false;
                if (!stack.empty()) {
                    printf("(: width) ");
                    width = stack.front();
                    stack.pop_front();
                }
            }
        }
        
        void maybe_width_if_odd() {
            if (stack.size() & 1)
                maybe_width();
        }
        
        void maybe_width_if_even() {
            if (!(stack.size() & 1))
                maybe_width();
        }
        
        void push() {
            points.push_back(point);
            modes.push_back(mode);
        }
        
        void dx() {
            point.x += stack.front(); stack.pop_front();
            push();
        }
        
        void dy() {
            point.y += stack.front(); stack.pop_front();
            push();
        }
        
        void dxy() {
            point.x += stack.front(); stack.pop_front();
            point.y += stack.front(); stack.pop_front();
            push();
        }
        
        void do_hstem() {
            while (!stack.empty()) {
                assert(stack.size() >= 2);
                hstem.push_back(stack.front()); stack.pop_front();
                hstem.push_back(stack.front()); stack.pop_front();
            }
        }
        
        void do_vstem() {
            while (!stack.empty()) {
                assert(stack.size() >= 2);
                vstem.push_back(stack.front()); stack.pop_front();
                vstem.push_back(stack.front()); stack.pop_front();
            }
        }
        
        void do_mask(wry::span<byte const>& str) {
            int number_of_bytes = (int)((hstem.size() + vstem.size() + 14) / 16);
            while (number_of_bytes--) {
                printf(" %#02hhx", str.pop_front());
            }
            printf("\n");
        }
        
        
    };
    
    
    bool Type2CharstringEngine::execute(std::span<byte const> str) {
        wry::Reader r{{ str.data(), str.size()}};
        while (!str.empty()) {
            uint8_t b0 = r.read<uint8_t>();
            // printf("<%d>", b0);
            if ((b0 <= 31) && (b0 != 28)) {
                switch (b0) {
                    case 1: {
                        maybe_width_if_odd();
                        printf(": hstem\n");
                        do_hstem();
                        assert(stack.empty());
                        break;
                    }
                    case 3: {
                        maybe_width_if_odd();
                        printf(": vstem\n");
                        do_vstem();
                        assert(stack.empty());
                        break;
                    }
                    case 4: {
                        maybe_width_if_even();
                        printf(": vmoveto\n");
                        mode = MOVE;
                        dy();
                        assert(stack.empty());
                        break;
                    }
                    case 5: {
                        printf(": rlineto\n");
                        mode = LINE;
                        do  {
                            dxy();
                        } while ((!stack.empty()));
                        break;
                    }
                    case 6: {
                        printf(": hlineto\n");
                        int parity = 0;
                        mode = LINE;
                        do {
                            if (parity)
                                point.y += stack.front();
                            else
                                point.x += stack.front();
                            stack.pop_front();
                            push();
                            parity ^= 1;
                        } while (!stack.empty());
                        break;
                    }
                    case 7: {
                        printf(": vlineto\n");
                        int parity = 1;
                        mode = LINE;
                        do {
                            if (parity)
                                point.y += stack.front();
                            else
                                point.x += stack.front();
                            stack.pop_front();
                            push();
                            parity ^= 1;
                        } while (!stack.empty());
                        break;
                    }
                    case 8: {
                        printf(": rrcurveto\n");
                        mode = BEZIER;
                        do {
                            dxy(); dxy(); dxy();
                        } while (!stack.empty());
                        break;
                    }
                    case 10: { // callsubr
                        printf(": callsubr\n");
                        int i = (int)stack.back() + 107;
                        stack.pop_back();
                        cs.push(r.s);
                        r.s = local_subroutines[i];
                        break;
                    }
                    case 11: { // return
                        printf(": return\n");
                        assert(str.empty());
                        str = cs.top(); cs.pop();
                        break;
                    }
                    case 14: // endchar
                        maybe_width_if_odd();
                        printf(": endchar\n");
                        assert(stack.empty());
                        // print_result();
                        // render_result();
                        return true;
                    case 18: { // hstemhm
                        maybe_width_if_odd();
                        printf(": hstemhm\n");
                        do_hstem();
                        break;
                    }
                    case 19: { // hintmask
                        maybe_width_if_odd();
                        if (!stack.empty()) {
                            printf("(: vstem) ");
                            do_vstem();
                        }
                        printf(": hintmask");
                        do_mask(r.s);
                        assert(stack.empty());
                        break;
                    }
                    case 20: { // cntrmask
                        maybe_width_if_odd();
                        if (!stack.empty()) {
                            printf("(: vstem) ");
                            do_vstem();
                        }
                        printf(": cntrmask");
                        do_mask(r.s);
                        assert(stack.empty());
                        break;
                    }
                    case 21: {
                        maybe_width_if_odd();
                        printf(": rmoveto\n");
                        mode = MOVE;
                        dxy();
                        assert(stack.empty());
                        break;
                    }
                    case 22: {
                        maybe_width_if_even();
                        printf(": hmoveto\n");
                        mode = MOVE;
                        dx();
                        assert(stack.empty());
                        break;
                    }
                    case 23: {
                        maybe_width_if_odd();
                        printf(": vstemhm\n");
                        do_vstem();
                        assert(stack.empty());
                        break;
                    }
                    case 24: {
                        printf(": rcurveline\n");
                        mode = BEZIER;
                        do {
                            dxy(); dxy(); dxy();
                        } while (stack.size() >= 6);
                        mode = LINE;
                        dxy();
                        assert(stack.empty());
                        break;
                    }
                    case 25: {
                        printf(": rlinecurve\n");
                        mode = LINE;
                        do {
                            dxy();
                        } while (stack.size() > 6);
                        mode = BEZIER;
                        dxy(); dxy(), dxy();
                        break;
                    }
                    case 26: {
                        printf(": vvcurveto\n");
                        if (stack.size() & 1) {
                            point.x += stack.front(); stack.pop_front();
                        }
                        mode = BEZIER;
                        do {
                            dy(); dxy(); dy();
                        } while (!stack.empty());
                        break;
                    }
                    case 27: {
                        printf(": hhcurveto\n");
                        if (stack.size() & 1) {
                            point.y += stack.front(); stack.pop_front();
                        }
                        mode = BEZIER;
                        do {
                            dx(); dxy(); dx();
                        } while (!stack.empty());
                        assert(stack.empty());
                        break;
                    }
                    case 30: {
                        printf(": vhcurveto\n");
                        bool parity = 1;
                        mode = BEZIER;
                        do {
                            assert(stack.size() >= 4);
                            if (parity == 0) {
                                dx(); dxy();
                                point.y += stack.front(); stack.pop_front();
                                if (stack.size() == 1) {
                                    point.x += stack.front(); stack.pop_front();
                                }
                            } else {
                                dy(); dxy();
                                point.x += stack.front(); stack.pop_front();
                                if (stack.size() == 1) {
                                    point.y += stack.front(); stack.pop_front();
                                }
                            }
                            push();
                            parity = parity ^ 1;
                        } while (!stack.empty());
                        break;                    }
                    case 31: {
                        printf(": hvcurveto\n");
                        bool parity = 0;
                        mode = BEZIER;
                        do {
                            assert(stack.size() >= 4);
                            if (parity == 0) {
                                dx(); dxy();
                                point.y += stack.front(); stack.pop_front();
                                if (stack.size() == 1) {
                                    point.x += stack.front(); stack.pop_front();
                                }
                            } else {
                                dy(); dxy();
                                point.x += stack.front(); stack.pop_front();
                                if (stack.size() == 1) {
                                    point.y += stack.front(); stack.pop_front();
                                }
                            }
                            push();
                            parity = parity ^ 1;
                        } while (!stack.empty());
                        break;
                    }
                    default:
                        printf(": Unhandled b0 = %d\n", b0);
                        abort();
                        break;
                }
            } else {
                // Is an operand (a number)
                double number{};
                if (b0 == 28) {
                    number = r.read<std::int16_t>();
                } else if (b0 <= 246) {
                    number = b0 - 139;
                } else if (b0 <= 250) {
                    number = (b0 - 247) * 256 + 108 + r.read<std::uint8_t>();
                } else if (b0 <= 254) {
                    number = -(b0 - 251) * 256 - 108 - r.read<std::uint8_t>();
                } else { assert(b0 == 255);
                    number = r.read<std::int32_t>() * 0x1p-16;
                }
                printf("%g ",number);
                stack.push_back(number);
            }
        }
        if (!stack.empty()) {
            printf("Missing operator??\n");
        }
        printf("Missing endchar??\n");
        abort();
    };
        
    
    
    
} // namespace CompactFontFormat

namespace OpenType {
    
    struct TableDirectory {
        
        uint32_t sfntVersion;
        uint16_t numTables;
        uint16_t searchRange;
        uint16_t entrySelector;
        uint16_t rangeShift;
        
        byte const* first;
        
        struct TableRecord {
            
            std::array<uint8_t, 4> tableTag;
            uint32_t checksum;
            uint32_t offset;
            uint32_t length;
            
            static TableRecord from(wry::Reader& r) {
                TableRecord c{};
                r.read(c.tableTag, c.checksum, c.offset, c.length);
                printf("    \"%.4s\" %x %d %d\n", (char const*)&c.tableTag, c.checksum, c.offset, c.length);
                return c;
            }
            
        };
        
        std::vector<TableRecord> tableRecords;
        
        static TableDirectory from(wry::span<byte const> s) {
            wry::Reader r{s};
            TableDirectory a{};
            r.read(a.sfntVersion,
                   a.numTables,
                   a.searchRange,
                   a.entrySelector,
                   a.rangeShift);
            assert((a.sfntVersion == 0x00010000) || (a.sfntVersion == 0x4F54544F));
            assert(a.numTables >= 9);
            printf("%u %d %x %x %d\n", a.sfntVersion, a.numTables, a.searchRange, a.entrySelector, a.rangeShift);
            a.first = s.data();
            for (int i = 0; i != a.numTables; ++i) {
                auto c = TableRecord::from(r);
                assert(c.offset + c.length <= s.size());
                a.tableRecords.push_back(c);
            }
            return a;
        }
        
        wry::span<byte const> operator[](const char* key) const {
            for (auto & a : tableRecords) {
                if (std::memcmp(key, &a.tableTag, 4) == 0)
                    return {first + a.offset, a.length};
            }
            return {};
        }
        
    };
    
    struct cmapHeader {
        
        uint16_t version;
        uint16_t numTables;
        
        struct EncodingRecord {
            
            uint16_t platformID;
            uint16_t encodingID;
            uint32_t subtableOffset;
            
            static EncodingRecord from(wry::Reader& r) {
                EncodingRecord a;
                r.read(a.platformID, a.encodingID, a.subtableOffset);
                return a;
            }
            
        };
        
        std::vector<EncodingRecord> encodingRecords;
        
        struct cmapSubtableFormat4 {
            
            size_t segCount;
            wry::span<byte const> tail;
            
            enum { MISSING_GLYPH = 0xFFFF };
            
            size_t lookup(size_t code) {
                uint16_t const* cursor = (uint16_t const*) tail.data();
                // TODO: Use Reader
                
                // look for first segment with end >= code
                while (ntohs(*cursor++) < code)
                    ;
                // padding word is accounted for by postincrement
                cursor += segCount;
                uint16_t startCode = ntohs(*cursor);
                if (startCode > code) {
                    // segment excludes the code; thus it is not present
                    return MISSING_GLYPH;
                }
                cursor += segCount;
                uint16_t idDelta = ntohs(*cursor);
                cursor += segCount;
                uint16_t idRangeOffset = ntohs(*cursor);
                if (idRangeOffset != 0) {
                    cursor += (idRangeOffset >> 1) + (code - startCode);
                    code = ntohs(*cursor);
                    if (code == 0)
                        return MISSING_GLYPH;
                }
                return (idDelta + code) & 0xFFFF;
            }
            
            explicit cmapSubtableFormat4(wry::span<byte const> s) {
                wry::Reader r{s};
                
                uint16_t format;
                uint16_t length;
                uint16_t language;
                uint16_t segCountX2;
                uint16_t searchRange;
                uint16_t entrySelector;
                uint16_t rangeShift;
                
                r.read(format,
                       length,
                       language,
                       segCountX2,
                       searchRange,
                       entrySelector,
                       rangeShift);
                assert(format == 4);
                assert(length = s.size());
                assert(language == 0);
                
                segCount = segCountX2 >> 1;
                tail = r.s;
            }
            
        };
        
        
        static cmapHeader from(wry::span<byte const> s) {
            wry::Reader r{s};
            cmapHeader a{};
            r.read(a.version, a.numTables);
            for (int i = 0; i != a.numTables; ++i) {
                a.encodingRecords.push_back(EncodingRecord::from(r));
            }
            return a;
        }
        
    };
    
    
    template<typename T>
    char const* formatString = "%%?";
    
    template<> char const* formatString<int8_t> = "%hhd";
    template<> char const* formatString<uint8_t> = "%hhu";
    template<> char const* formatString<int16_t> = "%hd";
    template<> char const* formatString<uint16_t> = "%hu";
    template<> char const* formatString<int32_t> = "%d";
    template<> char const* formatString<uint32_t> = "%u";
    template<> char const* formatString<std::array<uint8_t, 4>> = "%x";

    struct FontHeaderTable {
        
#define Y \
        X(uint16_t, majorVersion)\
        X(uint16_t, minorVersion)\
        X(uint32_t, fontRevision)\
        X(uint32_t, checksumAdjustment)\
        X(uint32_t, magicNumber)\
        X(uint16_t, flags)\
        X(uint16_t, unitsPerEm)\
        X(int64_t, created)\
        X(int64_t, modified)\
        X(int16_t, xMin)\
        X(int16_t, yMin)\
        X(int16_t, xMax)\
        X(int16_t, yMax)\
        X(uint16_t, macStyle)\
        X(uint16_t, lowestRecPPEM)\
        X(int16_t, fontDirectionHint)\
        X(int16_t, indexToLocFormat)\
        X(int16_t, glyphDataFormat)\
        
        
#define X(A, B) A B;
        Y
#undef X
        
        explicit FontHeaderTable(wry::span<byte const> s) {
            wry::Reader r{s};
#define X(A, B) r.read(B);
            Y
#undef X
            assert(majorVersion == 1);
            assert(minorVersion == 0);
            assert(magicNumber == 0x5F0F3CF5);
        }
        
        void debug() {
            constexpr std::size_t buf_size = 100;
            char buffer[buf_size];
            printf("{\n");
#define X(A, B) snprintf(buffer, buf_size, "    \"%%s\" : %s,\n", formatString<A>); printf(buffer, #B, B);
            Y
#undef X
            printf("}\n");
        }
        
#undef Y
        
    };


    struct HorizontalHeaderTable {
        
#define Y \
X(uint16_t, majorVersion)\
X(uint16_t, minorVersion)\
X(int16_t, ascender)\
X(int16_t, descender)\
X(int16_t, lineGap)\
X(uint16_t, advanceWidthMax)\
X(int16_t, minLeftSideBearing)\
X(int16_t, minRightSideBearing)\
X(int16_t, xMaxExtent)\
X(int16_t, caretSlopeRise)\
X(int16_t, caretSlopeRun)\
X(int16_t, caretSlopeOffset)\
X(int16_t, reserved0)\
X(int16_t, reserved1)\
X(int16_t, reserved2)\
X(int16_t, reserved3)\
X(int16_t, metricDataFormat)\
X(uint16_t, numberOfHMetrics)\

#define X(A, B) A B;
        Y
#undef X
        
        explicit HorizontalHeaderTable(wry::span<byte const> s) {
            wry::Reader r{s};
#define X(A, B) r.read(B);
            Y
#undef X
        }
        
        void debug() {
            constexpr std::size_t buf_size = 100;
            char buffer[buf_size];
            printf("{\n");
#define X(A, B) snprintf(buffer, buf_size, "    \"%%s\" : %s,\n", formatString<A>); printf(buffer, #B, B);
            Y
#undef X
            printf("}\n");
        }

#undef Y
        
    };
    
    
    struct HorizontalMetricsTable {
        
        uint16_t numGlyphs;
        uint16_t numberOfHMetrics;
        
        struct LongHorMetricRecord {
            uint16_t advanceWidth;
            int16_t lsb;
        };
        
        LongHorMetricRecord const* hMetrics;
        
        explicit HorizontalMetricsTable(uint16_t numGlyphs,
                                        uint16_t numberOfHMetrics,
                                        wry::span<byte const> s) {
            hMetrics = (LongHorMetricRecord const*)s.data();
            
        }
        
        LongHorMetricRecord lookup(uint16_t glyphID) {
            LongHorMetricRecord a{};
            if (glyphID < numberOfHMetrics) {
                a = hMetrics[glyphID];
            } else {
                int16_t const* leftSideBearings = (int16_t const*)(hMetrics + numberOfHMetrics);
                a.advanceWidth = hMetrics[numberOfHMetrics - 1].advanceWidth;
                a.lsb = leftSideBearings[glyphID - numberOfHMetrics];
            }
            return { ntohg(a.advanceWidth), ntohg(a.lsb) };
        }

        
    };
    
    struct MaximumProfileTable {
        
#define Y \
X(uint32_t, version)\
X(uint16_t, numGlyphs)\

#define Z \
X(uint16_t, maxPoints)\
X(uint16_t, maxContours)\
X(uint16_t, maxCompositePoints)\
X(uint16_t, maxCompositeContours)\
X(uint16_t, maxZones)\
X(uint16_t, maxTwilightPoints)\
X(uint16_t, maxStorage)\
X(uint16_t, maxFunctionDefs)\
X(uint16_t, maxInstructionDefs)\
X(uint16_t, maxStackElements)\
X(uint16_t, maxSizeOfInstructions)\
X(uint16_t, maxComponentElements)\
X(uint16_t, maxComponentDepth)\

#define X(A, B) A B;
        Y
        Z
#undef X
        
        explicit MaximumProfileTable(wry::span<byte const> s) {
            wry::Reader r{s};
#define X(A, B) r.read(B);
            Y
            if (version == 0x00010000) {
                Z
            }
#undef X
        }
        
        void debug() {
            constexpr std::size_t buf_size = 100;
            char buffer[buf_size];
            printf("{\n");
#define X(A, B) snprintf(buffer, buf_size, "    \"%%s\" : %s,\n", formatString<A>); printf(buffer, #B, B);
            Y
            if (version == 0x00010000) {
                Z
            }
#undef X
            printf("}\n");
        }
#undef Z
#undef Y
                
    };
    
    
    struct OS_2andWindowsMetricsTable {
        
        using uint8x10_t = std::array<uint8_t, 10>;
        using Tag = std::array<uint8_t, 4>;
                
#define Y \
X(uint16_t, version)\
X(int16_t, xAvgCharWidth)\
X(uint16_t, usWeightClass)\
X(uint16_t, usWidthClass)\
X(uint16_t, fsType)\
X(int16_t, ySubscriptXSize)\
X(int16_t, ySubscriptYSize)\
X(int16_t, ySubscriptXOffset)\
X(int16_t, ySubscriptYOffset)\
X(int16_t, ySuperscriptXSize)\
X(int16_t, ySuperscriptYSize)\
X(int16_t, ySuperscriptXOffset)\
X(int16_t, ySuperscriptYOffset)\
X(int16_t, yStrikeoutSize)\
X(int16_t, yStrikeoutPosition)\
X(int16_t, sFamilyClass)\
X(uint8_t, panose0)\
X(uint8_t, panose1)\
X(uint8_t, panose2)\
X(uint8_t, panose3)\
X(uint8_t, panose4)\
X(uint8_t, panose5)\
X(uint8_t, panose6)\
X(uint8_t, panose7)\
X(uint8_t, panose8)\
X(uint8_t, panose9)\
X(uint32_t, ulUnicodeRange1)\
X(uint32_t, ulUnicodeRange2)\
X(uint32_t, ulUnicodeRange3)\
X(uint32_t, ulUnicodeRange4)\
X(Tag, achVendID)\
X(uint16_t, fsSelection)\
X(uint16_t, usFirstCharIndex)\
X(uint16_t, usLastCharIndex)\
X(int16_t, sTypoAscender)\
X(int16_t, sTypoDescender)\
X(int16_t, sTypoLineGap)\
X(int16_t, usWinAscent)\
X(int16_t, usWinDescent)\
X(uint32_t, ulCodePageRange1)\
X(uint32_t, ulCodePageRange2)\
X(int16_t, sxHeight)\
X(int16_t, sCapHeight)\
X(uint16_t, usDefaultChar)\
X(uint16_t, usBreakChar)\
X(uint16_t, usMaxContext)\

#define Z \
X(uint16_t, usLowerOpticalPointSize)\
X(uint16_t, usUpperOpticalPointSize)\

#define X(A, B) A B;
        Y
        Z
#undef X
        
        explicit OS_2andWindowsMetricsTable(wry::span<byte const> s) {
            wry::Reader r{s};
#define X(A, B) r.read(B);
            Y
            if (version == 0x0005) {
                Z
            }
#undef X
        }
        void debug() {
            constexpr std::size_t buf_size = 100;
            char buffer[buf_size];
            printf("{\n");
#define X(A, B) snprintf(buffer, buf_size, "    \"%%s\" : %s,\n", formatString<A>); printf(buffer, #B, B);
            Y
            if (version == 0x0005) {
                Z
            }
#undef X
            printf("}\n");
        }
#undef Z
#undef Y

    };
    
    
    
    struct GlyphData {
        
        struct GlyphHeader {
            
            int16_t numberOfContours;
            int16_t xMin;
            int16_t yMin;
            int16_t xMax;
            int16_t yMax;
            
            std::vector<uint16_t> endPtsOfContours;
            uint16_t instructionLength;
            std::vector<uint8_t> instructions;
            std::vector<uint8_t> flags;
            std::vector<simd_short2> points;
            
            enum {
                ON_CURVE_POINT = 0x01,
                X_SHORT_VECTOR = 0x02,
                Y_SHORT_VECTOR = 0x04,
                REPEAT_FLAG    = 0x08,
                X_IS_SAME_OR_POSITIVE_X_SHORT_VECTOR = 0x10,
                Y_IS_SAME_OR_POSITIVE_Y_SHORT_VECTOR = 0x20,
            };
            
            GlyphHeader(wry::Reader r) {
                r.read(numberOfContours, xMin, yMin, xMax, yMax);
                assert(numberOfContours);
                assert(xMin <= xMax);
                assert(yMin <= yMax);
                for (int i = 0; i != numberOfContours; ++i)
                    endPtsOfContours.push_back(r.read<uint16_t>());
                r.read(instructionLength);
                for (int i = 0; i != instructionLength; ++i)
                    instructions.push_back(r.read<uint8_t>());
                ptrdiff_t x_bytes = 0;
                size_t sentinel = endPtsOfContours.back();
                
                // We need to parse the flags enough to find x and y
                
                wry::Reader rf{r};
                for (; sentinel;) {
                    uint8_t f{};
                    rf.read(f);
                    
                    // This could be a table?
                    // 0x2 | (0x1 << 0x2) | (1 << (0x2 | 0x10)
                    // x_bytes += (0x0406 >> (f & 0x12)) & 0x3
                    
                    switch (f & (X_SHORT_VECTOR | X_IS_SAME_OR_POSITIVE_X_SHORT_VECTOR)) {
                        case 0:
                            assert(((0x0406 >> (f & 0x12)) & 0x3) == 2);
                            x_bytes += 2;
                            break;
                        case X_SHORT_VECTOR:
                            assert(((0x0406 >> (f & 0x12)) & 0x3) == 1);
                            x_bytes += 1;
                            break;
                        case X_IS_SAME_OR_POSITIVE_X_SHORT_VECTOR:
                            assert(((0x0406 >> (f & 0x12)) & 0x3) == 0);
                            x_bytes += 0;
                            break;
                        case (X_SHORT_VECTOR | X_IS_SAME_OR_POSITIVE_X_SHORT_VECTOR):
                            assert(((0x0406 >> (f & 0x12)) & 0x3) == 1);
                            x_bytes += 1;
                            break;
                    }
                    if (f & REPEAT_FLAG)
                        sentinel -= rf.read<uint8_t>();
                }
                
                // Set up the readers for the next pass
                auto [sx, sy] = rf.s.partition(x_bytes);
                wry::Reader rx{sx}, ry{sy};
                rf.s = r.s.before(rf.s.begin());
                
                simd_short2 pen{};
                for (; !rf.s.empty();) {
                    uint8_t f = rf.read<uint8_t>();
                    uint8_t n = 1;
                    if (f & REPEAT_FLAG) {
                        n = rf.read<uint8_t>();
                    }
                    while (n--) {
                        switch (f & (X_SHORT_VECTOR | X_IS_SAME_OR_POSITIVE_X_SHORT_VECTOR)) {
                            case 0:
                                pen.x += rx.read<int16_t>();
                                break;
                            case X_SHORT_VECTOR:
                                pen.x -=(int)rx.read<uint8_t>();
                                break;
                            case X_IS_SAME_OR_POSITIVE_X_SHORT_VECTOR:
                                break;
                            case (X_SHORT_VECTOR | X_IS_SAME_OR_POSITIVE_X_SHORT_VECTOR):
                                pen.x += (int)rx.read<uint8_t>();
                                break;
                        }
                        switch (f & (Y_SHORT_VECTOR | Y_IS_SAME_OR_POSITIVE_Y_SHORT_VECTOR)) {
                            case 0:
                                pen.y = ry.read<int16_t>();
                                break;
                            case Y_SHORT_VECTOR:
                                pen.y = -(int)ry.read<uint8_t>();
                                break;
                            case Y_IS_SAME_OR_POSITIVE_Y_SHORT_VECTOR:
                                break;
                            case (Y_SHORT_VECTOR | Y_IS_SAME_OR_POSITIVE_Y_SHORT_VECTOR):
                                pen.y = (int)ry.read<uint8_t>();
                                break;
                        }
                        points.push_back(pen);
                    }
                }
                
                
                                
                
                
                
                
                

            }
            
        };
        
        
        
        bool parse_glyfHeader(u8 const*& first, u8 const* last) {
            u8 const* base = first;
            printf("cmap\n");
#define X(S) int16_t S{}; parse_network_int16(first, last, S); printf(#S " %d\n", S);
            X(numberOfContours);
            X(xMin);
            X(yMin);
            X(xMax);
            X(yMax);
            
#define U(S) uint16_t S{}; parse_network_uint16(first, last, S); printf(#S " %d\n", S);
            
            std::vector<int> epoc;
            for (int i = 0; i != numberOfContours; ++i) {
                U(endPtsOfContours);
                epoc.push_back(endPtsOfContours);
            }
            U(instructionLength);
            first += instructionLength;
            std::vector<u8> flags;
            enum {
                ON_CURVE_POINT = 0x01,
                X_SHORT_VECTOR = 0x02,
                Y_SHORT_VECTOR = 0x04,
                REPEAT_FLAG    = 0x08,
                X_IS_SAME_OR_POSITIVE_X_SHORT_VECTOR = 0x10,
                Y_IS_SAME_OR_POSITIVE_Y_SHORT_VECTOR = 0x20,
            };
            
            int xbytes = 0;
            for (; flags.size() != epoc.back() + 1;) {
                // Unpack bytes
                u8 a = *first++;
                switch (a & (X_SHORT_VECTOR | X_IS_SAME_OR_POSITIVE_X_SHORT_VECTOR)) {
                    case 0: xbytes += 2; break;
                    case X_SHORT_VECTOR: xbytes += 1; break;
                    case X_IS_SAME_OR_POSITIVE_X_SHORT_VECTOR: xbytes += 0; break;
                    case X_SHORT_VECTOR | X_IS_SAME_OR_POSITIVE_X_SHORT_VECTOR: xbytes +=1; break;
                }
                
                if (a & REPEAT_FLAG) {
                    u8 b = *first++;
                    printf("    repeating %02hhx %d times\n", a, b);
                    while (b--) {
                        flags.push_back(a);
                    }
                } else {
                    printf("    %#02hhx\n", a);
                    flags.push_back(a);
                }
            }
            
            // first now points to the xarray
            auto firsty = first + xbytes;
            Point current{};
            std::vector<Point> points;
            std::vector<bool> on_curve_points;
            for (auto f : flags) {
                switch (f & (X_SHORT_VECTOR | X_IS_SAME_OR_POSITIVE_X_SHORT_VECTOR)) {
                    case 0: {
                        int16_t dx{};
                        parse_network_int16(first, last, dx);
                        printf("dx is %d\n", dx);
                        current.x += dx;
                        break;
                    }
                    case X_SHORT_VECTOR:
                        printf("dx is -%d\n", *first);
                        current.x -= *first++;
                        break;
                    case X_IS_SAME_OR_POSITIVE_X_SHORT_VECTOR:
                        printf("dx is 0\n");
                        break;
                    case X_SHORT_VECTOR | X_IS_SAME_OR_POSITIVE_X_SHORT_VECTOR:
                        printf("dx is +%d\n", *first);
                        current.x += *first++;
                        break;
                }
                switch (f & (Y_SHORT_VECTOR | Y_IS_SAME_OR_POSITIVE_Y_SHORT_VECTOR)) {
                    case 0: {
                        int16_t dy{};
                        parse_network_int16(firsty, last, dy);
                        printf("dy is %d\n", dy);
                        current.y += dy;
                        break;
                    }
                    case Y_SHORT_VECTOR:
                        printf("dy is -%d\n", *firsty);
                        current.y -= *firsty++;
                        break;
                    case Y_IS_SAME_OR_POSITIVE_Y_SHORT_VECTOR:
                        printf("dy is 0\n");
                        break;
                    case Y_SHORT_VECTOR | Y_IS_SAME_OR_POSITIVE_Y_SHORT_VECTOR:
                        printf("dy is +%d\n", *firsty);
                        current.y += *firsty++;
                        break;
                }
                printf("%d, %g, %g\n", f & ON_CURVE_POINT, current.x, current.y);
                on_curve_points.push_back(f & ON_CURVE_POINT);
                points.push_back(current);
            }
            
            FILE* fd = fopen("/Users/antony/Desktop/dump.csv", "w");
            for (int i = 0; i != points.size(); ++i) {
                if (on_curve_points[i]) {
                    Point a = points[i];
                    fprintf(fd, "%g, %g\n", a.x, a.y);
                } else {
                    Point b = points[i];
                    Point a = b;
                    Point c = b;
                    if (i > 0) {
                        a = points[i - 1];
                        if (!on_curve_points[i - 1])
                            a = lerp(a, b, 0.5);
                    }
                    if (i + 1 < points.size()) {
                        c = points[i + 1];
                        if (!on_curve_points[i + 1])
                            c = lerp(c, b, 0.5);
                    }
                    for (double t = 0.5; t <= 1.0; t += 0.5) {
                        Point ab = lerp(a, b, t);
                        Point bc = lerp(b, c, t);
                        Point abc = lerp(ab, bc, t);
                        fprintf(fd, "%g, %g\n", abc.x, abc.y);
                    }
                }
            }
            
            
            
            exit(EXIT_SUCCESS);
            
            
            
            
            
            
            
            
            
            
            return true;
        }
        
    };

    
    
} // namespace OpenType















using u8 = unsigned char;

using int8 = int8_t;
using int16 = int16_t;
using int32 = int32_t;
using uint8 = uint8_t;
using uint16 = uint16_t;
using uint32 = uint32_t;

template<typename T>
struct Span {
    
    T* _begin;
    T* _end;
    
    Span() : _begin{}, _end{} {};
    Span(T* p, T* q) : _begin{p}, _end{q} {};
    Span(T* p, size_t n) : Span(p, p + n) {};
    
    Span(Span const&) =  default;
    ~Span() = default;
    Span& operator=(Span const&) =  default;
    
    bool empty() const { return _begin == _end; }
    size_t size() const { return _end - _begin; }
    T* data() const { return _begin; }
    T& front() const { assert(!empty()); return *_begin; }
    T& back() const { assert(!empty()); return *(_end - 1); }
    T& pop_front() { assert(!empty()); return *_begin++; }
    T& pop_back() { assert(!empty()); return *--_end; }
    void drop_front(size_t n) { assert(size() >= n); _begin += n; }
    void drop_back(size_t n) { assert(size() >= n); _end -= n; }
    
    T* begin() const { return _begin; }
    T* end() const { return _end; }
    
};




template<std::integral T>
bool parse(byte const*& first, byte const* last, T& victim) {
    if (last - first < sizeof(T))
        return false;
    T a{};
    std::memcpy(&a, first, sizeof(T));
    first += sizeof(T);
    victim = ntohg(a);
    return true;
}


template<size_t N>
bool parse(byte const*& first, byte const* last, std::array<uint8_t, N>& victim) {
    if (last - first < N)
        return false;
    std::memcpy(&victim, first, N);
    first += N;
    return true;
}

bool parse_uint8(u8 const*& first, u8 const* last, uint8& victim) {
    if (first == last)
        return false;
    memcpy(&victim, first, 1);
    first++;
    return true;
}

bool parse_network_uint16(u8 const*& first, u8 const* last, uint16& victim) {
    if (last - first < 2)
        return false;
    uint16_t a = {};
    memcpy(&a, first, 2);
    victim = ntohs(a);
    first += 2;
    return true;
}

bool parse_network_uint32(u8 const*& first, u8 const* last, uint32& victim) {
    if (last - first < 4)
        return false;
    uint32_t a = {};
    memcpy(&a, first, 4);
    victim = ntohl(a);
    first += 4;
    return true;
}

bool parse_network_int16(u8 const*& first, u8 const* last, int16& victim) {
    if (last - first < 2)
        return false;
    int16_t a = {};
    memcpy(&a, first, 2);
    victim = ntohs(a);
    first += 2;
    return true;
}

bool parse_network_int32(u8 const*& first, u8 const* last, int32& victim) {
    if (last - first < 4)
        return false;
    int32_t a = {};
    memcpy(&a, first, 4);
    victim = ntohl(a);
    first += 4;
    return true;
}

bool parse_network_int64(u8 const*& first, u8 const* last, int64_t& victim) {
    if (last - first < 4)
        return false;
    int64_t a = {};
    memcpy(&a, first, 8);
    victim = ntohll(a);
    first += 8;
    return true;
}





Span<const u8> mmap_path(char const* path) {
    int result{};
    result = open(path,
                  O_RDONLY);
    if (result == -1) {
        perror(__PRETTY_FUNCTION__);
        abort();
    }
    int fildes = result;
    struct stat buf = {};
    result = fstat(fildes, &buf);
    if (result == -1) {
        perror(__PRETTY_FUNCTION__);
        abort();
    }
    auto first = (u8 const*) mmap(nullptr, buf.st_size, PROT_READ,
                                  (MAP_FILE | MAP_PRIVATE), fildes, 0);
    if (first == MAP_FAILED) {
        perror(__PRETTY_FUNCTION__);
        abort();
    }
    
    close(result);
    return Span(first, buf.st_size);
}





using Tag = std::array<uint8, 4>;
using Offset32 = uint32;

bool parse_Tag(u8 const*& first, u8 const* last, Tag& victim) {
    if (last - first < 4)
        abort();
    memcpy(&victim, first, 4);
    first += 4;
    return true;
}

bool parse_Offset32(u8 const*& first, u8 const* last, Offset32& victim) {
    return parse_network_uint32(first, last, victim);
}



struct TableRecord {
    Tag      tableTag;
    uint32   checksum;
    Offset32 offset;
    uint32   length;
};

bool parse_TableRecord(u8 const*& first, u8 const* last, TableRecord& victim) {
    parse_Tag(first, last, victim.tableTag);
    parse_network_uint32(first, last, victim.checksum);
    parse_Offset32(first, last, victim.offset);
    parse_network_uint32(first, last, victim.length);
    return true;
}

void print_TableRecord(TableRecord const& x) {
    printf("\"%.4s\"\n", (char const*)&x.tableTag);
}


struct TableDirectory {
    uint32 sfntVersion;
    uint16 numTables;
    uint16 searchRange;
    uint16 entrySelector;
    uint16 rangeShift;
    std::map<Tag, TableRecord> tableRecords;
};

bool parse_TableDirectory(u8 const*& first, u8 const* last, TableDirectory& victim) {
    parse_network_uint32(first, last, victim.sfntVersion);
    switch (victim.sfntVersion) {
        case 0x00010000: //
            // TrueType
            break;
        case 0x4F54544F:
            // CFFx
            break;
        default:
            abort();
            break;
    }
    parse_network_uint16(first, last, victim.numTables);
    parse_network_uint16(first, last, victim.searchRange);
    assert(victim.searchRange == std::bit_floor(victim.numTables) * 16);
    parse_network_uint16(first, last, victim.entrySelector);
    assert(victim.entrySelector == std::__bit_log2(victim.numTables));
    parse_network_uint16(first, last, victim.rangeShift);
    for (int i = 0; i != victim.numTables; ++i) {
        TableRecord tableRecord = {};
        parse_TableRecord(first, last, tableRecord);
        victim.tableRecords.emplace(tableRecord.tableTag,
                                    std::move(tableRecord));
    }
    return true;
}

void print_TableDirectory(TableDirectory const& x) {
    printf("\"sfntVersion\" : %#08x\n", x.sfntVersion);
    printf("\"tableRecords\" : {\n");
    for (auto [k, v] : x.tableRecords) {
        print_TableRecord(v);
    }
    printf("}\n");
}




using Card8 = uint8;
using Card16 = uint16;
using OffSize = uint8;
using SID = uint16;

bool parse_Card8(u8 const*& first, u8 const* last, Card8& victim) {
    return parse_uint8(first, last, victim);
}

bool parse_Card16(u8 const*& first, u8 const* last, Card16& victim) {
    return parse_network_uint16(first, last, victim);
}

bool parse_OffSize(u8 const*& first, u8 const* last, OffSize& victim) {
    return parse_uint8(first, last, victim);
}

struct Header {
    Card8 major;
    Card8 minor;
    Card8 hdrSize;
    OffSize offSize;
};

bool parse_Header(u8 const*& first, u8 const* last, Header& victim) {
    return (parse_Card8(first, last, victim.major)
            && parse_Card8(first, last, victim.minor)
            && parse_Card8(first, last, victim.hdrSize)
            && parse_OffSize(first, last, victim.offSize));
}

struct INDEX {
    std::vector<u8 const*> data;
    struct iterator {
        u8 const* const* data;
        std::pair<u8 const*, u8 const*> const* deref() const {
            return (std::pair<u8 const*, u8 const*> const*) data;
        }
        auto operator*() const { return *deref(); }
        auto operator->() const { return deref(); }
        iterator& operator++() {
            ++data;
            return *this;
        }
        iterator operator++(int) {
            auto tmp = *this;
            ++*this;
            return tmp;
        }
        bool operator==(iterator const&) const = default;
    };
    auto size() const {
        return data.size() - 1;
    }
    iterator begin() const { return iterator{data.data()}; }
    iterator end() const { return iterator{data.data() + size()}; }
    std::pair<u8 const*, u8 const*> operator[](int i) const {
        return { data[i], data[i+1] };
    }
};

bool parse_INDEX(u8 const*& first, u8 const* last, INDEX& victim) {
    Card16 count = {};
    OffSize offSize = {};
    parse_Card16(first, last, count);
    printf("    count : %d %x\n", count, count);
    parse_OffSize(first, last, offSize);
    printf("    offSize : %d\n", offSize);
    victim.data.resize(count + 1);
    if (offSize == 1) {
        uint8 offset = {};
        auto q = first + (count + 1) - 1;
        for (int i = 0; i != count + 1; ++i) {
            parse_uint8(first, last, offset);
            // printf(" %d", offset);
            victim.data[i] = q + offset;
        }
    } else {
        uint16 offset = {};
        auto q = first + (count + 1) * 2 - 1;
        for (int i = 0; i != count + 1; ++i) {
            parse_network_uint16(first, last, offset);
            // printf(" %d", offset);
            victim.data[i] = q + offset;
        }
    }
    first = victim.data.back();
    return true;
}

void print_string_INDEX(INDEX const& index) {
    /*
     auto count = index.data.size() - 1;
     printf("[\n");
     for (int i = 0; i != count; ++i) {
     auto a = index.data[i];
     auto b = index.data[i + 1];
     printf("    \"%.*s\",\n", (int)(b - a), a);
     }
     printf("]\n");
     */
    printf("[\n");
    for (auto [a, b] : index) {
        printf("    \"%.*s\",\n", (int)(b - a), a);
    }
    printf("]\n");
}


using KEY = std::array<uint8, 2>;

bool parse_real(u8 const*& first, u8 const* last, double& victim) {
    std::string s;
    
    
    constexpr static char const* table[] = {
        "0", "1", "2", "3", "4", "5", "6", "7", "8", "9",
        ".", "E", "-E", "x", "-", "x"
    };
    
    for (;;) {
        if (first == last)
            return false;
        u8 b0 = *first++;
        int nibble = {};
        nibble = b0 >> 4;
        s.append(table[nibble]);
        nibble = b0 & 0xf;
        s.append(table[nibble]);
        if (nibble == 0xf)
            break;
    }
    std::from_chars(s.data(), s.data() + s.size(), victim);
    return true;
}

struct DICT {
    std::map<KEY, std::vector<double>> data;
};

bool parse_DICT(u8 const*& first, u8 const* last, DICT& victim) {
    std::vector<double> operands;
    for (;;) {
        if (first == last) {
            assert(operands.empty());
            break;
        }
        u8 b0 = *first++;
        if (b0 <= 21) {
            KEY key{ b0, b0 == 12 ? *first++ : (uint8)0 };
            victim.data.emplace(key, std::move(operands));
            operands.clear();
        } else if (b0 <= 27) {
            abort();
        } else if (b0 == 28) {
            int16 a = {};
            parse_network_int16(first, last, a);
            operands.push_back(a);
        } else if (b0 == 29) {
            int32 a = {};
            parse_network_int32(first, last, a);
            operands.push_back(a);
        } else if (b0 == 30) {
            double a = {};
            parse_real(first, last, a);
            operands.push_back(a);
        } else if (b0 == 31) {
            abort();
        } else if (b0 <= 246) {
            operands.push_back(b0 - 139);
        } else if (b0 <= 250) {
            u8 b1 = *first++;
            operands.push_back((b0 - 247) * 256 + b1 + 108);
        } else if (b0 <= 254) {
            u8 b1 = *first++;
            operands.push_back(-(b0 - 251) * 256 - b1 - 108);
        } else if (b0 == 255) {
            abort();
        }
    }
    return true;
}

void print_DICT(DICT const& x) {
    printf("    {\n");
    for (auto const& [k, v] : x.data) {
        printf("        \"%d,%d\" : [ ", k[0], k[1]);
        for (auto n : v) {
            printf("%g, ", n);
        }
        printf("],\n");
    }
    printf("    }\n");
}


void print_DICT_INDEX(INDEX const& index) {
    printf("[\n");
    for (auto [a, b] : index) {
        DICT dictionary = {};
        parse_DICT(a, b, dictionary);
        print_DICT(dictionary);
    }
    printf("]\n");
}


struct Type2Charstring {
    std::map<KEY, std::vector<double>> data;
};

// We can't parse a Type2Charstring without knowing the stem hint list
// This means there is no intermediate representation possible; we just
// have to execute the program??



union Point {
    struct { double x, y; };
    struct { double data[2]; };
};

struct Line {
    Point a;
    Point b;
};

struct Bezier2 {
    Point a;
    Point b;
    Point c;
};

struct Rect {
    Point a;
    Point b;
};

template<std::size_t N>
struct Poly {
    Point a[N];
};



static Point csv_cursor{};

double lerp(double a, double b, double t) {
    return a * (1.0 - t)  + b * t;
}
Point lerp(Point a, Point b, double t) {
    return Point{lerp(a.x, b.x, t), lerp(a.y, b.y, t)};
}

Point Bezier(Point a, Point b, Point c, Point d, double t) {
    auto ab = lerp(a, b, t);
    auto bc = lerp(b, c, t);
    auto cd = lerp(c, d, t);
    auto abc = lerp(ab, bc, t);
    auto bcd = lerp(bc, cd, t);
    return lerp(abc, bcd, t);
}

struct Type2CharstringEngine {
    
    INDEX global_subroutines;
    INDEX local_subroutines;
    
    std::deque<double> stack;
    std::stack<Span<const u8>> cs;
    bool is_first_stack_clearing_operator = true;
    double width = 0.0;
    Point point = {};
    u8 mode = {};
    
    enum {
        MOVE,
        LINE,
        BEZIER,
    };
    
    std::vector<double> hstem;
    std::vector<double> vstem;
    
    std::vector<Point> points;
    std::vector<u8> modes;
    
    bool execute(Span<const u8> str);
    
    void maybe_width() {
        if (is_first_stack_clearing_operator) {
            is_first_stack_clearing_operator = false;
            if (!stack.empty()) {
                printf("(: width) ");
                width = stack.front();
                stack.pop_front();
            }
        }
    }
    
    void maybe_width_if_odd() {
        if (stack.size() & 1)
            maybe_width();
    }
    
    void maybe_width_if_even() {
        if (!(stack.size() & 1))
            maybe_width();
    }
    
    void push() {
        points.push_back(point);
        modes.push_back(mode);
    }
    
    void dx() {
        point.x += stack.front(); stack.pop_front();
        push();
    }
    
    void dy() {
        point.y += stack.front(); stack.pop_front();
        push();
    }
    
    void dxy() {
        point.x += stack.front(); stack.pop_front();
        point.y += stack.front(); stack.pop_front();
        push();
    }
    
    void do_hstem() {
        while (!stack.empty()) {
            assert(stack.size() >= 2);
            hstem.push_back(stack.front()); stack.pop_front();
            hstem.push_back(stack.front()); stack.pop_front();
        }
    }
    
    void do_vstem() {
        while (!stack.empty()) {
            assert(stack.size() >= 2);
            vstem.push_back(stack.front()); stack.pop_front();
            vstem.push_back(stack.front()); stack.pop_front();
        }
    }
    
    void do_mask(Span<const u8>& str) {
        int number_of_bytes = (int)((hstem.size() + vstem.size() + 14) / 16);
        while (number_of_bytes--) {
            printf(" %#02hhx", str.pop_front());
        }
        printf("\n");
    }
    
    void print_result() {
        printf("[");
        for (size_t i = 0; i != points.size(); ++i) {
            auto p = points[i];
            auto m = modes[i];
            printf("[%g, %g, %d],", p.x, p.y, m);
        }
        printf("]\n");
    }
    
    
    
    void render_result() {
        FILE* fd = fopen("/Users/antony/Desktop/dump.csv", "a");
        
        size_t i = 0;
        size_t j = 0;
        for (; i != points.size(); ++i) {
            switch (modes[i]) {
                case MOVE:
                    if (i != 0) {
                        // we need to close the curve
                        for (double t = 0; t < 1.0; t += 0.0625) {
                            Point ab = lerp(points[i-1], points[j], t);
                            fprintf(fd, "%g, %g\n", ab.x + csv_cursor.x, ab.y + csv_cursor.y);
                        }
                        j = i;
                        // we need to break the curve
                        fprintf(fd, "%g, %g\n", NAN, NAN);
                    }
                    break;
                case LINE: {
                    for (double t = 0; t < 1.0; t += 0.0625) {
                        Point ab = lerp(points[i-1], points[i], t);
                        fprintf(fd, "%g, %g\n", ab.x + csv_cursor.x, ab.y + csv_cursor.y);
                    }
                } break;
                case BEZIER:
                    for (double t = 0; t < 1.0; t += 0.0625) {
                        Point abcd = Bezier(points[i-1], points[i], points[i + 1], points[i + 2], t);
                        fprintf(fd, "%g, %g\n", abcd.x + csv_cursor.x, abcd.y + csv_cursor.y);
                    }
                    i += 2;
                    break;
            }
        }
        if (i) {
            // close the curve with a line
            for (double t = 0; t < 1.0; t += 0.0625) {
                Point ab = lerp(points[i-1], points[j], t);
                fprintf(fd, "%g, %g\n", ab.x + csv_cursor.x, ab.y + csv_cursor.y);
            }
            fprintf(fd, "%g, %g\n", NAN, NAN);
        }
        
        fclose(fd);
        csv_cursor.x += 1000;
        if (csv_cursor.x >= 32000) {
            csv_cursor.x = 0;
            csv_cursor.y -= 1200;
        }
    }
    
    
    
    std::vector<Line> lines;
    
    std::vector<Bezier2> beziers;
    
    
    
    void line_dump() {
        // line dump
        size_t i = 0;
        size_t j = 0;
        for (; i != points.size(); ++i) {
            switch (modes[i]) {
                case MOVE:
                    if (i != 0) {
                        // we need to close the curve
                        Line ab{points[i-1],points[j]};
                        lines.push_back(ab);
                        j = i;
                        beziers.push_back(Bezier2{
                            ab.a,
                            lerp(ab.a, ab.b, 0.5),
                            ab.b
                        });
                    }
                    break;
                case LINE: {
                    Line ab{points[i-1],points[i]};
                    lines.push_back(ab);
                    beziers.push_back(Bezier2{
                        ab.a,
                        lerp(ab.a, ab.b, 0.5),
                        ab.b
                    });
                } break;
                case BEZIER: {
                    std::vector<Point> my_points;
                    Point a{points[i-1]};
                    Point b{points[i]};
                    Point c{points[i+1]};
                    Point d{points[i+2]};
                    for (double t = 0.0; t <= 1.0; t += 1.0/16.0) {
                        Point abcd = Bezier(points[i-1], points[i], points[i + 1], points[i + 2], t);
                        my_points.push_back(abcd);
                    }
                    i += 2;
                    for (int j = 0; j + 1 != my_points.size(); ++j) {
                        lines.push_back(Line{my_points[j],my_points[j+1]});
                    }
                    // on curve points are a -> e -> d
                    Point e = Bezier(a, b, c, d, 0.5);
                    // off curve points are something like
                    //     b
                    //    f
                    //   a e d
                    //      g
                    //     c
                    
                    // We need these triples to be co-linear
                    // a-f-b, f-e-g, d-g-c
                    //
                    // And we need f-e-g to be tangent to the Bezier through
                    // e
                    //
                    // We have the weightings
                    //
                    // q = a * (1-t)**3 + 3*b*t*(1-t)**2 + 3*c*t**2*(1-t) + d*t**3
                    //
                    // So dq/dt =
                    //
                    //   = -3*a*(1-t)**2
                    //     +3*b*((1-t)**2 - 2*t(1-t))
                    //     +3*c*(2*t*(1-t) - t**2)
                    //     +3*d*t**2
                    
//                    double z[4] = {
//                        -0.75,
//                        -0.75,
//                        +0.75,
//                        +0.75,
//                    };
//                    Point y{
//                        (-a.x-b.x+c.x+d.x)*0.75,
//                        (-a.y-b.y+c.y+d.y)*0.75
//                    };
                    
                    // i.e. the derivative is the direction from mean ab to mean cd
                    //      {-3/4,-3/4, 3/4, 3/4 }
                    // (note that e is
                    //      { 1/8, 3/8, 3/8, 1/8 }
                    
                    // Intersect the lines e + y*t and a + (b - a)*s
                    //
                    
                    // Not quite this, but try it
                    // (this only works if a-b and c-d have same projection othogonal to y)
                    Point f = lerp(a, b, 0.75);
                    Point g = lerp(c, d, 0.25);
                    
                    // Result: wonky!  Tends to produce diamondy-looking ellipses
                    
                    // TODO: Properly!
                    
                    beziers.push_back(Bezier2{a,f,e});
                    beziers.push_back(Bezier2{e,g,d});

                    
                } break;
            }
        }
        if (i) {
            // close the curve with a line
            Line ab{points[i-1],points[j]};
            lines.push_back(ab);
            beziers.push_back(Bezier2{
                ab.a,
                lerp(ab.a, ab.b, 0.5),
                ab.b
            });        }
    }
    
    
    
    
    
    
    void raster();
    void raster2();
    double raster3(Point);

    
    
};

bool Type2CharstringEngine::execute(Span<const u8> str) {
    while (!str.empty()) {
        u8 b0 = str.pop_front();
        // printf("<%d>", b0);
        if ((b0 <= 31) && (b0 != 28)) {
            switch (b0) {
                case 1: {
                    maybe_width_if_odd();
                    printf(": hstem\n");
                    do_hstem();
                    assert(stack.empty());
                    break;
                }
                case 3: {
                    maybe_width_if_odd();
                    printf(": vstem\n");
                    do_vstem();
                    assert(stack.empty());
                    break;
                }
                case 4: {
                    maybe_width_if_even();
                    printf(": vmoveto\n");
                    mode = MOVE;
                    dy();
                    assert(stack.empty());
                    break;
                }
                case 5: {
                    printf(": rlineto\n");
                    mode = LINE;
                    do  {
                        dxy();
                    } while ((!stack.empty()));
                    break;
                }
                case 6: {
                    printf(": hlineto\n");
                    int parity = 0;
                    mode = LINE;
                    do {
                        point.data[parity] += stack.front(); stack.pop_front();
                        push();
                        parity ^= 1;
                    } while (!stack.empty());
                    break;
                }
                case 7: {
                    printf(": vlineto\n");
                    int parity = 1;
                    mode = LINE;
                    do {
                        point.data[parity] += stack.front(); stack.pop_front();
                        push();
                        parity ^= 1;
                    } while (!stack.empty());
                    break;
                }
                case 8: {
                    printf(": rrcurveto\n");
                    mode = BEZIER;
                    do {
                        dxy(); dxy(); dxy();
                    } while (!stack.empty());
                    break;
                }
                case 10: { // callsubr
                    printf(": callsubr\n");
                    int i = (int)stack.back() + 107;
                    stack.pop_back();
                    cs.push(str);
                    std::tie(str._begin, str._end) = local_subroutines[i];
                    break;
                }
                case 11: { // return
                    printf(": return\n");
                    assert(str.empty());
                    str = cs.top(); cs.pop();
                    break;
                }
                case 14: // endchar
                    maybe_width_if_odd();
                    printf(": endchar\n");
                    assert(stack.empty());
                    // print_result();
                    render_result();
                    return true;
                case 18: { // hstemhm
                    maybe_width_if_odd();
                    printf(": hstemhm\n");
                    do_hstem();
                    break;
                }
                case 19: { // hintmask
                    maybe_width_if_odd();
                    if (!stack.empty()) {
                        printf("(: vstem) ");
                        do_vstem();
                    }
                    printf(": hintmask");
                    do_mask(str);
                    assert(stack.empty());
                    break;
                }
                case 20: { // cntrmask
                    maybe_width_if_odd();
                    if (!stack.empty()) {
                        printf("(: vstem) ");
                        do_vstem();
                    }
                    printf(": cntrmask");
                    do_mask(str);
                    assert(stack.empty());
                    break;
                }
                case 21: {
                    maybe_width_if_odd();
                    printf(": rmoveto\n");
                    mode = MOVE;
                    dxy();
                    assert(stack.empty());
                    break;
                }
                case 22: {
                    maybe_width_if_even();
                    printf(": hmoveto\n");
                    mode = MOVE;
                    dx();
                    assert(stack.empty());
                    break;
                }
                case 23: {
                    maybe_width_if_odd();
                    printf(": vstemhm\n");
                    do_vstem();
                    assert(stack.empty());
                    break;
                }
                case 24: {
                    printf(": rcurveline\n");
                    mode = BEZIER;
                    do {
                        dxy(); dxy(); dxy();
                    } while (stack.size() >= 6);
                    mode = LINE;
                    dxy();
                    assert(stack.empty());
                    break;
                }
                case 25: {
                    printf(": rlinecurve\n");
                    mode = LINE;
                    do {
                        dxy();
                    } while (stack.size() > 6);
                    mode = BEZIER;
                    dxy(); dxy(), dxy();
                    break;
                }
                case 26: {
                    printf(": vvcurveto\n");
                    if (stack.size() & 1) {
                        point.x += stack.front(); stack.pop_front();
                    }
                    mode = BEZIER;
                    do {
                        dy(); dxy(); dy();
                    } while (!stack.empty());
                    break;
                }
                case 27: {
                    printf(": hhcurveto\n");
                    if (stack.size() & 1) {
                        point.y += stack.front(); stack.pop_front();
                    }
                    mode = BEZIER;
                    do {
                        dx(); dxy(); dx();
                    } while (!stack.empty());
                    assert(stack.empty());
                    break;
                }
                case 30: {
                    printf(": vhcurveto\n");
                    bool parity = 1;
                    mode = BEZIER;
                    do {
                        assert(stack.size() >= 4);
                        if (parity == 0) {
                            dx(); dxy();
                            point.y += stack.front(); stack.pop_front();
                            if (stack.size() == 1) {
                                point.x += stack.front(); stack.pop_front();
                            }
                        } else {
                            dy(); dxy();
                            point.x += stack.front(); stack.pop_front();
                            if (stack.size() == 1) {
                                point.y += stack.front(); stack.pop_front();
                            }
                        }
                        push();
                        parity = parity ^ 1;
                    } while (!stack.empty());
                    break;                    }
                case 31: {
                    printf(": hvcurveto\n");
                    bool parity = 0;
                    mode = BEZIER;
                    do {
                        assert(stack.size() >= 4);
                        if (parity == 0) {
                            dx(); dxy();
                            point.y += stack.front(); stack.pop_front();
                            if (stack.size() == 1) {
                                point.x += stack.front(); stack.pop_front();
                            }
                        } else {
                            dy(); dxy();
                            point.x += stack.front(); stack.pop_front();
                            if (stack.size() == 1) {
                                point.y += stack.front(); stack.pop_front();
                            }
                        }
                        push();
                        parity = parity ^ 1;
                    } while (!stack.empty());
                    break;
                }
                default:
                    printf(": Unhandled b0 = %d\n", b0);
                    abort();
                    break;
            }
        } else {
            // Is an operand (a number)
            double number{};
            if (b0 == 28) {
                int16 a = {};
                parse_network_int16(str._begin, str._end, a);
                number = a;
            } else if (b0 <= 246) {
                number = b0 - 139;
            } else if (b0 <= 250) {
                u8 b1 = str.pop_front();
                number = (b0 - 247) * 256 + b1 + 108;
            } else if (b0 <= 254) {
                u8 b1 = str.pop_front();
                number = -(b0 - 251) * 256 - b1 - 108;
            } else { assert(b0 == 255);
                int32 a = {};
                parse_network_int32(str._begin, str._end, a);
                number = a * 0x1p-16;
            }
            printf("%g ",number);
            stack.push_back(number);
        }
    }
    if (!stack.empty()) {
        printf("Missing operator??\n");
    }
    printf("Missing endchar??\n");
    abort();
};

Rect bounding_box(Line a) {
    return Rect{
        Point{
            std::min(a.a.x, a.b.x),
            std::min(a.a.y, a.b.y),
        },
        Point{
            std::max(a.a.x, a.b.x),
            std::max(a.a.y, a.b.y),
        }
    };
}

Rect round(Rect a) {
    return Rect{
        Point{
            std::floor(a.a.x),
            std::floor(a.a.y),
        },
        Point{
            std::ceil(a.b.x),
            std::ceil(a.b.y),
        },
    };
}

Line yselect(Line a, double ylo, double yhi) {
    assert(a.a.y < a.b.y);
    assert(ylo < yhi);
    
    Line z = {};
    if (a.a.y < ylo) {
        z.a.x = a.a.x + (a.b.x - a.a.x) * (ylo - a.a.y) / (a.b.y - a.a.y);
        z.a.y = ylo;
    } else {
        z.a = a.a;
    }
    if (a.b.y > yhi) {
        z.b.x = a.a.x + (a.b.x - a.a.x) * (yhi - a.a.y) / (a.b.y - a.a.y);
        z.b.y = yhi;
    } else {
        z.b = a.b;
    }
    
    return z;
}


Point evaluate(Bezier2 a, double t) {
    auto ab = lerp(a.a, a.b, t);
    auto bc = lerp(a.b, a.c, t);
    return lerp(ab, bc, t);
}

double quadratic_root(double a, double b, double c, double fallback) {
    // TODO: This is numerically unstable
    // Look up numerical recipes rearrangement

    if (a == 0.0) {
        return fallback;
    }
    // assert(a != 0.0);
    
    if (a < 0) {
        a = -a;
        b = -b;
        c = -c;
    }

    // (-b +/- sqrt(b**2 - 4*a*c)) / 2*a
    double discrimiant = b*b - 4*a*c;
    assert(discrimiant > 0.0);
    
    double s = sqrt(discrimiant);
    
    double r1 = (-b - s) / (2*a);
    double r2 = (-b + s) / (2*a);
    
    if (r1 < 0.0) {
        assert(r2 >= 0.0);
        assert(r2 <= 1.0);
        return r2;
    } else {
        assert(r1 <= 1.0);
        assert(r2 > 1.0);
        return r1;
    }
        
        
    
    
    
    
    
    
    
}

Bezier2 yselect(Bezier2 a, double ylo, double yhi) {
    
    // We need a Bezier curve for x as a function of y
    
    // y = a*(1-t)**2 + b*2*(1-t)*t + c*t**2
    //
    // y = a*(1-2*t+t**2) + b*2*(t-t**2) + c*t**2
    // 0 = (a-2*b+ c)*t**2 + (-2*a+2*b)*t + (a - y)
    
    double tlo = 0.0;
    if (a.a.y < ylo && ylo < a.c.y) {
        tlo = quadratic_root(a.a.y - 2*a.b.y + a.c.y,
                                    -2.0*a.a.y + 2.0*a.b.y,
                                    a.a.y - ylo,
                             tlo);
        //printf("%g\n", tlo);
    }
    double thi = 1.0;
    if (a.a.y < yhi && yhi < a.c.y) {
        thi = quadratic_root(a.a.y - 2*a.b.y + a.c.y,
                                    -2.0*a.a.y + 2.0*a.b.y,
                                    a.a.y - yhi,
        thi);
        //printf("%g\n", thi);
    }
    
    Bezier2 a2;
    a2.a = evaluate(a, tlo);
    a2.c = evaluate(a, thi);
    a2.b = evaluate(a, (tlo + thi) * 0.5);
    
    //assert(a2.a.y >= ylo-0.01);
    //assert(a2.c.y <= yhi+0.01);

    
    a2.b.x += a2.b.x - (a2.a.x + a2.c.x) * 0.5;
    a2.b.y += a2.b.y - (a2.a.y + a2.c.y) * 0.5;
    

    
        
    return a;
    
    
    
};


Line xselect(Line a, double xlo, double xhi) {
    assert(a.a.x <= a.b.x);
    assert(xlo < xhi);
    
    Line z = {};
    if (a.a.x < xlo) {
        z.a.x = xlo;
        z.a.y = a.a.y + (a.b.y - a.a.y) * (xlo - a.a.x) / (a.b.x - a.a.x);
    } else {
        z.a = a.a;
    }
    if (a.b.x > xhi) {
        z.b.x = xhi;
        z.b.y = a.a.y + (a.b.y - a.a.y) * (xhi - a.a.x) / (a.b.x - a.a.x);
    } else {
        z.b = a.b;
    }
    
    return z;
}

void Type2CharstringEngine::raster() {
    
    line_dump();
    
    ptrdiff_t stride;
    double* image;
    
    stride = 1024;
    image = (double*) calloc(stride * stride, 8);
    
    
    for (int k = 0; k != lines.size(); ++k) {
        
        double sign = +1.0;
        Line a = lines[k];
        double s = 1.0;
        a.a.x /= s;
        a.a.y /= s;
        a.b.x /= s;
        a.b.y /= s;
        if (a.a.y == a.b.y)
            continue;
        if (a.a.y > a.b.y) {
            std::swap(a.a, a.b);
            sign *= -1.0;
        }
        Rect b = bounding_box(a);
        Rect c = round(b);
        
        // TODO: This is an inefficient implementation of the ideas in the
        // classic line-drawing algorithm that skips from grid intersect to
        // grid intersect in x or y steps of unity.  We can directly solve
        // ax + by = m for each step, with the other coordinate being unity,
        // and just step by the constants dy/dx and dx/dy
        
        for (double y = c.a.y; y != c.b.y; y += 1) {
            double ylo = y;
            double yhi = y + 1.0;
            Line d = yselect(a, ylo, yhi);
            if (d.a.x > d.b.x) {
                std::swap(d.a.x, d.b.x);
                // sign *= -1.0;
            }
            double xlob = std::floor(d.a.x);
            double xhib = std::ceil(d.b.x);
            for (double x = xlob; x != xhib; x += 1) {
                Line e = xselect(d, x, x + 1.0);
                // image[(int)x + stride * ((int)y + 128)] += 1.0;
                // area in pixel:
                assert(e.a.x <= e.b.x);
                double xmid = (e.a.x + e.b.x) * 0.5;
                double area = (e.b.y - e.a.y) * (x + 1.0 - xmid);
                double area2 = (e.b.y - e.a.y) * (xmid - x);
                image[(int)x + stride * (900-(int)y)] += area * sign;
                image[(int)x + 1 + stride * (900-(int)y)] += area2 * sign;
            }
            if (xlob == xhib) {
                image[(int)xlob + stride * (900-(int)y)] += (d.b.y - d.a.y) * sign;
            }
        }
    }
    
    // integrate
    for (int j = 0; j != stride; ++j) {
        double k = 0.0;
        for (int i = 0; i != stride; ++i) {
            k = (image[i + stride * j] += k);
        }
    }
    
    
    FILE* f = fopen("/Users/antony/Desktop/image.csv", "wt");
    for (int j = 0; j != stride; ++j) {
        for (int i = 0; i != stride; ++i) {
            if (i != 0)
                fprintf(f,",");
            fprintf(f, "%g", image[i + j * stride]);
        }
        fprintf(f, "\n");
    }
    fclose(f);
    
    
    
}

std::map<int, int> histogram;


double Type2CharstringEngine::raster3(Point v) {
    
    double dd = 2.0;
    
    double cumulant = 0.0;
    
    int counter = 0;
    for (Bezier2 z : beziers) {
        
        tmp::Bezier2 b{{z.a.x, z.a.y}, {z.b.x, z.b.y}, {z.c.x, z.c.y}};

        if (b.a.y == b.c.y)
            continue;
        
        double scale;
        if (b.a.y > b.c.y) {
            scale = -1.0;
            std::swap(b.a, b.c);
        } else {
            scale = +1.0;
        }

        // initial bounds

        double ylo = v.y - dd;
        double yhi = v.y + dd;
        double xlo = v.x - dd;
        double xhi = v.x + dd;
        double tlo = 0.0;
        double thi = 1.0;

        // Three coarse cuts
        if (yhi <= b.a.y)
            continue;
        if (b.c.y <= ylo)
            continue;
        counter++;
        if (xhi <= std::min(b.a.x, b.c.x))
            continue;
        // if (std::max(b.a.x, b.c.x) <= xlo)
           // continue;

        double tylo{};
        double xylo{};
        if (ylo <= b.a.y) {
            ylo = b.a.y;
            tylo = tlo;
            xylo = b.a.x;
        } else {
            tylo = b.t_for_y(ylo, tlo, thi);
            xylo = b.xy_for_t(tylo).x;
        }
        assert(ylo <= yhi);
        assert(tlo <= tylo);
        assert(tylo <= thi);
        double tyhi{};
        double xyhi{};
        if (b.c.y < yhi) {
            yhi = b.c.y;
            tyhi = thi;
            xyhi = b.c.x;
        } else {
            tyhi = b.t_for_y(yhi, tlo, thi);
            xyhi = b.xy_for_t(tyhi).x;
        }
        assert(ylo <= yhi);
        assert(tylo <= tyhi);
        assert(tyhi <= thi);

        // We now have bounded the curve to a row of pixels
        
        if (std::min(xylo, xyhi) >= xhi) {
            continue;
        }
        
        if (std::max(xylo, xyhi) <= xlo) {
            cumulant += scale * (yhi - ylo) * (xhi - xlo);
            continue;
        }
        
        double xylo2 = simd_clamp(xylo, xlo, xhi);
        double xyhi2 = simd_clamp(xyhi, xlo, xhi);
        
        double yxylo{};
        if (xylo2 == xylo) {
            yxylo = ylo;
        } else {
            yxylo = b.xy_for_t(b.t_for_x(xylo2, tylo, tyhi)).y;
        }
        double yxyhi{};
        if (xyhi2 == xyhi) {
            yxyhi = yhi;
        } else {
            yxyhi = b.xy_for_t(b.t_for_x(xyhi2, tylo, tyhi)).y;
        }

        cumulant += scale * (yxylo - ylo) * (xhi - xylo2);
        cumulant += scale * (yxyhi - yxylo) * (xhi - (xyhi2 + xylo2) * 0.5);
        cumulant += scale * (yhi - yxyhi) * (xhi - xyhi2);
        
        // TODO: This is the area of the straight line that has the same
        // endpoints as the curve after it is clipped to the pixel
        
        
    }
    histogram[counter]++;
    return cumulant;
}


void Type2CharstringEngine::raster2() {
    line_dump();
    
    printf("beziers %zd\n", beziers.size());
    
    ptrdiff_t stride;
    double* image;
    
    stride = 1024;
    image = (double*) calloc(stride * stride, 8);

    for (int j = 0; j != stride; ++j) {
        for (int i = 0; i != stride; ++i) {
            
            Point v{ i + 0.5, j + 0.5 };
            image[i + j * stride] = raster3(v);
        }
    }
    
    for (auto [k,v]:histogram) {
        printf("%d %d\n", k, v);
    }
    
    
    FILE* f = fopen("/Users/antony/Desktop/image.csv", "wt");
    for (int j = 0; j != stride; ++j) {
        for (int i = 0; i != stride; ++i) {
            if (i != 0)
                fprintf(f,",");
            fprintf(f, "%g", image[i + j * stride]);
        }
        fprintf(f, "\n");
    }
    fclose(f);
    
    
    
}




struct OpenTypeZ {
    
    // memory mapping
    void* mmap_addr{};
    size_t mmap_len{};
    
    OpenTypeZ() {
        
        // Load
        int fildes = open("/Users/antony/Desktop/assets/Futura Medium Condensed.otf", O_RDONLY);
        struct stat buf = {};
        fstat(fildes, &buf);
        mmap_len = buf.st_size;
        mmap_addr = mmap(nullptr, mmap_len, PROT_READ, (MAP_FILE | MAP_PRIVATE), fildes, 0);
        close(fildes);
        
        // We only want to process the contents on request, but we may also want
        // to cache some of this processing such as offsets to important
        // structures
        
    }
    
    
    
    ~OpenTypeZ() {
        munmap(mmap_addr, mmap_len);
    }
    
    // We want to specify size
    // Get glyph metrics:
    //   ascend descend advance
    // Get glyph rasterized
    
    // We want to pack a texture
    //    with several faces and sizes for mixed GUI rendering
    //    with other GUI features
    //    using skyline packing
    // We want to on-the-fly
    
    
};

struct CMapSubtableUnicodeBMP {
    
    ptrdiff_t segCount;
    uint16_t const* endCode;
    
    enum { MISSING_GLYPH = 0xFFFF };
    
    int lookup(int code) {
        uint16_t const* cursor = endCode;
        // look for first segment with end >= code
        while (ntohs(*cursor++) < code)
            ;
        // padding word is accounted for by postincrement
        cursor += segCount;
        uint16_t startCode = ntohs(*cursor);
        if (startCode > code) {
            // segment excludes the code; thus it is not present
            return MISSING_GLYPH;
        }
        cursor += segCount;
        uint16_t idDelta = ntohs(*cursor);
        cursor += segCount;
        uint16_t idRangeOffset = ntohs(*cursor);
        if (idRangeOffset != 0) {
            cursor += (idRangeOffset >> 1) + (code - startCode);
            code = ntohs(*cursor);
            if (code == 0)
                return MISSING_GLYPH;
        }
        return (idDelta + code) & 0xFFFF;
    }
    
};

bool parse_EncodingRecord(u8 const*& first, u8 const* last, u8 const* base) {
    printf("EncodingRecord\n");
    uint16 platformID{};
    parse_network_uint16(first, last, platformID);
    printf("platformID %d\n", platformID);
    // 0: Unicode
    // 1: Macintosh [discouraged]
    // 2: ISO [deprecated]
    // 3: Windows
    // 4: Custom
    uint16 encodingID{};
    parse_network_uint16(first, last, encodingID);
    printf("encodingID %d\n", encodingID);
    // 0,3: Unicode BMP (basic multilingual plane), 0..65535
    // 1,0: Roman [discouraged]
    // 3,1: Unicode BMP
    Offset32 subtableOffset{};
    parse_network_uint32(first, last, subtableOffset);
    printf("subtableOffset %d\n", subtableOffset);
    
    auto first2 = base + subtableOffset;
    uint16 format{};
    parse_network_uint16(first2, last, format);
    printf("format %d\n", format);

    if (format != 4)
        return false;
    
    uint16 length{};
    parse_network_uint16(first2, last, length);
    printf("length %d\n", length);
    uint16 language{};
    parse_network_uint16(first2, last, language);
    printf("language %d\n", language);
    uint16 segCountX2{};
    parse_network_uint16(first2, last, segCountX2);
    printf("segCountX2 %d\n", segCountX2);
    uint16 searchRange{};
    parse_network_uint16(first2, last, searchRange);
    printf("searchRange %d\n", searchRange);
    uint16 entrySelector{};
    parse_network_uint16(first2, last, entrySelector);
    printf("entrySelector %d\n", entrySelector);
    uint16 rangeShift{};
    parse_network_uint16(first2, last, rangeShift);
    printf("rangeShift %d\n", rangeShift);
    int segCount = segCountX2 / 2;
        
    CMapSubtableUnicodeBMP u;
    u.segCount = segCount;
    // TODO: this assumes alignment
    u.endCode = (uint16_t const*)first2;
    for (int i = 32; i != 127; ++i) {
        auto b = u.lookup(i);
        if (b != 0xFFFF) {
            printf("%d'%c'->%d\n", i,i, b);
        }
    }

    std::vector<uint16> endCode(segCount);
    for (int i = 0; i != segCount; ++i) {
        parse_network_uint16(first2, last, endCode[i]);
        printf("endCodes[%d] %d\n", i, endCode[i]);
    }
    assert(endCode.back() == 0xFFFF);
    uint16 reservedPad{};
    parse_network_uint16(first2, last, reservedPad);
    printf("reservedPad %d\n", reservedPad);
    std::vector<uint16> startCode(segCount);
    for (int i = 0; i != segCount; ++i) {
        parse_network_uint16(first2, last, startCode[i]);
        printf("startCode[%d] %d\n", i, startCode[i]);
    }
    assert(startCode.back() == 0xFFFF);
    std::vector<int16> idDelta(segCount);
    for (int i = 0; i != segCount; ++i) {
        parse_network_int16(first2, last, idDelta[i]);
        printf("idDelta[%d] %d\n", i, idDelta[i]);
    }
    std::vector<uint16> idRangeOffset(segCount);
    for (int i = 0; i != segCount; ++i) {
        parse_network_uint16(first2, last, idRangeOffset[i]);
        printf("idRangeOffset[%d] %d\n", i, idRangeOffset[i]);
    }
    
    
    return true;
}

bool parse_cmapHeader(u8 const*& first, u8 const* last) {
    u8 const* base = first;
    printf("cmap\n");
    uint16 version{};
    parse_network_uint16(first, last, version);
    printf("version %d\n", version);
    assert(version == 0);
    uint16 numTables{};
    parse_network_uint16(first, last, numTables);
    printf("numTables %d\n", numTables);
    for (int i = 0; i != numTables; ++i)
        parse_EncodingRecord(first, last, base);
    
    
    return true;
}




bool parse_glyfHeader(u8 const*& first, u8 const* last) {
    u8 const* base = first;
    printf("cmap\n");
#define X(S) int16_t S{}; parse_network_int16(first, last, S); printf(#S " %d\n", S);
    X(numberOfContours);
    X(xMin);
    X(yMin);
    X(xMax);
    X(yMax);

#define U(S) uint16_t S{}; parse_network_uint16(first, last, S); printf(#S " %d\n", S);

    std::vector<int> epoc;
    for (int i = 0; i != numberOfContours; ++i) {
        U(endPtsOfContours);
        epoc.push_back(endPtsOfContours);
    }
    U(instructionLength);
    first += instructionLength;
    std::vector<u8> flags;
    enum {
        ON_CURVE_POINT = 0x01,
        X_SHORT_VECTOR = 0x02,
        Y_SHORT_VECTOR = 0x04,
        REPEAT_FLAG    = 0x08,
        X_IS_SAME_OR_POSITIVE_X_SHORT_VECTOR = 0x10,
        Y_IS_SAME_OR_POSITIVE_Y_SHORT_VECTOR = 0x20,
    };
    
    int xbytes = 0;
    for (; flags.size() != epoc.back() + 1;) {
        // Unpack bytes
        u8 a = *first++;
        switch (a & (X_SHORT_VECTOR | X_IS_SAME_OR_POSITIVE_X_SHORT_VECTOR)) {
            case 0: xbytes += 2; break;
            case X_SHORT_VECTOR: xbytes += 1; break;
            case X_IS_SAME_OR_POSITIVE_X_SHORT_VECTOR: xbytes += 0; break;
            case X_SHORT_VECTOR | X_IS_SAME_OR_POSITIVE_X_SHORT_VECTOR: xbytes +=1; break;
        }
        
        if (a & REPEAT_FLAG) {
            u8 b = *first++;
            printf("    repeating %02hhx %d times\n", a, b);
            while (b--) {
                flags.push_back(a);
            }
        } else {
            printf("    %#02hhx\n", a);
            flags.push_back(a);
        }
    }
    
    // first now points to the xarray
    auto firsty = first + xbytes;
    Point current{};
    std::vector<Point> points;
    std::vector<bool> on_curve_points;
    for (auto f : flags) {
        switch (f & (X_SHORT_VECTOR | X_IS_SAME_OR_POSITIVE_X_SHORT_VECTOR)) {
            case 0: {
                int16_t dx{};
                parse_network_int16(first, last, dx);
                printf("dx is %d\n", dx);
                current.x += dx;
                break;
            }
            case X_SHORT_VECTOR:
                printf("dx is -%d\n", *first);
                current.x -= *first++;
                break;
            case X_IS_SAME_OR_POSITIVE_X_SHORT_VECTOR:
                printf("dx is 0\n");
                break;
            case X_SHORT_VECTOR | X_IS_SAME_OR_POSITIVE_X_SHORT_VECTOR:
                printf("dx is +%d\n", *first);
                current.x += *first++;
                break;
        }
        switch (f & (Y_SHORT_VECTOR | Y_IS_SAME_OR_POSITIVE_Y_SHORT_VECTOR)) {
            case 0: {
                int16_t dy{};
                parse_network_int16(firsty, last, dy);
                printf("dy is %d\n", dy);
                current.y += dy;
                break;
            }
            case Y_SHORT_VECTOR:
                printf("dy is -%d\n", *firsty);
                current.y -= *firsty++;
                break;
            case Y_IS_SAME_OR_POSITIVE_Y_SHORT_VECTOR:
                printf("dy is 0\n");
                break;
            case Y_SHORT_VECTOR | Y_IS_SAME_OR_POSITIVE_Y_SHORT_VECTOR:
                printf("dy is +%d\n", *firsty);
                current.y += *firsty++;
                break;
        }
        printf("%d, %g, %g\n", f & ON_CURVE_POINT, current.x, current.y);
        on_curve_points.push_back(f & ON_CURVE_POINT);
        points.push_back(current);
    }
    
    FILE* fd = fopen("/Users/antony/Desktop/dump.csv", "w");
    for (int i = 0; i != points.size(); ++i) {
        if (on_curve_points[i]) {
            Point a = points[i];
            fprintf(fd, "%g, %g\n", a.x, a.y);
        } else {
            Point b = points[i];
            Point a = b;
            Point c = b;
            if (i > 0) {
                a = points[i - 1];
                if (!on_curve_points[i - 1])
                    a = lerp(a, b, 0.5);
            }
            if (i + 1 < points.size()) {
                c = points[i + 1];
                if (!on_curve_points[i + 1])
                    c = lerp(c, b, 0.5);
            }
            for (double t = 0.5; t <= 1.0; t += 0.5) {
                Point ab = lerp(a, b, t);
                Point bc = lerp(b, c, t);
                Point abc = lerp(ab, bc, t);
                fprintf(fd, "%g, %g\n", abc.x, abc.y);
            }
        }
    }
    
    
    
    exit(EXIT_SUCCESS);
    
    
    
    
    
    


    
    
    return true;
}


bool parse_FontHeaderTable(u8 const*& first, u8 const* last) {

#define I16(NAME) int16_t NAME{}; parse_network_int16(first, last, NAME); printf(#NAME " %hd\n", NAME);
#define U16(NAME) uint16_t NAME{}; parse_network_uint16(first, last, NAME); printf(#NAME " %hu\n", NAME);
#define U32(NAME) uint32_t NAME{}; parse_network_uint32(first, last, NAME); printf(#NAME " %u\n", NAME);
#define FIXED(NAME) int32_t NAME{}; parse_network_int32(first, last, NAME); printf(#NAME " %g\n", NAME * 0x1p-16);
#define I64(NAME) int64_t NAME{}; parse_network_int64(first, last, NAME); printf(#NAME " %lld\n", NAME);

    
    U16(majorVersion)
    U16(minorVersion)
    FIXED(fontRevision)
    U32(checksumAdjustment)
    U32(magicNumber) // "Set to 0x5F0F3CF5"
    U16(flags)
    U16(unitsPerEm)
    I64(created)
    I64(modified)
    I16(xMin)
    I16(yMin)
    I16(xMax)
    I16(yMax)
    U16(macStyle)
    U16(lowestRecPPEM)
    I16(fontDirectionHint) // "Deprecated (Set to 2)"
    I16(indexToLocFormat)
    I16(glyphDataFormat)



    

    
    

    return true;
}




int main(int argc, const char * argv[]) {
    
    auto [first, last] = mmap_path("/Users/antony/Desktop/assets/Futura Medium Condensed.otf");
    //auto [first, last] = mmap_path("/Users/antony/Desktop/assets/OpenSans-VariableFont_wdth,wght.ttf");
    
    
    {
        auto tableDirectory = OpenType::TableDirectory::from(wry::span{first, last});
        {
            auto head = OpenType::FontHeaderTable{tableDirectory["head"]};
            head.debug();

            auto hhea = OpenType::HorizontalHeaderTable{tableDirectory["hhea"]};
            hhea.debug();
            
            auto maxp = OpenType::MaximumProfileTable{tableDirectory["maxp"]};
            maxp.debug();
            
            auto hmtx = OpenType::HorizontalMetricsTable{
                hhea.numberOfHMetrics,
                maxp.numGlyphs,
                tableDirectory["hmtx"]};
            // hmtx.debug();
            
            auto OS_2 = OpenType::OS_2andWindowsMetricsTable{tableDirectory["OS/2"]};
            OS_2.debug();

            
        }
        {
            auto cff_ = wry::Reader{tableDirectory["CFF "]};
            auto base = cff_;
            auto header = CompactFontFormat::Header::from(cff_);
            auto name_INDEX = CompactFontFormat::INDEX::from(cff_);
            auto top_DICT_INDEX = CompactFontFormat::INDEX::from(cff_);
            auto string_INDEX = CompactFontFormat::INDEX::from(cff_);
            auto global_subr_INDEX = CompactFontFormat::INDEX::from(cff_);
            
            // Everything else is now Face-specific via top
            assert(name_INDEX.count == top_DICT_INDEX.count);
            for (int i = 0; i != name_INDEX.count; ++i) {
                auto top_DICT = CompactFontFormat::DICT::from(top_DICT_INDEX[i]);
                enum KEYS : uint8_t {
                    CHARSET = 15,
                    ENCODINGS = 15,
                    CHARSTRINGS = 17,
                    PRIVATE = 18,
                };
                auto p = top_DICT[PRIVATE];
                std::span s{base.s.data() + (size_t)p[1], (size_t)p[0]};
                auto private_DICT = CompactFontFormat::DICT::from(s);
                printf("    {\n");
                for (auto const& [k, v] : private_DICT.dictionary) {
                    printf("        \"%u,%u\" : [", k[0], k[1]);
                    for (auto a : v) {
                        printf(" %g,", a);
                    }
                    printf("],\n");
                }
                printf("    },\n");
                
                
            }
            
                        
            printf("\"Name\" : [\n");
            for (int i = 0; i != name_INDEX.count; ++i) {
                auto k = name_INDEX[i];
                printf("    \"%.*s\",\n", (int)k.size(), (char const*)k.data());
            }
            printf("],\n");
            printf("\"Top\" : [\n");
            for (int i = 0; i != top_DICT_INDEX.count; ++i) {
                auto top_DICT = CompactFontFormat::DICT::from(top_DICT_INDEX[i]);
                printf("    {\n");
                for (auto const& [k, v] : top_DICT.dictionary) {
                    printf("        \"%u,%u\" : [", k[0], k[1]);
                    for (auto a : v) {
                        printf(" %g,", a);
                    }
                    printf("],\n");
                }
                printf("    },\n");
                // printf("name[%d] = \"%.*s\"\n", i, (int)k.size(), (char const*)k.data());
            }
            printf("],\n");
            printf("\"String\" : [\n");
            for (int i = 0; i != string_INDEX.count; ++i) {
                auto k = string_INDEX[i];
                printf("    \"%.*s\",\n", (int)k.size(), (char const*)k.data());
            }
            printf("],\n");
            
        }
    }
    
    
    
    
    
    
    
    
    auto base = first;
    TableDirectory tableDirectory = {};
    parse_TableDirectory(first, last, tableDirectory);
    print_TableDirectory(tableDirectory);
    
    {
        Tag tag = {'h','e','a','d'};
        auto a = base + (int)(tableDirectory.tableRecords[tag].offset);
        auto b = a + (int)(tableDirectory.tableRecords[tag].length);
        parse_FontHeaderTable(a, b);
    }
    
    
    {
        Tag cmap_ = {'c','m','a','p'};
        auto a = base + (int)(tableDirectory.tableRecords[cmap_].offset);
        auto b = a + (int)(tableDirectory.tableRecords[cmap_].length);
        parse_cmapHeader(a, b);
        
    }
    
//    {
//        Tag tag = {'g','l','y','f'};
//        auto a = base + (int)(tableDirectory.tableRecords[tag].offset);
//        auto b = a + (int)(tableDirectory.tableRecords[tag].length);
//        parse_glyfHeader(a, b);
//    }
    
    
    Tag CFF_ = {'C','F','F',' '};
    auto first3 = base + (int)(tableDirectory.tableRecords[CFF_].offset);
    auto last3 = first3 + (int)(tableDirectory.tableRecords[CFF_].length);
    printf("CFF is %zd in size", last3 - first3);
    auto CFF_zero = first3;
    Header h;
    parse_Header(first3, last3, h);
    INDEX name;
    parse_INDEX(first3, last3, name);
    printf("\"Name\" : ");
    print_string_INDEX(name);
    printf("\"Top\"\n");
    INDEX top;
    parse_INDEX(first3, last3, top);
    INDEX string;
    parse_INDEX(first3, last3, string);
    printf("\"String\" : ");
    print_string_INDEX(string);
    INDEX globalSubr;
    parse_INDEX(first3, last3, globalSubr);
    printf("\"GlobalSubr\" : ");
    print_string_INDEX(globalSubr);
    
    // Parse the charstring table
    assert(top.size() == 1);
    DICT topd = {};
    {
        auto [first4, last4] = *top.begin();
        parse_DICT(first4, last4, topd);
        assert(first4 == last4);
        print_DICT(topd);
        
        
        INDEX Subrs_INDEX{};
        INDEX CharStrings_INDEX{};
        
        {
            KEY Private{18, 0};
            int Private_size = topd.data[Private][0];
            int Private_offset = topd.data[Private][1];
            printf("\"Private\" : %d %d\n", Private_size, Private_offset);
            auto a = CFF_zero + Private_offset;
            auto Private_DICT_zero = a;
            auto b = a + Private_size;
            DICT Private_DICT{};
            parse_DICT(a, b, Private_DICT);
            print_DICT(Private_DICT);
            
            KEY Subrs{19, 0};
            int Subrs_offset = Private_DICT.data[Subrs][0];
            a = Private_DICT_zero + Subrs_offset;
            parse_INDEX(a, last3, Subrs_INDEX);
            //            int i = 0;
            //            for (auto [a, b] : Subrs_INDEX) {
            //                Type2Charstring x{};
            //                printf("[%d] \n", i++);
            //                parse_Type2Charstring(a, b, x);
            //            }
        }
        {
            KEY CharStrings{17, 0};
            int CharStrings_offset = topd.data[CharStrings][0];
            printf("\"CharStrings\" : %d\n", CharStrings_offset);
            auto first5 = CFF_zero + CharStrings_offset;
            printf("%zd\n", last3 - first5);
            parse_INDEX(first5, last3, CharStrings_INDEX);
            //            int i = 0;
            //            for (auto [a, b] : CharStrings_INDEX) {
            //                Type2Charstring x{};
            //                printf("[%d] \n", i++);
            //                parse_Type2Charstring(a, b, x);
            //            }
        }
        
        // for (int i = 0; i != CharStrings_INDEX.size(); ++i) {
        int i = 50; //(int)CharStrings_INDEX.size() - 1;
        printf("[%d]\n", i);
        Type2CharstringEngine engine;
        engine.global_subroutines = globalSubr;
        engine.local_subroutines = Subrs_INDEX;
        auto [first, last] = CharStrings_INDEX[i];
        engine.execute(Span{first, last});
        // engine.raster();
        engine.raster2();
        // }
        
    }
    
    
    
    
    return EXIT_SUCCESS;
}




#if 0



namespace wry {
    
    /*
     
     
     
     
     bool parse_TableRecord(byte const*& first, byte const* last, TableRecord& victim) {
     auto first2 = first;
     return (
     parse_tag(first, last, victim.tableTag)
     && parse_integral(first, last, victim.checksum)
     && parse_integral(first, last, victim.offset)
     && parse_integral(first, last, victim.length)
     && ((void) (first = first2), true)
     );
     }
     
     bool parse(byte const*& first, byte const* last, TableDirectory& x) {
     auto first2 = first;
     if (!(parse_integral(first2, last, x.sfntVersion)
     && parse_integral(first2, last, x.numTables)
     && parse_integral(first2, last, x.searchRange)
     && parse_integral(first2, last, x.entrySelector)
     && parse_integral(first2, last, x.rangeShift)
     ))
     return false;
     std::vector<TableRecord>
     
     return true;
     }
     
     
     struct TableDirectory {
     };
     */
    
    
    
    struct TableDirectory {
        
        byte const* _origin;
        
        uint32_t sfntVersion() const {
            uint32_t x{};
            memcpy(&x, _origin + 0, sizeof(x));
            return ntohg(x);
        }
        
        uint16_t numTables() const {
            uint16_t x{};
            memcpy(&x, _origin + 4, sizeof(x));
            return ntohg(x);
        }
        
        struct TableRecord {
            
            byte const* _origin;
            
            std::array<char, 4> tableTag() const {
                std::array<char, 4> x;
                memcpy(&x, _origin + 0, 4);
                return x;
            }
            
            uint32_t offset() const {
                uint32_t x{};
                memcpy(&x, _origin + 8, sizeof(x));
                return ntohg(x);
            }
            
            uint32_t length() const {
                uint32_t x{};
                memcpy(&x, _origin + 12, sizeof(x));
                return ntohg(x);
            }
            
        };
        
        TableRecord tableRecord(int i) const {
            return TableRecord{_origin + 12 + i * 16};
        }
        
    };
    
    struct CompactFontFormat {
        
        byte const* _origin;
        ptrdiff_t _length;
        
        struct Header {
            
            byte const* _origin;
            
            uint8_t major() { return *(_origin + 0); }
            uint8_t minor() { return *(_origin + 1); }
            uint8_t hdrSize() { return *(_origin + 2); }
            uint8_t offSize() { return *(_origin + 3); }
            
        };
        
        Header header() const {
            return Header{_origin};
        }
        
        struct INDEX {
            
            std::vector<uint32_t> _offsets;
            byte const* _base;
            
            explicit INDEX(byte const* origin) {
                uint16_t count{};
                memcpy(&count, origin, 2);
                origin += 2;
                count = ntohg(count);
                _offsets.resize(count + 1);
                int offSize = *origin++;
                switch (offSize) {
                    case 1:
                        for (int i = 0; i != count + 1; ++i) {
                            _offsets[i] = *origin++;
                        }
                        break;
                    case 2:
                        for (int i = 0; i != count + 1; ++i) {
                            uint16_t x{};
                            memcpy(&x, origin, 2);
                            origin += 2;
                            _offsets[i] = ntohs(x);
                        }
                        break;
                    case 4:
                        for (int i = 0; i != count + 1; ++i) {
                            uint32_t x{};
                            memcpy(&x, origin, 4);
                            origin += 4;
                            _offsets[i] = ntohl(x);
                        }
                        break;
                    default:
                        abort();
                }
                _base = origin - 1;
            }
            
            std::span<const byte> data(int i) const {
                return std::span(_base + _offsets[i], _base + _offsets[i + 1]);
            }
            
            byte const* end() const {
                return _base + _offsets.back();
            }
            
        };
        
        struct DICT {
            
            std::map<uint16_t, std::vector<double>> dictionary;
            
            explicit DICT(std::span<const byte> s) {
                uint16_t key;
                std::vector<double> value;
                const byte* first = s.data();
                const byte* last = s.data() + s.size();
                
                while (first < last) {
                    byte b0 = *first++;
                    if (b0 <= 21) {
                        // operator
                        key = b0;
                        if (b0 == 12) {
                            byte b1 = *first++;
                            key = (key << 8) | b1;
                        }
                        dictionary.emplace(key, std::move(value));
                        value.clear();
                    } else if (b0 <= 27) {
                        abort(); // reserved
                    } else if (b0 <= 28) {
                        uint16_t x{};
                        memcpy(&x, first, 2);
                        first += 2;
                        value.push_back(ntohs(x));
                    } else if (b0 <= 29) {
                        uint32_t x{};
                        memcpy(&x, first, 4);
                        first += 4;
                        value.push_back(ntohl(x));
                    } else if (b0 <= 30) {
                        // real number
                    } else if (b0 <= 31) {
                        abort(); // reserved
                    } else if (b0 <= 246) {
                        value.push_back((int)b0 - 139);
                    } else if (b0 <= 250) {
                        byte b1 = *first++;
                        value.push_back((((int)b0 - 247) << 8) + (int)b1 + 108);
                    } else if (b0 <= 254) {
                        byte b1 = *first++;
                        value.push_back((-((int)b0 - 247) << 8) - (int)b1 - 108);
                    } else if (b0 <= 255) {
                        abort(); // reserved
                    }
                }
                
                
            }
            
        };
        
    };
    
    
    
    
    
    
}




#endif
