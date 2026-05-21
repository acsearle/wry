//
//  gui.cpp
//  client
//
//  Created by Antony Searle on 29/12/2025.
//

#include "gui.hpp"

#include <algorithm>
#include <cstdio>
#include <memory>
#include <vector>

#include "gui_event.hpp"
#include "model.hpp"

namespace wry {

    namespace gui {

        // ----------------------------------------------------------------
        // Older immediate-mode scaffolding (kept temporarily; superseded by
        // the persistent overlay/widget tree that lands in step 2+).

        struct Localized;

        // brittle global state
        std::vector<std::shared_ptr<Localized>> _live; // under construction new GUI
        std::vector<std::shared_ptr<Localized>> _baked; // displayed and receiving clicks old GUI

        struct Base {
            virtual ~Base() = default;
        };

        struct Localized : Base {
            virtual double2 negotiate(double2 constraints);
        };

        // ----------------------------------------------------------------
        // Transitional event pump.  See gui_event.hpp for rationale.
        //
        // Every clause below is a direct port of the original switch in
        // WryDelegate.mm's keyDown: / mouse*: handlers, behavior preserved.
        // As widgets are introduced these clauses move into the widgets
        // and disappear from here.

        static void handle_key_down_legacy(model& m, Event const& e) {

            if (m._console_active) {
                // Console keymap.  Modifier-applied text (e.text) feeds the
                // line; named keys (e.key) drive editing and navigation.
                switch (e.key) {
                    case key::Enter:
                        m._console.emplace_back();
                        break;
                    case key::Escape:
                        m._console_active = false;
                        m.append_log(u8"[ESC] Hide console");
                        break;
                    case key::Backspace:
                        if (!m._console.back().empty()) {
                            m._console.back().pop_back();
                        }
                        break;
                    case key::ArrowUp:
                        std::rotate(m._console.begin(),
                                    m._console.end() - 1,
                                    m._console.end());
                        break;
                    case key::ArrowDown:
                        std::rotate(m._console.begin(),
                                    m._console.begin() + 1,
                                    m._console.end());
                        break;
                    case key::ArrowLeft:
                        if (!m._console.back().empty()) {
                            auto ch = m._console.back().back();
                            m._console.back().pop_back();
                            m._console.back().push_front(ch);
                        }
                        break;
                    case key::ArrowRight:
                        if (!m._console.back().empty()) {
                            auto ch = m._console.back().front_and_pop_front();
                            m._console.back().push_back(ch);
                        }
                        break;
                    default:
                        if (e.text[0] != '\0') {
                            auto p = reinterpret_cast<const char8_t*>(e.text);
                            m._console.back().append(p);
                        }
                        break;
                }
                return;
            }

            // Not in console mode: top-level shortcuts.  `key` is the
            // unmodified base character, so Shift+` summons the console
            // exactly as before, while plain `j`/`p`/`w` toggle their flags.
            char buffer[100];
            switch (e.key) {
                case '`':
                    m._console_active = true;
                    m.append_log("[~] Show console");
                    break;
                case 'j':
                    m._show_jacobian = !m._show_jacobian;
                    std::snprintf(buffer, sizeof(buffer), "%s [J]acobians",
                                  m._show_jacobian ? "Show" : "Hide");
                    m.append_log(buffer);
                    break;
                case 'p':
                    m._show_points = !m._show_points;
                    std::snprintf(buffer, sizeof(buffer), "%s [P]oints",
                                  m._show_points ? "Show" : "Hide");
                    m.append_log(buffer);
                    break;
                case 'w':
                    m._show_wireframe = !m._show_wireframe;
                    std::snprintf(buffer, sizeof(buffer), "%s [W]ireframe",
                                  m._show_wireframe ? "Show" : "Hide");
                    m.append_log(buffer);
                    break;
                default:
                    // Hex world-write path.  The old code used isxdigit on
                    // the raw unichar; after translation `e.key` is the
                    // unmodified lowercase ASCII value, so 0-9 / a-f are
                    // the only matches (capital A-F never appear here).
                    if ((e.key >= '0' && e.key <= '9') ||
                        (e.key >= 'a' && e.key <= 'f')) {
                        m._outstanding_keysdown.push_back(static_cast<char32_t>(e.key));
                    }
                    break;
            }
        }

        void pump_legacy(model& m, float2 view_size_pt) {

            // Defensive: if events arrive before the view has reported a
            // size, divisions below would produce NaN.  Falling back to 1
            // pins _mouse harmlessly close to the origin until the next
            // resize.
            float w = (view_size_pt.x > 0.0f) ? view_size_pt.x : 1.0f;
            float h = (view_size_pt.y > 0.0f) ? view_size_pt.y : 1.0f;

            while (!m._events.empty()) {
                Event e = m._events.pop_front();
                switch (e.kind) {

                    case WryEventKindMouseMove: {
                        // event.location is top-left y-down logical points;
                        // _mouse is NDC with y-up to match the pre-event-
                        // queue arithmetic in WryRenderer's overlay code.
                        m._mouse.x = 2.0f * e.location.x / w - 1.0f;
                        m._mouse.y = 1.0f - 2.0f * e.location.y / h;
                        break;
                    }

                    case WryEventKindMouseUp: {
                        // The old handler latched position and click on the
                        // up edge; preserve that.  MouseDown is unobserved.
                        if (e.button == MouseButton::Left) {
                            m._mouse.x = 2.0f * e.location.x / w - 1.0f;
                            m._mouse.y = 1.0f - 2.0f * e.location.y / h;
                            m._outstanding_click = true;
                        }
                        break;
                    }

                    case WryEventKindScroll: {
                        m._looking_at.x += e.scroll_delta.x;
                        m._looking_at.y += e.scroll_delta.y;
                        break;
                    }

                    case WryEventKindKeyDown:
                        handle_key_down_legacy(m, e);
                        break;

                    case WryEventKindKeyUp:
                    case WryEventKindMouseDown:
                    case WryEventKindMouseEnter:
                    case WryEventKindMouseExit:
                    case WryEventKindUnknown:
                        // No pre-existing behavior.  Events still flow
                        // through the queue so future widgets can observe.
                        break;
                }
            }
        }

    } // namespace gui

} // namespace wry
