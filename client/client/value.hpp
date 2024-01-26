//
//  value.hpp
//  client
//
//  Created by Antony Searle on 19/1/2024.
//

#ifndef value_hpp
#define value_hpp

#include <cinttypes>

#include "stdint.hpp"
#include "utility.hpp"
#include "string.hpp"
#include "table.hpp"
#include "test.hpp"
#include "debug.hpp"

/*
 
namespace wry::value {
    
    // Values live 
    //     in cells on the grid
    //     in the stacks of machines
    //     in the bins of non-machines
    
  
    // Superset of JSON
    
    
    // Small objects, such as bool, number, empty array, one-element array,
    // empty object, can all be stored with a single 64-bit word and no
    // heap allocations
    
    // Noting that JSON does not support NAN or INF, it is tempting to store
    // numbers as float64 and use the bit patterns
    //
    // x1111111 1111xxxx xxxxxxxx xxxxxxxx xxxxxxxx xxxxxxxx xxxxxxxx xxxxxxxx
    //
    // to store the pointers for other kinds.  Many platforms provide user
    // pointers of the form
    //
    // 00000000 00000000 0xxxxxxx xxxxxxxx xxxxxxxx xxxxxxxx xxxxxxxx xxxx0000
    //
    // addressing 128 TB of data with 16 byte alignment.  Even for wider buses,
    // we may be able to request all our allocations within this subspace.
    //
    // With 2^53 states available outside the numbers, and only 2^43 distinct
    // pointer values, we have 10 bits to further tag pointers, or to introduce
    // additional types (such as small strings, 32 bit integers, etc.)
    
    // Less dramatically, we can use a dynamic-allocation-avoiding type erasing
    // container like std::any to keep small objects inline
    //
    // Value holds a type-erased class derived from Base.  All the
    // derived types must exactly 16 bytes in size and alignment.  The intent
    // is that the first 8 bytes store the vtbl pointer.
    
    // 8 bytes: packed pointer
    // 16 bytes: vtbl, word pair; can be fat pointer dyn trait-like
    // 32 bytes: vtbl, word triple, supports Vec-like arrays and strings
    
    // ?
    //
    // std::variant<...>
    
    // Disderata:
    // Open, like std::any
    // Yields more interfaces
    //
    
    // Value is a type-erasing container
    //
    // Compared to std::any
    // - Common base class provides basic copy-assign semantics
    // - Stored types must <= 16 bytes, i.e. (vtbl, userdata)
    //   - Sometimes a pimpl but often a compact reprsentation such as an
    //     unboxed int
    //
    // Compared to dyn Trait fat pointer
    
    using i64 = std::int64_t;
    using u64 = std::uint64_t;
    using f64 = double;
    
#define WRY_FOR_TYPES\
    X(bool)\
    X(i64)\
    X(u64)\
    X(f64)
            
    struct Base;

    struct Array;
    struct Null;
    struct String;
    template<typename> struct Scalar;

    struct Value;
    
    struct Visitor;
    
    // double dispatcher
    struct Visitor {
        virtual void operator()(bool);
        virtual void operator()(i64);
        virtual void operator()(u64);
        virtual void operator()(f64);
    };

    struct Base {
        
        virtual ~Base() = 0;
        
        virtual void uninitialized_copy_into(Value* destination) const = 0;
        virtual void copy_from(const Value&);
        virtual void move_from(Value&&);
        
        virtual void debug() const;
        virtual void visit(Visitor&) const;

        // provide all operators
                
        virtual Value postincrement();

        virtual Value function_call(const Value&);
        
        virtual bool operator_bool() const = 0;
        virtual int operator_int() const = 0;
        virtual double operator_double() const = 0;

        virtual Value subscript(const Value&) const;
        virtual Value& subscript(const Value&);
        
        virtual Value& increment();
        
        virtual Value unary_plus() const;
        
        virtual Value binary_plus(const Value&) const;
        
        virtual Value& assigned_plus(const Value&);

        
        

    };
    
    static_assert(sizeof(Base) <= 16);
    
    struct Null final : Base {
        
        virtual ~Null() override;
        
        virtual void uninitialized_copy_into(Value* destination) const override;
        
        virtual bool operator_bool() const override;
        virtual int operator_int() const override;
        virtual double operator_double() const override;
        
    };
    
    static_assert(sizeof(Null) <= 16);

    template<typename T>
    struct Scalar : Base {
        
        static_assert(sizeof(T) <= 8);
        static_assert(std::is_scalar_v<T>);
        
        T data;
        
        explicit Scalar(T x) : data(x) {}
        
        virtual void uninitialized_copy_into(Value* destination) const override {
            std::construct_at((Scalar*) destination, data);
        }
        
        virtual bool operator_bool() const override {
            return data;
        }
        
        virtual int operator_int() const override {
            return (int) data;
        }
        
        virtual double operator_double() const override {
            return (double) data;
        }
        
    };
    
    static_assert(sizeof(Scalar<double>) == 16);
        
    struct Array : Base {
        
        wry::Array<Value>* pointer;
        
        explicit Array(wry::Array<Value>* p)
        : pointer(p) {
        }
        
        explicit Array(wry::Array<Value>&& x)
        : Array(new wry::Array<Value>(std::move(x))) {
        }
        
        virtual ~Array() {
            delete pointer;
        }
        
        virtual void uninitialized_copy_into(Value* destination) const override {
            std::construct_at((Array*) destination, new wry::Array<Value>(*pointer));
        }
        
        virtual bool operator_bool() const override {
            return !pointer->empty();
        }
        
        virtual int operator_int() const override {
            return (int) pointer->size();
        }
        
        virtual double operator_double() const override {
            return (double) pointer->size();
        }
        
        
    };
    
    static_assert(sizeof(Array) == 16);

    static_assert(sizeof(Base) <= 16);

    struct Value final {
        
        alignas(16) unsigned char _buffer[16];
        
        void debug() const;
        
        Value();
        Value(Value&&);
        Value(const Value&);
        ~Value();
        Value& operator=(Value&&);
        Value& operator=(const Value&);

        explicit Value(bool);
        explicit Value(int);
        explicit Value(std::int64_t);
        explicit Value(std::uint64_t);
        explicit Value(double);
        Value(auto first, auto last);
        
        Value& operator=(bool);
        Value& operator=(int);
        Value& operator=(std::int64_t);
        Value& operator=(std::uint64_t);
        Value& operator=(double);

        explicit operator bool() const;
        explicit operator int() const;
        explicit operator double() const;
        
        template<typename T> T* as();
        template<typename T> const T* as() const;

    };
    
    static_assert(sizeof(Value) == 16);
            
    inline Base::~Base() = default;
    
    void Base::copy_from(const Value& source) {
        std::destroy_at(this);
        ((const Base&) source).uninitialized_copy_into((Value*) this);
    }
    
    void Base::move_from(Value&& source) {
        std::destroy_at(this);
        std::memcpy((void*) this, (const void*) &source, 16);
        std::construct_at((Null*) &source);
    }
    
    void Base::debug() const {
        auto v = (const std::uint64_t*) this;
        printf("[ \"%s\", \"%0.16" PRIX64 "\" ]", typeid(*this).name(), v[1]);
    }
    
    Value Base::postincrement() { assert(false); }
    Value Base::function_call(const Value&) { assert(false); }
    Value& Base::subscript(const Value&) { assert(false); }
    Value& Base::increment() { assert(false); }
    Value Base::unary_plus() const { assert(false); }
    Value Base::binary_plus(const Value&) const { assert(false); }
    Value& Base::assigned_plus(const Value&) { assert(false); }


    inline Value::Value() {
        std::construct_at((Null*) this);
    }
    
    inline Value::Value(Value&& other) {
        std::memcpy(this, &other, 16);
        std::construct_at((Null*) &other);
    }
    
    inline Value::Value(const Value& other) {
        ((Base*) &other)->uninitialized_copy_into(this);
    }
    
    inline Value::~Value() {
        std::destroy_at((Base*) _buffer);
    }
    
    inline Value& Value::operator=(Value&& other) {
        ((Base*) _buffer)->move_from(std::move(other));
        return *this;
    }

    inline Value& Value::operator=(const Value& other) {
        ((Base*) _buffer)->copy_from(other);
        return *this;
    }
    
    inline Value& Value::operator=(bool x) {
        std::destroy_at((Base*) this);
        std::construct_at((Scalar<bool>*) this, x);
        return *this;
    }

    inline Value& Value::operator=(int x) {
        std::destroy_at((Base*) this);
        std::construct_at((Scalar<std::int64_t>*) this, x);
        return *this;
    }

    inline Value& Value::operator=(std::int64_t x) {
        std::destroy_at((Base*) this);
        std::construct_at((Scalar<std::int64_t>*) this, x);
        return *this;
    }

    inline Value& Value::operator=(std::uint64_t x) {
        std::destroy_at((Base*) this);
        std::construct_at((Scalar<std::uint64_t>*) this, x);
        return *this;
    }
    
    inline Value& Value::operator=(double x) {
        std::destroy_at((Base*) this);
        std::construct_at((Scalar<double>*) this, x);
        return *this;
    }

    inline void Value::debug() const {
        ((Base*) _buffer)->debug();
    }
    
    template<typename T>
    T* Value::as() {
        return dynamic_cast<T*>((Base*) this);
    }

    template<typename T>
    const T* Value::as() const {
        return dynamic_cast<const T*>((const Base*) this);
    }

    
    inline Value::operator bool() const {
        return ((Base*) _buffer)->operator_bool();
    }

    inline Value::operator int() const {
        return ((Base*) _buffer)->operator_int();
    }

    inline Value::operator double() const {
        return ((Base*) _buffer)->operator_double();
    }

    
    inline Null::~Null() = default;
    
    inline void Null::uninitialized_copy_into(Value* destination) const {
        std::construct_at((Null*) destination);
    }
    
    inline bool Null::operator_bool() const {
        return false;
    }

    inline int Null::operator_int() const {
        return 0;
    }
    
    inline double Null::operator_double() const {
        return 0.0; // Could be NAN, but probably too troublesome
    }

    




    Value::Value(bool x) {
        std::construct_at((Scalar<bool>*) _buffer, x);
    }

    Value::Value(int x)
    : Value::Value((std::int64_t) x) {
    }

    Value::Value(double x) {
        std::construct_at((Scalar<double>*) _buffer, x);
    }

    Value::Value(std::uint64_t x) {
        std::construct_at((Scalar<std::uint64_t>*) _buffer, x);
    }

    Value::Value(std::int64_t x) {
        std::construct_at((Scalar<std::int64_t>*) _buffer, x);
    }


    
    Value::Value(auto first, auto last) {
        std::construct_at((Array*) _buffer, new wry::Array<Value>(first, last));
    }
    
    
    define_test("Value") {

        Value a;
        Value b(false);
        Value c(true);
        Value d(0.0);
        Value e(1.0);
        Value f(0);
        Value g(1);
        Value h(-1);
        
        int z[] = {1,2,3,4};
        Value i(std::begin(z), std::end(z));
        
        a.debug(); printf("\n");
        b.debug(); printf("\n");
        c.debug(); printf("\n");
        d.debug(); printf("\n");
        e.debug(); printf("\n");
        f.debug(); printf("\n");
        g.debug(); printf("\n");
        h.debug(); printf("\n");
        i.debug(); printf("\n");

        DUMP((bool) a);
        DUMP((bool) b);
        DUMP((bool) c);
        DUMP((bool) d);
        DUMP((bool) e);
        DUMP((bool) f);
        DUMP((bool) g);
        DUMP((bool) h);
        DUMP((bool) i);


        DUMP((int) a);
        DUMP((int) b);
        DUMP((int) c);
        DUMP((int) d);
        DUMP((int) e);
        DUMP((int) f);
        DUMP((int) g);
        DUMP((int) h);
        DUMP((int) i);

        DUMP((double) a);
        DUMP((double) b);
        DUMP((double) c);
        DUMP((double) d);
        DUMP((double) e);
        DUMP((double) f);
        DUMP((double) g);
        DUMP((double) h);
        DUMP((double) i);



        a = c;
        b = e;
        d = i;
        DUMP((bool) a);
        DUMP((bool) b);
        DUMP((int) d);
        DUMP((int) i);

        
        
        
    };
    
    */
    
    /*
        
    struct Base;
    struct Value;
    
    struct alignas(16) Base {
        Value* as_value();
        const Value* as_value() const;
        virtual void destruct() const;
        virtual void uninitialized_copy_into(Value& destination) const;
        virtual void copy_into(Value& destination) const;
        virtual void move_from(Value&& source);
        virtual bool operator_bool() const = 0;
    };
    
    struct Value {
                
        void* _vtbl;
        void* _data;

        Base* _as_base();
        const Base* _as_base() const;
        void _destruct() const;
        void _uninitialized_copy_into(Value&) const;
        void _copy_into(Value&) const;
        void _move_from(Value&&);

        Value& swap(Value& other) {
            std::swap(_vtbl, other._vtbl);
            std::swap(_data, other._data);
            return other;
        }
        
        Value()
        : _vtbl(nullptr) {
        }
        
        Value(const Value& other) {
            other._uninitialized_copy_into(*this);
        }

        Value(Value&& other)
        : _vtbl(std::exchange(other._vtbl, nullptr))
        , _data(other._data) {
        }
        
        ~Value() { _destruct(); }
        
        Value& operator=(const Value& other) {
            other._copy_into(*this);
            return *this;
        }
        
        Value& operator=(Value&& other) {
            _move_from(std::move(other));
            return *this;
        }
        
        explicit Value(bool);
        
        explicit Value(int);
        explicit Value(std::int64_t);
        explicit Value(std::uint64_t);
        explicit Value(double);
        
        explicit Value(char);
        explicit Value(char32_t);
        explicit Value(const char*);
        explicit Value(StringView);
        explicit Value(String&&);

        explicit Value(ArrayView<Value>);
        explicit Value(Array<Value>&&);

        explicit Value(Table<String, Value>&&);
        
        operator bool() const;
                
    };
    
    inline void swap(Value& a, Value& b) {
        a.swap(b);
    }
    
    inline Value* Base::as_value() {
        return reinterpret_cast<Value*>(this);
    }
    
    inline const Value* Base::as_value() const {
        return reinterpret_cast<const Value*>(this);
    }
    
    inline void Base::destruct() const {
    }
    
    inline void Base::uninitialized_copy_into(Value &destination) const {
        std::memcpy(&destination, (void*) this, 16);
    }
        
    inline void Base::copy_into(Value& destination) const {
        destination._destruct();
        std::construct_at(&destination, *as_value());
    }

    inline void Base::move_from(Value&& source) {
        destruct();
        std::construct_at(as_value(), std::move(source));
    }

    Base* Value::_as_base() {
        assert(_vtbl);
        return reinterpret_cast<Base*>(this);
    }
    
    const Base* Value::_as_base() const {
        assert(_vtbl);
        return reinterpret_cast<const Base*>(this);
    }
        
    void Value::_destruct() const {
        if (_vtbl)
            _as_base()->destruct();
    }
        
    void Value::_uninitialized_copy_into(Value& destination) const {
        if (_vtbl)
            _as_base()->uninitialized_copy_into(destination);
        else
            destination._vtbl = nullptr;
    }
    
    void Value::_copy_into(Value& destination) const {
        if (_vtbl)
            _as_base()->copy_into(destination);
        else if (destination._vtbl) {
            destination._as_base()->destruct();
            destination._vtbl = nullptr;
        }
    }
    
    void Value::_move_from(Value&& source) {
        if (_vtbl) {
            _as_base()->move_from(std::move(source));
        } else {
            _vtbl = std::exchange(source._vtbl, nullptr);
            _data = source._data;
        }
    }
        
    inline Value::operator bool() const {
        return _vtbl && _as_base()->operator_bool();
    }
    

    
    struct Boolean : Base {
        
        bool x;
        
        constexpr explicit Boolean(bool y)
        : x(y) {
        }
        
        virtual bool operator_bool() const override {
            return x;
        }
        
    };
    
    Value::Value(bool y) {
        new (this) Boolean(y);
    }
        
    struct Number : Base {
        
        double x;
        
        constexpr explicit Number(double y) 
        : x(y) {
        }
        
        virtual bool operator_bool() const override {
            return x;
        }
        
    };
    
    Value::Value(double y) {
        new (this) Number(y);
    }
    
    struct String : Base {
        
    };
    
    struct HeavyString : String {
        wry::String* x;
        virtual bool operator_bool() const override {
            return x && !x->empty();
        }
    };
    
    struct ImmutableString : String {
        wry::immutable_string::implementation* x;
        virtual bool operator_bool() const override {
            return x && (x->_end != x->_begin);
        }
    };
    
    struct CString : String {
        const char* x;
        virtual bool operator_bool() const override {
            return x && *x;
        }
    };
    
    struct Array : Base {
        
    };
    
    struct HeavyArray : Array {
        wry::Array<Value>* x;
        virtual bool operator_bool() const override {
            return x && *x;
        }
    };

    */
    
    
/*
} // namespace wry::value
 
 */

namespace wry::value {
    
    struct Value;
    struct Base;
        
    using i64 = std::int64_t;
    using u64 = std::uint64_t;
    using f64 = std::uint64_t;
    
    // flags
    
    enum : u64 {
        
        IS_NOT_TRIVIAL = 1 << 1,

        // IS_PHYSICAL

        // IS_NUMERIC
        // IS_INTEGRAL
        // IS_SIGNED
        // IS_OPCODE
        
    };
        
    enum : u64 {
        
        EMPTY = 0,
        
        
    };
    
    struct Base {
        virtual ~Base() = default;
        virtual Base* clone() const = 0;
    };
    
    template<typename T>
    struct Wrap final : Base {
        
        T data;
                
        Wrap(std::in_place_t, auto&&... args)
        : data(std::forward<decltype(args)>(args)...) {
        }
        
        Wrap(const Wrap&) = default;
        
        virtual ~Wrap() override final = default;
        
        virtual Wrap* clone() const override final {
            return new Wrap(*this);
        }
        
    };
        
    using Array = Wrap<wry::Array<Value>>;
    using String = Wrap<wry::String>;
    using Object = Wrap<wry::Table<const char*, Value>>;
    
    // todo:
    //
    // presumably this object will be performance critical
    //
    // open questions in optimization
    // - polymorphism via if-else or switch or virtual
    //   - these correspond to the discriminant being flags, being enums, or
    //     being vtbl pointers, or some blending of the first two
    // - implicit copy vs move only vs explicit clone
    
    struct alignas(16) Value {
                
        u64 d = EMPTY;
        
        union {
            
            // trivial types
            u64 u;
            i64 i;
            f64 f;
            
            // pointer to a zero-terminated unbounded-lifetime string
            const char8_t* c;
                        
            // virtual interface to nontrivial types
            Base   *p;
            Array  *a;
            String *s;
            Object *o;
            
        };
        
        Value() = default;
                
        Value(Value&& x)
        : d(x.d)
        , u(x.u) {
            if (d & IS_NOT_TRIVIAL)
                x.d = EMPTY;
        }
        
        Value(const Value& x) {
            if (x.d & IS_NOT_TRIVIAL)
                p = x.p->clone();
            d = x.d;
        }
        
        ~Value() {
            if (d & IS_NOT_TRIVIAL) {
                delete p;
            }
        }
        
        Value& swap(Value& x) {
            u64 a = x.d;
            u64 b = x.u;
            x.d = d;
            x.u = u;
            d = a;
            u = b;
            return x;
        }
        
        Value& operator=(Value&& x) {
            return Value(std::move(x)).swap(*this);
        }
        
        Value& operator=(const Value& x) {
            return Value(x).swap(*this);
        }
        
        
    };
    
}



#endif /* value_hpp */
