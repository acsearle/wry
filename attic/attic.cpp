//
//  attic.cpp
//  
//
//  Created by Antony Searle on 1/12/2024.
//




#if 0 // Legacy

namespace wry {
    
    // Reference implementation of true O(1) bag via std::deque
    template<typename T>
    struct StandardDequeBag {
        
        std::deque<T>* _inner = nullptr;
        
        bool is_empty() {
            return !_inner || _inner->empty();
        }
        
        constexpr StandardDequeBag() = default;
        ~StandardDequeBag() {
            delete _inner;
        }
        StandardDequeBag(const StandardDequeBag&) = delete;
        StandardDequeBag(StandardDequeBag&& other)
        : _inner(std::exchange(other._inner, nullptr)) {
        }
        
        void swap(StandardDequeBag& other) {
            using std::swap;
            swap(_inner, other._inner);
        }
        
        StandardDequeBag& operator=(const StandardDequeBag&) = delete;
        StandardDequeBag& operator=(StandardDequeBag&& other) {
            StandardDequeBag(std::move(other)).swap(*this);
            return *this;
        }
        
        size_t size() const {
            return _inner ? _inner->size() : 0;
        }
        
        void push(T value) {
            if (!_inner)
                _inner = new std::deque<T>;
            _inner->push_back(std::move(value));
        }
        
        bool try_pop(T& victim) {
            if (is_empty())
                return false;
            victim = std::move(_inner->front());
            _inner->pop_front();
            return true;
        }
        
        void extend(StandardDequeBag&& other) {
            T victim;
            while (other.try_pop(victim)) {
                push(std::move(victim));
            }
        }
        
    };
    
} // namespace wry

#if 0



/*
 
 struct persistent_set {
 
 const Node<std::monostate>* root = nullptr;
 
 bool contains(uint64_t key) {
 return (root != nullptr) && root->contains(key);
 }
 
 persistent_set insert(uint64_t key) const {
 return persistent_set{
 root
 ? root->insert(key, std::monostate{})
 : Node<std::monostate>::make_with_key_value(key, std::monostate{})
 };
 };
 
 
 //size_t size() const {
 //    return root ? root->size() : 0;
 //}
 
 };
 
 inline persistent_set merge(persistent_set a, persistent_set b) {
 return persistent_set{Node<std::monostate>::merge(a.root, b.root)};
 }
 
 
 inline bool is_empty(persistent_set a) {
 return a.root == nullptr;
 }
 
 
 //inline persistent_set erase(uint64_t key, persistent_set a) {
 //    return persistent_set{a.root ? a.root->erase(key) : nullptr};
 //};
 
 template<typename F>
 void parallel_for_each(persistent_set s, F&& f) {
 parallel_for_each(s.root, std::forward<F>(f));
 }
 
 template<typename T, typename F>
 void parallel_for_each(const Node<T>* p, F&& f) {
 if (p == nullptr) {
 return;
 } else if (p->_shift) {
 int n = popcount(p->_bitmap);
 for (int i = 0; i != n; ++i)
 parallel_for_each(p->_children[i], f);
 return;
 } else {
 uint64_t b = p->_bitmap;
 // int i = 0;
 for (;;) {
 if (!b)
 return;
 int j = ctz(b);
 f(p->_prefix | j);
 b &= (b - 1);
 // ++i;
 }
 }
 }
 
 template<typename T, typename F>
 void parallel_rebuild(uint64_t lower_bound, uint64_t upper_bound,
 const Node<T>* left, const T& right,
 F&& f) {
 // recurse into 6-bit chunked keyspace
 // left->_prefix is lower bound
 // left->_prefix + ((uint64_t) 64 << _shift) is upper bound
 }
 */





/*
 [[nodiscard]] const Node* insert(uint64_t key, T value) const {
 //printf("%s\n", __PRETTY_FUNCTION__);
 auto [prefix, shift] = get_prefix_and_shift();
 uint64_t delta = key ^ prefix;
 if ((delta >> shift) >> 6)
 // prefix does not match
 return Node::merge_disjoint(this, Node::make_with_key_value(key, value));
 
 // prefix does match
 // we have to modify the node at this level
 uint64_t bit = decode(key >> shift);
 
 if (!(bit & _bitmap)) {
 // we have to make a new slot
 if (shift == 0) {
 // we just need to mark a bit
 return Node::make(prefix, shift, _bitmap | bit);
 }
 // we have to make a new node
 return clone_and_insert(Node::make_with_key_value(key, value));
 }
 
 
 // we have to update an existing slot
 if (shift == 0)
 // we are at the bottom level, so this is a blocked insert
 return this;
 // not at the bottom level, so we have to modify the existing
 // element
 int offset = popcount((bit - 1) & _bitmap);
 const Node* child = _children[offset];
 const Node* replacement = child->insert(key, value);
 if (replacement == child)
 // the element already existed so we don't need to do anything else
 return this;
 return this->clone_and_replace_child(replacement);
 
 }
 */




/*
 const Node* erase(uint64_t key) const {
 printf("%s\n", __PRETTY_FUNCTION__);
 auto [prefix, shift] = get_prefix_and_shift();
 
 _assert_invariant_shallow();
 
 uint64_t delta = key ^ prefix;
 if (delta >> shift >> 6)
 // prefix doesn't match so set does not contain key
 return this;
 
 uint64_t index = (key >> shift) & 0x3F;
 uint64_t bit = ztc(index);
 if (!(bit & _bitmap))
 // bitmap doesn't match so set does not contain key
 return this;
 
 if (shift == 0) {
 // erase from a leaf
 uint64_t bitmap = _bitmap ^ bit;
 if (!bitmap)
 // erased last entry
 return nullptr;
 return Node::make(prefix, shift, bitmap);
 }
 
 assert(shift > 0);
 // erase by recusion
 int count = popcount(_bitmap);
 assert(count >= 2);
 int offset = popcount((bit - 1) & _bitmap);
 const Node* child = _children[offset];
 const Node* replacement = child->erase(key);
 if (replacement == child)
 // key not present
 return this;
 if (replacement)
 // node not trivial
 return this->clone_and_replace(replacement);
 
 assert(replacement == nullptr);
 if (count == 2) {
 // this level would contain only one entry, so replace it
 // with its surviving child
 return _children[offset ^ 1];
 }
 return this->clone_and_erase(key);
 }*/



[[nodiscard]] static const Node* merge(const Node* a, const Node* b) {
    //printf("%s\n", __PRETTY_FUNCTION__);
    
    if (a) {
        a->_assert_invariant_shallow();
    }
    if (b) {
        b->_assert_invariant_shallow();
    }
    
    if (a == nullptr)
        return b;
    if (b == nullptr)
        return a;
    // structural sharing may let us prove that this merge is unnecessary
    if (a == b)
        return a;
    auto [a_prefix, a_shift] = a->get_prefix_and_shift();
    auto [b_prefix, b_shift] = b->get_prefix_and_shift();
    
    
    // assert(a_shift == 0 || popcount(a->_bitmap) > 1);
    // assert(b_shift == 0 || popcount(b->_bitmap) > 1);
    
    
    uint64_t delta = a_prefix ^ b_prefix;
    
    uint64_t c_shift = std::max(a_shift, b_shift);
    if (delta >> (c_shift + 6)) {
        // High bits don't match, sets are disjoint
        return Node::merge_disjoint(a, b);
    }
    
    if (a_shift != b_shift) {
        
        // Levels don't match
        if (a_shift < b_shift) {
            using std::swap;
            swap(a, b);
            swap(a_prefix, b_prefix);
            swap(a_shift, b_shift);
        }
        assert(a_shift > b_shift);
        
        auto index = (b_prefix >> a_shift) & 63;
        auto bit = decode(index);
        
        if (!(bit & a->_bitmap))
            return a->clone_and_insert_child(b);
        
        // b conflicts with a->_child[...]
        
        int index2 = popcount((bit - 1) & a->_bitmap);
        const Node* c = a->_children[index2];
        auto [c_prefix, c_shift] = c->get_prefix_and_shift();
        assert(c_shift < a_shift);
        const Node* d = merge(c, b);
        auto [d_prefix, d_shift] = d->get_prefix_and_shift();
        if (!(d_shift < a_shift)) {
            printf("\"a\" %llx : %d\n", a_prefix, a_shift);
            printf("\"b\" %llx : %d\n", b_prefix, b_shift);
            printf("\"c\" %llx : %d\n", c_prefix, c_shift);
            printf("\"d\" %llx : %d\n", d_prefix, d_shift);
            a->_assert_invariant_shallow();
            b->_assert_invariant_shallow();
            c->_assert_invariant_shallow();
            d->_assert_invariant_shallow();
        }
        assert(d_shift < a_shift);
        if (d == c)
            return a;
        
        return a->clone_and_assign_child(d);
    }
    
    assert(a_prefix == b_prefix);
    assert(a_shift == b_shift);
    
    uint64_t bitmap = a->_bitmap | b->_bitmap;
    Node* d = Node::make(prefix_and_shift_for_keylike_and_shift(a_prefix, a_shift), popcount(bitmap), bitmap);
    // fill the output from a, b, or merge
    
    // TODO: the merge does not need a new node when one is a subset of
    // the other, but we can't prove this without recursing down all
    // the common children, and then knowing how to compare T if T is
    // not monostate
    
    // We can either allocate a new node and discard it in the (rare?)
    // case it is not needed
    
    // or we can construct in an alloca arena and copy over if needed
    
    uint64_t a_map = a->_bitmap;
    uint64_t b_map = b->_bitmap;
    int a_index2 = 0;
    int b_index2 = 0;
    int d_index2 = 0;
    if (a_shift) {
        while (a_map | b_map) {
            int a_n = a_map ? ctz(a_map) : 64;
            int b_n = b_map ? ctz(b_map) : 64;
            if (a_n < b_n) {
                d->_children[d_index2] = a->_children[a_index2];
                ++a_index2; a_map &= (a_map - 1);
            } else if (b_n < a_n) {
                d->_children[d_index2] = b->_children[b_index2];
                ++b_index2; b_map &= (b_map - 1);
            } else {
                d->_children[d_index2] = merge(a->_children[a_index2], b->_children[b_index2]);
                ++a_index2; a_map &= (a_map - 1);
                ++b_index2; b_map &= (b_map - 1);
            }
            ++d_index2;
        }
    } else {
        while (a_map | b_map) {
            int a_n = a_map ? ctz(a_map) : 64;
            int b_n = b_map ? ctz(b_map) : 64;
            if (a_n < b_n) {
                d->_values[d_index2] = a->_values[a_index2];
                ++a_index2; a_map &= (a_map - 1);
            } else if (b_n < a_n) {
                d->_values[d_index2] = b->_values[b_index2];
                ++b_index2; b_map &= (b_map - 1);
            } else {
                abort();
                d->_values[d_index2] = a->_values[a_index2]; // favor random
                ++a_index2; a_map &= (a_map - 1);
                ++b_index2; b_map &= (b_map - 1);
            }
            ++d_index2;
        }                }
    
    return d;
    
}

/*
 void erase_key(uint64_t key) {
 if (!prefix_covers_key(key)) {
 return;
 }
 if (!bitmap_covers_key(key)) {
 return;
 }
 if (has_children()) {
 int compressed_index = get_compressed_index_for_key(key);
 const Node* child = _children[compressed_index];
 Node* p = child->clone();
 p->erase_key(key);
 Node* q = clone();
 q->_children[compressed_index] = p;
 } else {
 T _ = {};
 (void) compressed_array_try_erase_for_index(_bitmap,
 _values,
 index,
 _);
 }
 }
 */
#endif





// Coroutine of many features:
//
//
//
//    template<typename T>
//    struct Future {
//
//        struct Promise {
//
//            enum : uintptr_t {
//                INITIAL,
//                FINAL,
//                RELEASED,
//                /* CONTINUATION, */
//            };
//
//            Atomic<uintptr_t> _state;
//            std::variant<std::monostate, T, std::exception_ptr> _result;
//
//            Future get_return_object() {
//                return Future{this};
//            }
//
//            suspend_always initial_suspend() noexcept {
//                return suspend_always{};
//            }
//
//            void release() {
//                uintptr_t was = _state.exchange(RELEASE, Ordering::RELAXED);
//                switch (was) {
//                    case INITIAL:
//                        break;
//                    case FINAL:
//                        std::coroutine_handle<Promise>::from_promise(*this).destroy();
//                        break;
//                    default:
//                        abort();
//                }
//            }
//
//            void unhandled_exception() {
//                assert(_result.index() == 0);
//                _result.emplace<2>(std::current_exception());
//            }
//
//            void return_value(auto&& expr) {
//                assert(_result.index() == 0);
//                _result.emplace<1>(FORWARD(expr));
//            }
//
//            auto final_suspend() noexcept {
//                struct Awaitable {
//                    constexpr bool await_ready() const noexcept { return false; }
//                    std::coroutine_handle<> await_suspend(std::coroutine_handle<Promise> handle) noexcept {
//                        auto was = handle.promise()._state.exchange(FINAL, Ordering::RELEASE);
//                        switch (was) {
//                            case INITIAL:
//                                return std::noop_coroutine();
//                            case FINAL:
//                                abort();
//                            case RELEASED:
//                                handle.destroy();
//                                return std::noop_coroutine();
//                            default: /* CONTINUATION */
//                                (void) handle.promise()._state.load(Ordering::ACQUIRE);
//                                return std::coroutine_handle<>::from_address((void*)was);
//                        }
//                    }
//                    void await_resume() const noexcept {
//                        abort();
//                    }
//                };
//            }
//
//            decltype(auto) await_transform(auto&& expr) {
//                return FORWARD(expr).co_await_with_promise(this);
//            }
//
//        };
//
//        using promise_type = Promise;
//
//        Promise* _promise;
//
//
//        // co_await Future<T> runs and eventually resumes caller with a T
//        // co_fork Future<T> runs and schedules the caller to resume ASAP
//        // co_join suspends the caller until all forks have completed
//
//
//    };
//





// fork-join a coroutine from a non-coroutine
// must eventually call join explicitly

// consider unifying with co_eager<T>

struct co_fork {
    struct promise_type {
        enum {
            INITIAL,
            FINAL,
            AWAITED,
        };
        Atomic<int> _state{};
        ~promise_type() {
            printf("co_fork::promise_type::~promise_type()\n");
        }
        constexpr co_fork get_return_object() noexcept {
            return co_fork{this};
        }
        constexpr suspend_and_schedule initial_suspend() const noexcept { return {}; }
        constexpr auto final_suspend() const noexcept {
            struct awaitable : suspend_always {
                void await_suspend(std::coroutine_handle<promise_type> handle) noexcept {
                    Atomic<int>* state = &handle.promise()._state;
                    int was = state->exchange(FINAL, Ordering::RELEASE);
                    // The promise may now have been deleted under us.
                    switch (was) {
                        case INITIAL:
                            break;
                        case FINAL:
                            abort();
                        case AWAITED:
                            // Even if the promise is deleted, we can still
                            // notify on the address; worst case ABA causes
                            // a spurious wakeup on the new object
                            state->notify_one();
                            break;
                        default:
                            abort();
                    }
                }
            };
            return awaitable{};
        }
        void join() {
            int was = _state.exchange(AWAITED, Ordering::ACQUIRE);
            for (;;) switch (was) {
                case AWAITED:
                    // spurious wake?
                    [[fallthrough]];
                case INITIAL:
                    _state.wait(was, Ordering::ACQUIRE);
                    break;
                case FINAL:
                    std::coroutine_handle<promise_type>::from_promise(*this).destroy();
                    return;
            }
        }
        void return_void() const noexcept {}
        void unhandled_exception() const noexcept { abort(); }
        
        // auto await_transform(auto&& awaitable) {
        //     return coroutine::await_transform(*this, FORWARD(awaitable));
        // }
        
    };
    
    promise_type* _promise;
    
    explicit co_fork(promise_type* promise) : _promise(promise) {}
    
    co_fork() = delete;
    co_fork(co_fork const& other) = delete;
    co_fork(co_fork&& other)
    : _promise(std::exchange(other._promise, nullptr)) {
    }
    ~co_fork() {
        if (_promise)
            abort();
    }
    
    co_fork& operator=(co_fork const&) = delete;
    co_fork& operator=(co_fork&& other) {
        co_fork local(std::move(other));
        using std::swap;
        swap(_promise, local._promise);
        return *this;
    }
    
    void join() {
        std::exchange(_promise, nullptr)->join();
    }
    
};






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



//
//  wry/HeapArray.hpp
//  client
//
//  Created by Antony Searle on 19/6/2024.
//

#ifndef wry_HeapArray_hpp
#define wry_HeapArray_hpp

#include <bit>

#include "assert.hpp"
#include "garbage_collected.hpp"
#include "memory.hpp"
#include "utility.hpp"

namespace wry {
    
#if 0
    
    // This attempt at a garbage-collected mutable array suffers from the fact
    // that the collector must scan the elements and they must therefore be
    // thread-safe, either by making them truly const, or atomic.  The collector
    // must also scan the whole array since changes to the elements and the
    // valid subrange race each other.
    //
    // Instead we use persistent pure functional data structures in some
    // contexts, and scoped mutable concurrent data structures in others.  The
    // former do not mutate; the latter are not scanned.
    
    template<typename T> // requires(std::has_single_bit(sizeof(T)))
    struct ArrayStaticIndirect : GarbageCollected {
        
        using value_type = T;
        using size_type = std::size_t;
        using difference_type = std::ptrdiff_t;
        using reference = value_type&;
        using const_reference = const value_type&;
        using pointer = value_type*;
        using const_pointer = const value_type*;
        
        T* const _data;
        size_t const _size;
        
        void _invariant() {
            assert(_data);
            assert(std::has_single_bit(_size));
        }
        
        explicit ArrayStaticIndirect(size_t n)
        : _data((T*) calloc(n, sizeof(T)))
        , _size(n) {
            std::uninitialized_default_construct_n(_data, n);
            _invariant();
        }
        
        virtual ~ArrayStaticIndirect() override {
            // std::string_view sv = name_of<T>;
            // printf("~ArrayStaticIndirect<%.*s>(%zd)\n", (int)sv.size(), (const char*)sv.data(), _size);
            // std::destroy_n(_data, _size);
            free(_data);
        }
        
        size_t size() const { return _size; }
        
        T* data() { return _data; }
        const T* data() const { return _data; }
        
        T* begin() { return _data; }
        const T* begin() const { return _data; }
        
        T* end() { return _data + _size; }
        const T* end() const { return _data + _size; }
        
        T& front() { return *_data; }
        const T& front() const { return *_data; }
        
        T& back() { return _data[_size - 1]; }
        const T& back() const { return _data[_size - 1]; }
        
        T& operator[](size_t i) {
            assert(i < _size);
            return _data[i];
        }
        
        const T& operator[](size_t i) const {
            assert(i < _size);
            return _data[i];
        }
        
        virtual void _garbage_collected_scan() const override {
            for (const T& element : *this) {
                garbage_collected_scan(element);
            }
        }
        
        virtual void _garbage_collected_debug() const override {
            std::string_view sv = name_of<T>;
            printf("ArrayStaticIndirect<%.*s>(%zd){ ", (int)sv.size(), (const char*)sv.data(), _size);
            for (const T& element : *this) {
                any_debug(element);
                printf(", ");
            }
            printf("}");
        }
        
    };
    
    template<typename T>
    struct RingBufferView {
        
        T* _data;
        size_t _capacity;
        
        RingBufferView()
        : _data(nullptr)
        , _capacity(0) {
        }
        
        RingBufferView(const RingBufferView&) = delete;
        
        RingBufferView(RingBufferView&& other)
        : _data(exchange(other._data, nullptr))
        , _capacity(exchange(other._capacity, 0)) {
        }
        
        ~RingBufferView() = default;
        
        RingBufferView& operator=(const RingBufferView&) = delete;
        
        RingBufferView& operator=(RingBufferView&& other) {
            _data = exchange(other._data, nullptr);
            _capacity = exchange(other._capacity, 0);
            return *this;
        }
        
        size_t _mask(size_t i) const {
            return i & (_capacity - 1);
        }
        
        size_t capacity() const {
            return _capacity;
        }
        
        T& operator[](size_t i) {
            return _data[_mask(i)];
        }
        
        const T& operator[](size_t i) const {
            return _data[_mask(i)];
        }
        
    };
    
    template<typename T>
    struct RingDequeStatic {
        RingBufferView<T> _inner;
        size_t _begin = 0;
        size_t _end = 0;
        Atomic<ArrayStaticIndirect<T>*> _storage;
        
        RingDequeStatic()
        : _inner()
        , _begin(0)
        , _end(0)
        , _storage(nullptr) {
        }
        
        RingDequeStatic(RingDequeStatic&& other)
        : _inner(std::move(other._inner))
        , _begin(exchange(other._begin, 0))
        , _end(exchange(other._end, 0))
        , _storage(std::move(other._storage)) {
        }
        
        RingDequeStatic& operator=(RingDequeStatic&& other) {
            _inner = std::move(other._inner);
            _begin = exchange(other._begin, 0);
            _end = exchange(other._end, 0);
            _storage = std::move(other._storage);
            return *this;
        }
        
        size_t capacity() const {
            return _inner._capacity;
        }
        
        size_t size() const {
            return _end - _begin;
        }
        
        bool empty() const {
            return _begin == _end;
        }
        
        bool full() const {
            return _end - _begin == capacity();
        }
        
        T& front() {
            assert(!empty());
            return _inner[_begin];
        }
        
        T& back() {
            assert(!empty());
            return _inner[_end - 1];
        }
        
        T& operator[](size_t i) {
            assert(_begin <= i);
            assert(i < _end);
            return _inner[i];
        }
        
        void pop_front() {
            assert(!empty());
            ++_begin;
        }
        
        void pop_back() {
            assert(!empty());
            --_end;
        }
        
        template<typename U>
        void push_front(U&& value) {
            assert(!full());
            _inner[_begin - 1] = std::forward<U>(value);
            --_begin;
        }
        
        template<typename U>
        void push_back(U&& value) {
            assert(!full());
            _inner[_end] = std::forward<U>(value);
            ++_end;
        }
        
    };
    
    template<typename T>
    void garbage_collected_scan(const RingDequeStatic<T>& self) {
        garbage_collected_scan(self._storage);
    }
    
    template<typename T>
    void garbage_collected_shade(const RingDequeStatic<T>& self) {
        garbage_collected_shade(self._storage);
    }
    
    
    
    template<typename T>
    struct GCArray {
        mutable RingDequeStatic<T> _alpha;
        mutable RingDequeStatic<T> _beta;
        
        void _tax_front() const {
            if (!_beta.empty()) {
                _alpha[_beta._begin] = std::move(_beta.front());
                _beta.pop_front();
                if (_beta.empty())
                    _beta._storage = nullptr;
            }
        }
        
        void _tax_back() const {
            if (!_beta.empty()) {
                _alpha[_beta._end - 1] = std::move(_beta.back());
                _beta.pop_back();
                if (_beta.empty())
                    _beta._storage = nullptr;
            }
        }
        
        void _ensure_nonfull() const{
            if (_alpha.full()) {
                assert(_beta.empty());
                _beta._inner._data = _alpha._inner._data;
                _beta._inner._capacity = _alpha._inner._capacity;
                _beta._storage = std::move(_alpha._storage);
                _beta._begin = _alpha._begin;
                _beta._end = _alpha._end;
                _alpha._inner._capacity = max(_alpha._inner._capacity << 1, 1);
                auto p = new ArrayStaticIndirect<T>(_alpha._inner._capacity);
                _alpha._inner._data = p->_data;
                _alpha._storage = p;
            }
        }
        
        bool empty() const {
            return _alpha.empty();
        }
        
        size_t size() const {
            return _alpha.size();
        }
        
        T& front() const {
            _tax_front();
            return _alpha.front();
        }
        
        T& back() const {
            _tax_back();
            return _alpha.back();
        }
        
        void pop_front() {
            _tax_front();
            _alpha.pop_front();
        }
        
        void pop_back() {
            _tax_back();
            _alpha.pop_back();
        }
        
        template<typename U>
        void push_front(U&& value) {
            _tax_back();
            _ensure_nonfull();
            _alpha.push_front(std::forward<U>(value));
        }
        
        template<typename U>
        void push_back(U&& value) {
            _tax_front();
            _ensure_nonfull();
            _alpha.push_back(std::forward<U>(value));
        }
        
        T& operator[](size_t i) {
            _tax_front();
            assert(_alpha._begin <= i);
            assert(i < _alpha._end);
            if (i < _beta._begin || i >= _beta._end)
                return _alpha[i];
            else
                return _beta[i];
        }
        
        const T& operator[](size_t i) const {
            _tax_front();
            assert(_alpha._begin <= i);
            assert(i < _alpha._end);
            if (i < _beta._begin || i >= _beta._end)
                return _alpha[i];
            else
                return _beta[i];
        }
        
        void clear() {
            _alpha._begin = _alpha._end;
            _beta._begin = _beta._end;
            _beta._storage = nullptr;
        }
        
    };
    
    template<typename T>
    void garbage_collected_scan(const GCArray<T>& self) {
        garbage_collected_scan(self._alpha);
        garbage_collected_scan(self._beta);
    }
    
    template<typename T>
    void garbage_collected_shade(const GCArray<T>& self) {
        garbage_collected_shade(self._alpha);
        garbage_collected_shade(self._beta);
    }
    
    
    static_assert(std::is_move_assignable_v<GCArray<Scan<GarbageCollected*>>>);
    
#endif
    
} // namespace wry

#endif /* wry_HeapArray_hpp */


//
//  wry/HeapArray.cpp
//  client
//
//  Created by Antony Searle on 19/6/2024.
//

#include "HeapArray.hpp"

#include "test.hpp"
#include "value.hpp"

namespace wry {
    
#if 0
    define_test("HeapArray") {
        
        mutator_become_with_name("HeapArrayTest");
        
        auto* a = new GCArray<Scan<Value>>();
        
        assert(a->empty() == true);
        assert(a->size() == 0);
        
        for (int i = 0; i != 100; ++i) {
            assert(a->empty() == !i);
            assert(a->size() == i);
            a->push_back(i);
            assert(a->size() == i + 1);
            assert(a->back() == i);
            assert(a->front() == 0);
        }
        
        for (int i = 100; i--;) {
            assert(a->empty() == false);
            assert(a->size() == i+1);
            assert(a->back() == i);
            assert(a->front() == 0);
            a->pop_back();
            assert(a->size() == i);
        }
        
        assert(a->empty() == true);
        assert(a->size() == 0);
        
        mutator_handshake(true);
        
    };
#endif
    
} // namespace wry


//
//  wry/gc/HeapTable.hpp
//  client
//
//  Created by Antony Searle on 14/6/2024.
//

#ifndef wry_gc_HeapTable_hpp
#define wry_gc_HeapTable_hpp

#include <cassert>
#include <concepts>
#include <bit>
#include <optional>

//#include "adl.hpp"
#include "debug.hpp"
#include "garbage_collected.hpp"
#include "value.hpp"
#include "hash.hpp"

namespace wry {
    
#if 0
    
    // If the map has tombstones, it must periodically be copied to
    // restore an acceptable number of vacancies to terminate searches.
    //
    // Yet we can't incrementally copy such a map into the same sized
    // allocation unless: it is mostly unoccupied; or, we move a large (and
    // occupancy-dependent) number of elements per operation.
    //
    // We fall back to Robin Hood hashing, which has no tombstones and
    // instead performs this sort of "compaction" continuously.
    //
    // When the Robin Hood hash set needs to be resized, we double its
    // size.  The new capacity is half full; it is sufficient for every
    // insert to copy over one element (on average) for the incremental
    // copy to be complete before the new set fills up.  In practice we
    // want the incremental resize to complete even if the workflow is
    // mostly lookups, so we copy 2 slots (which we expect to contain 3/2
    // objects) per operation.
    
    
    // T must support
    // - T()
    // - .occupied
    // - .vacant
    // - .vacate
    // - .hash
    // - .assign
    
    // TODO: is it worth making Entry a concept?
    
    template<typename A, typename B>
    using Pair = std::pair<A, B>;
    
    template<typename K, typename V>
    struct BasicEntry {
        
        // we can't use std::optional here because we need to concurrently
        // scan the object, and construction/destruction/has_value are not
        // atomic
        
        // More generally, the Pair needs to always be in a constructed and
        // scannable state, even when unoccupied
        
        Pair<K, V> _kv;
        bool _occupied;
        
        bool occupied() const { return _occupied; }
        bool vacant() const { return !_occupied; }
        void vacate() {
            assert(_occupied);
            _occupied = false;
        }
        
        size_t _hash() const {
            return hash(_kv.first);
        }
        
        template<typename J, typename U>
        static size_t _hash(const Pair<J, U>& ju) {
            return hash(ju.first);
        }
        
        template<typename J>
        static size_t _hash(const J& j) {
            return hash(j);
        }
        
        void assign(BasicEntry&& other) {
            assert(other._occupied);
            _kv = std::move(other._kv);
            _occupied = true;
        }
        
        template<typename J, typename U>
        void assign(std::pair<J, U>&& ju) {
            _kv = std::move(ju);
            _occupied = true;
        }
        
        template<typename J, typename U>
        void assign(J&& j, U&& u) {
            _kv.first = std::forward<J>(j);
            _kv.second = std::forward<U>(u);
            _occupied = true;
        }
        
        template<typename J>
        void emplace(J&& j) {
            _kv.first = std::forward<J>(j);
            _kv.second = V();
            _occupied = true;
        }
        
        
        template<typename J>
        bool equivalent(size_t h, J&& j) const {
            assert(_occupied);
            return _kv.first == j;
        }
        
    };
    
    
    
    template<typename K, typename V>
    void garbage_collected_scan(const BasicEntry<K, V>& e) {
        garbage_collected_scan(e._kv.first);
        garbage_collected_scan(e._kv.second);
    }
    
    template<typename K, typename V>
    void garbage_collected_shade(const BasicEntry<K, V>& e) {
        garbage_collected_shade(e._kv.first);
        garbage_collected_shade(e._kv.second);
    }
    
    template<typename K, typename V>
    void any_debug(const BasicEntry<K, V>& e) {
        any_debug(e._kv.first);
        any_debug(e._kv.second);
    }
    
    template<typename K, typename V>
    ValueHash any_hash(const BasicEntry<K, V>& e) {
        return any_hash(e._kv.first);
    }
    
    
    /*
     template<typename K, typename V>
     void object_trace_weak(const BasicEntry<K, V>& e) {
     object_trace(e);
     }
     */
    
    
    
    // Provides Robin Hood semantics on a power-of-two-sized array of
    // entries that satisfy minimal requirements.
    //
    // Does not
    // - own storage
    // - track occupant count
    // - track load factor
    // - resize
    // - know about garbage collection
    //
    // These services are provided by the next layers
    
    template<typename T>
    struct BasicHashSetA {
        
        T* _data;
        size_t _capacity;
        
        BasicHashSetA()
        : _data(nullptr)
        , _capacity(0) {}
        
        BasicHashSetA(BasicHashSetA&& other)
        : _data(exchange(other._data, nullptr))
        , _capacity(exchange(other._capacity, 0)) {
        }
        
        BasicHashSetA& operator=(BasicHashSetA&& other) {
            _data = exchange(other._data, nullptr);
            _capacity = exchange(other._capacity, 0);
            return *this;
        }
        
        T* data() const { return _data; }
        size_t capacity() const { return _capacity; }
        
        T* begin() const { return _data; }
        T* end() const { return _data + _capacity; }
        
        size_t _mask(size_t i) const {
            return(_capacity - 1) & i;
        }
        
        size_t _succ(size_t index) const {
            return _mask(index + 1);
        }
        
        size_t _pred(size_t index) const {
            return _mask(index - 1);
        }
        
        size_t _displacement(size_t i) const {
            assert(i < capacity());
            assert(_data[i].occupied());
            size_t h = _data[i]._hash();
            return _mask(i - h);
        }
        
        size_t _invariant() const {
            assert(_capacity == 0 || std::has_single_bit(_capacity));
            size_t count = 0;
            for (size_t j = 0; j != _capacity; ++j) {
                if (_data[j].occupied()) {
                    ++count;
                    size_t e = _displacement(j);
                    size_t i = _pred(j);
                    if (_data[i].occupied()) {
                        // if there is an occupied slot before us, it should be
                        // at least as displaced as we are
                        size_t d = _displacement(i);
                        assert(d + 1 >= e);
                    } else {
                        // if there is an empty slot before this entry, we must
                        // be in our ideal slot
                        assert(e == 0);
                    }
                }
            }
            return count;
        }
        
        void _steal_from_the_rich(size_t i) const {
            _invariant();
            assert(i < capacity());
            assert(_data[i].occupied());
            size_t j = i;
            for (;;) {
                j = _succ(j);
                if (_data[j].vacant())
                    break;
            }
            // move_backward
            for (;;) {
                size_t k = _pred(j);
                _data[j].assign(std::move(_data[k]));
                if (k == i)
                    break;
                j = k;
            }
        }
        
        size_t _give_to_the_poor(size_t i) const {
            assert(i < capacity());
            assert(_data[i].occupied());
            for (;;) {
                size_t j = _succ(i);
                if (_data[j].vacant())
                    break;
                size_t e = _displacement(j);
                if (e == 0)
                    break;
                // move forward
                _data[i].assign(std::move(_data[j]));
                i = j;
            }
            assert(_data[i].occupied());
            return i;
        }
        
        // we claim that the element is present, so we don't need to check
        // several conditions
        template<typename Q>
        size_t _find_present(size_t h, Q&& q) const {
            size_t i = _mask(h);
            for (;;) {
                assert(_data[i].vacant());
                if (_data[i].equivalent(h, q))
                    return i;
                i = _succ(i);
            }
        }
        
        // the element is known not to be present; we are looking for where
        // it should be inserted
        template<typename Q>
        size_t _find_absent(size_t h, Q&& q) const {
            size_t d = 0;
            size_t i = _mask(h);
            for (;;) {
                if (_data[i].vacant())
                    return i;
                size_t e = _displacement(i);
                if (e < d)
                    return i;
                i = _succ(i);
                ++d;
            }
        }
        
        // where and if
        template<typename Q>
        std::pair<size_t, bool> _find(size_t h, Q&& q) const {
            if (_capacity == 0)
                return { 0, false };
            size_t d = 0;
            size_t i = _mask(h);
            for (;;) {
                if (_data[i].vacant())
                    return { i, false };
                if (_data[i].equivalent(h, q))
                    return { i, true };
                size_t e = _displacement(i);
                if (e < d)
                    return {i, false };
                i = _succ(i);
                ++d;
            }
        }
        
        template<typename Q>
        std::pair<size_t, bool> _erase(size_t h, Q&& q) const {
            auto [i, f] = _find(h, std::forward<Q>(q));
            if (f) {
                size_t j = _give_to_the_poor(i);
                _data[j].vacate();
            }
            return {i, f};
        }
        
        void _erase_present_at(size_t i) const {
            i = _give_to_the_poor(i);
            _data[i].vacate();
        }
        
        template<typename Q>
        size_t _erase_present(size_t h, Q&& q) const {
            size_t i = _find_present(h, std::forward<Q>(q));
            _erase_present_at(i);
            return i;
        }
        
        template<typename Q>
        size_t _assign_present(size_t h, Q&& q) const {
            size_t i = _find_present(h, q);
            _data[i].assign(std::forward<Q>(q));
            return i;
        }
        
        template<typename... Q>
        void _assign_present_at(size_t i, Q&&... q) const {
            _data[i].assign(std::forward<Q>(q)...);
        }
        
        template<typename... Q>
        void _insert_absent_at(size_t i, Q&&... q) {
            if (_data[i].occupied())
                _steal_from_the_rich(i);
            _data[i].assign(std::forward<Q>(q)...);
        }
        
        template<typename... Q>
        void _emplace_absent_at(size_t i, Q&&... q) {
            if (_data[i].occupied())
                _steal_from_the_rich(i);
            _data[i].emplace(std::forward<Q>(q)...);
        }
        
        template<typename Q>
        size_t _insert_absent(size_t h, Q&& q) {
            size_t i = _find_absent(h, q);
            _insert_absent_at(i, std::forward<Q>(q));
            return i;
        }
        
        template<typename Q>
        bool _insert_or_assign(size_t h, Q&& q) {
            auto [i, f] = _find(h, q);
            if (!f && _data[i].occupied())
                _steal_from_the_rich(i);
            _data[i].assign(std::forward<Q>(q));
            return !f;
        }
        
        template<typename Q>
        bool _try_insert(size_t h, Q&& q) {
            auto [i, f] = _find(h, q);
            if (!f) {
                // not found; insert
                if (_data[i].occupied())
                    _steal_from_the_rich(i);
                _data[i].assign(std::forward<Q>(q));
            }
            return !f;
        }
        
        size_t _threshold() const {
            return _capacity - (_capacity >> 2);
        }
        
    }; // BasicHashSetA
    
    
    // BasicHashSetC
    //
    // owns storage, counts occupants, knows if full
    // still not dynamically resized, but can be manually resized when empty
    
    template<typename T>
    struct BasicHashSetB {
        BasicHashSetA<T> _inner;
        size_t _size;
        Scan<ArrayStaticIndirect<T>*> _storage;
        
        const T* begin() const {
            return _inner._data;
        }
        
        const T* end() const {
            return _inner._data + _inner._capacity;
        }
        
        BasicHashSetB()
        : _inner()
        , _size(0)
        , _storage() {
        }
        
        BasicHashSetB(BasicHashSetB&& other)
        : _inner(std::move(other._inner))
        , _size(exchange(other._size, 0))
        , _storage(std::move(other._storage)) {
        }
        
        BasicHashSetB& operator=(BasicHashSetB&& other)
        {
            _inner = std::move(other._inner);
            _size = exchange(other._size, 0);
            _storage = std::move(other._storage);
            return *this;
        }
        
        void _invariant() {
            assert(_size < _inner._capacity || _size == 0);
            size_t n = _inner._invariant();
            assert(n == _size);
        }
        
        void clear() {
            _inner._data = nullptr;
            _inner._capacity = 0;
            _size = 0;
            _storage = nullptr;
        }
        
        void _reserve(size_t new_capacity) {
            assert(_size == 0);
            assert(std::has_single_bit(new_capacity));
            auto* p = new ArrayStaticIndirect<T>(new_capacity);
            _inner._data = p->data();
            _inner._capacity = new_capacity;
            _size = 0;
            _storage = p;
        }
        
        bool empty() const {
            return _size == 0;
        }
        
        bool full() const {
            // notably, true when the capacity is zero
            assert(_size <= _inner._threshold());
            return _size == _inner._threshold();
        }
        
        size_t capacity() const {
            return _inner.capacity();
        }
        
        size_t size() const {
            return _size;
        }
        
        size_t _insert_absent_hash(size_t h, T&& x) {
            size_t i = _inner._insert_absent(h, std::move(x));
            assert(!full());
            ++_size;
            return i;
        }
        
        size_t _insert_absent(T&& x) {
            size_t h = x._hash();
            return _insert_absent_hash(h, std::move(x));
        }
        
        template<typename... R>
        void _insert_absent_at(size_t i, R&&... r) {
            _inner._insert_absent_at(i, std::forward<R>(r)...);
            assert(!full());
            ++_size;
        }
        
        template<typename... R>
        void _emplace_absent_at(size_t i, R&&... r) {
            _inner._emplace_absent_at(i, std::forward<R>(r)...);
            assert(!full());
            ++_size;
        }
        
        void _erase_present_at(size_t i) {
            _inner._erase_present_at(i);
            assert(_size);
            --_size;
        }
        
        template<typename... R>
        void _assign_present_at(size_t i, R&&... r) {
            _inner._assign_present_at(i, std::forward<R>(r)...);
        }
        
        template<typename Q>
        bool _erase(size_t h, Q&& q) {
            if (_size == 0)
                return false;
            auto [i, f] = _inner._erase(h, std::forward<Q>(q));
            if (f)
                --_size;
            return f;
        }
        
        
    };
    
    template<typename T>
    void garbage_collected_scan(const BasicHashSetB<T>& self) {
        garbage_collected_scan(self._storage);
    }
    
    template<typename T>
    void garbage_collected_shade(const BasicHashSetB<T>& self) {
        garbage_collected_shade(self._storage);
    }
    
    
    // ?
    // object_shade
    // object_trace
    // !object_scan
    
    // BasicHashSetC
    //
    // Real time dynamic sized HashSet
    //
    // Contains two HashSetB.  When alpha fills up, it is moved into beta
    // and an empty map with twice the capacity is installed in alpha.
    // Subsequent operations are _taxed_ to incrementally move the elements of
    // beta into alpha, such that beta is fully drained before alpha itself can
    // become full.
    //
    // Thus all our operations are bounded to be less than the sum of
    // - a calloc of any size
    // - a bounded number (3?) of probes, themselves probabilistically bounded
    //     by the Robin Hood policy and the load factor.
    //
    // The average operation will be cheaper than this worst case, but
    // operations close in time will not be independently cheap, as early in
    // the copy lookups will miss on alpha a lot, and unlucky long probes will
    // be encountered repeatedly.
    //
    // This is quite a complicated bound but it is for practical purposes
    // true constant time, compared to the amortized constant time with
    // guaranteed O(N) hiccoughs of a non-incremental resize.
    //
    // The associated cost in manager size is x2, in heap size x1.5 while
    // the old allocation is copied from, and in runtime is actually not much
    // worse since the copies must happen at some time in either scheme;
    // In a read-heavy workload, once the last incremental resize is completed
    // the only overhead is that find must check that _beta is nonempty.
    
    template<typename T>
    struct BasicHashSetC {
        
        BasicHashSetB<T> _alpha;
        BasicHashSetB<T> _beta;
        size_t _partition = 0;
        
        BasicHashSetC()
        : _alpha()
        , _beta()
        , _partition(0) {
        }
        
        BasicHashSetC(BasicHashSetC&& other)
        : _alpha(std::move(other._alpha))
        , _beta(std::move(other._beta))
        , _partition(exchange(other._partition, 0)) {
        }
        
        BasicHashSetC& operator=(BasicHashSetC&& other) {
            _alpha = std::move(other._alpha);
            _beta = std::move(other._beta);
            _partition = exchange(other._partition, 0);
            return *this;
        }
        
        
        void _invariant() {
            _alpha._invariant();
            _beta._invariant();
            assert(_partition <= _beta.capacity());
            assert(_alpha.size() + _beta.size() <= _alpha._inner._threshold());
        }
        
        void _tax() {
            // TODO: messy
            if (_beta._inner._data) {
                if (!_beta.empty()) {
                    T& t = _beta._inner._data[_partition];
                    if (t.occupied()) {
                        _alpha._insert_absent(std::move(t));
                        _beta._erase_present_at(_partition);
                        if (_beta.empty()) {
                            _beta.clear();
                            _partition = -1;
                        }
                    }
                } else {
                    _beta.clear();
                    _partition = -1;
                }
                _partition = _beta._inner._succ(_partition);
            }
        }
        
        void _tax2() {
            _tax();
            _tax();
        }
        
        void _ensure_not_full() {
            if (_alpha.full()) {
                assert(_beta.empty());
                _beta = std::move(_alpha);
                _partition = 0;
                // TODO: clear, reserve
                _alpha.clear();
                _alpha._reserve(max(_beta.capacity() << 1, 4));
            }
        }
        
        size_t size() const {
            return _alpha.size() + _beta.size();
        }
        
        template<typename Q>
        Pair<size_t, bool> _find(size_t h, Q&& q) {
            _tax();
            auto [i, f] = _alpha._inner._find(h, q);
            if (f)
                return {i, f};
            auto [j, g] = _beta._inner._find(h, q);
            if (!g)
                return {i, f};
            _alpha._insert_absent_at(i, std::move(_beta._inner._data[j]));
            _beta._erase_present_at(j);
            return {i, g};
        }
        
        template<typename Q>
        bool _erase(size_t h, Q&& q) {
            _tax();
            return _alpha._erase(h, q) || _beta._erase(h, q);
        }
        
        template<typename Q, typename U>
        bool _insert_or_assign(size_t h, Q&& q, U&& u) {
            _tax2();
            _ensure_not_full();
            _invariant();
            auto [i, f] = _alpha._inner._find(h, q);
            _invariant();
            if (f) {
                _alpha._assign_present_at(i, std::forward<Q>(q), std::forward<U>(u));
            } else {
                _invariant();
                _beta._erase(h, q);
                _invariant();
                _alpha._insert_absent_at(i, std::forward<Q>(q), std::forward<U>(u));
                _invariant();
            }
            return !f;
        }
        
        template<typename Q>
        size_t _find_or_emplace(size_t h, Q&& q) {
            _tax2();
            _ensure_not_full();
            _invariant();
            auto [i, f] = _alpha._inner._find(h, q);
            _invariant();
            if (f) {
                // Found in alpha, return
                return i;
            }
            auto [j, g] = _beta._inner._find(h, q);
            if (g) {
                // Found in beta, move over
                // _beta.erase(h, q);
                i = _alpha._insert_absent_hash(h, std::move(_beta._inner._data[j]));
                _beta._erase_present_at(j);
                _invariant();
                return i;
            }
            _invariant();
            _alpha._emplace_absent_at(i, std::forward<Q>(q));
            _invariant();
            return i;
        }
        
        
        bool empty() const {
            return _alpha.empty() && _beta.empty();
        }
        
        void clear() {
            _alpha.clear();
            _beta.clear();
            _partition = 0;
        }
        
        template<typename Q>
        bool _insert(size_t h, Q&& q) {
            _tax2();
            _ensure_not_full();
            _invariant();
            auto [i, f] = _alpha._inner._find(h, q);
            if (f)
                return false;
            auto [j, g] = _beta._inner._find(h, q);
            if (g)
                return false;
            _invariant();
            _alpha._insert_absent_at(i, std::forward<Q>(q));
            return true;
        }
        
    }; // BasicHashSetC
    
    template<typename T>
    void garbage_collected_scan(const BasicHashSetC<T>& self) {
        garbage_collected_scan(self._alpha);
        garbage_collected_scan(self._beta);
    }
    
    template<typename T>
    void garbage_collected_shade(const BasicHashSetC<T>& self) {
        garbage_collected_shade(self._alpha);
        garbage_collected_shade(self._beta);
    }
    
    
    template<typename K, typename V>
    struct GCHashMap {
        
        // TODO: Traced<K>
        // More generally, decide on Traced by some typefunction to capture
        // pointers convertible Object and Values and...
        
        using T = BasicEntry<K, V>;
        
        // TODO: sort out const-correctness
        mutable BasicHashSetC<T> _inner;
        
        void _invariant() const {
            _inner._invariant();
        }
        
        size_t size() const {
            return _inner.size();
        }
        
        template<typename Q>
        auto read(Q&& q) const {
            size_t h = hash(q);
            auto [i, f] = _inner._find(h, q);
            using U = decltype(_inner._alpha._inner._data[i]._kv.second);
            return f ? any_read(_inner._alpha._inner._data[i]._kv.second) : any_none<U>;
        }
        
        template<typename J, typename U>
        void write(J&& j, U&& u) {
            size_t h = hash(j);
            _inner._insert_or_assign(h, j, u);
        }
        
        template<typename Q>
        void erase(Q&& q) {
            size_t h = hash(q);
            _inner._erase(h, q);
        }
        
        bool empty() const {
            return _inner.empty();
        }
        
        template<typename Q>
        bool contains(Q&& q) const {
            return _inner._find(hash(q), q).second;
        }
        
        // aka the notorious std::map::operator[]
        template<typename Q>
        V& find_or_emplace(Q&& q) {
            size_t h = hash(q);
            size_t i = _inner._find_or_emplace(h, std::forward<Q>(q));
            assert(_inner._alpha._inner._data);
            return _inner._alpha._inner._data[i]._kv.second;
        }
        
        template<typename Q>
        std::pair<K, V>* find(Q&& q) {
            size_t h = hash(q);
            auto [i, f] = _inner._find(h, q);
            return f ? &_inner._alpha._inner._data[i]._kv : nullptr;
        }
        
        
    };
    
    template<typename K, typename V>
    void garbage_collected_scan(const GCHashMap<K, V>& self) {
        garbage_collected_scan(self._inner);
    }
    
    template<typename K, typename V>
    void garbage_collected_shade(const GCHashMap<K, V>& self) {
        garbage_collected_shade(self._inner);
    }
    
    //
    //
    //    template<typename K, typename A>
    //    struct HashMap<K, A*> {
    //
    //        // TODO: Traced<K>
    //        // More generally, decide on Traced by some typefunction to capture
    //        // pointers convertible Object and Values and...
    //
    //        using T = BasicEntry<K, Scan<A*>>;
    //
    //        // TODO: sort out const-correctness
    //        mutable BasicHashSetC<T> _inner;
    //
    //        void _invariant() const {
    //            _inner._invariant();
    //        }
    //
    //        size_t size() const {
    //            return _inner.size();
    //        }
    //
    //        template<typename Q>
    //        A* read(Q&& q) const {
    //            size_t h = hash(q);
    //            auto [i, f] = _inner._find(h, q);
    //            return f ? _inner._alpha._inner._data[i]._kv.second : nullptr;
    //        }
    //
    //        template<typename J, typename U>
    //        void write(J&& j, U&& u) {
    //            size_t h = hash(j);
    //            _inner._insert_or_assign(h, j, u);
    //        }
    //
    //        template<typename Q>
    //        void erase(Q&& q) {
    //            size_t h = hash(q);
    //            _inner._erase(h, q);
    //        }
    //
    //        bool empty() const {
    //            return _inner.empty();
    //        }
    //
    //        template<typename Q>
    //        bool contains(Q&& q) const {
    //            return _inner._find(hash(q), q).second;
    //        }
    //
    //    };
    //
    //    template<typename K, typename V>
    //    void object_trace(const HashMap<K, V>& self) {
    //        return object_trace(self._inner);
    //    }
    //
    //    template<typename K, typename V>
    //    void garbage_collected_shade(const HashMap<K, V>& self) {
    //        return garbage_collected_shade(self._inner);
    //    }
    
    
    
    
    struct HeapHashMap : HeapValue {
        
        GCHashMap<Scan<Value>, Scan<Value>> _inner;
        
        void _invariant() const {
            _inner._invariant();
        }
        
        virtual void _garbage_collected_scan() const override {
            garbage_collected_scan(_inner);
        }
        
        
        virtual bool _value_empty() const override {
            _invariant();
            return _inner.empty();
        }
        
        virtual Value _value_erase(Value key) override {
            _invariant();
            // TODO: single lookup
            Value result = _inner.read(key);
            _inner.erase(key);
            return result;
        }
        
        virtual Value _value_find(Value key) const override {
            _invariant();
            return _inner.read(key);
        }
        
        virtual bool _value_contains(Value key) const override {
            _invariant();
            return _inner.contains(key);
        }
        
        virtual Value _value_insert_or_assign(Value key, Value value) override {
            _invariant();
            Value result = _inner.read(key);
            _inner.write(key, value);
            return result;
        }
        
        virtual size_t _value_size() const override {
            _invariant();
            return _inner.size();
        }
        
    };
    
    
    
    
    
    
    template<typename K>
    struct BasicHashSetEntry {
        K _key;
        bool _occupied;
        
        bool occupied() const { return _occupied; }
        bool vacant() const { return !_occupied; }
        void vacate() {
            assert(_occupied);
            _occupied = false;
        }
        
        size_t hash() const {
            return hash(_key);
        }
        
        template<typename J>
        static size_t hash(const J& j) {
            return any_hash(j);
        }
        
        void assign(BasicHashSetEntry&& other) {
            assert(other._occupied);
            _key = std::move(other._key);
            _occupied = true;
        }
        
        template<typename J>
        void assign(J&& j) {
            _key = std::forward<J>(j);
            _occupied = true;
        }
        
        template<typename J>
        bool equivalent(size_t h, J&& j) const {
            assert(_occupied);
            return _key == j;
        }
        
    };
    
    
    
    template<typename K>
    void garbage_collected_scan(const BasicHashSetEntry<K>& e) {
        garbage_collected_scan(e._key);
    }
    
    template<typename K>
    void garbage_collected_shade(const BasicHashSetEntry<K>& e) {
        garbage_collected_shade(e._key);
    }
    
    template<typename K>
    void any_debug(const BasicHashSetEntry<K>& e) {
        any_debug(e._key);
    }
    
    template<typename K>
    ValueHash any_hash(const BasicHashSetEntry<K>& e) {
        return any_hash(e._key);
    }
    
    
    template<typename K>
    void any_trace_weak(const BasicHashSetEntry<K>& e) {
        trace(e);
    }
    
    
    
    
    
    
    
    template<typename K>
    struct GCHashSet {
        
        // TODO: Traced<K>
        // More generally, decide on Traced by some typefunction to capture
        // pointers convertible Object and Values and...
        
        using T = BasicHashSetEntry<K>;
        
        // TODO: sort out const-correctness
        mutable BasicHashSetC<T> _inner;
        
        void _invariant() const {
            _inner._invariant();
        }
        
        size_t size() const {
            return _inner.size();
        }
        
        template<typename J>
        void write(J&& j) {
            size_t h = hash(j);
            _inner._insert_or_assign(h, j);
        }
        
        template<typename Q>
        void erase(Q&& q) {
            size_t h = hash(q);
            _inner._erase(h, q);
        }
        
        bool empty() const {
            return _inner.empty();
        }
        
        template<typename Q>
        bool contains(Q&& q) const {
            return _inner._find(hash(q), q).second;
        }
        
        void clear() {
            _inner.clear();
        }
        
        template<typename Q>
        bool insert(Q&& q) {
            size_t h = hash(q);
            return _inner._insert(h, std::forward<Q>(q));
        }
        
    };
    
    template<typename K>
    void garbage_collected_scan(const GCHashSet<K>& self) {
        garbage_collected_scan(self._inner);
    }
    
    template<typename K>
    void garbage_collected_shade(const GCHashSet<K>& self) {
        return garbage_collected_shade(self._inner);
    }
    
    
#endif
    
} // namespace wry

#endif /* wry_gc_HeapTable_hpp */


//
//  Scan.hpp
//  client
//
//  Created by Antony Searle on 16/6/2024.
//

#ifndef Scan_hpp
#define Scan_hpp

#if 0

//#include "adl.hpp"
#include "garbage_collected.hpp"

namespace wry {
    
    // Scan<T> indicates that the payload wants to be scanned by the garbage
    // collector.
    //     T* const - immutable
    //     T* - mutable, must be atomic internally, collector will ACQUIRE
    //     Atomic<T*> - explicitly atomic, multiple mutator threads may access
    
    template<typename T>
    struct Scan;
    
    template<PointerConvertibleTo<GarbageCollected> T>
    struct Scan<T* const> {
        
        T* const _object;
        
        Scan() : _object(nullptr) {}
        Scan(const Scan& other) = default;
        Scan(Scan&& other) = default;
        explicit Scan(auto* const& other) : _object(other) {}
        Scan(std::nullptr_t) : _object(nullptr) {}
        ~Scan() = default;
        Scan& operator=(const Scan& other) = delete;
        Scan& operator=(Scan&& other) = delete;
        
        T* operator->() const { return _object; }
        bool operator!() const { return !_object; }
        explicit operator bool() const { return static_cast<bool>(_object); }
        operator T*() const { return _object; }
        T& operator*() const { assert(_object); return *_object; }
        bool operator==(const Scan& other) const = default;
        std::strong_ordering operator<=>(const Scan& other) const = default;
        bool operator==(auto* const& other) const { return _object == other; }
        bool operator==(std::nullptr_t) const { return _object == nullptr; }
        std::strong_ordering operator<=>(auto* const& other) const { return _object <=> other; }
        std::strong_ordering operator<=>(std::nullptr_t) const { return _object <=> nullptr; }
        
        T* get() const { return _object; }
        void unsafe_set(T*);
        
    }; // Scan<T*const>
    
    template<std::derived_from<GarbageCollected> T>
    struct Scan<T*> {
        
        Atomic<T*> _object;
        
        Scan() = default;
        Scan(const Scan& other);
        Scan(Scan&& other);
        explicit Scan(T*const& other);
        Scan(std::nullptr_t);
        ~Scan() = default;
        Scan& operator=(const Scan& other);
        Scan& operator=(Scan&& other);
        Scan& operator=(T*const& other);
        Scan& operator=(std::nullptr_t);
        
        void swap(Scan<T*>& other);
        
        T* operator->() const;
        bool operator!() const;
        explicit operator bool() const;
        operator T*() const;
        T& operator*() const;
        bool operator==(const Scan& other) const;
        auto operator<=>(const Scan& other) const;
        
        T* get() const;
        T* take();
        
    }; // struct Traced<T*>
    
    template<std::derived_from<GarbageCollected> T>
    struct Scan<Atomic<T*>> {
        
        Atomic<T*> _object;
        
        Scan() = default;
        Scan(const Scan&) = delete;
        explicit Scan(T* object);
        Scan(std::nullptr_t);
        Scan& operator=(const Scan&) = delete;
        
        T* load(Ordering order) const;
        void store(T* desired, Ordering order);
        T* exchange(T* desired, Ordering order);
        bool compare_exchange_weak(T*& expected, T* desired, Ordering success, Ordering failure);
        bool compare_exchange_strong(T*& expected, T* desired, Ordering success, Ordering failure);
        
    }; // struct Traced<Atomic<T*>>
    
    
} // namespace wry

namespace wry {
    
    template<std::derived_from<GarbageCollected> T>
    Scan<T*>::Scan(const Scan& other)
    : Scan(other.get()) {
    }
    
    template<std::derived_from<GarbageCollected> T>
    Scan<T*>::Scan(Scan&& other)
    : Scan(other.take()) {
    }
    
    template<std::derived_from<GarbageCollected> T>
    Scan<T*>& Scan<T*>::operator=(const Scan& other) {
        return operator=(other.get());
    }
    
    template<std::derived_from<GarbageCollected> T>
    Scan<T*>& Scan<T*>::operator=(Scan&& other) {
        return operator=(other.take());
    }
    
    template<std::derived_from<GarbageCollected> T>
    Scan<T*>::Scan(T*const& other)
    : _object(other) {
    }
    
    template<std::derived_from<GarbageCollected> T>
    Scan<T*>::Scan(std::nullptr_t)
    : _object(nullptr) {
    }
    
    template<std::derived_from<GarbageCollected> T>
    void Scan<T*>::swap(Scan<T*>& other) {
        T* a = get();
        T* b = other.get();
        _object._store(b);
        other._object._store(a);
        garbage_collected_shade(a);
        garbage_collected_shade(b);
    }
    
    template<std::derived_from<GarbageCollected> T>
    Scan<T*>& Scan<T*>::operator=(T*const& other) {
        // Safety:
        //     An atomic::exchange is not used here because this_thread is
        // the only writer.
        T* discovered = get();
        _object.store(other, Ordering::RELEASE);
        garbage_collected_shade(discovered);
        garbage_collected_shade(other);
        return *this;
    }
    
    template<std::derived_from<GarbageCollected> T>
    Scan<T*>& Scan<T*>::operator=(std::nullptr_t) {
        // Safety:
        //     See above.
        T* discovered = get();
        _object.store(nullptr, Ordering::RELAXED);
        garbage_collected_shade(discovered);
        return *this;
    }
    
    template<std::derived_from<GarbageCollected> T>
    T* Scan<T*>::operator->() const {
        return _object.load(Ordering::RELAXED);
    }
    
    template<std::derived_from<GarbageCollected> T>
    bool Scan<T*>::operator!() const {
        return !get();
    }
    
    template<std::derived_from<GarbageCollected> T>
    Scan<T*>::operator bool() const {
        return (bool)get();
    }
    
    template<std::derived_from<GarbageCollected> T>
    Scan<T*>::operator T*() const {
        return get();
    }
    
    template<std::derived_from<GarbageCollected> T>
    T& Scan<T*>::operator*() const {
        return *get();
    }
    
    template<std::derived_from<GarbageCollected> T>
    bool Scan<T*>::operator==(const Scan& other) const {
        return get() == other.get();
    }
    
    template<std::derived_from<GarbageCollected> T>
    auto Scan<T*>::operator<=>(const Scan& other) const {
        return get() <=> other.get();
    }
    
    template<std::derived_from<GarbageCollected> T>
    T* Scan<T*>::get() const {
        return _object.load(Ordering::RELAXED);
    }
    
    template<std::derived_from<GarbageCollected> T>
    T* Scan<T*>::take() {
        T* discovered = get();
        _object.store(nullptr, Ordering::RELAXED);
        garbage_collected_shade(discovered);
        return discovered;
    }
    
    template<std::derived_from<GarbageCollected> T>
    Scan<Atomic<T*>>::Scan(T* object)
    : _object(object) {
    }
    
    template<std::derived_from<GarbageCollected> T>
    T* Scan<Atomic<T*>>::load(Ordering order) const {
        return _object.load(order);
    }
    
    template<std::derived_from<GarbageCollected> T>
    void Scan<Atomic<T*>>::store(T* desired, Ordering order) {
        (void) exchange(desired, order);
    }
    
    template<std::derived_from<GarbageCollected> T>
    T* Scan<Atomic<T*>>::exchange(T* desired, Ordering order) {
        T* discovered = _object.exchange(desired, order);
        garbage_collected_shade(discovered);
        garbage_collected_shade(desired);
        return discovered;
    }
    
    template<std::derived_from<GarbageCollected> T>
    bool Scan<Atomic<T*>>::compare_exchange_weak(T*& expected, T* desired, Ordering success, Ordering failure) {
        bool result = _object.compare_exchange_weak(expected, desired, success, failure);
        if (result) {
            garbage_collected_shade(expected);
            garbage_collected_shade(desired);
        }
        return result;
    }
    
    template<std::derived_from<GarbageCollected> T>
    bool Scan<Atomic<T*>>::compare_exchange_strong(T*& expected, T* desired, Ordering success, Ordering failure) {
        bool result = _object.compare_exchange_strong(expected, desired, success, failure);
        if (result) {
            garbage_collected_shade(expected);
            garbage_collected_shade(desired);
        }
        return result;
    }
    
    template<PointerConvertibleTo<GarbageCollected> T>
    void garbage_collected_scan(const Scan<T* const>& self) {
        garbage_collected_scan(self._object);
    }
    
    template<PointerConvertibleTo<GarbageCollected> T>
    void garbage_collected_scan(const Scan<T*>& self) {
        garbage_collected_scan(self._object.load(Ordering::ACQUIRE));
    }
    
    template<PointerConvertibleTo<GarbageCollected> T>
    void garbage_collected_scan(const Scan<Atomic<T*>>& self) {
        const T* a = self.load(Ordering::ACQUIRE);
        garbage_collected_scan(a);
    }
    
    template<PointerConvertibleTo<GarbageCollected> T>
    void garbage_collected_shade(const Scan<T* const>& self) {
        if (self._object)
            self._object->_garbage_collected_shade();
    }
    
    template<PointerConvertibleTo<GarbageCollected> T>
    void garbage_collected_shade(const Scan<T*>& self) {
        const T* a = self.get();
        if (a)
            a->_garbage_collected_shade();
    }
    
    template<PointerConvertibleTo<GarbageCollected> T>
    void garbage_collected_shade(const Scan<Atomic<T*>>& self) {
        const T* a = self.load(Ordering::ACQUIRE);
        if (a)
            a->_garbage_collected_shade();
    }
    
    template<typename T>
    T* any_read(const Scan<T* const>& self) {
        return self._object;
    }
    
    template<typename T>
    T* any_read(const Scan<T*>& self) {
        return self.get();
    }
    
    template<typename T>
    T* any_read(const Scan<Atomic<T*>>& self) {
        return self.load(std::memory_order_acquire);
    }
    
    template<typename T>
    inline constexpr T* any_none<Scan<T* const>> = nullptr;
    
    template<typename T>
    inline constexpr T* any_none<Scan<T*>> = nullptr;
    
    template<typename T>
    inline constexpr T* any_none<Scan<Atomic<T*>>> = nullptr;
    
    
    template<PointerConvertibleTo<GarbageCollected> T>
    void any_trace_weak(const Scan<T*const>& self) {
        trace_weak(self._object);
    }
    
} // namespace wry

#endif

#endif /* Scan_hpp */



template<typename T>
using Queue = std::queue<T, ContiguousDeque<T>>;

// A major pattern in our application is building a queue of Entity* without
// duplicates, then draining it.  These objects will typically have a small
// number of elements and brief lives, making designs with bad asymptotic
// performance worth considering.
//
// The not-prematurely-optimized version is { Queue<T>, HashSet<T> }, which
// gives us a "perfect" amortized O(1) try_push and O(N) storage.  The
// constant factors in both cases are substantial though.
//
// A Queue<T> with a linear search scales poorly but may win up to hundreds
// of elements.
//
// A Bloom filter with just a 64-bit hash or even just the Entity* bits
// will speed up only the very small cases that linear search already
// excels on :(
//
// Deferring the de-duplication to drain time costs the storage of the
// duplicates, but we only have to store the HashSet briefly.




// The data structures below preserve ordering like a queue, but reject
// duplicates like a set.  They make differet tradeoffs that may be
// interesting in the regime of small numbers of elements.

template<typename T>
struct QueueOfUniqueByFind {
    
    Queue<T> queue;
    
    // O(n)
    bool push(auto&& key) {
        if (queue.c.contains(key))
            return false;
        queue.push(std::forward<decltype(key)>(key));
        return true;
    }
    
    const T& front() const {
        return queue.front();
    }
    
    const T& back() const {
        return queue.back();
    }
    
    bool empty() const {
        return queue.empty();
    }
    
    size_type size() const {
        return queue.size();
    }
    
    void pop() const {
        queue.pop();
    }
    
    void swap(QueueOfUniqueByFind& other) {
        queue.swap(other.queue);
    }
    
};

template<typename T>
struct QueueOfUniqueByBloomOrFind {
    
    Queue<T> queue;
    
    uint64_t filter = {};
    
    bool push(auto&& key) {
        uint64_t h = hash(key);
        if (((filter & h) == h) && queue.c.contains(key))
            return false;
        queue.push(std::forward<decltype(key)>(key));
    }
    
};


#if 0

// This QueueOfUnique is scalable, with amortized O(1) push, but has
// significant overhead
//
// It maintains both an Array and a HashSet of the elements, the former
// encoding order and the latter encoding membership.  As the first
// use case is for pointer type and pointer equality, we store them
// directly in each set.  Other uses may prefer one of the containers to
// refer into the elements of the other (array index, if erasure is
// prohibited)
//
// The best queue for our applications may switch strategy from find
// to hash set membership

template<typename T>
struct QueueOfUnique {
    
    /*
     using value_type = typename Array<T>::value_type;
     using size_type = typename Array<T>::size_type;
     using reference = typename Array<T>::const_reference;
     using const_reference = typename Array<T>::const_reference;
     using iterator = typename Array<T>::iterator;
     using const_iterator = typename Array<T>::const_iterator;
     */
    
    GCArray<T> queue;
    GCHashSet<T> set;
    
    // invariant
    
    void _invariant() const {
        // the queue has no duplicates
        for (auto i = queue.cbegin(); i != queue.cend(); ++i) {
            for (auto j = queue.cbegin(); i != queue.cend(); ++j) {
                assert((*i == *j) == (i == j));
            }
        }
        // the queue and set represent the same sequence
        assert(queue.size() == set.size());
        // every queue item appears once in the set
        for (const auto& key : queue)
            assert(set.count(key) == 1);
        // every set item appears once in the queue
        for (const auto& key : set)
            assert(std::count(queue.cbegin(), queue.cend(), key) == 1);
    }
    
    // as regular type
    
    QueueOfUnique() = default;
    QueueOfUnique(const QueueOfUnique&) = delete;
    QueueOfUnique(QueueOfUnique&&) = default;
    ~QueueOfUnique() = default;
    QueueOfUnique& operator=(const QueueOfUnique&) = delete;
    QueueOfUnique& operator=(QueueOfUnique&&) = default;
    
    QueueOfUnique(ContiguousDeque<T>&& a, GCHashSet<T>&& b)
    : queue(std::move(a))
    , set(std::move(b)) {
    }
    
    std::pair<Queue<T>, GCHashSet<T>> destructure() {
        return std::pair<Queue<T>, GCHashSet<T>>(std::move(queue),
                                                 std::move(set));
    }
    
    void swap(QueueOfUnique& other) {
        queue.swap(other.queue);
        set.swap(other.set);
    }
    
    // as immutable sequence
    
    /*
     const_iterator begin() const { return queue.begin(); }
     const_iterator end() const { return queue.end(); }
     const_iterator cbegin() const { return queue.cbegin(); }
     const_iterator cend() const { return queue.cend(); }
     const_reference front() const { return queue.front(); }
     const_reference back() const { return queue.back(); }
     */
    bool empty() const { return queue.empty(); }
    auto size() const { return queue.size(); }
    
    // as STL queue
    
    void push(auto&& key) {
        (void) try_push(std::forward<decltype(key)>(key));
    }
    
    template<typename U>
    void push_range(QueueOfUnique<U>&& source) {
        // the uniqueness of source can't help us here
        size_t n = source.queue.size();
        //for (auto& value : source.queue)
        //    push(std::move(value));
        for (size_t i = 0; i != n; ++i)
            push(source.queue[i]);
        source.clear();
    }
    
    void emplace(auto&&... args) {
        (void) try_emplace(std::forward<decltype(args)>(args)...);
    }
    
    void pop() {
        set.erase(queue.front());
        queue.pop_front();
    }
    
    // as STL set
    
    void clear() {
        queue.clear();
        set.clear();
    }
    
    size_type count(auto&& value) {
        return set.count(std::forward<decltype(value)>(value));
    }
    
    bool contains(auto&& value) {
        return set.contains(std::forward<decltype(value)>(value));
    }
    
    // as extended queue
    
    void pop_into(T& victim) {
        victim = std::move(queue.front());
        queue.pop_front();
        set.erase(victim);
    }
    
    T take_one() {
        T value{std::move(queue.front())};
        queue.pop_front();
        set.erase(value);
        return value;
    }
    
    bool try_push(auto&& value) {
        bool flag = set.insert(value);
        if (flag) {
            queue.push_back(std::forward<decltype(value)>(value));
        }
        return flag;
    }
    
    bool try_emplace(auto&&... args) {
        return try_push(T{std::forward<decltype(args)>(args)...});
    }
    
    bool try_pop_into(T& victim) {
        if (queue.empty())
            return false;
        pop_into(victim);
    }
    
};

// static_assert(std::is_move_assignable_v<QueueOfUnique<Scan<Object*>>>);
// static_assert(std::is_move_assignable_v<HashSet<Scan<Object*>>>);

template<typename T>
void garbage_collected_scan(const QueueOfUnique<T>& self) {
    garbage_collected_scan(self.queue);
    garbage_collected_scan(self.set);
}

template<typename T>
void garbage_collected_shade(const QueueOfUnique<T>& self) {
    garbage_collected_shade(self.queue);
    garbage_collected_shade(self.set);
}

#endif
