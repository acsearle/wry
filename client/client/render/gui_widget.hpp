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
#include <limits>
#include <memory>
#include <optional>
#include <vector>

#include "gui_event.hpp"
#include "gui_overlay.hpp"
#include "rect.hpp"
#include "simd.hpp"
#include "string.hpp"
#include "functional.hpp"

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

        // Introspection, for the `widgets` and `press` commands
        // (core/docs/commands.md section 7): what the widget is, its text
        // if it shows one, the rect it was last arranged into (drawable
        // pixels), whether it clips its children to that rect, and its
        // children.  The defaults describe an anonymous childless leaf.
        virtual const char* kind() const { return "Widget"; }
        virtual StringView label() const { return {}; }
        virtual rect<float> bounds() const { return {}; }
        virtual bool clips_children() const { return false; }
        using ChildVisitor = std::function<void(Widget const&)>;
        virtual void for_each_child(ChildVisitor const&) const {}

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
        Button(StringView label, move_only_function<void()> on_click);

        Size measure(SizeConstraints, MeasureContext const&) override;
        void arrange(rect<float>) override;
        bool on_event(Event const&) override;
        void paint(Painter&) override;

        // External selection state.  Buttons used as list rows toggle this
        // from outside (the containing overlay maintains the selection
        // index); plain action buttons leave it default-false.  When set,
        // the button paints with a distinct background to mark the row.
        void set_selected(bool s) { _selected = s; }
        bool is_selected() const  { return _selected; }

        // Per-corner background radii, ordered (x0,y0) (x1,y0) (x1,y1)
        // (x0,y1).  Containers that group buttons set these so that only
        // the group's outer corners are rounded.  Unset: every corner gets
        // the default, proportional to the font cell.
        void set_corner_radii(float4 r) { _corner_radii = r; }

        // Background color for the given state.  Two palettes -- default
        // (gray) and selected (blue) -- each with hover and pressed
        // variants.  Public so other clickable cells (the opcode palette)
        // match buttons.
        static RGBA8Unorm_sRGB background_color(bool selected, bool hover, bool pressed);

        // The default background corner radius: a quarter of the font's
        // ascender-descender cell, so it scales with the text.
        static float default_corner_radius(Font const* font) {
            return font ? 0.25f * (font->ascender - font->descender) : 0.0f;
        }

        // The rect this Button was last arranged into (screen-space).
        // Used by parents that want to scroll a particular row into view.
        // Degenerate {0,0,0,0} before the first arrange.
        rect<float> arranged_rect() const { return _rect; }

        const char* kind() const override { return "Button"; }
        StringView label() const override { return _label; }
        rect<float> bounds() const override { return _rect; }

        // Padding constants are public so containers that need to size a
        // ScrollView to "N + 0.5 rows" can ask for the row height without
        // having to measure a sample button.
        static constexpr float kHorizPadding = 16.0f;
        static constexpr float kVertPadding  = 8.0f;
        static constexpr float kMinWidth     = 80.0f;

    private:
        // Fallback height if measure() is called without a font (shouldn't
        // happen in practice but keeps the widget defensible).
        static constexpr float kFallbackHeight = 48.0f;

        String _label;
        move_only_function<void()> _on_click;
        rect<float> _rect = {0, 0, 0, 0};
        bool _hover    = false;
        bool _pressed  = false;
        bool _selected = false;
        std::optional<float4> _corner_radii;
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

        const char* kind() const override { return "Column"; }
        rect<float> bounds() const override { return _rect; }
        void for_each_child(ChildVisitor const& f) const override {
            for (auto const& c : _children)
                f(*c);
        }

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

    // ----------------------------------------------------------------
    // Panel: a dark rounded plate around one child, casting a drop shadow.
    // The child is inset by a margin of one font stem width, so cells the
    // child paints flush to its edges read as outlined by the panel.  The
    // panel's corner radius is the button default radius plus the margin,
    // so a cell rounded at the default radius in a panel corner is
    // concentric with it.  Margin and radius derive from the font at
    // measure time.

    class Panel : public Widget {
    public:
        explicit Panel(std::unique_ptr<Widget> child = nullptr)
        : _child(std::move(child)) {}

        void set_child(std::unique_ptr<Widget> c) { _child = std::move(c); }

        const char* kind() const override { return "Panel"; }
        rect<float> bounds() const override { return _rect; }
        void for_each_child(ChildVisitor const& f) const override {
            if (_child)
                f(*_child);
        }

        Size measure(SizeConstraints, MeasureContext const&) override;
        void arrange(rect<float>) override;
        bool on_event(Event const&) override;
        void paint(Painter&) override;

        // The margin, which callers also use as the gap between cells
        // inside the panel: one font stem width.
        static float gap(Font const* font) {
            return font ? font->stem_width : 0.0f;
        }

        // Linear (pre-sRGB-encoding) gray of the panel, darker than the
        // default button background (0.08).
        static constexpr float kPanelGray = 0.02f;

    protected:
        std::unique_ptr<Widget> _child;
        float _gap = 0.0f;
        float _radius = 0.0f;
        rect<float> _rect = {0, 0, 0, 0};
    };

    // ----------------------------------------------------------------
    // ButtonPanel: a column of Buttons on a Panel, one gap apart.  By
    // default every button corner is rounded (grouping related items, as
    // Blender does); with round_inner_corners false, only the corners at
    // the panel's corners are, concentrically with it.

    class ButtonPanel : public Panel {
    public:
        ButtonPanel();

        // Take ownership of `button`.  Returns *this so adds can chain.
        ButtonPanel& add(std::unique_ptr<Button> button) {
            _buttons.push_back(button.get());
            _column->add(std::move(button));
            return *this;
        }

        void set_round_inner_corners(bool b) { _round_inner = b; }

        const char* kind() const override { return "ButtonPanel"; }

        Size measure(SizeConstraints, MeasureContext const&) override;

    private:
        Column* _column;                // non-owning, = _child
        std::vector<Button*> _buttons;  // non-owning, into _column
        bool _round_inner = true;
    };

    // ----------------------------------------------------------------
    // TextCell: a static line of text on a rounded background, sized like
    // a Button, for read-only cells in rows of buttons.  Text is left-
    // aligned with Button's horizontal padding and cap-centered.

    class TextCell : public Widget {
    public:
        explicit TextCell(StringView text) : _text(text) {}

        // Same ordering and default as Button::set_corner_radii.
        void set_corner_radii(float4 r) { _corner_radii = r; }

        const char* kind() const override { return "TextCell"; }
        StringView label() const override { return _text; }
        rect<float> bounds() const override { return _rect; }

        Size measure(SizeConstraints, MeasureContext const&) override;
        void arrange(rect<float> r) override { _rect = r; }
        bool on_event(Event const&) override { return false; }
        void paint(Painter&) override;

        // Linear gray: between the panel (0.02) and a button (0.08), so
        // read-only cells are distinct from clickable ones.
        static constexpr float kCellGray = 0.045f;

    private:
        String _text;
        rect<float> _rect = {0, 0, 0, 0};
        std::optional<float4> _corner_radii;
    };

    // ----------------------------------------------------------------
    // Row: the horizontal sibling of Column.  Lays children left-to-right
    // with uniform spacing.  Cross axis (height) stretches each child to
    // the row's height; main axis (width) keeps each child's natural width.

    class Row : public Widget {
    public:
        Row& add(std::unique_ptr<Widget> child) {
            _children.push_back(std::move(child));
            return *this;
        }

        void set_spacing(float s) { _spacing = s; }

        const char* kind() const override { return "Row"; }
        rect<float> bounds() const override { return _rect; }
        void for_each_child(ChildVisitor const& f) const override {
            for (auto const& c : _children)
                f(*c);
        }

        Size measure(SizeConstraints, MeasureContext const&) override;
        void arrange(rect<float>) override;
        bool on_event(Event const&) override;
        void paint(Painter&) override;

    private:
        std::vector<std::unique_ptr<Widget>> _children;
        float _spacing = 8.0f;
        rect<float> _rect = {0, 0, 0, 0};
    };

    // ----------------------------------------------------------------
    // Label: a static line of text.  No background, no events, no states.
    // Sized to the text's measured width and the font's glyph cell height.

    class Label : public Widget {
    public:
        explicit Label(StringView text) : _text(text) {}

        const char* kind() const override { return "Label"; }
        StringView label() const override { return _text; }
        rect<float> bounds() const override { return _rect; }

        Size measure(SizeConstraints, MeasureContext const&) override;
        void arrange(rect<float>) override;
        void paint(Painter&) override;

    private:
        String _text;
        rect<float> _rect = {0, 0, 0, 0};
    };

    // ----------------------------------------------------------------
    // MinWidth: single-child wrapper that widens the child's measured
    // width to at least `min_w`.  Rows built from naturally-sized
    // widgets are ragged; wrapping each cell in a MinWidth aligns
    // unrelated rows into columns (a poor man's table grid).  The child
    // is arranged into the full widened rect -- children already accept
    // being stretched beyond their measured size by Column / Row.

    class MinWidth : public Widget {
    public:
        MinWidth(float min_w, std::unique_ptr<Widget> child)
        : _min_w(min_w)
        , _child(std::move(child)) {}

        // Arranged into exactly its child's rect.
        const char* kind() const override { return "MinWidth"; }
        rect<float> bounds() const override { return _child ? _child->bounds() : rect<float>{}; }
        void for_each_child(ChildVisitor const& f) const override {
            if (_child)
                f(*_child);
        }

        Size measure(SizeConstraints, MeasureContext const&) override;
        void arrange(rect<float>) override;
        bool on_event(Event const&) override;
        void paint(Painter&) override;

    private:
        float _min_w = 0.0f;
        std::unique_ptr<Widget> _child;
    };

    // ----------------------------------------------------------------
    // ScrollView: single-child widget that clips and translates its child.
    //
    // Sizing.  measure() picks a width matching the constraints' cross
    // axis and a height that is the smallest of:
    //   * the constraint's max.h,
    //   * the height cap if one was set (set_height_cap; used by the save
    //     list to pin the view to "N + 0.5 row heights"),
    //   * the child's measured height (so we don't reserve dead space
    //     when the content fits entirely).
    //
    // Layout.  The child is measured with a loose vertical constraint
    // (it can be as tall as it needs to be) and a tight horizontal
    // constraint (it fills our cross axis).  arrange() places the child
    // at our top edge minus the current scroll offset, so the child rect
    // lives in *our* screen-space (translated by -offset).
    //
    // Events.  Scroll events adjust the offset (only when the cursor is
    // over us).  Other positional events are forwarded to the child only
    // when the location is inside our rect, so clicks on the clipped
    // portion of a partially-visible child don't activate.
    //
    // Painting.  push_clip(_rect) around the child paint, so children
    // outside our bounds are CPU-cropped.

    class ScrollView : public Widget {
    public:
        void set_child(std::unique_ptr<Widget> c) { _child = std::move(c); }
        Widget* child() const { return _child.get(); }

        const char* kind() const override { return "ScrollView"; }
        rect<float> bounds() const override { return _rect; }
        bool clips_children() const override { return true; }
        void for_each_child(ChildVisitor const& f) const override {
            if (_child)
                f(*_child);
        }

        // Upper bound on the view height in size-space.  See class comment.
        void set_height_cap(float h) { _height_cap = h; }

        // Inspection / programmatic scroll.
        float content_height() const { return _content_h; }
        float view_height()    const { return _rect.b.y - _rect.a.y; }
        float scroll_offset()  const { return _offset; }
        void  set_scroll_offset(float o);

        // Adjust _offset so that `target` (a screen-space rect, typically
        // a child widget's last-arranged rect) fits within the visible
        // band.  No-op if `target` is degenerate or already visible.
        void scroll_into_view(rect<float> target);

        Size measure(SizeConstraints, MeasureContext const&) override;
        void arrange(rect<float>) override;
        bool on_event(Event const&) override;
        void paint(Painter&) override;

    private:
        std::unique_ptr<Widget> _child;
        rect<float> _rect = {0, 0, 0, 0};
        float _offset = 0.0f;
        float _content_h = 0.0f;
        float _height_cap = std::numeric_limits<float>::infinity();

        void clamp_offset();
        void arrange_child();
    };

} // namespace wry::gui

#endif // gui_widget_hpp
