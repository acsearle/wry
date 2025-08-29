//
//  attic.cpp
//  
//
//  Created by Antony Searle on 1/12/2024.
//




// Given the complexity of minerals etc., can we reasonably simplify
// chemistry down to any scheme that roughly matches real industrial
// processes?  Or should we just have arbitrary IDs and recipes?

// processes:
//
// milling
// chloralkali
// pyrometallurgy
//   - calcination
//   - roasting / pyrolisis
//   - smelting
// electrolysis (AlO)
// leaching, precipitation

enum ELEMENT
: i64 {
    
    ELEMENT_NONE,
    
    ELEMENT_HYDROGEN,
    ELEMENT_HELIUM,
    
    ELEMENT_LITHIUM,
    ELEMENT_BERYLLIUM,
    ELEMENT_BORON,
    ELEMENT_CARBON,
    ELEMENT_NITROGEN,
    ELEMENT_OXYGEN,
    ELEMENT_FLUORINE,
    ELEMENT_NEON,
    
    ELEMENT_SODIUM,
    ELEMENT_MAGNESIUM,
    ELEMENT_ALUMINUM,
    ELEMENT_SILICON,
    ELEMENT_PHOSPHORUS,
    ELEMENT_SULFUR,
    ELEMENT_CHLORINE,
    ELEMENT_ARGON,
    
    ELEMENT_POTASSIUM,
    ELEMENT_CALCIUM,
    ELEMENT_SCANDIUM,
    ELEMENT_TITANIUM,
    ELEMENT_VANADIUM,
    
    ELEMENT_CHROMIUM,
    ELEMENT_MANGANESE,
    ELEMENT_IRON,
    ELEMENT_COBALT,
    ELEMENT_NICKEL,
    ELEMENT_COPPER,
    ELEMENT_ZINC,
    ELEMENT_GALLIUM,
    ELEMENT_GERMANIUM,
    ELEMENT_ARSENIC,
    ELEMENT_SELENIUM,
    ELEMENT_BROMINE,
    ELEMENT_KRYPTON,
    
    ELEMENT_RUBIDIUM,
    ELEMENT_STRONTIUM,
    ELEMENT_YTTRIUM,
    ELEMENT_ZIRCONIUM,
    ELEMENT_NIOBIUM,
    ELEMENT_MOLYBDENUM,
    
    // notable but relatively rare
    SILVER,
    TIN,
    PLATINUM,
    GOLD,
    MERCURY,
    LEAD,
    URANIUM,
};

enum COMPOUND : i64 {
    
    WATER, // H2O
    
    // by crust abundance
    SILICON_DIOXIDE,
    
    // ( source)
    
};



//
//  adl.hpp
//  client
//
//  Created by Antony Searle on 1/9/2024.
//

#ifndef adl_hpp
#define adl_hpp

#include <utility>


// The intent of this file is to provide access points of the form
//
//     foo(x)
//
// which find the correct implementation of foo by ADL even when the calling
// context shadows those names.
//
// To provide customization points for types in closed namespaces (notably std)
// we also may explicitly import a namespace

namespace wry {
    
    namespace orphan {
        
        // Forward declare fallback namespace
        //
        // Types that are defined in namespace that we cannot or should not
        // modify, such as :: and ::std, have their customization points
        // dumped here
        
    } // namespace orphan
    
} // namespace wry

#define MAKE_CUSTOMIZATION_POINT_OBJECT(NAME, NAMESPACE)\
namespace adl {\
namespace _detail {\
struct _##NAME {\
decltype(auto) operator()(auto&&... args) const {\
using namespace NAMESPACE;\
return NAME(FORWARD(args)...);\
}\
};\
}\
inline constexpr _detail::_##NAME NAME;\
}

MAKE_CUSTOMIZATION_POINT_OBJECT(debug, ::wry::orphan)
MAKE_CUSTOMIZATION_POINT_OBJECT(hash, ::wry::orphan)
MAKE_CUSTOMIZATION_POINT_OBJECT(passivate, ::wry::orphan)
MAKE_CUSTOMIZATION_POINT_OBJECT(shade, ::wry::orphan)
MAKE_CUSTOMIZATION_POINT_OBJECT(swap, ::std)
MAKE_CUSTOMIZATION_POINT_OBJECT(trace, ::wry::orphan)
MAKE_CUSTOMIZATION_POINT_OBJECT(trace_weak, ::wry::orphan)

#endif /* adl_hpp */



/*
 #include <map>
 #include <mutex>
 
 #include "utility.hpp"
 */

/*
 template<typename Key, typename T>
 struct StableConcurrentMap {
 std::mutex _mutex;
 std::map<Key, T> _map;
 
 bool insert_or_assign(auto&& k, auto&& v) {
 std::unique_lock lock{_mutex};
 return _map.insert_or_assign(FORWARD(k),
 FORWARD(v)).first;
 }
 
 decltype(auto) subscript_and_mutate(auto&& k, auto&& f) {
 std::unique_lock lock{_mutex};
 return FORWARD(f)(_map[FORWARD(k)]);
 }
 
 decltype(auto) access(auto&& f) {
 std::unique_lock lock{_mutex};
 FORWARD(f)(_map);
 }
 
 };
 */


#if 0
//
//  Deque.hpp
//  client
//
//  Created by Antony Searle on 28/6/2024.
//

#ifndef Deque_hpp
#define Deque_hpp

#include <cassert>

#include "garbage_collected.hpp"
#include "Scan.hpp"

// TODO: Remove or significant cleanup
// The motivating use case is now handled by the simpler Bag data structure.
//
// TODO: Rename
//
// A doubly-linked list of arrays.  Head and tail point directly into the
// arrays and rely on the power-of-two alignment of the list nodes to find the
// node header and footer.  Unlike std::deque, there is no top-level structure
// to efficiently index into the chunks.
//
// Both this and std::deque are (approximately) degenerate cases of radix
// trees.

namespace wry {
    
    template<typename T>
    struct Deque {
        
        struct alignas(4096) Page : GarbageCollected {
            
            constexpr static size_t CAPACITY = (4096 - sizeof(GarbageCollected) - sizeof(Page*) - sizeof(Page*)) / sizeof(T*);
            
            Scan<Page*> prev;
            T elements[CAPACITY];
            Scan<Page*> next;
            
            Page(Page* prev, Page* next)
            : prev(prev), next(next) {
            }
            
            T* begin() { return elements; }
            T* end() { return elements + CAPACITY; }
            
            void _garbage_collected_enumerate_fields(TraceContext*p) const override {
                _garbage_collected_trace(prev,p);
                for (const T& e : elements)
                    _garbage_collected_trace(e,p);
                _garbage_collected_trace(next,p);
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
    
} // namespace wry

#endif /* Deque_hpp */


#endif
