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

// Programatically (i.e., without Storyboard or similar files) set up the
// application, window, and view.
//
// [1] https://sarunw.com/posts/how-to-create-macos-app-without-storyboard/

int main(int argc, const char** argv) {
    
#ifndef NDEBUG
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_BACKGROUND, 0), ^{
        wry::run_tests(); // perform unit tests on a background queue
    });
#endif
    
    // create app
    auto mdl = std::make_shared<wry::model>();
    
    @autoreleasepool {
        NSApplication* application = [NSApplication sharedApplication];
        WryDelegate* delegate = [[WryDelegate alloc] initWithModel:mdl];
        [application setDelegate:delegate];
        return NSApplicationMain(argc, argv); // [[noreturn]]
    }
}
