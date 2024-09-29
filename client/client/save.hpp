//
//  save.hpp
//  client
//
//  Created by Antony Searle on 28/9/2024.
//

#ifndef save_hpp
#define save_hpp

#include "world.hpp"

namespace wry::sim {

World* reset_game();
World* load_game();
void save_game(World* world);

};

#endif /* save_hpp */
