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
#include "utility.hpp"

// A fast allocator for objects with bounded lifetimes
// ===================================================
//
// Many heap objects have usefully bounded lifetimes; for example, per step
// temporaries produced by the transaction system; per-frame temporaries
// produced by the graphics system.
//
// Allocations will typically require a pointer bump, be adjacent, and be in
// cache.  Slow path jumps to an existing chunk or allocates a new chunk.
//
// The arena can be reset to reuse the memory.  Destructors will not be called.
//
// The allocator is annotated for address sanitizer.
//
// Design issues
// -------------
//
// - Bump up or down? [down]
//   - realloc seems hard to use correctly
// - What strategy should be used to size new chunks? [constant]
//   - constant multiple of page size
//   - exponential growth of size
//   - actually bit_ceil(max($strategy, requested + header))
// - What does fragmentation mean above page size? [open]
//   - Does virtual memory mean that the fragmented resource is address space?
//   - OS specific?
//   - We can reserve large chunks of address space but not alloc them
//     - Count up?
// - When should we free (as opposed to reuse) blocks? [never]
// - Make the heap legible [no]
//   - Technically possible but no compelling use case
// - When can we reset the arena [?]
//   - requires tight coupling of threads
//   - One option is to hand off the old arena to the garbage collector, which
//     will keep it alive until all threads have passed a handshake
// - Implicit thread_local or explicit handle for different arenas?
//
// Reference
// ---------
//
// - Bump allocation
//   - https://docs.rs/bumpalo/latest/bumpalo/
//   - https://fitzgen.com/2019/11/01/always-bump-downwards.html
//   - https://coredumped.dev/2024/03/25/bump-allocation-up-or-down/
// - Arena allocation
//   - https://www.rfleury.com/p/untangling-lifetimes-the-arena-allocator
//   - https://nullprogram.com/blog/2023/09/27/
//   - 
// - Sanitizer
//   - https://blog.trailofbits.com/2024/09/10/sanitize-your-c-containers-asan-annotations-step-by-step/

namespace wry {
    
    namespace _arena_allocator {
        
        struct Node;
        
        constinit inline thread_local Node* _Nullable _thread_local_head = nullptr;
        constinit inline thread_local Node* _Nullable _thread_local_cursor = nullptr;

        // SAFETY: Bumpalo incurs some overhead to prevent overflows here.  We
        // don't bother since that's an egregious precondition violation and it
        // will swiftly cause a crash anyway.
        constexpr inline uintptr_t aligned_down(uintptr_t address, size_t count, size_t alignment) {
            assert(count < address); // thus address != 0
            assert(std::has_single_bit((std::size_t)alignment));
            return (address - count) & ~((std::size_t)alignment - 1);
        }
        
        // Each Node lives at the end of the chunk it manages.
        struct Node {
            
            void* _Nonnull _begin; // Beginning of chunk allocation
            void* _Nonnull _end;   // End of FREE space
            Node* _Nullable _next; // Next node, if any
            
            bool invariant() {
                uintptr_t a = (uintptr_t) _begin;
                uintptr_t b = (uintptr_t) _end;
                uintptr_t c = (uintptr_t) this;
                return a && b && (a <= b) && c && (a < c) && (b <= c);
            }
            
            void reset() {
                _end = this;
                uintptr_t a = (uintptr_t) _begin;
                uintptr_t b = (uintptr_t) _end;
                ASAN_POISON_MEMORY_REGION((void*) a, b - a);
            }
            
        };
                
        [[nodiscard]] inline Node* _Nonnull make_node_with_capacity(size_t capacity) {
            assert(capacity > sizeof(Node));
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
            node->_next = nullptr;
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
        
        inline void destroy_list() {
            print();
            Node* node = std::exchange(_thread_local_head, nullptr);
            _thread_local_cursor = nullptr;
            while (node) {
                free(std::exchange(node, node->_next)->_begin);
            }
        }
                
    } // namespace _arena_allocator
    
    struct ArenaAllocator {
                
        static void* _Nonnull allocate(std::size_t count, std::size_t alignment) {
            return _arena_allocator::allocate(count, alignment);
        }
        
        static void deallocate(void* _Nullable) {
            // no-op
        }
        
        static void reset() {
            _arena_allocator::reset();
        }

        static void clear() {
            _arena_allocator::destroy_list();
        }
                
    };
        
    struct ArenaAllocated {
        
        static void* _Nonnull operator new(std::size_t size, std::align_val_t align) {
            return ArenaAllocator::allocate(size, (size_t)align);
        }
        
        static void* _Nonnull operator new(std::size_t size, void* _Nonnull ptr) {
            return ptr;
        }
        
        static void operator delete(void* _Nullable) {
        }
        
    };
    

        
} // namespace wry
#endif /* arena_allocator_hpp */
