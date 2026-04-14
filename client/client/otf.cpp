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
        
        struct TableRecord {
            Tag tableTag;
            uint32 checksum;
            Offset32 offset;
            uint32 length;
        };
        
        uint32 sfntVersion;
        uint16 numTables;
        uint16 searchRange;
        uint16 entrySelector;
        uint16 rangeShift;
        TableRecord tableRecords[0];
    };
    
    
    
    struct CharacterToGlyphIndexMapping {
        
        struct EncodingRecord {
            uint16 platformID;
            uint16 encodingID;
            Offset32 subtableOffset;
        };
        
        uint16 version;
        uint16 numTables;
        EncodingRecord encodingRecords[0];
    
        struct Format4 {
            uint16 format;
            uint16 length;
            uint16 language;
            uint16 segCountX2;
            uint16 searchRange;
            uint16 entrySelector;
            uint16 rangeShift;
            uint16 endCode[0];
        };
        
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
        };
        
        LongHorMetric hMetrics[0];
        
    };
        
    
    struct MaximumProfile {
        Version16Dot16 version;
        uint16 numGlyphs;
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
        uint32 ulCodePageRange1;
        uint32 ulCodePageRange2;
        FWORD sxHeight;
        FWORD sCapHeight;
        uint16 usDefaultChar;
        uint16 usBreakChar;
        uint16 usMaxContext;
        uint16 usLowerOpticalPointSize;
        uint16 usUpperOpticalPointSize;
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
                        on_curve.push_back(f & ON_CURVE_POINT);
                    }
                }
                
                assert(points.size() == endPtsOfContours.back());
                
                
                
                
                
                
                
                
                
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
    
    void enumerate_tables(Bytes s, auto&& f) {
        auto tableDirectory = (TableDirectory const*)s.data();
        for (int i = 0; i != tableDirectory->numTables; ++i) {
            auto& r = tableDirectory->tableRecords[i];
            f(r.tableTag, s.subspan(r.offset, r.length));
        }
    }
    
    
    void cmap_enumerate_encoding_records(Bytes s, auto&& f) {
        auto cmap = (CharacterToGlyphIndexMapping const*)s.data();
        for (int i = 0; i != cmap->numTables; ++i) {
            auto& r = cmap->encodingRecords[i];
            f(r.platformID, r.encodingID, s.after(r.subtableOffset));
        }
    }
    
    void cmapSubtableFormat4_enumerate_mapping(Bytes s, auto&& f) {
        auto subtable = (CharacterToGlyphIndexMapping::Format4 const*)s.data();
        assert(subtable->format == 4);
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

    struct Context {
        
        TableDirectory const* tableDirectory;
        
        // Required tables
        CharacterToGlyphIndexMapping const* cmap;
        FontHeader const* head;
        HorizontalHeader const* hhea;
        HorizontalMetrics const* hmtx;
        MaximumProfile const* maxp;
        NamingTable const* name;
        OS_2andWindowsSpecificMetrics const* OS_2;
        PostScriptInformation const* post;

        // CFF outlines table
        void const* CFF_;

        // TrueType outlines tables
        GlyphData const* glyf;
        IndexToLocation const* loca;
        
        // Advanced
        GlyphPositioningData const* GPOS;
        
        // Derived quantities
        CharacterToGlyphIndexMapping::Format4 const* format4;
        double scale;
        double offset;
                
    };
    
    void get_one(Context* a, int c) {
        // Get glyph index
        int glyphId = 0;
        {
            // Use cmap format 4 table (basic multilingual plane)
            auto b = a->format4;
            auto segCount = b->segCountX2 / 2;
            uint16 const* endCode = b->endCode;
            uint16 const* reservedPad = endCode + segCount;
            uint16 const* startCode = reservedPad + 1;
            int16  const* idDelta = (int16 const*)startCode + segCount;
            uint16 const* idRangeOffset = (uint16 const*)idDelta + segCount;
            // exposition only
            [[maybe_unused]] uint16 const* glyphIdArray = idRangeOffset + segCount;
            // Find i such that c <= endCode[i]
            auto p = std::upper_bound(endCode, endCode + segCount, c - 1, std::less{});
            auto i = p - endCode;
            assert(c <= endCode[i]);
            int glyphId = 0;
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
            // Modulo 65536
            glyphId &= 0x0000FFFF;
        }
        
        // Get horizontal advance width
        double advanceWidth = 0;
        {
            int i = std::min(glyphId, a->hhea->numberOfHMetrics - 1);
            advanceWidth = a->hmtx->hMetrics[glyphId].advanceWidth;
        }
        
        // Get Bezier curves
        {
            
        }
        

    }
    
    void* parse(byte const* first, byte const* last) {
        
        printf("parsing .otf of %zd bytes\n", last - first);
        
        
        Context* a = new Context;
        a->tableDirectory = (TableDirectory const*)first;

        // Enumerate tables and record locations
        for (int i = 0; i != a->tableDirectory->numTables; ++i) {
            auto& r = a->tableDirectory->tableRecords[i];
            void const* s = first + r.offset;
            auto eq = [&](char const (&s)[5]) {
                return std::memcmp(&r.tableTag, s, 4) == 0;
            };
            if (eq("CFF "))
                a->CFF_ = (void const*)s;
            if (eq("GPOS"))
                a->GPOS = (GlyphPositioningData const*)s;
            if (eq("OS/2"))
                a->OS_2 = (OS_2andWindowsSpecificMetrics const*)s;
            if (eq("cmap"))
                a->cmap = (CharacterToGlyphIndexMapping const*)s;
            if (eq("head"))
                a->head = (FontHeader const*)s;
            if (eq("hhea"))
                a->hhea = (HorizontalHeader const*)s;
            if (eq("hmtx"))
                a->hmtx = (HorizontalMetrics const*)s;
            if (eq("maxp"))
                a->maxp = (MaximumProfile const*)s;
            if (eq("name"))
                a->name = (NamingTable const*)s;
            if (eq("post"))
                a->post = (PostScriptInformation const*)s;
        }
        
        for (int i = 0; i != a->cmap->numTables; ++i) {
            auto& r = a->cmap->encodingRecords[i];
            auto b = (byte const*)a->cmap + r.subtableOffset;
            int format = *(uint16 const*)b;
            if (format == 4) {
                a->format4 = (CharacterToGlyphIndexMapping::Format4 const*)b;
            }
        }
        
        double ascender =  a->OS_2->sTypoAscender;
        double descender = a->OS_2->sTypoDescender;
        double lineGap =   a->OS_2->sTypoLineGap;
        
        DUMP(ascender);
        DUMP(descender);
        DUMP(lineGap);
        DUMP(ascender-descender);
        DUMP(ascender-descender+lineGap);
        
        a->scale = 1.0 / (ascender - descender + lineGap);
        a->offset = -descender + lineGap * 0.5;
                
        int numGlyphs = a->maxp->numGlyphs;
        int numberOfHMetrics = a->hhea->numberOfHMetrics;

        
        
        
//        std::map<int, std::vector<BezierCurve<4>>> glyph_id_to_paths;
//        std::map<int, int> unicode_to_glyphid;
//
//        enumerate_tables({first, last},
//                         [&](Tag tableTag, Bytes s) {
//            printf("TableRecord \"%.4s\" of %zd bytes\n", (char const*)&tableTag, s.size());
//            auto eq = [tableTag](char const (&s)[5]) {
//                return std::memcmp(&tableTag, s, 4) == 0;
//            };
//            
//            if (eq("CFF ")) {
//                glyph_id_to_paths = cff::parse(s.begin(), s.end());
//            }
//            if (eq("OS/2")) {
//                
//            }
//            if (eq("cmap")) {
//                cmap_enumerate_encoding_records(s,
//                                                [&](uint16_t platformID,
//                                                    uint16_t encodingID,
//                                                    Bytes s) {
//                    if (platformID == 0 && encodingID == 3) {
//                        // cmapSubtableFormat4 x;
//                        // x.parse(s);
//                        cmapSubtableFormat4_enumerate_mapping(s,
//                                                              [&](int c, int glyphId) {
//                            unicode_to_glyphid.emplace(c, glyphId);
//                        });
//                    }
//                });
//            }
//            if (eq("hhea")) {
//                
//            }
//        });
//        
//        for (auto [c, i] : unicode_to_glyphid) {
//            printf("'%c' (%x) -> %zd curves\n", c, c,  glyph_id_to_paths[i].size());
//        }
        
        
//        
//        span<const byte> s{first, last};
//        
//        TableDirectory tableDirectory;
//        parse_TableDirectory(s, tableDirectory);
//        
//        cmapTable cmap;
//        parse_cmapTable(tableDirectory["cmap"], cmap);
//        cmap.debug();
//        
//        auto head = FontHeaderTable{tableDirectory["head"]};
//        head.debug();
//        
//        auto hhea = HorizontalHeaderTable{tableDirectory["hhea"]};
//        hhea.debug();
//        
//        auto maxp = MaximumProfileTable{tableDirectory["maxp"]};
//        maxp.debug();
//        
//        auto hmtx = HorizontalMetricsTable{
//            maxp.numGlyphs,
//            hhea.numberOfHMetrics,
//            tableDirectory["hmtx"]};
//        
//        // name
//        
//        auto OS_2 = OS_2andWindowsMetricsTable{tableDirectory["OS/2"]};
//        OS_2.debug();
//        
//        // These are the recommended metrics to use of the several present
//        auto ascender = OS_2.sTypoAscender; // positive
//        auto descender = OS_2.sTypoDescender; // negative
//        auto lineGap = OS_2.sTypoLineGap; // line gap
//        
//        if (tableDirectory.sfntVersion == 0x4F54544F) {
//            auto s = tableDirectory["CFF "];
//            
//            cff::parse(s._begin, s._end);
//              
//            
//        } else {
//            
//            auto loca = IndexToLocation{
//                head.indexToLocFormat,
//                maxp.numGlyphs,
//                tableDirectory["loca"]
//            };
//            
//            auto glyf = GlyphData{tableDirectory["glyf"]};
//            
//        }
//        

        
        
        
        return nullptr;
    }
    
    
} // namespace wry::otf



#if 0


struct TableDirectory {
    
#define Y \
X(uint32_t, checksum) \
X(uint32_t, offset) \
X(uint32_t, length) \

    struct TableRecord {
        
#define X(A, B) A B;
        std::array<uint8_t, 4> tableTag;
        Y
#undef X
    };
    
    static void parse_TableRecord(span<byte const>& s, TableRecord& x) {
#define X(A, B) parse_ntoh(s, x.B);
        parse_ntoh(s, x.tableTag);
        Y
#undef X
        printf("    \"%.4s\" %x %d %d\n",
               (char const*)&x.tableTag,
               x.checksum,
               x.offset,
               x.length);
    }
#undef Y
    
    
    byte const* first;
    
    std::vector<TableRecord> tableRecords;
    
    wry::span<byte const> operator[](const char* key) const {
        for (auto & a : tableRecords) {
            if (std::memcmp(key, &a.tableTag, 4) == 0)
                return {first + a.offset, a.length};
        }
        return {};
    }
    
};

void parse_TableDirectory(span<byte const>& s, TableDirectory& x) {
    x.first = s.data();
    auto n = s.size();
#define X(A, B) parse_ntoh(s, x.B);
    Y
#undef X
    assert((x.sfntVersion == 0x00010000) || (x.sfntVersion == 0x4F54544F));
    assert(x.numTables >= 9);
    assert(x.searchRange == std::bit_floor(x.numTables) * 16);
    assert(x.entrySelector == std::bit_width(x.numTables) - 1);
    assert(x.rangeShift == x.numTables * 16 - x.searchRange);
    // printf("%u %d %x %x %d\n", x.sfntVersion, x.numTables, x.searchRange, x.entrySelector, x.rangeShift);
    for (int i = 0; i != x.numTables; ++i) {
        TableDirectory::TableRecord y = {};
        TableDirectory::parse_TableRecord(s, y);
        assert(y.offset + y.length <= n);
        x.tableRecords.push_back(y);
    }
}
#undef Y


void print() {
    constexpr std::size_t buf_size = 100;
    char buffer[buf_size];
    printf("{\n");
#define X(A, B) snprintf(buffer, buf_size, "    \"%%s\" : %s,\n", formatString<A>); printf(buffer, #B, B);
    Y
#undef X
    printf("}\n");
}


template<typename T>
char const* formatString = "%%?";

template<> char const* formatString<int8_t> = "%hhd";
template<> char const* formatString<uint8_t> = "%hhu";
template<> char const* formatString<int16_t> = "%hd";
template<> char const* formatString<uint16_t> = "%hu";
template<> char const* formatString<int32_t> = "%d";
template<> char const* formatString<uint32_t> = "%u";
template<> char const* formatString<std::array<uint8_t, 4>> = "%x";


struct cmapTable {
    
    struct EncodingRecord {
        
        enum PlatformID : uint16_t { UNICODE = 0, };
        enum EncodingID : uint16_t { BMP = 3, };
        
        uint16_t platformID;
        uint16_t encodingID;
        uint32_t subtableOffset;
        
        void debug() {
            printf("    EncodingRecord {\n");
            printf("        platformID = %hu,\n", platformID);
            printf("        encodingID = %hu,\n", encodingID);
            printf("        subtableOffset = %u,\n", subtableOffset);
            printf("    },\n");
        }
        
    };
    
    struct cmapSubtableFormat4 {
        
        uint16_t format;
        uint16_t length;
        uint16_t language;
        uint16_t segCountX2;
        uint16_t searchRange;
        uint16_t entrySelector;
        uint16_t rangeShift;
        
        size_t segCount;
        wry::span<byte const> tail;
        
        enum { MISSING_GLYPH = 0xFFFF };
        
        uint16_t lookup(size_t code) {
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
        
        void debug() {
            printf("    cmapSubtableFormat4 {\n");
#define X(A) printf("    %s = %d,\n", #A, A);
            X(format)
            X(length)
            X(language)
            X(segCountX2)
            X(searchRange)
            X(entrySelector)
            X(rangeShift)
#undef X
            auto s = tail;
#define X(A)    printf("    " #A " = [\n");\
for (int i = 0; i != segCount; ++i) {\
uint16_t A;\
parse_ntoh(s, A);\
printf("        %d,\n", A);\
}\
printf("    ],\n");
            X(endCode)
            uint16_t reservedPad;
            parse_ntoh(s, reservedPad);
            printf("    reservedPad = %d,\n", reservedPad);
            X(startCode)
            X(idDelta)
            X(idRangeOffset)
#undef X
            printf("    },\n");
            
            
        }
        
    };
    
    uint16_t version;
    uint16_t numTables;
    std::vector<EncodingRecord> encodingRecords;
    
    cmapSubtableFormat4 bmp;
    
    void debug() {
        printf("CharacterToGlyphIndexMappingTable {\n");
        printf("    version = %hu,\n", version);
        printf("    numTables = %hu,\n", numTables);
        for (int i = 0; i != numTables; ++i)
            encodingRecords[i].debug();
        bmp.debug();
        printf("}\n");
    }
    
};

void parse_cmapSubtableFormat4(Bytes s, cmapTable::cmapSubtableFormat4& x) {
    wry::Reader r{s};
    
    
    
    r.read(x.format,
           x.length,
           x.language,
           x.segCountX2,
           x.searchRange,
           x.entrySelector,
           x.rangeShift);
    assert(x.format == 4);
    
    
    x.segCount = x.segCountX2 >> 1;
    x.tail = r.s;
}

void parse_EncodingRecord(Bytes& s, cmapTable::EncodingRecord& x) {
    parse_ntoh(s, x.platformID);
    parse_ntoh(s, x.encodingID);
    parse_ntoh(s, x.subtableOffset);
}

void parse_cmapTable(Bytes s, cmapTable& x) {
    Bytes t = s;
    parse_ntoh(s, x.version);
    parse_ntoh(s, x.numTables);
    for (int i = 0; i != x.numTables; ++i) {
        x.encodingRecords.emplace_back();
        parse_EncodingRecord(s, x.encodingRecords.back());
        if ((x.encodingRecords.back().platformID == cmapTable::EncodingRecord::UNICODE)
            && (x.encodingRecords.back().encodingID == cmapTable::EncodingRecord::BMP)) {
            parse_cmapSubtableFormat4(t.after(x.encodingRecords.back().subtableOffset), x.bmp);
        }
    }
    
}



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

struct LongHorMetricRecord {
    uint16_t advanceWidth;
    int16_t lsb;
};

LongHorMetricRecord const* hMetrics;

explicit HorizontalMetricsTable(uint16_t numGlyphs,
                                uint16_t numberOfHMetrics,
                                wry::span<byte const> s)
: numGlyphs{numGlyphs}
, numberOfHMetrics{numberOfHMetrics}
{
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
    return { ntoh(a.advanceWidth), ntoh(a.lsb) };
}


};


//
//    struct TableDirectory {
//#define Y \
//        X(uint32_t, sfntVersion) \
//        X(uint16_t, numTables) \
//        X(uint16_t, searchRange) \
//        X(uint16_t, entrySelector) \
//        X(uint16_t, rangeShift)
//#define X(A, B) A B;
//        Y
//#undef X
//        void parse(Bytes& s) {
//#define X(A, B) parse_ntoh(s, B);
//            Y
//#undef X
//            assert((sfntVersion == 0x00010000) || (sfntVersion == 0x4F54544F));
//            assert(numTables >= 9);
//            assert(searchRange == std::bit_floor(numTables) * 16);
//            assert(entrySelector == std::bit_width(numTables) - 1);
//            assert(rangeShift == numTables * 16 - searchRange);
//        }
//#undef Y
//    };
//
//    struct TableRecord {
//#define Y \
//        X(uint32_t, checksum) \
//        X(uint32_t, offset) \
//        X(uint32_t, length)
//#define X(A, B) A B;
//        Y
//#undef X
//        void parse(Bytes& s) {
//#define X(A, B) parse_ntoh(s, B);
//            Y
//#undef X
//        }
//#undef Y
//    };
//
//    struct cmapTable {
//#define Y \
//X(uint16_t, version) \
//X(uint16_t, numTables)
//#define X(A, B) A B;
//        Y
//#undef X
//        void parse(Bytes& s) {
//#define X(A, B) parse_ntoh(s, B);
//            Y
//#undef X
//        }
//#undef Y
//    };
//
//    struct EncodingRecord {
//#define Y \
//X(uint16_t, platformID) \
//X(uint16_t, encodingID) \
//X(uint32_t, subtableOffset)
//#define X(A, B) A B;
//        Y
//#undef X
//        void parse(Bytes& s) {
//#define X(A, B) parse_ntoh(s, B);
//            Y
//#undef X
//        }
//#undef Y
//    };
//
//    struct cmapSubtableFormat4 {
//#define Y \
//        X(uint16_t, format)\
//        X(uint16_t, length)\
//        X(uint16_t, language)\
//        X(uint16_t, segCountX2)\
//        X(uint16_t, searchRange)\
//        X(uint16_t, entrySelector)\
//        X(uint16_t, rangeShift)
//#define X(A, B) A B;
//        Y
//#undef X
//        void parse(Bytes& s) {
//#define X(A, B) parse_ntoh(s, B);
//            Y
//#undef X
//        }
//#undef Y
//    };
//
#endif
