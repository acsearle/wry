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

// ---- Scene backdrop helpers --------------------------------------------

// Load every "<prefix>*.<ext>" texture in the resource directory, sorted by
// filename.  Scenes use this for their backdrop images (splash / main menu),
// so dropping another matching file in just works.
- (nonnull NSArray<id<MTLTexture>>*)loadTexturesWithPrefix:(nonnull NSString*)prefix
                                                    ofType:(nonnull NSString*)ext;

// Draw `texture` across the whole viewport into the current render pass,
// sampling the texCoord sub-rect `window` = (u0, v0, u1, v1) and modulating
// by `alpha` with straight (non-premultiplied) alpha blending.  This is the
// backdrop primitive: `alpha` drives fades, `window` drives Ken-Burns
// pan / zoom.  The caller owns the render pass (begin / clear / end).
- (void)drawImage:(nonnull id<MTLTexture>)texture
           window:(simd_float4)window
            alpha:(float)alpha
         viewport:(simd_float2)viewportPx
      withEncoder:(nonnull id<MTLRenderCommandEncoder>)encoder;

@end

#endif /* WryRenderContext_h */
