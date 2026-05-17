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

    struct GlyphInterpreter {

//        std::vector<uint16_t> endPtsOfContours;
//        uint16_t instructionLength;
//        std::vector<uint8_t> instructions;
//        std::vector<uint8_t> flags;
        std::vector<simd_float2> points;
        std::vector<bool> on_curve;

        enum {
            ON_CURVE_POINT = 0x01,
            X_SHORT_VECTOR = 0x02,
            Y_SHORT_VECTOR = 0x04,
            REPEAT_FLAG    = 0x08,
            X_IS_SAME_OR_POSITIVE_X_SHORT_VECTOR = 0x10,
            Y_IS_SAME_OR_POSITIVE_Y_SHORT_VECTOR = 0x20,
        };

        std::vector<bezier4> parse(span<byte const> s) {
            auto h = (GlyphHeader const*)s.data();
            // TODO: Compund glyphs are not handled yet
            printf("%d\n", (int)h->numberOfContours);
            printf("%d\n", (int)h->xMin);
            printf("%d\n", (int)h->yMin);
            printf("%d\n", (int)h->xMax);
            printf("%d\n", (int)h->yMax);
            if (h->numberOfContours == 0)
                return {};
            CHECK(h->numberOfContours > 0); // negative values indicate compiste glyphs
            CHECK(h->xMin <= h->xMax);
            CHECK(h->yMin <= h->yMax);
            s.drop_front(sizeof(GlyphHeader));


            span<uint16 const> endPtsOfContours((uint16 const*)s.data(), h->numberOfContours);
            uint16_t instructionLength = *endPtsOfContours.end(); // <-- deliberate one-past-the-end
            span<uint8 const> instructions((uint8 const*)(endPtsOfContours.end() + 1), instructionLength);
            uint8 const* flags_first = instructions.end();

            ptrdiff_t x_bytes = 0;

            // We need to parse the flags enough to find x and y

            auto flags_last = flags_first;
            for (int remaining = endPtsOfContours.back() + 1; remaining;) {
                assert(remaining > 0);
                uint8_t f = *flags_last++;
                int n = 1;
                if (f & REPEAT_FLAG)
                    n += *flags_last++;
                switch (f & (X_SHORT_VECTOR | X_IS_SAME_OR_POSITIVE_X_SHORT_VECTOR)) {
                    case 0:
                        x_bytes += (n << 1);
                        break;
                    case X_SHORT_VECTOR:
                        x_bytes += n;
                        break;
                    case X_IS_SAME_OR_POSITIVE_X_SHORT_VECTOR:
                        x_bytes += 0;
                        break;
                    case (X_SHORT_VECTOR | X_IS_SAME_OR_POSITIVE_X_SHORT_VECTOR):
                        x_bytes += n;
                        break;
                }
                remaining -= n;
            }

            // Set up the readers for the next pass
            // auto [sx, sy] = rf.s.partition(x_bytes);
            uint8 const* sx = flags_last;
            uint8 const* sy = sx + x_bytes;

            wry::Reader rx{{sx, sy}}, ry{{sy, s.end()}};
            // rf.s = r.s.before(rf.s.begin());

            simd_short2 pen{};
            for (byte const* rf = flags_first; rf != flags_last;) {
                uint8_t f = *rf++;
                uint8_t n = 1;
                if (f & REPEAT_FLAG) {
                    n += *rf++;
                }
                while (n--) {
                    switch (f & (X_SHORT_VECTOR | X_IS_SAME_OR_POSITIVE_X_SHORT_VECTOR)) {
                        case 0:
                            pen.x += rx.read<int16_t>();
                            break;
                        case X_SHORT_VECTOR:
                            pen.x -= (int)rx.read<uint8_t>();
                            break;
                        case X_IS_SAME_OR_POSITIVE_X_SHORT_VECTOR:
                            break;
                        case (X_SHORT_VECTOR | X_IS_SAME_OR_POSITIVE_X_SHORT_VECTOR):
                            pen.x += (int)rx.read<uint8_t>();
                            break;
                    }
                    switch (f & (Y_SHORT_VECTOR | Y_IS_SAME_OR_POSITIVE_Y_SHORT_VECTOR)) {
                        case 0:
                            pen.y += ry.read<int16_t>();
                            break;
                        case Y_SHORT_VECTOR:
                            pen.y -= (int)ry.read<uint8_t>();
                            break;
                        case Y_IS_SAME_OR_POSITIVE_Y_SHORT_VECTOR:
                            break;
                        case (Y_SHORT_VECTOR | Y_IS_SAME_OR_POSITIVE_Y_SHORT_VECTOR):
                            pen.y += (int)ry.read<uint8_t>();
                            break;
                    }
                    points.push_back(simd_float2{(float)pen.x, (float)pen.y});
                    on_curve.push_back(f & ON_CURVE_POINT);
                }
            }

            CHECK(points.size() == endPtsOfContours.back() + 1);

            std::vector<bezier4> result;
            simd_float2 a, b, c;
            int i0 = 0, i1;
            for (int j = 0; j != endPtsOfContours.size(); ++j) {
                i1 = endPtsOfContours[j];
                for (int i = i0; i != i1 - 1; ++i) {
                    if (on_curve[i + 1]) {
                        if (on_curve[i + 0]) {
                            a = points[i + 0];
                            c = points[i + 1];
                            b = simd_mix(a, c, 0.5f);
                        } else {
                            continue;
                        }
                    } else {
                        assert(i + 2 < points.size());
                        a = points[i + 0];
                        b = points[i + 1];
                        c = points[i + 2];
                        if (!on_curve[i + 0]) {
                            a = simd_mix(a, b, 0.5f);
                        }
                        if (!on_curve[i + 2]) {
                            c = simd_mix(b, c, 0.5f);
                        }
                    }
                    result.push_back(bezier4{{
                        a,
                        simd_mix(a, b, 2.0f / 3.0f),
                        simd_mix(b, c, 1.0f / 3.0f),
                        c
                    }});
                }
                i0 = i1;
            }


            for (auto& p : result)
                for (int i = 0; i != 4; ++i)
                    printf("  %g %g\n", p.columns[i].x, p.columns[i].y);

            return result;
        }

    };


    struct Handle::Inner {

        // Hold a minimal set of subtable mappings

        HorizontalMetrics const* hmtx;
        OS_2andWindowsSpecificMetrics const* os_2;
        CharacterToGlyphIndexMapping::Format4 const* cmap_subtable;

        span<byte const> glyf;
        span<byte const> loca;

        cff::Handle cff_;

        int head_indexToLocFormat;
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

        std::vector<bezier4> outline_for_glyph_index(int glyph_index) const {
            if (cff_) {
                return cff_.outline_for_glyph_index(glyph_index);
            } else if (glyf && loca) {
                uint32_t a, b;
                switch (head_indexToLocFormat) {
                    case 0:
                        // Short format: stored value is the actual offset divided by 2
                        a = (uint32_t)((Offset16 const*)loca.data())[glyph_index + 0] * 2;
                        b = (uint32_t)((Offset16 const*)loca.data())[glyph_index + 1] * 2;
                        break;
                    case 1:
                        a = ((Offset32 const*)loca.data())[glyph_index + 0];
                        b = ((Offset32 const*)loca.data())[glyph_index + 1];
                        break;
                    default:
                        abort();
                }
                if (a == b) {
                    // Empty glyph (e.g. .notdef, space) — no outline
                    return {};
                }
                auto c = glyf.subspan(a, b - a);

                GlyphInterpreter e;
                return e.parse(c);
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

    Handle::Metrics Handle::metrics_for_face() const {
        return _inner->metrics_for_face();
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
        h->hhea_numberOfHMetrics = hhea->numberOfHMetrics;
        h->maxp_numberOfGlyphs = maxp->numGlyphs;

        Handle g;
        g._inner = h;
        return g;
    }


} // namespace wry::otf

