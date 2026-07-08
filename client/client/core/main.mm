//
//  main.mm
//  client
//
//  Created by Antony Searle on 21/6/2023.
//

#include <cinttypes>
#include <random>
#include <thread>
#include <filesystem>

#import <AppKit/AppKit.h>
#include <sqlite3.h>

#include "atomic.hpp"
#include "vector.hpp"

#include "world_state.hpp"
#include "test.hpp"
#include "coroutine.hpp"
#include "thread_public.hpp"

#import "WryDelegate.h"


int main(int argc, const char** argv) {

    // Argument handling, deliberately minimal.
    //
    //   --test-only [SUBSTRING]
    //       Skip NSApplication setup; run the test suite, then exit.
    //       If SUBSTRING is given, only tests whose metadata contains
    //       it (as a substring) are run.
    //
    // TODO: paths, immediate load savegame, etc.
    bool test_only = false;
    std::string_view test_filter;
    for (int i = 1; i < argc; ++i) {
        std::string_view a = argv[i];
        if (a == "--test-only") {
            test_only = true;
            // Optional positional filter consumes the next arg if it
            // isn't itself a flag.
            if (i + 1 < argc && argv[i+1][0] != '-')
                test_filter = argv[++i];
        }
    }

    std::filesystem::current_path("/Users/antony/Desktop/assets/");

    {
        // randomness
        std::random_device rd;
        printf("startup random\n");
        printf("    letter  : '%c'\n", std::uniform_int_distribution<>('A', 'Z')(rd));
        printf("    decimal : %016" PRIu64 "\n", std::uniform_int_distribution<uint64_t>(0, 9999999999999999)(rd));
        printf("    hex     : %016" PRIX64  "\n", std::uniform_int_distribution<uint64_t>(0)(rd));
    }

    std::thread collector_thread(&wry::collector_run_on_this_thread);
    std::vector<std::thread> workers;
    for (int i = 0; i != 4; ++i) {
        workers.emplace_back(&wry::global_work_queue_service);
    }

    // In --test-only mode there is no NSApp event loop driving the epoch
    // forward, so phase transitions that gate on `_finalized` (which only
    // advances when the collector ingests fresh mutator reports) would
    // stall once the test suspends.  A small heartbeat thread cycles
    // pin/unpin to publish empty reports, keeping the collector advancing.
    // The GUI flow doesn't need this — its renderer / event handling
    // produces plenty of mutator activity.
    std::atomic<bool> heartbeat_stop{false};
    std::thread heartbeat_thread;
    if (test_only) {
        heartbeat_thread = std::thread([&heartbeat_stop] {
            pthread_setname_np("test-heartbeat");
            wry::mutator_pin();
            wry::thread_public_register("test-heartbeat");
            wry::mutator_unpin();
            while (!heartbeat_stop.load(std::memory_order_relaxed)) {
                wry::mutator_pin();
                wry::mutator_unpin();
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            wry::mutator_pin();
            wry::thread_public_deregister();
            wry::mutator_unpin();
        });
    }

    // TODO: At this point, optionally run a stress test of the garbage
    // collector and epoch system itself.

    // TODO: Does it make sense to nest lifetime regions?  I think the epoch is
    // already implementing this


    // TODO: We rely on the epoch system to keep the hidden stack variables of
    // coroutines alive, idiomatically pinning the epoch in the root of a
    // tree of work.


    // Anchor the unit-test runner (and, later, any background saves) in the
    // process-lifetime WaitGroup, which we block on before tearing down the
    // thread pool.
    wry::wait_group_spawn(wry::run_tests(test_filter));


    wry::mutator_pin();
    wry::thread_public_register("main");

    if (!test_only) {
        @autoreleasepool {

            // create AppKit application

            NSApplication* application = [NSApplication sharedApplication];
            WryDelegate* delegate = [[WryDelegate alloc] init];
            [application setDelegate:delegate];

            // return NSApplicationMain(argc, argv); // noreturn
            [application finishLaunching];
            while (![delegate done]) {
                @autoreleasepool {
                    while (NSEvent* event = [application nextEventMatchingMask:NSEventMaskAny
                                                                     untilDate:nil
                                                                        inMode:NSDefaultRunLoopMode
                                                                       dequeue:YES]) {
                        [application sendEvent:event];
                    }
                    // The (X) button hides the window from within -sendEvent:
                    // (windowShouldClose: -> orderOut: + done).  Skip the final
                    // render -- no point drawing into a window that's going away.
                    if ([delegate done])
                        break;
                    [application updateWindows];
                    [delegate render];
                }
            }

            // Shutdown.  The (X) button and QUIT TO DESKTOP both hid the window
            // (orderOut:) and set `done`, ending the loop above.  (We hide
            // rather than -close: a real close tears down the CAMetalLayer and
            // collides with the drain pump below -- see the quit block in
            // WryDelegate for the full rationale.)
            printf("main is terminal\n");
            printf("main is joining background work\n");
            wry::mutator_unpin();
            // Hand the menu bar back / drop out of the foreground while we drain:
            // closing the last window does NOT deactivate a window-less app, so
            // otherwise the app would keep owning the menu bar until the process
            // exits.  (We can't [NSApp terminate:] -- it would exit() past the
            // RAII teardown below.)
            [application hide:nil];
            // Stay unpinned for the rest of shutdown.  If main re-pins, the
            // collector can't advance the epoch past main's pin, so its
            // `epoch::wait` in `loop_until_canceled` never returns and the
            // collector_thread.join() below deadlocks.  main pumps the run loop
            // below but never as a mutator; the tasks finishing on worker
            // threads are the mutators the collector needs.

            // AppKit only hands the window's removal to the Window Server when
            // the run loop next sleeps (kCFRunLoopBeforeWaiting commits the
            // pending transaction); after that one pass the Window Server takes
            // the window down on its own timeline, independent of this thread.
            // do/while so we always make at least one such pass, even when the
            // wait group is already drained -- otherwise a QUIT with no
            // background work in flight would fall straight into the blocking
            // joins below with the window still on screen (the original bug).
            // When work *is* in flight the loop keeps pumping so a long save
            // doesn't strand a half-closed window; the 50ms timeout just bounds
            // the poll.  (The terminating nil-returning nextEvent call is the
            // one that sleeps and so commits the removal.)

            // coroutine layout-compatible, with resume as first member
            struct callback_t {
                void (*resume)(void*);
                wry::Atomic<bool> done{false};
            };
            callback_t c;
            c.resume = [](void* p) {
                NSEvent* event = [NSEvent otherEventWithType:NSEventTypeApplicationDefined
                                                    location:NSMakePoint(0.0, 0.0)
                                               modifierFlags:0
                                                   timestamp:0
                                                windowNumber:0
                                                     context:nil
                                                     subtype:0
                                                       data1:0
                                                       data2:0];
                [[NSApplication sharedApplication] postEvent:event atStart:NO];
                ((callback_t*)p)->done.store_release(true);
            };

            wry::wait_group_set_callback(&c);

            do {
                @autoreleasepool {
                    while (NSEvent* event = [application nextEventMatchingMask:NSEventMaskAny
                                                                     untilDate:[NSDate dateWithTimeIntervalSinceNow:0.05]
                                                                        inMode:NSDefaultRunLoopMode
                                                                       dequeue:YES]) {
                        [application sendEvent:event];
                    }
                }
            } while (!c.done.load_acquire());

        } // @autoreleasepool
    } else {
        printf("main is terminal\n");
        printf("main is joining background work\n");
        // No run loop to service in headless test mode, so just block on the
        // drain.  Stay unpinned afterwards for the same reason as the GUI branch.
        wry::mutator_unpin();
        wry::wait_group_wait();
    }

    if (test_only) {
        heartbeat_stop.store(true, std::memory_order_relaxed);
        heartbeat_thread.join();
    }

    printf("main is joining worker threads\n");
    // Transient pin only (like collector_cancel's poke): main must not
    // *stay* pinned during shutdown.
    wry::mutator_pin();
    wry::thread_public_deregister();
    wry::mutator_unpin();
    wry::global_work_queue_cancel();
    while (!workers.empty()) {
        workers.back().join();
        workers.pop_back();
    }
    
    printf("main is joining collector thread\n");
    wry::collector_cancel();
    collector_thread.join();
    printf("main is done\n");
    return EXIT_SUCCESS;
    
} // int main(int argc, char** argv)
