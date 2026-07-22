# The Machine Language

Documentation for the language defined by `OPCODE` (game/opcode.hpp) and its
interpreter `Machine::notify` (game/machine.cpp), as of 2026-07-21.  The
second half of this document is a design review: missing opcodes, rough
edges, and contradictions found while tracing the implementation.

Status: descriptive, not normative.  Where the implementation and the
apparent intent disagree, both are stated and the discrepancy is flagged.

--------------------------------------------------------------------------

## 1. The computational model

The world is an unbounded 2D grid.  Each cell has two independent planes:

- a **value plane**: one `Term` per cell (empty = null Term), holding
  numbers, opcode glyphs, matter, or any other Term;
- an **occupancy plane**: at most one entity (machine) per cell.

A **machine** is an oriented stack machine that drives across the grid.
There is no program counter and no jump: the machine's *position and
heading are the program counter*.  A program is a geometric arrangement of
opcode glyphs on the ground; the machine executes whatever it drives over.
Control flow is turning.  Data and code share the value plane: a cell can
hold a number today and an opcode tomorrow, and machines can read, write,
and relocate both.

Instructions take effect at **arrival instants**.  Each hop between
adjacent cells takes 64 ticks, and everything a machine does at an arrival
-- read the cell, execute it, mutate its stack, perform a pending memory
operation, claim the next cell -- happens in a single transaction that
commits or aborts atomically.  Between arrivals the machine is inert
(travelling).  Two machines' transactions conflict only when they touch
the same cells; conflicts are resolved by per-tick pseudorandom priority,
losers retry, and nobody starves (see core/transaction.hpp).

Machines never overlap.  A machine that wants to enter an occupied cell
**blocks**: it parks and sleeps until the occupant leaves (or until the
value under the parked machine changes, which serves as an external escape
hatch).  Blocking is the language's only synchronization primitive, and it
is a good one -- but deadlock is expressible (section 8.13).

Matter is conserved.  A `MATTER` Term denotes a physical object; the
opcode semantics refuse to copy it (`DUPLICATE`, `OVER`) or destroy it
(`STORE` onto matter waits instead of overwriting).  Numbers, by
contrast, are information: freely copied by reading and destroyed by
overwriting.  Only a `Source` entity creates matter and only a `Sink`
destroys it.

### Headings

Headings are quarter-turns from north: 0 = north (+y), 1 = east (+x),
2 = south (-y), 3 = west (-x).  Internally a heading is a full `i64` and
is only reduced mod 4 when a step is taken; turns add or subtract, so the
stored heading accumulates (this leaks: see 8.6).


## 2. Machine state

A machine is, in language terms:

| register    | contents                                                    |
|-------------|-------------------------------------------------------------|
| location    | the cell it is on (or arriving at)                          |
| heading     | direction of travel, quarter-turns from north               |
| stack       | a stack of Terms, unbounded depth                           |
| pending op  | `_on_arrival`: a deferred memory opcode, applied on arrival |

The stack is a persistent linked list; pushes of null Terms are silently
discarded, and all partial operations (arity or type mismatch) leave the
stack untouched rather than faulting.  There are no error states: every
opcode that cannot apply is a no-op.  This "fail to a no-op" discipline is
systematic and deliberate -- a machine can never crash, only do nothing.


## 3. The execution cycle

The interpreter is a pipeline over two cells: the opcode a machine drives
over is *executed* at that cell, but its *memory effect* (if it is one of
the four memory opcodes) applies to the **next** cell the machine enters.
Precisely, at each arrival at cell C:

1. **Pending memory op gates interpretation.**  If `_on_arrival` (set at
   the previous cell) is `LOAD`, `STORE`, `EXCHANGE`, or `SKIP`, the value
   at C is treated as *data*: it is not executed and not auto-picked-up.
   Otherwise, if the value at C is an opcode glyph, it becomes this
   arrival's action.

2. **HALT check.**  If the action is `HALT`, the machine parks on C and
   sleeps until the value at C changes.  (On wake it re-runs this cycle
   and executes whatever is now at C.)

3. **STORE guard.**  If the pending op is `STORE` and either (a) C holds
   matter, or (b) the top of stack is matter and C is not empty, the
   machine parks and retries when C's value changes.  Matter is never
   overwritten, and matter may only be placed into an empty cell.

4. **Heading resolution.**  Turn opcodes (absolute, relative, branch,
   flip-flop) compute the new heading now.  All other actions go straight.

5. **Claim or block.**  The next cell is location + one step along the new
   heading.  If it is occupied, the machine parks (waiting on that cell's
   occupancy, or on its own cell's value as an escape hatch) and *nothing
   else happens* -- steps 6-7 are not performed until the way is clear, and
   the whole arrival re-runs then.  Otherwise the machine writes itself
   into the next cell's occupancy now (it briefly occupies both cells
   while travelling).

6. **Pending memory effect at C.**
   - `LOAD`: push C's value.  Matter is *moved* (cell emptied); anything
     else is *copied* (cell keeps its value).
   - `STORE`: pop and write into C (overwriting information; matter was
     guarded in step 3).  Popping an empty stack yields null, so a STORE
     with an empty stack *erases* C.
   - `EXCHANGE`: pop x, push C's value, write x into C.  A pure swap;
     conserves everything.  With an empty stack it moves C's value onto
     the stack and leaves C empty.
   - `SKIP`: nothing.
   - none of the above (the common case): **auto-pickup** -- if C holds a
     small integer, push a copy of it.  Driving over a number reads it.
     This is the idiomatic way both literals and signals enter the stack.

7. **Action effect.**  Stack manipulation, arithmetic, logic, and the
   flip-flop self-modification happen here (see the reference).  Branches
   consume their operand here.

8. **Depart.**  `_on_arrival` is set to this arrival's action, and the
   machine begins the 64-tick hop.  So a memory opcode executed at C
   addresses the cell *straight ahead of* C (memory opcodes never turn),
   and any other opcode is simply remembered as "already done".

The pipeline means an instruction with an operand is laid out as two
consecutive cells along the direction of travel:

    heading east:   [LOAD] [x]     pushes x's cell contents
                    [STORE] [_]    pops into the cell after STORE
                    [SKIP] [F]     drives over F without executing it

and that the operand cell is protected from interpretation: `[LOAD] [F]`
picks up the glyph F as data rather than executing it.  `LOAD` is quote.

A consequence of step 5's ordering: a machine's memory effect and its
motion are indivisible.  If it cannot move on, it has not yet acted.


## 4. Values, as the machine sees them

| Term class          | executed?  | auto-pickup? | ALU?  | notes                              |
|---------------------|------------|--------------|-------|------------------------------------|
| opcode glyph        | yes        | no           | no    | LOAD/EXCHANGE move it as data      |
| small integer       | no         | yes (copy)   | yes   | 60-bit signed inline               |
| heap integer        | no         | no           | no    | overflow product; inert (see 8.5)  |
| boolean             | no         | no           | no    | predicate output; inert (see 8.2)  |
| matter              | no         | no           | no    | conserved; opaque to tests (8.7)   |
| empty (null)        | no         | no           | no    | pushes of null are discarded       |
| anything else       | no         | no           | no    | strings etc.: inert cargo          |

"ALU" means the arithmetic/logic/comparison opcodes will operate on it;
they all require *small integers* and no-op otherwise.

The 0/1/2/3 heading encoding doubles as the language's enum-of-directions,
consumed by `BRANCH_*` as "number of quarter-turns".


## 5. Opcode reference

Stack effects are written `( before -- after )`, top of stack rightmost.
Unless stated otherwise, an opcode whose operands are missing or not small
integers is a no-op (stack, heading, and world all untouched).

### 5.1 Motion and control

| opcode        | effect                                                        |
|---------------|---------------------------------------------------------------|
| NOOP          | nothing                                                       |
| HALT          | park on this cell until its value changes; then execute anew  |
| TURN_NORTH    | heading := 0 (likewise EAST 1, SOUTH 2, WEST 3)               |
| TURN_RIGHT    | heading += 1                                                  |
| TURN_LEFT     | heading -= 1                                                  |
| TURN_BACK     | heading += 2                                                  |
| BRANCH_RIGHT  | ( n -- ) heading += n quarter-turns                           |
| BRANCH_LEFT   | ( n -- ) heading -= n quarter-turns                           |
| FLIP_FLOP     | heading += 1, and the glyph rewrites itself to FLOP_FLIP      |
| FLOP_FLIP     | heading -= 1, and the glyph rewrites itself to FLIP_FLOP      |

`BRANCH_*` with n = 0 goes straight, so sign-valued data (-1/0/+1 from
`SIGN` or `COMPARE`) steers left/straight/right directly.  With n in
0..3 it is a four-way computed switch.  The flip-flops are the language's
built-in alternator: a stream of machines through one splits alternately
right and left.

### 5.2 Memory (prefix opcodes; operand = the next cell entered)

| opcode    | effect on arrival at operand cell                              |
|-----------|----------------------------------------------------------------|
| LOAD      | ( -- x ) push cell value; matter is moved, other values copied |
| STORE     | ( x -- ) wait until legal, then write x over the cell          |
| EXCHANGE  | ( x -- y ) swap top of stack with cell value                   |
| SKIP      | pass over the cell without executing or picking it up          |

All four suppress interpretation of their operand cell.  STORE waits
rather than destroy matter, and waits for an empty cell when placing
matter.  STORE of information over information overwrites silently.

### 5.3 Machine registers

| opcode          | effect                                                     |
|-----------------|------------------------------------------------------------|
| HEADING_LOAD    | intended ( -- h ): push current heading.  As implemented, also sets heading from the (un-popped) top of stack -- see BUG in 8.1 |
| HEADING_STORE   | intended ( h -- ): set heading.  As implemented, pops h and *does not* set the heading -- see BUG in 8.1 |
| LOCATION_LOAD   | UNIMPLEMENTED (executes as NOOP); blocked on Coordinate-as-Term encoding |
| LOCATION_STORE  | UNIMPLEMENTED (executes as NOOP); as specified it would be teleportation, which contradicts the no-random-access principle -- see 8.3 |

### 5.4 Stack manipulation

| opcode     | effect                                                       |
|------------|--------------------------------------------------------------|
| DROP       | ( x -- ) discard; if x is matter, instead becomes a STORE of x into the next cell (matter is put down, never destroyed) |
| DUPLICATE  | ( x -- x x ), refused for matter                             |
| SWAP       | ( x y -- y x ), any Terms including matter                   |
| OVER       | ( x y -- x y x ), refused when x is matter                   |

There is no ROT, PICK, or DEPTH: the stack below the top two elements is
unreachable except by popping (see 8.11).

### 5.5 Predicates and logic -- all currently produce inert booleans (8.2)

| opcode                    | effect as implemented                        |
|---------------------------|----------------------------------------------|
| IS_NOT_ZERO               | ( a -- bool(a != 0) )                        |
| LOGICAL_NOT               | ( a -- bool(!a) )                            |
| LOGICAL_AND               | ( a b -- bool(a && b) )                      |
| LOGICAL_OR                | ( a b -- bool(a \|\| b) )                    |
| LOGICAL_XOR               | ( a b -- bool(!a != !b) )                    |
| EQUAL                     | ( a b -- bool(a == b) )                      |
| NOT_EQUAL                 | ( a b -- bool(a != b) )                      |
| LESS_THAN                 | ( a b -- bool(a < b) )                       |
| GREATER_THAN              | ( a b -- bool(a > b) )                       |
| LESS_THAN_OR_EQUAL_TO     | ( a b -- bool(a <= b) )                      |
| GREATER_THAN_OR_EQUAL_TO  | ( a b -- bool(a >= b) )                      |
| IS_ZERO                   | UNIMPLEMENTED (NOOP)                         |
| IS_POSITIVE               | UNIMPLEMENTED (NOOP)                         |
| IS_NEGATIVE               | UNIMPLEMENTED (NOOP)                         |
| IS_NOT_POSITIVE           | UNIMPLEMENTED (NOOP)                         |
| IS_NOT_NEGATIVE           | UNIMPLEMENTED (NOOP)                         |

The `bool(...)` results are boolean Terms, which no opcode -- including
these same logical opcodes -- accepts as input.  Until 8.2 is resolved,
express conditionals with `SIGN` / `COMPARE` (below), whose outputs are
integers.

### 5.6 Arithmetic and bit operations (operands and results are integers)

| opcode         | effect                                                   |
|----------------|----------------------------------------------------------|
| ADD            | ( a b -- a+b )                                           |
| SUBTRACT       | ( a b -- a-b )                                           |
| NEGATE         | ( a -- -a )                                              |
| ABS            | ( a -- \|a\| )                                           |
| SIGN           | ( a -- -1/0/+1 )                                         |
| COMPARE        | ( a b -- +1 if a<b, 0 if a=b, -1 if a>b ) -- note the inverted convention, 8.4 |
| BITWISE_NOT    | ( a -- ~a )                                              |
| BITWISE_AND    | ( a b -- a&b )                                           |
| BITWISE_OR     | ( a b -- a\|b )                                          |
| BITWISE_XOR    | ( a b -- a^b )                                           |
| BITWISE_SPLIT  | ( a b -- a&b a^b ) partition of the set bits             |
| SHIFT_RIGHT    | ( a b -- a>>b ) arithmetic shift; b < 0 or b > 63 is UB (8.6) |
| POPCOUNT       | ( a -- popcount of the 64-bit two's-complement pattern ) |

There is no MULTIPLY, DIVIDE, MODULO, or SHIFT_LEFT (8.10).
BITWISE_SPLIT is the bit-conserving decomposition (intersection and
symmetric difference); together with AND/XOR it is the half-adder.


## 6. Idioms

The examples use compass conventions (y increases northward/up) and this
legend for cell glyphs:

    .  empty        R TURN_RIGHT   L TURN_LEFT     U TURN_BACK
    ^v<> TURN_N/S/W/E (absolute)   B BRANCH_RIGHT  b BRANCH_LEFT
    G LOAD          S STORE        X EXCHANGE      K SKIP
    D DROP          d DUPLICATE    s SIGN          F FLIP_FLOP
    H HALT          digits are literal values on the ground

### 6.1 Literals and tracks

Numbers on the ground are both literals and scenery: a machine that drives
over `5` pushes a copy of 5 and the cell keeps its 5.  A program's
"constant pool" is just numbers placed along its track.  The corollary: a
machine *cannot* drive over a number without picking it up, except behind
a `K` (SKIP).  Route tracks around data you do not want, or SKIP it.

### 6.2 The loop

Four TURN_RIGHTs make a clockwise patrol; any closed circuit of turns is
an unconditional loop:

    R . . R
    .     .
    R . . R

### 6.3 The counted loop

Keep the counter on the stack; each lap decrement it, duplicate, take the
sign, and branch on it.  BRANCH_RIGHT by +1 turns right (stay in the
loop); by 0 goes straight (exit).

    R 1 - d s B . . . exit (east)
    .         .
    .         .
    N . . . . R
    ^
    5                 <- entry: drive north over the count
    ^
    start, heading north

`-` is SUBTRACT here.  The machine enters with [5]; each lap costs one; on
the lap where the counter reaches 0 the `B` sees sign 0 and lets it run
straight off the loop, counter (now 0) still on the stack.  Note the entry
merges through the absolute `N` at the corner: loop traffic arriving
westbound turns north there, entry traffic arriving northbound passes over
it unchanged.  Absolute turns are self-normalizing merge points.

### 6.4 The computed switch, and subroutines by geometry

`BRANCH_RIGHT` consuming a value in 0..3 is a four-way dispatch.  This is
also the return statement: there are no calls, but a shared stretch of
track is a subroutine.  Each caller pushes a *return code* (how many
quarter-turns to take at the shared exit) before merging into the shared
track; the track ends in `B`, which consumes the code and sends each
machine back its own way.  Calling convention: the return code must be on
top of the stack when the machine reaches the exit, so the shared code
must have net stack effect zero above it.

### 6.5 Signals: machines talking through the ground

A cell's value is a mailbox.  A producer drives a track that STOREs a
direction code into cell W; a consumer's track crosses W, auto-picks the
code (a copy -- the signal stays until overwritten), and branches on it:

              P  (southbound; the S glyph is north of W)
              S
    C > > > > W > B . .
              .
              exit for P

Occupancy arbitrates the shared cell W: P and C cannot be on it at once,
and the transaction system serializes same-instant access.  There is no
data race, only geometry and timing.

### 6.6 The alternator

A FLIP_FLOP splits a stream of machines alternately right and left -- a
balancer, a turnstile, a round-robin scheduler in one glyph.  Because it
rewrites itself on the ground, its state is shared by all machines and
survives any of them.

### 6.7 Gates and mailboxes

`HALT` parks a machine until *the cell under it* changes.  Nothing another
machine can do wakes it (machines cannot write under an occupant -- STORE
targets the cell the storer itself enters, and entry is blocked while the
sleeper occupies it).  A halted machine is therefore a valve operated by
non-machine entities: a Source or Sink under it, or the player editing the
cell.  Write `TURN_EAST` under a sleeper and it leaves eastward, executing
the glyph as it wakes.

For machine-to-machine gating, use occupancy itself: a parked machine *is*
the gate token, and the queue behind it is the wait set.

### 6.8 Matter logistics

`LOAD` takes a container (the cell empties), `DROP` puts the carried one
down in the next empty cell ahead (waiting for it to empty if needed),
`EXCHANGE` swaps cargo with the ground, `STORE` places it but only into
emptiness.  A Source under a pile cell refills it; a Sink consumes
whatever is placed on its cell.  Note that a machine can carry any number
of items (the stack is unbounded) and that matter blocks nothing -- it
lives on the value plane; machines drive over it freely (through the
stack of containers, as it were).

### 6.9 Crossings

Two tracks may cross on an empty cell with no interaction beyond
occasionally queueing on occupancy.  If the crossing cell carries one
track's data or glyph, the other track protects itself with a SKIP laid
on its own approach cell.


## 7. Turing completeness, honestly

Two observations temper the obvious claim:

- The stack ALU is *not* an unbounded-integer machine: past +/- 2^59,
  arithmetic results silently box to heap integers that every subsequent
  opcode refuses (8.5).  The stack-plus-ALU fragment alone is finite-state
  in practice.
- The grid, however, is unbounded, and `EXCHANGE` with an empty stack is
  "pick up and clear" while `STORE` is "put down": a machine can shuttle a
  marker value along an unbounded row one cell at a time, and can test
  where it is by driving over sentinel values and branching.

So a single machine can realize a two-counter register machine with each
counter represented as the distance of a marker from a home cell:
increment and decrement are EXCHANGE-carry-STORE shuttles, zero-test is
"the value I just drove over is the home sentinel, branch on it", and the
finite control is a fixed circuit of branch glyphs.  Two-counter machines
are Turing complete; therefore so is one machine on an unbounded grid --
through the grid, not through the ALU.  (With the small-integer cliff
fixed, the much more direct construction "two counters live on the stack,
SWAP reaches both, SIGN+BRANCH tests them" also works, and is a nice
argument for why SWAP alone -- without ROT -- is just barely enough.)


## 8. Rough edges, contradictions, and missing pieces

Ordered roughly by severity.

### 8.1 BUG: HEADING_LOAD and HEADING_STORE have crossed wires

In machine.cpp the heading-resolution switch handles `OPCODE_HEADING_LOAD`
by setting the heading from the top of stack (without popping), while the
action switch pushes the old heading under `HEADING_LOAD` and pops the top
under `HEADING_STORE` -- which never touches the heading at all.  Net
effect as shipped:

- `HEADING_STORE` ( h -- ) : discards h, heading unchanged.  Broken.
- `HEADING_LOAD` ( h -- h old ) : sets heading to h, which *stays* on the
  stack, and pushes the previous heading on top of it.  An accidental
  exchange-with-dregs.

Clearly the heading-resolution case is mislabeled: it belongs to
`HEADING_STORE` (matching the cell-op convention where LOAD means
register-to-stack).  Intended and recommended semantics:

- `HEADING_LOAD` ( -- h ) : push current heading.
- `HEADING_STORE` ( h -- ) : pop h, heading := h.

### 8.2 CONTRADICTION: predicates produce values nothing can consume

All eleven implemented comparison and logical opcodes compute a C++ `bool`
and assign it to a Term, which selects the `Term(bool)` constructor and
produces a *boolean* Term.  But every consumer in the machine -- the
`BRANCH_*` steering, the logical opcodes themselves, all arithmetic, and
auto-pickup -- requires `is_int64_t()`, which booleans fail.  Therefore:

- `EQUAL` followed by `BRANCH_RIGHT` does not branch.
- `LESS_THAN` twice followed by `LOGICAL_AND` computes nothing: logic
  cannot consume its own output.
- A stored boolean on the ground is invisible to passing machines.

The type system has a schism: the predicate sublanguage and the control
sublanguage do not compose.  Today the only working conditionals are
`SIGN` and `COMPARE`, whose bool-minus-bool arithmetic yields genuine
integers.  Two coherent fixes:

1. Predicates push small integers 0/1 (change `a = ...bool...` to push
   `Term(int(...))`).  Keeps consumers simple; booleans stop appearing in
   machine programs entirely.
2. Consumers accept booleans (BRANCH treats true as 1, logic ops accept
   mixed bool/int via the falsey rule).  Keeps the boolean type visible.

Option 1 is smaller and matches the "heading codes are just integers"
spirit.  Either way, decide; the current halfway state is the single
biggest usability defect in the language.

### 8.3 Unimplemented opcodes execute as silent no-ops

`IS_ZERO`, `IS_POSITIVE`, `IS_NEGATIVE`, `IS_NOT_POSITIVE`,
`IS_NOT_NEGATIVE`, `LOCATION_LOAD`, and `LOCATION_STORE` are declared in
opcode.hpp but have no case anywhere in machine.cpp: placing the glyph
does nothing at all, indistinguishable from NOOP.  Notes:

- The IS_ family is largely redundant: `IS_ZERO` is `LOGICAL_NOT`,
  `IS_POSITIVE` / `IS_NEGATIVE` are recoverable from `SIGN`, and once 8.2
  is fixed the comparisons cover the rest.  Either implement them as
  integer-producing predicates for convenience or delete them; a glyph
  that looks meaningful and does nothing is a trap.
- `LOCATION_LOAD` is blocked on the parked Coordinate-as-Term encoding
  (Morton-60 design); fine to leave pending, but it should probably HALT
  or refuse visibly rather than silently no-op.
- `LOCATION_STORE` as named would be teleportation, which contradicts the
  founding constraint (no random access; machines move only to
  neighbors).  Recommend deleting it rather than implementing it.  If a
  location *value* is ever needed for comparison ("am I home?"), that is
  LOCATION_LOAD plus EQUAL, and no store is required.

### 8.4 COMPARE's sign convention is inverted relative to SUBTRACT

`COMPARE` computes `(a < b) - (b < a)`, i.e. sign(b - a): +1 when the
*deeper* operand is smaller.  The C family convention (memcmp, strcmp,
three-way compare) is sign(a - b).  Within this language, `SUBTRACT` then
`SIGN` computes sign(a - b) = the negation of `COMPARE` on the same
operands.  Having the fused opcode disagree with its own decomposition
will produce persistent off-by-sign bugs in user programs.  Recommend
flipping COMPARE to sign(a - b) (save-format-neutral; it is a behavior
change only).  The stray `(i64)` cast on one side of the comparison is
harmless asymmetry worth cleaning while there.

### 8.5 The 2^59 cliff: overflow makes values inert, silently

Small integers are 60-bit.  `ADD`, `SUBTRACT`, `NEGATE`, and `ABS` compute
in int64 and re-box via `term_make_integer_with`, which spills results
outside [-2^59, 2^59) into heap-allocated `HeapInt64` -- and every machine
opcode tests `is_int64_t()`, which is false for heap integers.  So the
first overflow produces a value that all subsequent arithmetic,
comparison, branching, and auto-pickup silently ignore.  The program does
not wrap, does not fault; it just goes numb around that value.  (At the
full int64 boundary the addition itself is also UB.)  Options: saturate,
wrap explicitly at 60 bits, or teach the ALU to unbox HeapInt64 (the
accessors exist).  Any of the three is better than the silent cliff.
Related: auto-pickup ignores heap integers on the ground, but LOAD happily
picks them up as inert cargo.

### 8.6 Smaller sharp edges

- `SHIFT_RIGHT` with a negative or >= 64 shift count is C++ UB in the
  interpreter.  Clamp or mask; and see 8.10 for the missing left shift.
- The stored heading accumulates unmasked (TURN_RIGHT is ++, flip-flops
  increment forever); it is reduced mod 4 at stepping time, which is
  correct, and the visualization needs the winding for its lerp -- but a
  (fixed) HEADING_LOAD would push the raw winding number, so a program
  comparing a heading against a literal 0..3 breaks after any full
  rotation.  Canonicalize (mask to 0..3) at the HEADING_LOAD boundary,
  keeping the internal winding for animation.
- `ABS`/`NEGATE` of INT64_MIN is UB in principle; unreachable-ish given
  8.5 boxes first at 2^59, but the same cleanup pass can settle it.
- `POPCOUNT` counts the 64-bit two's-complement pattern, so popcount of a
  negative number is 60 bits of value plus 4 bits of sign extension.
  Defensible, but worth documenting as "of the representation".

### 8.7 Matter is opaque to control flow

No opcode can test what a machine is carrying or what lies in a cell:
`EQUAL` requires integers, `BRANCH` requires integers, and there is no
IS_MATTER.  Consequence: with more than one matter kind, *a sorter is
inexpressible* -- machines cannot route cargo by kind, which is presumably
a core gameplay verb.  Cheapest coherent fix: make `EQUAL` / `NOT_EQUAL`
total by delegating to `term_eq` (which already exists and is total) and
pushing an integer 0/1.  Then a reference glyph or matter sample on the
ground (LOADed as data) becomes a comparison standard: LOAD sample, test
cargo, branch.  This composes with 8.2's fix.

### 8.8 EXCHANGE can place matter onto a non-empty cell

The stated rule (matter.hpp, and the STORE guard's comment) is that matter
may only be placed into an empty cell, never over a program glyph.  But
EXCHANGE has no guard at all: with matter on top of the stack and a glyph
in the cell, it swaps them -- the glyph goes to the stack and the matter
now sits where code was.  Nothing is destroyed (the swap conserves both),
so this may be acceptable or even desirable; but it contradicts the
never-over-a-glyph half of the stated rule.  Decide which is canon: if
placement-only-into-emptiness is the physical law, EXCHANGE needs the
guard when its stack side is matter; if conservation is the only law,
soften the comments.

### 8.9 STORE with an empty stack erases the target cell

Pop of an empty stack yields null, and STORE writes it: a machine with
nothing to give wipes the operand cell (of information; the matter guard
still protects matter).  Handy as an eraser, alarming as an accident --
an under-provisioned producer silently deletes the very signal cell it
was meant to feed.  Worth a deliberate decision: either document it as
the eraser idiom, or make empty-stack STORE a no-op (park-and-wait would
also be coherent: "wait until I have something to store").

### 8.10 Missing operations, ranked

1. **ROT** (or PICK): the stack below the top two elements is unreachable
   without destroying it.  {DUP, DROP, SWAP, OVER} cannot express ROT --
   that is exactly why Forth makes it primitive.  Every third live value
   forces a spill to the ground, which costs cells, hops, and 64 ticks
   each.  One opcode buys a lot of program density.
2. **MULTIPLY / DIVIDE / MODULO**: absent, though Term already carries
   `operator*` / `/` / `%` machinery.  Synthesizing multiplication from
   ADD and SHIFT_RIGHT via shift-and-add is a genuinely large 2D circuit.
   This might be deliberate game design (make the player build the
   multiplier factory!); if so, say so loudly in the design docs, because
   as a language omission it reads as an oversight.  MODULO in particular
   is the natural "extract a digit / a field" tool for signal encoding.
3. **SHIFT_LEFT**: right shift exists, left does not; doubling via
   DUPLICATE+ADD forces a loop per bit.  Cheap to add, pairs with fixing
   8.6's UB.
4. **WAIT n** (sleep for a duration): the transaction layer already has
   `on_commit_sleep_for`; exposing it would give programs timing control.
   Today the only temporal primitives are the 64-tick hop and unbounded
   blocking, so phase-offsetting two machines requires track-length
   gymnastics.
5. **Sensing** (IS_MATTER on top-of-stack, or a "test the cell ahead"
   predicate): both are local and would not breach the no-random-access
   principle; they would let programs avoid blocking rather than only
   experience it.  See 8.7 and 8.13.
6. Conveniences that can wait: NIP/TUCK (sugar over SWAP/DROP/OVER),
   DEPTH, MAX/MIN (COMPARE+BRANCH geometry covers them).

Non-goals, endorsed as such: no jump, no address arithmetic, no random
number source (determinism), no I/O opcodes (the player and the
Source/Sink/Spawner entities are the I/O), no machine-spawns-machine
opcode yet (a von Neumann constructor would be a lovely late-game
artifact, and the Spawner entity shows the seam where it would go).

### 8.11 The stack access horizon

Related to 8.10.1 but worth stating as a language property: with top-2
access only, the stack is a *pushdown store with a two-slot window*, and
the grid is the real random-access memory (at 64 ticks per cell of
distance).  This is a coherent design -- it pushes complexity out into
space, which is the game -- but it means "register allocation" in this
language is literally town planning.  Decide whether that is the intended
difficulty knob before adding ROT; adding it later is save-compatible
(append-only opcode list), so the cautious default is to withhold it and
watch what players build.

### 8.12 Interpreter hygiene (behavior-preserving today, landmines tomorrow)

- Four sites read the *pre-transaction* stack: the three `peek()` calls in
  heading resolution (BRANCH_LEFT, BRANCH_RIGHT, HEADING_LOAD) and one in
  POPCOUNT, all on `this` where every neighboring case uses
  `new_this->peek()`.  Today this cannot diverge -- when an opcode
  executes, the pending-op stage never pushed (executing implies the cell
  held an opcode, so auto-pickup had nothing to grab) -- but the proof is
  three switches apart, and any future opcode that both pushes in the
  pending stage and executes would quietly read stale state.  Normalize to
  `new_this`.
- The interleaving of the two big switches (heading resolution peeks,
  action effects pop) means BRANCH's operand is examined in one place and
  consumed in another, with the no-pop-if-not-integer rule duplicated.  A
  future refactor to single-switch-per-opcode would remove the class of
  bug that produced 8.1.
- `abs()` on i64 resolves through the C library overload set; spell it
  `std::abs` (or the sign trick used by SIGN) to be immune to header
  drift.

### 8.13 Deadlock is expressible and unrecoverable in-language

Two machines approaching head-on each wait for the other's cell forever;
so do four machines in a rotational cycle.  The escape hatches (a blocked
machine also wakes if the value under it changes) require an *external*
actor -- the player, or an entity like a Source -- because machines cannot
write under other machines.  There is no timeout, no arbitration, no
in-language detection.  For a game this is arguably content: gridlock is
real, one-way loops and flip-flop balancers are the players' traffic
engineering; the priority-randomized transaction layer already guarantees
the *simulation* never wedges, only the players' programs.  Two cheap
mitigations if wanted later: a sensing predicate (8.10.5) so programs can
test-and-turn instead of committing to a blocked entry, and a WAIT with
timeout semantics.  Neither compromises determinism.

### 8.14 Small semantic asymmetries to either bless or sand off

- LOAD copies information but moves matter; EXCHANGE moves both ways;
  auto-pickup copies integers only.  Three different acquisition
  semantics, each individually sensible; document them as the trio
  (copy-read, swap, take) and keep them stable.
- SKIP protects exactly one cell.  A long foreign data run needs a SKIP
  per cell.  Acceptable at game scale; a SKIP-with-count would change the
  two-cell pipeline shape, so is probably not worth it.
- A machine woken from HALT executes whatever the cell now holds,
  including auto-picking a number and driving on.  Powerful (a writable
  "next instruction" slot under a sleeping machine) and slightly spooky
  (a stray write launches it).  The machine.cpp TODO asking whether the
  top of stack itself should serve as the instruction latch points at the
  same design nerve.

--------------------------------------------------------------------------

## 9. Tarpit assessment

The fear was that locality makes the language a Turing tarpit.  Verdict:
the locality is the *good* part; the current pain is concentrated in
fixable seams.

What locality actually costs: control flow consumes area (a conditional
is a junction, a loop is a circuit); code reuse is physical travel
(subroutines are shared track plus a return-direction code); deep data
is far away (64 ticks per cell).  These are exactly the costs the game
wants the player to feel, and the mitigation idioms -- ground spills,
signal cells, absolute-turn merges, flip-flop balancers, occupancy
queues -- are the gameplay.

What is tarpit-flavored but *not* inherent: predicates that cannot feed
branches (8.2), registers that cannot be set (8.1), glyphs that silently
do nothing (8.3), matter that cannot be sorted (8.7), arithmetic that
goes numb at 2^59 (8.5), and a stack whose third element might as well
be on the moon (8.10.1).  Fix the first five and the language is a
pleasant, teachable spatial Forth; leave them and every nontrivial
program is built from the two opcodes that happen to type-check.

Compared to relatives: Befunge is easier only because it cheats -- its
`p`/`g` give random-access grid writes from anywhere, which this design
rightly refuses.  Turmites/Langton ants sit far below: no stack, no
ALU, no addressing.  This language occupies a genuinely interesting
middle: a concurrent, transactional, conservation-respecting Forth
where the program counter is a truck.
