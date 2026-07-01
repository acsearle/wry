//
//  WrySplashScene.mm
//  client
//
//  Created by Antony Searle on 2026-06-25.
//

#import <Metal/Metal.h>

#import "WrySplashScene.h"
#import "WryBackdrop.h"

@implementation WrySplashScene
{
    WryRenderContext* _ctx;
    id<MTLTexture> _target;        // offscreen we hand to the host
    WryBackdrop* _backdrop;        // single splash image, cover + fade-in
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
        // Minimal asset, loaded synchronously so the splash shows ASAP: the
        // first splash*.png in the resource folder, drawn as a single cover
        // image fading in over the first ~45% of the splash.
        id<MTLTexture> image =
            [context loadTexturesWithPrefix:@"splash" ofType:@"png"].firstObject;
        _backdrop = [[WryBackdrop alloc] initSingleImageWithContext:context
                                                             image:image
                                                     fadeInSeconds:durationSeconds * 0.45];
    }
    return self;
}

- (void)update:(double)dtSeconds {
    _elapsedSeconds += dtSeconds;
    [_backdrop updateWithDelta:dtSeconds];
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

    [_backdrop encodeInto:enc viewport:_viewportPx];

    [enc endEncoding];
    return _target;
}

- (id<WryScene>)nextScene {
    return (_elapsedSeconds >= _durationSeconds) ? _next : nil;
}

@end
