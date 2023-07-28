//
//  WryDelegate.h
//  client
//
//  Created by Antony Searle on 21/6/2023.
//

#ifndef WryDelegate_h
#define WryDelegate_h

#import <AppKit/AppKit.h>

#include "model.hpp"
#include "WryMetalView.h"

// WryDelegate receives all notifications from macOS and AppKit that were
// variously directed to the Application, Window or View, and handles them
// on the main thread, typically passing them on asynchronously to the
// model or the render threads.

@interface WryDelegate : NSResponder <NSApplicationDelegate, NSWindowDelegate,
    WryMetalViewDelegate>

-(nonnull instancetype) initWithModel:(std::shared_ptr<wry::model>) mdl;

@end

#endif /* WryDelegate_h */
