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

#include <algorithm>

#include "gui_event.hpp"
#include "gui_widget.hpp"
#include "save.hpp"
#include "scene_image.hpp"

namespace {

// Backdrop "photo album" timing, in seconds.
constexpr double kSlideSeconds     = 8.0;    // one image leads before the next
constexpr double kCrossfadeSeconds = 2.5;    // dissolve overlap
constexpr float  kZoom             = 0.80f;  // <1 leaves room to pan
// Keep the pan window off the image edges by this much (texCoord fraction), so
// a pan never reveals the photo's border even mid-crossfade.
constexpr float  kPanMargin        = 0.15f;

// splitmix64-style hash -> [0,1).  Deterministic per (slide, channel), so each
// slide gets its own pan path without any persistent RNG state.
float hash01(uint64_t x) {
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ull;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebull;
    x =  x ^ (x >> 31);
    return (float)(x & 0xFFFFFFull) / (float)0x1000000;
}

// Pan window for `slide` at parameter `t` in [0,1].  Start and end points are
// hashed from the slide index (within the safe interior [margin, 1-margin]),
// so successive slides pan differently and the window never touches a border.
simd_float4 menu_pan_window(id<MTLTexture> img, simd_float2 vp,
                            uint64_t slide, float t) {
    const float lo = kPanMargin, hi = 1.0f - kPanMargin;
    auto pt = [&](int which) { return lo + (hi - lo) * hash01(slide * 4 + which); };
    const float sx0 = pt(0), sy0 = pt(1), sx1 = pt(2), sy1 = pt(3);
    const float sx = sx0 + (sx1 - sx0) * t;
    const float sy = sy0 + (sy1 - sy0) * t;
    return wry::image_pan_window((float)img.width, (float)img.height,
                                 vp.x, vp.y, kZoom, sx, sy);
}

} // namespace

@implementation WryMainMenuScene
{
    WryRenderContext* _ctx;
    std::shared_ptr<wry::WorldState> _model;
    id<MTLTexture> _target;
    simd_float2 _viewportPx;       // drawable pixels; widgets lay out in this
    uint64_t _frameCount;
    std::unique_ptr<wry::gui::Widget> _root;   // the button column
    id<WryScene> _pendingNext;     // set by NEW / LOAD; -nextScene returns it
    id<WryScene> (^_nextFactory)(void);
    void (^_quit)(void);

    // Backdrop photo-album state: elapsed seconds drive a continuous pan with
    // an overlapping crossfade (see -update / -encode...).
    NSArray<id<MTLTexture>>* _images;
    double _albumSeconds;
}

- (nonnull instancetype)initWithContext:(nonnull WryRenderContext*)context
                                  model:(std::shared_ptr<wry::WorldState>)model
                                   next:(nonnull id<WryScene> (^)(void))nextFactory
                                   quit:(nonnull void (^)(void))quit
{
    if ((self = [super init])) {
        _ctx = context;
        _model = model;
        _nextFactory = [nextFactory copy];
        _quit = [quit copy];
        _viewportPx = simd_make_float2(0.0f, 0.0f);
        _frameCount = 0;

        // Backdrop images (every mainMenu*.png), and the pan/crossfade state.
        _images = [context loadTexturesWithPrefix:@"mainMenu" ofType:@"png"];
        _albumSeconds = 0.0;

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
            uself->_model->new_game();
            uself->_pendingNext = uself->_nextFactory ? uself->_nextFactory() : nil;
        }));

        // LOAD GAME: for now load the most recent save (enumerate_games returns
        // (filename, id) newest first); a real save picker is the eventual UX.
        col->add(std::make_unique<Button>("LOAD GAME", [uself] {
            auto games = wry::enumerate_games();
            if (!games.empty()) {
                uself->_model->load_from_save(games.front().second);
                uself->_pendingNext = uself->_nextFactory ? uself->_nextFactory() : nil;
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
    while (!_model->_gui.events.empty()) {
        wry::gui::Event e = _model->_gui.events.pop_front();
        e.location.x *= sx;
        e.location.y *= sy;
        _root->on_event(e);
    }
}

- (void)update:(double)dtSeconds {
    ++_frameCount;
    // Continuous timeline; the pan / crossfade are derived from it in -encode.
    if (_images.count > 0)
        _albumSeconds += dtSeconds;
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

    // --- Backdrop: continuously panning, crossfading photo album --------
    // A single timeline (_albumSeconds) drives everything.  Each "slide" lasts
    // kSlideSeconds; the current image leads at full alpha while panning, and
    // for the last kCrossfadeSeconds the next image dissolves in OVER it while
    // both keep panning -- so no stop-fade-start.  The pan params stay off the
    // borders (kPanMargin), so neither image ever has to freeze.
    const NSUInteger n = _images.count;
    if (n > 0) {
        const double S = kSlideSeconds;
        const double X = kCrossfadeSeconds;
        const uint64_t slide = (uint64_t)(_albumSeconds / S);
        const double lt = _albumSeconds - (double)slide * S;   // seconds into slide

        // Current image: visible since X seconds before its slide began, so it
        // pans continuously across its whole [fade-in, lead, fade-out] span.
        id<MTLTexture> cur = _images[(NSUInteger)(slide % n)];
        const float ti = (float)std::min(1.0, (lt + X) / (S + X));
        [_ctx drawImage:cur
                 window:menu_pan_window(cur, _viewportPx, slide, ti)
                  alpha:1.0f
               viewport:_viewportPx
            withEncoder:enc];

        // During the tail of the slide, the next image dissolves in over the
        // current one, panning from the start of its own (continuing) path.
        if (n > 1 && lt >= S - X) {
            const uint64_t nslide = slide + 1;
            id<MTLTexture> next = _images[(NSUInteger)(nslide % n)];
            const float ct = (float)((lt - (S - X)) / X);          // fade-in
            const float tj = (float)((lt - (S - X)) / (S + X));    // its pan
            [_ctx drawImage:next
                     window:menu_pan_window(next, _viewportPx, nslide, tj)
                      alpha:ct
                   viewport:_viewportPx
                withEncoder:enc];
        }
    }

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
