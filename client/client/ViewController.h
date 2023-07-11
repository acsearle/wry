//
//  ViewController.h
//  client
//
//  Created by Antony Searle on 21/6/2023.
//

#ifndef ViewController_h
#define ViewController_h

#import <AppKit/AppKit.h>

#include "ClientView.h"
#include "model.hpp"

@interface ViewController : NSViewController <ClientViewDelegate>

-(nonnull instancetype) initWithModel:(std::shared_ptr<wry::model>) mdl;

@end

#endif /* ViewController_h */
