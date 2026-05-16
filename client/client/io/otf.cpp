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
                printf("%.4s ? %s\n", first->tableTag.data(), key);
                if (std::memcmp(&first->tableTag, key, 4) == 0) {
                    printf("yea: %d, %d\n", (int)first->offset, (int)first->length);
                    return {
                        (byte const*)this + first->offset,
                        first->length
                    };
                } else {
                    printf("nea\n");
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
                } else {
                    abort();
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

        Version16Dot16 version;      // v0.5
        uint16 numGlyphs;
        /*
        uint16 maxPoints;            // v1.0
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
         */

    };
    
    struct NamingTable {
        
    };
        
    struct OS_2andWindowsSpecificMetrics {
        
        uint16 version;                 // v0
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
        /*
        uint32 ulCodePageRange1;        // v1
        uint32 ulCodePageRange2;        // v1
        FWORD sxHeight;                 // v4
        FWORD sCapHeight;               // v4
        uint16 usDefaultChar;           // v4
        uint16 usBreakChar;             // v4
        uint16 usMaxContext;            // v4
        uint16 usLowerOpticalPointSize; // v5
        uint16 usUpperOpticalPointSize; // v5
         */
    };

    struct PostScriptInformation {
    };
    
    struct GlyphPositioningData {
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
            std::vector<bool> on_curve;
            
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
                // TODO: Compund glyphs are not handled yet
                CHECK(numberOfContours);
                CHECK(xMin <= xMax);
                CHECK(yMin <= yMax);
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
                            CHECK(((0x0406 >> (f & 0x12)) & 0x3) == 2);
                            x_bytes += 2;
                            break;
                        case X_SHORT_VECTOR:
                            CHECK(((0x0406 >> (f & 0x12)) & 0x3) == 1);
                            x_bytes += 1;
                            break;
                        case X_IS_SAME_OR_POSITIVE_X_SHORT_VECTOR:
                            CHECK(((0x0406 >> (f & 0x12)) & 0x3) == 0);
                            x_bytes += 0;
                            break;
                        case (X_SHORT_VECTOR | X_IS_SAME_OR_POSITIVE_X_SHORT_VECTOR):
                            CHECK(((0x0406 >> (f & 0x12)) & 0x3) == 1);
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
                        points.push_back(pen);
                        on_curve.push_back(f & ON_CURVE_POINT);
                    }
                }
                
                CHECK(points.size() == endPtsOfContours.back() + 1);
                
                
                
                
                
                
                
                
                
            }
            
        };
        
        wry::span<byte const> s;
        explicit GlyphData(wry::span<byte const> s)
        : s(s) {
            
        };
        
    };
    
    
    
    struct IndexToLocation {
        
        int16_t indexToLocFormat;
        uint16_t numGlyphs;
        wry::span<byte const> s;
        
        explicit IndexToLocation(int16_t indexToLocFormat, uint16_t numGlyphs, wry::span<byte const> s)
        : indexToLocFormat(indexToLocFormat)
        , numGlyphs(numGlyphs)
        , s(s) {
        };
        
        uint32_t operator[](uint32_t glyphID) {
            wry::Reader r{s};
            if (!indexToLocFormat) {
                r.skip(glyphID * sizeof(uint16_t));
                return r.read<uint16_t>();
            } else {
                r.skip(glyphID * sizeof(uint32_t));
                return r.read<uint32_t>();
            }
        }
        
    };
    
    void enumerate_tables(span<byte const> s, auto&& f) {
        auto tableDirectory = (TableDirectory const*)s.data();
        for (int i = 0; i != tableDirectory->numTables; ++i) {
            auto& r = tableDirectory->tableRecords[i];
            f(r.tableTag, s.subspan(r.offset, r.length));
        }
    }
    
    
    void cmap_enumerate_encoding_records(span<byte const> s, auto&& f) {
        auto cmap = (CharacterToGlyphIndexMapping const*)s.data();
        for (int i = 0; i != cmap->numTables; ++i) {
            auto& r = cmap->encodingRecords[i];
            f(r.platformID, r.encodingID, s.after(r.subtableOffset));
        }
    }
    
    void cmapSubtableFormat4_enumerate_mapping(span<byte const> s, auto&& f) {
        auto subtable = (CharacterToGlyphIndexMapping::Format4 const*)s.data();
        CHECK(subtable->format == 4);
        int segCount = subtable->segCountX2 >> 1;
        uint16 const* startCode = subtable->endCode + segCount + 1;
        int16 const* idDelta = (int16 const*)startCode + segCount;
        uint16 const* idRangeOffset = (uint16 const*)idDelta + segCount;
        for (int i = 0; i != segCount; ++i) {
            for (int c = startCode[i];; ++c) {
                uint16_t glyphID = {};
                if (idRangeOffset[i]) {
                    glyphID = *(idRangeOffset[i] / 2
                                + (c - startCode[i])
                                + &idRangeOffset[i]);
                    if (glyphID != 0) {
                        glyphID += idDelta[i];
                    }
                } else {
                    glyphID = c + idDelta[i];
                }
                // printf("unicode %x -> glyphID -> %d\n", (int)(startCode + j), (int)glyphID);
                f(c, glyphID);
                if (c == subtable->endCode[i])
                    break;
            }
        }
    }

    struct Handle {

        // Hold a minimal set of subtable mappings

        HorizontalMetrics const* hmtx;
        cff::Handle const* cff_;
        CharacterToGlyphIndexMapping::Format4 const* cmap_subtable;

        int maxp_numberOfGlyphs;
        int hhea_numberOfHMetrics;

        float scale;
        float offset;

        HorizontalMetrics::LongHorMetric horizontal_metrics_for_glyph_index(int glyph_inddex) {
            return hmtx->hMetrics[std::min(glyph_inddex, hhea_numberOfHMetrics)];
        }

        std::vector<bezier4> path_for_glyph_index(int glyph_index) {
            std::vector<bezier4> result = cff::path_for_glyph_index(cff_, glyph_index);
            for (bezier4& a : result)
                for (simd_float2& b : a.columns)
                    (b += simd_float2{0.0f, (float)offset}) *= (float)scale;
            return result;
        }

        std::vector<bezier4> path_for_character(int c) {
            int glyph_index = cmap_subtable->glyph_index_from_character(c);
            return path_for_glyph_index(glyph_index);
        }

        float advance_for_character(int c) {
            int glyph_index = cmap_subtable->glyph_index_from_character(c);
            return horizontal_metrics_for_glyph_index(glyph_index).advanceWidth * (float)scale;
        }

    };

    template<typename T>
    T const* span_cast(span<byte const> a) {
        if (a.empty()) {
            return nullptr;
        } else {
            assert(sizeof(T) <= a.size());
            return (T const*)a.data();
        }
    }

    Handle const* parse_Handle(span<byte const> whole) {

        auto h = new Handle;

        auto tableDirectory = span_cast<TableDirectory>(whole);

        // Required tables

        auto cmap = span_cast<CharacterToGlyphIndexMapping>(tableDirectory->find("cmap"));
        // auto head = span_cast<FontHeader>(tableDirectory->find("head"));
        auto hhea = span_cast<HorizontalHeader>(tableDirectory->find("hhea"));
        h->  hmtx = span_cast<HorizontalMetrics>(tableDirectory->find("hmtx"));
        auto maxp = span_cast<MaximumProfile>(tableDirectory->find("maxp"));
        // auto name = span_cast<NamingTable>(tableDirectory->find("name"));
        auto os_2 = span_cast<OS_2andWindowsSpecificMetrics>(tableDirectory->find("OS/2"));
        // auto post = span_cast<PostScriptInformation>(tableDirectory->find("post"));

        // Tables related to TrueType outlines
        // ...

        // auto glyf = span_cast<GlyphData>(tableDirectory->find("glyf"));
        // auto loca = span_cast<IndexToLocation>(tableDirectory->find("loca"));

        // Tables related to CFF outlines

        h->cff_ = cff::parse_CFF(tableDirectory->find("CFF "));

        // Derived quantities

        h->cmap_subtable = cmap->find_format4();

        h->maxp_numberOfGlyphs = maxp->numGlyphs;
        h->hhea_numberOfHMetrics = hhea->numberOfHMetrics;

        double ascender =  os_2->sTypoAscender;
        double descender = os_2->sTypoDescender;
        double lineGap =   os_2->sTypoLineGap;

        h->scale = 1.0f / (ascender - descender + lineGap);
        h->offset = -descender + lineGap * 0.5f;

        return h;
    }


} // namespace wry::otf

