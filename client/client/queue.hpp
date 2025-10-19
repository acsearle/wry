//
//  queue.hpp
//  client
//
//  Created by Antony Searle on 22/2/2024.
//

#ifndef queue_hpp
#define queue_hpp

#include <deque>

#include "mutex.hpp"

namespace wry {
    
    template<typename T>
    struct BlockingDeque {
        
        mutable FastBasicLockable _mutex;
        std::deque<T> _deque;
                
        void push_back(auto&&... args) {
            std::unique_lock lock{_mutex};
            _deque.emplace_back(FORWARD(args)...);
        }

        void push_front(auto&&... args) {
            std::unique_lock lock{_mutex};
            _deque.emplace_front(FORWARD(args)...);
        }
        
        bool pop_front(T& victim) {
            std::unique_lock lock{_mutex};
            bool result = !_deque.empty();
            if (result) {
                victim = std::move(_deque.front());
                _deque.pop_front();
            }
            return result;
        }

        bool pop_back(T& victim) {
            std::unique_lock lock{_mutex};
            bool result = !_deque.empty();
            if (result) {
                victim = std::move(_deque.back());
                _deque.pop_back();
            }
            return result;
        }

    };
    
    template<typename T>
    void garbage_collected_scan(BlockingDeque<T> const& a) {
        std::unique_lock lock{a._mutex};
        for (T const& b : a._deque)
            garbage_collected_scan(b);        
    }
    
} // namespace wry

#endif /* queue_hpp */
