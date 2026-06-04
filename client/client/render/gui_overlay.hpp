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

#include <limits>

#include "contiguous_deque.hpp"
#include "font.hpp"
#include "gui_event.hpp"
#include "palette.hpp"
#include "rect.hpp"
#include "simd.hpp"
#include "SpriteAtlas.hpp"
#include "string.hpp"
#include "text.hpp"
#include "term.hpp"

namespace wry {

    namespace gui {
        class Widget;
    }

    namespace gui {

        // Things an Overlay needs to draw itself in screen space.  Pure
        // data; no Metal types leak in.  Constructed afresh each frame by
        // the renderer.
        //
        // All drawing helpers respect the current `clip` rect: sprites
        // fully outside are discarded, partially overlapping ones are
        // CPU-cropped on both position and texCoord.  Parents push a
        // tighter clip before drawing children and pop it afterwards.
        struct Painter {

            SpriteAtlas* atlas = nullptr;
            Font* font = nullptr;
            // Drawable-pixel viewport size (= logical-point view size *
            // backingScaleFactor).  The widget tree and the screen-space
            // overlay transform both operate in drawable pixels, so this
            // is the unit widgets see at layout / paint time.  The pump
            // converts NSEvent point-locations to pixels at the dispatch
            // boundary so event.location matches.
            float2 viewport_size_px = {0.0f, 0.0f};
            uint64_t frame_count = 0;   // for cursor blink, etc.

            // A copy of the atlas's reserved white sprite.  Lets widgets
            // draw solid-color rectangles cheaply via fill_rect.  The
            // renderer sets this when constructing the Painter each frame.
            Sprite white_sprite = {};

            // Current clip rect in screen-space (logical points).  The
            // renderer initialises this to the viewport rect each frame;
            // widgets push tighter clips via push_clip / pop_clip.
            rect<float> clip = {
                -std::numeric_limits<float>::infinity(),
                -std::numeric_limits<float>::infinity(),
                +std::numeric_limits<float>::infinity(),
                +std::numeric_limits<float>::infinity(),
            };


            // -- Clip stack --------------------------------------------------
            //
            // Hierarchical clipping is just "intersect on push, restore on
            // pop"; callers pass the returned previous value back to
            // pop_clip themselves (no implicit stack).  Widgets can also
            // read `clip` directly to do their own coarse culling (e.g.
            // text-run bails when the pen passes clip.b.x).

            rect<float> push_clip(rect<float> r) {
                rect<float> prev = clip;
                clip = intersection(prev, r);
                return prev;
            }

            void pop_clip(rect<float> previous) {
                clip = previous;
            }


            // -- Clipped sprite emission ------------------------------------

            // CPU-crop an axis-aligned sprite against `clip` and push it
            // to the atlas.  Adjusts position and texCoord proportionally
            // so partially-clipped sprites render correctly.  Degenerate
            // sprites (zero extent) and fully-clipped sprites are dropped.
            void push_sprite_clipped(Sprite s, RGBA8Unorm_sRGB color) {
                if (!atlas) return;

                const float sa_x = s.a.position.x, sa_y = s.a.position.y;
                const float sb_x = s.b.position.x, sb_y = s.b.position.y;
                const float ext_x = sb_x - sa_x;
                const float ext_y = sb_y - sa_y;
                if (ext_x <= 0.0f || ext_y <= 0.0f) return;

                const float ca_x = std::max(sa_x, clip.a.x);
                const float ca_y = std::max(sa_y, clip.a.y);
                const float cb_x = std::min(sb_x, clip.b.x);
                const float cb_y = std::min(sb_y, clip.b.y);
                if (ca_x >= cb_x || ca_y >= cb_y) return;

                // Fast path: fully inside, push as-is.
                if (ca_x == sa_x && ca_y == sa_y &&
                    cb_x == sb_x && cb_y == sb_y) {
                    atlas->push_sprite(s, color);
                    return;
                }

                // Partial crop: lerp texCoords by the same fractions we
                // moved the corners.
                const float2 t0 = s.a.texCoord;
                const float2 t1 = s.b.texCoord;
                const float2 ta = simd_make_float2(
                    t0.x + (t1.x - t0.x) * (ca_x - sa_x) / ext_x,
                    t0.y + (t1.y - t0.y) * (ca_y - sa_y) / ext_y);
                const float2 tb = simd_make_float2(
                    t0.x + (t1.x - t0.x) * (cb_x - sa_x) / ext_x,
                    t0.y + (t1.y - t0.y) * (cb_y - sa_y) / ext_y);

                Sprite cropped;
                cropped.a.position = simd_make_float4(ca_x, ca_y, 0.0f, 1.0f);
                cropped.a.texCoord = ta;
                cropped.b.position = simd_make_float4(cb_x, cb_y, 0.0f, 1.0f);
                cropped.b.texCoord = tb;
                atlas->push_sprite(cropped, color);
            }


            // -- Higher-level helpers ---------------------------------------

            // Solid-color rectangle.  Stretches and tints the atlas's white
            // sprite, then routes through push_sprite_clipped.
            void fill_rect(rect<float> r, RGBA8Unorm_sRGB color) {
                if (!atlas) return;
                Sprite s = white_sprite;
                s.a.position.x = r.a.x;
                s.a.position.y = r.a.y;
                s.b.position.x = r.b.x;
                s.b.position.y = r.b.y;
                push_sprite_clipped(s, color);
            }

            // Draw a single line of text starting at the given baseline
            // pen position.  No line breaks, no y-axis early-out.  Stops
            // early when the pen passes the right edge of the current
            // clip rect (everything further would be entirely clipped
            // anyway).  Returns the pen position after the last glyph
            // (useful e.g. for placing a cursor / caret).
            //
            // Defined in gui.mm; declaration only here.
            float2 draw_text_run(float2 pen,
                                 StringView text,
                                 RGBA8Unorm_sRGB color);
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

            // Set by an overlay during its own on_event to request being
            // popped from the stack.  OverlayStack::dispatch drains this
            // flag from the top of the stack after each event, so popping
            // is deferred until after dispatch returns -- safe to set from
            // inside an event handler without invalidating iteration.
            bool wants_close = false;
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
            wry::Palette<wry::Term>& controls()             { return _controls; }
            wry::Palette<wry::Term> const& controls() const { return _controls; }

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
            wry::Palette<wry::Term> _controls;
            int _selected_i = -1;
            int _selected_j = -1;
            int _hover_i    = -1;
            int _hover_j    = -1;
            bool _cursor_dirty = false;
            struct ::wry::model* _model = nullptr;
        };

        // ----------------------------------------------------------------
        // Main menu overlay.  Modal in both mouse and keyboard.  Hosts a
        // root Widget (built in its constructor) that lays out a column of
        // buttons centered on the viewport.  Pressing ESC, or clicking the
        // CONTINUE button, sets `wants_close` and the stack pops it after
        // the dispatch returns.
        //
        // Pushed onto the OverlayStack by the pump's legacy fallback when
        // ESC is unconsumed by anything above it.  When pushed it lives at
        // the top of the stack; while there it eats every event that
        // doesn't land on one of its buttons.

        class MainMenuOverlay : public Overlay {
        public:
            MainMenuOverlay();
            ~MainMenuOverlay() override;

            bool on_event(Event const&) override;
            void paint(Painter&) override;

            bool modal_mouse()    const override { return true; }
            bool modal_keyboard() const override { return true; }

            // The LOAD button needs to push the save-list overlay onto
            // the model's stack; we get the model pointer set once at
            // startup and the button's click lambda reads it via `this`.
            void set_model(struct ::wry::model* m) { _model = m; }

        private:
            // unique_ptr to a forward-declared Widget so this header
            // doesn't need to know the layout of the widget tree.  The
            // tree is constructed in the .cpp (well, .mm).
            std::unique_ptr<Widget> _root;
            struct ::wry::model* _model = nullptr;
        };

        // ----------------------------------------------------------------
        // Save-game list overlay.  Pushed by MainMenuOverlay's LOAD button.
        // Modal in both axes.  Hosts:
        //
        //   * a title Label,
        //   * a ScrollView containing a Column of selectable Buttons (one
        //     per save entry),
        //   * a Row of action Buttons (Load / Delete / Cancel).
        //
        // The save data itself is mocked for phase 4; the actual savefile
        // system slots in later by replacing make_mock_saves().

        class SaveListOverlay : public Overlay {
        public:
            SaveListOverlay();
            ~SaveListOverlay() override;

            bool on_event(Event const&) override;
            void paint(Painter&) override;

            bool modal_mouse()    const override { return true; }
            bool modal_keyboard() const override { return true; }

        private:
            std::unique_ptr<Widget> _root;
            // Non-owning into the widget tree above; we need direct access
            // to scroll arithmetic and to flip the selected state on rows.
            class ScrollView*       _scroll = nullptr;
            std::vector<class Button*> _row_buttons;

            int _selected_index = 0;

            void update_selection_visuals();
            void move_selection(int delta);
            void scroll_to_selected();
            void load_selected();
            void delete_selected();
        };

    } // namespace gui

} // namespace wry

#endif // gui_overlay_hpp
