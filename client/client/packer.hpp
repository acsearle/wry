//
//  packer.hpp
//  client
//
//  Created by Antony Searle on 26/6/2023.
//

#ifndef packer_hpp
#define packer_hpp

#include <set>

#include "rect.hpp"

namespace wry {
    
    // Guillotine algorithm for packing rectangles, typically for packing
    // images into a sprite sheet / texture atlas
    
    template<typename T>
    struct packer {
        
        using T2 = typename rect<T>::T2;
        
        std::multiset<rect<T>, rect_area_cmp> _free;
        
        bool invariant() {
            if (_free.empty())
                return true;
            // Nondegenerate
            if (area(*_free.begin()) == 0)
                return false;
            // Non-overlapping
            //     too expensive
        }
        
        explicit packer(T n)
        : packer(rect<T>(0, 0, n, n)) {
        }
        
        explicit packer(const rect<T>& r) {
            _free.insert(r);
        }
        
        void _emplace(T a, T b, T c, T d) {
            _free.emplace(a, b, c, d);
        }
        
        T2 place(T2 wh) {
            
            // Start with the smallest free rectangle with enough area
            auto i = _free.lower_bound(rect<T>(0, 0, wh.x, wh.y));
            // Search for free rectangle with big enough dimensions
            while ((i != _free.end()) && ((i->width() < wh.x) || (i->height() < wh.y)))
                ++i;
            // Check we found one
            if (i == _free.end()) {
                assert(false);
            }
            // Remove it from the free list
            rect<T> old = *i;
            _free.erase(i);
            
            // Compute the new corner
            T2 c(old.a + wh);
            
            // Compute which split yields a bigger free rectangle
            if (((old.b.x - old.a.x) * (old.b.y - c.y)) >= ((old.b.x - c.x) * (old.b.y - old.a.y))) {
                // Add the new free rectangles if they are not degenerate
                if (c.y != old.b.y)
                    _emplace(old.a.x, c.y, old.b.x, old.b.y);
                if (c.x != old.b.x)
                    _emplace(c.x, old.a.y, old.b.x, c.y);
            } else {
                if (c.x != old.b.x)
                    _emplace(c.x, old.a.y, old.b.x, old.b.y);
                if (c.y != old.b.y)
                    _emplace(old.a.x, c.y, c.x, old.b.y);
            }
            
            return old.a;
            
        }
        
        void release(T2 a, T2 b) {
            _free.emplace(a, b);
        }
        
    }; // class packer<T>
    
    
} // namespace wry

#endif /* packer_hpp */
