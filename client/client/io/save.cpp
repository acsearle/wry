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

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

#include "save.hpp"
#include "save_format.hpp"

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

    void save_game(World* world) {
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

        // Pick a new numeric id: highest existing + 1.
        int next_id = 1;
        for (auto& entry : std::filesystem::directory_iterator(saves_dir())) {
            const auto& p = entry.path();
            if (p.extension() != ".wry") continue;
            int id = 0;
            if (std::sscanf(p.filename().string().c_str(), "save_%d.wry", &id) == 1)
                next_id = std::max(next_id, id + 1);
        }

        auto path = save_path_for_id(next_id);
        std::ofstream out(path, std::ios::binary);
        out.write((const char*)s._stream.data(), (std::streamsize)s._stream.size());
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

} // namespace wry
