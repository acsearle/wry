//
//  asset_preload.cpp
//  client
//
//  Created by Antony Searle on 2026-11-08.
//

#include "asset_preload.hpp"

#include <cstring>
#include <system_error>

#include "Wavefront.hpp"
#include "load_timing.hpp"
#include "test.hpp"

namespace wry {

    namespace {

        // Owns the slot's publication: whatever `value` holds when this
        // dies is published, nullopt included.  It is the task's
        // parameter, so it lives in the coroutine frame from the call:
        // a task that completes publishes its value as the frame is
        // destroyed after co_return, and one destroyed without ever
        // running (its scope stopped first) publishes nothing, so a
        // waiting take always wakes
        template<typename T>
        struct Publisher {
            std::shared_ptr<AssetPreload::Slot<T>> slot;
            std::optional<T> value;
            explicit Publisher(std::shared_ptr<AssetPreload::Slot<T>> s) : slot(std::move(s)) {}
            Publisher(Publisher&&) = default;
            ~Publisher() {
                if (slot)
                    slot->publish(std::move(value));
            }
        };

        coroutine::Task decode_image(Publisher<matrix<RGBA8Unorm_sRGB>> out, std::string path, bool premultiplied) {
            LoadTimer timer("preload image ", path.c_str());
            // from_png aborts on a missing file: leave that to the
            // synchronous load, where it always happened
            std::error_code ec;
            if (!std::filesystem::exists(path, ec))
                co_return;
            try {
                matrix<RGBA8Unorm_sRGB> image = from_png(StringView(path.c_str()));
                if (premultiplied)
                    multiply_alpha_inplace(image);
                out.value = std::move(image);
            } catch (...) {
                // the synchronous load will report it
            }
            co_return;
        }

        coroutine::Task decode_mesh(Publisher<wry::mesh::mesh> out, std::string path) {
            LoadTimer timer("preload mesh ", path.c_str());
            std::error_code ec;
            if (!std::filesystem::exists(path, ec))
                co_return;
            try {
                wry::mesh::mesh m = from_obj(path);
                m.MeshVertexify();
                out.value = std::move(m);
            } catch (...) {
            }
            co_return;
        }

        // On a cache hit the two parts are independent: the decode as a
        // child, the field (the longer, ~380 ms Debug) here, so that the
        // field starts when this task does (a child joins the back of the
        // pool's queue); the join's acquire of the child's release
        // decrement orders its writes before the reads below, as in
        // build_symbol_sdf_parallel.  On a miss the field is built from
        // the one decode.  Either part empty publishes nothing (the
        // synchronous load reports it)
        coroutine::Task decode_symbol_atlas(Publisher<AssetPreload::SymbolAtlas> out, std::string path,
                                            int in_flight) {
            LoadTimer timer("preload symbol atlas ", path.c_str());
            std::error_code ec;
            if (!std::filesystem::exists(path, ec))
                co_return;
            AssetPreload::SymbolAtlas result;
            if (symbol_sdf_cache_fresh(path)) {
                coroutine::Nursery image;
                image.soon([](matrix<RGBA8Unorm_sRGB>* image, std::string const* path) -> coroutine::Task {
                    LoadTimer timer("preload symbol atlas image ", path->c_str());
                    try {
                        *image = from_png(StringView(path->c_str()));
                    } catch (...) {
                    }
                    co_return;
                }(&result.image, &path));
                {
                    LoadTimer timer("preload symbol field ", path.c_str());
                    try {
                        result.field = co_await symbol_sdf_for_atlas_parallel(path);
                    } catch (...) {
                    }
                }
                [[maybe_unused]] std::ptrdiff_t cancelled = co_await image.join();
            } else {
                try {
                    result.image = from_png(StringView(path.c_str()));
                    LoadTimer timer("preload symbol field (build) ", path.c_str());
                    result.field = co_await symbol_sdf_for_atlas_parallel(path, &result.image, in_flight);
                } catch (...) {
                }
            }
            if (result.image.minor() && result.field.image.minor())
                out.value = std::move(result);
            co_return;
        }

    } // namespace

    void AssetPreload::image(std::filesystem::path const& path, bool premultiplied) {
        auto key = std::make_pair(path.string(), premultiplied);
        if (_images.contains(key))
            return;
        auto slot = std::make_shared<Slot<matrix<RGBA8Unorm_sRGB>>>();
        _images.emplace(key, slot);
        _scope->soon(decode_image(Publisher(std::move(slot)), key.first, premultiplied));
    }

    void AssetPreload::mesh(std::filesystem::path const& path) {
        std::string key = path.string();
        if (_meshes.contains(key))
            return;
        auto slot = std::make_shared<Slot<wry::mesh::mesh>>();
        _meshes.emplace(key, slot);
        _scope->soon(decode_mesh(Publisher(std::move(slot)), key));
    }

    void AssetPreload::symbol_atlas(std::filesystem::path const& path) {
        std::string key = path.string();
        if (_symbol_atlases.contains(key))
            return;
        auto slot = std::make_shared<Slot<SymbolAtlas>>();
        _symbol_atlases.emplace(key, slot);
        _scope->soon(decode_symbol_atlas(Publisher(std::move(slot)), key, 0));
    }

    std::shared_ptr<AssetPreload::Slot<AssetPreload::SymbolAtlas>>
    AssetPreload::symbol_atlas_detached(std::filesystem::path const& path, int in_flight) {
        auto slot = std::make_shared<Slot<SymbolAtlas>>();
        _scope->soon(decode_symbol_atlas(Publisher(slot), path.string(), in_flight));
        return slot;
    }

    namespace {
        template<typename Map, typename Key>
        auto take_from(Map& map, Key const& key) -> decltype(map.begin()->second->take()) {
            auto it = map.find(key);
            if (it == map.end())
                return std::nullopt;
            auto slot = std::move(it->second);
            map.erase(it);
            return slot->take();
        }
    }

    std::optional<matrix<RGBA8Unorm_sRGB>> AssetPreload::take_image(std::filesystem::path const& path, bool premultiplied) {
        return take_from(_images, std::make_pair(path.string(), premultiplied));
    }

    std::optional<wry::mesh::mesh> AssetPreload::take_mesh(std::filesystem::path const& path) {
        return take_from(_meshes, path.string());
    }

    std::optional<AssetPreload::SymbolAtlas> AssetPreload::take_symbol_atlas(std::filesystem::path const& path) {
        return take_from(_symbol_atlases, path.string());
    }

    void AssetPreload::wait() {
        for (auto& [key, slot] : _images)
            slot->wait();
        for (auto& [key, slot] : _meshes)
            slot->wait();
        for (auto& [key, slot] : _symbol_atlases)
            slot->wait();
    }

    // A preloaded image is the synchronous decode; a missing file takes as
    // nullopt (the synchronous path reports it); an unrequested asset is
    // nullopt at once; a take consumes the request.  The takes come after
    // the scope's join, so none of them blocks this test's worker (a
    // take before then would wait on a condition variable, holding a
    // pool thread the decode may need)
    define_test("asset_preload") {
        const std::filesystem::path white = "white.png";
        const bool have_white = std::filesystem::exists(white);
        coroutine::Nursery scope;
        AssetPreload preload(&scope);
        if (have_white)
            preload.image(white, true);
        preload.image("no_such_asset_preload_test.png", false);
        [[maybe_unused]] std::ptrdiff_t cancelled = co_await scope.join();
        require(cancelled == 0);
        if (have_white) {
            auto image = preload.take_image(white, true);
            require(image.has_value());
            matrix<RGBA8Unorm_sRGB> direct = from_png(StringView(white.c_str()));
            multiply_alpha_inplace(direct);
            require(image->minor() == direct.minor() && image->major() == direct.major());
            for (std::size_t y = 0; y != direct.minor(); ++y)
                require(std::memcmp(&(*image)[(ptrdiff_t)y, 0], &direct[(ptrdiff_t)y, 0],
                                   direct.major() * sizeof(RGBA8Unorm_sRGB)) == 0);
            require(!preload.take_image(white, true).has_value());
        }
        require(!preload.take_image("no_such_asset_preload_test.png", false).has_value());
        require(!preload.take_mesh("never_requested.obj").has_value());
        co_return;
    };

} // namespace wry
