//
//  svg.hpp
//  client
//
//  Created by Antony Searle on 8/3/2026.
//

#ifndef svg_hpp
#define svg_hpp

#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "bezier.hpp"
#include "simd.hpp"
#include "sRGB.hpp"
#include "matrix.hpp"

namespace wry::svg {

    // Partial SVG loading, to geometry rather than to pixels.
    //
    // The intent is symbols of comparable complexity to text glyphs (icon
    // sheets, OpenType 'SVG ' glyphs), not general rendering.  A document
    // becomes a list of flat-coloured Layers, each a list of cubic Beziers
    // forming closed contours: exactly the representation the font engine
    // gets from otf::Handle::outline_for_glyph_index, and rasterized by the
    // same code (bezier.hpp: bezier4_coverage / rasterize_coverage on the
    // CPU, Shaders.metal::otf::bezierFragmentFunction on the GPU, which
    // consumes the curves packed as otf::CubicBezier + otf::GlyphData the
    // way build_font2 packs glyphs).
    //
    // Supported:
    // - <svg> (root and nested) with width / height / viewBox and the
    //   default preserveAspectRatio (xMidYMid meet) or "none"
    // - <g>, <a>, <defs>, <use> (href / xlink:href, x, y; <symbol>
    //   targets with viewBox), transform= on any element (matrix,
    //   translate, scale, rotate, skewX, skewY), composed down the tree
    // - <path d> with the full path grammar; arcs become cubics
    // - <rect> (rx / ry), <circle>, <ellipse>, <line>, <polyline>,
    //   <polygon>
    // - fill, fill-rule, fill-opacity, opacity, color / currentColor,
    //   display, visibility, as attributes or in style="..."
    // - stroke, stroke-width, stroke-opacity, stroke-linecap,
    //   stroke-linejoin, stroke-miterlimit: a stroke is expanded to a
    //   union of filled pieces (segment bodies, join wedges, caps), which
    //   the clamped winding sum of the rasterizer unions for free
    // - colours: #rgb #rgba #rrggbb #rrggbbaa, rgb() / rgba(), the CSS
    //   basic named colours, none, transparent
    //
    // Approximated (counted in Document::skipped):
    // - gradient paint: flat fill with the mean stop colour
    // - group opacity: multiplied into each descendant's alpha, which
    //   differs from SVG where descendants overlap
    // - fill rules: contours are reoriented so the rasterizer's clamped
    //   winding sum reproduces the rule; exact for contours that do not
    //   cross one another, approximate where they do
    //
    // Skipped (counted in Document::skipped): text, images, filters,
    // masks, clip paths, markers, patterns, dashes, CSS <style> sheets,
    // other preserveAspectRatio values.

    struct Layer {
        // Straight (not premultiplied) RGBA; rgb sRGB-encoded as authored,
        // alpha includes fill / stroke opacity and ancestor opacity.
        simd_float4 color;
        // Closed contours, oriented so that the clamped bezier4_coverage
        // is the paint's coverage.
        std::vector<bezier4> curves;
        bool is_stroke;
    };

    struct Document {
        // The viewport, in output units: the root width / height (px),
        // else the viewBox size.  For OpenType glyphs, zero.
        simd_float2 size = {0.0f, 0.0f};
        // In painting order.
        std::vector<Layer> layers;
        // Every element in the source tree, by name.
        std::map<std::string, int> elements;
        // Unsupported or approximated features met while rendering, by
        // description.
        std::map<std::string, int> skipped;
        // Empty unless the XML was malformed or a path had an error (SVG
        // renders a path up to its first error, as we do).
        std::string error;

        bool ok() const { return error.empty(); }
    };

    // Output coordinates are the root viewport's user units (px), flipped
    // to y-up with the origin at the bottom left: (x, size.y - y).  This is
    // the frame bezier4_coverage and the font outlines use.
    Document parse(std::string_view source);

    // An OpenType 'SVG ' table document: renders only the element with
    // id="glyph<glyph_index>" (and what it references).  Output is in font
    // design units, y-up, with the origin at the glyph origin: (x, -y),
    // matching otf::Handle::outline_for_glyph_index.  The root's viewBox
    // is not applied (fonts in the wild leave it absent or equal to the
    // em square).  gzip-compressed documents are reported as an error.
    Document parse_otf_glyph(std::string_view document, int glyph_index);

    // Composite the layers into an image covering the output-space box
    // [lo, hi], at pixels_per_unit, over an opaque background (linear RGB),
    // in linear light.  Each layer's coverage is bezier4_coverage.
    matrix<RGBA8Unorm_sRGB> rasterize(Document const& doc,
                                      float pixels_per_unit,
                                      simd_float2 lo, simd_float2 hi,
                                      simd_float3 background);

    // ---- Exposed for tests -----------------------------------------------

    struct Affine {
        // x' = a x + c y + e, y' = b x + d y + f
        double a = 1, b = 0, c = 0, d = 1, e = 0, f = 0;
        simd_double2 operator()(simd_double2 p) const {
            return simd_double2{a * p.x + c * p.y + e, b * p.x + d * p.y + f};
        }
        // (L * R)(p) == L(R(p))
        friend Affine operator*(Affine const& L, Affine const& R) {
            return Affine{
                L.a * R.a + L.c * R.b, L.b * R.a + L.d * R.b,
                L.a * R.c + L.c * R.d, L.b * R.c + L.d * R.d,
                L.a * R.e + L.c * R.f + L.e, L.b * R.e + L.d * R.f + L.f,
            };
        }
    };

    // Parses a transform list; false (and identity) on a syntax error.
    bool parse_transform(std::string_view, Affine&);

    // Parses a colour; false if it is not one.
    bool parse_color(std::string_view, simd_float4&);

    struct Cubic {
        simd_double2 p[4];
        bool line; // straight; p[1], p[2] at thirds
    };

    struct Subpath {
        std::vector<Cubic> segments;
        simd_double2 start = {0, 0};
        bool closed = false;
        // A drawing command was given (a zero-length subpath with one
        // still gets caps).
        bool drawn = false;
    };

    using Path = std::vector<Subpath>;

    // Parses path data, appending to `out`.  On a syntax error returns
    // false with `out` holding everything before the error.
    bool parse_path_data(std::string_view d, Path& out);

    // The SVG elliptical arc from `from` to `to` (SVG 1.1 F.6), appended
    // as cubics of at most 90 degrees each.
    void append_arc(Subpath&, simd_double2 from, double rx, double ry,
                    double x_axis_rotation_degrees, bool large_arc,
                    bool sweep, simd_double2 to);

} // namespace wry::svg

#endif /* svg_hpp */
