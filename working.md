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














  // derivation:
    //
    // recall that to integrate over angle
    // ```
    //     \int f dw
    // ```
    // in polar coordinates
    // ```
    //     \int\int f sin(\theta) d\theta d\phi
    // ```
    // is equivalent to
    // ```
    //     \int\int f d\cos(\theta) d\phi
    // ```
    //
    // For the Trowbridge-Reitz distribution, we have
    //
    // p(\theta) = \alpha^2 / [ \pi ( cos^2(\theta)^2 (\alpha^2 - 1) + 1)^2 ]
    //
    // which is a rather nasty integral, but fortunately we always want to
    // integrate it as multiplied by
    // ```
    //     cos(\theta)    \theta < M_PI
    //     0              otherwise
    // ```
    // and the combined function,
    // ```
    //    g = \alpha^2 cos(\theta) / [ \pi ( cos^2(\theta) (\alpha^2 - 1) + 1)^2 ]
    // ```
    // has a much simpler integral
    // ```
    //     \int \alpha^2 cos(\theta) / [ \pi ( cos^2(\theta) (\alpha^2 - 1) + 1)^2 ] d cos(\theta)
    //
    //        = \alpha^2 / [ 2\pi (1-\alpha^2) ((\alpha^2 - 1) * cos^2(\theta) + 1)
    // ```
    // When \theta = 0, cos\theta = 1,
    // ```
    //        = \alpha^2 / [2\pi (1-\alpha^2) ((\alpha^2 - 1) + 1)
    //        = 1 / [2\pi (1 - \alpha^2)]
    // ```
    // And when \theta = \pi/2, cos\theta = 0
    // ```
    //        = \alpha^2 / [2\pi (1 - \alpha^2)]
    // ```
    // thus the total integral is
    // ```
    //        = (1 - \alpha^2) / (2\pi (1 - \alpha^2))
    //        = 1 / 2\pi
    // ```
    // Note that this indicates that the T-R form used is actually normalized
    // over the cos\theta weighted hemisphere, not the sphere.  Is that OK?
    //
    // Pulling out the 2\pi term that normalizes the integral over \phi, we
    // have obtained the cumulative density function
    //
    // and the CDF is
    // ```
    //     \Chi = \alpha^2 / [(1-\alpha^2)((\alpha^2-1)*cos^2(\theta)+1) - \alpha^2/(1-\alpha^2)
    // ```
    // Solving for cos\theta,
    // ```
    //     X*(1-a^2) = a^2/[(a^2-1)c^2+1] - a^2
    //     X*(1-a^2)+a^2 = a^2/[(a^2-1)c^2+1]
    //     (a^2-1)c^2+1 = a^2/[X*(1-a^2)+a^2]
    //     (a^2-1)c^2 = [a^2-X*(1-a^2)-a^2]/[X*(1-a^2)+a^2]
    //     (a^2-1)c^2 = [-X*(1-a^2)/[X*(1-a^2)+a^2]
    //     c^2 = X/[X*(1-a^2)+a^2]
    //     c^2 = 1 / [(1-a^2) + a^2 / X ]
    // ```
    // The final form's only dependence on X is in the a^2 / X term showing how
    // the scale factor if the problem changes with alpha (?)
    //
    // If we change variables X = 1-Y
    // ```
    //     c^2 = (1-Y)/[(1-Y)(1-a^2)+a^2]
    //         = (1-Y)/[Y(a^2-1) + 1]
    // ```
    // we recover the UE4 expression, indicating they chose the other direction
    // of CDF convention







For normal recovery from image:

For a given camera pixel, we know V, the camera direction.

For a given screen pixel, L is a function of the surface distance d along the
camera ray

For a circle of screen pixels centered on the camera, VdotL is constant

The BRDF model we use:

```
k / (k - k * NdotH^2)
*
NdotV / (k * NdotV + k)
*
NdotL / (k * NdotL + k)
*
k + k (1 - HdotV)^5
*
1 / NdotV  xxxx
*
1 / NdotL  xxxx
* 
NdotL 
```

We can actually look at the logarithm and linearize lots of these specular 
terms:
```
-log((k - k * NdotH^2)
-log(k * NdotV)
+NdotL
-log(k * NdotL + k)
+
...
```

The fresnel term requires (1 - HdotV) approxeq 1 to be significant, which requires 
H to be nearly perpendicular to V and thus L to be nearly opposite to V.

The camera and screen geometry constrain L and V to be quite similar, within 30
degrees, and H is between them.

When the light source is close to the camera, L, V and H are the same.  We
could look to a different parameterization,
```
V = (0, 0, 1)
H = (x, 0, 1 - x^2)
L = (2x, 0, 1 - 4x^2)

NdotV = Nz
NdotH = Nx*x + Nz*(1-x^2)
NodtL = Nx*2*x + Nz*(1-4x^2)
```
Where the scale of x is determined by the distance d

If the camera pixel is dominated by specular lighting, we can directly
find N = H = V + L as the peak.

If the camera pixel is dominated by diffuse lighting, we can directly find
N = L as the peak.

Whatever the situation, we will be brightest when N lies in the LV plane,
letting us constrain one of the degrees of freedom of N immediately.



### Subdivision

Suppose that edge a-b is the least-squared error representation of a quadratic
curve,

```
f(x) = x^2

e = \int_{-1}^{1} (f(x) - y)^2 dx
  = \int (x^2 - y)^2 dx
  = \int x^4 - 2x^2y + y^2 dx
  = [ 1/5 x^5 - 2/3 x^3 y + x y^2 ] _{-1}^{+1}
e = 2/5 - 4/3 y + 2 y^2
```
With minimum error at
```
0 = -4/3 + 4 y
0 = -1/3 + y
y = 1/3
```
and intercept
```
x = f^-1(y) = sqrt(1/3) = 0.577
```
Note that the endpoints of the line do not lie on the surface, it crosses
the true curve twice.

So, given the points (-1, 0), (+1, 0), (+3, 1) what curve is represented?



Suppose we subdivide the line, then we get
x,y = (+/- 1/2, +1/12)

So, for a shape -1, +1, +3 -> 0, 0, 


