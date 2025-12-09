//
//  persistent_stack.hpp
//  client
//
//  Created by Antony Searle on 18/10/2025.
//

#ifndef persistent_stack_hpp
#define persistent_stack_hpp

#include "assert.hpp"
#include "garbage_collected.hpp"
#include "utility.hpp"

namespace wry {
    
    // Persistent stack implemented with a classic functional cons list

    template<typename T>
    struct PersistentStack {
        
        struct Node : GarbageCollected {
            
            Node const* _Nullable _next;
            T _payload;
            
            explicit
            Node(Node const* _Nullable next, auto&&... args)
            : _next{next}
            , _payload{FORWARD(args)...} {
            }
            
            virtual void _garbage_collected_scan() const override {
                using wry::garbage_collected_scan;
                garbage_collected_scan(_next);
                garbage_collected_scan(_payload);
            }
            
        };
                
        Node const* _Nullable _head = nullptr;

        [[nodiscard]] auto
        tail() const -> PersistentStack {
            assert(_head);
            return PersistentStack{_head->_next};
        }

        [[nodiscard]] auto
        pop() const -> std::pair<T, PersistentStack> {
            assert(_head);
            return { _head->_payload, PersistentStack{_head->_next} };
        }
        
        [[nodiscard]] auto
        pop_else(T alternative) const -> std::pair<T, PersistentStack> {
            return _head ? pop() : std::pair<T, PersistentStack>{ alternative, PersistentStack{} };
        }
        
        auto
        pop() -> T {
            assert(_head);
            mutator_overwrote(_head);
            return std::exchange(_head, _head->_next)->_payload;
        }
        
        auto pop_else(T alternative) -> T {
            return _head ? pop() : alternative;
        }

        
        [[nodiscard]] auto
        push(auto&& desired) const -> PersistentStack {
            return PersistentStack{new Node(_head, FWD(desired))};
        }
        
        auto
        push(auto&& desired) -> void {
            mutator_overwrote(_head);
            _head = new Node(_head, FORWARD(desired));
        }
                        
        [[nodiscard]] auto
        peek() const -> T {
            assert(_head);
            return _head->_payload;
        }
        
        [[nodiscard]] auto
        peek_else(T alternative) const {
            return _head ? peek() : alternative;
        }
        
        [[nodiscard]] auto
        is_empty() const -> bool {
            return !_head;
        }
        
        [[nodiscard]] auto
        emplace(auto&&... args) const -> PersistentStack {
            return PersistentStack{new Node(_head, FWD(args)...)};
        }
        
        [[nodiscard]] static auto
        singleton(auto&&... args) -> PersistentStack {
            return PersistentStack{ new Node(nullptr, FWD(args)...) };
        }
        
        [[nodiscard]] auto
        drop(size_t n) const -> PersistentStack {
            auto a = _head;
            for (;;) {
                if (!a || !n)
                    return PersistentStack{a};
                --n;
                a = a->_next;                
            }
        }
        
        // some gross debugging methods
        
        size_t size() const {
            size_t n = 0;
            Node const* p = _head;
            while (p) {
                ++n;
                p = p->_next;
            }
            return n;
        }
        
        T operator[](ptrdiff_t i) const {
            Node const* p = _head;
            while (i) {
                assert(p);
                p = p->_next;
                --i;
            }
            assert(p);
            return p->_payload;
        }
                
    };
    
    template<typename T>
    void garbage_collected_scan(PersistentStack<T> const& x) {
        garbage_collected_scan(x._head);
    }
    
} // namespace wry


#endif /* persistent_stack_hpp */
