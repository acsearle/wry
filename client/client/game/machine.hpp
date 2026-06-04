//
//  machine.hpp
//  client
//
//  Created by Antony Searle on 20/9/2023.
//

#ifndef machine_hpp
#define machine_hpp

#include "contiguous_deque.hpp"
#include "sim.hpp"
#include "entity.hpp"
#include "debug.hpp"
#include "opcode.hpp"
#include "persistent_stack.hpp"
#include "save_types.hpp"
#include "vector.hpp"

namespace wry {

    struct Machine : Entity {

        static constexpr uint64_t SAVE_TYPE_TAG = save_type_tag_fnv1a("wry::Machine");

        virtual void _garbage_collected_debug() const override {
            printf("%s\n", __PRETTY_FUNCTION__);
        }

        virtual uint64_t _save_type_tag() const override { return SAVE_TYPE_TAG; }
        virtual void _save_body(Saver& saver) const override;

        enum {
            PHASE_TRAVELLING,
            PHASE_WAITING_FOR_OLD,
            PHASE_WAITING_FOR_NEW,
        } _phase = PHASE_WAITING_FOR_NEW;
        
        i64 _on_arrival = OPCODE_NOOP;
        
        PersistentStack<Term> const* _stack = nullptr;
        

        // The _old_* and _new_* states represent the beginning and end states
        // of travelling.  They are used by the visualization as a lerp
        
        i64 _old_heading = HEADING_NORTH;
        i64 _new_heading = HEADING_NORTH;
        Coordinate _old_location = { 0, 0 };
        Coordinate _new_location = { 0, 0 };
        Time _old_time = 0;
        Time _new_time = 0;
        
        Machine* make_mutable_clone() const {
            return new Machine(*this);
        }
        
        void push(Term x) {
            if (!term_is_null(x))
                _stack = PersistentStack<Term>::push(_stack, x);
        }

        Term pop() {
            if (PersistentStack<Term>::is_empty(_stack))
                return Term{};
            Term result = PersistentStack<Term>::peek(_stack);
            _stack = PersistentStack<Term>::tail(_stack);
            return result;
        }

        Term peek() const {
            return PersistentStack<Term>::is_empty(_stack)
                ? Term{}
                : PersistentStack<Term>::peek(_stack);
        }
        
        std::pair<Term, Term> pop2() {
            Term z = pop();
            Term y = pop();
            return {y, z};
        }
        
        std::pair<Term, Term> peek2() const {
            std::pair<Term, Term> result = {};
            if (_stack) {
                result.second = _stack->_payload;
                if (_stack->_next)
                    result.first = _stack->_next->_payload;
            }
            return result;
        }

        void pop2push1(Term x) {
            _stack = PersistentStack<Term>::tail(_stack);
            _stack = PersistentStack<Term>::tail(_stack);
            _stack = PersistentStack<Term>::push(_stack, x);
        }
        
        virtual void notify(TransactionContext* context) const override;

        void _schedule_arrival(World* world);
        
        virtual void _garbage_collected_scan() const override {
            garbage_collected_scan(_stack);
        }
                        
    };
    
} // namespace wry::sim

#endif /* machine_hpp */
