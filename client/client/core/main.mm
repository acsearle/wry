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

#include "model.hpp"
#include "test.hpp"
#include "coroutine.hpp"

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
            while (!heartbeat_stop.load(std::memory_order_relaxed)) {
                wry::mutator_pin();
                wry::mutator_unpin();
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
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
                    [application updateWindows];
                    [delegate render];
                }
            }

        } // @autoreleasepool
    }

    printf("main is terminal\n");

    // Block until all WaitGroup-anchored work (the unit tests, any in-flight
    // background saves) has finished -- the GUI loop has stopped, so nothing
    // new is spawned past this point, and the workers are still alive to drain
    // it.  Must precede global_work_queue_cancel below.
    printf("main is joining background work\n");
    wry::mutator_unpin();
    wry::wait_group_wait();
    // Stay unpinned for shutdown.  If main re-pins here, the collector
    // can't ever advance the epoch past main's pin, so its `epoch::wait`
    // at the top of `loop_until_canceled` never returns and the
    // subsequent `collector_thread.join()` deadlocks.  None of the
    // teardown calls below need main to be a mutator.

    if (test_only) {
        heartbeat_stop.store(true, std::memory_order_relaxed);
        heartbeat_thread.join();
    }

    printf("main is joining worker threads\n");
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
