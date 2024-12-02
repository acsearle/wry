//
//  work_stealing_queue.hpp
//  client
//
//  Created by Antony Searle on 24/11/2024.
//

#ifndef work_stealing_queue_hpp
#define work_stealing_queue_hpp

#include <cstddef>
#include <cassert>

#include <atomic>
#include <concepts>
#include <new>
#include <bit>

#include "object.hpp"

namespace wry {
    
    constexpr size_t CACHE_LINE_BYTES = 64;
    
    namespace _work_stealing_queue {
        
        using std::atomic;
        
        template<typename T>
        struct circular_array : gc::Object {
            
            size_t _mask;
            mutable atomic<T> _data[0];
            
            size_t capacity() const { return _mask + 1; }
            
            explicit circular_array(size_t mask) : _mask(mask) {
                assert(std::has_single_bit(_mask + 1));
            }
            
            static circular_array* make(size_t capacity) {
                void* raw = calloc(sizeof(circular_array) + sizeof(intptr_t) * capacity, 1);
                size_t mask = capacity - 1;
                return new(raw) circular_array(mask);
            }
            
            atomic<T>& operator[](size_t i) const {
                return _data[i & _mask];
            }
            
        };
        
        
        // Nhat Minh Lê, Antoniu Pop, Albert Cohen, Francesco Zappa Nardelli.
        // Correct and Efficient WorkStealing for Weak Memory Models.
        // PPoPP ’13 - Proceedings of the 18th ACM SIGPLAN symposium on
        // Principles and practice of parallel programming, Feb 2013, Shenzhen,
        // China. pp.69-80, ff10.1145/2442516.2442524ff. ffhal-00802885f
        
        template<typename T>
        concept AlwaysLockFreeAtomic = std::atomic<T>::is_always_lock_free;
        
        
        template<AlwaysLockFreeAtomic T>
        struct work_stealing_queue {
            
            alignas(CACHE_LINE_BYTES) struct {
                mutable atomic<const circular_array<T>*> _array;
                mutable atomic<ptrdiff_t> _bottom;
                mutable ptrdiff_t _cached_top;
            };
            
            alignas(CACHE_LINE_BYTES) struct {
                mutable atomic<ptrdiff_t> _top;
            };
            
            explicit work_stealing_queue(const circular_array<T>* array)
            : _array(array)
            , _bottom(0)
            , _cached_top(0)
            , _top(0) {
            }
            
            explicit work_stealing_queue(size_t capacity)
            : work_stealing_queue(circular_array<T>::make(capacity)) {
            }
            
            work_stealing_queue()
            : work_stealing_queue(16) {
            }
            
            void push(T item) const {
                const circular_array<T>* array = this->_array.load(std::memory_order_relaxed);
                ptrdiff_t bottom = this->_bottom.load(std::memory_order_relaxed);
                ptrdiff_t capacity = array->capacity();
                assert(bottom - _cached_top <= capacity);
                if (bottom - _cached_top == capacity) {
                    // we may be out of space; refresh our knowledge of top
                    _cached_top = this->_top.load(std::memory_order_seq_cst);
                    assert(bottom - _cached_top <= capacity);
                    if (bottom - _cached_top == capacity) {
                        // we are out of space; expand the array
                        circular_array<T>* new_array = circular_array<T>::make(capacity << 1);
                        for (ptrdiff_t i = _cached_top; i != bottom; ++i)
                            (*new_array)[i].store((*array)[i].load(std::memory_order_relaxed), std::memory_order_relaxed);
                        array->shade();
                        new_array->shade();
                        _array.store(new_array, std::memory_order_release);
                        array = new_array;
                    }
                }
                (*array)[bottom].store(item, std::memory_order_relaxed);
                _bottom.store(bottom + 1, std::memory_order_release);
            }
            
            bool pop(T& item) const {
                ptrdiff_t bottom = this->_bottom.load(std::memory_order_relaxed);
                ptrdiff_t new_bottom = bottom - 1;
                
                _bottom.store(new_bottom, std::memory_order_relaxed);
                ptrdiff_t _cached_top = _top.load(std::memory_order_seq_cst);
                
                assert(_cached_top <= bottom);
                ptrdiff_t new_top = _cached_top + 1;
                ptrdiff_t new_size = new_bottom - _cached_top;
                if (new_size < 0) {
                    // the queue had no items
                    _bottom.store(bottom, std::memory_order_relaxed);
                    return false;
                }
                const circular_array<T>* array = this->_array.load(std::memory_order_relaxed);
                // speculative load
                item = (*array)[new_bottom].load(std::memory_order_relaxed);
                if (new_size > 0) {
                    // the queue had multiple items
                    return true;
                }
                assert(new_size == 0);
                // the queue had one item
                // we race try_pop_front for the last element
                bool success = this->_top.compare_exchange_weak(_cached_top,
                                                                new_top,
                                                                std::memory_order_seq_cst,
                                                                std::memory_order_relaxed);
                assert(bottom == new_top);
                _bottom.store(bottom, std::memory_order_relaxed);
                return success;
            }
            
            bool steal(T& item) const {
                ptrdiff_t top = _top.load(std::memory_order_seq_cst);
                ptrdiff_t bottom = _bottom.load(std::memory_order_acquire);
                if (!(top < bottom))
                    return false;
                const circular_array<T>* array = _array.load(std::memory_order_acquire);
                // speculative load
                item = (*array)[top].load(std::memory_order_relaxed);
                ptrdiff_t new_top = top + 1;
                // try to claim the right to actually look at item
                return _top.compare_exchange_weak(top,
                                                  new_top,
                                                  std::memory_order_seq_cst,
                                                  std::memory_order_relaxed);
            }
            
            
            
        };
        
        
    } // namespace _work_stealing_queue
    
    using _work_stealing_queue::work_stealing_queue;
    
} // namespace wry

#endif /* work_stealing_queue_hpp */
