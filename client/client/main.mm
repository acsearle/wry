//
//  main.mm
//  client
//
//  Created by Antony Searle on 21/6/2023.
//

#import <AppKit/AppKit.h>
#import "WryDelegate.h"

#include "model.hpp"
#include "test.hpp"

int main(int argc, const char** argv) {
    
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
