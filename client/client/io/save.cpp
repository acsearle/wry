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
#include <atomic>
#include <chrono>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include <fcntl.h>
#include <stdio.h>  // renamex_np
#include <unistd.h>

#include "coroutine.hpp"
#include "save.hpp"
#include "save_format.hpp"
#include "term.hpp"
#include "test.hpp"
#include "kqueue_reactor.hpp"

namespace wry {

    namespace {
        constexpr uint32_t SAVE_VERSION = TERM_SAVE_VERSION;

        std::filesystem::path saves_dir() {
            // TODO: real per-user save location.  Cwd is fine for sketch.
            std::filesystem::path p{"saves"};
            std::error_code ec;
            std::filesystem::create_directories(p, ec);
            return p;
        }

        // The file of a checked name
        std::filesystem::path save_path(std::string_view name) {
            std::string file(name);
            file += ".wry";
            return saves_dir() / file;
        }

        // The N of a stem save_N (decimal digits only), if it is one
        std::optional<int> automatic_save_number(std::string_view stem) {
            constexpr std::string_view prefix = "save_";
            if (!stem.starts_with(prefix) || stem.size() == prefix.size())
                return std::nullopt;
            long long n = 0;
            for (char c : stem.substr(prefix.size())) {
                if (c < '0' || c > '9')
                    return std::nullopt;
                n = n * 10 + (c - '0');
                if (n > 1'000'000'000)
                    return std::nullopt;
            }
            return (int)n;
        }

        // Whether the bytes are well-formed UTF-8: no stray continuation
        // bytes, truncations, overlong forms, surrogates or code points
        // beyond U+10FFFF
        bool is_utf8(std::string_view s) {
            size_t i = 0;
            while (i < s.size()) {
                const unsigned char c = (unsigned char)s[i];
                if (c < 0x80) {
                    ++i;
                    continue;
                }
                size_t n = 0;
                unsigned lo = 0x80, hi = 0xBF;  // the second byte's range
                if (c >= 0xC2 && c <= 0xDF) {
                    n = 1;
                } else if (c >= 0xE0 && c <= 0xEF) {
                    n = 2;
                    if (c == 0xE0) lo = 0xA0;   // overlong
                    if (c == 0xED) hi = 0x9F;   // surrogates
                } else if (c >= 0xF0 && c <= 0xF4) {
                    n = 3;
                    if (c == 0xF0) lo = 0x90;   // overlong
                    if (c == 0xF4) hi = 0x8F;   // beyond U+10FFFF
                } else {
                    return false;
                }
                if (s.size() - i <= n)
                    return false;
                for (size_t k = 1; k <= n; ++k) {
                    const unsigned char d = (unsigned char)s[i + k];
                    const unsigned a = (k == 1) ? lo : 0x80;
                    const unsigned b = (k == 1) ? hi : 0xBF;
                    if (d < a || d > b)
                        return false;
                }
                i += n + 1;
            }
            return true;
        }
    }

    std::expected<std::string, std::string> check_save_name(std::string_view name) {
        if (name.ends_with(".wry"))
            name.remove_suffix(4);
        // One ".wry" is the file's extension; a stem that still ends in it
        // ("notes.wry.wry") would be listed, loaded and deleted under a
        // different name ("notes") than it was saved under
        if (name.ends_with(".wry"))
            return std::unexpected(std::string("a save's name cannot end in \".wry.wry\""));
        if (name.empty())
            return std::unexpected(std::string("a save's name cannot be empty"));
        if (name.size() > SAVE_NAME_MAX_BYTES)
            return std::unexpected("a save's name can be at most "
                                   + std::to_string(SAVE_NAME_MAX_BYTES) + " bytes");
        if (name.front() == '.')
            return std::unexpected(std::string("a save's name cannot start with '.'"));
        for (char c : name) {
            const unsigned char u = (unsigned char)c;
            if (c == '/' || c == '\\' || c == ':')
                return std::unexpected(std::string("a save's name cannot contain '") + c + "'");
            if (u < 0x20 || u == 0x7F)
                return std::unexpected(std::string("a save's name cannot contain a control character"));
        }
        if (!is_utf8(name))
            return std::unexpected(std::string("a save's name must be UTF-8"));
        return std::string(name);
    }

    bool save_exists(std::string_view name) {
        std::error_code ec;
        return std::filesystem::is_regular_file(save_path(name), ec);
    }

    std::filesystem::path saves_directory() {
        return saves_dir();
    }

    World* restart_game() {
        World* world = new World;
        return world;
    }

    // Serialize the (immutable) world into a byte buffer.  Pure read of the
    // world plus local Saver state, so it is lock-free and safe to run
    // concurrently -- background saves may overlap.
    std::vector<uint8_t> serialize_world(const World* world) {
        Saver s;
        // Reserve space for the file header.  We write the real values once
        // we know the record count.
        s.write_u32(SAVE_MAGIC);
        s.write_u32(SAVE_VERSION);
        size_t record_count_offset = s._stream.size();
        s.write_u32(0);  // placeholder
        // The World's content hash, for the loader to check what it
        // rebuilt (Loader::load_and_commission).  O(1): the maps cache
        // their hashes, and the entities are sealed
        s.write_u64(world->_compute_content_hash());

        SaveRef root_ref = s.save_world(world);
        s.resolve_pending();

        // Patch record count.
        uint32_t record_count = s._next_ref - 1;
        std::memcpy(s._stream.data() + record_count_offset, &record_count, sizeof(uint32_t));

        // Append root ref at the tail.
        s.write_ref(root_ref);

        return std::move(s._stream);
    }

    // Atomic-save plumbing.  We never write the destination file in place: a
    // reader (the load list, our own load_game) must only ever see a complete
    // save, and a crash mid-write must not leave a corrupt one.  So we write to
    // a uniquely-named temp file IN THE SAVES DIRECTORY, flush it to stable
    // storage, then rename it into place under a lock.  The temp lives in the
    // saves dir on purpose: rename is only atomic within one filesystem, so a
    // temp in the OS's per-user temp dir (a likely different volume) would make
    // the publish a non-atomic cross-device copy (EXDEV) -- exactly what we're
    // avoiding.  A leftover temp from a crash is harmless: enumerate filters on
    // the .wry extension, so it is never offered as a save.

    // Create a unique temp file in the saves directory; returns an open fd and
    // fills out_path.  fd < 0 on failure.
    static int make_temp_save(std::filesystem::path& out_path) {
        std::string templ = (saves_dir() / ".savetmp.XXXXXX").string();
        int fd = mkstemp(templ.data());  // mutates the XXXXXX in place
        if (fd >= 0)
            out_path = templ;
        return fd;
    }

    static bool write_all(int fd, const uint8_t* data, size_t n) {
        for (size_t off = 0; off < n; ) {
            ssize_t w = ::write(fd, data + off, n - off);
            if (w < 0) {
                if (errno == EINTR) continue;
                return false;
            }
            off += (size_t)w;
        }
        return true;
    }

    // Flush the temp to stable storage before publishing, so a crash can't leave
    // a named-but-empty save.  On macOS plain fsync only reaches the drive;
    // F_FULLFSYNC asks the drive to flush its own write cache to the platter.
    // Returns false if the flush reports an error.
    static bool flush_temp_save(int fd) {
#ifdef F_FULLFSYNC
        return fcntl(fd, F_FULLFSYNC) == 0;
#else
        return fsync(fd) == 0;
#endif
    }

    // Close the temp -- which also surfaces deferred write errors -- and, only
    // if every step succeeded, atomically rename it into place: as `name`,
    // replacing any save of that name (rename(2) swaps the directory entry in
    // one step, so a reader sees the old save or the new, never neither), or,
    // if `name` is empty, as the next save_N (picked here, under a lock, so
    // concurrent saves can't collide).  On any failure the temp is removed and
    // nothing is published: a failed save leaves no file rather than a corrupt
    // one, and the save it would have replaced is untouched.
    static SaveOutcome publish_or_discard(int fd, const std::filesystem::path& temp_path,
                                          bool ok, std::string const& name) {
        ok = (close(fd) == 0) && ok;  // close() always runs; its error counts
        SaveOutcome outcome;
        if (ok) {
            static std::mutex publish_mutex;
            std::scoped_lock guard{publish_mutex};
            if (!name.empty()) {
                const std::filesystem::path destination = save_path(name);
                std::error_code ec;
                outcome.name = name;
                outcome.replaced = std::filesystem::exists(destination, ec);
                std::filesystem::rename(temp_path, destination, ec);
                ok = !ec;
            } else {
                // One past the highest save_N.  The lock serializes this
                // process's saves; another process saving into the same
                // directory (a second client, a test run) could pick the same
                // N, so the rename refuses to replace and the next N is tried
                int next = 1;
                for (std::string const& stem : enumerate_games())
                    if (std::optional<int> n = automatic_save_number(stem))
                        next = std::max(next, *n + 1);
                for (int attempt = 0; ; ++attempt, ++next) {
                    // automatic_save_number recognizes at most 10^9; a save
                    // numbered beyond it would not count toward the next
                    // number, so every later save would retry over it
                    if (next > 1'000'000'000) {
                        ok = false;
                        break;
                    }
                    outcome.name = "save_" + std::to_string(next);
                    const std::string destination = save_path(outcome.name).string();
#if defined(__APPLE__)
                    ok = renamex_np(temp_path.c_str(), destination.c_str(), RENAME_EXCL) == 0;
                    if (!ok && errno == EEXIST && attempt < 1000)
                        continue;
#else
                    std::error_code ec;
                    std::filesystem::rename(temp_path, destination, ec);
                    ok = !ec;
#endif
                    break;
                }
            }
        }
        if (!ok) {
            std::error_code ec;
            std::filesystem::remove(temp_path, ec);
            return {};
        }
        return outcome;
    }

    // Synchronous save: blocks on the flush -- fine, this is the synchronous API
    // (only the tests use it now); callers that must not stall a pool worker
    // spawn save_game.
    SaveOutcome save_game(const World* world, std::string_view name) {
        std::vector<uint8_t> buffer = serialize_world(world);
        std::filesystem::path temp;
        int fd = make_temp_save(temp);
        if (fd < 0) return {};
        bool ok = write_all(fd, buffer.data(), buffer.size()) && flush_temp_save(fd);
        return publish_or_discard(fd, temp, ok, std::string(name));
    }

    // Detached coroutine form of save_game.  Holds the rooted snapshot in its
    // frame and yields (reschedules to the work queue) between the walk and the
    // file write, and between file chunks -- so it never holds a worker's mutator
    // pin across the whole save, letting the epoch advance in between.  The
    // blocking F_FULLFSYNC is offloaded to a throwaway thread.  When the save
    // finishes, it returns what was published (an empty name if nothing was).
    static coroutine::Future<SaveOutcome> background_save_coroutine(Root<World const*> snapshot,
                                                                    std::string name) {
        std::vector<uint8_t> buffer = serialize_world(&*snapshot);

        co_await coroutine::TransferToPoolExecutor{};  // yield after the walk

        SaveOutcome outcome;
        std::filesystem::path temp;
        int fd = make_temp_save(temp);
        if (fd >= 0) {
            bool wrote = true;
            constexpr size_t CHUNK = size_t{1} << 16;
            for (size_t off = 0; off < buffer.size() && wrote; ) {
                size_t n = std::min(CHUNK, buffer.size() - off);
                wrote = write_all(fd, buffer.data() + off, n);
                off += n;
                if (wrote && off < buffer.size())
                    co_await coroutine::TransferToPoolExecutor{};  // yield between chunks
            }

            bool flushed = false;
            if (wrote) {
#ifdef F_FULLFSYNC
                // F_FULLFSYNC can block for tens of ms, so run it on a throwaway
                // thread, then hop BACK to a pool worker.  The hop-back is
                // load-bearing: when this coroutine completes its final_suspend
                // destroys the frame and its Root, and ~Root asserts a mutator
                // thread -- the throwaway thread is not one.
                co_await coroutine::TransferToBlockableExecutor{};
                flushed = flush_temp_save(fd);
                co_await coroutine::TransferToPoolExecutor{};
#else
                flushed = flush_temp_save(fd);
#endif
            }
            outcome = publish_or_discard(fd, temp, wrote && flushed, name);
        }
        // Fall off the end: final_suspend frees this frame (and the Root, on a
        // mutator worker) and resumes the continuation.
        co_return outcome;
    }

    coroutine::Task save_game(Root<World const*> snapshot, std::string name,
                              move_only_function<void(SaveOutcome)> on_done) {
        // The frame owns the Root snapshot; the spawner's scope owns the frame
        SaveOutcome outcome = co_await background_save_coroutine(std::move(snapshot), std::move(name));
        if (on_done)
            on_done(std::move(outcome));
    }

    World* load_game(std::string_view name) {
        auto path = save_path(name);
        std::ifstream in(path, std::ios::binary | std::ios::ate);
        if (!in) return nullptr;
        std::streamsize sz = in.tellg();
        if (sz < 0) return nullptr;
        in.seekg(0, std::ios::beg);

        std::vector<uint8_t> buf((size_t)sz);
        in.read((char*)buf.data(), sz);

        Loader L;
        L._cursor = buf.data();
        L._end    = buf.data() + buf.size();
        // Null for a file this build refuses (an older version, a hash the
        // rebuilt World does not reproduce, damage): the caller reports it.
        // It used to fall back to an empty, unsealed `new World`, which the
        // session then hosted as if it had loaded
        return L.load_and_commission();
    }

    World* load_world_from_bytes(std::span<const uint8_t> bytes) {
        Loader L;
        L._cursor = bytes.data();
        L._end    = bytes.data() + bytes.size();
        return L.load_and_commission();
    }

    World* continue_game() {
        std::vector<std::string> names = enumerate_games();
        if (names.empty()) return nullptr;
        return load_game(names.front());
    }

    std::vector<std::string> enumerate_games() {
        struct Entry {
            std::string name;
            std::filesystem::file_time_type modified;
        };
        std::vector<Entry> v;
        std::error_code ec;
        for (auto& entry : std::filesystem::directory_iterator(saves_dir(), ec)) {
            const auto& p = entry.path();
            if (p.extension() != ".wry") continue;
            std::error_code fec;
            if (!entry.is_regular_file(fec)) continue;
            // Only names `load` would accept, so every save listed can be
            // named: a hand-made "a:b.wry" or ".x.wry" is not offered
            std::string stem = p.stem().string();
            auto checked = check_save_name(stem);
            if (!checked || *checked != stem) continue;
            auto modified = entry.last_write_time(fec);
            if (fec) continue;  // gone since the scan saw it
            v.push_back({std::move(stem), modified});
        }
        // Newest first; ties (a coarse clock) by name, the later first
        std::sort(v.begin(), v.end(), [](Entry const& a, Entry const& b) {
            if (a.modified != b.modified)
                return a.modified > b.modified;
            return a.name > b.name;
        });
        std::vector<std::string> names;
        names.reserve(v.size());
        for (Entry& e : v)
            names.push_back(std::move(e.name));
        return names;
    }

    bool delete_game(std::string_view name) {
        std::error_code ec;
        return std::filesystem::remove(save_path(name), ec);
    }

    // ---- Tests ---------------------------------------------------------
    //
    // These write into the real saves directory (./saves relative to the
    // process cwd), which may hold the player's own saves and other tests'
    // concurrent ones, so every name they use is unique to the process
    // and the test, and each test removes exactly the files it made.

    // Read a save file's raw bytes (empty vector if absent).  Used by the tests
    // to identify their own file by exact content rather than by name
    // arithmetic, which races when other tests save concurrently.
    static std::vector<uint8_t> read_save_file(std::string_view name) {
        std::vector<uint8_t> b;
        std::ifstream in(save_path(name), std::ios::binary | std::ios::ate);
        if (in) {
            auto sz = in.tellg();
            in.seekg(0);
            b.resize((size_t)sz);
            in.read((char*)b.data(), sz);
        }
        return b;
    }

    // A name no other test or process is using
    static std::string unique_test_save_name(std::string_view label) {
        static std::atomic<int> counter{0};
        // Relaxed: only the values' distinctness matters
        const int k = counter.fetch_add(1, std::memory_order_relaxed);
        return "wry-test " + std::string(label) + " " + std::to_string(getpid())
            + "-" + std::to_string(k);
    }

    static World* make_test_world(Time time, int value) {
        World* w = new World;
        w->_time = Time{time};
        w->_term_for_coordinate.set(Coordinate{3, -7}, term_make_integer_with(value));
        w->hack_repair_invariant();
        return w;
    }

    static bool holds_test_value(World const* w, int value) {
        Term t;
        bool has_term = w->_term_for_coordinate.try_get(Coordinate{3, -7}, t);
        return has_term && t._data == term_make_integer_with(value)._data;
    }

    static bool listed(std::string const& name) {
        auto names = enumerate_games();
        return std::find(names.begin(), names.end(), name) != names.end();
    }

    define_test("save_name_validation") {
        auto accepted = [](std::string_view in, std::string_view want) {
            auto r = check_save_name(in);
            return r && *r == want;
        };
        auto refused = [](std::string_view in) { return !check_save_name(in); };
        require(accepted("base", "base"));
        require(accepted("my base, take 2", "my base, take 2"));
        require(accepted("save_3.wry", "save_3"));          // the file's name names it
        require(accepted("a.b", "a.b"));
        require(accepted("caf\xC3\xA9 \xE6\x97\xA5\xE6\x9C\xAC", "caf\xC3\xA9 \xE6\x97\xA5\xE6\x9C\xAC"));
        require(accepted(std::string(SAVE_NAME_MAX_BYTES, 'x'), std::string(SAVE_NAME_MAX_BYTES, 'x')));
        require(refused(""));
        require(refused(".wry"));
        require(refused(std::string(SAVE_NAME_MAX_BYTES + 1, 'x')));
        require(refused("a/b"));
        require(refused("a\\b"));
        require(refused("a:b"));
        require(refused(".hidden"));
        require(refused("."));
        require(refused(".."));
        require(refused(".savetmp.ABCDEF"));
        require(refused("tab\there"));
        require(refused("line\nbreak"));
        require(refused(std::string("nul\0byte", 8)));
        require(refused("del\x7F"));
        require(refused("\xC3"));                            // truncated
        require(refused("\xC0\xAF"));                        // overlong '/'
        require(refused("\xED\xA0\x80"));                    // a surrogate
        require(refused("\xF4\x90\x80\x80"));                // beyond U+10FFFF
        require(refused("\x80"));                            // a stray continuation
        require(refused("notes.wry.wry"));                   // would list as "notes"
        require(accepted("notes.wry", "notes"));
        co_return;
    };

    // Round-trip through the real save files (save_game writes save_N.wry
    // when given no name, load_game reads it back), exercising the file
    // glue that the in-memory save_format tests do not.
    define_test("save_game_file_roundtrip") {
        World* w = make_test_world(1234, 99);

        // save_game is synchronous, so its file is complete on return; it
        // names the file it wrote, which holds exactly the serialization
        std::vector<uint8_t> ref = serialize_world(w);
        SaveOutcome saved = save_game(w);
        require(saved);
        require(automatic_save_number(saved.name).has_value());
        require(!saved.replaced);
        require(read_save_file(saved.name) == ref);
        require(listed(saved.name));

        World* w2 = load_game(saved.name);
        require(w2);
        require(w2->_time == w->_time);
        require(holds_test_value(w2, 99));

        // Deleted once, and then the name is given up: in the shared saves
        // directory another process may already have published a new save
        // under the same automatic name, which a second delete or a read
        // would touch
        require(delete_game(saved.name));
        co_return;
    };

    // A save made under a name: saving it again replaces it (and says so),
    // and the replacement is what loads
    define_test("save_game_named_overwrite") {
        const std::string name = unique_test_save_name("overwrite");
        require(check_save_name(name).value_or("") == name);
        require(!save_exists(name));

        World* first = make_test_world(11, 1);
        SaveOutcome a = save_game(first, name);
        require(a && a.name == name && !a.replaced);
        require(save_exists(name) && listed(name));

        World* second = make_test_world(22, 2);
        SaveOutcome b = save_game(second, name);
        require(b && b.name == name && b.replaced);
        require(read_save_file(name) == serialize_world(second));

        World* loaded = load_game(name);
        require(loaded && loaded->_time == second->_time && holds_test_value(loaded, 2));

        require(delete_game(name));
        require(!save_exists(name) && !listed(name));
        co_return;
    };

    // Any *.wry file in the directory is a save, whatever made it: a file
    // put there by hand lists by its stem and loads by it, newest first;
    // files whose stem is not a valid name, and files of other kinds, are
    // not listed.  The automatic save_N numbering ignores foreign names
    define_test("save_listing_foreign_files") {
        const std::string older = unique_test_save_name("foreign caf\xC3\xA9");
        const std::string newer = unique_test_save_name("foreign (copy)");
        const std::string hidden = "." + unique_test_save_name("hidden");
        const std::string other = unique_test_save_name("not a save") + ".txt";
        World* w = make_test_world(77, 7);
        const std::vector<uint8_t> bytes = serialize_world(w);
        auto put = [&](std::string const& file) {
            std::ofstream out(saves_directory() / file, std::ios::binary);
            out.write((const char*)bytes.data(), (std::streamsize)bytes.size());
            return (bool)out;
        };
        require(put(older + ".wry"));
        require(put(newer + ".wry"));
        require(put(hidden + ".wry"));
        require(put(other));
        // Order them by modification time, the way a player's copies are
        const auto now = std::filesystem::file_time_type::clock::now();
        std::error_code ec;
        std::filesystem::last_write_time(save_path(older), now - std::chrono::hours(2), ec);
        require(!ec);
        std::filesystem::last_write_time(save_path(newer), now - std::chrono::hours(1), ec);
        require(!ec);

        const std::vector<std::string> names = enumerate_games();
        auto at = [&](std::string const& n) {
            return std::find(names.begin(), names.end(), n) - names.begin();
        };
        require(at(older) < (ptrdiff_t)names.size());
        require(at(newer) < at(older));                       // newest first
        require(!listed(hidden) && !listed(hidden.substr(1)));
        for (std::string const& n : names)
            require(n.find("not a save") == std::string::npos);

        World* loaded = load_game(older);
        require(loaded && loaded->_time == w->_time && holds_test_value(loaded, 7));

        // An unnamed save is numbered past the save_N files only
        SaveOutcome numbered = save_game(w);
        require(numbered && automatic_save_number(numbered.name).has_value());
        require(delete_game(numbered.name));

        require(delete_game(older));
        require(delete_game(newer));
        std::filesystem::remove(saves_directory() / (hidden + ".wry"), ec);
        std::filesystem::remove(saves_directory() / other, ec);
        require(!listed(older) && !listed(newer));
        co_return;
    };

    // Exercises the detached background-save coroutine end to end: launch it
    // against a rooted snapshot and wait for it to report the save it
    // published, whose bytes must match a reference serialization of the same
    // world (`serialize_world` is deterministic), first as save_N and then
    // under a name, replacing it.
    define_test("save_game_async_roundtrip") {

        World* w = new World;
        w->_time = Time{0x5A5E5A5E};
        w->_term_for_coordinate.set(Coordinate{1, 1}, term_make_integer_with(5));
        w->hack_repair_invariant();

        std::vector<uint8_t> ref = serialize_world(w);

        // Wait for the save with a wall-clock deadline.  (The old
        // iteration-counted poll deflated with optimization: 200k yields is
        // seconds of budget in Debug but ~50ms at -O, less than one
        // F_FULLFSYNC, and asserted spuriously.)
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        std::optional<SaveOutcome> result
            = co_await with_deadline(deadline, background_save_coroutine(Root<World const*>(w), std::string()));
        require(result && *result);
        if (!result || !*result)
            co_return;
        require(automatic_save_number(result->name).has_value() && !result->replaced);
        // The name reported is the file published
        require(read_save_file(result->name) == ref);
        require(delete_game(result->name));

        const std::string name = unique_test_save_name("async");
        for (bool replacing : {false, true}) {
            deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
            result = co_await with_deadline(deadline, background_save_coroutine(Root<World const*>(w), name));
            require(result && *result);
            if (!result || !*result)
                co_return;
            require(result->name == name && result->replaced == replacing);
            require(read_save_file(name) == ref);
        }
        require(delete_game(name));

        co_return;
    };

} // namespace wry
