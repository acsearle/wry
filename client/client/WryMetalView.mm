//
//  WryMetalView.mm
//  client
//
//  Created by Antony Searle on 21/6/2023.
//

#include <iostream>
#include <thread>

#include "WryMetalView.h"

// [1] https://sarunw.com/posts/how-to-create-macos-app-without-storyboard/
// [2] https://developer.apple.com/documentation/metal/onscreen_presentation/creating_a_custom_metal_view?language=objc
// [3] https://developer.apple.com/documentation/metal/resource_synchronization/synchronizing_cpu_and_gpu_work?language=objc

@implementation WryMetalView
{
    NSTrackingArea* _tracking_area;
}

- (nonnull instancetype) initWithFrame:(CGRect)frame
{
    NSLog(@"%s\n", __PRETTY_FUNCTION__);
    if ((self = [super initWithFrame:frame])) {
    }
    return self;
}

- (void)viewDidChangeBackingProperties
{
    NSLog(@"%s\n", __PRETTY_FUNCTION__);
    [super viewDidChangeBackingProperties];
    [_delegate viewDidChangeBackingProperties];
}

- (void)setFrameSize:(NSSize)size
{
    NSLog(@"%s\n", __PRETTY_FUNCTION__);
    [super setFrameSize:size];
    [_delegate viewDidChangeFrameSize];
}

- (void)setBoundsSize:(NSSize)size
{
    NSLog(@"%s\n", __PRETTY_FUNCTION__);
    [super setBoundsSize:size];
    [_delegate viewDidChangeBoundsSize];
}

- (void)viewDidMoveToWindow
{
    NSLog(@"%s\n", __PRETTY_FUNCTION__);
    [super viewDidMoveToWindow];
    [_delegate viewDidMoveToWindow];
}

-(void) updateTrackingAreas {
    NSLog(@"%s\n", __PRETTY_FUNCTION__);
    _tracking_area = [[NSTrackingArea alloc] initWithRect:self.bounds
                                                  options:(NSTrackingMouseEnteredAndExited | NSTrackingActiveWhenFirstResponder)
                                                    owner:_delegate userInfo:nil];
    [self removeTrackingArea:_tracking_area];
    [self addTrackingArea:_tracking_area];
    [super updateTrackingAreas];
}


@end
