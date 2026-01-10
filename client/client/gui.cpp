//
//  gui.cpp
//  client
//
//  Created by Antony Searle on 29/12/2025.
//

#include "gui.hpp"

#include <memory>
#include <vector>

namespace wry {
    
    namespace gui {
        
        struct Localized;
        
        // brittle global state
        std::vector<std::shared_ptr<Localized>> _live; // under construction new GUI
        std::vector<std::shared_ptr<Localized>> _baked; // displayed and receiving clicks old GUI

        struct Base {
            virtual ~Base() = default;
        };
        
        struct Localized : Base {
            virtual double2 negotiate(double2 constraints);
        };
        
        
        
        
    } // namespace gui
    
} // namespace gui
