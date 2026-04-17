//
//  otf.hpp
//  client
//
//  Created by Antony Searle on 26/3/2026.
//

#ifndef otf_hpp
#define otf_hpp

// Open Type Format
//
// https://learn.microsoft.com/en-us/typography/opentype/spec/

namespace wry::otf {

    using byte = unsigned char;
    
    void* parse(byte const* first, byte const* last);
    
    // TODO:
    
    // Many decisions
    
    // Path format: Just a list of cubic Bezier curves, in no particular order,
    // that should form some number of oriented closed paths to be filled
    
    // Some fonts will be second order, but it's easier to promote them to
    // cubics (and lines to cubics) than to support a hetrogenous layout
        
    // For small Roman fonts, we can dump all the basic data
    // Global: Ascent, descent, line spacing
    // Per character:
    //   Bounding box, advance
    //   Cubic bezier curves
    
    // More advanced usage allows
    // - character existence and on-demand loading
    // - compound glyphs
    // - parameterization
    // - ligatures and basic kerning
    
    // We don't even attempt text shaping
    
        
    
} // namespace wry::otf

#endif /* otf_hpp */
