//
//  cff.cpp
//  client
//
//  Created by Antony Searle on 26/3/2026.
//

#include <cassert>
#include <charconv>
#include <concepts>
#include <map>
#include <string>
#include <vector>
#include <deque>

#include "cff.hpp"

#include "NetworkToHostReader.hpp"
#include "simd.hpp"

namespace wry::cff {
    
    struct Header {
        
#define Y \
X(uint8_t, major) \
X(uint8_t, minor) \
X(uint8_t, hdrSize) \
X(uint8_t, offSize)
        
#define X(A, B) A B;
        Y
#undef X
        
        void debug() {
            printf("    Header {\n");
#define X(A, B) printf("        " #B " = %d,\n", B);
            Y
#undef X
            printf("    },\n");
        }
        
    }; // struct HEADER
    
    void parse_Header(span<byte const>& s, Header& x) {
#define X(A, B) parse_ntoh(s, x.B);
        Y
#undef X
        
#undef Y
    }
    
    struct INDEX {
        
        uint16_t count;
        uint8_t offSize;
        byte const* offsets;
        
        template<typename T>
        span<byte const> _dispatch(size_t i) const {
            assert(i < count);
            byte const* src = offsets + i * sizeof(T);
            T j[2] = {};
            // we can't assume that the data is aligned
            std::memcpy(j, src, sizeof(T) * 2);
            byte const* base = offsets + (count + 1) * sizeof(T) - 1;
            
            // printf("%d, %d\n", ntoh(j[0]), ntoh(j[1]));
            return { base + ntoh(j[0]), base + ntoh(j[1]) };
        }
        
        span<byte const> operator[](size_t i) const {
            if (i >= count)
                abort();
            switch (offSize) {
                case 1:
                    return _dispatch<uint8_t>(i);
                case 2:
                    return _dispatch<uint16_t>(i);
                case 4:
                    return _dispatch<uint32_t>(i);
                default:
                    abort();
            }
        }
        
        void debug() {
            printf("INDEX { count = %d, offSize = %d }\n", count, offSize);
        }
        
    }; // struct INDEX
    
    
    void parse_INDEX(span<const byte>& s, INDEX& x) {
        parse_ntoh(s, x.count);
        if (x.count) {
            parse_ntoh(s, x.offSize);
            assert((x.offSize == 1) ||
                   (x.offSize == 2) ||
                   (x.offSize == 4));
            x.offsets = s.begin();
            // set the span to begin after the last entry
            s._begin = x[x.count - 1]._end;
        }
    }
    
    struct DICT {
        
        std::map<std::array<uint8_t, 2>, std::vector<double>> dictionary;
        
        std::span<double const> operator[](int i, int j = 0) const {
            auto it = dictionary.find(std::array<uint8_t, 2>{(uint8_t)i, (uint8_t)j});
            if (it != dictionary.end()) {
                return {it->second.data(), it->second.size()};
            } else {
                return {};
            }
        }
        
        static void parse_real(span<byte const>& s, double& x) {
            const size_t N = 32;
            char buffer[N] = {};
            char* dst = buffer;
            char* guard = dst + N - 3;
            constexpr static char const* table[] = {
                "0", "1", "2", "3", "4", "5", "6", "7", "8", "9",
                ".", "E", "-E", "_", "-", "_"
            };
            
            for (;;) {
                byte b0 = 0;
                parse_ntoh(s, b0);
                int nibble;
                nibble = b0 >> 4;
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
            std::from_chars_result result = std::from_chars(buffer, dst, x);
            // assert((result.ptr == dst));
        }
        
    }; // struct DICT
    
    
    void parse_DICT(span<const byte> s, DICT& x) {
        
        std::array<uint8_t, 2> key;
        std::vector<double> value;
        
        while (!s.empty()) {
            byte b0 = 0;
            parse_ntoh(s, b0);
            if (b0 <= 21) {
                // operator
                key[0] = b0;
                if (b0 == 12) {
                    byte b1 = 0;
                    parse_ntoh(s, b1);
                    key[1] = b1;
                }
                // printf("key = %d %d\n", key[0], key[1]);
                x.dictionary.emplace(key, std::move(value));
                key[0] = 0; key[1] = 0;
                value.clear();
            } else if (b0 <= 27) {
                abort(); // reserved
            } else if (b0 <= 28) {
                uint16_t u{};
                parse_ntoh(s, u);
                value.push_back(u);
            } else if (b0 <= 29) {
                uint32_t u{};
                parse_ntoh(s, u);
                value.push_back(u);
            } else if (b0 <= 30) {
                double u{};
                DICT::parse_real(s, u);
                value.push_back(u);
            } else if (b0 <= 31) {
                abort(); // reserved
            } else if (b0 <= 246) {
                value.push_back((int)b0 - 139);
            } else if (b0 <= 250) {
                byte b1 = 0;
                parse_ntoh(s, b1);
                value.push_back((((int)b0 - 247) << 8) + (int)b1 + 108);
            } else if (b0 <= 254) {
                byte b1 = 0;
                parse_ntoh(s, b1);
                value.push_back((-((int)b0 - 247) << 8) - (int)b1 - 108);
            } else if (b0 <= 255) {
                abort(); // reserved
            }
        }
        if (!value.empty())
            abort();
    }
    
    
    
    struct Type2CharstringEngine {
        
        INDEX global_subroutines;
        INDEX local_subroutines;
        
        std::deque<double> argument_queue;
        std::vector<wry::span<byte const>> call_stack;
        bool is_not_first_argument_queue_clearing_operator = false;
        double width = 0.0;
        simd_double2 pen = {};
        uint8_t mode = {};
        
        enum {
            MOVE,
            LINE,
            BEZIER,
        };
        
        // std::vector<double> hstem;
        // std::vector<double> vstem;
        int nhstem = 0;
        int nvstem = 0;
        
        std::vector<simd_double2> points;
        std::vector<uint8_t> modes;
        
        // TODO: clumsy
        void reset() {
            assert(argument_queue.empty());
            // assert(cs.empty());
            is_not_first_argument_queue_clearing_operator = false;
            pen = {};
            mode = 0;
            nhstem = 0;
            nvstem = 0;
            points.clear();
            modes.clear();
        }
        
        void maybe_width() {
            if (!is_not_first_argument_queue_clearing_operator) {
                is_not_first_argument_queue_clearing_operator = true;
                if (!argument_queue.empty()) {
                    // printf("(: width) ");
                    width = argument_queue.front();
                    argument_queue.pop_front();
                }
            }
        }
        
        void maybe_width_if_odd() {
            if (argument_queue.size() & 1)
                maybe_width();
        }
        
        void maybe_width_if_even() {
            if (!(argument_queue.size() & 1))
                maybe_width();
        }
        
        void push() {
            points.push_back(pen);
            modes.push_back(mode);
        }
        
        void dx() {
            pen.x += argument_queue.front(); argument_queue.pop_front();
            push();
        }
        
        void dy() {
            pen.y += argument_queue.front(); argument_queue.pop_front();
            push();
        }
        
        void dxy() {
            pen.x += argument_queue.front(); argument_queue.pop_front();
            pen.y += argument_queue.front(); argument_queue.pop_front();
            push();
        }
        
        void do_hstem() {
            while (!argument_queue.empty()) {
                assert(argument_queue.size() >= 2);
                ++nhstem; argument_queue.pop_front();
                ++nhstem; argument_queue.pop_front();
            }
        }
        
        void do_vstem() {
            while (!argument_queue.empty()) {
                assert(argument_queue.size() >= 2);
                ++nvstem; argument_queue.pop_front();
                ++nvstem; argument_queue.pop_front();
            }
        }
        
        void do_mask(wry::span<byte const>& str) {
            assert(!(nhstem & 1));
            assert(!(nvstem & 1));
            int number_of_bytes = (int)((nhstem + nvstem + 15) / 16);
            while (number_of_bytes--) {
                printf(" %#02hhx", str.pop_front());
            }
            printf("\n");
        }
        
        
        
        bool execute(std::span<byte const> str);
        
        struct tmp {
            struct Bezier1 {
                simd_double2 x[2];
                static Bezier1 fromLine(...);
            };
            struct Bezier2 {
                simd_double2 x[3];
                static Bezier2 fromLine(...);
            };
            struct Bezier3 {
                simd_double2 x[4];
                static Bezier3 fromLine(...);
            };
        };
        
        std::vector<tmp::Bezier2> to_Bezier2_list() const {
            std::vector<tmp::Bezier2> result;
            
            assert(points.size() == modes.size());
            size_t i = 0;
            size_t j = 0; // index of curve-closing point
            for (; i != points.size(); ++i) {
                switch (modes[i]) {
                    case MOVE:
                        if (i != 0) {
                            // we need to close the curve
                            result.push_back(tmp::Bezier2::fromLine(points[i-1], points[j]));
                            j = i;
                        }
                        break;
                    case LINE: {
                        assert(i != 0);
                        result.push_back(tmp::Bezier2::fromLine(points[i-1], points[i]));
                    } break;
                    case BEZIER: {
                        assert(i != 0);
                        auto c = tmp::Bezier3{
                            points[i-1],
                            points[i+0],
                            points[i+1],
                            points[i+2]
                        };
                        // auto [a, b] = tmp::reduce_parametric_continuity(c);
                        //auto [a, b, d, e] = tmp::reduce_on_curve_points_MORE(c);
                        // result.push_back(a);
                        // result.push_back(b);
                        //result.push_back(d);
                        //result.push_back(e);
                        i += 2;
                    } break;
                }
            }
            if (i) {
                // close the curve with a line
                result.push_back(tmp::Bezier2::fromLine(points[i-1], points[j]));
            }
            return result;
        }
        
        std::vector<tmp::Bezier3> to_Bezier3_list() const {
            std::vector<tmp::Bezier3> result;
            
            assert(points.size() == modes.size());
            size_t i = 0;
            size_t j = 0; // index of curve-closing point
            for (; i != points.size(); ++i) {
                switch (modes[i]) {
                    case MOVE:
                        if (i != 0) {
                            // we need to close the curve
                            result.push_back(tmp::Bezier3::fromLine(points[i-1], points[j]));
                            j = i;
                        }
                        break;
                    case LINE: {
                        assert(i != 0);
                        result.push_back(tmp::Bezier3::fromLine(points[i-1], points[i]));
                    } break;
                    case BEZIER: {
                        assert(i != 0);
                        result.push_back(tmp::Bezier3{
                            points[i-1],
                            points[i+0],
                            points[i+1],
                            points[i+2]
                        });
                        i += 2;
                    } break;
                }
            }
            if (i) {
                // close the curve with a line
                result.push_back(tmp::Bezier3::fromLine(points[i-1], points[j]));
            }
            return result;
        }
        
    }; // struct Type2CharstringEngine
        
    bool Type2CharstringEngine::execute(std::span<byte const> str) {
        reset();
        wry::Reader r{{ str.data(), str.size()}};
        while (!r.s.empty()) {
            uint8_t b0 = r.read<uint8_t>();
            // printf("<%d>", b0);
            if ((b0 <= 31) && (b0 != 28)) {
                switch (b0) {
                    case 1: {
                        maybe_width_if_odd();
                        // printf(": hstem\n");
                        do_hstem();
                        assert(argument_queue.empty());
                        break;
                    }
                    case 3: {
                        maybe_width_if_odd();
                        // printf(": vstem\n");
                        do_vstem();
                        assert(argument_queue.empty());
                        break;
                    }
                    case 4: {
                        maybe_width_if_even();
                        // printf(": vmoveto\n");
                        mode = MOVE;
                        dy();
                        assert(argument_queue.empty());
                        break;
                    }
                    case 5: {
                        // printf(": rlineto\n");
                        mode = LINE;
                        do  {
                            dxy();
                        } while ((!argument_queue.empty()));
                        break;
                    }
                    case 6: {
                        // printf(": hlineto\n");
                        int parity = 0;
                        mode = LINE;
                        do {
                            if (parity)
                                pen.y += argument_queue.front();
                            else
                                pen.x += argument_queue.front();
                            argument_queue.pop_front();
                            push();
                            parity ^= 1;
                        } while (!argument_queue.empty());
                        break;
                    }
                    case 7: {
                        // printf(": vlineto\n");
                        int parity = 1;
                        mode = LINE;
                        do {
                            if (parity)
                                pen.y += argument_queue.front();
                            else
                                pen.x += argument_queue.front();
                            argument_queue.pop_front();
                            push();
                            parity ^= 1;
                        } while (!argument_queue.empty());
                        break;
                    }
                    case 8: {
                        // printf(": rrcurveto\n");
                        mode = BEZIER;
                        do {
                            dxy(); dxy(); dxy();
                        } while (!argument_queue.empty());
                        break;
                    }
                    case 10: { // callsubr
                               // printf(": callsubr\n");
                        int i = (int)argument_queue.back() + 107;
                        argument_queue.pop_back();
                        call_stack.push_back(r.s);
                        r.s = local_subroutines[i];
                        break;
                    }
                    case 11: { // return
                               // printf(": return\n");
                        assert(r.s.empty());
                        r.s = call_stack.back(); call_stack.pop_back();
                        break;
                    }
                    case 14: // endchar
                        maybe_width_if_odd();
                        // printf(": endchar\n");
                        assert(argument_queue.empty());
                        for (auto s : call_stack)
                            assert(s.empty());
                        // print_result();
                        // render_result();
                        return true;
                    case 18: { // hstemhm
                        maybe_width_if_odd();
                        // printf(": hstemhm\n");
                        do_hstem();
                        break;
                    }
                    case 19: { // hintmask
                        maybe_width_if_odd();
                        if (!argument_queue.empty()) {
                            // printf("(: vstem) ");
                            do_vstem();
                        }
                        // printf(": hintmask");
                        do_mask(r.s);
                        assert(argument_queue.empty());
                        break;
                    }
                    case 20: { // cntrmask
                        maybe_width_if_odd();
                        if (!argument_queue.empty()) {
                            // printf("(: vstem) ");
                            do_vstem();
                        }
                        // printf(": cntrmask");
                        do_mask(r.s);
                        assert(argument_queue.empty());
                        break;
                    }
                    case 21: {
                        maybe_width_if_odd();
                        // printf(": rmoveto\n");
                        mode = MOVE;
                        dxy();
                        assert(argument_queue.empty());
                        break;
                    }
                    case 22: {
                        maybe_width_if_even();
                        // printf(": hmoveto\n");
                        mode = MOVE;
                        dx();
                        assert(argument_queue.empty());
                        break;
                    }
                    case 23: {
                        maybe_width_if_odd();
                        // printf(": vstemhm\n");
                        do_vstem();
                        assert(argument_queue.empty());
                        break;
                    }
                    case 24: {
                        // printf(": rcurveline\n");
                        mode = BEZIER;
                        do {
                            dxy(); dxy(); dxy();
                        } while (argument_queue.size() >= 6);
                        mode = LINE;
                        dxy();
                        assert(argument_queue.empty());
                        break;
                    }
                    case 25: {
                        // printf(": rlinecurve\n");
                        mode = LINE;
                        do {
                            dxy();
                        } while (argument_queue.size() > 6);
                        mode = BEZIER;
                        dxy(); dxy(), dxy();
                        break;
                    }
                    case 26: {
                        // printf(": vvcurveto\n");
                        if (argument_queue.size() & 1) {
                            pen.x += argument_queue.front(); argument_queue.pop_front();
                        }
                        mode = BEZIER;
                        do {
                            dy(); dxy(); dy();
                        } while (!argument_queue.empty());
                        break;
                    }
                    case 27: {
                        // printf(": hhcurveto\n");
                        if (argument_queue.size() & 1) {
                            pen.y += argument_queue.front(); argument_queue.pop_front();
                        }
                        mode = BEZIER;
                        do {
                            dx(); dxy(); dx();
                        } while (!argument_queue.empty());
                        assert(argument_queue.empty());
                        break;
                    }
                    case 30: {
                        // printf(": vhcurveto\n");
                        bool parity = 1;
                        mode = BEZIER;
                        do {
                            assert(argument_queue.size() >= 4);
                            if (parity == 0) {
                                dx(); dxy();
                                pen.y += argument_queue.front(); argument_queue.pop_front();
                                if (argument_queue.size() == 1) {
                                    pen.x += argument_queue.front(); argument_queue.pop_front();
                                }
                            } else {
                                dy(); dxy();
                                pen.x += argument_queue.front(); argument_queue.pop_front();
                                if (argument_queue.size() == 1) {
                                    pen.y += argument_queue.front(); argument_queue.pop_front();
                                }
                            }
                            push();
                            parity = parity ^ 1;
                        } while (!argument_queue.empty());
                        break;                    }
                    case 31: {
                        // printf(": hvcurveto\n");
                        bool parity = 0;
                        mode = BEZIER;
                        do {
                            assert(argument_queue.size() >= 4);
                            if (parity == 0) {
                                dx(); dxy();
                                pen.y += argument_queue.front(); argument_queue.pop_front();
                                if (argument_queue.size() == 1) {
                                    pen.x += argument_queue.front(); argument_queue.pop_front();
                                }
                            } else {
                                dy(); dxy();
                                pen.x += argument_queue.front(); argument_queue.pop_front();
                                if (argument_queue.size() == 1) {
                                    pen.y += argument_queue.front(); argument_queue.pop_front();
                                }
                            }
                            push();
                            parity = parity ^ 1;
                        } while (!argument_queue.empty());
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
                // printf("%g ",number);
                argument_queue.push_back(number);
            }
        }
        if (!argument_queue.empty()) {
            printf("Missing operator??\n");
        }
        printf("Missing endchar??\n");
        abort();
        
    }
    
    
    void* parse(byte const* first, byte const* last) {
        
        printf("parsing .cff of %zd bytes\n", last - first);

        span s{first, last};
        span reset = s;
        auto r = wry::Reader{s};

        
        Header header; parse_Header(s, header); header.debug();
        INDEX name_INDEX; parse_INDEX(s, name_INDEX); name_INDEX.debug();
        for (int i = 0; i != name_INDEX.count; ++i) {
            auto t = name_INDEX[i];
            printf("\"%.*s\"\n", (int)t.size(), (char const*)t._begin);
        }
        INDEX top_DICT_INDEX; parse_INDEX(s, top_DICT_INDEX); top_DICT_INDEX.debug();
        INDEX string_INDEX; parse_INDEX(s, string_INDEX); string_INDEX.debug();
        for (int i = 0; i != string_INDEX.count; ++i) {
            auto t = string_INDEX[i];
            printf("\"%.*s\"\n", (int)t.size(), (char const*)t._begin);
        }INDEX global_subr_INDEX; parse_INDEX(s, global_subr_INDEX); global_subr_INDEX.debug();
        
        DICT top_DICT; {
            span t{top_DICT_INDEX[0]};
            parse_DICT(t, top_DICT);
        }
                
        enum KEYS : uint8_t {
            // top
            CHARSET = 15,
            ENCODINGS = 15,
            CHARSTRINGS = 17,
            PRIVATE = 18,
            // private
            SUBRS = 19,
        };
        std::span<double const> values;
        
        // Get CharStrings offset(0)
        values = top_DICT[CHARSTRINGS];
        assert(values.size() == 1);
        printf("CharStrings @ %g\n", values[0]);
        s = reset;
        r.s = s;
        r.skip((std::size_t)values[0]);
        INDEX charstrings_INDEX; parse_INDEX(r.s, charstrings_INDEX); charstrings_INDEX.debug();
        
        // Get PrivateDict size and offset(0)
        values = top_DICT[PRIVATE];
        assert(values.size() == 2);
        printf("PrivateDict(%g) @ %g\n", values[1], values[0]);
        auto private_DICT_s = wry::span{
            s.data() + (std::size_t)values[1],
            (std::size_t)values[0]
        };
        DICT private_DICT; parse_DICT(private_DICT_s, private_DICT);
        
        values = private_DICT[SUBRS];
        printf("PrivateDict @ %g\n", values[0]);
        r.s = { private_DICT_s._begin, s._end };
        r.skip((std::size_t)values[0]);
        INDEX local_subr_INDEX; parse_INDEX(r.s, local_subr_INDEX); local_subr_INDEX.debug();
        
        Type2CharstringEngine e;
        e.global_subroutines = global_subr_INDEX;
        e.local_subroutines = local_subr_INDEX;
        
        for (int i = 0; i != charstrings_INDEX.count; ++i) {
            e.execute(charstrings_INDEX[i]);
            printf("e.points.size() = %zd\n", e.points.size());
        }
        
        
//        std::vector<Type2CharstringEngine::tmp::Bezier2> g;
//        simd_double2 pen = {};
//        std::size_t maxf = 0;
//        
        // OTF font head table circular dependency?)
//        auto xy_min = simd_make_double2(head.xMin, head.yMin);
//        auto xy_max = simd_make_double2(head.xMax, head.yMax);
//        double scale = 1.0;
//        {
//            xy_max -= xy_min;
//            while (xy_max.y * scale > 1/8.0) {
//                scale *= 0.5;
//            }
//            xy_min *= scale;
//            xy_max *= scale;
//            xy_min -= (1.0/16 - xy_max) * 0.5;
//            
//        }
//        
//        
//        int jumps[256] = {};
//        
//        for (int i = 33; i != 127; ++i) {
        
//            auto j = cmap.bmp.lookup(i);
//            // printf("'%c' -> %d\n", i, j);
//            e.execute(charstrings_INDEX[j]);

//            auto f = e.to_Bezier2_list();
//            for (auto& b : f) {
//                b.a *= scale;
//                b.b *= scale;
//                b.c *= scale;
//                b.a -= xy_min;
//                b.b -= xy_min;
//                b.c -= xy_min;
//            }
//            //            {
//            //                auto n = f.size();
//            //                std::erase_if(f, [](auto const& b){
//            //                    return b.a.y == b.c.y;
//            //                });
//            //            }
//            if (f.size() > maxf) {
//                printf("largest char %c with %zd\n", i, f.size());
//            }
//            maxf = std::max(maxf, f.size());
//            pen.x = (i % 16) / 16.0;
//            pen.y = (i / 16) / 8.0 + 1/32.0;
//            jumps[i] = (int)g.size();
//            for (auto b : f) {
//                g.push_back(b + pen);
//            }
//            //printf("%hx\n", hmtx.lookup(j).advanceWidth);
//            //            pen.x += hmtx.lookup(j).advanceWidth + hmtx.lookup(0).advanceWidth;
//            //            if (pen.x > 7000) {
//            //                pen.x = 0;
//            //                pen.y -= OS_2.sTypoAscender - OS_2.sTypoDescender + OS_2.sTypoLineGap;
//            //            }
//        }
        
//        printf("allf is %zd\n", g.size());
//        printf("maxf is %zd\n", maxf);
        
//        debug_dump_Bezier2_list(g);
//        
//        debug_raster_bezier_list(g, jumps);
        
        return nullptr;
        
    }
    
    
} // namespace wry::CFF
