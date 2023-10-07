## Sum types

C style:
```
struct ab {
    enum {
        A,
        B,
    } discriminant;
    union {
        T a;
        U b;
    }; 
};

switch (x.discriminant) {
    case A:
        foo(x.a);
        break;
    case B:
        bar(x.a);
        break;
    default:
        abort();
};

```
C++ style:
```
enum { A, B, };
using ab = std::variant<T, U>;
std::visit(overloaded{
    [](T a) { foo(a); },
    [](U b) { bar(b); },
}, x);
```
Rust style:
```
enum Ab {
    A(T),
    B(U),
}
match x {
    A(a) => foo(a),
    B(b) => bar(b),
}
```

Rustic style:
```
struct A { T a; };
struct B { U b; };
using Ab = std::variant<A, B>;
std::visit(overloaded{
    [](A y) { foo(y.a); },
    [](B y) { bar(y.b); },
}, x);
```

Notably the C++ variant's visitor can't distinguish between occurances of the
same type

C++ can't customize the discriminant's number and Rust can't even access it

Rust can't be valueless, C++ only by exception, C anything goes
```
struct Ab;
using A = Alternative<Ab, 0, T>;
using B = Alternative<Ab, 1, U>;


template<typename E, std::size_t K, typename... Args>  
struct Alternative { 
    std::tuple<Args...> _;
};


template<typename... Ts>
struct Variant {
    /* discriminant_type */ discriminant;
    unsigned char alignas(Ts...) storage[max(sizeof(Ts)...)];
    
    void _visit(F&& foo) {
        (((_discriminant == index_v<Ts>) && (foo((Ts&) storage), true)) || ...);
    } 
    
};

 
```



Possible formats

JSON
- number
- string
- array [ json, ... ]
- object { string : json, ... }
  - benefits over [[string, json], ... ] ?

binary
- bytes 

base64
- chars

csv


many things are arbitrarily long, so we must be able to stream them


variant options
- primitive scalars
- strings
- bytes
- dynamically sized hetrogenous sequence
- statically sized hetrogenous sequence
-  


