//
//  load_timing.hpp
//  client
//
//  Startup and asset-load timing, off unless WRY_LOAD_TIMING is set in the
//  environment (docs/async_loading.md).  Each line on stderr reads
//
//      [load] +  1234.5 ms main   12.3 ms  label
//
//  the time since the clock's origin (the first call: main marks it as it
//  starts), the thread (main or a worker), the span's own duration, and
//  what it was.  A disabled timer costs one load of a function-local
//  static and a branch; an enabled one a steady_clock read at each end and
//  an fprintf, so it stays out of per-frame and per-texel loops.
//

#ifndef load_timing_hpp
#define load_timing_hpp

#include <chrono>
#include <cstdio>
#include <cstdlib>

#include <pthread.h>

namespace wry {

    inline bool load_timing_enabled() {
        // A magic static: initialized once, thread-safely, on first use
        static const bool enabled = std::getenv("WRY_LOAD_TIMING") != nullptr;
        return enabled;
    }

    inline std::chrono::steady_clock::time_point load_timing_origin() {
        static const std::chrono::steady_clock::time_point origin = std::chrono::steady_clock::now();
        return origin;
    }

    inline void load_timing_print(std::chrono::steady_clock::time_point t0,
                                  std::chrono::steady_clock::time_point t1,
                                  const char* label,
                                  const char* detail = "") {
        using ms = std::chrono::duration<double, std::milli>;
        std::fprintf(stderr, "[load] +%9.1f ms %-6s %9.1f ms  %s%s\n",
                     ms(t1 - load_timing_origin()).count(),
                     pthread_main_np() ? "main" : "worker",
                     ms(t1 - t0).count(),
                     label, detail);
    }

    // A point in time: duration zero
    inline void load_timing_mark(const char* label, const char* detail = "") {
        if (!load_timing_enabled())
            return;
        (void) load_timing_origin();
        auto t = std::chrono::steady_clock::now();
        load_timing_print(t, t, label, detail);
    }

    // A scoped span: printed at destruction.  The label and detail must
    // outlive the timer (literals, or strings the caller keeps alive)
    struct LoadTimer {
        const char* _label;
        const char* _detail;
        std::chrono::steady_clock::time_point _t0;
        explicit LoadTimer(const char* label, const char* detail = "")
        : _label(label), _detail(detail) {
            if (load_timing_enabled()) {
                (void) load_timing_origin();
                _t0 = std::chrono::steady_clock::now();
            }
        }
        LoadTimer(LoadTimer const&) = delete;
        LoadTimer& operator=(LoadTimer const&) = delete;
        ~LoadTimer() {
            if (load_timing_enabled())
                load_timing_print(_t0, std::chrono::steady_clock::now(), _label, _detail);
        }
    };

} // namespace wry

#endif /* load_timing_hpp */
