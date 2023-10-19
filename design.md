Design document
===============

SpaceChem + Factorio
--------------------
SpaceChem has unique and excellent mechanics where the programming is spatial, 
geometrical, and on the same surface as the action.
- Subsequent Zachtronics walked back from this choice into sequencing virtual 
  robots in one space, with a document in another space. 
- SpaceChem conceptualizes itself as puzzle game, with very small grids of 
  specified tasks.

By contrast, Factorio has:
- An unbounded grid
- An unbounded number of "actors"
- Emergent tactical goals, where the strategic survive/grow objective leads
  to solving a tree of self-defined sub-goals
Factorio is must more about just routing than logic
- The primitives are too smart
  - Burner inserters choose from belt on the basis of what target can accept
  - Splitters prioritize and sort
  - Trains route
- Only logistic robots are dumb
- Circuit networks are optional and "offscreen"
At endgame, trains and bots replace belts at the macro and micro scale, and
the game devolves to debugging train AI

Only belts and pipes are fully described by their onscreen appearance and
orientation.  Other mechanics rely on windows to choose mostly hidden settings,
some of which may be shown in ALT-mode.

Fluids are implemented as a crude ODE that uses lots of CPU for poor results,
such as order and direction effects. 



Combined, we can envision a game with
- An unbounded grid
  - Procedurally generated with resources and obstacles
- The player has a limited ability to write symbols to the grid cells
  - For instance, lay down arrows
- A (large, increasing) number of agents move around the grid reading the
  symbols and acting on them
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

Purely cooperative and unsecret.  Server cannot provide state, which is
enormous and expensive to step.  Instead, the sever acts only to
authoritatively order player actions.  Clients must accept user input to
generate actions and send them; then wait for the server to return the
authoritative list of actions.  Incoming actions, from self or other players,
are not guaranteed to be valid; each client must check if they are allowed.
They may become invalid because of other actions by other players.

Each (good faith) client will have the same game state at a given tick, but
different knowledge of sent actions, and different partial receipt of future
actions.

"Bad" clients (hacked or bugged) can submit malformed or too many actions or
request too many resends etc.

Clients cannot progress past their latest server knowledge, but the server is
not required to wait for any client; it will keep resending allowing a client
to scramble back to real time.  

A client that experiences a temporary outage will progress to last known server
state, then wait there requesting until re-established.  It will have fallen
behind though, and has to fast forward to get back to near-real time.  This
can be done off-screen.

For a severed network, the client can checkpoint last known state and fork a
new local game.  On clean shutdown of server, all clients will be informed to 
take a checkpoint to resume the game.  New players can join an existing game
by requesting transfer of a checkpoint from peer(s) and then fast forwarding it.

Because action lists are small and cheap, we can store them plus checkpoints to
reconstruct game state over some number of seconds.  It is expensive to fast
forward, though, so we must checkpoint (we can't just save seed + actions)

Checkpointing without interrupting the game will impose significant
requirements on the engine




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

Take two:

Do user interface and rendering on the main thread.

Loosely coupled work like audio, IO and the actual simulation on other threads 
 


