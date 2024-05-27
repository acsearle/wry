//
//  bag.hpp
//  client
//
//  Created by Antony Searle on 27/5/2024.
//

#ifndef bag_hpp
#define bag_hpp

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <utility>

namespace gc {
    
    // Bag is unordered storage optimized to make the mutator's common
    // operations cheap
    //
    // True O(1) push to log new pointers
    // True O(1) splice to combine logs
    
    template<typename T> 
    struct Bag;
    
    template<typename T>
    struct Bag<T*> {
        
        struct Slab {
            
            constexpr static std::size_t CAPACITY = (4096 - 16) / sizeof(T*);
            
            Slab* __nullable next;
            std::size_t count;
            T* __nonnull elements[CAPACITY];
            
            Slab(Slab* __nullable next, T* __nonnull item) {
                this->next = next;
                count = 1;
                elements[0] = item;
            }
            
            std::size_t size() const { return count; }
            bool empty() const { return !count; }
            bool full() const { return count == CAPACITY; }
            
            T* __nonnull const& top() const {
                assert(!empty());
                return elements[count - 1];
            }

            T* __nonnull& top() {
                assert(!empty());
                return elements[count - 1];
            }

            void pop() {
                assert(!empty());
                --count;
                // no dtor
            }
            
            void push(T* __nonnull x) {
                assert(!full());
                elements[count++] = std::move(x);
            }
            
        };
        
        static_assert(sizeof(Slab) == 4096);

        Slab* __nullable head;
        Slab* __nullable tail;
        std::size_t count;
        
        Bag()
        : head(nullptr)
        , tail(nullptr)
        , count(0) {
        }
        
        Bag(const Bag&) = delete;
        
        Bag(Bag&& other)
        : head(std::exchange(other.head, nullptr))
        , tail(std::exchange(other.tail, nullptr))
        , count(std::exchange(other.count, 0)) {
        }
        
        ~Bag() {
            assert(count == 0);
            while (head) {
                assert(head->empty());
                assert(head->next || head == tail);
                delete std::exchange(head, head->next);
            }
        }
        
        void swap(Bag& other) {
            std::swap(head, other.head);
            std::swap(tail, other.tail);
            std::swap(count, other.count);
        }
        
        Bag& operator=(const Bag&) = delete;
        Bag& operator=(Bag&&) = delete;
        
        T* const __nonnull& top() const {
            assert(count);
            Slab* a = head;
            for (;;) {
                assert(a);
                if (a->count)
                    return a->elements[a->count - 1];
                a = a->next;
            }
        }
        
        T* __nonnull& top() {
            assert(count);
            for (;;) {
                assert(head);
                if (head->count)
                    return head->elements[head->count - 1];
                delete std::exchange(head, head->next);
            }
        }

        void push(T* __nonnull x) {
            ++count;
            assert(!head == !tail);
            if (!head || head->full()) {
                head = new Slab(head, std::move(x));
                if (!tail)
                    tail = head;
                return;
            }
            head->push(std::move(x));
        }
        

        [[nodiscard]] T* __nullable pop() {
            if (!count)
                return nullptr;
            --count;
            for (;;) {
                assert(head);
                if (head->count)
                    return head->elements[--head->count];
                delete std::exchange(head, head->next);
            }
        }
                
        // O(1) splice
        void splice(Bag&& other) {
            if (!other.head)
                return;
            if (!head) {
                head = std::exchange(other.head, nullptr);
                tail = std::exchange(other.tail, nullptr);
                count = std::exchange(other.count, 0);
                return;
            }
            assert(tail->next == nullptr);
            tail->next = std::exchange(other.head, nullptr);
            tail = std::exchange(other.tail, nullptr);
            count += std::exchange(other.count, 0);
        }
        
        bool empty() const {
            return !count;
        }
        
        
        
        // do we ever iterate or do we just pop everything?
        
        struct iterator {
            
            Slab* __nullable slab;
            std::intptr_t index;
            
            iterator& operator++() {
                assert(slab);
                ++index;
                for (;;) {
                    if ((index != slab->count) || (!slab->next))
                        return *this;
                    slab = slab->next;
                    index = 0;
                }
            }
            
            T* __nonnull& operator*() const {
                assert(slab && index != slab->count);
                return slab->elements[index];
            }
            
        };

    }; // struct Bag<T*>
    
} // namespace gc

#endif /* bag_hpp */
