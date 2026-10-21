//
//  save.hpp
//  client
//
//  Created by Antony Searle on 28/9/2024.
//

#ifndef save_hpp
#define save_hpp

#include <filesystem>
#include <functional>
#include <span>
#include <vector>

#include "world.hpp"
#include "coroutine.hpp"
#include "functional.hpp"

namespace wry {

World* restart_game();
World* continue_game();
World* load_game(int id);
// Synchronous save; returns false on any I/O failure.  On failure the temp file
// is discarded, so a failed save leaves no file rather than a corrupt one.
bool save_game(const World* world);
// Background save, as a task for the caller to spawn into its scope (the
// process scope, so a shutdown drains it rather than abandoning it
// mid-yield): serializes the rooted snapshot off the main thread (yielding
// so it does not pin the epoch or monopolize a worker), writes a temp +
// atomic rename, with the blocking flush offloaded to a throwaway thread.
// The snapshot World must be immutable; the Root keeps it alive for the
// duration.  When the save finishes, `on_done(ok)` is invoked on a worker
// thread (ok == false on any I/O failure).
coroutine::Task save_game(Root<World const*> snapshot, move_only_function<void(bool)> on_done = {});
void delete_game(int id);

// The save format's bytes (a file's whole contents) for a frozen World,
// and a World from them: what a donated save is on the wire
// (multiplayer.md section 6).  The loader rejects bytes it cannot read
// instead of asserting on them, and the World it returns is commissioned
// (hack_repair_invariant); null if the bytes are not a save this build
// reads.
std::vector<uint8_t> serialize_world(const World* world);
World* _Nullable load_world_from_bytes(std::span<const uint8_t> bytes);

std::vector<std::pair<std::string, int>> enumerate_games();

// Where the saves live (created if absent); the session logs live beside
// them (game/session_log.hpp)
std::filesystem::path saves_directory();

} // namespace wry::sim

#endif /* save_hpp */
