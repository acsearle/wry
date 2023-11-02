//
//  packed.hpp
//  client
//
//  Created by Antony Searle on 11/9/2023.
//

#ifndef packed_hpp
#define packed_hpp

#include "stdfloat.hpp"
#include "stdint.hpp"

// Note that
//
//     __attribute__((__ext_vector_type__(3),__aligned__(4))) float
//
// is 4-byte aligned but still 16-byte sized, probably because it has to map
// directly to a SIMD register size.  So while the alignment can be relaxed, we
// can't get rid of the padding.  This is why simd_packed_TN are all powers of
// two.
//
// We define some packed...3 structures that are not padded, but are
// also not __ext_vector_type__ and will have to be elementwise copied over to
// the corresponding simd types.
//
// Compare MTLPackedFloat3, MTLPackedFloat4x3

namespace wry::simd::packed {
    
#define WRY_DEFINE_PACKED_T( T )\
struct T##3 { T x, y, z; };\
struct T##2x3 { T##3 columns[2]; };\
struct T##3x3 { T##3 columns[3]; };\
struct T##4x3 { T##3 columns[4]; };
    
    WRY_DEFINE_PACKED_T(char)
    WRY_DEFINE_PACKED_T(uchar)
    WRY_DEFINE_PACKED_T(short)
    WRY_DEFINE_PACKED_T(ushort)
    WRY_DEFINE_PACKED_T(int)
    WRY_DEFINE_PACKED_T(uint)
    WRY_DEFINE_PACKED_T(long)
    WRY_DEFINE_PACKED_T(ulong)
    WRY_DEFINE_PACKED_T(half)
    WRY_DEFINE_PACKED_T(float)
    WRY_DEFINE_PACKED_T(double)
    
#undef WRY_DEFINE_PACKED_T
    
    // sanity
    static_assert(alignof(float3) == 4, "wry::packed::float3 is over-aligned");
    static_assert(sizeof(float3) == 12, "wry::packed::float3 is padded");
    
} // namespace wry::simd::packed

#endif /* packed_hpp */
