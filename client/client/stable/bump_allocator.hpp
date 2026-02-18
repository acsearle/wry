//
//  bump_allocator.hpp
//  client
//
//  Created by Antony Searle on 13/10/2025.
//

#ifndef bump_allocator_hpp
#define bump_allocator_hpp

#include <sanitizer/asan_interface.h>

#include <bit>
#include <cstdlib>
#include <new>

#include "algorithm.hpp"
#include "assert.hpp"
#include "stddef.hpp"
#include "stdint.hpp"
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
    
    
    namespace bump {
        
        // Single-threaded bump allocator suitable for an arena allocator or
        // as a building block of a thread-safe epochal allocator
        
        constexpr inline auto
        bump_down(intptr_t begin, intptr_t end, size_t count, size_t alignment) -> intptr_t {
            assert(std::has_single_bit(alignment));
            intptr_t new_end = (end - (intptr_t)count) & -(intptr_t)alignment;
            return (new_end >= begin) ? new_end : 0;
        }
        
        struct Slab {
            
            Slab* _Nullable _next;
            unsigned char* _Nonnull const _end;
            unsigned char _data[];
            
            bool invariant() const {
                return _data < _end;
            }
            
            static auto
            make_with_minimum_capacity(std::size_t capacity) -> Slab* _Nonnull
            {
                std::size_t size = std::bit_ceil(sizeof(Slab) + capacity);
                void* _Nonnull ptr = malloc(size);
                unsigned char* _Nonnull end = (unsigned char*)ptr + capacity;
                Slab* _Nonnull that = new(ptr) Slab{
                    nullptr,
                    end,
                };
                return that;
            }
            
        }; // bump::Slab
        
        // TODO:
        // - Bump down or up?
        // - Growth strategy
        // - Separate hot and cold state more?
        
        struct State {
            
            // Free address range of the current slab
            intptr_t _begin;
            intptr_t _end;
            
            // List of slabs and our place in that list
            Slab* _Nullable _cursor;
            Slab* _Nullable _head;
            
            // Some of these fields are hot (touched every allocation) and some
            // are cold (touched only when the current Slab is full).  It may
            // be worth splitting the object up so the former can live in a
            // cache line with other hot thread_local objects
            
            void _configure_memory_region_from_cursor() {
                if (_cursor) {
                    _begin = (intptr_t)(_cursor->_data);
                    _end = (intptr_t)(_cursor->_end);
                    ASAN_POISON_MEMORY_REGION((void*)_begin, (size_t)(_end - _begin));
                } else {
                    _begin = 0;
                    _end = 0;
                }
            }
            
            
            void* _Nonnull _allocate_slow(std::size_t count,
                                          std::size_t alignment)
            {
                for (;;) {
                    // There is no current slab, or the current slab is too
                    // small to be used.
                    
                    // If there is a next slab, install it, else make a new
                    // slab of sufficient capacity
                    
                    if (_cursor && _cursor->_next) {
                        // Use next slab
                        _cursor = _cursor->_next;
                    } else {
                        // Create new slab
                        Slab* _Nonnull tail = Slab::make_with_minimum_capacity(std::max<std::size_t>(count, (1 << 24) - sizeof(Slab)));
                        // printf("BUMP: New slab %zd\n", tail->_end - tail->_data);
                        if (_cursor) {
                            // Link to existing list
                            _cursor->_next = tail;
                        } else {
                            assert(!_head);
                            // Start a new list
                            // printf("BUMP: New list\n");
                            _head = tail;
                        }
                        // Use new slab
                        _cursor = tail;
                    }
                    
                    // The slab is now current but the addresses do not yet
                    // point into it
                    _configure_memory_region_from_cursor();
                    
                    // Try to allocate from the slab
                    intptr_t new_end = bump_down(_begin, _end, count, alignment);
                    if (new_end) [[likely]] {
                        ASAN_UNPOISON_MEMORY_REGION((void*)new_end, count);
                        // printf("BUMP: allocated %zd (slow path)\n", _end - new_end);
                        _end = new_end;
                        return (void*)_end;
                    }
                }
            }
            
            void* _Nonnull allocate(size_t count,
                                    size_t alignment = alignof(std::max_align_t))
            {
                intptr_t new_end = bump_down(_begin, _end, count, alignment);
                if (new_end) [[likely]] {
                    ASAN_UNPOISON_MEMORY_REGION((void*)new_end, count);
                    // printf("BUMP: allocated %zd\n", _end - new_end);
                    _end = new_end;
                    return (void*)_end;
                } else {
                    return _allocate_slow(count, alignment);
                }
            }
            
            void deallocate(void* _Nullable) {
                // no-op
            }
            
            // Poison all the backing memory
            void _asan_poison_all() {
                Slab* _Nullable head = _head;
                while (head) {
                    ASAN_POISON_MEMORY_REGION(head->_data, (size_t)(head->_end - _head->_data));
                    head = head->_next;
                }
            }
            
            // Reuse the backing memory
            void restart() {
                // printf("BUMP: Restart\n");
                _asan_poison_all();
                _cursor = _head;
                _configure_memory_region_from_cursor();
            }
            
            // Deallocate the backing memory
            void teardown() {
                _begin = 0;
                _end = 0;
                _cursor = nullptr;
                Slab* _Nonnull head = std::exchange(_head, nullptr);
                while (head) {
                    free(std::exchange(head, head->_next));
                }
            }
            
            // Swap out the backing memory
            [[nodiscard]] Slab* _Nullable exchange_head_and_restart(Slab* _Nullable desired) {
                Slab* _Nullable result = std::exchange(_head, desired);
                restart();
                return result;
            }
            
        }; // bump::State
        
        inline constinit thread_local State this_thread_state = {};
        
        inline void* _Nonnull allocate(std::size_t count, size_t alignment = alignof(std::max_align_t)) {
            return this_thread_state.allocate(count, alignment);
        }

    } // namespace wry::bump
    
    
    // Base class for BumpAllocated objects
    struct BumpAllocated {

        void* _Nonnull operator new(std::size_t size) {
            return bump::this_thread_state.allocate(size);
        }

        void* _Nonnull operator new(std::size_t size, std::align_val_t align) {
            return bump::this_thread_state.allocate(size, (size_t)align);
        }
        
        void* _Nonnull operator new(std::size_t size, void* _Nonnull ptr) {
            return ptr;
        }

        void operator delete(void* _Nullable) {
            // no-op
        }

    };
    
    // STL-compatible-ish allocator
    template<typename T>
    struct BumpAllocator {
        
        typedef T value_type;
        
        [[nodiscard]] T* _Nonnull allocate(size_t count) const {
            return bump::allocate(count * sizeof(T), alignof(T));
        }
        
        void deallocate(T* _Nullable, size_t) const {
            // no-op
        }
    };
    
    inline void garbage_collected_scan(BumpAllocated const* _Nullable) {
        // no-op
    }
        
} // namespace wry

#endif /* bump_allocator_hpp */
