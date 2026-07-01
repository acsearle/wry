//
//  WryBackdrop.h
//  client
//
//  Created by Antony Searle on 2026-07-01.
//

#ifndef WryBackdrop_h
#define WryBackdrop_h

#import <Metal/Metal.h>

#import "WryRenderContext.h"

#include <simd/simd.h>

// A full-screen photo backdrop, drawn into a render pass the caller owns (via
// WryRenderContext -drawImage:).  The scene owns the offscreen target + render
// pass; WryBackdrop just issues the drawImage calls into the encoder.  Two
// flavours share the same draw plumbing and timeline:
//
//   * ALBUM (main menu): an async, streaming album of "<prefix>*.png".  Images
//     load on demand off the main thread (MTKTextureLoader) as the timeline
//     reaches them; the current image pans (Ken Burns) and the next dissolves
//     in over it, cyclically.
//
//   * SINGLE IMAGE (splash): one synchronously-supplied texture, static cover,
//     fading in over `fadeInSeconds` then holding.  The degenerate one-slide
//     case -- the caller loads the image itself (splash stays synchronous).
@interface WryBackdrop : NSObject

- (nonnull instancetype)initAlbumWithContext:(nonnull WryRenderContext*)context
                                      prefix:(nonnull NSString*)prefix;

- (nonnull instancetype)initSingleImageWithContext:(nonnull WryRenderContext*)context
                                             image:(nullable id<MTLTexture>)image
                                     fadeInSeconds:(double)fadeInSeconds;

// Advance the backdrop's own timeline (pan / crossfade / fade-in are derived
// from it in -encodeInto:viewport:).
- (void)updateWithDelta:(double)dtSeconds;

- (void)encodeInto:(nonnull id<MTLRenderCommandEncoder>)encoder
          viewport:(simd_float2)viewportPx;

@end

#endif /* WryBackdrop_h */
