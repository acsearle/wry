//
//  otf.cpp
//  client
//
//  Created by Antony Searle on 26/3/2026.
//

#include <cassert>

#include <array>
#include <vector>
#include "simd.hpp"

#include "NetworkToHostReader.hpp"
#include "cff.hpp"
#include "otf.hpp"

namespace wry::otf {
    
#define Y \
X(uint32_t, checksum) \
X(uint32_t, offset) \
X(uint32_t, length) \

    struct TableRecord {
        
        std::array<uint8_t, 4> tableTag;
#define X(A, B) A B;
        Y
#undef X
    };
    
    void parse_TableRecord(span<byte const>& s, TableRecord& x) {
        std::memcpy(&x.tableTag, s.data(), 4);
        s.drop_front(4);
#define X(A, B) parse_ntoh(s, x.B);
        Y
#undef X
        printf("    \"%.4s\" %x %d %d\n",
               (char const*)&x.tableTag,
               x.checksum,
               x.offset,
               x.length);
    }
#undef Y
    
    
    
    struct TableDirectory {
        
#define Y \
X(uint32_t, sfntVersion) \
X(uint16_t, numTables) \
X(uint16_t, searchRange) \
X(uint16_t, entrySelector) \
X(uint16_t, rangeShift) \

#define X(A, B) A B;
        Y
#undef X
        
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
        // printf("%u %d %x %x %d\n", x.sfntVersion, x.numTables, x.searchRange, x.entrySelector, x.rangeShift);
        for (int i = 0; i != x.numTables; ++i) {
            TableRecord y = {};
            parse_TableRecord(s, y);
            assert(y.offset + y.length <= n);
            x.tableRecords.push_back(y);
        }
    }
#undef Y
    
    /*
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
     */
    
    struct cmapTable {
        
        uint16_t version;
        uint16_t numTables;
        
        enum {
            // PlatformID
            UNICODE = 0,
            
            // EncodingID
            BMP = 3,
        };
        
        struct EncodingRecord {
            
            uint16_t platformID;
            uint16_t encodingID;
            uint32_t subtableOffset;
            
            explicit EncodingRecord(wry::Reader& r) {
                r.read(platformID, encodingID, subtableOffset);
            }
            
            void debug() {
                printf("    EncodingRecord {\n");
                printf("        platformID = %hu,\n", platformID);
                printf("        encodingID = %hu,\n", encodingID);
                printf("        subtableOffset = %u,\n", subtableOffset);
                printf("    },\n");
            }
            
        };
        
        std::vector<EncodingRecord> encodingRecords;
        
        struct cmapSubtableFormat4 {
            
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
            
            cmapSubtableFormat4() = default;
            
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
        
        cmapSubtableFormat4 bmp;
        
        
        explicit cmapTable(wry::span<byte const> s)
        : bmp{} {
            wry::Reader r{s};
            r.read(version, numTables);
            for (int i = 0; i != numTables; ++i) {
                encodingRecords.emplace_back(r);
                if ((encodingRecords.back().platformID == UNICODE)
                    && (encodingRecords.back().encodingID == BMP)) {
                    bmp = cmapSubtableFormat4(s.after(encodingRecords.back().subtableOffset));
                }
            }
            
        }
        
        void debug() {
            printf("CharacterToGlyphIndexMappingTable {\n");
            printf("    version = %hu,\n", version);
            printf("    numTables = %hu,\n", numTables);
            for (int i = 0; i != numTables; ++i)
                encodingRecords[i].debug();
            printf("}\n");
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
    
    
    
    void* parse(byte const* first, byte const* last) {
        
        printf("parsing .otf of %zd bytes\n", last - first);
        
        span<const byte> s{first, last};
        
        TableDirectory tableDirectory;
        parse_TableDirectory(s, tableDirectory);
        
        auto cmap = cmapTable{tableDirectory["cmap"]};
        cmap.debug();
        
        auto head = FontHeaderTable{tableDirectory["head"]};
        head.debug();
        
        auto hhea = HorizontalHeaderTable{tableDirectory["hhea"]};
        hhea.debug();
        
        auto maxp = MaximumProfileTable{tableDirectory["maxp"]};
        maxp.debug();
        
        auto hmtx = HorizontalMetricsTable{
            maxp.numGlyphs,
            hhea.numberOfHMetrics,
            tableDirectory["hmtx"]};
        
        // name
        
        auto OS_2 = OS_2andWindowsMetricsTable{tableDirectory["OS/2"]};
        OS_2.debug();
        
        if (tableDirectory.sfntVersion == 0x4F54544F) {
            auto s = tableDirectory["CFF "];
            
            cff::parse(s._begin, s._end);
              
            
        } else {
            
            auto loca = IndexToLocation{
                head.indexToLocFormat,
                maxp.numGlyphs,
                tableDirectory["loca"]
            };
            
            auto glyf = GlyphData{tableDirectory["glyf"]};
            
        }
        

        
        
        
        return nullptr;
    }
    
    
} // namespace wry::otf



