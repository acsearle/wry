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
    
#define MALFORMED() abort();
        
    using Card8 = std::uint8_t;
    using Card16 = NetworkByteOrder<std::uint16_t>;
    using Offset8 = std::uint8_t;
    using Offset16 = NetworkByteOrder<std::uint16_t>;
    using Offset32 = NetworkByteOrder<std::uint32_t>;
    using OffSize = std::uint8_t;
    using SID = NetworkByteOrder<std::uint16_t>;
    
    struct Header {
        
        Card8 major;
        Card8 minor;
        Card8 hdrSize;
        OffSize offSize;

    }; // struct Header
    
    // project the raw bytes, validate, and advance the span
    Header const* parse_Header(span<const byte>& s) {
        Header const* h = (Header const*)s.data();
        CHECK(h->hdrSize >= sizeof(Header)); // basic header must fit
        CHECK((1 <= h->offSize) && (h->offSize <= 4));
        s.drop_front(h->hdrSize);
        return h;
    }
        
    struct INDEX {
        
        Card16 count;
        OffSize offSize;
        byte offsets[];
        
        uint32_t _parse_offset(byte const*& p) const {
            uint32_t result{};
            // because we use arithmetic operations to compose the result,
            // it is correct for big- and little-endian hosts.
            for (int i = 0; i != offSize; ++i)
                result = (result << 8) | *p++;
            return result;
        }
        
        span<byte const> operator[](size_t i) const {
            CHECK(i < count);
            CHECK((1 <= offSize) && (offSize <= 4));
            byte const* p = offsets + i * offSize;
            uint32_t a = _parse_offset(p);
            uint32_t b = _parse_offset(p);
            byte const* q = offsets + (count + 1) * offSize - 1;
            return { q + a, q + b };
        }
                
    }; // struct INDEX
    
    
    // project the raw bytes, validate, and advance the span
    INDEX const* parse_INDEX(span<const byte>& s) {
        INDEX const* x = (INDEX const*)s.data();
        if (x->count) {
            CHECK((1 <= x->offSize) && (x->offSize <= 4));
            s._begin = (*x)[x->count - 1]._end;
        } else {
            // If the count is zero, offsize does not exist
            s.drop_front(sizeof(Card16));
        }
        return x;
    }

    // overload so the caller isn't forced to make an lvalue
    INDEX const* parse_INDEX(span<const byte>&& s) {
        // ... but now that we have one, use it
        return parse_INDEX(s);
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
        
        static void _parse_real(span<byte const>& s, double& target) {
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
    
    // parse the entire span into a data structure that supports efficient lookups
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
                MALFORMED(); // reserved
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
                DICT::_parse_real(s, u);
                value.push_back(u);
            } else if (b0 <= 31) [[unlikely]] {
                MALFORMED(); // reserved
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
                MALFORMED(); // reserved
            }
        }
        if (!value.empty())
            MALFORMED();
    }
    
    
    
    struct Type2CharstringInterpreter {
        
        INDEX const* global_subroutines;
        INDEX const* local_subroutines;
        
        std::deque<float> operand_stack;
        std::vector<wry::span<byte const>> call_stack;
        
        // NOTE: This awkward double negative name is suggested by the CFF
        // specification
        bool is_not_first_operand_stack_clearing_operator = false;
        
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
            operand_stack.clear();
            call_stack.clear();
            is_not_first_operand_stack_clearing_operator = false;
            pen = {};
            mode = 0;
            nhstem = 0;
            nvstem = 0;
            points.clear();
            modes.clear();
        }
        
        void maybe_width() {
            if (!is_not_first_operand_stack_clearing_operator) {
                is_not_first_operand_stack_clearing_operator = true;
                if (!operand_stack.empty()) {
                    width = operand_stack.front();
                    operand_stack.pop_front();
                }
            }
        }
        
        void maybe_width_if_odd() {
            if (operand_stack.size() & 1)
                maybe_width();
        }
        
        void maybe_width_if_even() {
            if (!(operand_stack.size() & 1))
                maybe_width();
        }
        
        float pop_front_operand() {
            CHECK(!operand_stack.empty());
            float a = operand_stack.front();
            operand_stack.pop_front();
            return a;
        }

        float pop_back_operand() {
            CHECK(!operand_stack.empty());
            float a = operand_stack.back();
            operand_stack.pop_back();
            return a;
        }
        
        void shift_x() {
            pen.x += pop_front_operand();
        }
        
        void shift_y() {
            pen.y += pop_front_operand();
        }
        
        void if_odd_shift(bool to_x) {
            if (operand_stack.size() & 1) {
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
            while (operand_stack.size() >= 2) {
                ++counter; operand_stack.pop_front();
                ++counter; operand_stack.pop_front();
            }
            CHECK(operand_stack.empty());
        }
                
        void do_mask(wry::span<byte const>& str) {
            CHECK(!(nhstem & 1));
            CHECK(!(nvstem & 1));
            int number_of_bytes = (int)((nhstem + nvstem + 15) / 16);
            // Discard hinting information (unused by our renderer)
            str.drop_front(number_of_bytes);
        }
        
        void call_subroutine(INDEX const& subroutines, span<byte const>& s) {
            int i = (int)pop_back_operand();
            int bias = (subroutines.count < 1240 ? 107
                        : (subroutines.count < 33900 ? 1131
                           : 32768));
            call_stack.push_back(s);
            s = subroutines[i + bias];
        }
        
        void do_alternating_curves(bool start_with_x) {
            // VHCURVETO is start_with_x = false; HVCURVETO is start_with_x = true.
            mode = BEZIER;
            do {
                if (start_with_x) {
                    dx(); dxy();
                    shift_y();
                    if (operand_stack.size() == 1) shift_x();
                } else {
                    dy(); dxy();
                    shift_x();
                    if (operand_stack.size() == 1) shift_y();
                }
                push();
                start_with_x = !start_with_x;
            } while (!operand_stack.empty());
        }
        
        void do_alternating_lineto(bool start_with_x) {
            // HLINETO is start_with_x = true; VLINETO is start_with_x = false.
            mode = LINE;
            do {
                if (start_with_x) shift_x(); else shift_y();
                push();
                start_with_x = !start_with_x;
            } while (!operand_stack.empty());
        }
        
        bool execute(span<byte const> str);
        
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
        
    }; // struct Type2CharstringInterpreter
    
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
        
    bool Type2CharstringInterpreter::execute(span<byte const> s) {
        reset();
        while (!s.empty()) {
            uint8_t b0;
            parse_ntoh(s, b0);
            if ((b0 <= 31) && (b0 != 28)) {
                switch (b0) {
                    case HSTEM: {
                        maybe_width_if_odd();
                        do_stem(nhstem);
                        CHECK(operand_stack.empty());
                        break;
                    }
                    case VSTEM: {
                        maybe_width_if_odd();
                        do_stem(nvstem);
                        CHECK(operand_stack.empty());
                        break;
                    }
                    case VMOVETO: {
                        maybe_width_if_even();
                        mode = MOVE;
                        dy();
                        CHECK(operand_stack.empty());
                        break;
                    }
                    case RLINETO: {
                        mode = LINE;
                        do  {
                            dxy();
                        } while ((!operand_stack.empty()));
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
                        } while (!operand_stack.empty());
                        break;
                    }
                    case CALLSUBR: {
                        call_subroutine(*local_subroutines, s);
                        break;
                    }
                    case RETURN: {
                        s = call_stack.back(); call_stack.pop_back();
                        break;
                    }
                    case ENDCHAR: {
                        maybe_width_if_odd();
                        CHECK(operand_stack.empty());
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
                        if (!operand_stack.empty()) {
                            do_stem(nvstem);
                        }
                        do_mask(s);
                        CHECK(operand_stack.empty());
                        break;
                    }
                    case CNTRMASK: {
                        maybe_width_if_odd();
                        if (!operand_stack.empty()) {
                            do_stem(nvstem);
                        }
                        do_mask(s);
                        CHECK(operand_stack.empty());
                        break;
                    }
                    case RMOVETO: {
                        maybe_width_if_odd();
                        mode = MOVE;
                        dxy();
                        CHECK(operand_stack.empty());
                        break;
                    }
                    case HMOVETO: {
                        maybe_width_if_even();
                        mode = MOVE;
                        dx();
                        CHECK(operand_stack.empty());
                        break;
                    }
                    case VSTEMHM: {
                        maybe_width_if_odd();
                        do_stem(nvstem);
                        CHECK(operand_stack.empty());
                        break;
                    }
                    case RCURVELINE: {
                        mode = BEZIER;
                        do {
                            dxy(); dxy(); dxy();
                        } while (operand_stack.size() >= 6);
                        mode = LINE;
                        dxy();
                        CHECK(operand_stack.empty());
                        break;
                    }
                    case RLINECURVE: {
                        mode = LINE;
                        do {
                            dxy();
                        } while (operand_stack.size() > 6);
                        mode = BEZIER;
                        dxy(); dxy(); dxy();
                        break;
                    }
                    case VVCURVETO: {
                        if_odd_shift(true);
                        mode = BEZIER;
                        do {
                            dy(); dxy(); dy();
                        } while (!operand_stack.empty());
                        break;
                    }
                    case HHCURVETO: {
                        if_odd_shift(false);
                        mode = BEZIER;
                        do {
                            dx(); dxy(); dx();
                        } while (!operand_stack.empty());
                        break;
                    }
                    case CALLGSUBR: {
                        call_subroutine(*global_subroutines, s);
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
                        MALFORMED();
                        break;
                }
            } else {
                // Is an operand (a number)
                double number{};
                if (b0 == 28) {
                    std::int16_t n;
                    parse_ntoh(s, n);
                    number = n;
                } else if (b0 <= 246) {
                    number = b0 - 139;
                } else if (b0 <= 250) {
                    std::uint8_t n;
                    parse_ntoh(s, n);
                    number = (b0 - 247) * 256 + 108 +n;
                } else if (b0 <= 254) {
                    std::uint8_t n;
                    parse_ntoh(s, n);
                    number = -(b0 - 251) * 256 - 108 - n;
                } else { CHECK(b0 == 255);
                    std::int32_t n;
                    parse_ntoh(s, n);
                    number = n * 0x1p-16;
                }
                // printf("%g ",number);
                operand_stack.push_back(number);
            }
        }
        if (!operand_stack.empty()) {
            printf("Missing operator??\n");
        }
        printf("Missing endchar??\n");
        MALFORMED();
        
    }
    
    
    std::map<int, std::vector<BezierCurve<4>>> parse(byte const* first, byte const* last) {
        
        std::map<int, std::vector<BezierCurve<4>>> result;
        
        printf("parsing .cff of %zd bytes\n", last - first);

        span whole{first, last};
        span s{whole};
        std::size_t size, offset;
        
        auto header = parse_Header(s);
        CHECK(header->major == 1);
        
        auto name_INDEX = parse_INDEX(s);
        for (int i = 0; i != name_INDEX->count; ++i) {
            auto t = (*name_INDEX)[i];
            printf("\"%.*s\"\n", (int)t.size(), (char const*)t._begin);
        }
        auto top_DICT_INDEX = parse_INDEX(s);
        auto string_INDEX = parse_INDEX(s);
        for (int i = 0; i != string_INDEX->count; ++i) {
            auto t = (*string_INDEX)[i];
            printf("\"%.*s\"\n", (int)t.size(), (char const*)t._begin);
        }
        INDEX const* global_subr_INDEX = parse_INDEX(s);
        
        DICT top_DICT; {
            parse_DICT((*top_DICT_INDEX)[0], top_DICT);
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
        offset = (std::size_t)values[0];
        auto charstrings_INDEX = parse_INDEX(whole.after(offset));
        
        // Get PrivateDict size and offset(0)
        values = top_DICT[PRIVATE];
        CHECK(values.size() == 2);
        printf("PrivateDict(%g) @ %g\n", values[1], values[0]);
        size   = (std::size_t)values[0];
        offset = (std::size_t)values[1];
        auto private_DICT_s = whole.subspan(offset, size);
        DICT private_DICT; parse_DICT(private_DICT_s, private_DICT);
        
        values = private_DICT[SUBRS];
        printf("PrivateDict @ %g\n", values[0]);
        offset = (std::size_t)values[0];
        auto local_subr_INDEX = parse_INDEX(whole.after(private_DICT_s.begin() + offset));
        
        Type2CharstringInterpreter e;
        e.global_subroutines = global_subr_INDEX;
        e.local_subroutines = local_subr_INDEX;
        
        for (int i = 0; i != charstrings_INDEX->count; ++i) {
            e.execute((*charstrings_INDEX)[i]);
            printf("e.points.size() = %zd\n", e.points.size());
            result.emplace(i, e.to_Bezier_list());
        }
        return result;
        
    }
    
    
} // namespace wry::CFF
