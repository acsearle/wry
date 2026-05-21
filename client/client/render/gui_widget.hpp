//
//  gui_widget.hpp
//  client
//
//  Created by Antony Searle on 2026-05-21.
//
//  Widget layout / event / paint primitives.  Constraints-down, sizes-up.
//
//  Two coordinate domains run through this file and it's worth being
//  pedantic about which is which:
//
//    *Size space* is one-dimensional in spirit: widths and heights, no
//    location.  `Size` is a (w, h) pair; `SizeConstraints` is a (min, max)
//    range over `Size`.  The measure pass lives entirely in size space:
//    parent hands child a `SizeConstraints`, child returns a chosen
//    `Size`.  Nothing in this pass has any notion of "where on the screen."
//
//    *Position space* is screen-space (logical points, top-left origin,
//    y-down).  `rect<float>` lives here.  The arrange pass crosses the
//    boundary: the parent has both child sizes (size space) and its own
//    rect (position space), and assigns each child an absolute screen
//    rect.  After arrange, paint and event-dispatch operate purely in
//    position space.
//
//  The two-pass shape:
//
//      Size measure(SizeConstraints c, MeasureContext const& ctx)
//          Parent imposes `c`; child picks a Size within those bounds and
//          stores it (`_last_measured_size`) for the arrange pass.  Pure
//          function of inputs -- no side effects beyond caching, no
//          recursion into a second pass for dependent layouts.
//
//      void arrange(rect<float> r)
//          Parent fixes the child's absolute screen-space rect.  Child
//          stores `r` and recursively arranges its own children using
//          their cached measured sizes.
//
//      bool on_event(Event const&)
//          Hit-tests against the cached rect and either consumes or
//          forwards.  Cached rects are from the previous frame's paint.
//
//      void paint(Painter&)
//          Draws using the cached rect.  Parents call paint on children
//          after arranging them.
//

#ifndef gui_widget_hpp
#define gui_widget_hpp

#include <algorithm>
#include <functional>
#include <memory>
#include <vector>

#include "gui_event.hpp"
#include "gui_overlay.hpp"
#include "rect.hpp"
#include "simd.hpp"
#include "string.hpp"

namespace wry::gui {

    // A pure size in size-space: width and height, no position.  Distinct
    // from float2 (which is the catch-all 2D vector) so the type system
    // catches accidental mixing of "this thing is 100x40 tall" with "this
    // thing is at (100, 40) on screen."  Convert explicitly at boundaries.
    struct Size {
        float w = 0.0f;
        float h = 0.0f;
    };

    // A min/max range over Size.  Structurally a rect-in-size-space, but
    // semantically a constraint, so kept as its own type to keep position-
    // space and size-space from being interchangeable at call sites.
    //
    // `constrain(s)` clamps `s` componentwise into the allowed range -- the
    // canonical way for a Widget to honour its constraints when its
    // natural size would fall outside them.  `loose` and `tight` are
    // the two factory shapes you almost always want.
    struct SizeConstraints {
        Size min;
        Size max;

        Size constrain(Size s) const {
            return Size{
                std::clamp(s.w, min.w, max.w),
                std::clamp(s.h, min.h, max.h),
            };
        }

        static SizeConstraints loose(Size m) {
            return SizeConstraints{ Size{0.0f, 0.0f}, m };
        }
        // Convenience for the very common "the viewport gives us float2"
        // case -- the explicit conversion crosses the size-space boundary
        // visibly at the call site.
        static SizeConstraints loose(float2 m) {
            return loose(Size{ m.x, m.y });
        }

        static SizeConstraints tight(Size s) {
            return SizeConstraints{ s, s };
        }
        static SizeConstraints tight(float2 s) {
            return tight(Size{ s.x, s.y });
        }
    };

    // Inputs to the measure pass that aren't constraints.  Anything a
    // widget needs to compute its natural size goes here -- today that's
    // just the font, but theme / dpi / locale / fallback metrics will all
    // accumulate here as the GUI grows.
    struct MeasureContext {
        Font const* font = nullptr;
    };

    class Widget {
    public:
        virtual ~Widget() = default;

        // Size-space pass.  Pick a Size within `c`, using `ctx` for any
        // measurement that depends on font / theme / dpi.  Implementations
        // should cache the chosen size in `_last_measured_size`; the
        // parent reads it during the arrange pass to position children
        // without re-measuring.  No knowledge of absolute position.
        virtual Size measure(SizeConstraints c,
                             MeasureContext const& ctx) = 0;

        // Size-space -> position-space transition.  The parent has chosen
        // a screen-space rect for this child; the child stores `r` and
        // recursively arranges its own children using their cached sizes.
        virtual void arrange(rect<float> r) = 0;

        // Hit-test cached rect; consume or forward.
        virtual bool on_event(Event const&) { return false; }

        // Paint using the cached rect.
        virtual void paint(Painter&) {}

        Size last_measured_size() const { return _last_measured_size; }

    protected:
        Size _last_measured_size = {};
    };

    // ----------------------------------------------------------------
    // Button: a text label inside a coloured rectangle that fires its
    // on_click callback when the left mouse button is released over it.
    // Hover and pressed visuals are colour shifts on the background.

    class Button : public Widget {
    public:
        // Take StringView so callers can pass `"CONTINUE"` literals: the
        // implicit `const char*` -> StringView path is fine, and we
        // explicitly construct a String for the owned label inside.
        // Passing an existing String also works (implicit String -> StringView).
        Button(StringView label, std::function<void()> on_click);

        Size measure(SizeConstraints, MeasureContext const&) override;
        void arrange(rect<float>) override;
        bool on_event(Event const&) override;
        void paint(Painter&) override;

    private:
        // Padding around the label (left/right and overall height padding).
        // Width is `text_width + 2 * kHorizPadding`, clamped to a sensible
        // minimum so empty / very short labels still look like buttons.
        // Height is `font_cell_height + 2 * kVertPadding`, computed in
        // measure() so the button always fits its font.
        static constexpr float kHorizPadding = 16.0f;
        static constexpr float kVertPadding  = 8.0f;
        static constexpr float kMinWidth     = 80.0f;
        // Fallback height if measure() is called without a font (shouldn't
        // happen in practice but keeps the widget defensible).
        static constexpr float kFallbackHeight = 48.0f;

        String _label;
        std::function<void()> _on_click;
        rect<float> _rect = {0, 0, 0, 0};
        bool _hover   = false;
        bool _pressed = false;
    };

    // ----------------------------------------------------------------
    // Column: stacks children top-to-bottom with uniform spacing, and
    // stretches them on the cross axis to the column's full width during
    // the arrange pass.

    class Column : public Widget {
    public:
        // Take ownership of `child`.  Returns *this so adds can chain.
        Column& add(std::unique_ptr<Widget> child) {
            _children.push_back(std::move(child));
            return *this;
        }

        void set_spacing(float s) { _spacing = s; }

        Size measure(SizeConstraints, MeasureContext const&) override;
        // Stretches children to the column's measured width during the
        // arrange pass (cross-axis fill).  Children must accept being
        // given a wider rect than they asked for in measure() -- Button
        // does this naturally by simply filling its rect.
        void arrange(rect<float>) override;
        bool on_event(Event const&) override;
        void paint(Painter&) override;

    private:
        std::vector<std::unique_ptr<Widget>> _children;
        float _spacing = 8.0f;
        rect<float> _rect = {0, 0, 0, 0};
    };

} // namespace wry::gui

#endif // gui_widget_hpp
