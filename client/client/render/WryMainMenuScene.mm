//
//  WryMainMenuScene.mm
//  client
//
//  Created by Antony Searle on 2026-06-25.
//

// Shared Metal header first for no contamination
#include "ShaderTypes.h"

#import <Metal/Metal.h>

#import "WryMainMenuScene.h"
#import "WryBackdrop.h"

#include "gui_event.hpp"
#include "gui_widget.hpp"
#include "save.hpp"

@implementation WryMainMenuScene
{
    WryRenderContext* _ctx;
    wry::GuiContext* _gui;         // host-owned; the WorldStates we build borrow it
    id<MTLTexture> _target;
    simd_float2 _viewportPx;       // drawable pixels; widgets lay out in this
    uint64_t _frameCount;
    std::unique_ptr<wry::gui::Widget> _root;   // the button column
    id<WryScene> _pendingNext;     // set by NEW / LOAD; -nextScene returns it
    // Wraps a freshly-built WorldState in the world scene.
    id<WryScene> (^_nextFactory)(std::shared_ptr<wry::WorldState>);
    void (^_quit)(void);

    // Backdrop: streaming "mainMenu*.png" album (Ken Burns pan + crossfade),
    // owned as a reusable component (see WryBackdrop).
    WryBackdrop* _backdrop;
}

- (nonnull instancetype)initWithContext:(nonnull WryRenderContext*)context
                                    gui:(nonnull wry::GuiContext*)gui
                                   next:(nonnull id<WryScene> (^)(std::shared_ptr<wry::WorldState>))nextFactory
                                   quit:(nonnull void (^)(void))quit
{
    if ((self = [super init])) {
        _ctx = context;
        _gui = gui;
        _nextFactory = [nextFactory copy];
        _quit = [quit copy];
        _viewportPx = simd_make_float2(0.0f, 0.0f);
        _frameCount = 0;

        _backdrop = [[WryBackdrop alloc] initAlbumWithContext:context prefix:@"mainMenu"];

        using namespace wry::gui;

        // Unretained: the widget tree is owned by this scene and is only
        // dispatched to while the scene is alive, so the click lambdas never
        // outlive self.  An unretained capture avoids a retain cycle
        // (self -> _root -> button -> lambda -> self).
        __unsafe_unretained WryMainMenuScene* uself = self;

        auto col = std::make_unique<Column>();
        col->set_spacing(12.0f);

        // NEW GAME: fresh starting world, then drop into it.
        col->add(std::make_unique<Button>("NEW GAME", [uself] {
            // Fresh WorldState (its constructor installs the starting world).
            auto world = std::make_shared<wry::WorldState>(*uself->_gui);
            uself->_pendingNext = uself->_nextFactory ? uself->_nextFactory(world) : nil;
        }));

        // LOAD GAME: for now load the most recent save (enumerate_games returns
        // (filename, id) newest first); a real save picker is the eventual UX.
        col->add(std::make_unique<Button>("LOAD GAME", [uself] {
            auto games = wry::enumerate_games();
            if (!games.empty()) {
                auto world = std::make_shared<wry::WorldState>(*uself->_gui);
                world->load_from_save(games.front().second);
                uself->_pendingNext = uself->_nextFactory ? uself->_nextFactory(world) : nil;
            }
        }));

        // JOIN GAME: multiplayer is a future feature (deterministic lockstep);
        // placeholder no-op for now.
        col->add(std::make_unique<Button>("JOIN GAME", [] { /* TODO: multiplayer */ }));

        // QUIT TO DESKTOP: ask the host to end the run loop (graceful shutdown).
        col->add(std::make_unique<Button>("QUIT TO DESKTOP", [uself] {
            if (uself->_quit) uself->_quit();
        }));

        _root = std::move(col);
    }
    return self;
}

- (void)handleEventsWithViewSize:(CGSize)viewSizePoints {
    // Promote event locations from logical points to drawable pixels (the
    // space the widget tree is laid out in), mirroring gui::pump.
    float wpt = viewSizePoints.width  > 0.0 ? (float)viewSizePoints.width  : 1.0f;
    float hpt = viewSizePoints.height > 0.0 ? (float)viewSizePoints.height : 1.0f;
    float sx = _viewportPx.x / wpt;
    float sy = _viewportPx.y / hpt;
    while (!_gui->events.empty()) {
        wry::gui::Event e = _gui->events.pop_front();
        e.location.x *= sx;
        e.location.y *= sy;
        _root->on_event(e);
    }
}

- (void)update:(double)dtSeconds {
    ++_frameCount;
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
    _target.label = @"Main menu target";
}

- (nullable id<MTLTexture>)encodeIntoCommandBuffer:(nonnull id<MTLCommandBuffer>)commandBuffer {
    using namespace wry::gui;
    if (!_target)
        return nil;

    MTLRenderPassDescriptor* rp = [MTLRenderPassDescriptor new];
    rp.colorAttachments[0].texture = _target;
    rp.colorAttachments[0].loadAction = MTLLoadActionClear;
    rp.colorAttachments[0].storeAction = MTLStoreActionStore;
    rp.colorAttachments[0].clearColor = MTLClearColorMake(0.0, 0.0, 0.0, 1.0);
    id<MTLRenderCommandEncoder> enc = [commandBuffer renderCommandEncoderWithDescriptor:rp];
    enc.label = @"Main menu";

    // Backdrop: streaming, panning, crossfading photo album (WryBackdrop).
    [_backdrop encodeInto:enc viewport:_viewportPx];

    // --- Foreground: the button column ----------------------------------
    [enc setRenderPipelineState:_ctx.uiRenderPipelineState];

    // Screen-space transform: pixel coords -> NDC, y-down.  Same shape as the
    // world overlay's screen-space block.
    MyUniforms uniforms;
    uniforms.position_transform = matrix_float4x4{{
        {2.0f / _viewportPx.x, 0.0f, 0.0f},
        {0.0f, -2.0f / _viewportPx.y, 0.0f, 0.0f},
        { 0.0f, 0.0f, 1.0f, 0.0f },
        {-1.0f, +1.0f, 0.0f, 1.0f},
    }};
    [enc setVertexBytes:&uniforms length:sizeof(uniforms) atIndex:AAPLBufferIndexUniforms];

    // Lay the button column out centered on the viewport and paint it through
    // the shared atlas; commit flushes the accumulated sprites to the encoder.
    Painter painter;
    painter.atlas = _ctx.atlas;
    painter.font = _ctx.font;
    painter.viewport_size_px = _viewportPx;
    painter.frame_count = _frameCount;
    painter.white_sprite = _ctx.atlas->_white;
    painter.clip = wry::rect<float>{0.0f, 0.0f, _viewportPx.x, _viewportPx.y};

    MeasureContext mctx{ _ctx.font };
    SizeConstraints c = SizeConstraints::loose(_viewportPx);
    // Qualify Size: <MacTypes.h> defines a global ::Size that the function-scope
    // `using namespace wry::gui` would otherwise make ambiguous.
    wry::gui::Size desired = _root->measure(c, mctx);
    float x = (_viewportPx.x - desired.w) * 0.5f;
    float y = (_viewportPx.y - desired.h) * 0.5f;
    _root->arrange(wry::rect<float>{x, y, x + desired.w, y + desired.h});

    // Scrim behind the column so the buttons stay legible over the photo.
    // Pushed before the buttons, so it sits under them in the atlas draw order.
    const float pad = 28.0f;
    painter.fill_rect(wry::rect<float>{x - pad, y - pad,
                                       x + desired.w + pad, y + desired.h + pad},
                      wry::RGBA8Unorm_sRGB(0.0f, 0.0f, 0.0f, 0.5f));

    _root->paint(painter);

    _ctx.atlas->commit((__bridge void*)enc);
    [enc endEncoding];
    return _target;
}

- (id<WryScene>)nextScene {
    return _pendingNext;
}

@end
