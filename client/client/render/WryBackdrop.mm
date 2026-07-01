//
//  WryBackdrop.mm
//  client
//
//  Created by Antony Searle on 2026-07-01.
//

#import <Metal/Metal.h>

#import "WryBackdrop.h"
#import "WryTextureLoader.h"

#include <algorithm>
#include <deque>
#include <memory>

#include "platform.hpp"
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

// One backdrop image's async-load slot.  The loader (on an MTK/dispatch
// thread) fills `texture` then publishes via `ready` (store_release); the
// render thread reads `ready` (load_acquire) and, once set, draws `texture`.
// `in_flight` is render-thread-only and stops the same load being re-kicked.
struct LoadSlot {
    wry::Atomic<bool> ready{false};
    id<MTLTexture> texture = nil;   // published via `ready`
    bool in_flight = false;         // render-thread only
};

// Wrap the generic load_texture in a task that deposits into `slot`.  It takes
// the slot by shared_ptr, so the slot (and its backing deque) stay alive until
// the load completes -- even if the backdrop is torn down first, the result
// just lands in an orphaned slot and is dropped.  No drain, no cancellation.
// wait_group_spawn'd, which both anchors it for process shutdown and supplies
// the continuation a Task requires.
wry::Coroutine::Task load_into_slot(std::shared_ptr<LoadSlot> slot,
                                    MTKTextureLoader* loader,
                                    NSURL* url,
                                    NSDictionary<MTKTextureLoaderOption, id>* options) {
    id<MTLTexture> texture = co_await wry::load_texture(loader, url, options);
    slot->texture = texture;                 // published by the release below
    slot->ready.store_release(true);
}

} // namespace

@implementation WryBackdrop
{
    WryRenderContext* _ctx;
    double _seconds;
    BOOL _isAlbum;

    // Album mode: streaming album of <prefix>*.png.
    NSArray<NSString*>* _stems;
    std::shared_ptr<std::deque<LoadSlot>> _slots;   // one per stem; async-filled
    MTKTextureLoader* _loader;                      // reused across loads
    NSDictionary<MTKTextureLoaderOption, id>* _loaderOptions;

    // Single-image mode: one synchronously-supplied texture, cover + fade-in.
    id<MTLTexture> _single;
    double _fadeInSeconds;
}

- (instancetype)initAlbumWithContext:(WryRenderContext*)context
                              prefix:(NSString*)prefix
{
    if ((self = [super init])) {
        _ctx = context;
        _seconds = 0.0;
        _isAlbum = YES;
        // Enumerate stems now (no decode); each image loads on demand via
        // MTKTextureLoader as -imageForIndex: reaches it, off the main thread.
        // One slot per stem, in a shared_ptr deque so an in-flight load can
        // outlive this backdrop.
        _stems = [context textureStemsWithPrefix:prefix ofType:@"png"];
        _slots = std::make_shared<std::deque<LoadSlot>>(_stems.count);
        _loader = [[MTKTextureLoader alloc] initWithDevice:context.device];
        _loaderOptions = @{
            MTKTextureLoaderOptionSRGB:               @YES,
            MTKTextureLoaderOptionTextureUsage:       @(MTLTextureUsageShaderRead),
            MTKTextureLoaderOptionTextureStorageMode: @(MTLStorageModePrivate),
        };
    }
    return self;
}

- (instancetype)initSingleImageWithContext:(WryRenderContext*)context
                                     image:(id<MTLTexture>)image
                             fadeInSeconds:(double)fadeInSeconds
{
    if ((self = [super init])) {
        _ctx = context;
        _seconds = 0.0;
        _isAlbum = NO;
        _single = image;
        _fadeInSeconds = fadeInSeconds;
    }
    return self;
}

- (void)updateWithDelta:(double)dtSeconds {
    _seconds += dtSeconds;
}

// Album texture for index `i`, kicking its async load the first time it's asked
// for.  Returns nil until the load lands; the caller skips a nil layer (so the
// backdrop shows whatever is ready and fades the rest in as they arrive)
// instead of blocking.  Render-thread only.
- (nullable id<MTLTexture>)imageForIndex:(NSUInteger)i {
    LoadSlot& slot = (*_slots)[i];
    if (slot.ready.load_acquire())
        return slot.texture;
    if (!slot.in_flight) {
        slot.in_flight = true;
        std::filesystem::path p =
            wry::path_for_resource(_stems[i].UTF8String, "png");
        NSURL* url = [NSURL fileURLWithPath:@(p.c_str())];
        // Aliasing shared_ptr: shares ownership of the whole deque while
        // pointing at slot i, so the load can complete safely even if this
        // backdrop is gone by then (the result lands in the orphaned slot).
        std::shared_ptr<LoadSlot> owned{_slots, &slot};
        wry::wait_group_spawn(load_into_slot(std::move(owned), _loader, url,
                                             _loaderOptions));
    }
    return nil;
}

- (void)encodeInto:(id<MTLRenderCommandEncoder>)enc
          viewport:(simd_float2)vp
{
    if (!_isAlbum) {
        // Single image: static cover, fading in over _fadeInSeconds then holding.
        if (_single) {
            const double fade = std::max(0.001, _fadeInSeconds);
            const float alpha = (float)std::min(1.0, _seconds / fade);
            const simd_float4 window = wry::image_cover_window((float)_single.width,
                                                              (float)_single.height,
                                                              vp.x, vp.y);
            [_ctx drawImage:_single
                     window:window
                      alpha:alpha
                   viewport:vp
                withEncoder:enc];
        }
        return;
    }

    // Album: a single timeline (_seconds) drives everything.  Each "slide" lasts
    // kSlideSeconds; the current image leads at full alpha while panning, and for
    // the last kCrossfadeSeconds the next image dissolves in OVER it while both
    // keep panning -- so no stop-fade-start.  The pan params stay off the borders
    // (kPanMargin), so neither image ever has to freeze.
    const NSUInteger n = _stems.count;
    if (n == 0)
        return;

    const double S = kSlideSeconds;
    const double X = kCrossfadeSeconds;
    const uint64_t slide = (uint64_t)(_seconds / S);
    const double lt = _seconds - (double)slide * S;   // seconds into slide

    // Prefetch the next slide so its decode finishes before the crossfade begins.
    if (n > 1)
        (void)[self imageForIndex:(NSUInteger)((slide + 1) % n)];

    // Current image: visible since X seconds before its slide began, so it pans
    // continuously across its whole [fade-in, lead, fade-out] span.  On the first
    // pass over a slide it may still be loading; skip the layer until it lands.
    id<MTLTexture> cur = [self imageForIndex:(NSUInteger)(slide % n)];
    if (cur) {
        const float ti = (float)std::min(1.0, (lt + X) / (S + X));
        [_ctx drawImage:cur
                 window:menu_pan_window(cur, vp, slide, ti)
                  alpha:1.0f
               viewport:vp
            withEncoder:enc];
    }

    // During the tail of the slide, the next image dissolves in over the current
    // one, panning from the start of its own (continuing) path.
    if (n > 1 && lt >= S - X) {
        const uint64_t nslide = slide + 1;
        id<MTLTexture> next = [self imageForIndex:(NSUInteger)(nslide % n)];
        if (next) {
            const float ct = (float)((lt - (S - X)) / X);          // fade-in
            const float tj = (float)((lt - (S - X)) / (S + X));    // its pan
            [_ctx drawImage:next
                     window:menu_pan_window(next, vp, nslide, tj)
                      alpha:ct
                   viewport:vp
                withEncoder:enc];
        }
    }
}

@end
