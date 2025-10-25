//
//  concurrent_queue.hpp
//  client
//
//  Created by Antony Searle on 22/10/2025.
//

#ifndef concurrent_queue_hpp
#define concurrent_queue_hpp

#include <condition_variable>
#include <mutex>
#include <queue>

namespace wry {
    
    template<typename T>
    struct BlockingConcurrentQueue {
        
        std::mutex _mutex;
        std::condition_variable _condition_variable;
        std::queue<T> _queue;
        ptrdiff_t _waiting = 0;
        bool _is_canceled = false;
        
        void push(T item) {
            bool notify = false;
            {
                std::unique_lock lock{_mutex};
                _queue.push(item);
                if (_waiting) {
                    notify = true;
                    --_waiting;
                }
            }
            if (notify)
                _condition_variable.notify_one();
        }
        
        bool try_pop(T& item) {
            std::unique_lock lock{_mutex};
            bool result = !_queue.empty();
            if (result) {
                item = _queue.front();
                _queue.pop();
            }
            return result;
        }
        
        void wait_pop(T& item) {
            std::unique_lock lock{_mutex};
            while (_queue.empty()) {
                ++_waiting;
                printf("a worker started waiting on global_work_queue\n");
                _condition_variable.wait(lock);
                printf("a worker stopped waiting on global_work_queue\n");
            }
        }

        // spurious wakes are permitted
        void wait_not_empty() {
            std::unique_lock lock{_mutex};
            if (_queue.empty()) {
                ++_waiting;
                printf("a worker started waiting on global_work_queue\n");
                _condition_variable.wait(lock);
                printf("a worker stopped waiting on global_work_queue\n");
            }
        }
        
        void cancel() {
            {
                std::unique_lock lock{_mutex};
                _is_canceled = true;
                _waiting = 0;
            }
            _condition_variable.notify_all();
        }
        
        bool is_canceled() {
            std::unique_lock lock{_mutex};
            return _is_canceled;
        }
        
    };
    
    
    
} // namespace wry

#endif /* concurrent_queue_hpp */
