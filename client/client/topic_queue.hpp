//
//  topic_queue.hpp
//  client
//
//  Created by Antony Searle on 5/11/2023.
//

#ifndef topic_queue_hpp
#define topic_queue_hpp

#include "array.hpp"

namespace wry {
    
    // A queue whose members signal interest in topics with flags
    //
    // observers.for_topics(TOPIC_WRITE, &T::notify_write);
    
    template<typename T, typename E = std::int64_t>
    struct TopicQueue {
            
        static auto topic_intersection(E topics) {
            return [topics](const std::pair<E, T>& kv) {
                return kv.first & topics;
            };
        }
        
        static auto value_equality(const T& value) {
            [&value](const std::pair<E, T>& kv) {
                return kv.second == value;
            };
        }
        
        Array<std::pair<E, T>> _inner;
                
        void push(E topics, T value) {
            _inner.emplace_back(std::move(topics), std::move(value));
        }
        
        void for_topics(E topics, auto&& action) {
            auto predicate = topic_intersection(topics);
            for (auto& [key, value] : _inner)
                if (predicate(key))
                    action(value);
        }
        
        auto find(E topics) {
            return std::find_if(_inner._begin,
                                _inner._end,
                                topic_intersection(topics));
        }

        auto find(const T& value) {
            return std::find_if(_inner._begin,
                                _inner._end,
                                value_equality(value));
        }

        auto erase(const T& value) {
            return _inner.erase_if(value_equality(value));
        }
        
        bool contains(const T& value) const {
            return _inner.contains_if(value_equality(value));
        }

        bool contains(E topics) const {
            return _inner.contains_if(topic_intersection(topics));
        }

    };
    
} // namespace wry

#endif /* topic_queue_hpp */
