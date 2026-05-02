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
#include "mutex.hpp"
#include "utility.hpp"

namespace wry {
    
    constexpr size_t CACHE_LINE_BYTES = 64;
    
    namespace _blocking_work_stealing_queue {
        
        template<Relocatable T>
        struct WorkStealingQueue {
            
            std::mutex _mutex;
            T* _data = nullptr;
            size_t _mask = 0;
            size_t _begin = 0;
            size_t _end = 0;
            
            ~WorkStealingQueue() {
                free(_data);
            }
            
            void push(T item) {
                WITH(std::unique_lock guard(_mutex)) {
                    if (_end - _begin == _mask) {
                        size_t new_mask = (_mask << 1) + 1;
                        T* new_data = malloc(sizeof(T) * (new_mask + 1));
                        for (size_t i = _begin; i != _end; ++i)
                            new_data[i & new_mask] = _data[i & _mask];
                        free(_data);
                        _data = new_data;
                        _mask = new_mask;
                    }
                    _data[(_end++) & _mask] = item;
                }
            }
            
            bool try_pop(T& victim) {
                WITH(std::unique_lock guard(_mutex)) {
                    bool result = _end != _begin;
                    if (result)
                        victim = std::move(_data[(--_end) & _mask]);
                    return result;
                }
            }
            
            bool try_steal(T& victim) {
                WITH(std::unique_lock guard(_mutex)) {
                    bool result = _end != _begin;
                    if (result)
                        victim = std::move(_data[(_begin++) & _mask]);
                    return result;
                }
            }
                        
                        
        }; // WorkStealingQueue
        
    } // namespace _blocking_work_stealing_queue
    
    namespace _lockfree_work_stealing_queue {
                
        template<AlwaysLockFreeAtomic T>
        struct CircularWeakArray : GarbageCollected {
            
            size_t _mask;
            mutable Atomic<T> _data[];
            
            size_t capacity() const { return _mask + 1; }
            
            explicit CircularWeakArray(size_t mask) : _mask(mask) {
                assert(std::has_single_bit(_mask + 1));
            }
            
            static CircularWeakArray* make(size_t capacity) {
                void* raw = calloc(sizeof(CircularWeakArray) + sizeof(T) * capacity, 1);
                size_t mask = capacity - 1;
                return new(raw) CircularWeakArray(mask);
            }
            
            Atomic<T>& operator[](size_t i) const {
                return _data[i & _mask];
            }
            
            virtual void _garbage_collected_scan() const override { /* weak */ }
            
        }; // struct CircularWeakArray<AlwaysLockFreeAtomic T>
        
        
        // Nhat Minh Lê, Antoniu Pop, Albert Cohen, Francesco Zappa Nardelli.
        // Correct and Efficient WorkStealing for Weak Memory Models.
        // PPoPP ’13 - Proceedings of the 18th ACM SIGPLAN symposium on
        // Principles and practice of parallel programming, Feb 2013, Shenzhen,
        // China. pp.69-80, ff10.1145/2442516.2442524ff. ffhal-00802885f
        
        template<AlwaysLockFreeAtomic T>
        struct WorkStealingQueue {
            
            alignas(CACHE_LINE_BYTES) struct {
                mutable Atomic<CircularWeakArray<T> const*> _array;
                mutable Atomic<ptrdiff_t> _bottom;
                mutable ptrdiff_t _cached_top;
            };
            
            alignas(CACHE_LINE_BYTES) struct {
                mutable Atomic<ptrdiff_t> _top;
            };
            
            explicit WorkStealingQueue(const CircularWeakArray<T>* array)
            : _array(array)
            , _bottom(0)
            , _cached_top(0)
            , _top(0) {
            }
            
            explicit WorkStealingQueue(size_t capacity)
            : WorkStealingQueue(CircularWeakArray<T>::make(capacity)) {
            }
            
            WorkStealingQueue()
            : WorkStealingQueue(16) {
            }
            
            void push(T item) const {
                CircularWeakArray<T> const* array = this->_array.load_relaxed();
                ptrdiff_t bottom = this->_bottom.load_relaxed();
                ptrdiff_t capacity = array->capacity();
                assert(bottom - _cached_top <= capacity);
                if (bottom - _cached_top == capacity) {
                    // we may be out of space; refresh our knowledge of top
                    _cached_top = this->_top.load_seq_cst();
                    assert(bottom - _cached_top <= capacity);
                    if (bottom - _cached_top == capacity) {
                        // we are out of space; expand the array
                        CircularWeakArray<T>* new_array = CircularWeakArray<T>::make(capacity << 1);
                        for (ptrdiff_t i = _cached_top; i != bottom; ++i)
                            (*new_array)[i].store_relaxed((*array)[i].load_relaxed());
                        _array.store_release(new_array);
                        array = new_array;
                    }
                }
                (*array)[bottom].store_relaxed(item);
                _bottom.store_release(bottom + 1);
            }
            
            bool try_pop(T& item) const {
                ptrdiff_t bottom = this->_bottom.load_relaxed();
                ptrdiff_t new_bottom = bottom - 1;
                
                _bottom.store_relaxed(new_bottom);
                ptrdiff_t _cached_top = _top.load_seq_cst();
                
                assert(_cached_top <= bottom);
                ptrdiff_t new_top = _cached_top + 1;
                ptrdiff_t new_size = new_bottom - _cached_top;
                if (new_size < 0) {
                    // the queue had no items
                    _bottom.store_relaxed(bottom);
                    return false;
                }
                CircularWeakArray<T> const* array = this->_array.load_relaxed();
                // speculative load
                item = (*array)[new_bottom].load_relaxed();
                if (new_size > 0) {
                    // the queue had multiple items
                    return true;
                }
                assert(new_size == 0);
                // the queue had one item
                // we race try_pop_front for the last element
                bool success = this->_top.compare_exchange_weak_seq_cst_relaxed(_cached_top,
                                                                                new_top);
                assert(bottom == new_top);
                _bottom.store_relaxed(bottom);
                return success;
            }
            
            bool try_steal(T& item) const {
                ptrdiff_t top = _top.load_seq_cst();
                ptrdiff_t bottom = _bottom.load_acquire();
                if (!(top < bottom))
                    return false;
                CircularWeakArray<T> const* array = _array.load_acquire();
                // speculative load
                item = (*array)[top].load_relaxed();
                ptrdiff_t new_top = top + 1;
                // try to claim the right to actually look at item
                return _top.compare_exchange_weak_seq_cst_relaxed(top,
                                                                  new_top);
            }
            
            
            
        }; // struct WorkStealingQueue<AlwaysLockFreeAtomic T>
        
    } // namespace _locking_work_stealing_queue
    
    using _lockfree_work_stealing_queue::WorkStealingQueue;
    
} // namespace wry

#endif /* work_stealing_queue_hpp */
