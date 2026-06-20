//
//  save.cpp
//  client
//
//  Created by Antony Searle on 28/9/2024.
//
//  Replaces the prior sqlite-blob stub.  Uses save_format.{hpp,cpp} for the
//  actual graph serialization.  This file is just the file-system glue:
//  enumerate saves, write one out, read one back.
//

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <set>
#include <vector>

#include "coroutine.hpp"
#include "save.hpp"
#include "save_format.hpp"
#include "term.hpp"
#include "test.hpp"

namespace wry {

    namespace {
        constexpr uint32_t SAVE_MAGIC   = 0x57525953;  // 'WRYS'
        constexpr uint32_t SAVE_VERSION = 1;

        std::filesystem::path saves_dir() {
            // TODO: real per-user save location.  Cwd is fine for sketch.
            std::filesystem::path p{"saves"};
            std::error_code ec;
            std::filesystem::create_directories(p, ec);
            return p;
        }

        std::filesystem::path save_path_for_id(int id) {
            char buf[32];
            std::snprintf(buf, sizeof buf, "save_%d.wry", id);
            return saves_dir() / buf;
        }
    }

    World* restart_game() {
        return new World;
    }

    // Serialize the (immutable) world into a byte buffer.  Pure read of the
    // world plus local Saver state, so it is lock-free and safe to run
    // concurrently -- background saves may overlap.
    static std::vector<uint8_t> serialize_world(const World* world) {
        Saver s;
        // Reserve space for the file header.  We write the real values once
        // we know the record count.
        s.write_u32(SAVE_MAGIC);
        s.write_u32(SAVE_VERSION);
        size_t record_count_offset = s._stream.size();
        s.write_u32(0);  // placeholder

        SaveRef root_ref = s.save_world(world);
        s.resolve_pending();

        // Patch record count.
        uint32_t record_count = s._next_ref - 1;
        std::memcpy(s._stream.data() + record_count_offset, &record_count, sizeof(uint32_t));

        // Append root ref at the tail.
        s.write_ref(root_ref);

        return std::move(s._stream);
    }

    // Allocate the next save id and create an empty file to claim it, under a
    // lock, so two concurrent saves can't pick the same id.  Returns the path
    // to fill in (the writer truncates and overwrites it).
    static std::filesystem::path reserve_new_save_path() {
        static std::mutex save_id_mutex;
        std::scoped_lock guard{save_id_mutex};
        int next_id = 1;
        for (auto& entry : std::filesystem::directory_iterator(saves_dir())) {
            const auto& p = entry.path();
            if (p.extension() != ".wry") continue;
            int id = 0;
            if (std::sscanf(p.filename().string().c_str(), "save_%d.wry", &id) == 1)
                next_id = std::max(next_id, id + 1);
        }
        auto path = save_path_for_id(next_id);
        std::ofstream{path, std::ios::binary};  // create empty -> reserves the id
        return path;
    }

    void save_game(const World* world) {
        std::vector<uint8_t> buffer = serialize_world(world);
        auto path = reserve_new_save_path();
        std::ofstream out(path, std::ios::binary);
        out.write((const char*)buffer.data(), (std::streamsize)buffer.size());
    }

    // Detached coroutine form of save_game.  Holds the rooted snapshot in its
    // frame and yields (reschedules to the work queue) between the walk and the
    // file write, and between file chunks -- so it never holds a worker's
    // mutator pin across the whole save, letting the epoch advance in between,
    // and stays a good work-queue citizen.  The Root keeps the immutable
    // snapshot alive across every yield, regardless of which worker/epoch
    // resumes it.  Launched detached by save_game_async; frees its own frame.
    static Coroutine::Task background_save_coroutine(Root<World const*> snapshot) {
        std::vector<uint8_t> buffer = serialize_world(&*snapshot);

        co_await Coroutine::SuspendAndSchedule{};  // yield after the walk

        std::filesystem::path path = reserve_new_save_path();
        std::ofstream out(path, std::ios::binary);
        constexpr size_t CHUNK = size_t{1} << 16;
        for (size_t off = 0; off < buffer.size(); ) {
            size_t n = std::min(CHUNK, buffer.size() - off);
            out.write((const char*)buffer.data() + off, (std::streamsize)n);
            off += n;
            if (off < buffer.size())
                co_await Coroutine::SuspendAndSchedule{};  // yield between chunks
        }

        // Fall off the end: final_suspend frees this frame (and the Root, on a
        // mutator worker) and resumes the wait_group runner, which releases the
        // WaitGroup count.
        co_return;
    }

    void save_game_async(Root<World const*> snapshot) {
        // Anchor the save in the process-lifetime WaitGroup so a shutdown can't
        // abandon it mid-yield; the coroutine owns the Root snapshot in its frame.
        wait_group_spawn(background_save_coroutine(std::move(snapshot)));
    }

    World* load_game(int id) {
        auto path = save_path_for_id(id);
        std::ifstream in(path, std::ios::binary | std::ios::ate);
        if (!in) return new World;
        std::streamsize sz = in.tellg();
        in.seekg(0, std::ios::beg);

        std::vector<uint8_t> buf((size_t)sz);
        in.read((char*)buf.data(), sz);

        Loader L;
        L._cursor = buf.data();
        L._end    = buf.data() + buf.size();
        return L.load_world();
    }

    World* continue_game() {
        // Load the most-recent save by id.  Empty world if none.
        int best_id = -1;
        for (auto& entry : std::filesystem::directory_iterator(saves_dir())) {
            const auto& p = entry.path();
            if (p.extension() != ".wry") continue;
            int id = 0;
            if (std::sscanf(p.filename().string().c_str(), "save_%d.wry", &id) == 1)
                best_id = std::max(best_id, id);
        }
        if (best_id < 0) return new World;
        return load_game(best_id);
    }

    std::vector<std::pair<std::string, int>> enumerate_games() {
        std::vector<std::pair<std::string, int>> v;
        std::error_code ec;
        for (auto& entry : std::filesystem::directory_iterator(saves_dir(), ec)) {
            const auto& p = entry.path();
            if (p.extension() != ".wry") continue;
            int id = 0;
            if (std::sscanf(p.filename().string().c_str(), "save_%d.wry", &id) == 1) {
                v.emplace_back(p.filename().string(), id);
            }
        }
        // Newest (highest id) first.
        std::sort(v.begin(), v.end(),
                  [](auto& a, auto& b) { return a.second > b.second; });
        return v;
    }

    void delete_game(int id) {
        std::error_code ec;
        std::filesystem::remove(save_path_for_id(id), ec);
    }

    // Read a save file's raw bytes (empty vector if absent).  Used by the tests
    // to identify their own file by exact content rather than by id arithmetic,
    // which races when other tests save concurrently into the shared ./saves.
    static std::vector<uint8_t> read_save_file(int id) {
        std::vector<uint8_t> b;
        std::ifstream in(save_path_for_id(id), std::ios::binary | std::ios::ate);
        if (in) {
            auto sz = in.tellg();
            in.seekg(0);
            b.resize((size_t)sz);
            in.read((char*)b.data(), sz);
        }
        return b;
    }

    // Round-trip through the real save files (save_game writes a numbered
    // .wry, load_game reads it back), exercising the file glue that the
    // in-memory save_format tests do not.  Self-cleaning: the file it
    // creates is deleted at the end.  Writes under ./saves relative to the
    // process cwd.
    define_test("save_game_file_roundtrip") {

        World* w = new World;
        w->_time = Time{1234};
        w->_term_for_coordinate.set(Coordinate{3, -7}, term_make_integer_with(99));

        // save_game is synchronous, so its file is complete on return; find it
        // by exact bytes (the sentinel content is unique amid concurrent saves).
        std::vector<uint8_t> ref = serialize_world(w);
        save_game(w);

        int new_id = -1;
        for (auto& [name, id] : enumerate_games())
            if (read_save_file(id) == ref) { new_id = id; break; }
        assert(new_id >= 0);

        World* w2 = load_game(new_id);
        assert(w2);
        assert(w2->_time == w->_time);
        Term t;
        assert(w2->_term_for_coordinate.try_get(Coordinate{3, -7}, t));
        assert(t._data == term_make_integer_with(99)._data);

        delete_game(new_id);
        assert(read_save_file(new_id).empty());

        co_return;
    };

    // Exercises the detached background-save coroutine end to end: launch it
    // against a rooted snapshot, then poll (yielding so the saver gets worker
    // time) until a save file appears whose bytes match a reference
    // serialization of the same world.  `serialize_world` is deterministic, so
    // the async write must reproduce it exactly; the distinctive `_time`
    // sentinel makes the file unambiguous amid any concurrent saves.  Reads
    // raw bytes (never `load_game`) so a half-written file is simply a non-match.
    define_test("save_game_async_roundtrip") {

        World* w = new World;
        w->_time = Time{0x5A5E5A5E};
        w->_term_for_coordinate.set(Coordinate{1, 1}, term_make_integer_with(5));

        std::vector<uint8_t> ref = serialize_world(w);

        Root<World const*> root(w);
        save_game_async(root);

        int found = -1;
        for (int iter = 0; iter < 200000 && found < 0; ++iter) {
            for (auto& [name, id] : enumerate_games()) {
                if (read_save_file(id) == ref) {
                    found = id;
                    break;
                }
            }
            if (found < 0)
                co_await Coroutine::SuspendAndSchedule{};  // let the saver run
        }
        assert(found >= 0);
        delete_game(found);

        co_return;
    };

} // namespace wry
