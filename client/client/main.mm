//
//  main.mm
//  client
//
//  Created by Antony Searle on 21/6/2023.
//

#import <AppKit/AppKit.h>
#import "WryDelegate.h"

#include "model.hpp"

// Programatically (i.e., without Storyboard or similar files) set up the
// application, window, and view.
//
// [1] https://sarunw.com/posts/how-to-create-macos-app-without-storyboard/

int main(int argc, const char** argv) {
    auto mdl = std::make_shared<wry::model>();
    @autoreleasepool {
        NSApplication* application = [NSApplication sharedApplication];
        WryDelegate* delegate = [[WryDelegate alloc] initWithModel:mdl];
        [application setDelegate:delegate];
        return NSApplicationMain(argc, argv); // [[noreturn]]
    }
}
