//
//  save.hpp
//  client
//
//  Created by Antony Searle on 28/9/2024.
//

#ifndef save_hpp
#define save_hpp

#include <expected>
#include <filesystem>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "world.hpp"
#include "coroutine.hpp"
#include "functional.hpp"

namespace wry {

// A save is a file NAME.wry in saves_directory(), and its name is the
// file's stem: whatever the player called it, or save_N for a save made
// without a name.  Any *.wry file there whose stem is a valid name is a
// save, so saves can be renamed, backed up, deleted, shared and dropped in
// by hand.  Nothing inside the file records its name.

// The name a player gave, checked and normalized: a trailing ".wry" is
// dropped (so "save_3.wry" names save_3), and a name is refused (the
// error says why) if it is empty, longer than SAVE_NAME_MAX_BYTES, starts
// with '.' (hidden files, and the .savetmp temps of saves in progress),
// contains '/', '\\', ':' or a control character, or is not UTF-8.
// Spaces and any other UTF-8 are fine.
constexpr size_t SAVE_NAME_MAX_BYTES = 100;
std::expected<std::string, std::string> check_save_name(std::string_view name);

// What a save published: its name, and whether it replaced a save of
// that name.  An empty name is a failed save
struct SaveOutcome {
    std::string name;
    bool replaced = false;
    explicit operator bool() const { return !name.empty(); }
};

World* restart_game();
// The newest save (by modification time); null if there is none or this
// build refuses it (older version, hash mismatch, damage)
World* _Nullable continue_game();
// Null if there is no save `name` (a checked name) or this build refuses it
World* _Nullable load_game(std::string_view name);
bool save_exists(std::string_view name);
// Synchronous save as `name` (a checked name), or as the next save_N if
// empty; fails on any I/O failure.  On failure the temp file is discarded,
// so a failed save leaves no file rather than a corrupt one, and a save it
// would have replaced is untouched.
SaveOutcome save_game(const World* world, std::string_view name = {});
// Background save, as a task for the caller to spawn into its scope (the
// process scope, so a shutdown drains it rather than abandoning it
// mid-yield): serializes the rooted snapshot off the main thread (yielding
// so it does not pin the epoch or monopolize a worker), writes a temp +
// atomic rename, with the blocking flush offloaded to a throwaway thread.
// The snapshot World must be immutable; the Root keeps it alive for the
// duration.  It saves as `name` (a checked name), replacing any save of
// that name in one atomic rename, or, if `name` is empty, as the next
// save_N.  When the save finishes, `on_done` is invoked on a worker thread
// with what was published (an empty name on any I/O failure).
coroutine::Task save_game(Root<World const*> snapshot, std::string name = {},
                          move_only_function<void(SaveOutcome)> on_done = {});
// Remove save `name` (a checked name); false if there was none to remove
bool delete_game(std::string_view name);

// The save format's bytes (a file's whole contents) for a frozen World,
// and a World from them: what a donated save is on the wire
// (multiplayer.md section 6).  The loader rejects bytes it cannot read
// instead of asserting on them, and the World it returns is commissioned
// (hack_repair_invariant); null if the bytes are not a save this build
// reads.
std::vector<uint8_t> serialize_world(const World* world);
World* _Nullable load_world_from_bytes(std::span<const uint8_t> bytes);

// The saves' names, newest (modification time) first.  Files whose stem
// is not a valid name (check_save_name) are not listed
std::vector<std::string> enumerate_games();

// Where the saves live (created if absent); the session logs live beside
// them (game/session_log.hpp)
std::filesystem::path saves_directory();

} // namespace wry::sim

#endif /* save_hpp */
