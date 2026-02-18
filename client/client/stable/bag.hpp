//
//  bag.hpp
//  client
//
//  Created by Antony Searle on 17/7/2025.
//

#ifndef bag_hpp
#define bag_hpp

#include <cstdio>
#include "utility.hpp"

namespace wry {
    
    // A simple and fast unordered collection for plain old data types,
    // implemented as an unrolled linked list.  Push and pop are usually
    // trivial, and in the worst case still O(1), so this data structure is
    // suitable for real-time contexts.

    // Used by the garbage collector to receive and manage pointers.  The
    // bag nodes are not themselves garbage collected.
        
    template<typename T>
    struct SinglyLinkedListOfInlineStacksBag {
        
        struct Node {
            
            constexpr static size_t CAPACITY = (4096 - 16) / sizeof(T);
            
            Node* _next = nullptr;
            size_t _size = 0;
            T _elements[CAPACITY];
            
            size_t size() const { return _size; }
            bool is_empty() const { return !_size; }
            bool is_full() const { return _size == CAPACITY; }

            bool try_push(const T& value) {
                bool result = !is_full();
                if (result) {
                    _elements[_size++] = value;
                }
                return result;
            }

            bool try_push(T&& value) {
                bool result = !is_full();
                if (result) {
                    _elements[_size++] = std::move(value);
                }
                return result;
            }
            
            bool try_pop(T& victim) {
                bool result = !is_empty();
                if (result) {
                    victim = std::move(_elements[--_size]);
                }
                return result;
            }

        };
        
        static_assert(sizeof(Node) == 4096);
        
        using value_type = T;
        using size_type = std::size_t;
        using reference = T&;
        using const_reference = T const&;
        
        Node* _head = nullptr;
        Node* _tail = nullptr;
#ifndef NDEBUG
        size_t _debug_size = 0;
#endif // NDEBUG

        void swap(SinglyLinkedListOfInlineStacksBag& other) {
            using std::swap;
            swap(_head, other._head);
            swap(_tail, other._tail);
#ifndef NDEBUG
            swap(_debug_size, other._debug_size);
#endif // NDEBUG
        }

        // constexpr constructor permits use as a constinit thread_local
        constexpr SinglyLinkedListOfInlineStacksBag()
        : _head(nullptr)
        , _tail(nullptr)
#ifndef NDEBUG
        , _debug_size(0) {
#endif // NDEBUG
        }
        
        SinglyLinkedListOfInlineStacksBag(const SinglyLinkedListOfInlineStacksBag&) = delete;
        
        SinglyLinkedListOfInlineStacksBag(SinglyLinkedListOfInlineStacksBag&& other)
        : _head(std::exchange(other._head, nullptr))
        , _tail(std::exchange(other._tail, nullptr))
#ifndef NDEBUG
        , _debug_size(std::exchange(other._debug_size, 0))
#endif // NDEBUG
        {
        }
        
        ~SinglyLinkedListOfInlineStacksBag() {
            assert(_head == nullptr);
            assert(_tail == nullptr);
#ifndef NDEBUG
            assert(_debug_size == 0);
#endif // NDEBUG
        }
                
        SinglyLinkedListOfInlineStacksBag& operator=(const SinglyLinkedListOfInlineStacksBag&) = delete;
        
        SinglyLinkedListOfInlineStacksBag& operator=(SinglyLinkedListOfInlineStacksBag&& other) {
            SinglyLinkedListOfInlineStacksBag(std::move(other)).swap(*this);
            return *this;
        }
        
#ifndef NDEBUG
        bool debug_is_empty() const {
            return !_debug_size;
        }
        
        size_t debug_size() const {
            return _debug_size;
        }
#endif // NDEBUG

        void push(T value) {
#ifndef NDEBUG
            ++_debug_size;
#endif // NDEBUG
            while (!_head || !_head->try_push(value)) {
                Node* node = new Node;
                node->_next = _head;
                node->_size = 0;
                _head = node;
                if (!_tail)
                    _tail = _head;
            }
        }
        
        bool try_pop(T& victim) {
            for (;;) {
                if (!_head)
                    return false;
                if (_head->try_pop(victim)) {
#ifndef NDEBUG
                    --_debug_size;
#endif // NDEBUG
                    return true;
                }
                delete std::exchange(_head, _head->_next);
                if (!_head)
                    _tail = nullptr;
            }
        }
        
        void splice(SinglyLinkedListOfInlineStacksBag&& other) {
            if (other._head) {
                if (_head) {
                    assert(_tail && !(_tail->_next));
                    _tail->_next = exchange(other._head, nullptr);
                } else {
                    assert(!_tail);
                    _head = exchange(other._head, nullptr);
                }
                _tail = exchange(other._tail, nullptr);
#ifndef NDEBUG
                _debug_size += exchange(other._debug_size, 0);
#endif // NDEBUG
            }
        }
        
        void leak() {
            _head = nullptr;
            _tail = nullptr;
#ifndef NDEBUG
            _debug_size = 0;
#endif // NDEBUG
        }
        
    }; // struct SinglyLinkedListOfInlineStacksBag<T>
    
    template<typename T>
    void swap(SinglyLinkedListOfInlineStacksBag<T>& left, SinglyLinkedListOfInlineStacksBag<T>& right) {
        left.swap(right);
    }
    
    template<typename T>
    using Bag = SinglyLinkedListOfInlineStacksBag<T>;
        
}

#endif /* bag_hpp */
