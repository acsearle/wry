//
//  number_caption.hpp
//  client
//
//  Created by Antony Searle on 2026-11-08.
//
//  The text printed on a number tile (WryWorldScene.mm emit_number and
//  the held-value cursor): at most four characters plus an optional
//  leading minus, so the row never shrinks past legibility.
//
//  |n| < 10000 is printed in full ("9999", "-42").  Larger magnitudes
//  are Factorio style, truncated towards zero to the largest SI unit
//  (k, M, G, T, P, E = 10^3 .. 10^18) that leaves a mantissa of at least
//  one: a mantissa of 10 .. 999 is printed whole ("12k", "999k",
//  "123M"), one of 1 .. 9 with one truncated decimal ("1.2M", "9.9M"
//  for 9999999).  Nothing rounds up into the next unit, so the result is
//  never five characters; 10000 .. 999999 are "10k" .. "999k" (their
//  mantissa is never below ten), and the int64 extremes are "9.2E" and
//  "-9.2E".
//
//  The alphabet is "-0123456789.kMGTPE"; the renderer has a glyph for
//  each (the cutout atlas, assets.json).
//

#ifndef number_caption_hpp
#define number_caption_hpp

#include <cstdint>
#include <string>

namespace wry {

    std::string number_caption(std::int64_t value);

} // namespace wry

#endif /* number_caption_hpp */
