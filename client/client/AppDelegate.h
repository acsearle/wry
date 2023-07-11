//
//  AppDelegate.h
//  client
//
//  Created by Antony Searle on 21/6/2023.
//

#ifndef AppDelegate_h
#define AppDelegate_h

#import <AppKit/AppKit.h>

#include "model.hpp"

@interface AppDelegate : NSObject <NSApplicationDelegate>

-(nonnull instancetype) initWithModel:(std::shared_ptr<wry::model>) mdl;

@end

#endif /* AppDelegate_h */
