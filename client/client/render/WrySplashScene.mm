//
//  WrySplashScene.mm
//  client
//
//  Created by Antony Searle on 2026-06-25.
//

#import <Metal/Metal.h>

#import "WrySplashScene.h"

#include <algorithm>

#include "scene_image.hpp"

@implementation WrySplashScene
{
    WryRenderContext* _ctx;
    id<MTLTexture> _target;        // offscreen we hand to the host
    id<MTLTexture> _image;         // the splash image (nil if none found)
    simd_float2 _viewportPx;
    double _elapsedSeconds;
    double _durationSeconds;
    id<WryScene> _next;
}

- (nonnull instancetype)initWithContext:(nonnull WryRenderContext*)context
                        durationSeconds:(double)durationSeconds
                                   next:(nonnull id<WryScene>)next
{
    if ((self = [super init])) {
        _ctx = context;
        _durationSeconds = durationSeconds;
        _next = next;
        _elapsedSeconds = 0.0;
        _viewportPx = simd_make_float2(0.0f, 0.0f);
        // Minimal asset: the first splash*.png in the resource folder.
        _image = [context loadTexturesWithPrefix:@"splash" ofType:@"png"].firstObject;
    }
    return self;
}

- (void)update:(double)dtSeconds {
    _elapsedSeconds += dtSeconds;
}

- (void)drawableResize:(CGSize)size {
    if (size.width < 1 || size.height < 1)
        return;
    _viewportPx = simd_make_float2((float)size.width, (float)size.height);
    MTLTextureDescriptor* d = [MTLTextureDescriptor new];
    d.textureType = MTLTextureType2D;
    d.pixelFormat = MTLPixelFormatRGBA16Float;
    d.width = (NSUInteger)size.width;
    d.height = (NSUInteger)size.height;
    d.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
    d.storageMode = MTLStorageModePrivate;
    _target = [_ctx.device newTextureWithDescriptor:d];
    _target.label = @"Splash target";
}

- (nullable id<MTLTexture>)encodeIntoCommandBuffer:(nonnull id<MTLCommandBuffer>)commandBuffer {
    if (!_target)
        return nil;  // not sized yet; present nothing this frame
    MTLRenderPassDescriptor* rp = [MTLRenderPassDescriptor new];
    rp.colorAttachments[0].texture = _target;
    rp.colorAttachments[0].loadAction = MTLLoadActionClear;
    rp.colorAttachments[0].storeAction = MTLStoreActionStore;
    rp.colorAttachments[0].clearColor = MTLClearColorMake(0.0, 0.0, 0.0, 1.0);
    id<MTLRenderCommandEncoder> enc = [commandBuffer renderCommandEncoderWithDescriptor:rp];
    enc.label = @"Splash";

    if (_image) {
        // Fade in over the first ~45% of the splash, then hold.
        const double fadeSeconds = std::max(0.001, _durationSeconds * 0.45);
        const float alpha = (float)std::min(1.0, _elapsedSeconds / fadeSeconds);
        const simd_float4 window = wry::image_cover_window((float)_image.width,
                                                           (float)_image.height,
                                                           _viewportPx.x, _viewportPx.y);
        [_ctx drawImage:_image
                 window:window
                  alpha:alpha
               viewport:_viewportPx
            withEncoder:enc];
    }

    [enc endEncoding];
    return _target;
}

- (id<WryScene>)nextScene {
    return (_elapsedSeconds >= _durationSeconds) ? _next : nil;
}

@end
