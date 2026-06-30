//
//  WryTextureLoader.h
//  client
//
//  Created by Antony Searle on 2026-06-30.
//

#ifndef WryTextureLoader_h
#define WryTextureLoader_h

#import <MetalKit/MetalKit.h>

#include "coroutine.hpp"

namespace wry {

    // Asynchronously load a texture from `url` via MTKTextureLoader, which
    // decodes the image file (PNG/JPEG/...) and uploads it -- plus optional
    // mipmaps -- on its own libdispatch threads, off the GC mutator pool.  The
    // returned Future resolves on MTK's completion thread; co_await it (or
    // Nursery::fork it into a join target).  `loader` should be created once per
    // device ([[MTKTextureLoader alloc] initWithDevice:device]) and reused.  On
    // failure the result is `nil` and the error is logged.
    //
    // This is the Metal backend of an "async file -> GPU texture" capability;
    // a DirectX/Vulkan backend would implement the same Future-returning shape.
    Coroutine::Future<id<MTLTexture>>
    load_texture(MTKTextureLoader* loader,
                 NSURL* url,
                 NSDictionary<MTKTextureLoaderOption, id>* options);

}

#endif /* WryTextureLoader_h */
