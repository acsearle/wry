//
//  save.hpp
//  client
//
//  Created by Antony Searle on 28/9/2024.
//

#ifndef save_hpp
#define save_hpp

#include <functional>

#include "world.hpp"

namespace wry {

World* restart_game();
World* continue_game();
World* load_game(int id);
// Synchronous save; returns false on any I/O failure.  On failure the temp file
// is discarded, so a failed save leaves no file rather than a corrupt one.
bool save_game(const World* world);
// Background save: serializes the rooted snapshot off the main thread (yielding
// so it does not pin the epoch or monopolize a worker), writes a temp + atomic
// rename, with the blocking flush offloaded to a throwaway thread.  The snapshot
// World must be immutable; the Root keeps it alive for the duration.  When the
// save finishes, `on_done(ok)` is invoked on a worker thread (ok == false on any
// I/O failure).  Returns immediately.
void save_game_async(Root<World const*> snapshot, std::function<void(bool)> on_done = {});
void delete_game(int id);

std::vector<std::pair<std::string, int>> enumerate_games();

} // namespace wry::sim

#endif /* save_hpp */
