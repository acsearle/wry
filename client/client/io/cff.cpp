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
    
#define CHECK(x) do { if(!(x)) [[unlikely]] std::abort(); } while(0)
    
    using Card8 = std::uint8_t;
    using Card16 = NetworkByteOrder<std::uint16_t>;
    using Offset8 = std::uint8_t;
    using Offset16 = NetworkByteOrder<std::uint16_t>;
    using Offset32 = NetworkByteOrder<std::uint32_t>;
    using OffSize = std::uint8_t;
    using SID = NetworkByteOrder<std::uint16_t>;
    
    
#define Y \
X(Card8, major) \
X(Card8, minor) \
X(Card8, hdrSize) \
X(OffSize, offSize)

    struct Header {
                
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
    }
#undef Y

    
    struct INDEX {
        
        uint16_t count;
        uint8_t offSize;
        byte const* offsets;
        
        uint32_t parse_offset(byte const*& p) const {
            uint32_t result{};
            for (int i = 0; i != offSize; ++i)
                result = (result << 8) | *p++;
            return result;
        }
        
        span<byte const> operator[](size_t i) const {
            CHECK(i < count);
            CHECK((1 <= offSize) && (offSize <= 4));
            byte const* p = offsets + i * offSize;
            uint32_t a = parse_offset(p);
            uint32_t b = parse_offset(p);
            byte const* q = offsets + (count + 1) * offSize - 1;
            return { q + a, q + b };
        }
        
        void debug() {
            printf("INDEX { count = %d, offSize = %d }\n", count, offSize);
        }
        
    }; // struct INDEX
    
    
    void parse_INDEX(span<const byte>& s, INDEX& x) {
        parse_ntoh(s, x.count);
        if (x.count) {
            parse_ntoh(s, x.offSize);
            CHECK((1 <= x.offSize) && (x.offSize <= 4));
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
        
        static void parse_real(span<byte const>& s, double& target) {
            constexpr static char const* table[] = {
                "0", "1", "2", "3", "4", "5", "6", "7", "8", "9",
                ".", "E", "E-", "", "-", ""
            };
            std::string buffer;
            for (;;) {
                byte b0 = 0;
                parse_ntoh(s, b0);
                int nibble;
                nibble = (b0 >> 4) & 0xF;
                CHECK(nibble != 0xD); // reserved
                if (nibble == 0xF) {
                    CHECK((b0 & 0xF) == 0xF);
                    break;
                }
                buffer.append(table[nibble]);
                nibble = b0 & 0xf;
                CHECK(nibble != 0xD);
                if (nibble == 0xf)
                    break;
                buffer.append(table[nibble]);
            }
            char const* first = buffer.data();
            char const* last = first + buffer.size();
            std::from_chars_result result = std::from_chars(first, last, target);
            CHECK(result.ptr == last);
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
            } else if (b0 <= 27) [[unlikely]] {
                CHECK(false); // reserved
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
            } else if (b0 <= 31) [[unlikely]] {
                CHECK(false); // reserved
            } else if (b0 <= 246) {
                value.push_back((int)b0 - 139);
            } else if (b0 <= 250) {
                byte b1 = 0;
                parse_ntoh(s, b1);
                value.push_back((((int)b0 - 247) << 8) + (int)b1 + 108);
            } else if (b0 <= 254) {
                byte b1 = 0;
                parse_ntoh(s, b1);
                value.push_back((-((int)b0 - 251) << 8) - (int)b1 - 108);
            } else if (b0 <= 255)  [[unlikely]] {
                CHECK(false); // reserved
            }
        }
        if (!value.empty())
            CHECK(false);
    }
    
    
    
    struct Type2CharstringEngine {
        
        INDEX global_subroutines;
        INDEX local_subroutines;
        
        std::deque<float> argument_queue;
        std::vector<wry::span<byte const>> call_stack;
        
        // NOTE: This awkward double negative name is suggested by the CFF
        // specification
        bool is_not_first_argument_queue_clearing_operator = false;
        
        // NOTE: CFF width is not returned to the caller, because the
        // OpenType layer above provides its own authoritative value.
        float width = 0.0;
        
        simd_float2 pen = {};
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
        
        std::vector<simd_float2> points;
        std::vector<uint8_t> modes;
        
        // TODO: clumsy
        void reset() {
            // DEBUG: We want to know if reset is discarding unused arguments
            // or partial call stacks
            CHECK(argument_queue.empty());
            CHECK(call_stack.empty());
            argument_queue.clear();
            call_stack.clear();
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
        
        float pop_arg() {
            CHECK(!argument_queue.empty());
            float a = argument_queue.front();
            argument_queue.pop_front();
            return a;
        }

        float pop_arg_back() {
            CHECK(!argument_queue.empty());
            float a = argument_queue.back();
            argument_queue.pop_back();
            return a;
        }
        
        void shift_x() {
            pen.x += pop_arg();
        }
        
        void shift_y() {
            pen.y += pop_arg();
        }
        
        void if_odd_shift(bool to_x) {
            if (argument_queue.size() & 1) {
                if (to_x)
                    shift_x();
                else
                    shift_y();
            }
        }

        
        void push() {
            points.push_back(pen);
            modes.push_back(mode);
        }

        void dx() {
            shift_x(); push();
        }
        
        void dy() {
            shift_y(); push();
        }
        
        void dxy() {
            shift_x(); shift_y(); push();
        }
        
        void do_stem(int& counter) {
            while (argument_queue.size() >= 2) {
                ++counter; argument_queue.pop_front();
                ++counter; argument_queue.pop_front();
            }
            CHECK(argument_queue.empty());
        }
                
        void do_mask(wry::span<byte const>& str) {
            CHECK(!(nhstem & 1));
            CHECK(!(nvstem & 1));
            int number_of_bytes = (int)((nhstem + nvstem + 15) / 16);
            // Discard hinting information (unused by our renderer)
            str.drop_front(number_of_bytes);
        }
        
        void call_subroutine(INDEX const& subroutines, wry::Reader& r) {
            int i = (int)pop_arg_back();
            int bias = (subroutines.count < 1240 ? 107
                        : (subroutines.count < 33900 ? 1131
                           : 32768));
            call_stack.push_back(r.s);
            r.s = subroutines[i + bias];
        }
        
        void do_alternating_curves(bool start_with_x) {
            // VHCURVETO is start_with_x = false; HVCURVETO is start_with_x = true.
            mode = BEZIER;
            do {
                if (start_with_x) {
                    dx(); dxy();
                    shift_y();
                    if (argument_queue.size() == 1) shift_x();
                } else {
                    dy(); dxy();
                    shift_x();
                    if (argument_queue.size() == 1) shift_y();
                }
                push();
                start_with_x = !start_with_x;
            } while (!argument_queue.empty());
        }
        
        void do_alternating_lineto(bool start_with_x) {
            // HLINETO is start_with_x = true; VLINETO is start_with_x = false.
            mode = LINE;
            do {
                if (start_with_x) shift_x(); else shift_y();
                push();
                start_with_x = !start_with_x;
            } while (!argument_queue.empty());
        }
        
        bool execute(std::span<byte const> str);
        
        static BezierCurve<4> fromLine(simd_float2 a, simd_float2 b) {
            simd_float2 aab = simd_mix(a, b, 1.0f / 3.0f);
            simd_float2 abb = simd_mix(a, b, 2.0f / 3.0f);
            return BezierCurve<4>{{a,aab,abb,b}};
        }
        
        std::vector<BezierCurve<4>> to_Bezier_list() const {
            std::vector<BezierCurve<4>> result;
            
            CHECK(points.size() == modes.size());
            size_t i = 0;
            size_t j = 0; // index of curve-closing point
            for (; i != points.size(); ++i) {
                switch (modes[i]) {
                    case MOVE:
                        if (i != 0) {
                            // we need to close the curve
                            result.push_back(fromLine(points[i-1], points[j]));
                            j = i;
                        }
                        break;
                    case LINE: {
                        CHECK(i != 0);
                        result.push_back(fromLine(points[i-1], points[i]));
                    } break;
                    case BEZIER: {
                        CHECK(i != 0);
                        result.push_back(BezierCurve<4>{
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
                result.push_back(fromLine(points[i-1], points[j]));
            }
            return result;
        }
        
    }; // struct Type2CharstringEngine
    
    enum  {
        HSTEM = 1,
        VSTEM = 3,
        VMOVETO,
        RLINETO,
        HLINETO,
        VLINETO,
        RRCURVETO,
        CALLSUBR = 10,
        RETURN,
        ESCAPE,
        ENDCHAR = 14,
        HSTEMHM = 18,
        HINTMASK,
        CNTRMASK,
        RMOVETO,
        HMOVETO,
        VSTEMHM,
        RCURVELINE,
        RLINECURVE,
        VVCURVETO,
        HHCURVETO,
        SHORTINT,
        CALLGSUBR,
        VHCURVETO,
        HVCURVETO,
    };
        
    bool Type2CharstringEngine::execute(std::span<byte const> str) {
        reset();
        wry::Reader r{{ str.data(), str.size()}};
        while (!r.s.empty()) {
            uint8_t b0 = r.read<uint8_t>();
            // printf("<%d>", b0);
            if ((b0 <= 31) && (b0 != 28)) {
                switch (b0) {
                    case HSTEM: {
                        maybe_width_if_odd();
                        do_stem(nhstem);
                        CHECK(argument_queue.empty());
                        break;
                    }
                    case VSTEM: {
                        maybe_width_if_odd();
                        do_stem(nvstem);
                        CHECK(argument_queue.empty());
                        break;
                    }
                    case VMOVETO: {
                        maybe_width_if_even();
                        mode = MOVE;
                        dy();
                        CHECK(argument_queue.empty());
                        break;
                    }
                    case RLINETO: {
                        mode = LINE;
                        do  {
                            dxy();
                        } while ((!argument_queue.empty()));
                        break;
                    }
                    case HLINETO: {
                        do_alternating_lineto(true);
                        break;
                    }
                    case VLINETO: {
                        do_alternating_lineto(false);
                        break;
                    }
                    case RRCURVETO: {
                        mode = BEZIER;
                        do {
                            dxy(); dxy(); dxy();
                        } while (!argument_queue.empty());
                        break;
                    }
                    case CALLSUBR: {
                        call_subroutine(local_subroutines, r);
                        break;
                    }
                    case RETURN: {
                        CHECK(r.s.empty());
                        r.s = call_stack.back(); call_stack.pop_back();
                        break;
                    }
                    case ENDCHAR: {
                        maybe_width_if_odd();
                        CHECK(argument_queue.empty());
                        for (auto s : call_stack)
                            CHECK(s.empty());
                        return true;
                    }
                    case HSTEMHM: { // hstemhm
                        maybe_width_if_odd();
                        do_stem(nhstem);
                        break;
                    }
                    case HINTMASK: {
                        maybe_width_if_odd();
                        if (!argument_queue.empty()) {
                            do_stem(nvstem);
                        }
                        do_mask(r.s);
                        CHECK(argument_queue.empty());
                        break;
                    }
                    case CNTRMASK: {
                        maybe_width_if_odd();
                        if (!argument_queue.empty()) {
                            do_stem(nvstem);
                        }
                        do_mask(r.s);
                        CHECK(argument_queue.empty());
                        break;
                    }
                    case RMOVETO: {
                        maybe_width_if_odd();
                        mode = MOVE;
                        dxy();
                        CHECK(argument_queue.empty());
                        break;
                    }
                    case HMOVETO: {
                        maybe_width_if_even();
                        mode = MOVE;
                        dx();
                        CHECK(argument_queue.empty());
                        break;
                    }
                    case VSTEMHM: {
                        maybe_width_if_odd();
                        do_stem(nvstem);
                        CHECK(argument_queue.empty());
                        break;
                    }
                    case RCURVELINE: {
                        mode = BEZIER;
                        do {
                            dxy(); dxy(); dxy();
                        } while (argument_queue.size() >= 6);
                        mode = LINE;
                        dxy();
                        CHECK(argument_queue.empty());
                        break;
                    }
                    case RLINECURVE: {
                        mode = LINE;
                        do {
                            dxy();
                        } while (argument_queue.size() > 6);
                        mode = BEZIER;
                        dxy(); dxy(); dxy();
                        break;
                    }
                    case VVCURVETO: {
                        if_odd_shift(true);
                        mode = BEZIER;
                        do {
                            dy(); dxy(); dy();
                        } while (!argument_queue.empty());
                        break;
                    }
                    case HHCURVETO: {
                        if_odd_shift(false);
                        mode = BEZIER;
                        do {
                            dx(); dxy(); dx();
                        } while (!argument_queue.empty());
                        break;
                    }
                    case CALLGSUBR: {
                        call_subroutine(global_subroutines, r);
                        break;
                    }
                    /* case SHORTINT: */
                    case VHCURVETO: {
                        do_alternating_curves(false);
                        break;
                    }
                    case HVCURVETO: {
                        do_alternating_curves(true);
                        break;
                    }
                    default:
                        printf(": Unhandled b0 = %d\n", b0);
                        CHECK(false);
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
                } else { CHECK(b0 == 255);
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
        CHECK(false);
        
    }
    
    
    std::map<int, std::vector<BezierCurve<4>>> parse(byte const* first, byte const* last) {
        
        std::map<int, std::vector<BezierCurve<4>>> result;
        
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
        }
        INDEX global_subr_INDEX; parse_INDEX(s, global_subr_INDEX); global_subr_INDEX.debug();
        
        DICT top_DICT; {
            span t{top_DICT_INDEX[0]};
            parse_DICT(t, top_DICT);
        }
                
        enum KEYS : uint8_t {
            // top
            CHARSET = 15,
            ENCODINGS = 16,
            CHARSTRINGS = 17,
            PRIVATE = 18,
            // private
            SUBRS = 19,
        };
        std::span<double const> values;
        
        // Get CharStrings offset(0)
        values = top_DICT[CHARSTRINGS];
        CHECK(values.size() == 1);
        printf("CharStrings @ %g\n", values[0]);
        s = reset;
        r.s = s;
        r.skip((std::size_t)values[0]);
        INDEX charstrings_INDEX; parse_INDEX(r.s, charstrings_INDEX); charstrings_INDEX.debug();
        
        // Get PrivateDict size and offset(0)
        values = top_DICT[PRIVATE];
        CHECK(values.size() == 2);
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
            result.emplace(i, e.to_Bezier_list());
        }
        return result;
        
    }
    
    
} // namespace wry::CFF
