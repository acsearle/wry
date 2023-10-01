//
//  packed.hpp
//  client
//
//  Created by Antony Searle on 11/9/2023.
//

#ifndef packed_hpp
#define packed_hpp

#include "opencl.h"

#define DEFINE_PACKED_T( T )\
typedef struct { T x, y, z; } packed_##T##3;\
typedef struct { packed_##T##3 columns[2]; } packed_##T##2x3;\
typedef struct { packed_##T##3 columns[3]; } packed_##T##3x3;\
typedef struct { packed_##T##3 columns[4]; } packed_##T##4x3;

DEFINE_PACKED_T( char )
DEFINE_PACKED_T( uchar )
DEFINE_PACKED_T( short )
DEFINE_PACKED_T( ushort )
DEFINE_PACKED_T( int )
DEFINE_PACKED_T( uint )
DEFINE_PACKED_T( long )
DEFINE_PACKED_T( ulong )
DEFINE_PACKED_T( half )
DEFINE_PACKED_T( float )
DEFINE_PACKED_T( double )

#endif /* packed_hpp */
