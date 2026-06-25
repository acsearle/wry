//
//  WrySplashScene.mm
//  client
//
//  Created by Antony Searle on 2026-06-25.
//

#import <Metal/Metal.h>

#import "WrySplashScene.h"

@implementation WrySplashScene
{
    WryRenderContext* _ctx;
    id<MTLTexture> _target;        // offscreen we clear and hand to the host
    uint64_t _elapsedFrames;
    uint64_t _durationFrames;
    id<WryScene> _next;
}

- (nonnull instancetype)initWithContext:(nonnull WryRenderContext*)context
                         durationFrames:(uint64_t)durationFrames
                                   next:(nonnull id<WryScene>)next
{
    if ((self = [super init])) {
        _ctx = context;
        _durationFrames = durationFrames;
        _next = next;
        _elapsedFrames = 0;
    }
    return self;
}

- (void)update {
    ++_elapsedFrames;
}

- (void)drawableResize:(CGSize)size {
    if (size.width < 1 || size.height < 1)
        return;
    // The host blits our target into the (RGBA16Float) drawable, so the target
    // must match the drawable's format and size.
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
    rp.colorAttachments[0].clearColor = MTLClearColorMake(0.05, 0.07, 0.12, 1.0);
    id<MTLRenderCommandEncoder> enc = [commandBuffer renderCommandEncoderWithDescriptor:rp];
    enc.label = @"Splash clear";
    [enc endEncoding];
    return _target;
}

- (id<WryScene>)nextScene {
    return (_elapsedFrames >= _durationFrames) ? _next : nil;
}

@end
