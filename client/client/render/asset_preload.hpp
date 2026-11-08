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
        // The symbol field for the atlas at `path`, by
        // symbol_sdf_for_atlas_parallel (which may build and write the
        // cache: request each atlas at most once at a time)
        void symbol_field(std::filesystem::path const& path);

        // The decoded value, waiting for its task if it is still running
        std::optional<matrix<RGBA8Unorm_sRGB>> take_image(std::filesystem::path const& path, bool premultiplied);
        std::optional<wry::mesh::mesh> take_mesh(std::filesystem::path const& path);
        std::optional<SymbolSDF> take_symbol_field(std::filesystem::path const& path);

        // Until every request not yet taken has finished, without taking
        void wait();

    private:

        coroutine::Nursery* _scope;
        std::map<std::pair<std::string, bool>, std::shared_ptr<Slot<matrix<RGBA8Unorm_sRGB>>>> _images;
        std::map<std::string, std::shared_ptr<Slot<wry::mesh::mesh>>> _meshes;
        std::map<std::string, std::shared_ptr<Slot<SymbolSDF>>> _symbol_fields;

    };

} // namespace wry

#endif /* asset_preload_hpp */
