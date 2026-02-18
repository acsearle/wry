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
void save_game(World* world);

std::vector<std::pair<std::string, int>> enumerate_games();

} // namespace wry::sim

#endif /* save_hpp */
