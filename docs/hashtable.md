#  Hash table

We need
- Garbage collection-compatible
- Bounded worse case, in particular no O(N) rehash/resize
- Performant

We don't need
- Particularly high load factors / space efficiency
- Reference stability

It may be worth having different objects rather than a one-size fits all
for tables that have short lives, are otherwise bounded, or do not support
erase.

An incremental copying strategy is reasonably straightforward but seems
incompatible with tombstones.  When the table is roughly steady state, it will
accrue tombstones, and then rehash; the rehash then trades size growth off
against the number of slots that must be scanned per increment, with six being
the minimum needed to downsize a sparsely populated graveyard.  

An alternative is robin hood hashing which produces no tombstones and instead
does the work of recompacting as part of erasure, and will never need to resize
in a steady state.  By moving stuff around, it will generate a lot of GC 
traffic.

The no-resize alternative is the HAMT or BTree with O(log N) operations.



