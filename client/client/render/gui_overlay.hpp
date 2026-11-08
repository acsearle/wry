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
//  Each overlay owns its own state.  Some overlays (LogOverlay) paint
//  themselves via the supplied Painter; others (ConsoleOverlay, the menus,
//  the settings screens) lay out and paint a widget tree
//  (gui_widget.hpp).  Others
//  (PaletteOverlay for now) participate in event dispatch only; their
//  paint method is a stub and the renderer reads their state directly to
//  draw them, because they need a render path the Painter does not yet
//  abstract over.  That asymmetry is a known phase-2 wart.

#ifndef gui_overlay_hpp
#define gui_overlay_hpp

#include <algorithm>
#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <vector>

#include <limits>

#include "command.hpp"
#include "contiguous_deque.hpp"
#include "font.hpp"
#include "gui_event.hpp"
#include "gui_keymap.hpp"
#include "gui_clipboard.hpp"
#include "gui_line_editor.hpp"
#include "gui_line_history.hpp"
#include "palette.hpp"
#include "rect.hpp"
#include "simd.hpp"
#include "SpriteAtlas.hpp"
#include "string.hpp"
#include "text.hpp"
#include "term.hpp"

namespace wry {

    struct GuiContext;

    namespace gui {
        class Widget;
        class ConsoleView;
    }

    namespace gui {

        // Things an Overlay needs to draw itself in screen space.  Pure
        // data; no Metal types leak in.  Constructed afresh each frame by
        // the renderer.
        //
        // All drawing helpers respect the current `clip` rect: quads
        // fully outside are discarded, partially overlapping ones are
        // CPU-cropped on both draw rect and uv.  Parents push a
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

            // Drawable pixels per UI point (GuiContext::pixels_per_point;
            // see MeasureContext in gui_widget.hpp for the model).  The
            // default is the hosts' default, a Retina display at UI scale 1.
            float pixels_per_point = 2.0f;

            float pt(float points) const { return points * pixels_per_point; }

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


            // -- Clipped quad emission --------------------------------------

            // CPU-crop a quad's draw rect against `clip` and push it to the
            // atlas.  Adjusts uv proportionally so partially-clipped
            // textured quads render correctly.  The shape (and hence any
            // rounded-box mask) is untouched, so a cropped box keeps its
            // true corners.  Degenerate and fully-clipped quads are dropped.
            void push_quad_clipped(WryQuadInstance q) {
                if (!atlas) return;

                const float4 d = q.draw;
                const float ext_x = d.z - d.x;
                const float ext_y = d.w - d.y;
                if (ext_x <= 0.0f || ext_y <= 0.0f) return;

                const float4 c = simd_make_float4(std::max(d.x, clip.a.x),
                                                  std::max(d.y, clip.a.y),
                                                  std::min(d.z, clip.b.x),
                                                  std::min(d.w, clip.b.y));
                if (c.x >= c.z || c.y >= c.w) return;

                // Partial crop: lerp uv by the same fractions we moved the
                // corners.
                if (simd_any(c != d)) {
                    const float4 t = q.uv;
                    q.uv = simd_make_float4(
                        t.x + (t.z - t.x) * (c.x - d.x) / ext_x,
                        t.y + (t.w - t.y) * (c.y - d.y) / ext_y,
                        t.x + (t.z - t.x) * (c.z - d.x) / ext_x,
                        t.y + (t.w - t.y) * (c.w - d.y) / ext_y);
                    q.draw = c;
                }
                atlas->push_quad(q);
            }

            // A sprite (e.g. glyph) tinted by color.
            void push_sprite_clipped(Sprite s, RGBA8Unorm_sRGB color) {
                push_quad_clipped(SpriteAtlas::make_quad(s, color));
            }


            // -- Higher-level helpers ---------------------------------------

            // Solid-color rectangle, hard-edged.
            void fill_rect(rect<float> r, RGBA8Unorm_sRGB color) {
                push_quad_clipped(SpriteAtlas::make_quad(
                    simd_make_float4(r.a.x, r.a.y, r.b.x, r.b.y), color));
            }

            // Gaussian prefilter sigma of mask edges, in pixels; matches
            // the rasterized glyphs (font.mm).
            static constexpr float kEdgeSigma = 0.5f;

            // Drop shadow defaults (for boxes; text is outlined instead):
            // a Gaussian blur of sigma 0.5 pt of the shape, offset 1 pt
            // down, opaque black.  (At 2 px/pt, the [1 4 6 4 1]/16
            // binomial's sigma of 1 px, 2 px down.)
            static constexpr float kShadowSigmaPt = 0.5f;
            static constexpr float2 kShadowOffsetPt = {0.0f, 1.0f};
            float shadow_sigma() const { return pt(kShadowSigmaPt); }
            float2 shadow_offset() const { return kShadowOffsetPt * pixels_per_point; }

            // Solid-color rectangle with rounded corners, antialiased in
            // quadFragmentShader.  The 50%-coverage edge lies on `r`.
            // radii are per corner, ordered (x0,y0) (x1,y0) (x1,y1)
            // (x0,y1), each clamped to the half-extent (a pill at most).
            void fill_round_rect(rect<float> r, float4 radii,
                                 RGBA8Unorm_sRGB color) {
                push_blurred_round_rect(r, radii, kEdgeSigma, color);
            }

            void fill_round_rect(rect<float> r, float radius,
                                 RGBA8Unorm_sRGB color) {
                fill_round_rect(r, simd_make_float4(radius, radius, radius, radius), color);
            }

            // The shadow `r` (with corner radii) casts: the same shape,
            // offset, and blurred by sigma.  Paint it before the shape.
            void drop_shadow(rect<float> r, float4 radii,
                             RGBA8Unorm_sRGB color = RGBA8Unorm_sRGB(0.0f, 0.0f, 0.0f, 1.0f)) {
                r += shadow_offset();
                push_blurred_round_rect(r, radii, shadow_sigma(), color);
            }

            // Rounded rectangle blurred by an isotropic Gaussian of the
            // given sigma.  The drawn quad extends three sigma beyond the
            // shape, where the coverage falls below 0.2%.
            void push_blurred_round_rect(rect<float> r, float4 radii, float sigma,
                                         RGBA8Unorm_sRGB color) {
                if (auto q = blurred_round_rect_quad(r, radii, sigma, color))
                    push_quad_clipped(*q);
            }

            // The quad push_blurred_round_rect emits, unclipped, for
            // callers that batch quads themselves.  nullopt if r is empty.
            static std::optional<WryQuadInstance>
            blurred_round_rect_quad(rect<float> r, float4 radii, float sigma,
                                    RGBA8Unorm_sRGB color) {
                const float hw = (r.b.x - r.a.x) * 0.5f;
                const float hh = (r.b.y - r.a.y) * 0.5f;
                if (hw <= 0.0f || hh <= 0.0f) return std::nullopt;
                const float e = 3.0f * sigma;
                WryQuadInstance q = SpriteAtlas::make_quad(
                    simd_make_float4(r.a.x, r.a.y, r.b.x, r.b.y), color);
                q.draw = q.shape + simd_make_float4(-e, -e, e, e);
                q.radii = simd_clamp(radii, 0.0f, std::min(hw, hh));
                q.sigma = sigma;
                q.flags = WryQuadFlagMasked;
                return q;
            }

            // What lies under a run of text.  Free text (the default) is
            // drawn over the world or other arbitrary backgrounds, so it
            // gets a heavy dark outline (WryQuadFlagOutlined) to read on
            // light and dark alike; text on an opaque panel or button
            // already has a known dark ground and is drawn plain.
            enum class TextGround { free, panel };

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
                                 RGBA8Unorm_sRGB color,
                                 TextGround ground = TextGround::free);

            // Marked-up text: a single line where `[...]` is a keycap --
            // its contents drawn as panel text on a small dark rounded
            // key, e.g. "[Shift+R] rotate back"; `[[` is a literal '['.
            // The first piece of the help-text markup the todo asks for
            // (glyphs would be the next).  marked_text_width measures the
            // same layout without drawing.  Defined in gui.mm.
            float2 draw_marked_text(float2 pen, StringView text, RGBA8Unorm_sRGB color);
            float marked_text_width(StringView text) const;
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

            // The widget tree this overlay lays out and paints, if any, for
            // the `widgets` and `press` commands.
            virtual Widget const* widget_root() const { return nullptr; }
        };

        // Ordered, top-of-stack-last.  Non-owning: the stack just borrows
        // pointers to overlays whose lifetime is managed elsewhere (today,
        // by direct membership in `wry::WorldState`).
        class OverlayStack {
        public:
            void push(Overlay* o)    { _overlays.push_back(o); }
            void pop()               { _overlays.pop_back(); }
            bool empty() const       { return _overlays.empty(); }
            std::size_t size() const { return _overlays.size(); }

            // Whether `o` is anywhere on the stack; used to make pushes
            // of persistent overlays (settings) idempotent.
            bool contains(Overlay const* o) const {
                for (Overlay const* p : _overlays)
                    if (p == o)
                        return true;
                return false;
            }

            // Top-down walk.  Returns true if any overlay consumed the
            // event, or if a modal overlay blocked it.
            bool dispatch(Event const& e);

            // Bottom-up walk.  Earlier overlays paint first, later on top.
            void paint(Painter& p);

            // Visit overlays topmost first (the order input reaches them).
            template<typename F>
            void for_each_top_down(F&& f) const {
                for (auto it = _overlays.rbegin(); it != _overlays.rend(); ++it)
                    f(**it);
            }

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

        // Quake-style drop-down console: toggled by the toggle-console
        // binding (backtick by default), captures all keyboard input
        // while active.  Enter issues the input line as a command
        // (core/docs/commands.md).  The line is a LineEditor with a
        // LineHistory behind it; the conventions for all of it are in
        // render/docs/line_editing.md.  Beyond the editor's own keys:
        //
        //   Enter                    submit
        //   Up / Down, Ctrl-P / N    walk the history (the typed line is
        //                            kept, and is the search prefix)
        //   Tab                      complete the word before the cursor
        //   Right / End at the end   accept the grey suggestion
        //   Alt-Right at the end     accept one word of it
        //   Cmd-V                    paste; each line break submits
        //   Ctrl-C                   cancel the pending command
        //   Escape, the toggle key   close
        //   click / drag / double    place the cursor / select / select a
        //   click on the input line  word
        //
        // The overlay is the controller and owns the model: the output
        // lines, the line editor, the history and the suggestion.  What
        // shows is a widget tree of views onto them (gui_widget.hpp: a
        // ConsoleView of a TextLog over a LineField), laid out, hit
        // tested and painted by the widget system, and listed by the
        // `widgets` command while the console is open.  The console's own
        // keys are handled here; the rest of the keys, and the mouse, go
        // to the LineField.
        class ConsoleOverlay : public Overlay {
        public:
            ConsoleOverlay();
            ~ConsoleOverlay() override;

            bool active() const { return _active; }
            void set_active(bool a) { _active = a; }
            bool on_event(Event const&) override;
            void paint(Painter&) override;

            // The widget tree, while the console is open.
            Widget const* widget_root() const override;

            // Append a line of output above the input line.
            void print(StringView line);

            // Wired by GuiContext to its command issue: the console's
            // Enter (origin console) and its toggle key (origin binding)
            // go through it.  Unwired, Enter just opens a new line and
            // the toggle acts directly.
            using Issue = std::function<void(command::Origin, StringView)>;
            void set_issue(Issue f) { _issue = std::move(f); }

            // Wired by GuiContext: Ctrl-C cancels the console's pending
            // command and the lines queued behind it.
            void set_cancel(std::function<void()> f) { _cancel = std::move(f); }

            // Wired by GuiContext to the registry: what could stand at
            // the end of this text (Tab, and the grey suggestion).
            using Complete = std::function<command::Completion(StringView before)>;
            void set_complete(Complete f) { _complete = std::move(f); }

            // Wired by GuiContext: where copy, cut and paste go.
            void set_clipboard(Clipboard const* c) {
                _clipboard = c;
                _input.set_clipboard(c);
            }

            // While active, the console eats keyboard but lets clicks
            // through to whatever is behind it, except those on its
            // input line.
            bool modal_keyboard() const override { return _active; }
            bool modal_mouse()    const override { return false; }

            // The output lines, oldest first.
            ContiguousDeque<String>& lines() { return _lines; }

            // The line being typed.
            LineEditor const& input() const { return _input; }

            // What the user has submitted.  The host attaches a file to
            // make it outlive the process.
            LineHistory& history() { return _history; }

            // The grey text offered after the line: the rest of the
            // newest history entry that begins with it, else of the one
            // word that completes it.  Empty unless the cursor is at the
            // end of a non-empty line.
            StringView suggestion() const { return _suggestion; }

            // Wired by the model so the console can echo "[~] Show console"
            // etc. into the floating log.
            void set_log(LogOverlay* l) { _log = l; }

            // Wired by GuiContext to the live keymap so the toggle key
            // follows the user's toggle-console binding.
            void set_keymap(Keymap const* k) { _keymap = k; }

        private:
            bool _on_key(Event const&);
            bool _on_mouse(Event const&);
            void _submit();
            void _paste();
            void _complete_word();
            void _accept_suggestion(bool whole);
            void _update_suggestion();

            ContiguousDeque<String> _lines;     // output, oldest first
            LineEditor _input;                  // the line being typed
            LineHistory _history;
            String _suggestion;

            // The views (gui_widget.hpp), onto the members above.
            std::unique_ptr<ConsoleView> _view;

            bool _active = false;
            // the console closed with a press on its input line held: the
            // release that follows is swallowed (see _on_mouse)
            bool _swallow_release = false;
            LogOverlay* _log = nullptr;
            Keymap const* _keymap = nullptr;
            Clipboard const* _clipboard = nullptr;
            Issue _issue;
            std::function<void()> _cancel;
            Complete _complete;
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
            void set_model(struct ::wry::WorldState* m) { _model = m; }

            // The palette's top edge in drawable pixels (y down), from the
            // same composition on_event inverts; the viewport's height if
            // there is no viewport
            float top_pixels() const;

            // Bring-up: WryRenderer fills this at init from assets.json.
            // The struct name `Palette<T>` reads as "a grid of T values";
            // this particular grid is "the controls", hence `_controls`.
            wry::Palette<wry::Term>& controls()             { return _controls; }
            wry::Palette<wry::Term> const& controls() const { return _controls; }

            // Layout, shared by the hit test and the renderer: the grid
            // of kCellPt-point cells (smaller only if the viewport is too
            // narrow for them: cell_size), i across and j down, centered
            // on the bottom edge of the viewport.  The rect of cell (i, j), in
            // drawable pixels; (i0, j0, i1, j1) spans cells i0 <= i < i1,
            // j0 <= j < j1.
            static constexpr float kCellPt = 30.0f;
            float cell_size(float2 viewport_px, float pixels_per_point) const;
            rect<float> cells_rect(float i0, float j0, float i1, float j1,
                                   float2 viewport_px, float pixels_per_point) const;

            // Renderer reads these to draw the highlights.  -1 means none.
            int selected_i() const { return _selected_i; }
            int selected_j() const { return _selected_j; }
            int hover_i()    const { return _hover_i; }
            int hover_j()    const { return _hover_j; }

            // True for exactly one frame after a selection change, so the
            // renderer knows to refresh the NSCursor image.  Renderer
            // clears it after consuming.  Anything else that changes the
            // held value (e.g. the reorientation keys) requests a refresh
            // the same way.
            bool cursor_needs_refresh() const { return _cursor_dirty; }
            void clear_cursor_refresh()      { _cursor_dirty = false; }
            void request_cursor_refresh()    { _cursor_dirty = true; }
            // The hand was dropped: no slot is the held one any more.
            void clear_selection()           { _selected_i = _selected_j = -1; }

            // Rewrite the cell showing `previous` so it shows `next`.  The
            // reorientation actions (R / Shift-R / H / V) call this so the
            // palette tracks the orientation in hand: the glyph turns with
            // the cursor, and a re-click picks that orientation back up.
            // Cells are one per orbit and pairwise distinct, so at most
            // one matches.  No-op when none does (hand and palette have
            // drifted apart; leave the palette alone rather than clobber
            // some other cell).
            void replace_glyph(Term previous, Term next);
            // The pipette picked up an opcode: the slot showing its
            // reorientation orbit becomes the held slot and shows the
            // picked orientation (no slot for an opcode without art).
            void select_orbit_of(Term picked);

        private:
            wry::Palette<wry::Term> _controls;
            int _selected_i = -1;
            int _selected_j = -1;
            int _hover_i    = -1;
            int _hover_j    = -1;
            bool _cursor_dirty = false;
            struct ::wry::WorldState* _model = nullptr;
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
            Widget const* widget_root() const override { return _root.get(); }
            void paint(Painter&) override;

            bool modal_mouse()    const override { return true; }
            bool modal_keyboard() const override { return true; }

            // The LOAD button needs to push the save-list overlay onto
            // the model's stack; we get the model pointer set once at
            // startup and the button's click lambda reads it via `this`.
            void set_model(struct ::wry::WorldState* m) { _model = m; }

        private:
            // unique_ptr to a forward-declared Widget so this header
            // doesn't need to know the layout of the widget tree.  The
            // tree is constructed in the .cpp (well, .mm).
            std::unique_ptr<Widget> _root;
            struct ::wry::WorldState* _model = nullptr;
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
        // The rows are populated from the real save files (io/save.hpp
        // enumerate_games) via refresh(); the MainMenuOverlay's LOAD button
        // refreshes before showing this, and delete_selected refreshes after
        // removing a file.

        class SaveListOverlay : public Overlay {
        public:
            SaveListOverlay();
            ~SaveListOverlay() override;

            bool on_event(Event const&) override;
            Widget const* widget_root() const override { return _root.get(); }
            void paint(Painter&) override;

            bool modal_mouse()    const override { return true; }
            bool modal_keyboard() const override { return true; }

            // load_selected / delete_selected drive the model's world and the
            // save files; set once at startup by the model constructor.
            void set_model(struct ::wry::WorldState* m) { _model = m; }

            // Rebuild the row list from the current save files.  Call before
            // showing the overlay (saves may have changed since last shown).
            void refresh();

        private:
            std::unique_ptr<Widget> _root;
            // Non-owning into the widget tree above; we need direct access
            // to scroll arithmetic and to flip the selected state on rows.
            class ScrollView*       _scroll = nullptr;
            std::vector<class Button*> _row_buttons;
            // File id for each row, parallel to _row_buttons; the selected
            // row's id is what load/delete act on.
            std::vector<int>           _save_ids;

            struct ::wry::WorldState* _model = nullptr;

            int _selected_index = 0;

            void rebuild_rows();
            void update_selection_visuals();
            void move_selection(int delta);
            void scroll_to_selected();
            void load_selected();
            void delete_selected();
        };

        // ----------------------------------------------------------------
        // Settings root overlay: KEY BINDINGS / RESET TO DEFAULTS / BACK.
        // App-tier: lives on GuiContext::overlays (pushed by
        // GuiContext::open_settings), so the same UI is reachable from
        // both the main-menu scene and the in-game menu.  Modal in both
        // axes; ESC closes.

        class SettingsOverlay : public Overlay {
        public:
            SettingsOverlay();
            ~SettingsOverlay() override;

            bool on_event(Event const&) override;
            Widget const* widget_root() const override { return _root.get(); }
            void paint(Painter&) override;

            bool modal_mouse()    const override { return true; }
            bool modal_keyboard() const override { return true; }

            // The buttons drive settings reset / the key-bindings overlay
            // through the owning GuiContext; set once in its constructor.
            void set_context(GuiContext* g) { _gui = g; }

        private:
            std::unique_ptr<Widget> _root;
            GuiContext* _gui = nullptr;
        };

        // ----------------------------------------------------------------
        // Key-bindings overlay: one row per action showing its display
        // name, its bound combos, and a CLEAR button.  Clicking the
        // action arms capture: the next keystroke becomes a binding for
        // that action, stolen from any action that previously held it;
        // Escape (or any click) cancels.  Every change saves
        // settings.json immediately.
        //
        // While capture is armed the overlay consumes *every* event.  It
        // sits on the app-tier stack, which each scene dispatches first,
        // so the next keystroke genuinely cannot land anywhere else --
        // no deeper hook into the platform layer is needed.
        //
        // Widget-tree rebuilds (row labels change on every binding
        // change) are deferred to paint() via _rows_dirty: a rebuild
        // inside a button's own on_click would destroy the button
        // mid-dispatch.

        class KeyBindingsOverlay : public Overlay {
        public:
            KeyBindingsOverlay();
            ~KeyBindingsOverlay() override;

            bool on_event(Event const&) override;
            Widget const* widget_root() const override { return _root.get(); }
            void paint(Painter&) override;

            bool modal_mouse()    const override { return true; }
            bool modal_keyboard() const override { return true; }

            void set_context(GuiContext* g) { _gui = g; }

            // Mark the rows stale (rebuilt lazily at next paint) and
            // disarm capture.  Called before the overlay is shown.
            void refresh();

            // Arm capture for `action`: the next bindable keystroke binds
            // to it.  Normally driven by clicking the action's row;
            // exposed so a caller (or a test) can arm it directly.
            void begin_capture(Action a) { _capturing = a; _rows_dirty = true; }
            bool is_capturing() const { return _capturing.has_value(); }

        private:
            std::unique_ptr<Widget> _root;
            // Non-owning pointer into the tree, for the height cap.
            class ScrollView* _scroll = nullptr;

            GuiContext* _gui = nullptr;

            // Armed capture target: the next keystroke binds to this
            // action.  nullopt = browsing.
            std::optional<Action> _capturing;

            bool _rows_dirty = true;
            float _built_pixels_per_point = 0.0f;   // of the font rebuild() used

            // Gaps and corner radii derive from the font.
            void rebuild(Font const*);
            void apply_captured_combo(KeyCombo);
        };

    } // namespace gui

} // namespace wry

#endif // gui_overlay_hpp
