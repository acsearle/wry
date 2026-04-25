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
    // suitable for soft-real-time contexts.

    // Used by the garbage collector to receive and manage pointers.  The
    // bag nodes are not themselves garbage collected.
    
    // TODO: Consider flexible array member instead of finessing the struct
    
    enum : std::size_t { BAG_PAGE_SIZE = 4096 };
        
    template<typename T>
    struct SinglyLinkedListOfInlineStacksBag {
        
        struct Node {
            
            static void* operator new(std::size_t count) {
                void* ptr = std::aligned_alloc(BAG_PAGE_SIZE, count);
                if (!ptr) [[unlikely]] {
                    abort();
                }
                return ptr;
            }
            
            static void operator delete(void* ptr) {
                std::free(ptr);
            }
            
            static_assert(BAG_PAGE_SIZE - 16 >= sizeof(T));            
            constexpr static size_t CAPACITY = (BAG_PAGE_SIZE - 16) / sizeof(T);
            
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
        
        static_assert(sizeof(Node) == BAG_PAGE_SIZE);
        
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
        SinglyLinkedListOfInlineStacksBag& operator=(SinglyLinkedListOfInlineStacksBag&&) = delete;

        
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
            while (!_head || !_head->try_push(std::move(value))) {
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
                    _tail->_next = std::exchange(other._head, nullptr);
                } else {
                    assert(!_tail);
                    _head = std::exchange(other._head, nullptr);
                }
                _tail = std::exchange(other._tail, nullptr);
#ifndef NDEBUG
                _debug_size += std::exchange(other._debug_size, 0);
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
        
        struct const_iterator {
            
            using iterator_category = std::forward_iterator_tag;
            using value_type = T;
            using difference_type = std::ptrdiff_t;
            using pointer = T*;
            using reference = T&;
            
            Node* _current;
            size_t _index;
            
            T const& operator*() {
                return _current->_elements[_index];
            }
            
            T const* operator->() {
                return _current->_elements + _index;
            }
                        
            const_iterator& operator++() {
                if (++_index == _current->_size) {
                    _current = _current->_next;
                    _index = 0;
                }
                return *this;
            }
            
            const_iterator operator++(int) {
                const_iterator tmp{*this};
                ++*this;
                return tmp;
            }
            
            bool operator==(const const_iterator&) const = default;
            
        };
        
        const_iterator begin() const {
            return const_iterator{_head, 0};
        }
        
        const_iterator end() const {
            return const_iterator{nullptr, 0};
        }

        const_iterator cbegin() const {
            return const_iterator{_head, 0};
        }
        
        const_iterator cend() const {
            return const_iterator{nullptr, 0};
        }

    }; // struct SinglyLinkedListOfInlineStacksBag<T>
    
    template<typename T>
    void swap(SinglyLinkedListOfInlineStacksBag<T>& left, SinglyLinkedListOfInlineStacksBag<T>& right) {
        left.swap(right);
    }
    
    template<typename T>
    using Bag = SinglyLinkedListOfInlineStacksBag<T>;
        
} // namespace wry

#endif /* bag_hpp */
