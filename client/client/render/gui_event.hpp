//
//  gui_event.hpp
//  client
//
//  Created by Antony Searle on 2026-05-21.
//
//  Platform-agnostic input events.  WryDelegate is the sole place that knows
//  about NSEvent; it translates each NSEvent into a wry::gui::Event and
//  enqueues it on the model's EventQueue.  Everything downstream (the
//  overlay stack once it exists, and meanwhile the transitional pump_legacy)
//  reads from the queue.  No NSEvent / AppKit type appears past this file.
//
//  Coordinates are in logical points (NSView "points"), with the origin at
//  the top-left of the metal view and y increasing downward.  That matches
//  the 2D overlay convention used by the text and sprite code; the y-flip
//  vs. Cocoa's bottom-left view space happens once, in the delegate.
//

#ifndef gui_event_hpp
#define gui_event_hpp

#include <cstddef>
#include <cstdint>

#include "contiguous_deque.hpp"
#include "simd.hpp"

namespace wry { struct model; }

// Apple-style discriminator at global scope: a non-scoped enum with a Wry
// prefix on the type and on every value, matching the convention Cocoa /
// HIToolbox / Win32 / X11 use for their public C types.  The point isn't
// pretty -- it's that the name is globally unique against every system
// header we will ever pull in.  We hit this directly: <HIToolbox/Events.h>
// declares `typedef UInt16 EventKind` at global scope, and any TU with both
// that header and `using namespace wry::gui` made the bare name `EventKind`
// ambiguous.  Wry-prefixing the enum sidesteps the collision permanently
// and sets the pattern for future enums we expose to platform code.
typedef enum WryEventKind : uint8_t {
    WryEventKindUnknown,
    WryEventKindKeyDown,
    WryEventKindKeyUp,
    WryEventKindMouseMove,
    WryEventKindMouseDown,
    WryEventKindMouseUp,
    WryEventKindMouseEnter,
    WryEventKindMouseExit,
    WryEventKindScroll,
} WryEventKind;

namespace wry::gui {

    enum class MouseButton : uint8_t {
        Left,
        Right,
        Middle,
        Other,
    };

    // Modifier flags.  Bit names are platform-neutral: Alt corresponds to
    // Option on macOS; Cmd to Command on macOS, the Windows key on Windows,
    // and Super on Linux.
    struct Modifiers {
        enum : uint16_t {
            Shift = 1u << 0,
            Ctrl  = 1u << 1,
            Alt   = 1u << 2,
            Cmd   = 1u << 3,
            Caps  = 1u << 4,
            Fn    = 1u << 5,
        };
        uint16_t bits = 0;

        constexpr bool has(uint16_t m) const { return (bits & m) != 0; }
    };

    // Logical key codes.  Printable ASCII keys are reported as their
    // unmodified, lowercased ASCII value: Shift+A reports key='a' with
    // Modifiers::Shift in mods.  Named / navigation / function keys live
    // above the ASCII range so they never collide with printables.
    //
    // Use Event::text to read the modifier-applied text representation of a
    // keystroke (e.g. "A", "!", "$") for text-input use cases.
    namespace key {
        constexpr uint32_t Unknown    = 0x000;
        // 0x20..0x7E: ASCII printable (lowercased letters).
        constexpr uint32_t Backspace  = 0x100;
        constexpr uint32_t Tab        = 0x101;
        constexpr uint32_t Enter      = 0x102;
        constexpr uint32_t Escape     = 0x103;
        constexpr uint32_t Delete     = 0x104; // forward-delete
        constexpr uint32_t ArrowUp    = 0x110;
        constexpr uint32_t ArrowDown  = 0x111;
        constexpr uint32_t ArrowLeft  = 0x112;
        constexpr uint32_t ArrowRight = 0x113;
        constexpr uint32_t Home       = 0x114;
        constexpr uint32_t End        = 0x115;
        constexpr uint32_t PageUp     = 0x116;
        constexpr uint32_t PageDown   = 0x117;
        constexpr uint32_t F1         = 0x120;
        // F2..F12 are F1+1 .. F1+11; no need to enumerate yet.
    } // namespace key

    struct Event {
        WryEventKind kind = WryEventKindUnknown;
        Modifiers mods{};

        // Mouse*, Scroll: position in logical points, top-left origin.
        // Undefined for KeyDown / KeyUp.
        float2 location = {0.0f, 0.0f};

        // MouseDown / MouseUp only.
        MouseButton button = MouseButton::Left;

        // KeyDown / KeyUp only.  Unmodified key (see `key` namespace).
        uint32_t key = key::Unknown;
        bool is_repeat = false;

        // KeyDown only.  The modifier-applied text representation of this
        // keystroke as UTF-8, NUL-terminated.  Empty for navigation /
        // function keys and for keystrokes that produce no insertable text.
        // 12 bytes is enough for any single grapheme cluster from typical
        // input (no IME support yet).
        char text[12] = {};

        // Scroll only.  Delta in logical points; positive y = content
        // scrolls down.
        // TODO: scroll_delta is currently multiplied by backingScaleFactor
        // in the delegate to preserve the pre-event-queue arithmetic in the
        // legacy _looking_at handler.  Normalize to logical points when the
        // world-pan code becomes a proper world-overlay handler.
        float2 scroll_delta = {0.0f, 0.0f};
    };

    // Per-frame event queue.  Producer is WryDelegate's NSResponder
    // callbacks; consumer is the per-frame pump in `[WryDelegate render]`.
    // Both run on the main thread, so no synchronization is required.
    struct EventQueue {
        ContiguousDeque<Event> events;

        void push(Event const& e) { events.push_back(e); }
        bool empty() const { return events.empty(); }
        std::size_t size() const { return events.size(); }

        // front()-then-pop_front pattern (ContiguousDeque's pop_front returns
        // void); copies the event out before destroying it in the queue.
        Event pop_front() {
            Event e = events.front();
            events.pop_front();
            return e;
        }

        void clear() { events.clear(); }
    };

    // Per-frame event pump.  Drains EventQueue, walking each event through
    // the model's OverlayStack (top-down) and falling back -- for events
    // that no overlay claimed -- to a transitional legacy handler that
    // still backs the world click / scroll-pan / debug-toggle paths.  As
    // more overlays come online (WorldOverlay, MainMenu, ...) the legacy
    // fallback shrinks and is eventually deleted.
    //
    //   view_size_pt: bounds of the metal view in logical points, used by
    //   the legacy fallback to map event.location into the model's NDC
    //   `_mouse` field while the renderer still expects NDC there.
    void pump(model& m, float2 view_size_pt);

} // namespace wry::gui

#endif // gui_event_hpp
