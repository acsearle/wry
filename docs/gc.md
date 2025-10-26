Garbage Collection


Global state is
- the current epoch
- things pinning the current epoch
- things pinning the previous epoch

The global current epoch can advance when we can prove that nothing is pinning 
the previous epoch.  We increment the epoch, and move the list of things
that were pinning that epoch to the empty list of things pinning the previous
epoch.

We can implement pinning as a count to detect emptiness, or we can implement it
as a list with one node per thing, and its current epoch (if any).  If we then
observe that the list contains only the current epoch, we can advance.
When an epoch has gone from current to prior to prior^2 we can destroy its
objects.

This requires that once there is nobody in the previous epoch, it stays that
way.  We can accomplish this by allowing things to pin the current epoch, or to
pin the epoch associated with another current pin.

Threads pin the current epoch, note that epoch, create objects, publish them to
other threads, and unpin the same epoch (which may now be the previous epoch).  
Such objects will live long enough beyond the unpinning to allow other threads
to finish accessing them, but ownership cannot be transfered to another thread
because that other thread may be pinning the next epoch.

On an already pinned thread, we can pin the epoch again, note it, transfer the
pin and the noted epoch to another thread, and unpin it there.  This
extends the epoch of the original thread and makes an explicit lifetime or
scope.  For example, we can pin a thread, create the root of a tree of work,
take a transferrable pin+epoch, and then unpin it only when the work is
completed and all of the associated items may be collected.  In the limit of
pinning each item individually, we are effectively reference counting the
allocator.  Note that we can't use RAII for this since epoch allocation does
not destroy individual objects.

Taking transferrable pins from the thread epoch rather than the current epoch
can increase the prior count but never increases it from zero.

When a thread discovers that an epoch has advanced, it can discard last-but-one
thread local resources.


The overlapping epochs, advancing when consensus is reached, is very similar to
the tracing collector.  Each mutator likewise enters and leaves states, and we
defer destruction until we have consensus on the past.  The tracing collector
does not have an incrementing epoch, but does have the color vector advancing.


In the tracing collector, each thread allocates a Session (pin).









#  Garbage Collection

## Todo

- Mutator shading leaf, is an unconditional write BLACK faster than a
  compare_exchange?
- Can we segregate leafs from the scan list?
- Should the mutators maintain and proffer gray lists instead of just a gray
  flag?
- Can we use timestamps instead of colors to help do better weak pointers and
  have the GC always in sweeping state?
  - Each thread has an epoch
  - Epoch advances by consensus, so all threads are running with current or
    old epoch
  - threads mark objects leq their epoch with their epoch+1 or +2
  - threads mark weak objects that are one or two behind, and fail to load ones
    that are too early
  - collector kills objects 4-behind the epoch
  - collector can scan and sweep simultaneously
  - mutators can upgrade weaks that aren't too old, but how do they decide too
    old when they have ragged epoch?  Weaks that are two cycles behind can
    be upgraded by oldies and downgraded ny noobs
  0 white  -> 0 weak?     -> 0 dead
  1 gray   -> 1 weakgray? -> 1 forbidden 
  2 black  -> 2 white     -> 2 weak ...
              3 gray     
              4 black

- The mutator following pointers to swap their colors is not great, in
  particular for overwriten pointers that are not otherwise needed in cache.
  If the mutator instead records these pointers for the GC in a hot queue, it
  could be faster.  But the queue will be immense?  And duplicative across and
  even within mutators.  These queues effectively are the collectors's 
  workstack, so it makes sense to publish them frequently to the collector, more
  frequently than per handshake.  Mutators would not know if they are "dirty"
  in this scenario.
  
  Conversely, if the mutator is already writing to an object header, should it
  snapshot the pointers out of that object for the workqueue?  No, because this
  is an unbounded amount of work.
  
  
  
  
- This technique is pause free by most definitions, requiring the mutator to
  check in with the collector periodically in a single lock-free operation that
  never makes the mutator wait (though it may have to retry).  But it is not
  widely used.  Why not?
  
  One reason may be that there is literally no path for the collector to apply
  backpressure on a mutator; the mutator(s) may simply outrun the
  collector(s) and exhaust memory even though the true live set is small.  This
  seems unlikely in practice because the collector becomes more efficient the
  more garbage there is, but no doubt an example could be contrived to break it
  (several mutators allocating small objects flat-out?).  Is this efficieny 
  argument enough to keep the system stable or do we become unstable, with
  more allocations increasing the time to the next collection increasing the
  number of allocations in the next collection, and so forth. 


## Notes

As we allow users to effectively execute abritrary (albeit sandboxed) code, we
must be able to handle cycles, which pushes us to garbage collection for the
same reason Lua et al are garbage collected.

Unlike general-purpose languages, we are operating in a soft-real-time system
and strongly favor no pauses over increased throughput.  We take the extreme 
stance that garbage collection must never cause an O(N) pause, and accept 
that this will impose significant overheads on common operations.  Most 
mutator operations will be wait-free; we use some locks for simplicitly but 
everything can in principle be obstruction-free at least.

Our method is inspired by Doligez-Leroy-Gonthier[^1].  We dedicate a garbage 
*collector* thread.  Other *mutator* threads must run with a *write barrier* 
that updates metadata whenever a pointer write changes the object graph, and 
periodically (perhaps once per frame) performs an O(1) handshake to synchronize
with the collector.  The collector concurrently traces the object graph, so all
participant objects must be concurrency-aware *enough* to produce a
conservative reachability result; this is a much weaker requirement than what 
is usually meant by a concurrent data structure, but it still imposes
significant costs and restrictions.  This system is obstruction(?)-free for the
mutators, but the collector thread will rely on regular handshaking to
progress.

Consider the less powerful std::shared_ptr system; there, an implicit write
barrier performs an atomic release-decrement to one control block, and an atomic
relaxed-increment to another.  The decrement may invoke a destructor which in
turn invokes other decrements, resulting in an unbounded pause, or even a
stack overflow for naively implemented list-like data structures.  And, of
course, the system cannot collect cycles.  Rectifying these problems tends to
make the system resemble a concurrent collector [^1].

Our collector does not compact, and relies on the platform malloc/free to
control fragmentation.  A concurrent compating collector would require a read
barrier.

The write barrier is conservative and simple, unconditionally shading the old
and new pointer values.  We expect it would be more expensive to check the
parent's color and the collector state and then conditionally shade.  

[^1]: Jones, Hosking & Moss, The Garbage Collection Handbook

## Algorithm

Each participating object stores an explicit *color* for the *tri-color
abstraction*.

Initially, there are no objects.

Mutator threads allocate new objects *white*, and record their addresses in a
thread-local queue.

There are only *white* objects.

As objects are stored into each other, the write barrier *shades* both the old
pointee and the new pointee from *white* to *gray*.  *Gray* indicates that the
object has been *reached* but not yet had its own fields scanned.  If a
pointer is null or the pointee is already *gray*, the barrier has no effect.

There are only *white* objects and *gray* objects.  Any object stored in
another object is *gray*.

This situation is stable.

The collector thread announces a new allocation color, *black*, and waits for
all mutator threads to acknowledge this change.

Some threads are allocating *black*.

Objects of all colors are present.  Any object stored in another object is
*gray*.  In particular, the *tri-color invariant* that no *black* object has
*white* members is upheld.

Each mutator handshakes the collector to acknowledge that it is now allocating
*black*, and sends the collector its list of allocations.

All mutators have handshaked and are allocating *black*.  The collector knows
of all allocations by a thread before its handshake, and thus it knows of
all objects that are *white* or *gray*.

After handshaking, the mutator *shades* its roots, and continues to execute the
write barrier, turning *white* objects *gray*.  When it turns an object *gray*,
it notes that it is *dirty*.  It also continues to allocate *black* objects.

Concurrently, the collector *scans* its list of all known objects.  When it
discovers a *gray* object, it *traces* through the object to *shade* its fields
*gray*, and then *blackens* the original object.  Like the mutator thread, the 
collector thread marks itself *dirty* whenever it turns an object *gray*.

Several optimizations are important.  Once an object is *black* it needs no
further  action so it can be placed in a *blacklist* to avoid rescanning.
When an object is turned *gray*, it can be remembered in a stack and processed
promptly, before returning to scanning the main list; this prevents the need to
rescan all objects to progress through every link of a long chain.  The stack
lets us traverse depth first, which reduces the size of the stack.  Finally,
since the queue records outstanding work, we can directly turn such objects 
*black*, in temporary violation of the *tri-color invariant*.  This is safe
because the mutator behaves the same for all nonwhite colors, and the collector
restores the invariant before relying on it again.  Thus collector never needs
to rediscover work it generated, so it never needs to set the dirty flag.

When the collector finishes scanning all known objects, it requests another
round of handshakes with the mutators.  Each handshake transfers and clears the
mutator's *dirty* flag and list of recent (black) allocations (which can be put
directly into the blacklist).

If the collector or any mutator is *dirty*, there may be an new *gray* object
in the list of known objects for the collector to scan, and we continue another
round of scanning and handshakes.

If there are no *gray* objects to process, we have traced and *blackened* all
objects reachable from the roots at any point since the first round of
handshakes, and all objects allocated since that time started out and remain
*black*.  Any *white* objects are unreachable and may be collected.

At this point, all black objects are in the blacklist, or in the mutator's 
recent allocations list.  There are no gray objects anywhere.  All white
objects are left in the list of known objects.

We walk the list of known objects, asserting that each is white, and deleting
it.  The object destructors are run and can be non-trivial, but they must not
follow any garbage collected pointers they contain.  They can free any non-gc
memory they hold (such as a member std::vector) and other resources like
file handles.

We now replace the known list with the blacklist.

To return to the initial condition of white and gray objects, we must clear
each black object back to white, but without violating the tri-color invariant.
This is impossible to do piecewise, as can be trivially seen from cycles.

Instead, the mutator publishes a redefinition of the numbers interpreted as
black and white, exchanging them, and requests acknowledgement.

A mutator is allocating black and observing only nonwhite objects.  When the
redefinition is received, it finds itself allocating white objects and seeing
only nonblack objects.  It begins to shade some white objects gray, but if
another mutator that has not yet observed the redefinition sees these objects,
it views them as black to gray, and only cares they are nonwhite.  No mutator
observes a white object pointing to a black object, so the invariant is upheld.

When all mutators have acknowledged the change, we are back to the initial
condition, where there white objects and gray objects, but no black objects.

### Leaf objects

A *leaf* object is one that has no garbage-collected fields.  This is very
common.  Such an object doesn't need any scanning, so we can shade it directly
from white to black.  This is an important optimization because it reduces the
amount of gray objects made by the mutators, which delays collector completion.

This change doesn't violate the tricolor invariant, but it does mean that the
list of known objects may contain some black objects when being swept, so we
must check the color while sweeping and permit black leaves.  

TODO: do leafs ping pong when we redefine white?

### Weak leafs

A pointer is weak if it does not participate in tracing; the pointee will be
reclaimed unless another "strong" pointer keeps it alive.  This is desirable
in caches, memoization and canonicalization, all of which want to enable the
lookup of objects, but not keep unused objects alive.

When an object is discovered by a weak pointer, it must be upgraded to be
held by a strong pointer to be used.  This is done by shading the object,
shading it white to black if it is a leaf.  However, if the object is to be
reclaimed, this races with sweeping.  Weak leafs that are white are condemned
from white to red by the sweeper.  This races with the upgrade process.  If
the upgrade wins, no further action is taken.  If the condemnation wins, the
object will still be reached by the mutator threads to discover the red color,
so the object must live a little longer.  The mutator and collector threads
again race to respectively replace or erase the object from the weak container.
After the next round of handshakes, mutators will only see the new state of the
container and the object is truly unreachable.  

## Interface

The collector needs to execute a variety of operations on objects that depend
on their type, or at least their layout.  The most straightforward
implementation in C++ is to require all garbage collection participants to
derive from a base class with a vtbl pointer, and to override a subset of the
behaviors.  The base class also contains the atomic color, which has only four
valid states but occupies a full 64 bit word for simplicity.  There is
obviously ample opportunity to combine the color and type information somehow.

Allocation and pointer writes both need access to some state:

- The current encoding of white (and black)
- The current encoding of new allocation color
- The log of allocations since the last handshake

These could be global relaxed atomics and a concurrent list, or they could
be `thread_local` variables updated during handshakes.  TLS is expensive on
some platforms.  It's clunky to pass a context pointer around into all
operations.  Global vars may be the cheapest method.

The concurrent log is harder; we have an expensive TLS lookup or a
contested concurrent bag with a few atomic operations per thingy.
