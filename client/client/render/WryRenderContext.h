//
//  WryRenderContext.h
//  client
//
//  Created by Antony Searle on 2026-06-24.
//

#ifndef WryRenderContext_h
#define WryRenderContext_h

#import <Metal/Metal.h>

#include "font.hpp"
#include "SpriteAtlas.hpp"

// WryRenderContext owns the scene-agnostic rendering infrastructure: the
// Metal device / command queue / shader library, the small pile of state
// every scene needs (depth-stencil states, the full-screen quad), the
// pipeline / texture factory helpers, and the shared 2D services (sprite
// atlas, font, overlay compositing pipeline).
//
// It is the seam between "basic rendering infrastructure needed by all
// scenes" and the scene-specific code.  Every scene (WryWorldScene,
// WrySplashScene, WryMainMenuScene) is built against one shared context, so
// they share the device + library + command queue and can draw text / sprites
// through one shared atlas without re-deriving any of this.

@interface WryRenderContext : NSObject

- (nonnull instancetype)initWithDevice:(nonnull id<MTLDevice>)device
                   drawablePixelFormat:(MTLPixelFormat)drawablePixelFormat;

// ---- Device infrastructure (every scene needs these) -------------------

@property (nonatomic, readonly, nonnull) id<MTLDevice> device;
@property (nonatomic, readonly, nonnull) id<MTLLibrary> library;
@property (nonatomic, readonly, nonnull) id<MTLCommandQueue> commandQueue;
@property (nonatomic, readonly) MTLPixelFormat drawablePixelFormat;

@property (nonatomic, readonly, nonnull) id<MTLBuffer> screenTriangleStripVertexBuffer;
@property (nonatomic, readonly, nonnull) id<MTLDepthStencilState> enabledDepthStencilState;
@property (nonatomic, readonly, nonnull) id<MTLDepthStencilState> disabledDepthStencilState;

// ---- Shared 2D services (menus, splash, and the world HUD all use these) -

@property (nonatomic, readonly, nonnull) wry::SpriteAtlas* atlas;
@property (nonatomic, readonly, nonnull) wry::Font* font;
@property (nonatomic, readonly, nonnull) id<MTLRenderPipelineState> overlayRenderPipelineState;

// 2D UI pipeline for scenes that draw directly into their own single
// RGBA16Float color target (splash, menu), as opposed to the overlay pipeline
// which runs inside the world's deferred G-buffer pass.  Same shaders, one
// color attachment, alpha blend, no depth.
@property (nonatomic, readonly, nonnull) id<MTLRenderPipelineState> uiRenderPipelineState;

// Returned by reference: callers read the cubic-bezier / glyph tables in
// place rather than copying the (vector-backed) Font2 each access.
- (wry::Font2&)font2;

// ---- Factory helpers ---------------------------------------------------
//
// Thin wrappers around the device that abort() on failure, matching how
// the renderer treats a missing pipeline / shader as fatal.

- (nonnull id<MTLFunction>)newFunctionWithName:(nonnull NSString*)name;

- (nonnull id<MTLRenderPipelineState>)
    newRenderPipelineStateWithDescriptor:(nonnull MTLRenderPipelineDescriptor*)descriptor;

- (nonnull id<MTLRenderPipelineState>)
    newRenderPipelineStateWithMeshDescriptor:(nonnull MTLMeshRenderPipelineDescriptor*)descriptor;

- (nonnull id<MTLTexture>)newTextureFromResource:(nonnull NSString*)name
                                          ofType:(nonnull NSString*)ext;

- (nonnull id<MTLTexture>)newTextureFromResource:(nonnull NSString*)name
                                          ofType:(nonnull NSString*)ext
                                 withPixelFormat:(MTLPixelFormat)pixelFormat;

@end

#endif /* WryRenderContext_h */
