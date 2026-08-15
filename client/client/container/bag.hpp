//
//  bag.hpp
//  client
//
//  Created by Antony Searle on 17/7/2025.
//

#ifndef bag_hpp
#define bag_hpp

#include <cstdio>
#include <type_traits>
#include <pthread.h>   // TEMP (2026-08-15): thread-reap diagnostics in ~Bag
#include "utility.hpp"

namespace wry {
    
    // A simple and fast unordered collection for plain old data types,
    // implemented as an unrolled linked list.  Push and pop are usually
    // trivial, and in the worst case still O(1), so this data structure is
    // somewhat suitable for soft-real-time contexts.

    // Used by the garbage collector to receive and manage pointers.  The
    // bag nodes are not themselves garbage collected.

    // Performance hazard: Push/pop at a chunk boundary will thrash node
    // creation and destruction

    // Naming: Merging is important.  We currently concatenate singly linked
    // lists; this requires an object that manages the tail.  It also means that
    // it's not guaranteed that internal nodes are full.

    // TODO: Consider flexible array member instead of finessing the struct
    
    enum : std::size_t { BAG_PAGE_SIZE = 4096 };

    struct poisoned_t { explicit poisoned_t() = default; };
    inline constexpr poisoned_t poisoned{};

    template<typename T>
    struct SinglyLinkedListOfInlineStacksBag {
        
        struct Node {

            static_assert(BAG_PAGE_SIZE - 16 >= sizeof(T));
            constexpr static size_t CAPACITY = (BAG_PAGE_SIZE - 16) / sizeof(T);

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
        size_t _size = 0;

        void swap(SinglyLinkedListOfInlineStacksBag& other) {
            using std::swap;
            swap(_head, other._head);
            swap(_tail, other._tail);
            swap(_size, other._size);
        }

        // constexpr constructor permits use as a constinit thread_local
        constexpr SinglyLinkedListOfInlineStacksBag()
        : _head(nullptr)
        , _tail(nullptr)
        , _size(0)
        {
        }

        constexpr SinglyLinkedListOfInlineStacksBag(poisoned_t)
        : _head((Node*)alignof(Node*)) // <-- probably doesn't work?
        , _tail(nullptr)
        , _size(0)
        {
        }

        SinglyLinkedListOfInlineStacksBag(const SinglyLinkedListOfInlineStacksBag&) = delete;
        
        SinglyLinkedListOfInlineStacksBag(SinglyLinkedListOfInlineStacksBag&& other)
        : _head(std::exchange(other._head, nullptr))
        , _tail(std::exchange(other._tail, nullptr))
        , _size(std::exchange(other._size, 0))
        {
        }
        
        ~SinglyLinkedListOfInlineStacksBag() {
            // The destructor does no work; it is purely a tripwire.  A
            // nonempty bag at destruction is a lost mutator report (first
            // seen as a rare abort when libdispatch reaped an idle thread
            // whose TLS Root destructor shaded after its final report,
            // 2026-08).  Narrate before asserting so any occurrence
            // identifies the thread and the stranded pointers.
            if (_head || _tail || _size) {
                char name[64] = {};
                pthread_getname_np(pthread_self(), name, sizeof name);
                size_t nodes = 0;
                for (Node* n = _head; n; n = n->_next)
                    ++nodes;
                fprintf(stderr,
                        "WRY-BAG-LEAK: ~Bag %p on thread \"%s\" (%p): "
                        "size=%zu nodes=%zu\n",
                        (void*)this, name, (void*)pthread_self(),
                        _size, nodes);
                if constexpr (std::is_pointer_v<T>) {
                    size_t printed = 0;
                    for (Node* n = _head; n && printed < 8; n = n->_next)
                        for (size_t i = 0; i < n->_size && printed < 8; ++i, ++printed)
                            fprintf(stderr, "WRY-BAG-LEAK:   [%zu] %p\n",
                                    printed, (const void*)n->_elements[i]);
                }
            }
            // END TEMP
            assert(_head == nullptr);
            assert(_tail == nullptr);
            assert(_size == 0);
        }
                
        SinglyLinkedListOfInlineStacksBag& operator=(const SinglyLinkedListOfInlineStacksBag&) = delete;
        SinglyLinkedListOfInlineStacksBag& operator=(SinglyLinkedListOfInlineStacksBag&&) = delete;

        
        // The count is maintained, not computed, so these are production
        // API (the collector's receive and sweep-fold branch on emptiness).
        // Note that _head != nullptr does not imply nonempty: a pop can
        // leave an exhausted node in place.
        bool is_empty() const {
            return !_size;
        }

        size_t size() const {
            return _size;
        }

        void push(T value) {
            ++_size;
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
                    --_size;
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
                _size += std::exchange(other._size, 0);
            }
        }
        
        void leak() {
            _head = nullptr;
            _tail = nullptr;
            _size = 0;
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
