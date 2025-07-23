//
//  arena_allocator.hpp
//  client
//
//  Created by Antony Searle on 22/7/2025.
//

#ifndef arena_allocator_hpp
#define arena_allocator_hpp

#include <bit>
#include <memory>
#include "algorithm.hpp"

namespace wry {
    
    namespace _arena_allocator {
        
        struct Node;
        
        constinit inline thread_local Node* _thread_local_node = nullptr;
        constinit inline thread_local Node* _thread_local_head = nullptr;

        struct Node {
            void* _begin;
            void* _end;
            Node* _next;
        };
        
        constexpr inline uintptr_t aligned_down(uintptr_t address, size_t count, size_t alignment) {
            assert(count < address);
            assert(std::has_single_bit((std::size_t)alignment));
            return (address - count) & ~((std::size_t)alignment - 1);
        }
        
        [[nodiscard]] inline Node* make_node_with_capacity(size_t capacity) {
            printf("Arena new chunk\n");
            void* begin = malloc(capacity);
            assert(begin);
            uintptr_t a = (uintptr_t)begin;
            uintptr_t c = a + capacity;
            uintptr_t b = aligned_down(c, sizeof(Node), alignof(Node));
            assert(a < b);
            Node* node = new((void*) b) Node;
            node->_begin = (void*)a;
            node->_end = (void*)node;
            return node;
        }
        
        inline void* allocate_slow(size_t count, size_t alignment, Node* node) {
            if (node) {
                while (node->_next) {
                    node = node->_next;
                    node->_end = node;
                    uintptr_t a = (uintptr_t) node->_begin;
                    uintptr_t c = (uintptr_t) node->_end;
                    uintptr_t b = aligned_down(c, count, alignment);
                    if (!(b < a)) {
                        return node->_end = (void*)b;
                    }
                }
            }
            size_t capacity = std::bit_ceil(std::max(count + sizeof(Node), (size_t)1 << 20));
            Node* node2 = make_node_with_capacity(capacity);
            if (node) {
                node->_next = node2;
            } else {
                _thread_local_head = node2;
            }
            node = node2;
            _thread_local_node = node;
            uintptr_t a = (uintptr_t) node->_begin;
            uintptr_t c = (uintptr_t) node->_end;
            uintptr_t b = aligned_down(c, count, alignment);
            assert(!(b < a));
            return node->_end = (void*)b;
        }
        
        inline void* allocate(size_t count, size_t alignment) {
            Node* node = _thread_local_head;
            if (node) {
                uintptr_t a = (uintptr_t) node->_begin;
                uintptr_t c = (uintptr_t) node->_end;
                uintptr_t b = aligned_down(c, count, alignment);
                if (!(b < a)) {
                    return node->_end = (void*)b;
                }
            }
            return allocate_slow(count, alignment, node);
        }
        
        inline void reset() {
            Node* node = _thread_local_head;
            if (node) {
                node->_end = node;
            }
            _thread_local_node = node;
        }
        
        

        
        
                
        struct ArenaAllocated {
            static void* operator new(std::size_t size, std::align_val_t align) {
                return allocate(size, (size_t)align);
            }
            static void* operator new(std::size_t size, void* ptr) {
                return ptr;
            }
            static void operator delete(void*) {
                // no-op
            }
        };
        
    } // namespace _arena_allocator
    
    using _arena_allocator::ArenaAllocated;
    
} // namespace wry
#endif /* arena_allocator_hpp */
