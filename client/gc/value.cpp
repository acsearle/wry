//
//  value.cpp
//  client
//
//  Created by Antony Searle on 31/5/2024.
//

#include "value.hpp"

namespace gc {
        
    bool HeapValue::logical_not() const {
        abort();
    }
    
    std::partial_ordering HeapValue::three_way_comparison(Value other) const {
        abort();
    }
    
    bool HeapValue::equality(Value) const {
        abort();
    }
    
    
    Value HeapValue::multiplication(Value) const {
        return Value::make_error();
    }

    Value HeapValue::division(Value) const {
        return Value::make_error();
    }

    Value HeapValue::remainder(Value) const {
        return Value::make_error();
    }
    
    Value HeapValue::addition(Value) const {
        return Value::make_error();
    }

    Value HeapValue::subtraction(Value) const {
        return Value::make_error();
    }

    Value HeapValue::bitwise_and(Value) const {
        return Value::make_error();
    }
    
    Value HeapValue::bitwise_or(Value) const {
        return Value::make_error();
    }
    
    Value HeapValue::bitwise_xor(Value) const {
        return Value::make_error();
    }
    
    Value HeapValue::function_call() const {
        return Value::make_error();
    }
    
    Value HeapValue::subscript_const(Value) const {
        return Value::make_error();
    }
    
    Value HeapValue::left_shift(Value) const {
        return Value::make_error();
    }
    
    Value HeapValue::right_shift(Value) const {
        return Value::make_error();
    }
    
    Value HeapValue::unary_plus() const {
        return Value::make_error();
    }
    
    Value HeapValue::unary_minus() const {
        return Value::make_error();
    }
    
    Value HeapValue::bitwise_not() const {
        return Value::make_error();
    }
    
    
    
    
    
    void HeapValue::prefix_increment(Value& self) const {
        self += Value::from_int64(1);
    }

    void HeapValue::prefix_decrement(Value& self) const {
        self -= Value::from_int64(1);
    }

    Value HeapValue::postfix_increment(Value& self) const {
        Value old;
        ++self;
        return old;
    }
    
    Value HeapValue::postfix_decrement(Value& self) const {
        Value old;
        --self;
        return old;
    }
    
    
    
    void HeapValue::assigned_addition(Value& self, Value other) const {
        self = self + other;
    }

    void HeapValue::assigned_subtraction(Value& self, Value other) const {
        self = self - other;
    }

    void HeapValue::assigned_multiplication(Value& self, Value other) const {
        self = self * other;
    }

    void HeapValue::assigned_division(Value& self, Value other) const {
        self = self / other;
    }
    
    void HeapValue::assigned_remainder(Value& self, Value other) const {
        self = self % other;
    }
    
    void HeapValue::assigned_bitwise_and(Value& self, Value other) const {
        self = self & other;
    }
    
    void HeapValue::assigned_bitwise_xor(Value& self, Value other) const {
        self = self ^ other;
    }
    
    void HeapValue::assigned_bitwise_or(Value& self, Value other) const {
        self = self | other;
    }
    
    void HeapValue::assigned_left_shift(Value& self, Value other) const {
        self = self << other;
    }

    void HeapValue::assigned_right_shift(Value& self, Value other) const {
        self = self >> other;
    }

    
    
    DeferredElementAccess HeapValue::subscript_mutable(Value& self, Value pos) {
        return {self, pos};
    }
    
    
    const HeapArray* HeapValue::as_HeapArray() const {
        return nullptr;
    }
    
    const HeapInt64* HeapValue::as_HeapInt64() const {
        return nullptr;
    }
    
    const HeapString* HeapValue::as_HeapString() const {
        return nullptr;
    }
    

    
    
    String HeapValue::str() const {
        return String{Value::from_ntbs("HeapValue")};
    };
    
} // namespace gc
