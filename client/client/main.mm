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

#include "model.hpp"
#include "test.hpp"

int main(int argc, const char** argv) {
    
    {
        // randomness
        std::random_device a;
        printf("startup random\n");
        printf("    letter  : '%c'\n", std::uniform_int_distribution<>('A', 'Z')(a));
        printf("    decimal : %016" PRIu64 "\n", std::uniform_int_distribution<uint64_t>(0, 9999999999999999)(a));
        printf("    hex     : %016" PRIX64  "\n", std::uniform_int_distribution<uint64_t>(0)(a));
    }
    
    
    // execute unit tests on a background queue
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_BACKGROUND, 0), ^{
        wry::run_tests();
    });
    
    // create UIKit application
    @autoreleasepool {
        NSApplication* application = [NSApplication sharedApplication];
        WryDelegate* delegate = [[WryDelegate alloc] init];
        [application setDelegate:delegate];
        return NSApplicationMain(argc, argv); // noreturn
    }
}
