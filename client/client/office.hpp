//
//  office.hpp
//  client
//
//  Created by Antony Searle on 11/10/2025.
//

#ifndef office_hpp
#define office_hpp

#include <vector>

#include "algorithm.hpp"
#include "mutex.hpp"
#include "stdint.hpp"

namespace wry {

    // An office issues tickets
    
    struct BlockingOfficeState {
        
        FastBasicLockable mutex;
        uint64_t count = 0;
        std::vector<uint64_t> priorities;
        bool processed = false;
        
        void open() {
            
        };
        
        void apply(uint64_t priority) {
            std::unique_lock lock{mutex};
            assert(!processed);
            priorities.push_back(priority);
        };
        
        void _assign() {
            std::sort(priorities.begin(), priorities.end());
            auto partition = std::unique(priorities.begin(), priorities.end());
            priorities.erase(partition, priorities.end());
        }
        
        uint64_t collect(uint64_t priority) {
            std::unique_lock lock{mutex};
            if (!processed) {
                _assign();
                processed = true;
            }
            auto a = std::lower_bound(priorities.begin(), priorities.end(), priority);
            assert(a != priorities.end());
            assert(*a == priority);
            return count + std::distance(priorities.begin(), a);
        };
        
        void close() {
            std::unique_lock lock{mutex};
            count += priorities.size();
            priorities.clear();
        }
        
    }; // BlockingOfficeState
    
} // namespace wry


#endif /* office_hpp */
