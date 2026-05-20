//
//  otf.cpp
//  client
//
//  Created by Antony Searle on 26/3/2026.
//

#include <cassert>
#include <cuchar>

#include <array>
#include <vector>
#include <algorithm>
#include <deque>

#include "simd.hpp"

#include "NetworkToHostReader.hpp"
#include "cff.hpp"
#include "otf.hpp"
#include "debug.hpp"

namespace wry::otf {

    // Fundamental types of OTF specification
    
    // They implicitly convert to the corresponding host byte order type
    
    using uint8 = std::uint8_t;
    using int8 = std::int8_t;
    using uint16 = NetworkByteOrder<std::uint16_t>;
    using int16 = NetworkByteOrder<std::int16_t>;
    using uint32 = NetworkByteOrder<std::uint32_t>;
    using int32 = NetworkByteOrder<std::int32_t>;
    using Fixed = FixedPoint<int32, double, 0x1.0p-16>;
    using FWORD = int16;
    using UFWORD = uint16;
    using F2DOT14 = FixedPoint<int16, float, 0x1.0p-14f>;
    using LONGDATETIME = NetworkByteOrder<std::int64_t>;
    using Tag = std::array<uint8, 4>;
    using Offset8 = uint8;
    using Offset16 = uint16;
    using Offset32 = uint32;
    using Version16Dot16 = int32;

    struct TableDirectory {

        uint32 sfntVersion;
        uint16 numTables;
        uint16 searchRange;
        uint16 entrySelector;
        uint16 rangeShift;

        struct TableRecord {
            Tag tableTag;
            uint32 checksum;
            Offset32 offset;
            uint32 length;
        } tableRecords[];


        span<byte const> find(const char* key) const {
            auto first = tableRecords;
            auto last = first + numTables;
            for (; first != last; ++first) {
                if (std::memcmp(&first->tableTag, key, 4) == 0) {
                    return {
                        (byte const*)this + first->offset,
                        first->length
                    };
                }
            }
            return {};
        }

    };
    
    
    
    struct CharacterToGlyphIndexMapping {
        
        uint16 version;
        uint16 numTables;

        struct EncodingRecord {
            uint16 platformID;
            uint16 encodingID;
            Offset32 subtableOffset;
        } encodingRecords[];
    
        struct Format4 {
            uint16 format;
            uint16 length;
            uint16 language;
            uint16 segCountX2;
            uint16 searchRange;
            uint16 entrySelector;
            uint16 rangeShift;
            uint16 endCode[];

            int glyph_index_from_character(int c) const {
                int glyphId = 0;
                assert(format == 4);
                auto segCount = segCountX2 / 2;
                uint16 const* reservedPad = endCode + segCount;
                uint16 const* startCode = reservedPad + 1;
                int16  const* idDelta = (int16 const*)startCode + segCount;
                uint16 const* idRangeOffset = (uint16 const*)idDelta + segCount;
                // uint16 const* glyphIdArray = idRangeOffset + segCount; // exposition only
                // Find i such that c <= endCode[i]
                auto p = std::upper_bound(endCode, endCode + segCount, c - 1, std::less{});
                auto i = p - endCode;
                CHECK(c <= endCode[i]);
                if (startCode[i] <= c) {
                    if (idRangeOffset[i] != 0) {
                        // Follow the cursed specification verbatim
                        glyphId = *(idRangeOffset[i]/2
                                    + (c - startCode[i])
                                    + &idRangeOffset[i]);
                        if (glyphId != 0) {
                            glyphId += idDelta[i];
                        }
                    } else {
                        glyphId = idDelta[i] + c;
                    }
                }
                return glyphId & 0xFFFF;
            }

        };

        Format4 const* find_format4() const {
            for (int i = 0; i != numTables; ++i) {
                void const* subtable = (byte const*)this + encodingRecords[i].subtableOffset;
                if (*(uint16 const*)subtable == 4)
                    return (Format4 const*)subtable;
            }
            return nullptr;
        }

    };
    
    


    struct FontHeader {

        uint16 majorVersion;
        uint16 minorVersion;
        Fixed fontRevision;
        uint32 checksumAdjustment;
        uint32 magicNumber;
        uint16 flags;
        uint16 unitsPerEm;
        LONGDATETIME created;
        LONGDATETIME modified;
        int16 xMin;
        int16 yMin;
        int16 xMax;
        int16 yMax;
        uint16 macStyle;
        uint16 lowestRecPPEM;
        int16 fontDirectionHint;
        int16 indexToLocFormat;
        int16 glyphDataFormat;

    };
    
    struct HorizontalHeader {

        uint16 majorVersion;
        uint16 minorVersion;
        FWORD ascender;
        FWORD descender;
        FWORD lineGap;
        UFWORD advanceWidthMax;
        FWORD minLeftSideBearing;
        FWORD minRightSideBearing;
        FWORD xMaxExtent;
        int16 caretSlopeRise;
        int16 caretSlopeRun;
        int16 caretSlopeOffset;
        int16 _reserved[4];
        int16 metricDataFormat;
        uint16 numberOfHMetrics;

    };
    
    struct HorizontalMetrics {
        
        struct LongHorMetric {
            UFWORD advanceWidth;
            FWORD lsb;
        } hMetrics[];

    };
        

    struct MaximumProfile {
        // v0.5
        Version16Dot16 version;
        uint16 numGlyphs;
        // v1.0
        uint16 maxPoints;
        uint16 maxContours;
        uint16 maxCompositePoints;
        uint16 maxCompositeContours;
        uint16 maxZones;
        uint16 maxTwilightPoints;
        uint16 maxStorage;
        uint16 maxFunctionDefs;
        uint16 maxInstructionDefs;
        uint16 maxStackElements;
        uint16 maxSizeOfInstructions;
        uint16 maxComponentElements;
        uint16 maxComponentDepth;
    };
    
    struct NamingTable {
        
    };
        
    struct OS_2andWindowsSpecificMetrics {
        // v0.0
        uint16 version;
        FWORD xAvgCharWidth;
        uint16 usWeightClass;
        uint16 usWidthClass;
        uint16 fsType;
        FWORD ySubscriptXSize;
        FWORD ySubscriptYSize;
        FWORD ySubscriptXOffset;
        FWORD ySubscriptYOffset;
        FWORD ySuperscriptXSize;
        FWORD ySuperscriptYSize;
        FWORD ySuperscriptXOffset;
        FWORD ySuperscriptYOffset;
        FWORD yStrikeoutSize;
        FWORD yStrikeoutPosition;
        int16 sFamilyClass;
        uint8 panose[10];
        uint32 ulUnicodeRange1;
        uint32 ulUnicodeRange2;
        uint32 ulUnicodeRange3;
        uint32 ulUnicodeRange4;
        Tag achVendID;
        uint16 fsSelection;
        uint16 usFirstCharIndex;
        uint16 usLastCharIndex;
        FWORD sTypoAscender;
        FWORD sTypoDescender;
        FWORD sTypoLineGap;
        UFWORD usWinAscent;
        UFWORD usWinDescent;
        // v1.0
        uint32 ulCodePageRange1;
        uint32 ulCodePageRange2;
        // v4.0
        FWORD sxHeight;
        FWORD sCapHeight;
        uint16 usDefaultChar;
        uint16 usBreakChar;
        uint16 usMaxContext;
        // v5.0
        uint16 usLowerOpticalPointSize;
        uint16 usUpperOpticalPointSize;
    };

    struct PostScriptInformation {
    };
    
    struct GlyphPositioningData {
    };


    struct GlyphHeader {

        int16 numberOfContours;
        int16 xMin;
        int16 yMin;
        int16 xMax;
        int16 yMax;

    };

    template<std::integral T>
    T read(span<byte const>& s) {
        CHECK(s.size() >= sizeof(T));
        T x = {};
        std::memcpy(&x, s.data(), sizeof(T));
        s.drop_front(sizeof(T));
        return ntoh(x);
    }

    enum SimpleGlyphFlags {
        ON_CURVE_POINT = 0x01,
        X_SHORT_VECTOR = 0x02,
        Y_SHORT_VECTOR = 0x04,
        REPEAT_FLAG    = 0x08,
        X_IS_SAME_OR_POSITIVE_X_SHORT_VECTOR = 0x10,
        Y_IS_SAME_OR_POSITIVE_Y_SHORT_VECTOR = 0x20,
        OVERLAP_SIMPLE = 0x40,
        // RESERVED = 0x80,
    };

    struct SimpleGlyph {

        std::vector<uint16_t> endPtsOfContours;
        std::vector<uint8_t> flags;
        std::vector<simd_float2> coordinates;

        void transform(simd_float2x2 A) {
            for (auto& x : coordinates)
                x = simd_mul(A, x);
        }

        void translate(simd_float2 b) {
            for (auto& x : coordinates)
                x += b;
        }

        void extend(SimpleGlyph const& other) {
            auto numberOfPoints = endPtsOfContours.back() + 1;
            assert(flags.size() == numberOfPoints);
            assert(coordinates.size() == numberOfPoints);
            for (auto e : other.endPtsOfContours)
                endPtsOfContours.push_back(numberOfPoints + e);
            flags.insert(flags.end(), other.flags.begin(), other.flags.end());
            coordinates.insert(coordinates.end(), other.coordinates.begin(), other.coordinates.end());
        }

    };

    SimpleGlyph otf_parse_simple_glyph(span<byte const>& s, int numberOfContours) {

        assert(numberOfContours >= 0);
        if (numberOfContours == 0)
            return {};   // legal empty simple glyph (e.g. .notdef, space)

        std::vector<uint16_t> endPtsOfContours;
        for (int i = 0; i != numberOfContours; ++i)
            endPtsOfContours.push_back(read<uint16_t>(s));
        size_t numberOfPoints = endPtsOfContours.back() + 1;

        auto instructionLength = read<uint16_t>(s);

        s.drop_front(instructionLength);

        std::vector<uint8_t> flags;
        for (size_t i = 0; i != numberOfPoints;) {
            assert(i < numberOfPoints);
            int n = 1;
            auto b = read<uint8_t>(s);
            if (b & REPEAT_FLAG)
                n += read<uint8_t>(s);
            flags.insert(flags.end(), n, b);
            i += n;
        }

        assert(flags.size() == numberOfPoints);

        std::vector<simd_float2> coordinates(numberOfPoints);

        float x = 0.0f;
        for (size_t i = 0; i != numberOfPoints; ++i) {
            auto f = flags[i];
            switch (f & (X_SHORT_VECTOR | X_IS_SAME_OR_POSITIVE_X_SHORT_VECTOR)) {
                case 0:
                    x += read<int16_t>(s);
                    break;
                case X_SHORT_VECTOR:
                    x -= read<uint8_t>(s);
                    break;
                case X_IS_SAME_OR_POSITIVE_X_SHORT_VECTOR:
                    break;
                case (X_SHORT_VECTOR | X_IS_SAME_OR_POSITIVE_X_SHORT_VECTOR):
                    x += read<uint8_t>(s);
                    break;
                default:
                    std::unreachable();
            }
            coordinates[i].x = x;
        }

        float y = 0.0f;
        for (size_t i = 0; i != numberOfPoints; ++i) {
            auto f = flags[i];
            switch (f & (Y_SHORT_VECTOR | Y_IS_SAME_OR_POSITIVE_Y_SHORT_VECTOR)) {
                case 0:
                    y += read<int16_t>(s);
                    break;
                case Y_SHORT_VECTOR:
                    y -= read<uint8_t>(s);
                    break;
                case Y_IS_SAME_OR_POSITIVE_Y_SHORT_VECTOR:
                    break;
                case (Y_SHORT_VECTOR | Y_IS_SAME_OR_POSITIVE_Y_SHORT_VECTOR):
                    y += read<uint8_t>(s);
                    break;
                default:
                    std::unreachable();
            }
            coordinates[i].y = y;
        }

        // all data consumed (up to offset granualrity)
        CHECK(s.size() <= 1);

        return SimpleGlyph {
            endPtsOfContours,
            flags,
            coordinates,
        };

    }


    enum ComponentGlyphFlags {
        ARG1_AND_2_ARE_WORDS = 0x0001,
        ARGS_ARE_XY_VALUES = 0x0002,
        ROUND_XY_TO_GRID = 0x0004,
        WE_HAVE_A_SCALE = 0x0008,
        MORE_COMPONENTS = 0x0020,
        WE_HAVE_AN_X_AND_Y_SCALE = 0x0040,
        WE_HAVE_A_TWO_BY_TWO = 0x0080,
        WE_HAVE_INSTRUCTIONS = 0x0100,
        USE_MY_METRICS = 0x0200,
        OVERLAP_COMPOUND = 0x0400,
        SCALED_COMPONENT_OFFSET = 0x0800,
        UNSCALED_COMPONENT_OFFSET = 0x1000,
        // RESERVED = 0xE010,
    };

    struct ComponentGlyph {
        uint16_t flags;
        uint16_t glyphIndex;
        int32_t argument1;
        int32_t argument2;
        simd_float2x2 scale;
    };

    ComponentGlyph otf_parse_component_glyph(span<byte const>& s) {

        uint16_t flags = read<uint16_t>(s);
        uint16_t glyphIndex = read<uint16_t>(s);
        int32_t argument1, argument2;
        if (flags & ARGS_ARE_XY_VALUES) {
            if (flags & ARG1_AND_2_ARE_WORDS) {
                argument1 = read<int16_t>(s);
                argument2 = read<int16_t>(s);
            } else {
                argument1 = read<int8_t>(s);
                argument2 = read<int8_t>(s);
            }
        } else {
            if (flags & ARG1_AND_2_ARE_WORDS) {
                argument1 = read<uint16_t>(s);
                argument2 = read<uint16_t>(s);
            } else {
                argument1 = read<uint8_t>(s);
                argument2 = read<uint8_t>(s);
            }
        }
        // float xscale = 1.0f, yscale = 1.0f, scale01 = 0.0f, scale10 = 0.0f;
        simd_float2x2 scale = {{{0x1.0p+14f,0.0f},{0.0f,0x1.0p+14f}}};
        if (flags & WE_HAVE_A_SCALE) {
            scale.columns[0].x = read<int16_t>(s);
            scale.columns[1].y = scale.columns[0].x;
        } else if (flags & WE_HAVE_AN_X_AND_Y_SCALE) {
            scale.columns[0].x = read<int16_t>(s);
            scale.columns[1].y = read<int16_t>(s);
        } else if (flags & WE_HAVE_A_TWO_BY_TWO) {
            // Wire order is xscale, scale01, scale10, yscale (per OT spec).
            // Transform is [x';y'] = [xscale scale01; scale10 yscale] [x;y].
            // simd_float2x2 is column-major, so M[row][col] = M.columns[col][row].
            scale.columns[0].x = read<int16_t>(s);   // xscale
            scale.columns[1].x = read<int16_t>(s);   // scale01
            scale.columns[0].y = read<int16_t>(s);   // scale10
            scale.columns[1].y = read<int16_t>(s);   // yscale
        }
        scale.columns[0] *= 0x1.0p-14f;
        scale.columns[1] *= 0x1.0p-14f;


        // if !ARGS_ARE_XY_VALUES:
        // the tranformed point[arg2] in the child must align with
        // the point[arg1] in the parent glyph (the concatenated points of all previously processed glyphs)

        return ComponentGlyph{
            flags,
            glyphIndex,
            argument1,
            argument2,
            scale
        };

    }


    std::vector<bezier4> process_points(SimpleGlyph const& a) {

        std::vector<bezier4> result;
        std::deque<simd_float2> points;
        auto push = [&](int i, int j) {
            bool fi = a.flags[i] & ON_CURVE_POINT;
            bool fj = a.flags[j] & ON_CURVE_POINT;
            simd_float2 vi = a.coordinates[i];
            simd_float2 vj = a.coordinates[j];
            points.push_back(vi);
            // Insert an implicit midpoint between adjacent same-type points:
            //  on-on   -> the midpoint is the implicit control of a line
            //             (degenerate quadratic).
            //  off-off -> the midpoint is the implicit on-curve anchor.
            if (fi == fj) {
                points.push_back(simd_mix(vi, vj, 0.5f));
            }
        };

        int start = 0;
        for (int end : a.endPtsOfContours) {
            size_t curves_begin = result.size();
            for (int i = start; i != end; ++i)
                push(i, i + 1);
            // close the contour
            push(end, start);
            if (a.flags[start] & ON_CURVE_POINT) {
                // on-start: append the starting anchor at the back so the
                // consumer's last (a, b, c) triple closes the loop.
                points.push_back(a.coordinates[start]);
            } else {
                // off-start: the natural start anchor was deposited at the
                // back by the closing push (either an on-curve point or an
                // implicit midpoint). Rotate it to the front so the consumer
                // starts there.
                points.push_front(points.back());
            }
            while (points.size() >= 3) {
                auto a = points.front(); points.pop_front();
                auto b = points.front(); points.pop_front();
                auto c = points.front();
                result.push_back(bezier4{{
                    a,
                    simd_mix(a, b, 2.0f / 3.0f),
                    simd_mix(b, c, 1.0f / 3.0f),
                    c
                }});
            }
            points.clear();

            // TrueType contours are oriented clockwise (with y-up); the analytic
            // coverage integrator (and the CFF input format) expects
            // counter-clockwise.  Reverse the per-contour curve slice and swap
            // each cubic's endpoint/control pairs to flip orientation.
            std::reverse(result.begin() + curves_begin, result.end());
            for (size_t k = curves_begin; k != result.size(); ++k) {
                auto& cubic = result[k];
                std::swap(cubic.columns[0], cubic.columns[3]);
                std::swap(cubic.columns[1], cubic.columns[2]);
            }

            start = end + 1;
        }

        for (auto& p : result)
            for (int i = 0; i != 4; ++i)
                printf("  %g %g\n", p.columns[i].x, p.columns[i].y);

        return result;
    }







    struct Handle::Inner {

        // Hold a minimal set of subtable mappings

        HorizontalMetrics const* hmtx;
        OS_2andWindowsSpecificMetrics const* os_2;
        CharacterToGlyphIndexMapping::Format4 const* cmap_subtable;

        span<byte const> glyf;
        span<byte const> loca;

        cff::Handle cff_;

        int head_indexToLocFormat;
        int head_unitsPerEm;
        int maxp_numberOfGlyphs;
        int hhea_numberOfHMetrics;

        Metrics metrics_for_face() const {
            return Metrics{
                .ascender = (float)os_2->sTypoAscender,
                .descender = (float)os_2->sTypoDescender,
                .line_gap = (float)os_2->sTypoLineGap,
            };
        }

        HorizontalMetrics::LongHorMetric horizontal_metrics_for_glyph_index(int glyph_index) const {
            return hmtx->hMetrics[std::min(glyph_index, hhea_numberOfHMetrics - 1)];
        }

        span<byte const> otf_span_for_glyph_index(int glyph_index) const {
            size_t a, b;
            switch (head_indexToLocFormat) {
                case 0:
                    // Short format: stored value is the actual offset divided by 2
                    a = (size_t)((Offset16 const*)loca.data())[glyph_index + 0] * 2;
                    b = (size_t)((Offset16 const*)loca.data())[glyph_index + 1] * 2;
                    break;
                case 1:
                    a = ((Offset32 const*)loca.data())[glyph_index + 0];
                    b = ((Offset32 const*)loca.data())[glyph_index + 1];
                    break;
                default:
                    abort();
            }
            if (a == b) {
                // Empty glyph (e.g. .notdef, space): no outline
                return {};
            }
            auto c = glyf.subspan(a, b - a);
            return c;
        }

        SimpleGlyph otf_parse_glyph(span<byte const>& s) const {
            auto header = (GlyphHeader const*)s.data();
            s.drop_front(sizeof(GlyphHeader));
            if (header->numberOfContours >= 0) {
                return otf_parse_simple_glyph(s, header->numberOfContours);
            } else if (header->numberOfContours == -1) {
                SimpleGlyph parent = {};
                ComponentGlyph a;
                do {
                    a = otf_parse_component_glyph(s);
                    span<byte const> t = otf_span_for_glyph_index(a.glyphIndex);
                    SimpleGlyph b = otf_parse_glyph(t);
                    b.transform(a.scale);
                    if (a.flags & ARGS_ARE_XY_VALUES) {
                        b.translate({(float)a.argument1, (float)a.argument2});
                    } else {
                        // Match-point semantics require a non-empty parent.
                        CHECK(!parent.endPtsOfContours.empty());
                        b.translate(parent.coordinates[a.argument1] - b.coordinates[a.argument2]);
                    }
                    if (parent.endPtsOfContours.empty())
                        parent = std::move(b);
                    else
                        parent.extend(b);
                } while (a.flags & MORE_COMPONENTS);
                return parent;
            } else {
                abort();
            }
        }

        std::vector<bezier4> outline_for_glyph_index(int glyph_index) const {
            if (cff_) {
                return cff_.outline_for_glyph_index(glyph_index);
            } else if (glyf && loca) {
                span<byte const> a = otf_span_for_glyph_index(glyph_index);
                if (!a)
                    return {};
                SimpleGlyph b = otf_parse_glyph(a);
                return process_points(b);
            } else {
                abort();
            }
        }

        std::vector<bezier4> outline_for_character(int c) const {
            int glyph_index = cmap_subtable->glyph_index_from_character(c);
            return outline_for_glyph_index(glyph_index);
        }

        float advance_for_character(int c) const {
            int glyph_index = cmap_subtable->glyph_index_from_character(c);
            return horizontal_metrics_for_glyph_index(glyph_index).advanceWidth;
        }

    };

    Handle::~Handle() {
        delete _inner;
    }

    std::vector<bezier4> Handle::outline_for_character(int c) const {
        return _inner->outline_for_character(c);
    }

    std::vector<bezier4> Handle::outline_for_glyph_index(int glyph_index) const {
        return _inner->outline_for_glyph_index(glyph_index);
    }

    int Handle::glyph_index_for_character(int c) const {
        return _inner->cmap_subtable->glyph_index_from_character(c);
    }

    float Handle::advance_for_glyph_index(int glyph_index) const {
        return _inner->horizontal_metrics_for_glyph_index(glyph_index).advanceWidth;
    }

    float Handle::advance_for_character(int c) const {
        return _inner->advance_for_character(c);
    }

    Handle::Metrics Handle::metrics_for_face() const {
        return _inner->metrics_for_face();
    }

    int Handle::units_per_em() const {
        return _inner->head_unitsPerEm;
    }

    template<typename T>
    T const* span_cast(span<byte const> a) {
        if (a.empty()) {
            return nullptr;
        } else {
            assert(sizeof(T) <= a.size());
            return (T const*)a.data();
        }
    }

    Handle Handle::parse(span<byte const> whole) {

        auto h = new Handle::Inner;

        auto tableDirectory = span_cast<TableDirectory>(whole);

        // Required tables

        auto cmap = span_cast<CharacterToGlyphIndexMapping>(tableDirectory->find("cmap"));
        auto head = span_cast<FontHeader>(tableDirectory->find("head"));
        auto hhea = span_cast<HorizontalHeader>(tableDirectory->find("hhea"));
        h->hmtx = span_cast<HorizontalMetrics>(tableDirectory->find("hmtx"));
        auto maxp = (MaximumProfile const*)tableDirectory->find("maxp").data();
        // auto name = span_cast<NamingTable>(tableDirectory->find("name"));
        h->os_2 = (OS_2andWindowsSpecificMetrics const*)tableDirectory->find("OS/2").data();
        // auto post = span_cast<PostScriptInformation>(tableDirectory->find("post"));

        // Tables related to TrueType outlines

        {
            h->glyf = tableDirectory->find("glyf");
            h->loca = tableDirectory->find("loca");
            printf("h->glyf %p %zd\n", h->glyf.data(), h->glyf.size());
            printf("h->loca %p %zd\n", h->loca.data(), h->loca.size());
        }

        // Tables related to CFF outlines

        {
            if (auto s = tableDirectory->find("CFF "))
                h->cff_ = cff::Handle::parse(s);
        }

        // Derived quantities

        h->cmap_subtable = cmap->find_format4();

        h->head_indexToLocFormat = head->indexToLocFormat;
        h->head_unitsPerEm = head->unitsPerEm;
        h->hhea_numberOfHMetrics = hhea->numberOfHMetrics;
        h->maxp_numberOfGlyphs = maxp->numGlyphs;

        Handle g;
        g._inner = h;
        return g;
    }


} // namespace wry::otf

