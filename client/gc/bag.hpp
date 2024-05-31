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
#include <cstdio>
#include <utility>

namespace gc {
    
    // Bag is unordered storage optimized to make the mutator's common
    // operations cheap
    //
    // True O(1) push to log new pointers
    // True O(1) splice to combine logs
    
    // TODO: if we splice the other way around we are actually a LIFO
    // stack
    // TODO: if we add some more pointers we are actually a deque
    //
    // Compared with gch::deque we need to have each Slab maintain its own
    // begin and end, maybe wrapping?

    
    template<typename T> 
    struct Bag;
    
    template<typename T>
    struct Bag<T*> {
        
        struct Slab {
            
            constexpr static std::size_t CAPACITY = (4096 - 16) / sizeof(T*);
            
            Slab* next;
            std::size_t count;
            T* elements[CAPACITY];
            
            Slab(Slab* next, T* item) {
                this->next = next;
                count = 1;
                elements[0] = item;
            }
            
            std::size_t size() const { return count; }
            bool empty() const { return !count; }
            bool full() const { return count == CAPACITY; }
            
            T* const& top() const {
                assert(!empty());
                return elements[count - 1];
            }

            T* & top() {
                assert(!empty());
                return elements[count - 1];
            }

            void pop() {
                assert(!empty());
                --count;
                // no dtor
            }
            
            void push(T* x) {
                assert(!full());
                elements[count++] = std::move(x);
            }
            
        };
        
        static_assert(sizeof(Slab) == 4096);

        Slab* head;
        Slab* tail;
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
        
        T* const& top() const {
            assert(count);
            Slab* a = head;
            for (;;) {
                assert(a);
                if (a->count)
                    return a->elements[a->count - 1];
                a = a->next;
            }
        }
        
        T*& top() {
            assert(count);
            for (;;) {
                assert(head);
                if (head->count)
                    return head->elements[head->count - 1];
                delete std::exchange(head, head->next);
            }
        }

        void push(T* x) {
            printf("pushing %p\n", x);
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
        

        [[nodiscard]] T* pop() {
            if (!count) {
                printf("popped %p\n", nullptr);
                return nullptr;
            }
            --count;
            for (;;) {
                assert(head);
                if (head->count) {
                    T* p = head->elements[--head->count];
                    printf("popped %p\n", p);
                    return p;
                }
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
        
        std::size_t size() const {
            return count;
        }
        
        
        
        
        /*
        // do we ever iterate or do we just pop everything?
        
        struct iterator {
            
            Slab* slab;
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
            
            T*& operator*() const {
                assert(slab && index != slab->count);
                return slab->elements[index];
            }
            
        };
         */

    }; // struct Bag<T*>
    
    
    
    template<typename T>
    struct Deque {
        struct alignas(4096) Slab {
            static constexpr std::size_t CAPACITY = 4064 / sizeof(T);
            T* _begin;
            T* _end;
            Slab* _prev;
            Slab* _next;
            T _elements[CAPACITY];
            
            void assert_invariant() {
                assert(_elements <= _begin);
                assert(_begin <= _end);
                assert(_end <= _elements + CAPACITY);
            }
            
            explicit Slab(T value, Slab* prev, Slab* next)
            : _begin(_elements)
            , _end(_elements + 1)
            , _prev(prev)
            , _next(next){
                _elements[0] = value;
            }
            
            bool can_push_back() const {
                return _end != _elements + CAPACITY;
            }
            
            bool can_push_front() const {
                return _elements != _begin;
            }
            
            bool can_pop() const {
                return _begin != _end;
            }
            
            void push_back(T x) {
                *_end++ = x;
            }
            
            void push_front(T x) {
                *--_begin = x;
            }
            
            void pop_front() {
                ++_begin;
            }
            
            void pop_back() {
                --_end;
            }
            
            T& front() {
                return *_begin;
            }
            
            T& back() {
                return *(_end - 1);
            }
            
        };
        
        static_assert(sizeof(Slab) == 4096);
        
        Slab* _head;
        Slab* _tail;
        std::size_t _count;
        
        void assert_invariant() {
            assert(!_head == !_tail);
            if (_head)
                for (Slab* a = _head; a != _tail; a = a->_next)
                    a->assert_invariant();
        }
        
        void push_back(T value) {
            ++_count;
            assert(!_head == !_tail);
            if (!_tail || _tail->can_push_back()) {
                _tail = new Slab(std::move(value), _tail, nullptr);
                if (!_head)
                    _head = _tail;
            } else {
                _tail->push_back(value);
            }
        }
        
        void push_front(T value) {
            ++_count;
            assert(!_head == !_tail);
            if (!_head || _head->can_push_front()) {
                _tail = new Slab(std::move(value), _tail, nullptr);
                if (!_head)
                    _head = _tail;
            } else {
                _tail->push_back(value);
            }
        }
        
    };
    
    
    
    
    
    // We can make a concurrent deque by:
    
    // Two trieber stacks for front and back
    // Reversed copies of each that are incrementally constructed so as to
    // be ready when needed to be grabbed by the other side
    //
    // 
    
} // namespace gc

#endif /* bag_hpp */
