//
//  bag.hpp
//  client
//
//  Created by Antony Searle on 27/5/2024.
//

#ifndef bag_hpp
#define bag_hpp

#include "utility.hpp"

namespace wry::gc {
    
    // Bag is unordered storage optimized to make the mutator's common
    // operations cheap
    //
    // True O(1) push to append to log
    // True O(1) splice to combine logs
    
    template<typename T>
    struct Bag;
    
    template<typename T>
    struct Bag<T*> {
        
        struct Page {
            
            constexpr static size_t CAPACITY = (4096 - 16) / sizeof(T*);
            
            Page* next;
            size_t count;
            T* elements[CAPACITY];
            
            Page(Page* next, T* item) {
                this->next = next;
                count = 1;
                elements[0] = item;
            }
            
            size_t size() const { return count; }
            bool empty() const { return !count; }
            bool full() const { return count == CAPACITY; }
            
            T*const& top() const {
                assert(!empty());
                return elements[count - 1];
            }

            T*& top() {
                assert(!empty());
                return elements[count - 1];
            }

            void pop() {
                assert(!empty());
                --count;
            }
            
            void push(T* x) {
                assert(!full());
                elements[count++] = std::move(x);
            }
            
        };
        
        static_assert(sizeof(Page) == 4096);
        
        using value_type = T*;
        using size_type = std::size_t;
        using reference = T*&;
        using const_reference = T*const&;
        
        Page* head;
        Page* tail;
        size_t count;
        
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
        
        Bag& operator=(Bag&& other) {
            Bag(std::move(other)).swap(*this);
            return *this;
        }
                        
        T* const& top() const {
            assert(count);
            Page* page = head;
            for (;;) {
                assert(page);
                if (!page->empty())
                    return page->top();
                page = page->next;
            }
        }
        
        T*& top() {
            assert(count);
            for (;;) {
                assert(head);
                if (!head->empty())
                    return head->top();
                delete exchange(head, head->next);
            }
        }
        
        bool empty() const {
            return !count;
        }
        
        size_t size() const {
            return count;
        }

        void push(T* x) {
            ++count;
            assert(!head == !tail);
            if (!head || head->full()) {
                head = new Page(head, std::move(x));
                if (!tail)
                    tail = head;
                return;
            }
            head->push(std::move(x));
        }
        
        void pop() {
            if (!count)
                abort();
            --count;
            for (;;) {
                assert(head);
                if (!head->empty())
                    return head->pop();
                delete exchange(head, head->next);
            }
        }
                
        void splice(Bag&& other) {
            if (other.head) {
                if (head) {
                    assert(tail && !(tail->next));
                    tail->next = exchange(other.head, nullptr);
                } else {
                    assert(!tail && !count);
                    head = exchange(other.head, nullptr);
                }
                tail = exchange(other.tail, nullptr);
                count += exchange(other.count, 0);
            }
        }
        
    }; // struct Bag<T*>
    
    template<typename T>
    void swap(Bag<T*>& left, Bag<T*>& right) {
        left.swap(right);
    }
    
} // namespace wry::gc

#endif /* bag_hpp */
