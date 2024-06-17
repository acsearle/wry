//
//  main.mm
//  client
//
//  Created by Antony Searle on 21/6/2023.
//

#include <cinttypes>
#include <random>

#import <AppKit/AppKit.h>
#import "WryDelegate.h"

#include "gc.hpp"
#include "model.hpp"

#include "test.hpp"

int main(int argc, const char** argv) {
    
    // initialize garbage collection and spawn collector thread
    wry::gc::collector_start();
    // it is now safe to call mutator_enter on any thread; the collector global
    // state is set up even though the spawned collector thread may not have
    // actually been scheduled yet
    
    // randomness
    std::random_device rd;
    printf("startup random\n");
    printf("    letter  : '%c'\n", std::uniform_int_distribution<>('A', 'Z')(rd));
    printf("    decimal : %016" PRIu64 "\n", std::uniform_int_distribution<uint64_t>(0, 9999999999999999)(rd));
    printf("    hex     : %016" PRIX64  "\n", std::uniform_int_distribution<uint64_t>(0)(rd));
    
    
    // execute unit tests on a background queue
    @autoreleasepool {
        dispatch_async(dispatch_get_global_queue(QOS_CLASS_BACKGROUND, 0), ^{            
            wry::run_tests();
        });
    }
    
    // create UIKit application
    @autoreleasepool {
        NSApplication* application = [NSApplication sharedApplication];
        WryDelegate* delegate = [[WryDelegate alloc] init];
        [application setDelegate:delegate];
        return NSApplicationMain(argc, argv); // noreturn
    }
    
    // unreachable
    
}
