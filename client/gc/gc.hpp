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
#include <concepts>
#include <deque>

#include "bag.hpp"

namespace gc {
    
    // Garbage collector interface
    
    // TODO: There should be some way to make the list of active mutators
    // lock-free and efficiently-waitable-upon but it's defeated me so far.
    // When marking a node for destruction, the mutator races to wake up the
    // collector, with an already awake collector destroying the waited-upon
    // atomic.  Likewise, a leaving mutator races to republish its log while the
    // collector reads it.
    //
    // A locking solution means the mutator has to wait on a per-thread lock
    // contested only by the collector, and held by both parties only for O(1)
    // time, per handshake, which is pretty benign.
    
    
    
    // Usage note: Though there is some difference between American and British
    // English, we will use *collectible* as a noun and *collectable* as an
    // adjective
   
    // Per-thread states
   
    struct Mutator;
    struct Collector;
    
    // Basic objects

    struct Participant;
    struct Colored;
    
    using Color = std::intptr_t;
    
    // Adaptors implementing behaviors
    
    template<typename Adaptee> struct Leaf;
    template<typename T, typename Adaptee> struct Array;
    
    // Smart pointer implementing write barrier
    
    template<typename T> struct Pointer;
        
    
    // Interface with no-op implementation, useful for static lifetime things
    // such as literal strings
    
    struct Participant {
        explicit Participant(Mutator&);
        virtual ~Participant();
        virtual std::size_t gc_bytes() const;
        virtual void gc_enumerate(Collector&) const;
        virtual void _gc_shade(Mutator&) const;
        virtual void _gc_trace(Collector&) const;
    };
    
    // Object traced via an explicit color
        
    struct Colored : Participant {
        mutable std::atomic<std::intptr_t> _gc_color;
        explicit Colored(Mutator&);
        virtual ~Colored() override;
        virtual void _gc_shade(Mutator&) const override;
        virtual void _gc_trace(Collector&) const override;
        
    };
    
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
    
    
    
    namespace detail {
        
        struct BlackImposter {
        
            Color _white;
            
            operator Color() const {
                return _white ^ 1;
            }
            
            BlackImposter& operator=(Color value) {
                _white = value ^ 1;
                return *this;
            }
            
        }; // struct BlackImposter
        
        template<typename T>
        struct TaggedPointer {
            
            enum : std::intptr_t {
                TAG_MASK = 7,
                PTR_MASK = -8,
            };
            
            struct TagImposter {
                std::intptr_t _value;
                operator std::intptr_t() const {
                    return _value & TAG_MASK;
                }
                TagImposter& operator=(std::intptr_t t) {
                    assert(!(t & PTR_MASK));
                    _value = (_value & PTR_MASK) | t;
                    return *this;
                }
            };
            
            struct PtrImposter {
                std::intptr_t _value;
                operator T*() const {
                    return reinterpret_cast<T*>(_value & PTR_MASK);
                }
                PtrImposter& operator=(T* p) {
                    std::intptr_t q = reinterpret_cast<std::intptr_t>(p);
                    assert(!(q & TAG_MASK));
                    _value = (_value & TAG_MASK) | q;
                }
            };
                        
            union {
                std::intptr_t _value;
                TagImposter tag;
                PtrImposter ptr;
            };
            
            TaggedPointer() = default;
            
            explicit TaggedPointer(T* p) {
                std::intptr_t q = reinterpret_cast<std::intptr_t>(p);
                assert(!(q & TAG_MASK));
                _value = q;
            }
            
            explicit TaggedPointer(std::intptr_t pt)
            : _value(pt) {
            }
            
            TaggedPointer(const TaggedPointer&) = default;
            
            TaggedPointer(T* p, std::intptr_t t) {
                std::intptr_t q = reinterpret_cast<std::intptr_t>(p);
                assert(!(q & TAG_MASK) && !(t & PTR_MASK));
                _value = q | t;
            }
            
            T* operator->() const {
                return ptr.operator T*();
            }
            
        };
        
    } // namespace detail
    
    
    struct Palette {
        union {
            Color white;
            detail::BlackImposter black;
        };
        Color alloc;
        enum : Color {
            gray = 2,
            red = 3,
        };
    };
    
    struct Log {
        
        bool dirty;
        Bag<const Colored*> allocations;
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
        
   

    
    // garbage collector state for one mutator thread
    
    struct Mutator {

        // Thread-local state access point

        static thread_local Mutator* _thread_local_context_pointer;

        static Mutator& get();
        [[nodiscard]] static Mutator* _exchange(Mutator*);

        // Thread local state

        Palette palette; // colors received from Collector
        Log     log    ; // activity to publish to Collector

        void _white_to_gray(std::atomic<std::intptr_t>& color);
        bool _white_to_black(std::atomic<std::intptr_t>& color) const;
        void shade(const Participant*);
        template<typename T> T* write(std::atomic<T*>& target, std::type_identity_t<T>* desired);
        template<typename T> T* write(std::atomic<T*>& target, std::nullptr_t);

        
        
        void* _allocate(std::size_t bytes);
        
        // Mutator endures for the whole thread lifetime
        // Channel is per-resumption.
        
        struct Channel {
            enum : std::intptr_t {
                MUTATOR_DID_ENTER,
                MUTATOR_DID_PUBLISH,
                MUTATOR_DID_LEAVE,
                COLLECTOR_DID_QUERY,
                COLLECTOR_DID_WAIT,
            };
            std::atomic<detail::TaggedPointer<Channel>> next;
            Palette palette; // Collector to Mutator
            Log log;         // Mutator to Collector
        };
        
        Channel* _channel = nullptr;
        
        void enter();
        void handshake();
        void leave();
        
    };

    
    // garbage collector state for the unique collector thread, which is
    // also a mutator
    
    struct Collector : Mutator {
        
        // mutator activity
        std::atomic<detail::TaggedPointer<Channel>> channel_list_head;
        std::atomic<Palette> _atomic_palette;
        
        
        // scanning activity
        std::deque<const Colored*> _scan_stack;
        
        void _process_scan_stack() {
            while (!this->_scan_stack.empty()) {
                const Colored* collectible = this->_scan_stack.back();
                this->_scan_stack.pop_back();
                assert(collectible);
                collectible->gc_enumerate(*this);
            }
        }
        
        void visit(const Participant* particpiant);
        template<typename T> void visit(const std::atomic<T*>& participant);
        template<typename T> void visit(const Pointer<T>& participant);

        void collect();
        
    };
    
    inline Collector* global_collector = nullptr;
    
    
    
    inline void Mutator::shade(const Participant* participant) {
        if (participant) {
            participant->_gc_shade(*this);
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
    
    template<typename T>
    struct Pointer {
        
        std::atomic<T*> _ptr;
        
        explicit Pointer(T* other)
        : _ptr(other) {
        }

        T* load() const {
            return _ptr.load(std::memory_order_relaxed);
        }
        
        T* _load_acquire() const {
            return _ptr.load(std::memory_order_acquire);
        }
                
        Pointer() = default;
        
        explicit Pointer(std::nullptr_t)
        : _ptr(nullptr) {
        }
        
        Pointer(const Pointer& other)
        : Pointer(other.get()) {
        }
        
        Pointer& operator=(std::nullptr_t) {
            Mutator::get().write(*this, nullptr);
            return *this;
        }
                    
        Pointer& operator=(T* other) {
            Mutator::get().write(_ptr, other);
            return *this;
        }

        Pointer& operator=(const Pointer& other) {
            return *this = other.get();
        }
        
        T* operator->() const {
            return load();
        }

        explicit operator bool() const {
            return static_cast<bool>(load());
        }
        
        bool operator!() const {
            return !static_cast<bool>(*this);
        }

        operator T*() const {
            return load();
        }

        T& operator&() const { 
            return *load();
        }
        
    };


    inline void* Mutator::_allocate(std::size_t bytes) {
        void* __nonnull pointer = malloc(bytes);
        assert(pointer);
        this->log.total += bytes;
        return pointer;
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
    

    void Collector::visit(const Participant* participant) {
        if (participant)
            participant->_gc_trace(*this);
    }
    
    template<typename T>
    void Collector::visit(const std::atomic<T*>& participant) {
        this->visit(participant.load(std::memory_order_acquire));
    }
    
    template<typename T>
    void Collector::visit(const Pointer<T>& p) {
        this->visit(p._load_acquire());
    }

    inline Participant::Participant(Mutator&) {
    }
    
    inline Participant::~Participant() {
    }
    
    inline std::size_t Participant::gc_bytes() const {
        return sizeof(Participant);
    }

    inline void Participant::gc_enumerate(Collector&) const {
    }

    inline void Participant::_gc_shade(Mutator&) const {
    }
    
    inline void Participant::_gc_trace(Collector &) const {
    }

    inline Colored::Colored(Mutator& context)
    : Participant(context), _gc_color(context.palette.alloc) {
    }

    inline Colored::~Colored() {
    }
        
    
    inline void Colored::_gc_shade(Mutator& context) const {
        context._white_to_gray(this->_gc_color);
    }
    
    inline void Colored::_gc_trace(Collector& context) const {
        if (context._white_to_black(this->_gc_color)) {
            context._scan_stack.push_back(this);
        }
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
        return context._white_to_black(this->_gc_color);
    }

    template<typename Adaptee>
    void Leaf<Adaptee>::_gc_trace(Collector& context) const {
        context._white_to_black(this->_gc_color);
    }

    
} // namespace gc

#endif /* gc_hpp */
