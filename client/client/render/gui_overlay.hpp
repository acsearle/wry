//
//  gui_overlay.hpp
//  client
//
//  Created by Antony Searle on 2026-05-21.
//
//  Overlay / OverlayStack: a small modal-stack of persistent GUI elements
//  that share an event-dispatch and paint discipline.  The stack runs
//  between the platform event source (WryDelegate -> EventQueue) and the
//  renderer:
//
//      events ---> OverlayStack::dispatch  (top-down)
//                          |
//                          v
//                    Overlay::on_event(e)    returns true if consumed
//                    Overlay::modal_*()      otherwise may block lower overlays
//                          |
//                          v  (unconsumed events fall back to pump_legacy)
//
//      paint  <--- OverlayStack::paint      (bottom-up; later overlays on top)
//                          |
//                          v
//                    Overlay::paint(Painter&)
//
//  Each overlay owns its own state.  Some overlays (LogOverlay,
//  ConsoleOverlay) paint themselves via the supplied Painter.  Others
//  (PaletteOverlay for now) participate in event dispatch only; their
//  paint method is a stub and the renderer reads their state directly to
//  draw them, because they need a render path the Painter does not yet
//  abstract over.  That asymmetry is a known phase-2 wart.

#ifndef gui_overlay_hpp
#define gui_overlay_hpp

#include <chrono>
#include <map>
#include <memory>
#include <vector>

#include "contiguous_deque.hpp"
#include "gui_event.hpp"
#include "palette.hpp"
#include "simd.hpp"
#include "string.hpp"
#include "value.hpp"

namespace wry {

    // Forward decls so Painter can hold pointers without dragging the full
    // headers into every TU that includes us.
    struct SpriteAtlas;
    struct Font;

    namespace gui {

        // Things an Overlay needs to draw itself in screen space.  Pure
        // data; no Metal types leak in.  Constructed afresh each frame by
        // the renderer.
        struct Painter {
            SpriteAtlas* atlas = nullptr;
            Font* font = nullptr;
            float2 viewport_size_pt = {0.0f, 0.0f};
            uint64_t frame_count = 0;   // for cursor blink, etc.
        };

        class Overlay {
        public:
            virtual ~Overlay() = default;

            // Return true if this overlay consumed the event.  Default is
            // pass-through.
            virtual bool on_event(Event const&) { return false; }

            // Draw into Painter.  Default is no-op; useful for overlays
            // that participate only in event dispatch.
            virtual void paint(Painter&) {}

            // If true, mouse / keyboard events that this overlay sees but
            // does not consume should still be blocked from reaching lower
            // overlays.  This is the modality flag.  Mouse and keyboard
            // are independent so e.g. the palette can capture clicks on
            // its area without blocking the world from receiving keys.
            virtual bool modal_mouse() const { return false; }
            virtual bool modal_keyboard() const { return false; }
        };

        // Ordered, top-of-stack-last.  Non-owning: the stack just borrows
        // pointers to overlays whose lifetime is managed elsewhere (today,
        // by direct membership in `wry::model`).
        class OverlayStack {
        public:
            void push(Overlay* o)    { _overlays.push_back(o); }
            void pop()               { _overlays.pop_back(); }
            bool empty() const       { return _overlays.empty(); }
            std::size_t size() const { return _overlays.size(); }

            // Top-down walk.  Returns true if any overlay consumed the
            // event, or if a modal overlay blocked it.
            bool dispatch(Event const& e);

            // Bottom-up walk.  Earlier overlays paint first, later on top.
            void paint(Painter& p);

        private:
            std::vector<Overlay*> _overlays;
        };

        // ----------------------------------------------------------------
        // Concrete overlays.

        // Floating timed messages (the existing "log" stream at the top
        // of the screen).  Purely visual; no input.
        class LogOverlay : public Overlay {
        public:
            void append(StringView msg,
                        std::chrono::steady_clock::duration endurance
                            = std::chrono::seconds(5));

            void paint(Painter&) override;

        private:
            std::multimap<std::chrono::steady_clock::time_point, String> _logs;
        };

        // Quake-style drop-down console: toggled by `'`'`, captures all
        // keyboard input while active.
        class ConsoleOverlay : public Overlay {
        public:
            ConsoleOverlay();

            bool active() const { return _active; }
            bool on_event(Event const&) override;
            void paint(Painter&) override;

            // While active, the console eats keyboard but lets clicks
            // through to whatever is behind it.
            bool modal_keyboard() const override { return _active; }
            bool modal_mouse()    const override { return false; }

            // Direct access (chiefly for the model's append_log shim and
            // for the bring-up where the initial banner is pushed).
            ContiguousDeque<String>& lines() { return _lines; }

            // Wired by the model so the console can echo "[~] Show console"
            // etc. into the floating log.
            void set_log(LogOverlay* l) { _log = l; }

        private:
            ContiguousDeque<String> _lines;
            bool _active = false;
            LogOverlay* _log = nullptr;
        };

        // The opcode-picker grid.  For phase 2 this overlay handles input
        // (click selects an item; hover updates highlight) and owns the
        // payload + selection state.  Its paint() is empty -- WryRenderer
        // still draws the palette directly because the existing palette
        // render path uses a projective transform and a separate texture
        // binding that Painter does not yet abstract over.
        //
        // TODO (phase 3+): move palette painting into paint(), with the
        // palette laid out in screen-space and pushed through the atlas.
        class PaletteOverlay : public Overlay {
        public:
            bool on_event(Event const&) override;

            // PaletteOverlay reads model state for the live screen-to-
            // palette projection (it needs the world / palette transform
            // and the screen-space cursor).  The model pointer is set
            // once at startup by the renderer / app glue.
            void set_model(struct ::wry::model* m) { _model = m; }

            // Bring-up: WryRenderer fills this at init from assets.json.
            // The struct name `Palette<T>` reads as "a grid of T values";
            // this particular grid is "the controls", hence `_controls`.
            wry::Palette<wry::Value>& controls()             { return _controls; }
            wry::Palette<wry::Value> const& controls() const { return _controls; }

            // Renderer reads these to draw the highlights.  -1 means none.
            int selected_i() const { return _selected_i; }
            int selected_j() const { return _selected_j; }
            int hover_i()    const { return _hover_i; }
            int hover_j()    const { return _hover_j; }

            // True for exactly one frame after a selection change, so the
            // renderer knows to refresh the NSCursor image.  Renderer
            // clears it after consuming.
            bool cursor_needs_refresh() const { return _cursor_dirty; }
            void clear_cursor_refresh()      { _cursor_dirty = false; }

        private:
            wry::Palette<wry::Value> _controls;
            int _selected_i = -1;
            int _selected_j = -1;
            int _hover_i    = -1;
            int _hover_j    = -1;
            bool _cursor_dirty = false;
            struct ::wry::model* _model = nullptr;
        };

    } // namespace gui

} // namespace wry

#endif // gui_overlay_hpp
