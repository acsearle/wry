//
//  gui.hpp
//  client
//
//  Created by Antony Searle on 29/12/2025.
//

#ifndef gui_hpp
#define gui_hpp

#include "simd.hpp"

namespace wry {
    
    namespace gui {
        
        // Immediate mode GUI, since all the cool kids were doing it
        
        // User API
        
        void line(const char*);
        void multiline(const char*);
        void label(const char*);
        bool button(const char*);
        void title(const char*);
        void window(const char*);
        void icon(int);
        void editor(char*, size_t);
        void editor(int*);
        void editor(double*);
        
        // void paletteBegin();
        //     void paletteNewRow();
        // int2 paletteEnd();
        
        
        // Manager API
        
        bool offer_mouse(double2 xy);
        void offer_keys(/* ... */);
        bool mouse_is_captured();
        bool keyboard_is_captured();
        
        struct vertex;
        
        std::size_t bake(vertex*, std::size_t);
        
    } // namespace gui
    
} // namespace wry

#endif /* gui_hpp */
