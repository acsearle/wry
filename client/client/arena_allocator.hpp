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
        
        struct ArenaAllocator;
        
        
        struct ArenaAllocator {
                    
            constinit inline static thread_local ArenaAllocator* _head = nullptr;
            
            intptr_t _address;
            intptr_t _end;
            ArenaAllocator* _next;
            unsigned char _start[0];
            
            void* _try_allocate(size_t count) {
                intptr_t new_address = (_address - count) & ~(alignof(max_align_t)-1);
                intptr_t start = (intptr_t)(_start);
                if (new_address >= start) {
                    _address = new_address;
                    return (void*)new_address;
                } else {
                    return nullptr;
                }
            }
            
            static ArenaAllocator* _make_arena_allocator(size_t count) {
                count = std::bit_ceil(count);
                void* a = malloc(count);
                intptr_t b = (intptr_t)a;
                ArenaAllocator* self = new(a) ArenaAllocator;
                self->_address = self->_end = b + count;
                self->_next = nullptr;
                return self;
            }

            static void* allocate(size_t count) {
                ArenaAllocator* p = _head;
                if (p) {
                    void* q = p->_try_allocate(count);
                    if (q)
                        return q;
                }
                return _allocate_slow(p, count);
            }
            
            static void* _allocate_slow(ArenaAllocator* p, size_t count) {
                if (!p) {
                    // no slabs are present on this thread
                    size_t n = (count + sizeof(ArenaAllocator)) | ((size_t)1 << 20);
                    p = _make_arena_allocator(n);
                    _head = p;
                } else {
                    // The slab is full
                    ArenaAllocator* q = p->_next;
                    if (!q) {
                        q = _make_arena_allocator((count + sizeof(ArenaAllocator)) | ((size_t)1 << 20));
                    }
                    // We now need to retire p
                    
                    // Reset its address, since the next time we encounter it
                    // will be when we reset the arena
                    p->_address = p->_end;
                    
                    // Install q for future requests
                    // Stash p somewhere for reuse in the next epoch
                }
                abort();
                return nullptr;
            }


                        
            
        };

                
        struct ArenaAllocated {
            static void* operator new(size_t size);
            static void* operator new(size_t size, void*);
            static void operator delete(void*) {
                // no-op
            }
        };
        
    } // namespace _arena_allocator
    
} // namespace wry
#endif /* arena_allocator_hpp */
