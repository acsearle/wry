//
//  debug.cpp
//  client
//
//  Created by Antony Searle on 26/6/2023.
//

#include <mach/mach_time.h>

#include "debug.hpp"

namespace wry {
    
    timer::timer(char const* context)
    : _begin(mach_absolute_time())
    , _context(context) {
    }
    
    timer::~timer() {
        printf("%s: %gms\n", _context, (mach_absolute_time() - _begin) * 1e-6);
    }
    
    
    
} // namespace manic
