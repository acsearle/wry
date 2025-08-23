//
//  arena_allocator.hpp
//  client
//
//  Created by Antony Searle on 22/7/2025.
//

#ifndef arena_allocator_hpp
#define arena_allocator_hpp

#include <sanitizer/asan_interface.h>

#include <bit>
#include <memory>
#include "algorithm.hpp"

// Reference:
//
// Bumpalo
//
// https://blog.trailofbits.com/2024/09/10/sanitize-your-c-containers-asan-annotations-step-by-step/

namespace wry {
    
    namespace _arena_allocator {
        
        struct Node;
        
        constinit inline thread_local Node* _Nullable _thread_local_head = nullptr;
        constinit inline thread_local Node* _Nullable _thread_local_cursor = nullptr;

        constexpr inline uintptr_t aligned_down(uintptr_t address, size_t count, size_t alignment) {
            assert(count < address);
            assert(std::has_single_bit((std::size_t)alignment));
            return (address - count) & ~((std::size_t)alignment - 1);
        }
        
        
        struct Node {
            
            void* _Nonnull _begin;
            void* _Nonnull _end;
            Node* _Nullable _next;
            
            void reset() {
                _end = this;
                uintptr_t a = (uintptr_t) _begin;
                uintptr_t c = (uintptr_t) _end;
                ASAN_POISON_MEMORY_REGION((void*) a, c - a);
            }
            
        };
                
        [[nodiscard]] inline Node* _Nonnull make_node_with_capacity(size_t capacity) {
            assert(capacity > sizeof(Node));
            printf("Arena new chunk\n");
            void* _Nonnull begin = malloc(capacity);
            ASAN_POISON_MEMORY_REGION(begin, capacity);
            assert(begin);
            uintptr_t a = (uintptr_t)begin;
            uintptr_t c = a + capacity;
            uintptr_t b = aligned_down(c, sizeof(Node), alignof(Node));
            assert(b >= a);
            ASAN_UNPOISON_MEMORY_REGION((void*) b, c - b);
            Node* node = new((void*) b) Node;
            node->_begin = (void*)a;
            node->_end = (void*)node;
            return node;
        }
        
        inline void* _Nonnull allocate_slow(size_t count, size_t alignment, Node* _Nullable cursor) {
            if (cursor) {
                while (cursor->_next) {
                    // Move cursor to next node
                    cursor = cursor->_next;
                    // Reuse the memory
                    cursor->reset();
                    uintptr_t a = (uintptr_t) cursor->_begin;
                    uintptr_t c = (uintptr_t) cursor->_end;
                    uintptr_t b = aligned_down(c, count, alignment);
                    // Try to use the node
                    if (!(b < a)) {
                        ASAN_UNPOISON_MEMORY_REGION((void*) b, c - b);
                        _thread_local_cursor = cursor;
                        return cursor->_end = (void*)b;
                    }
                }
            }
            // Allocate a new node
            // TODO: is the max sufficient
            // TODO: growth / scaling
            size_t capacity = std::bit_ceil(std::max(count + sizeof(Node), (size_t)1 << 20));
            Node* next = make_node_with_capacity(capacity);
            if (cursor) {
                cursor->_next = next;
            } else {
                _thread_local_head = next;
            }
            cursor = next;
            uintptr_t a = (uintptr_t) cursor->_begin;
            uintptr_t c = (uintptr_t) cursor->_end;
            uintptr_t b = aligned_down(c, count, alignment);
            assert(!(b < a));
            ASAN_UNPOISON_MEMORY_REGION((void*) b, c - b);
            _thread_local_cursor = cursor;
            return cursor->_end = (void*)b;
        }
        
        inline void* _Nonnull allocate(size_t count, size_t alignment) {
            printf("_arena_allocated::allocate(%zu, %zu)\n", count, alignment);
            Node* node = _thread_local_cursor;
            if (node) {
                uintptr_t a = (uintptr_t) node->_begin;
                uintptr_t c = (uintptr_t) node->_end;
                uintptr_t b = aligned_down(c, count, alignment);
                if (!(b < a)) {
                    ASAN_UNPOISON_MEMORY_REGION((void*) b, c - b);
                    return (node->_end = (void*)b);
                }
            }
            return allocate_slow(count, alignment, node);
        }
        
        inline void deallocate(void* _Nonnull addr, size_t size) {
            ASAN_POISON_MEMORY_REGION(addr, size);
        }
        
        inline void print() {
            Node* node = _thread_local_head;
            for (;;) {
                if (!node) {
                    printf("_arena_allocated::Node nullptr\n");
                    return;
                } else {
                    uintptr_t a = (uintptr_t) node->_begin;
                    uintptr_t b = (uintptr_t) node->_end;
                    uintptr_t c = (uintptr_t) node;
                    printf("_arena_allocated::Node free: %zd used: %zd\n", b-a, c-b);
                }
                node = node->_next;
            }
        }
        
        inline void reset() {
            print();
            Node* head = _thread_local_head;
            if (head)
                head->reset();
            _thread_local_cursor = head;
        }
                
                
        struct ArenaAllocated {
            
            static void* _Nonnull operator new(std::size_t size, std::align_val_t align) {
                return allocate(size, (size_t)align);
            }
            
            static void* _Nonnull operator new(std::size_t size, void* _Nonnull ptr) {
                return ptr;
            }
            
            static void operator delete(void* _Nullable) {
                // no-op
            }
            
        };
        
    } // namespace _arena_allocator
    
    using _arena_allocator::ArenaAllocated;
    
} // namespace wry
#endif /* arena_allocator_hpp */
