//
//  queue.hpp
//  client
//
//  Created by Antony Searle on 22/2/2024.
//

#ifndef queue_hpp
#define queue_hpp

#include <queue>

#include "array.hpp"
#include "table.hpp"

namespace wry {
    
    template<typename T>
    using Queue = std::queue<T, Array<T>>;
    
    // A major pattern in our application is building a queue of Entity* without
    // duplicates, then draining it.  These objects will typically have a small
    // number of elements and brief lives, making designs with bad asymptotic
    // performance worth considering.
    //
    // The not-prematurely-optimized version is { Queue<T>, HashSet<T> }, which
    // gives us a "perfect" amortized O(1) try_push and O(N) storage.  The
    // constant factors in both cases are substantial though.
    //
    // A Queue<T> with a linear search scales poorly but may win up to hundreds
    // of elements.
    //
    // A Bloom filter with just a 64-bit hash or even just the Entity* bits
    // will speed up only the very small cases that linear search already
    // excels on :(
    //
    // Deferring the de-duplication to drain time costs the storage of the
    // duplicates, but we only have to store the HashSet briefly.
    
    
    
    
    // The data structures below preserve ordering like a queue, but reject
    // duplicates like a set.  They make differet tradeoffs that may be
    // interesting in the regime of small numbers of elements.
    
    template<typename T>
    struct QueueOfUniqueByFind {
        
        Queue<T> queue;
        
        // O(n)
        bool push(auto&& key) {
            if (queue.c.contains(key))
                return false;
            queue.push(std::forward<decltype(key)>(key));
            return true;
        }
        
        const T& front() const {
            return queue.front();
        }
        
        const T& back() const {
            return queue.back();
        }
        
        bool empty() const {
            return queue.empty();
        }
        
        size_type size() const {
            return queue.size();
        }
        
        void pop() const {
            queue.pop();
        }
        
        void swap(QueueOfUniqueByFind& other) {
            queue.swap(other.queue);
        }
                
    };
    
    template<typename T>
    struct QueueOfUniqueByBloomOrFind {
        
        Queue<T> queue;
        
        uint64_t filter = {};
        
        bool push(auto&& key) {
            uint64_t h = hash(key);
            if (((filter & h) == h) && queue.c.contains(key))
                return false;
            queue.push(std::forward<decltype(key)>(key));
        }
        
    };
    
    
    // This QueueOfUnique is scalable, with amortized O(1) push, but has
    // significant overhead
    //
    // It maintains both an Array and a HashSet of the elements, the former
    // encoding order and the latter encoding membership.  As the first
    // use case is for pointer type and pointer equality, we store them
    // directly in each set.  Other uses may prefer one of the containers to
    // refer into the elements of the other (array index, if erasure is
    // prohibited)
    //
    // The best queue for our applications may switch strategy from find
    // to hash set membership
    
    template<typename T>
    struct QueueOfUnique {
        
        using value_type = typename Array<T>::value_type;
        using size_type = typename Array<T>::size_type;
        using reference = typename Array<T>::const_reference;
        using const_reference = typename Array<T>::const_reference;
        using iterator = typename Array<T>::iterator;
        using const_iterator = typename Array<T>::const_iterator;

        Array<T> queue;
        HashSet<T> set;
        
        // invariant
        
        void _invariant() const {
            // the queue has no duplicates
            for (auto i = queue.cbegin(); i != queue.cend(); ++i) {
                for (auto j = queue.cbegin(); i != queue.cend(); ++j) {
                    assert((*i == *j) == (i == j));
                }
            }
            // the queue and set represent the same sequence
            assert(queue.size() == set.size());
            // every queue item appears once in the set
            for (const auto& key : queue)
                assert(set.count(key) == 1);
            // every set item appears once in the queue
            for (const auto& key : set)
                assert(std::count(queue.cbegin(), queue.cend(), key) == 1);
        }
        
        // as regular type
        
        QueueOfUnique() = default;
        QueueOfUnique(const QueueOfUnique&) = delete;
        QueueOfUnique(QueueOfUnique&&) = default;
        ~QueueOfUnique() = default;
        QueueOfUnique& operator=(const QueueOfUnique&) = delete;
        QueueOfUnique& operator=(QueueOfUnique&&) = default;

        QueueOfUnique(Array<T>&& a, HashSet<T>&& b)
        : queue(std::move(a))
        , set(std::move(b)) {
        }
        
        std::pair<Queue<T>, HashSet<T>> destructure() {
            return std::pair<Queue<T>, HashSet<T>>(std::move(queue),
                                                   std::move(set));
        }
        
        void swap(QueueOfUnique& other) {
            queue.swap(other.queue);
            set.swap(other.set);
        }
        
        // as immutable sequence
        
        const_iterator begin() const { return queue.begin(); }
        const_iterator end() const { return queue.end(); }
        const_iterator cbegin() const { return queue.cbegin(); }
        const_iterator cend() const { return queue.cend(); }
        const_reference front() const { return queue.front(); }
        const_reference back() const { return queue.back(); }
        bool empty() const { return queue.empty(); }
        auto size() const { return queue.size(); }
        
        // as STL queue
        
        void push(auto&& key) {
            (void) try_push(std::forward<decltype(key)>(key));
        }
        
        void push_range(QueueOfUnique&& source) {
            // the uniqueness of source can't help us here
            for (auto& value : source.queue)
                push(std::move(value));
            source.clear();
        }
        
        void emplace(auto&&... args) {
            (void) try_emplace(std::forward<decltype(args)>(args)...);
        }
        
        void pop() {
            set.erase(queue.front());
            queue.pop_front();
        }
                                                        
        // as STL set

        void clear() {
            queue.clear();
            set.clear();
        }
        
        size_type count(auto&& value) {
            return set.count(std::forward<decltype(value)>(value));
        }
        
        bool contains(auto&& value) {
            return set.contains(std::forward<decltype(value)>(value));
        }

        // as extended queue
        
        void pop_into(T& victim) {
            victim = std::move(queue.front());
            queue.pop_front();
            set.erase(victim);
        }
        
        T take_one() {
            T value{std::move(queue.front())};
            queue.pop_front();
            set.erase(value);
            return value;
        }
        
        bool try_push(auto&& value) {
            auto [_, flag] = set.insert(value);
            if (flag) {
                queue.push_back(std::forward<decltype(value)>(value));
            }
            return flag;
        }
        
        bool try_emplace(auto&&... args) {
            return try_push(T{std::forward<decltype(args)>(args)...});
        }
                
        bool try_pop_into(T& victim) {
            if (queue.empty())
                return false;
            pop_into(victim);
        }
        
    };
    
} // namespace wry

#endif /* queue_hpp */
