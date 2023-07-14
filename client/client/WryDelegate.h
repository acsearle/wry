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
#include "ClientView.h"

@interface WryDelegate : NSResponder <NSApplicationDelegate, NSWindowDelegate,
    WryMetalViewDelegate>

-(nonnull instancetype) initWithModel:(std::shared_ptr<wry::model>) mdl;

@end

#endif /* WryDelegate_h */
