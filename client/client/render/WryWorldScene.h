//
//  WryWorldScene.h
//  client
//
//  Created by Antony Searle on 1/7/2023.
//

#ifndef WryWorldScene_h
#define WryWorldScene_h

#import <AVFoundation/AVFoundation.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

#include "model.hpp"

#import "WryRenderContext.h"
#import "WryScene.h"

// WryWorldScene is the game-world scene: it owns the deferred PBR pipelines,
// shadow / environment maps, meshes and G-buffers, steps the simulation in
// -update, and encodes it in -encodeIntoCommandBuffer:.  It conforms to
// WryScene so the host can drive it without knowing its concrete type.

@interface WryWorldScene : NSObject <WryScene>

// Built against the host-owned render context (device + shared 2D services).
- (nonnull instancetype)initWithContext:(nonnull WryRenderContext*)context
                                  model:(std::shared_ptr<wry::model>)model_;

// Advance the simulation one step.  Call once per frame before -render; the
// two are deliberately separate so a scene with no world (splash / menu) can
// skip the step while still drawing.
- (void)update;

// -encodeIntoCommandBuffer: comes from the WryScene protocol.

- (void)drawableResize:(CGSize)drawableSize;

-(void)resetCursor;

@end

#endif /* WryWorldScene_h */
