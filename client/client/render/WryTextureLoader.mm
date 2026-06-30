//
//  WryTextureLoader.mm
//  client
//
//  Created by Antony Searle on 2026-06-30.
//

#import "WryTextureLoader.h"

#include <cstdio>

namespace wry {

    namespace {

        // Bridges MTKTextureLoader's async completion into a coroutine resume.
        // The awaitable lives in load_texture's coroutine frame across the
        // suspend; the completion block (retained by MTK until it fires) writes
        // the result through `this` and resumes.  resume() must be the block's
        // LAST touch of `this` / `handle`: resuming runs the continuation, which
        // can co_return and destroy the frame (and this awaitable with it).
        struct AwaitMTKTexture {
            MTKTextureLoader* _loader;
            NSURL* _url;
            NSDictionary<MTKTextureLoaderOption, id>* _options;
            id<MTLTexture> _result;

            bool await_ready() const noexcept { return false; }

            void await_suspend(std::coroutine_handle<> handle) noexcept {
                [_loader newTextureWithContentsOfURL:_url
                                             options:_options
                                   completionHandler:^(id<MTLTexture> texture,
                                                       NSError* error) {
                    if (!texture)
                        fprintf(stderr, "load_texture: %s\n",
                                error.localizedDescription.UTF8String);
                    _result = texture;
                    handle.resume();   // last touch of `this` / `handle`
                }];
            }

            id<MTLTexture> await_resume() const noexcept { return _result; }
        };

    } // namespace

    Coroutine::Future<id<MTLTexture>>
    load_texture(MTKTextureLoader* loader,
                 NSURL* url,
                 NSDictionary<MTKTextureLoaderOption, id>* options) {
        co_return co_await AwaitMTKTexture{loader, url, options, nil};
    }

}
