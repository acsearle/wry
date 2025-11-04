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


namespace wry::coroutine {

    SingleConsumerLatch::WillDecrement workyworky(SingleConsumerLatch*, int* p) {
        co_await suspend_and_schedule{};
        *p = 1;
    }
    
    co_fork spawning_coroutine_example(int n) {
                
        SingleConsumerLatch latch{64};
        
        int results[64] = {};
        
        for (int i = 0; i != 64; ++i)
            workyworky(&latch, results + i);

        printf("about to wait on latch\n");
        
        co_await latch;
        
        printf("resumed after latch\n");
        for (int i = 0; i != 64; ++i)
            assert(results[i] == 1);

        co_return;
    }

    
}


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
    
    std::thread collector_thread([](){
        wry::collector_run_on_this_thread();
    });
    wry::mutator_pin();
        
    
    std::vector<std::thread> workers;
    for (int i = 0; i != 4; ++i) {
        workers.emplace_back(&wry::coroutine::global_work_queue_service);
    }
    
    auto cx = wry::coroutine::spawning_coroutine_example(1000);
    
    
    
    
    // execute unit tests on a background thread
    // TODO: ... on the worker pool?
    std::thread tests(wry::run_tests);
    
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
    
    // join forked coroutines before canceling the queue
    cx.join();
    
    wry::coroutine::global_work_queue_cancel();
    while (!workers.empty()) {
        printf("main waiting to join a worker thread\n");
        workers.back().join();
        workers.pop_back();
    }
    
    printf("main waiting to join unit tests thread\n");
    tests.join();
    wry::mutator_unpin();
    wry::collector_cancel();
    printf("main waiting to join the collector thread\n");
    collector_thread.join();
    
    return EXIT_SUCCESS;
    
} // int main(int argc, char** argv)
