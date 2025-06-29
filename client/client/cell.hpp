//
//  cell.hpp
//  client
//
//  Created by Antony Searle on 12/8/2024.
//

#ifndef cell_hpp
#define cell_hpp

#include <utility>

namespace wry {
    
    // Rust vocabulary type
    //
    // Mutable, but not by reference
    
    template<typename T>
    struct Cell {
        
        T _inner;
        
        T load() const;
        void store(T);
        T exchange(T);
        bool compare_exchange(T&, T);
        
        T get() const;
        void set(T);
        T take();
        T replace(T);
        T into_inner() &&;
        
        template<typename F> T update(F&& f) const& { return _inner = std::forward<F>(f)(_inner); }
        template<typename F> T update(F&& f) const&& { return _inner = std::forward<F>(f)(std::move(_inner)); }
        template<typename F> T update(F&& f) & { return _inner = std::forward<F>(f)(_inner); }
        template<typename F> T update(F&& f) && { return _inner = std::forward<F>(f)(std::move(_inner)); }

    };
    
} // namespace wry


#endif /* cell_hpp */
