//
//  gui_clipboard.hpp
//  client
//
//  Created by Antony Searle on 2026-11-05.
//
//  The system clipboard, as plain text.
//
//  Every platform has one (NSPasteboard, the Win32 clipboard, an X11 or
//  Wayland selection) and every one of them can be asked for "the text,
//  if what is there has a text form".  That is the whole interface.  The
//  platform layer installs the real one (WryDelegate.mm, windowed runs
//  only); until then, and in headless and offscreen runs and in tests,
//  the clipboard is private to the process, so a driven run never reads
//  or overwrites what the user has copied.
//

#ifndef gui_clipboard_hpp
#define gui_clipboard_hpp

#include <functional>
#include <memory>

#include "string.hpp"

namespace wry::gui {

    struct Clipboard {

        // The clipboard's text; empty if it holds none.
        std::function<String()> read;

        // Replace the clipboard's contents with `text`.
        std::function<void(StringView)> write;

        // The in-process clipboard.
        Clipboard() {
            auto held = std::make_shared<String>();
            read = [held] { return *held; };
            write = [held](StringView text) { *held = text; };
        }

    };

} // namespace wry::gui

#endif /* gui_clipboard_hpp */
