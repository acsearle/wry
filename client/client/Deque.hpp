//
//  Deque.hpp
//  client
//
//  Created by Antony Searle on 28/6/2024.
//

#ifndef Deque_hpp
#define Deque_hpp

#include <cassert>

#include "object.hpp"
#include "Scan.hpp"

namespace wry {

    template<typename T>
    struct Deque {
        
        struct alignas(4096) Page : gc::Object {
            
            constexpr static size_t CAPACITY = (4096 - sizeof(gc::Object) - sizeof(Page*) - sizeof(Page*)) / sizeof(T*);

            gc::Scan<Page*> prev;
            T elements[CAPACITY];
            gc::Scan<Page*> next;
            
            Page(Page* prev, Page* next)
            : prev(prev), next(next) {
            }
            
            T* begin() { return elements; }
            T* end() { return elements + CAPACITY; }
            
            void _object_scan() const override {
                _object_trace(prev);
                for (const T& e : elements)
                    _object_trace(e);
                _object_trace(next);
            }
            
        };
        
        static_assert(sizeof(Page) == 4096);
        static constexpr uintptr_t MASK = -4096;

        T* _begin;
        T* _end;
        size_t _size;
        
        static Page* _page(T* p) {
            return (Page*)((uintptr_t)p & MASK);
        }

        void _assert_invariant() const {
            assert(!_begin == !_end);
            assert(!_begin || _begin != _page(_begin)->end());
            assert(!_end || _end != _page(_end)->begin());
            assert((_page(_begin) != _page(_end)) || (_size == _end - _begin));
        }
        
                
        Page* _initialize() {
            assert(!_begin);
            assert(!_end);
            assert(!_size);
            Page* q = new Page(nullptr, nullptr);
            q->next = q;
            q->prev = q;
            _begin = q->elements + (Page::CAPACITY >> 1);
            _end = _begin;
            return q;
        }
                                
        void push_back(auto&& value) {
            Page* q = _page(_end);
            if (!_end) {
                q = _initialize();
            } else if (_end == q->end()) {
                // page has no free space at end
                Page* p = _pp(_begin);
                assert(p);
                if (q->next == p) {
                    // loop has no free page at end
                    Page* r = new Page(q, p);
                    p->prev = r;
                    q->next = r;
                }
                q = q->next;
                _end = q->begin();
            }
            *_end++ = std::forward<decltype(value)>(value);
            ++_size;
            assert(_end != q->begin());
        }
        
        void push_front(auto&& value) {
            Page* p = _page(_begin);
            if (!_begin) {
                p = _initialize();
            } else if (_begin == p->begin()) {
                Page* q = _pp(_end);
                assert(q);
                if (p->prev == q) {
                    Page* r = new Page(q, p);
                    p->prev = r;
                    q->next = r;
                }
                p = p->prev;
                _begin = p->end();
            }
            *--_begin = std::forward<decltype(value)>(value);
            assert(_begin != p->end());
        }
        
        void pop_back() {
            assert(_size);
            Page* q = _page(_end);
            assert(_end != q->begin());
            --_end;
            --_size;
            if (_end == q->begin()) {
                _end = q->prev->end();
            }
        }
        
        
        void pop_front() {
            Page* p = _page(_begin);
            assert(_size);
            assert(_begin != p->end());
            ++_begin;
            --_size;
            if (_begin == p->end()) {
                p = p->next;
                _begin = p->begin();
            }
        }
        
        T& front() {
            assert(_size);
            assert(_begin != _page(_begin)->end());
            return *_begin;
        }
        
        
        const T& front() const {
            assert(_size);
            assert(_begin != _page(_begin)->end());
            return *_begin;
        }
        
        T& back() {
            assert(_size);
            Page* p = _page(_end);
            assert(_end != p->begin());
            return *(_end - 1);
        }

        const T& back() const {
            assert(_size);
            Page* p = _page(_end);
            assert(_end != p->begin());
            return *(_end - 1);
        }
        
    }; // struct Deque<T>
    
} // namespace gc

#endif /* Deque_hpp */
