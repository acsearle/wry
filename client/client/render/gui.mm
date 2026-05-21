//
//  gui.cpp
//  client
//
//  Created by Antony Searle on 29/12/2025.
//

#include "gui.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "gui_event.hpp"
#include "gui_overlay.hpp"
#include "gui_widget.hpp"
#include "model.hpp"
#include "SpriteAtlas.hpp"
#include "font.hpp"
#include "text.hpp"

namespace wry {

    namespace gui {

        // ================================================================
        // Painter::draw_text_run
        // ================================================================

        float2 Painter::draw_text_run(float2 pen,
                                      StringView text,
                                      RGBA8Unorm_sRGB color) {
            if (!font || !atlas) return pen;

            // Walk the run.  `pen` is the baseline position; the per-glyph
            // sprite is positioned relative to that by SpriteAtlas::place
            // (origin = -bitmap_left, -bitmap_top so `s + pen` lands the
            // glyph correctly).
            //
            // No linebreaking, no y-axis check.  Coarse x-axis early-out:
            // once the pen has crossed the right edge of the clip rect,
            // every subsequent glyph would be fully clipped, so bail.
            // Per-glyph CPU clipping handles the partial-overlap case via
            // push_sprite_clipped.
            while (!text.empty()) {
                char32_t c = text.front();
                text.pop_front();
                if (pen.x > clip.b.x) break;
                auto q = font->charmap.find(c);
                if (q == font->charmap.end()) continue;
                push_sprite_clipped(q->second.sprite_ + pen, color);
                pen.x += q->second.advance;
            }
            return pen;
        }

        // ================================================================
        // OverlayStack
        // ================================================================

        bool OverlayStack::dispatch(Event const& e) {

            const bool is_mouse = (e.kind == WryEventKindMouseMove ||
                                   e.kind == WryEventKindMouseDown ||
                                   e.kind == WryEventKindMouseUp   ||
                                   e.kind == WryEventKindMouseEnter ||
                                   e.kind == WryEventKindMouseExit  ||
                                   e.kind == WryEventKindScroll);
            const bool is_kbd   = (e.kind == WryEventKindKeyDown ||
                                   e.kind == WryEventKindKeyUp);

            bool consumed = false;

            // Top-down: later-pushed overlays sit on top and see events first.
            for (auto it = _overlays.rbegin(); it != _overlays.rend(); ++it) {
                Overlay* o = *it;
                if (o->on_event(e))                  { consumed = true; break; }
                if (is_mouse && o->modal_mouse())    { consumed = true; break; }
                if (is_kbd   && o->modal_keyboard()) { consumed = true; break; }
            }

            // After dispatch, pop any top-of-stack overlay that asked to be
            // closed during its on_event.  Deferring the pop here means an
            // overlay can safely set `wants_close` from inside its own
            // event handler without invalidating the iteration above.
            while (!_overlays.empty() && _overlays.back()->wants_close) {
                _overlays.back()->wants_close = false;
                _overlays.pop_back();
            }

            return consumed;
        }

        void OverlayStack::paint(Painter& p) {
            // Bottom-up: later-pushed overlays paint last and so appear on top.
            for (Overlay* o : _overlays) {
                o->paint(p);
            }
        }

        // ================================================================
        // LogOverlay
        // ================================================================

        void LogOverlay::append(StringView msg,
                                std::chrono::steady_clock::duration endurance) {
            _logs.emplace(std::chrono::steady_clock::now() + endurance, msg);
        }

        void LogOverlay::paint(Painter& p) {
            if (!p.font || !p.atlas) return;

            const RGBA8Unorm_sRGB color(0.5f, 0.5f, 0.5f, 1.0f);
            const float left   = p.font->height * 0.5f;
            const float slot_h = p.font->height;
            // Place each line's glyph cell visually centered in a slot of
            // height font.height; this matches the pre-3.5 placement.
            //   baseline = slot_top + slot_h/2 + (ascender + descender)/2
            float slot_top = p.font->height * 0.5f;
            const auto t = std::chrono::steady_clock::now();

            for (auto it = _logs.begin(); it != _logs.end(); ) {
                if (it->first < t) {
                    it = _logs.erase(it);
                } else {
                    const float baseline = slot_top + slot_h * 0.5f
                        + (p.font->ascender + p.font->descender) * 0.5f;
                    p.draw_text_run(simd_make_float2(left, baseline),
                                    it->second, color);
                    slot_top += slot_h;
                    ++it;
                }
            }
        }

        // ================================================================
        // ConsoleOverlay
        // ================================================================

        ConsoleOverlay::ConsoleOverlay() {
            _lines.emplace_back("WryApplication");
            _lines.emplace_back("");
        }

        bool ConsoleOverlay::on_event(Event const& e) {
            if (e.kind != WryEventKindKeyDown) return false;

            if (!_active) {
                // Inactive: only the activation hotkey is ours.
                if (e.key == '`') {
                    _active = true;
                    if (_log) _log->append("[~] Show console");
                    return true;
                }
                return false;
            }

            // Active: full console keymap.  modal_keyboard() == true so the
            // dispatcher blocks any keyboard events we don't explicitly claim
            // from leaking to lower overlays / legacy handlers.
            switch (e.key) {
                case key::Enter:
                    _lines.emplace_back();
                    break;
                case '`':
                    // Tilde toggles -- in-game-console convention.  Without
                    // this it'd just fall through to the text-insert default
                    // case and end up appended to the input line.
                    _active = false;
                    if (_log) _log->append("[~] Hide console");
                    break;
                case key::Escape:
                    _active = false;
                    if (_log) _log->append(u8"[ESC] Hide console");
                    break;
                case key::Backspace:
                    if (!_lines.back().empty()) {
                        _lines.back().pop_back();
                    }
                    break;
                case key::ArrowUp:
                    std::rotate(_lines.begin(),
                                _lines.end() - 1,
                                _lines.end());
                    break;
                case key::ArrowDown:
                    std::rotate(_lines.begin(),
                                _lines.begin() + 1,
                                _lines.end());
                    break;
                case key::ArrowLeft:
                    if (!_lines.back().empty()) {
                        auto ch = _lines.back().back();
                        _lines.back().pop_back();
                        _lines.back().push_front(ch);
                    }
                    break;
                case key::ArrowRight:
                    if (!_lines.back().empty()) {
                        auto ch = _lines.back().front_and_pop_front();
                        _lines.back().push_back(ch);
                    }
                    break;
                default:
                    if (e.text[0] != '\0') {
                        auto p = reinterpret_cast<const char8_t*>(e.text);
                        _lines.back().append(p);
                    }
                    break;
            }
            return true;
        }

        void ConsoleOverlay::paint(Painter& p) {
            if (!_active) return;
            if (!p.font || !p.atlas) return;

            const RGBA8Unorm_sRGB color(1.0f, 1.0f, 1.0f, 1.0f);
            const float left   = p.font->height * 0.5f;
            const float slot_h = p.font->height;
            // Bottommost slot ends a half-line above the bottom edge.
            float slot_top = p.viewport_size_pt.y - p.font->height * 0.5f;
            bool first = true;

            for (auto it = _lines.end();
                 (slot_top >= 0) && (it != _lines.begin()); )
            {
                --it;
                slot_top -= slot_h;
                const float baseline = slot_top + slot_h * 0.5f
                    + (p.font->ascender + p.font->descender) * 0.5f;
                const float2 end_pen = p.draw_text_run(
                    simd_make_float2(left, baseline), *it, color);
                if (first) {
                    // Blinking cursor on the input (most recent) line.
                    p.draw_text_run(end_pen,
                                    (p.frame_count & 0x40) ? "_" : " ",
                                    color);
                    first = false;
                }
            }
        }

        // ================================================================
        // PaletteOverlay
        // ================================================================
        //
        // Currently the renderer still draws the palette (it uses a
        // projective transform and binds the `_symbols` texture, neither of
        // which Painter abstracts yet).  The overlay's job in phase 2 is to
        // own the input -- it hit-tests clicks against the same projective
        // transform the renderer uses, updates hover, and signals selection.
        //
        // Phase 3+ will move the palette into screen space and let
        // PaletteOverlay::paint() draw itself through the atlas like
        // everything else.

        bool PaletteOverlay::on_event(Event const& e) {
            if (!_model) return false;

            if (e.kind != WryEventKindMouseMove &&
                e.kind != WryEventKindMouseUp)
                return false;
            if (e.kind == WryEventKindMouseUp &&
                e.button != MouseButton::Left)
                return false;

            const float vw = _model->_viewport_size.x;
            const float vh = _model->_viewport_size.y;
            if (vw <= 0.0f || vh <= 0.0f) return false;

            // Same composition as WryRenderer's drawOverlay: aspect-correct
            // NDC view + the palette's own _transform.  Note we use only
            // the aspect ratio vw/vh from _viewport_size, which is in
            // drawable pixels -- aspect-from-pixels equals aspect-from-
            // logical-points, so no unit conversion is needed here.
            // Keeping this in sync with the renderer's matrix is a known
            // fragility; phase 3 collapses it when the palette moves to
            // screen space.
            simd_float4x4 screen_to_palette = simd_mul(
                matrix_float4x4{{
                    {1.0f,        0.0f,           0.0f, 0.0f},
                    {0.0f, -vw / vh,              0.0f, 0.0f},
                    {0.0f,        0.0f,           1.0f, 0.0f},
                    {0.0f,       -1.0f,           0.0f, 1.0f},
                }},
                _controls._transform);

            // _model->_mouse is the NDC cursor position.  The pump keeps
            // it in lockstep with the event currently being dispatched
            // (see pump() below), so reading it here gives us the correct
            // cursor for this exact event without the overlay having to
            // plumb a screen-pt-to-NDC conversion of its own (which would
            // need a view-size-in-logical-points value -- different from
            // _viewport_size, which is in drawable pixels).
            simd_float4 ray = simd_make_float4(_model->_mouse.x,
                                               _model->_mouse.y,
                                               0.0f, 1.0f);
            float2 mmm = project_screen_ray(screen_to_palette, ray);

            auto& m = _controls._payload;
            const auto i = (difference_type)std::floor(mmm.x);
            const auto j = (difference_type)std::floor(mmm.y);
            const bool in_bounds = (0 <= i) && (i < m.minor()) &&
                                   (0 <= j) && (j < m.minor());

            if (e.kind == WryEventKindMouseMove) {
                _hover_i = in_bounds ? (int)i : -1;
                _hover_j = in_bounds ? (int)j : -1;
                return false;   // hover is non-consuming.
            }

            // MouseUp Left.
            if (!in_bounds) return false;

            _selected_i = (int)i;
            _selected_j = (int)j;
            _model->_holding_value = m[i, j];
            _cursor_dirty = true;
            return true;
        }

        // ================================================================
        // Button
        // ================================================================

        Button::Button(StringView label, std::function<void()> on_click)
        : _label(label)
        , _on_click(std::move(on_click)) {
        }

        Size Button::measure(SizeConstraints c, MeasureContext const& ctx) {
            // Width fits the label plus padding, with a minimum so the
            // button always looks substantial.
            float text_w = ctx.font
                ? wry::text_run_width(ctx.font, _label)
                : 0.0f;
            float natural_w = std::max(kMinWidth, text_w + 2.0f * kHorizPadding);

            // Height fits the font's glyph cell plus padding above and
            // below.  Glyph cell extent = ascender - descender (descender
            // is negative, so the subtraction adds).  If the font isn't
            // available we use a hand-tuned fallback large enough for
            // anything reasonable; that keeps measure() side-effect-free
            // even in degenerate setups.
            float natural_h = kFallbackHeight;
            if (ctx.font) {
                const float cell_h = ctx.font->ascender - ctx.font->descender;
                natural_h = cell_h + 2.0f * kVertPadding;
            }

            _last_measured_size = c.constrain(Size{ natural_w, natural_h });
            return _last_measured_size;
        }

        void Button::arrange(rect<float> r) {
            _rect = r;
        }

        bool Button::on_event(Event const& e) {
            const float2 p = e.location;
            const bool inside = _rect.contains(p);

            switch (e.kind) {

                case WryEventKindMouseMove:
                    _hover = inside;
                    // Pressed state survives a drag-off; we mirror the OS
                    // convention of "click only fires on up-inside-button".
                    // Don't consume -- other siblings might also care.
                    return false;

                case WryEventKindMouseDown:
                    if (e.button != MouseButton::Left) return false;
                    if (!inside) return false;
                    _pressed = true;
                    return true;

                case WryEventKindMouseUp: {
                    if (e.button != MouseButton::Left) return false;
                    const bool was_pressed = _pressed;
                    _pressed = false;
                    if (!was_pressed) return false;
                    if (!inside)      return false;
                    if (_on_click)    _on_click();
                    return true;
                }

                case WryEventKindMouseExit:
                    _hover   = false;
                    _pressed = false;
                    return false;

                default:
                    return false;
            }
        }

        void Button::paint(Painter& p) {
            if (!p.atlas) return;

            // Background.  Darker base, slightly lighter on hover, slightly
            // darker again while pressed.
            RGBA8Unorm_sRGB bg(0.20f, 0.20f, 0.20f, 1.0f);
            if (_pressed) {
                bg = RGBA8Unorm_sRGB(0.12f, 0.12f, 0.12f, 1.0f);
            } else if (_hover) {
                bg = RGBA8Unorm_sRGB(0.28f, 0.28f, 0.28f, 1.0f);
            }
            p.fill_rect(_rect, bg);

            // Label.  Horizontally center the text within the button
            // (subject to a minimum left-edge padding), vertically center
            // the glyph cell.  Push a clip rect at the button bounds so
            // an overlong label gets cropped to the button rather than
            // spilling across siblings.
            if (p.font) {
                const RGBA8Unorm_sRGB fg(0.0f, 0.0f, 0.0f, 1.0f);

                const float button_w = _rect.b.x - _rect.a.x;
                const float button_h = _rect.b.y - _rect.a.y;
                const float text_w   = wry::text_run_width(p.font, _label);

                // Horizontal pen: centered, clamped to leave at least
                // kHorizPadding on the left.
                const float pen_x = _rect.a.x +
                    std::max(kHorizPadding, (button_w - text_w) * 0.5f);

                // Vertical baseline: place glyph cell midpoint at button
                // midpoint.  See gui_widget.hpp for the derivation.
                const float baseline = _rect.a.y + button_h * 0.5f
                    + (p.font->ascender + p.font->descender) * 0.5f;

                rect<float> prev_clip = p.push_clip(_rect);
                p.draw_text_run(simd_make_float2(pen_x, baseline),
                                _label, fg);
                p.pop_clip(prev_clip);
            }
        }

        // ================================================================
        // Column
        // ================================================================

        Size Column::measure(SizeConstraints c, MeasureContext const& ctx) {
            float max_w = 0.0f;
            float total_h = 0.0f;
            for (std::size_t i = 0; i < _children.size(); ++i) {
                if (i > 0) total_h += _spacing;
                Size cs = _children[i]->measure(c, ctx);
                max_w   = std::max(max_w, cs.w);
                total_h += cs.h;
            }
            _last_measured_size = c.constrain(Size{ max_w, total_h });
            return _last_measured_size;
        }

        void Column::arrange(rect<float> r) {
            _rect = r;
            const float column_w = r.b.x - r.a.x;
            float y = r.a.y;
            for (std::size_t i = 0; i < _children.size(); ++i) {
                if (i > 0) y += _spacing;
                const Size cs = _children[i]->last_measured_size();
                // Stretch on the cross axis: every child gets the column's
                // full width.  Buttons sized themselves for their label in
                // measure(), so when the column took max(child widths) we
                // already know the widest button.  Stretching everyone to
                // that width makes them uniform without an extra pass.
                rect<float> child_rect{
                    r.a.x,
                    y,
                    r.a.x + column_w,
                    y + cs.h,
                };
                _children[i]->arrange(child_rect);
                y += cs.h;
            }
        }

        bool Column::on_event(Event const& e) {
            // Forward to children in document order.  Children don't
            // overlap in a column, so order is just convention.
            for (auto& child : _children) {
                if (child->on_event(e)) return true;
            }
            return false;
        }

        void Column::paint(Painter& p) {
            for (auto& child : _children) {
                child->paint(p);
            }
        }

        // ================================================================
        // MainMenuOverlay
        // ================================================================

        MainMenuOverlay::MainMenuOverlay() {
            auto col = std::make_unique<Column>();
            col->set_spacing(8.0f);

            // The CONTINUE button closes the menu by setting our own
            // wants_close.  The lambda captures `this` so the click handler
            // can flag it -- safe because the overlay outlives its widget
            // tree.
            col->add(std::make_unique<Button>(
                "CONTINUE", [this] { this->wants_close = true; }));

            // Placeholder actions; real implementations land in later
            // phases as save/load/settings get built out.
            col->add(std::make_unique<Button>("NEW",      [] { /* TODO */ }));
            col->add(std::make_unique<Button>("LOAD",     [] { /* TODO */ }));
            col->add(std::make_unique<Button>("SAVE",     [] { /* TODO */ }));
            col->add(std::make_unique<Button>("SETTINGS", [] { /* TODO */ }));

            _root = std::move(col);
        }

        MainMenuOverlay::~MainMenuOverlay() = default;

        bool MainMenuOverlay::on_event(Event const& e) {
            // ESC always closes the menu, regardless of what's hovered.
            if (e.kind == WryEventKindKeyDown && e.key == key::Escape) {
                wants_close = true;
                return true;
            }
            // Otherwise forward to the widget tree.
            return _root->on_event(e);
        }

        void MainMenuOverlay::paint(Painter& p) {
            // Translucent backdrop dims the world behind the menu and also
            // makes our "we ate that click" modality visually obvious.
            p.fill_rect(rect<float>{0.0f, 0.0f,
                                    p.viewport_size_pt.x,
                                    p.viewport_size_pt.y},
                        RGBA8Unorm_sRGB(0.0f, 0.0f, 0.0f, 0.5f));

            // Layout: ask the root for its natural size given the viewport
            // as a loose upper bound, then center it.  The MeasureContext
            // hands the font down so widgets that size themselves to text
            // (Button) can do so during measure.  Note the size-space ->
            // position-space transition: `desired` is a Size (no location);
            // we choose `x`, `y` here to turn it into the screen rect we
            // pass to arrange.
            MeasureContext mctx{ p.font };
            SizeConstraints c = SizeConstraints::loose(p.viewport_size_pt);
            Size desired = _root->measure(c, mctx);
            float x = (p.viewport_size_pt.x - desired.w) * 0.5f;
            float y = (p.viewport_size_pt.y - desired.h) * 0.5f;
            _root->arrange(rect<float>{x, y, x + desired.w, y + desired.h});

            _root->paint(p);
        }

        // ================================================================
        // Event pump
        // ================================================================

        static void pump_legacy_event(model& m,
                                      Event const& e,
                                      float2 view_size_pt);

        void pump(model& m, float2 view_size_pt) {
            const float w = (view_size_pt.x > 0.0f) ? view_size_pt.x : 1.0f;
            const float h = (view_size_pt.y > 0.0f) ? view_size_pt.y : 1.0f;

            while (!m._events.empty()) {
                Event e = m._events.pop_front();

                // Keep _mouse (NDC, y-up) in lockstep with the event
                // currently being dispatched.  Doing this once here, up
                // front, means every overlay reads a current,
                // consistently-converted value and no overlay needs its
                // own screen-pt -> NDC plumbing.  Note view_size_pt is in
                // logical points (the view's bounds.size); _viewport_size
                // elsewhere on the model is in drawable pixels.
                if (e.kind == WryEventKindMouseMove  ||
                    e.kind == WryEventKindMouseDown  ||
                    e.kind == WryEventKindMouseUp    ||
                    e.kind == WryEventKindMouseEnter ||
                    e.kind == WryEventKindMouseExit  ||
                    e.kind == WryEventKindScroll) {
                    m._mouse.x = 2.0f * e.location.x / w - 1.0f;
                    m._mouse.y = 1.0f - 2.0f * e.location.y / h;
                }

                bool consumed = m._stack.dispatch(e);
                if (!consumed) {
                    pump_legacy_event(m, e, view_size_pt);
                }
            }
        }

        // Whatever the overlay stack didn't claim ends up here.  This still
        // exists in phase 2 because the world (ground-plane click, hex-key
        // write, scroll-pan) and the f-keyish debug toggles haven't moved
        // into overlays yet.  Each clause peels off into a real overlay
        // in later phases and this function shrinks toward nothing.
        static void pump_legacy_event(model& m,
                                      Event const& e,
                                      float2 view_size_pt) {
            // _mouse is already up to date -- pump() refreshed it before
            // dispatching this event.  view_size_pt is unused here for the
            // same reason; left in the signature for future legacy clauses
            // that may need it.
            (void)view_size_pt;

            switch (e.kind) {

                case WryEventKindMouseUp:
                    if (e.button == MouseButton::Left) {
                        m._outstanding_click = true;
                    }
                    break;

                case WryEventKindScroll:
                    m._looking_at.x += e.scroll_delta.x;
                    m._looking_at.y += e.scroll_delta.y;
                    break;

                case WryEventKindKeyDown: {
                    char buffer[100];
                    switch (e.key) {
                        case key::Escape:
                            // Nothing else claimed ESC, so this is the
                            // open-the-main-menu case.  The menu's own
                            // on_event closes it again when ESC is pressed
                            // while it's on the stack.
                            m._stack.push(&m._main_menu_overlay);
                            break;
                        case 'j':
                            m._show_jacobian = !m._show_jacobian;
                            std::snprintf(buffer, sizeof(buffer),
                                          "%s [J]acobians",
                                          m._show_jacobian ? "Show" : "Hide");
                            m.append_log(buffer);
                            break;
                        case 'p':
                            m._show_points = !m._show_points;
                            std::snprintf(buffer, sizeof(buffer),
                                          "%s [P]oints",
                                          m._show_points ? "Show" : "Hide");
                            m.append_log(buffer);
                            break;
                        case 'w':
                            m._show_wireframe = !m._show_wireframe;
                            std::snprintf(buffer, sizeof(buffer),
                                          "%s [W]ireframe",
                                          m._show_wireframe ? "Show" : "Hide");
                            m.append_log(buffer);
                            break;
                        default:
                            if ((e.key >= '0' && e.key <= '9') ||
                                (e.key >= 'a' && e.key <= 'f')) {
                                m._outstanding_keysdown.push_back(
                                    (char32_t)e.key);
                            }
                            break;
                    }
                    break;
                }

                default:
                    // KeyUp / MouseDown / MouseEnter / MouseExit / Unknown:
                    // no pre-existing legacy behavior.  Events still flow
                    // through the queue so future widgets can observe them.
                    break;
            }
        }

    } // namespace gui

} // namespace wry
