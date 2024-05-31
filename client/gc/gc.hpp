//
//  gc.hpp
//  client
//
//  Created by Antony Searle on 26/5/2024.
//

#ifndef gc_hpp
#define gc_hpp

#include <cassert>
#include <cstdint>

#include <atomic>
#include <deque>
#include <vector>

#include "bag.hpp"
#include "utility.hpp"

namespace gc {
        
    using Color = std::intptr_t;
    struct Object;
    
    struct Mutator;
    struct Collector;
    
    
    // TODO:
    //
    // Almost all the mutator and collector state can be hidden.  We need to
    // narrowly define the interfaces we absolutely need to be public to
    // support
    // - basic use
    // - inlining critical functions
    // - templates
    //
    // since TLS lookup is potentially expensive, we want to provide interfaces
    // that make it easy to pass in an explict context.
    
    
    struct MutatorContext {
        
        // needs these interface functions:
        
        [[nodiscard]] std::pair<void*, Color> allocate(std::size_t bytes);
        
        void shade(Object*);
        
        
        
    };
    
    struct CollectorContext {
        // needs these interface functions:
        
    };
    
    // to mark roots we want to be able to shade
    // 
    //     shade(const Object*)
    //
    // which will call
    //
    //
    
    struct ShadeContext;
    struct AllocContext;
    
    
    // color_white_to_gray(context, object)
    // color_white_to_black(context, object)
    
    //

    
    struct Object {
        
        static void* operator new(std::size_t count);
        static void* operator new[](std::size_t count) = delete;
        
        Object();
        virtual ~Object();

        explicit Object(Mutator&);
        
        virtual std::size_t gc_hash() const;
        // return a hash value, by default derived from the object's address

        virtual std::size_t gc_bytes() const;
        // return size in bytes of the backing allocation, which may vary due
        // to flexible array member idiom
                
        virtual void gc_enumerate(Collector&) const;
        // invoke the argument function object on each member object to trace
        // the graph.  invoked only on collector thread
        
        mutable std::atomic<Color> _gc_color;
        // explicitly store the tricolor abstraction color of the object
        
        virtual void _gc_shade(Mutator&) const;
        // shade the object using the mutator palette, from white to gray or
        // black depending is the node has any children
        
        virtual void _gc_trace(Collector&) const;
        // (TODO: name) customize if/how we store ourself in the worklist
        
    }; // struct Object
    
    
    
    
    
    

    
    
    template<typename T>
    struct Pointer {
        
        std::atomic<T*> _ptr;
        
        Pointer() = default;
        Pointer(const Pointer& other);
        ~Pointer() = default;
        Pointer& operator=(const Pointer& other);
        
        explicit Pointer(T* other);
        explicit Pointer(std::nullptr_t);
        Pointer& operator=(T* other);
        Pointer& operator=(std::nullptr_t);
        
        T* operator->() const;
        bool operator!() const;
        explicit operator bool() const;
        operator T*() const;
        T& operator*() const;
        
        bool operator==(const Pointer<T>& other);
        auto operator<=>(const Pointer<T>& other);
        
        T* get() const;
        
    };
    

    
    
    
    
    
    
    
    
    

    template<typename Adaptee>
    struct Leaf;
    
    template<typename T, typename Adaptee>
    struct Array;
        

    
    
    
   

    
    
    
    
    
    
    

    
    // Provides default behavior for objects with no fields to trace
    
    template<typename Adaptee>
    struct Leaf : Adaptee {
        
        template<typename... Args>
        explicit Leaf(Args&&... args);

        virtual ~Leaf() override;
                
        virtual void _gc_shade(Mutator&) const override final;
        virtual void _gc_trace(Collector&) const override;


    };
    
    // Provides default implementation for inline arrays

    template<typename T, typename Adaptee>
    struct Array : Adaptee {
        
        std::size_t _size;
        T _elements[0]; // flexible array member
        
        template<typename... Args>
        explicit Array(std::size_t n, Args&&... args);

        virtual ~Array() override;
        
        virtual std::size_t gc_bytes() const override final;
        virtual void gc_scan(Collector&) const override;
        
    };
    
    
    
    
    
    
    
    
    
    
    
    
    struct Palette {
        struct BlackImposter {
            Color white;
            operator Color() const { return white ^ 1; }
            //BlackImposter& operator=(Color value) { white = value ^ 1; return *this; }
        };
        union {
            struct {
                Color white;
            };
            BlackImposter black;
        };
        Color alloc;
        enum : Color {
            gray = 2,
            red = 3,
        };
    };

    struct Log {
        
        bool dirty;
        Bag<const Object*> allocations;
        std::intptr_t total;
        
        Log()
        : dirty(false)
        , allocations()
        , total(0) {}
        
        Log(const Log&) = delete;
        
        Log(Log&& other)
        : dirty(std::exchange(other.dirty, false))
        , allocations(std::move(other.allocations))
        , total(std::exchange(other.total, 0)) {
            assert(other.allocations.empty());
        }
        
        ~Log() {
            assert(!dirty);
            assert(allocations.empty());
            assert(total == 0);
        }
        
        Log& operator=(const Log&) = delete;
        
        Log& operator=(Log&&) = delete;
                
        Log& splice(Log&& other) {
            dirty = std::exchange(other.dirty, false) || dirty;
            allocations.splice(std::move(other.allocations));
            total += std::exchange(other.total, 0);
            return *this;
        }
        
    };
    
    struct LogNode : Log {
        LogNode* log_stack_next;
    };
    
   
    struct Channel {
        
        // A Channel is shared by one Mutator and one Collector.  They must
        // both release it before it is deleted.
        std::atomic<std::intptr_t> reference_count_minus_one = 1;
        Channel* entrant_stack_next = nullptr;
        enum Tag : std::intptr_t {
            COLLECTOR_DID_REQUEST_NOTHING, // -> HANDSHAKE, LEAVE
            COLLECTOR_DID_REQUEST_HANDSHAKE, // -> WAKEUP, LOGS, LEAVE
            COLLECTOR_DID_REQUEST_WAKEUP, // -> LOGS, LEAVE
            MUTATOR_DID_PUBLISH_LOGS, // -> NOTHING, LEAVE
            MUTATOR_DID_LEAVE, // -> .
        };

        Palette palette;
        std::atomic<TaggedPtr<LogNode>> log_stack_head;
        
        void release() {
            if (!reference_count_minus_one.fetch_sub(1, std::memory_order_release)) {
                (void) reference_count_minus_one.load(std::memory_order_acquire);
                delete this;
            }
        }
        

        
        
    };
    
    // garbage collector state for one mutator thread
    
    struct Mutator {

        // Thread-local state access point

        static thread_local Mutator* _thread_local_context_pointer;

        static Mutator& get();
        [[nodiscard]] static Mutator* _exchange(Mutator*);

        // Thread local state

        Palette palette; // colors received from Collector
        Log     log    ; // activity to publish to Collector

        void _white_to_gray(std::atomic<Color>& color);
        bool _white_to_black(std::atomic<Color>& color) const;
        void shade(const Object*);
        template<typename T> T* write(std::atomic<T*>& target, std::type_identity_t<T>* desired);
        template<typename T> T* write(std::atomic<T*>& target, std::nullptr_t);

        
        
        void* _allocate(std::size_t bytes);
        
        // Mutator endures for the whole thread lifetime
        // Channel is per enter-leave pairing
        // Channel must outlive not just leave, but the shutdown of the thread
        
        
        
        Channel* _channel = nullptr;
        
        void _publish_with_tag(Channel::Tag tag);
        
        void enter();
        void handshake();
        void leave();
        
    };
    
    // garbage collector state for the unique collector thread, which is
    // also a mutator
    
    struct Collector : Mutator {
        
        void visit(const Object* object);
        template<typename T> void visit(const std::atomic<T*>& object);
        template<typename T> void visit(const Pointer<T>& object);
        
        // Safety:
        // _scan_stack is only resized by the Collector thread, which is not
        // real time bounded
        std::vector<const Object*> _scan_stack;

        // These details can be done by a private class
        
        // lockfree channel
        // std::atomic<TaggedPtr<Channel>> channel_list_head;
        std::atomic<Channel*> entrant_stack_head;
        std::atomic<Palette> _atomic_palette;
        
        std::vector<Channel*> _active_channels;
        
        Log _collector_log;
        
        void _process_scan_stack() {
            while (!this->_scan_stack.empty()) {
                const Object* object = this->_scan_stack.back();
                this->_scan_stack.pop_back();
                assert(object);
                object->gc_enumerate(*this);
            }
        }
        
        void collect();
        
        void _set_alloc_to_black();
        void _swap_white_and_black();
        
        void _publish_palette_and_accept_entrants();

        void _initiate_handshakes();
        void _consume_logs(LogNode* logs);
        void _finalize_handshakes();
        void _synchronize();
        
        

        
    };
    
    inline Collector* global_collector = nullptr;
    
    
    
    inline void Mutator::shade(const Object* object) {
        if (object) {
            object->_gc_shade(*this);
        }
    }
    
    // write barrier (non-trivial)
    template<typename T>
    T* Mutator::write(std::atomic<T*>& target, std::type_identity_t<T>* desired) {
        T* discovered = target.exchange(desired, std::memory_order_release);
        this->shade(desired);
        this->shade(discovered);
    }

    template<typename T>
    T* Mutator::write(std::atomic<T*>& target, std::nullptr_t) {
        T* discovered = target.exchange(nullptr, std::memory_order_release);
        this->shade(discovered);
    }

    
    // StrongPtr is a convenience wrapper that implements the write barrier for
    // a strong reference.
    

    inline void* Mutator::_allocate(std::size_t bytes) {
        Object* object = (Object*) malloc(bytes);
        // Safety: we don't use or publish these pointers until we handshake
        // by which time the objects are fully constructed
        this->log.allocations.push(object);
        this->log.total += bytes;
        return object;
    }
    
    inline void Mutator::_white_to_gray(std::atomic<std::intptr_t>& color) {
        std::intptr_t expected = this->palette.white;
        if (color.compare_exchange_strong(expected,
                                          this->palette.gray,
                                          std::memory_order_relaxed,
                                          std::memory_order_relaxed)) {
            this->log.dirty = true;
        }
    }
    
    inline bool Mutator::_white_to_black(std::atomic<std::intptr_t>& color) const {
        std::intptr_t expected = this->palette.white;
        return color.compare_exchange_strong(expected,
                                             this->palette.black,
                                             std::memory_order_relaxed,
                                             std::memory_order_relaxed);
    }
    

    inline void Collector::visit(const Object* object) {
        if (object)
            object->_gc_trace(*this);
    }
    
    template<typename T>
    void Collector::visit(const std::atomic<T*>& participant) {
        this->visit(participant.load(std::memory_order_acquire));
    }
    
    template<typename T>
    void Collector::visit(const Pointer<T>& p) {
        this->visit(p._load_acquire());
    }

    
    inline void* Object::operator new(std::size_t count) {
        return Mutator::get()._allocate(count);
    }
    
    inline Object::Object()
    : _gc_color(Mutator::get().palette.alloc) {
    }
   
    inline Object::Object(Mutator& context)
    : _gc_color(context.palette.alloc) {
        printf("context.palette.alloc = %zd\n", context.palette.alloc);
    }

    inline Object::~Object() {
    }
    
    inline std::size_t Object::gc_hash() const {
        return std::hash<const void*>()(this);
    }

    template<typename Adaptee> template<typename... Args>
    Leaf<Adaptee>::Leaf(Args&&... args) :
    Adaptee(std::forward<Args>(args)...) {
    }
    
    template<typename Adaptee>
    Leaf<Adaptee>::~Leaf() {
    }
        
    template<typename Adaptee>
    void Leaf<Adaptee>::_gc_shade(Mutator& context) const {
        context._white_to_black(this->_gc_color);
    }

    template<typename Adaptee>
    void Leaf<Adaptee>::_gc_trace(Collector& context) const {
        context._white_to_black(this->_gc_color);
    }
    
    
    

    
    
    template<typename T>
    Pointer<T>::Pointer(const Pointer& other)
    : Pointer(other.get()) {
    }
    
    template<typename T>
    Pointer<T>& Pointer<T>::operator=(const Pointer& other) {
        return operator=(other.get());
    }
    
    template<typename T>
    Pointer<T>::Pointer(T* other)
    : _ptr(other) {
    }
    
    template<typename T>
    Pointer<T>::Pointer(std::nullptr_t)
    : _ptr(nullptr) {
    }
    
    template<typename T>
    Pointer<T>& Pointer<T>::operator=(T* other) {
        Mutator::get().write(_ptr, other);
        return *this;
    }
    
    template<typename T>
    Pointer<T>& Pointer<T>::operator=(std::nullptr_t) {
        Mutator::get().write(_ptr, nullptr);
        return *this;
    }
    
    template<typename T>
    T* Pointer<T>::operator->() const {
        return _ptr.load(std::memory_order_relaxed);
    }
    
    template<typename T>
    bool Pointer<T>::operator!() const {
        return !get();
    }
    
    template<typename T>
    Pointer<T>::operator bool() const {
        return get();
    }
    
    template<typename T>
    Pointer<T>::operator T*() const {
        return get();
    }
    
    template<typename T>
    T& Pointer<T>::operator*() const {
        return get();
    }
    
    template<typename T>
    bool Pointer<T>::operator==(const Pointer<T>& other) {
        return get() == other.get();
    }
    
    template<typename T>
    auto Pointer<T>::operator<=>(const Pointer<T>& other) {
        return get() <=> other.get();
    }
    
    template<typename T>
    T* Pointer<T>::get() const {
        return _ptr.load(std::memory_order_relaxed);
    }
    
    
    inline void shade(const Object* object) {
        if (object) {
            Mutator::get().shade(object);
        }
    }

    
    
} // namespace gc

#endif /* gc_hpp */
