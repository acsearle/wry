//
//  asset_preload.hpp
//  client
//
//  Created by Antony Searle on 2026-11-08.
//
//  Decoding assets on the pool ahead of need (docs/async_loading.md).
//
//  The world scene's assets are CPU work, not disk work: PNG inflate and
//  unfilter, alpha premultiplication, OBJ parsing, and the symbol field
//  (from its cache PNG, or built).  The Metal objects made from them are
//  cheap.  So the host asks for the decodes as it starts, each a task on
//  the pool, concurrent with each other and with the splash and the main
//  menu; the scene, built on the main thread at New Game, takes the
//  decoded values and makes the Metal objects from them as before.
//
//  Everything here except the tasks themselves is for one thread (the
//  main thread): requests, takes and wait.  Each request is a Slot shared
//  between the table here and its task.  The task publishes its value
//  (or nothing, if it failed or was destroyed unrun) under the slot's
//  mutex and notifies; a take waits for that under the same mutex, so
//  the task's writes to the value happen-before the taker's reads
//  (unlock / lock of one mutex).  A take removes the entry: each request
//  is consumed once, and a later load of the same asset (a second New
//  Game) decodes on the spot as it always did.  Nothing is cached across
//  takes, so nothing goes stale.
//
//  A take returns nullopt for an asset never requested, or whose task
//  failed: the caller falls back to the synchronous load, which then
//  reports the problem the way it always has.
//
//  The world scene keeps what it makes from these (the Metal textures and
//  buffers) in the render context for the process, so a later New Game
//  neither decodes nor finds anything here (WryRenderContext assetCache).
//
//  A symbol atlas is one request for both of its parts, the atlas image
//  and its field, so that a field rebuilt on a cache miss is built from
//  the decode the texture uses rather than a second one.  The `atlas
//  NAME` switch asks for one outside the table (symbol_atlas_detached):
//  its requester owns the slot and polls it from the frame loop, and
//  wait() (New Game's) never waits for it.
//

#ifndef asset_preload_hpp
#define asset_preload_hpp

#include <condition_variable>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>

#include "image.hpp"
#include "matrix.hpp"
#include "mesh.hpp"
#include "nursery.hpp"
#include "symbol_sdf.hpp"

namespace wry {

    struct AssetPreload {

        template<typename T>
        struct Slot {
            std::mutex mutex;
            std::condition_variable published;
            bool done = false;              // under mutex
            std::optional<T> value;         // under mutex

            void publish(std::optional<T>&& v) {
                {
                    std::scoped_lock lock{mutex};
                    value = std::move(v);
                    done = true;
                }
                published.notify_all();
            }

            std::optional<T> take() {
                std::unique_lock lock{mutex};
                published.wait(lock, [this] { return done; });
                return std::move(value);
            }

            void wait() {
                std::unique_lock lock{mutex};
                published.wait(lock, [this] { return done; });
            }

            // Whether take() would return at once.  True is final: done
            // is set once, and seeing it under the mutex orders the
            // publisher's writes to the value before the caller's take
            bool ready() {
                std::scoped_lock lock{mutex};
                return done;
            }
        };

        // The atlas PNG as decoded (straight alpha) and its field
        struct SymbolAtlas {
            matrix<RGBA8Unorm_sRGB> image;
            SymbolSDF field;
        };

        // The tasks are spawned into `scope` (the process scope), which
        // outlives this object's users and joins the tasks at shutdown
        explicit AssetPreload(coroutine::Nursery* scope) : _scope(scope) {}

        AssetPreload(AssetPreload const&) = delete;
        AssetPreload& operator=(AssetPreload const&) = delete;

        // A PNG decoded by from_png, premultiplied if asked, as
        // WryRenderContext's newTextureFromResource does
        void image(std::filesystem::path const& path, bool premultiplied);
        // An OBJ by from_obj, then MeshVertexify'd
        void mesh(std::filesystem::path const& path);
        // The symbol atlas at `path`, decoded, and its field by
        // symbol_sdf_for_atlas_parallel (from the cache beside it, read
        // concurrently with the decode; or, on a miss, built from that
        // decode and cached)
        void symbol_atlas(std::filesystem::path const& path);
        // The same, but not entered in the table: the caller owns the
        // only reference besides the task's, polls ready() and takes it;
        // dropping it abandons the result (the task still runs to its
        // end, then frees it)
        // `in_flight`: a field built on a cache miss queues that many cells
        // at a time (build_symbol_sdf_parallel), for a request made while
        // a world is running
        std::shared_ptr<Slot<SymbolAtlas>> symbol_atlas_detached(std::filesystem::path const& path,
                                                                 int in_flight);

        // The decoded value, waiting for its task if it is still running
        std::optional<matrix<RGBA8Unorm_sRGB>> take_image(std::filesystem::path const& path, bool premultiplied);
        std::optional<wry::mesh::mesh> take_mesh(std::filesystem::path const& path);
        std::optional<SymbolAtlas> take_symbol_atlas(std::filesystem::path const& path);

        // Until every request not yet taken has finished, without taking
        void wait();

    private:

        coroutine::Nursery* _scope;
        std::map<std::pair<std::string, bool>, std::shared_ptr<Slot<matrix<RGBA8Unorm_sRGB>>>> _images;
        std::map<std::string, std::shared_ptr<Slot<wry::mesh::mesh>>> _meshes;
        std::map<std::string, std::shared_ptr<Slot<SymbolAtlas>>> _symbol_atlases;

    };

} // namespace wry

#endif /* asset_preload_hpp */
