//
//  WryRenderContext.h
//  client
//
//  Created by Antony Searle on 2026-06-24.
//

#ifndef WryRenderContext_h
#define WryRenderContext_h

#import <Metal/Metal.h>

#include "asset_preload.hpp"
#include "font.hpp"
#include "SpriteAtlas.hpp"
#import "WryBufferArena.h"

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

// Per-frame transient GPU memory; the host closes each frame with
// -endFrameOnCommandBuffer: on the frame's command buffer.
@property (nonatomic, readonly, nonnull) WryBufferArena* arena;

@property (nonatomic, readonly, nonnull) wry::SpriteAtlas* atlas;
@property (nonatomic, readonly, nonnull) wry::Font* font;

// Environment maps already prefiltered this process, by resource name and
// kind ("day/specular", "day/irradiance"): the world scene's lighting
// prefilter costs ~250 ms of GPU per map (docs/async_loading.md), and a
// new scene for every New Game would otherwise redo it.  Main thread only.
@property (nonatomic, readonly, nonnull) NSMutableDictionary<NSString*, id<MTLTexture>>* environmentCache;

// Metal objects made from resource files, kept for the process and shared
// by every scene that asks again (a second New Game), by a key naming the
// file, its modification time and how it was made (-assetKeyForResource:
// ofType:recipe:).  Only objects nobody writes after they are made go in:
// the mipmapped textures below, the shared textures, and what scenes put
// here themselves (the world scene's mesh buffers and finished symbol
// atlases).  Holding them costs their memory once the scene is gone
// (docs/async_loading.md).  Main thread only.
@property (nonatomic, readonly, nonnull) NSMutableDictionary<NSString*, id>* assetCache;

// "NAME.EXT|<mtime>|recipe": a file changed on disk misses
- (nonnull NSString*)assetKeyForResource:(nonnull NSString*)name
                                  ofType:(nonnull NSString*)ext
                                  recipe:(nonnull NSString*)recipe;

// Bytes the GPU holds for assetCache and environmentCache
- (NSUInteger)cachedAssetBytes;

// Rasterize the font afresh for a UI scale (GuiContext::pixels_per_point)
// if it was built for another; the Font is replaced in place, so the
// pointer stays valid.  The host calls this each frame before the scenes
// paint.  The font starts at the hosts' default, 2.
- (void)setFontPixelsPerPoint:(float)pixelsPerPoint;
@property (nonatomic, readonly, nonnull) id<MTLRenderPipelineState> overlayRenderPipelineState;

// GUI quad pipelines for SpriteAtlas::commit (quadVertexShader /
// quadFragmentShader over WryQuadInstance).  The overlay variant runs inside
// the world's deferred G-buffer pass (same attachments as
// overlayRenderPipelineState); the ui variant is for scenes that draw
// directly into their own single RGBA16Float color target (menu): one color
// attachment, alpha blend, no depth.
@property (nonatomic, readonly, nonnull) id<MTLRenderPipelineState> overlayQuadRenderPipelineState;
@property (nonatomic, readonly, nonnull) id<MTLRenderPipelineState> uiQuadRenderPipelineState;

// Returned by reference: callers read the cubic-bezier / glyph tables in
// place rather than copying the (vector-backed) Font2 each access.
- (wry::Font2&)font2;

// ---- Asset preload (render/asset_preload.hpp) --------------------------
//
// nil until the host starts it.  Once started, the newTextureFromResource
// family takes a decode it requested (same path, same premultiplication)
// instead of decoding again; scenes take preloaded meshes and fields from
// it directly.  Main thread only, like the rest of this object.
- (void)startAssetPreloadWithScope:(nonnull wry::coroutine::Nursery*)scope;
- (nullable wry::AssetPreload*)assetPreload;

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

// A single-level texture, premultiplied, made once per process and shared
// (assetCache): the caller must never write it.
- (nonnull id<MTLTexture>)sharedTextureFromResource:(nonnull NSString*)name
                                             ofType:(nonnull NSString*)ext
                                    withPixelFormat:(MTLPixelFormat)pixelFormat;

// Opt-in mipmapped variant for textures that are NOT CPU-mutated after
// load (mesh materials).  Allocates the full mip chain and fills it with
// a blit generateMipmaps, blocking until complete.  Made once per process
// and shared (assetCache): the caller must never write it; the same holds
// for every newMipmappedTextureFromResource variant.  The plain
// newTextureFromResource stays single-level because some callers mutate
// the texture bytes after loading.
- (nonnull id<MTLTexture>)newMipmappedTextureFromResource:(nonnull NSString*)name
                                                   ofType:(nonnull NSString*)ext
                                          withPixelFormat:(MTLPixelFormat)pixelFormat;

// For textures whose level 0 the caller edits on the CPU after loading
// (the symbol atlas stamps labels into it): allocates the full mip chain
// but fills only level 0.  Sampling the other levels is undefined until
// the caller has finished editing and called -generateMipmapsForTexture:.
- (nonnull id<MTLTexture>)newTextureFromResource:(nonnull NSString*)name
                                          ofType:(nonnull NSString*)ext
                                 withPixelFormat:(MTLPixelFormat)pixelFormat
                                       mipmapped:(BOOL)mipmapped
                                   premultiplied:(BOOL)premultiplied;
- (nonnull id<MTLTexture>)newTextureWithMipStorageFromResource:(nonnull NSString*)name
                                                        ofType:(nonnull NSString*)ext;

// The texture those make, from an image already decoded (as it is, no
// premultiplication): level 0 filled, the rest of the chain (if
// mipmapped) left for -generateMipmapsForTexture...
- (nonnull id<MTLTexture>)newTextureWithImage:(wry::matrix<wry::RGBA8Unorm_sRGB> const&)image
                                  pixelFormat:(MTLPixelFormat)pixelFormat
                                    mipmapped:(BOOL)mipmapped
                                        label:(nonnull NSString*)label;

// Fill levels 1... of `texture` from level 0 with a blit generateMipmaps,
// blocking until complete.  Call again after any later level-0 edit.  On
// Apple GPUs this is a 2x2 box filter in linear light (sRGB formats are
// decoded first): correctly phased, and for an atlas of power-of-two cells
// on a power-of-two grid, free of bleeding between cells down to one texel
// per cell.  Apple documents the filter only as implementation-dependent;
// measured on M1 Max, 2026-10-11.
- (void)generateMipmapsForTexture:(nonnull id<MTLTexture>)texture;

// Fill levels 1... of `texture` from level 0 with mip::downsample_kernel
// (Shaders.metal), blocking until complete.  Each level is the one above
// it filtered by the binomial [1 3 3 1] / 8 along each axis (a near-
// Gaussian of sigma 0.87 source texels = 0.43 destination texels, about
// 0.5 destination texels compounded down the chain; a triple zero at the
// source's Nyquist frequency, where the 2x2 box [1 1] / 2 has a single
// zero and leaves visible aliasing) and decimated by 2, in linear light.  `cell` > 0 names the power-of-two cell size, in
// level-0 texels, of an atlas on a regular grid: every tap is clamped to
// the destination texel's cell, so no level mixes neighbouring cells
// while a cell is at least one texel (below that the cells merge, as with
// any filter).  `cell` == 0 treats the whole texture as one cell
// (clamp-to-edge).  `straightAlpha`: the texture is straight alpha, and
// colour is averaged weighted by alpha + 1/256: premultiplied filtering
// where there is coverage (no dark fringe from the black rgb of empty
// texels), the plain average where there is none (keeps the colour the
// atlas margins carry).  Falls back to -generateMipmapsForTexture: unless
// the texture is RGBA8Unorm(_sRGB), power-of-two on both axes and has
// MTLTextureUsageShaderWrite (the loaders above set it on mipmapped
// textures).
- (void)generateMipmapsForTexture:(nonnull id<MTLTexture>)texture
                         cellSize:(NSUInteger)cell
                    straightAlpha:(BOOL)straightAlpha;

// -newMipmappedTextureFromResource:ofType:withPixelFormat: with the mips
// from -generateMipmapsForTexture:cellSize:straightAlpha: (premultiplied,
// as that loader leaves it)
- (nonnull id<MTLTexture>)newMipmappedTextureFromResource:(nonnull NSString*)name
                                                   ofType:(nonnull NSString*)ext
                                          withPixelFormat:(MTLPixelFormat)pixelFormat
                                                 cellSize:(NSUInteger)cell;

// ---- Scene backdrop helpers --------------------------------------------

// Enumerate the file stems of every "<prefix>*.<ext>" in the resource
// directory, sorted by filename -- without loading anything.  Lets a scene
// stream its backdrop images in on demand rather than decoding the whole
// album up front.
- (nonnull NSArray<NSString*>*)textureStemsWithPrefix:(nonnull NSString*)prefix
                                               ofType:(nonnull NSString*)ext;

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
