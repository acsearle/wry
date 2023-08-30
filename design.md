Design document
===============

SpaceChem + Factorio
--------------------
SpaceChem has unique and excellent mechanics where the programming is spatial, 
geometrical, and on the same surface as the action.
- Subsequent Zachtronics walked back from this choice into sequencing virtual 
  robots in one space, with a document in another space 
SpaceChem conceptualizes itself as puzzle game, with very small grids of 
specified tasks.

By contrast, Factorio has 
- an unbounded grid, and an unbounded and very large amount of stuff happening
- an emergent tree of goals, where the high level goal of survive/grow leads
  to solving a tree of self-defined sub-goals
- much more about "just" routing than logic
  - the inserters are shockingly intelligent (and the splitters can be)
  - endgame logistic robots remove the routing, leaving... not much? 
Though Factorio belts are probably doing computations, there is also another
off-grid layer of circuit programming

Combined, we can envision a game with
- An unbounded grid
  - Procedurally generated with resources and obstacles
- The player has a limited ability to write symbols to the grid cells
  - For instance, lay down arrows
- An (increasing) number of agents move around the grid reading the symbols
  and acting on them
  - Go east, pick up, compare with X, conditional left turn, drop
  - SpaceChem waldos, Turing machines, "trucks"
- Most agents will be running around some relatively small loop with a few
  branches.  A group might run the same route for long distance transport of
  items which are then dumped on and sorted by a single specialized agent.
  AKA, it could end up as agent-based computing.  This is up to the user though!
- Massive scale.  As a concrete example, we want to be able to comfortably
  handle 1k agents on a 1k x 1k grid.
- No PDEs!  Fluid flow, creep, pollution, etc. are all death to performance.
  - The use of agents with local views of the world is a much better fit to how
    CPUs work.
  - What about GPUs?
- Agents spend most of their time traversing cells; they have to be slow enough
  that the player can see what they're doing.  This makes them cheap; they sleep
  while travelling, their location is visualized as moving but they do no work
  until they arrive.  If 1 cell per second, they sleep:wake 60:1 and in out 1k
  target we only have to do 15 waldos per tick on average; we can make
  different tasks be coprime so they rapidly distribute themselves over
  timeslices.  
  
Geometry
--------
SpaceChem and its 3d successor, Infinifactory, build physical structures out of
grid cell sized atoms (literally in SpaceChem) and move these extended bodies
around, where they can collide in complex and non-local ways.  This is a very
elegant way to solve the "what to construct" issue, but rotation wrecks
"gridness" and collisions while rotating become a complicated, nonlocal and slow
problem.

By contrast, all things in factorio (in their stored/beltable form at least) are
exactly the same size.  We transform them into other things with fixed recipes.

Simple industrial chemistry like reduction/smelting, oxidation/burning,
calcination etc. can be regarded as operations on bitmasks of the presence of
elements
```
    iron ore + hydrocarbons + oxygen -> steel + carbon dioxide + water
    FeO + CH + O -> FeC + CO + HO
    hydrocarbons + oxygen -> carbon dioxide + water     
    CH + O -> CO + HO
    limestone + oxygen -> lime + carbon dioxide 
    CaCO + O -> CaO + CO 
```
and potentially even conservative operations on bitmasks, where we dump waste
products as the XOR of the inputs or something.
while this is intriguing it doesn't really seem rich enough - we have different
things with the same elements present, we have processed goods, etc.
  
Skin
----
Factorio (from Minecraft from...) has a natural resource crafting tech
progression.

Lots of natural stack machine opcodes either copy or destroy values, which is
not great for a resource game, where we want them to be conserved unless 
transformed by some process (crafting).

So, unlike a basic stack machine, we have to represent pysicality somehow and
have opcodes behave differently on physical values.

For example, if we just have a flag bit indicating, conserved, then load becomes
exchange 0; store becomes exchange top; pop becomes noop; dup becomes noop (or,
make a "ghost" version?


A Von Neumann stack machine in 2D
---------------------------------
With symbols (opcodes) and resources (data) in the same address space, we have
something like a von Neumann machine.  (The other Zachtronics games are 
Harvard).

The complexity of the opcodes must be minimal, leading us to 0-operand stack
machines.  The SpaceChem waldos can be understood as single-slot stack
machines that can load (grab), store (drop), and compare.  

Stack machines are fairly intuitive, and we can understand much of their
instructions as a forklift or dumptruck picking up a load, moving along,
choosing a turn based on the contents of the load, and dropping it off
somewhere.  However, the push-down stack is not a great analogy for these - 
they can carry one "thing"; real transporters of multiple things don't have a 
big linear pile, they more frequently have a basket or slots.

In a 2D address space, the program counter becomes a coordinate, and jump
instructions would be teleportation.  Instead of conditional jumps, we can
conditionally change the direction we are moving across the address space.
Loops become actual physical loops and we literally branch our paths.  This is
very attractive.  To get to a "routine", we have to drive there.  Our waldos
will (potentially) spend a lot of their time driving along straight "roads" of 
"no op".  Unlike Factorio, we will get a proportial penalty for meandering
routes--it will cost more time every traversal, not just a one-off startup
penalty.

- Jump (teleportation) and random-access memory (radio?) might be good endgame
  options.
- Speed decreasing with stack size?

Mutual Exclusion
----------------
With many agents in the same address space, we have to deal with concurrent
access to cells.  This actually opens up rich gameplay, like Factorio train
signalling and the associated deadlicks.

If we want agents to not overlap, then they need to lock and unlock cells as
they enter and leave them, and to efficiently wait on locked cells, as in a
condion variable's queue.  To pass each other in cross-traffic, it is not
enough to do one-ahead locking, we need to lock two or none (like Factorio 
rail signalling).  Head on collisions will deadlock, as they should.

This needs then the ability to wait on several locks (?; or, wait on one and 
unlock the ones already acquired atomically).  We also want to wait on changes
of value?


Multiplayer
-----------

Purely cooperative and unsecret






Platform
--------

macOS / AppKit:

We will get various notifications on the main thread / queue / runloop, such as
keys, mouse, window resize...

We get the critical display link on another thread.  Currently we lock out the
other thread when resizing and rendering, which is not great; we should just
atomically update the size, and resize the drawable as we need it. 




AppKit
------

Do we need a ViewController?  it seems mostly to be for loading-from-file views

Notifications we need go to:

NSApplicationDelegate
X CALayerDelegate -- only needed for event-based drawing
NSView has no delegate, instead we subclass it
NSView subscribes to windowWillClose which is available to NSWindowDelegate

Should we just make 
`WryDelegate : NSObject <NSApplicationDelegate, NSWindowDelegate, WryViewDelegate>`
as the clearing-house for all `dispatch_queue_main` stuff?

And then, `WryRenderer` becomes the class associated with all stuff that occurs
on the CVDisplayLink thread

`WryModel` lives on a third thread/queue, receives notifications from main queue
and sends minimal data to render; advances game state

`WryNetwork` lives on a fourth thread for multiplayer

 


NSView
CALayerDelegate


# ZZZ

Rotating a vector 

(a, b)

by 60 degrees is

( c, -s)
( s,  c)

(ca - sb)
(sa + cb)

(a - b sqrt(3)) / 2
(sqrt(3) a + b) / 2

sqrt(3) = 1.73    1.75 = 7/4

( 1/2   -7/8)
( 7/8   +1/2)

(4 a - 7 b) / 8
(7 a + 4 b) / 8


(1, 1) -> (- 3, 13) / 8

(1, 2) -> (-10, 18) / 8
(1, 3) -> (-17, 19) / 8
(2, 3) -> (-13, 26) / 8 = (
(1, 4) -> (-24, 23) / 8 = (-3, 2.875)
(3, 4) -> (-16, 37) / 8 = (-2, 4.625)

ASPMT



// projection
// isometric looks nice but square is better for drafting

// isometric angle is (1,1,1)
// right triangle is 1, sqrt(2), sqrt(3)
// so we are talking about 1/sqrt(3) as the v:h pitch
// 0.577 = 37/64




// Illumination

What shape is the shadow of a rectangle:

Consider the shadow of a quadrant illuminated by a uniform hemisphere sky

Shadow cast on a surface by blocker on plane 1 unit up

In the coordinates of the plane, an unobstructed region dxdy contributes in
proportion to the solid angle it subtends

area:

dx dy

distance:

1 / (x^2 + y^2 + 1)

angle:

cos theta = 1 / sqrt(x^2 + y^2 + 1)

dx dy / (x^2 + y^2 + 1)^1.5

and another factor of cos theta for the glancing incidence

dx dy / (x^2 + y^2 + 1)^2

This is the radiosity _view factor_, cos theta1 const theta2 / (pi s^2)

Compute the shadow of a corner, which is 1/(x^2 + y^2 + 1) convolved
This is now a cumulative shadow/illumination map
We can make any rectangular shadow by corners:
```
 + -
 - + 
```
And scaling the shadow map by distance
And using the texture map edge clamped mode to extend beyond the map
Shadow is now four lookups in the same texture


### Notes on deferred physically-based rendering

- On Apple silicon the gbuffer can be entirely memoryless if we do everything 
in the screen pass

- No readback from the current depth texture, but can we bind it as an
attachment?

Importance sampling:

To integrate f cos\theta over a hemisphere, we can importance sample from an
offset sphere:
```
(x, y) = (cos theta, sin theta) * cos theta
(r, theta) = (cos theta, theta)
```
describes a circle of radius 0.5 centered at (0.5, 0.0).

As a probility distribution of theta, this sphere is
```
P(r, theta, phi) = r^2 sin(theta)
P(theta, phi) = 1/3 cos(theta)^3 * sin(theta)
```
And the indefinite integral wrt theta is
```
CDF(theta) * PDF(phi) = cos(theta)^4
```

so we can importance sample directions with `x, y in [0, 1]` mapped to
```
theta = acos(pow(x, 0.25))
phi = 2 * pi * y
```
and thus
``` 
u = r sin(theta) * cos(phi) 
v = r sin(theta) * sin(phi)
w = r cos(theta)

w = pow(x, 0.25)
v = sqrt(1.0 - sqrt(x)) * cos(phi))
u = sqrt(1.0 - sqrt(x)) * sin(phi))
```

Step two:

Now sample from a sphere scaled by a,
```
(s, z) = (a sin psi, cos psi) * cos psi
```
We again have the spherical polar weights
```
P(r, theta, phi) = r^2 sin(theta)
```
We need to relate psi and theta
```
```

But now theta and psi are different, we need to integrate out to
`r_max` such that 

```
r sin theta = a sin psi cos psi = a sin 2 psi
r = a sin 2 psi / sin theta
``` 

```
P(theta, phi) = 1/3 r^3 sin(theta)
    = (a sin 2 psi)^3 / (3 sin(theta)^2)
```




Volume of our sphere is 4/3 pi r^3 vs cube = 1 so sphere occupies
4/3 pi / 8 = pi / 6 of bounds
so rejection sampling will waste half the samples

to sample from a sphere:

x^2 + y^2 + z^2 = 1

x^2 + y^2 = 1 - z^2

area of slice at z is pi(1 - z^2)

cumulative area is pi(z - 1/3 z^3)

solve    y = z - 1/3 z^3

y = z (1 - 1 / 3 z^2) ugh

aha:

Malley's method:

given samples on a unit disk
`x, y`
or
`r, phi`
project to hemisphere
`z = sqrt(1 - r^2)`
this is now a cos theta weighted importance sampling of a hemisphere

 

now rescale horizontal plane 
```
u, v = x * a, y * a
s = r * a, phi
```

no longer normalized, but it doesn't need to be for cube sampling 

now compute a second normalization (if needed) 
```
= sqrt(s^2 + z^2)
= sqrt(a^2 r^2 + 1 - r^2)
= sqrt((a^2 - 1)r^2 + 1)
```
finally,
```
t = r*a / sqrt((a^2-1) r^2 + 1)
zz = sqrt((1 - r^2)/((a^2-1)r^2+1))

```







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


