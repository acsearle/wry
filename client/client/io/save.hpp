//
//  save.hpp
//  client
//
//  Created by Antony Searle on 28/9/2024.
//

#ifndef save_hpp
#define save_hpp

#include "world.hpp"

namespace wry {

World* restart_game();
World* continue_game();
World* load_game(int id);
void save_game(const World* world);
// Background save: serializes the rooted snapshot to a new file on the work
// queue, yielding periodically so it does not pin the epoch or monopolize a
// worker.  The snapshot World must be immutable (a frozen persistent-DS World);
// the Root keeps it alive for the duration.  Returns immediately.
void save_game_async(Root<World const*> snapshot);
void delete_game(int id);

std::vector<std::pair<std::string, int>> enumerate_games();

} // namespace wry::sim

#endif /* save_hpp */
