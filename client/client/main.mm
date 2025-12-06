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
    
    // auto unit_tests = wry::run_tests().start();
    wry::coroutine::Nursery nursery;
    nursery.spawn(wry::run_tests());
    
    
    wry::mutator_pin();
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
    wry::mutator_unpin();
    
    printf("main is terminal\n");
    
    // Blocking join unit test job
    printf("main is joining unit tests\n");
    nursery.sync_join();
    wry::mutator_pin();

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
