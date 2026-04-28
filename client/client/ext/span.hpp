//
//  span.hpp
//  client
//
//  Created by Antony Searle on 26/3/2026.
//

#ifndef span_hpp
#define span_hpp


#include <span>

#include "assert.hpp"
#include "type_traits.hpp"

namespace wry {

    // TODO: Yet Another range type.  Resolve with siblings
    
    // NOTE: The mutability of the span object is orthogonal to the mutability
    // of the data it is a view of

    template<typename T>
    struct span {
        
        T* _begin;
        T* _end;
        
        span() : _begin{}, _end{} {}
        span(span const&) = default;
        span(T* p, T* q) : _begin(p), _end(q) { CHECK(p <= q); }
        span(T* p, std::size_t n) : _begin(p), _end(p + n) {}
        
        // const
        
        [[nodiscard]] bool empty() const { return _begin == _end; }
        [[nodiscard]] explicit operator bool() const { return _begin != _end; }
        
        [[nodiscard]] std::size_t size() const { return _end - _begin; }
        [[nodiscard]] T* data() const { return _begin; }
        
        [[nodiscard]] T* begin() const { return _begin; }
        [[nodiscard]] T* end() const { return _end; }
        [[nodiscard]] std::add_const_t<T>* cbegin() const { return _begin; }
        [[nodiscard]] std::add_const_t<T>* cend() const { return _end; }
        
        [[nodiscard]] T& operator[](std::size_t i) const { CHECK(i < size()); return _begin[i]; }
        
        [[nodiscard]] T& front() const { CHECK(!empty()); return *_begin; }
        [[nodiscard]] T& back() const { CHECK(!empty()); return *(_end - 1); }
        
        [[nodiscard]] span<T> before(T* p) const {
            CHECK((_begin <= p) && (p <= _end));
            return { _begin, p };
        }
        
        [[nodiscard]] span<T> after(T* p) const {
            CHECK((_begin <= p) && (p <= _end));
            return { p, _end };
        }
        
        [[nodiscard]] span<T> before(std::size_t i) const {
            CHECK(i <= size());
            return { _begin, _begin + i };
        }
        
        [[nodiscard]] span<T> after(std::size_t i) const {
            CHECK(i <= size());
            return { _begin + i, _end };
        }
        
        [[nodiscard]] std::pair<span<T>, span<T>> partition(T* p) const {
            CHECK((_begin <= p) && (p <= _end));
            return {{ _begin, p}, {p, _end}};
        }
        
        [[nodiscard]] span<T> first(std::size_t n) const {
            CHECK(n <= size());
            return { _begin, _begin + n };
        }
        
        [[nodiscard]] span<T> last(std::size_t n) const {
            CHECK(n <= size());
            return { _end - n, _end };
        }
        
        [[nodiscard]] std::pair<span<T>, span<T>> partition(std::size_t n) const {
            CHECK(n <= size());
            return partition(_begin + n);
        }
        
        [[nodiscard]] span<T> between(std::size_t i, std::size_t j) const {
            CHECK((i <= j) && (j <= size()));
            return { _begin + i, _begin + j };
        }
        
        [[nodiscard]] span<T> subspan(std::size_t i, std::size_t n) const {
            return between(i, i + n);
        }
        
        // mutating
        
        T& pop_front() { CHECK(!empty()); return *_begin++; };
        T& pop_back() { CHECK(!empty()); return *--_end; };
        
        span<T> drop_before(T* p) {
            CHECK((_begin <= p) && (p <= _end));
            return {std::exchange(_begin, p), p};
        }
        
        span<T> drop_after(T* p) {
            CHECK((_begin <= p) && (p <= _end));
            return {p, std::exchange(_end, p)};
        }
        
        span<T> drop_front(std::size_t n) {
            CHECK(n <= size());
            return drop_before(_begin + n);
        }
        
        span<T> drop_back(std::size_t n) {
            CHECK(n <= size());
            return drop_after(_end - n);
        }
        
        // unsafe mutating
        
        void unsafe_unpop_front() {
            --_begin;
        }
        
        void unsafe_unpop_back() {
            ++_end;
        }
        
    }; // struct span
        
} // namespace wry


#endif /* span_hpp */
