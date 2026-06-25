//
//  WryMainMenuScene.mm
//  client
//
//  Created by Antony Searle on 2026-06-25.
//

#import <Metal/Metal.h>

#import "WryMainMenuScene.h"

#include "gui_event.hpp"

@implementation WryMainMenuScene
{
    WryRenderContext* _ctx;
    std::shared_ptr<wry::model> _model;
    id<MTLTexture> _target;
    BOOL _startRequested;
    id<WryScene> (^_nextFactory)(void);
    id<WryScene> _next;
}

- (nonnull instancetype)initWithContext:(nonnull WryRenderContext*)context
                                  model:(std::shared_ptr<wry::model>)model
                                   next:(nonnull id<WryScene> (^)(void))nextFactory
{
    if ((self = [super init])) {
        _ctx = context;
        _model = model;
        _nextFactory = [nextFactory copy];
        _startRequested = NO;
    }
    return self;
}

// While the menu is showing it owns input: the host calls this instead of the
// world pump.  Drain the model's event queue and start the game on the first
// mouse-down or key-down.  (A real menu will route these to buttons; for now
// any press starts the game.)
- (void)handleEvents {
    while (!_model->_events.empty()) {
        wry::gui::Event e = _model->_events.pop_front();
        if (e.kind == WryEventKindMouseDown || e.kind == WryEventKindKeyDown)
            _startRequested = YES;
    }
}

- (void)update {
}

- (void)drawableResize:(CGSize)size {
    if (size.width < 1 || size.height < 1)
        return;
    MTLTextureDescriptor* d = [MTLTextureDescriptor new];
    d.textureType = MTLTextureType2D;
    d.pixelFormat = MTLPixelFormatRGBA16Float;
    d.width = (NSUInteger)size.width;
    d.height = (NSUInteger)size.height;
    d.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
    d.storageMode = MTLStorageModePrivate;
    _target = [_ctx.device newTextureWithDescriptor:d];
    _target.label = @"Main menu target";
}

- (nullable id<MTLTexture>)encodeIntoCommandBuffer:(nonnull id<MTLCommandBuffer>)commandBuffer {
    if (!_target)
        return nil;
    MTLRenderPassDescriptor* rp = [MTLRenderPassDescriptor new];
    rp.colorAttachments[0].texture = _target;
    rp.colorAttachments[0].loadAction = MTLLoadActionClear;
    rp.colorAttachments[0].storeAction = MTLStoreActionStore;
    rp.colorAttachments[0].clearColor = MTLClearColorMake(0.08, 0.10, 0.09, 1.0);
    id<MTLRenderCommandEncoder> enc = [commandBuffer renderCommandEncoderWithDescriptor:rp];
    enc.label = @"Main menu clear";
    [enc endEncoding];
    return _target;
}

- (id<WryScene>)nextScene {
    if (!_startRequested)
        return nil;
    if (!_next && _nextFactory)
        _next = _nextFactory();
    return _next;
}

@end
