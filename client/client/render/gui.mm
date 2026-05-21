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
#include "model.hpp"
#include "SpriteAtlas.hpp"
#include "font.hpp"
#include "text.hpp"

namespace wry {

    namespace gui {

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

            // Top-down: later-pushed overlays sit on top and see events first.
            for (auto it = _overlays.rbegin(); it != _overlays.rend(); ++it) {
                Overlay* o = *it;
                if (o->on_event(e))                       return true;
                if (is_mouse && o->modal_mouse())         return true;
                if (is_kbd   && o->modal_keyboard())      return true;
            }
            return false;
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

            RGBA8Unorm_sRGB color(0.5f, 0.5f, 0.5f, 1.0f);
            float y = p.font->height / 2;
            const auto t = std::chrono::steady_clock::now();

            for (auto it = _logs.begin(); it != _logs.end(); ) {
                if (it->first < t) {
                    it = _logs.erase(it);
                } else {
                    wry::drawOverlay_draw_text(
                        p.font, p.atlas,
                        rect<float>{p.font->height / 2, y,
                                    p.viewport_size_pt.x, p.viewport_size_pt.y},
                        it->second, color);
                    y += p.font->height;
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

            RGBA8Unorm_sRGB color(1.0f, 1.0f, 1.0f, 1.0f);
            float y = p.viewport_size_pt.y - p.font->height / 2;
            bool first = true;

            for (auto it = _lines.end(); (y >= 0) && (it != _lines.begin()); ) {
                --it;
                y -= p.font->height;
                float2 z = wry::drawOverlay_draw_text(
                    p.font, p.atlas,
                    rect<float>{p.font->height / 2, y,
                                p.viewport_size_pt.x, p.viewport_size_pt.y},
                    *it, color);
                if (first) {
                    // Blinking cursor on the input (most recent) line.
                    wry::drawOverlay_draw_text(
                        p.font, p.atlas,
                        rect<float>{z.x, z.y,
                                    p.viewport_size_pt.x, p.viewport_size_pt.y},
                        (p.frame_count & 0x40) ? "_" : " ", color);
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
