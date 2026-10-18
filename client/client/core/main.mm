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
#include "execution.hpp"
#include "kqueue_reactor.hpp"
#include "nursery.hpp"
#include "thread_public.hpp"

#import "WryDelegate.h"


namespace {

    // The receiver for the process scope's join on the GUI path: completion
    // arrives on a worker; it wakes main's run loop with an event and sets
    // `done`.  The join's value is its cancelled-child count, not needed
    // here; the join runs under no token, so it is never cancelled
    struct MainJoinReceiver {
        wry::Atomic<bool>* _done;
        friend void set_value(MainJoinReceiver&& receiver, std::ptrdiff_t&&) {
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
            receiver._done->store_release(true);
        }
        friend void set_error(MainJoinReceiver&&, std::exception_ptr&&) { abort(); }
        friend void set_stopped(MainJoinReceiver&&) { abort(); }
        friend std::stop_token get_stop_token(MainJoinReceiver const&) { return {}; }
    };

} // namespace

int main(int argc, const char** argv) {

    // Argument handling, deliberately minimal.
    //
    //   --test-only [SUBSTRING]
    //       Skip NSApplication setup; run the test suite, then exit.
    //       If SUBSTRING is given, only tests whose metadata contains
    //       it (as a substring) are run.
    //
    //   --headless
    //       No window: run the game driven through its control socket,
    //       rendering frames only while commands have work (see
    //       core/docs/commands.md section 9).  Implies --control-socket.
    //
    //   --control-socket [PATH]   (read by WryDelegate)
    //
    // TODO: paths, immediate load savegame, etc.
    bool test_only = false;
    bool headless = false;
    std::string_view test_filter;
    for (int i = 1; i < argc; ++i) {
        std::string_view a = argv[i];
        if (a == "--test-only") {
            test_only = true;
            // Optional positional filter consumes the next arg if it
            // isn't itself a flag.
            if (i + 1 < argc && argv[i+1][0] != '-')
                test_filter = argv[++i];
        } else if (a == "--headless") {
            headless = true;
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

    wry::this_thread_set_is_mutator();
    std::thread collector_thread(&wry::collector_run_on_this_thread);
    std::vector<std::thread> workers;
    for (int i = 0; i != 4; ++i) {
        workers.emplace_back(&wry::global_work_queue_service);
    }

    wry::global_reactor_start();

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


    // The process scope: every background task -- the test runner, saves,
    // map builds, texture loads, the control socket's loop -- is spawned
    // into this nursery, from main or from a live child of it, and shutdown
    // is its join, before the pool is torn down.  Its interior token is the
    // root token, which the GUI shutdown requests so that servers parked in
    // the reactor unwind rather than hold the join open.
    wry::coroutine::Nursery root;

    // The unit-test runner, shielded from the root token: the suite is not
    // written to be cancelled (a test that owns a nursery with live children
    // sleeps before stopping them, and a root stop request landing in that
    // sleep destroys the test's frame under its nursery, which asserts), so
    // QUIT waits for the suite.
    root.soon([](std::string_view filter) -> wry::coroutine::Task {
        (void) co_await wry::execution::shield(wry::run_tests(filter));
    }(test_filter));


    wry::mutator_pin();
    wry::thread_public_register("main");

    if (!test_only) {
        @autoreleasepool {

            // create AppKit application

            NSApplication* application = [NSApplication sharedApplication];
            WryDelegate* delegate = [[WryDelegate alloc] initWithScope:&root];
            [application setDelegate:delegate];

            if (headless) {
                // No window, no Dock icon, no event loop: frames run while
                // commands have work, and otherwise main sleeps on the
                // control socket (repinning, so the collector advances).
                [application setActivationPolicy:NSApplicationActivationPolicyProhibited];
                // Never release the delegate.  Its scenes own the WorldState,
                // whose Root destructors shade; run after main unpins for
                // shutdown (below), a shade during marking traps
                // (WRY-GC-UNPINNED).  Windowed, the metal view's strong
                // delegate reference already keeps it alive to exit; headless
                // has no view, so the same lifetime is made explicit.
                (void) CFBridgingRetain(delegate);
                [delegate setUpHeadless];
                while (![delegate done]) {
                    @autoreleasepool {
                        if ([delegate hasCommandWork])
                            [delegate render];
                        else
                            [delegate waitForCommands];
                    }
                }
            }

            // return NSApplicationMain(argc, argv); // noreturn
            if (!headless)
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
            // No more commands: shut the control socket down (its accept
            // loop finishes in the drain below) and remove its file.
            [delegate shutdownControlSocket];
            printf("main is joining background work\n");
            // Shutdown has begun: request the root token, so any task parked
            // in the reactor (a server's accept, a timed wait) unwinds now
            // rather than holding the join below open.  Finite work drains.
            root.request_stop();
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
            // scope is already drained -- otherwise a QUIT with no
            // background work in flight would fall straight into the blocking
            // joins below with the window still on screen (the original bug).
            // When work *is* in flight the loop keeps pumping so a long save
            // doesn't strand a half-closed window; the 50ms timeout just bounds
            // the poll.  (The terminating nil-returning nextEvent call is the
            // one that sleeps and so commits the removal.)

            // The join, delivered to this run loop: the root's join is
            // lifted into a Future and connected to a receiver that posts
            // an event and sets `done`, on whichever worker the last child
            // retires on.  Started here, inline: if the scope is already
            // drained the receiver fires before start returns
            wry::Atomic<bool> done{false};
            auto joining = root.join();
            auto joined = wry::coroutine::connect(
                wry::coroutine::lift_awaitable<decltype(joining)&>(&joining),
                MainJoinReceiver{&done});
            wry::coroutine::start(joined);

            do {
                @autoreleasepool {
                    while (NSEvent* event = [application nextEventMatchingMask:NSEventMaskAny
                                                                     untilDate:[NSDate dateWithTimeIntervalSinceNow:0.05]
                                                                        inMode:NSDefaultRunLoopMode
                                                                       dequeue:YES]) {
                        [application sendEvent:event];
                    }
                }
            } while (!done.load_acquire());

            // Drained: the socket's loop has finished (GuiContext itself is
            // not torn down at exit, so this is explicit)
            [delegate releaseControlSocket];

        } // @autoreleasepool
    } else {
        printf("main is terminal\n");
        printf("main is joining background work\n");
        // No run loop to service in headless test mode, so just block on the
        // join.  Stay unpinned afterwards for the same reason as the GUI
        // branch.  The root token is not requested here: nothing but the
        // (shielded) test runner is in the scope.
        wry::mutator_unpin();
        (void) wry::coroutine::sync_wait(root.join());
    }

    if (test_only) {
        heartbeat_stop.store(true, std::memory_order_relaxed);
        heartbeat_thread.join();
    }

    // The scope has drained, so no I/O waits are outstanding;
    // stop the reactor before cancelling the workers its wake-ups target.
    wry::global_reactor_stop();

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
