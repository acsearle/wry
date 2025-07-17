//
//  bag.cpp
//  client
//
//  Created by Antony Searle on 17/7/2025.
//

#include "bag.hpp"

    
    
#if 0

namespace wry {

    // Reference implementation of true O(1) bag via std::deque
    template<typename T>
    struct StandardDequeBag {
        
        std::deque<T>* _inner = nullptr;
        
        bool is_empty() {
            return !_inner || _inner->empty();
        }
        
        constexpr StandardDequeBag() = default;
        ~StandardDequeBag() {
            delete _inner;
        }
        StandardDequeBag(const StandardDequeBag&) = delete;
        StandardDequeBag(StandardDequeBag&& other)
        : _inner(std::exchange(other._inner, nullptr)) {
        }
        
        void swap(StandardDequeBag& other) {
            using std::swap;
            swap(_inner, other._inner);
        }
        
        StandardDequeBag& operator=(const StandardDequeBag&) = delete;
        StandardDequeBag& operator=(StandardDequeBag&& other) {
            StandardDequeBag(std::move(other)).swap(*this);
            return *this;
        }
        
        size_t size() const {
            return _inner ? _inner->size() : 0;
        }
        
        void push(T value) {
            if (!_inner)
                _inner = new std::deque<T>;
            _inner->push_back(std::move(value));
        }
        
        bool try_pop(T& victim) {
            if (is_empty())
                return false;
            victim = std::move(_inner->front());
            _inner->pop_front();
            return true;
        }
        
        void extend(StandardDequeBag&& other) {
            T victim;
            while (other.try_pop(victim)) {
                push(std::move(victim));
            }
        }
        
    };
    
} // namespace wry

#endif
    
