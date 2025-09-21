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

#include <concepts>
#include <new>
#include <bit>

#include "atomic.hpp"
#include "garbage_collected.hpp"

namespace wry {
    
    constexpr size_t CACHE_LINE_BYTES = 64;
    
    namespace _work_stealing_queue {
                
        template<AlwaysLockFreeAtomic T>
        struct CircularArray : GarbageCollected {
            
            size_t _mask;
            mutable Atomic<T> _data[0];
            
            size_t capacity() const { return _mask + 1; }
            
            explicit CircularArray(size_t mask) : _mask(mask) {
                assert(std::has_single_bit(_mask + 1));
            }
            
            static CircularArray* make(size_t capacity) {
                void* raw = calloc(sizeof(CircularArray) + sizeof(T) * capacity, 1);
                size_t mask = capacity - 1;
                return new(raw) CircularArray(mask);
            }
            
            Atomic<T>& operator[](size_t i) const {
                return _data[i & _mask];
            }
            
        }; // struct CircularArray<AlwaysLockFreeAtomic T>
        
        
        // Nhat Minh Lê, Antoniu Pop, Albert Cohen, Francesco Zappa Nardelli.
        // Correct and Efficient WorkStealing for Weak Memory Models.
        // PPoPP ’13 - Proceedings of the 18th ACM SIGPLAN symposium on
        // Principles and practice of parallel programming, Feb 2013, Shenzhen,
        // China. pp.69-80, ff10.1145/2442516.2442524ff. ffhal-00802885f
        
        template<AlwaysLockFreeAtomic T>
        struct WorkStealingQueue {
            
            alignas(CACHE_LINE_BYTES) struct {
                mutable Atomic<const CircularArray<T>*> _array;
                mutable Atomic<ptrdiff_t> _bottom;
                mutable ptrdiff_t _cached_top;
            };
            
            alignas(CACHE_LINE_BYTES) struct {
                mutable Atomic<ptrdiff_t> _top;
            };
            
            explicit WorkStealingQueue(const CircularArray<T>* array)
            : _array(array)
            , _bottom(0)
            , _cached_top(0)
            , _top(0) {
            }
            
            explicit WorkStealingQueue(size_t capacity)
            : WorkStealingQueue(CircularArray<T>::make(capacity)) {
            }
            
            WorkStealingQueue()
            : WorkStealingQueue(16) {
            }
            
            void push(T item) const {
                const CircularArray<T>* array = this->_array.load(Ordering::RELAXED);
                ptrdiff_t bottom = this->_bottom.load(Ordering::RELAXED);
                ptrdiff_t capacity = array->capacity();
                assert(bottom - _cached_top <= capacity);
                if (bottom - _cached_top == capacity) {
                    // we may be out of space; refresh our knowledge of top
                    _cached_top = this->_top.load(Ordering::SEQ_CST);
                    assert(bottom - _cached_top <= capacity);
                    if (bottom - _cached_top == capacity) {
                        // we are out of space; expand the array
                        CircularArray<T>* new_array = CircularArray<T>::make(capacity << 1);
                        for (ptrdiff_t i = _cached_top; i != bottom; ++i)
                            (*new_array)[i].store((*array)[i].load(Ordering::RELAXED), Ordering::RELAXED);
                        array->garbage_collected_shade();
                        new_array->garbage_collected_shade();
                        _array.store(new_array, Ordering::RELEASE);
                        array = new_array;
                    }
                }
                (*array)[bottom].store(item, Ordering::RELAXED);
                _bottom.store(bottom + 1, Ordering::RELEASE);
            }
            
            bool pop(T& item) const {
                ptrdiff_t bottom = this->_bottom.load(Ordering::RELAXED);
                ptrdiff_t new_bottom = bottom - 1;
                
                _bottom.store(new_bottom, Ordering::RELAXED);
                ptrdiff_t _cached_top = _top.load(Ordering::SEQ_CST);
                
                assert(_cached_top <= bottom);
                ptrdiff_t new_top = _cached_top + 1;
                ptrdiff_t new_size = new_bottom - _cached_top;
                if (new_size < 0) {
                    // the queue had no items
                    _bottom.store(bottom, Ordering::RELAXED);
                    return false;
                }
                const CircularArray<T>* array = this->_array.load(Ordering::RELAXED);
                // speculative load
                item = (*array)[new_bottom].load(Ordering::RELAXED);
                if (new_size > 0) {
                    // the queue had multiple items
                    return true;
                }
                assert(new_size == 0);
                // the queue had one item
                // we race try_pop_front for the last element
                bool success = this->_top.compare_exchange_weak(_cached_top,
                                                                new_top,
                                                                Ordering::SEQ_CST,
                                                                Ordering::RELAXED);
                assert(bottom == new_top);
                _bottom.store(bottom, Ordering::RELAXED);
                return success;
            }
            
            bool steal(T& item) const {
                ptrdiff_t top = _top.load(Ordering::SEQ_CST);
                ptrdiff_t bottom = _bottom.load(Ordering::ACQUIRE);
                if (!(top < bottom))
                    return false;
                const CircularArray<T>* array = _array.load(Ordering::ACQUIRE);
                // speculative load
                item = (*array)[top].load(Ordering::RELAXED);
                ptrdiff_t new_top = top + 1;
                // try to claim the right to actually look at item
                return _top.compare_exchange_weak(top,
                                                  new_top,
                                                  Ordering::SEQ_CST,
                                                  Ordering::RELAXED);
            }
            
            
            
        }; // struct WorkStealingQueue<AlwaysLockFreeAtomic T>
        
        
    } // namespace _work_stealing_queue
    
    using _work_stealing_queue::WorkStealingQueue;
    
} // namespace wry

#endif /* work_stealing_queue_hpp */
