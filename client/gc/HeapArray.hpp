//
//  wry/gc/HeapArray.hpp
//  client
//
//  Created by Antony Searle on 19/6/2024.
//

#ifndef wry_gc_HeapArray_hpp
#define wry_gc_HeapArray_hpp

#include "utility.hpp"
#include "value.hpp"
#include "IndirectFixedCapacityValueArray.hpp"

namespace wry::gc {
    
    struct HeapArray : Object {
        
        struct InnerArray {
            
            Traced<Value>* _begin;
            Traced<Value>* _end;
            Traced<Value>* _capacity;
            Traced<const HeapManaged<Traced<Value>>*> _manager;
            
            InnerArray();
            InnerArray(InnerArray&&);
            InnerArray& operator=(InnerArray&&);
            void swap(InnerArray& other);

            bool empty() const;
            bool full() const;
            size_t size() const;
            const Traced<Value>& front() const;
            const Traced<Value>& back() const;

            Traced<Value>& front();
            Traced<Value>& back();
            void push_back(Value);
            void push_front(Value);
            void pop_front();
            void pop_back();
            void clear();
            void reserve(size_t n);

        };
        
        enum State {
            INITIAL,
            NORMAL,
            RESIZING,
        };

        InnerArray _alpha;
        InnerArray _beta;
        State _state;
        
        HeapArray();
        virtual ~HeapArray() override;

        const Traced<Value>& operator[](size_t) const;
        Traced<Value>& operator[](size_t);

        Traced<Value>& front();
        const Traced<Value>& front() const;
        Traced<Value>& back();
        const Traced<Value>& back() const;

        bool empty() const;
        size_t size() const;

        void clear();
        Value insert_or_assign(Value key, Value value);
        void push_back(Value);
        void pop_back();
                
        Value find(Value key) const;
                
    }; // struct HeapArray
    
} // namespace wry::gc

#endif /* wry_gc_HeapArray_hpp */
