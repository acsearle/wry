# Coroutines, senders and the reactor

## Purpose

This is the design of record for the coroutine library: `Promise`,
`Delegate` and `Future` in `ext/coroutine.hpp`, the sender seam in
`ext/execution.hpp`, the `Nursery` in `core/nursery.hpp`, and the leaves
that park frames on something outside the process's control -- the kqueue
reactor (`core/kqueue_reactor.{hpp,cpp}`), the Metal event listener
(`render/metal_event.{hpp,mm}`), and the in-process events and queues
(`core/single_consumer_event.hpp`, `container/mpsc_queue.hpp`).  Until now
these rules lived in header comments and in chat; the comments are still
the authority on detail, and every claim below names the function it is
about so it can be checked against them.

The library is small and opinionated.  There is one coroutine type,
`Future<T>` (`Task` is `Future<void>`); one executor, the global work queue;
one cancellation mechanism, `std::stop_token`; and one way to cancel a
suspended frame: destroy it.  Read section 1 for the vocabulary, then the
protocol sections in any order; section 11 is the checklist for anyone
adding a leaf awaitable.

The concurrency claims use the vocabulary of `core/docs/garbage_collection.md`
section 1: an edge is named by its release (or RMW) and the acquire (or RMW)
that reads from it.  `Atomic<T>` (`ext/atomic.hpp`) spells the ordering into
the method name (`exchange_acq_rel`, `fetch_sub_release`, `load_acquire`,
...).

## 1. Concepts

### 1.1 Frames and the two words

Every schedulable thing is reached by a pointer whose first two words are a
`coroutine::Header { resume, destroy }` -- the layout clang uses for its
coroutine frames.  The work queue (`global_work_queue_schedule(void*)`,
`core/global_work_queue.hpp`) calls word 0 and nothing else.  This lets
non-coroutine objects pose as frames ("fake frames"):

| fake frame | word 0 (resume) | word 1 (destroy) |
|---|---|---|
| `Nursery` (`_header`) | `_static_resume`: one child retired | `_static_destroy`: one child retired, cancelled |
| `FutureOperationState::Frame` | `_static_resume`: value or error to the receiver | `_static_destroy`: stopped to the receiver |
| `CancelableKEventFrame` | `static_resume`: the knote fired | `nullptr` (never called) |
| `sync_wait`'s `Starter`, `ArrivalOperationState` (test) | start the operation | `abort` |
| `DestroyThunk` (`global_work_queue_schedule_destroy_by_address`, `ext/coroutine.cpp`) | destroy the target | same |

A delegate that names a fake frame as its continuation (section 2) makes
completion and cancellation arrive through word 0 and word 1 respectively;
the test fence `UnwindFence` (`ext/coroutine_test.hpp`) is the minimal
instance.

### 1.2 Pinned threads and foreign threads

The pool workers (`global_work_queue_service`, `core/global_work_queue.cpp`)
are GC mutators: each pins (`mutator_pin`) around a batch of at most
`GLOBAL_WORK_QUEUE_REPIN_CADENCE` (10) tasks and unpins between batches.
Coroutine frames own GC state (`Root<T>` parameters and locals) whose
destructors shade, and a shade on an unpinned thread is the bug that
`_debug_note_unpinned` (`core/garbage_collected.cpp`) now asserts on.  So:

- Frames are resumed and destroyed only on pinned mutator threads: a pool
  worker, or a thread that has pinned itself (main, around
  `root.request_stop()` in `core/main.mm`).
- The reactor thread (`reactor_run`, named `R0`) and the Metal listener's
  dispatch queue (`shared_event_listener`, label `wry.metal-event`) are
  FOREIGN: they never resume or destroy a frame, they only hand pointers to
  the pool.
- `TransferToBlockableExecutor` deliberately moves a frame onto a GCD
  global-queue thread, which is not a mutator.  The one user,
  `background_save_coroutine` (`io/save.cpp`), hops back with
  `TransferToPoolExecutor` before anything that could shade, and parks on
  no cancellable leaf while away.  That excursion is the exception, and
  anyone copying it inherits the obligation.

The work queue is a mutex-protected `BlockingDeque<void*>`
(`container/concurrent_queue.hpp`), pushed and popped at the back, so it
is LIFO.  Its mutex is the publishing edge for every schedule: everything
the scheduling thread did before `push_back` happens-before the worker's
call of word 0.  Most protocols below lean on that edge implicitly.

### 1.3 Promise, delegate, Future

`Promise<T>` (via `BasicPromise<T>` and `MixinPromise<T>`) owns nothing
about where its result goes.  Every post-connection decision is forwarded
to a `Delegate<T>*`, set exactly once with `set_delegate` (a second set
aborts in Debug and names both delegate types):

| promise event | delegate hook |
|---|---|
| `co_return v` / `co_return` | `return_value(T)` / `return_void()` |
| `unhandled_exception()` | `unhandled_exception()` (records only; final suspend still follows) |
| final suspend (`FinalAwaitable::await_suspend`) | `final_await_suspend()` -> the continuation |
| `~BasicPromise` with the delegate still set | `unhandled_stopped()` |
| `get_stop_token(promise)` | `get_stop_token()` |

`Future<T>` is a move-only owner of a not-yet-started frame
(`initial_suspend` is `suspend_always`).  A Future must be consumed --
awaited, forked, started as a sender, or handed to `handle_from_future` --
or `~Future` aborts.  There is no detached spawn: every frame has a delegate
that waits for it.

`co_await x` inside a Promise goes through `await_transform_helper`: first
the `transform_await(promise, x)` customization point (Future, Outcome,
`GetPromise`, `GetStopToken`, `WithDeadlineSender`), then `x` as an awaitable,
then `x` as a sender wrapped in `SenderAwaitable`; anything else is a
`static_assert`.

### 1.4 Three channels and the Outcome

Every operation ends in exactly one of value, error (`std::exception_ptr`)
or stopped.  `execution::Outcome<T>` reifies the result as a variant
`{empty, value, error, stopped}`; the sink verbs `set_value` / `set_error`
/ `set_stopped` on an Outcome abort unless it is empty, and `value()`,
`value_or()` and `stopped_as_optional()` consume it back to empty, so a
double take aborts rather than yielding a moved-from value.  `co_await` of
an Outcome (`OutcomeAwaitable`) returns the value, rethrows the error, or
destroys the awaiting frame for stopped: a stopped result continues the
cancellation (test `nursery_outcome_rethrow` covers the error path).

### 1.5 Senders

The sender seam (`ext/execution.hpp`, "Sender concepts") is a reduced
std::execution: `SenderTraits<S>::value_type`, `connect(S&&, R&&)` returning
an IMMOVABLE operation state constructed where it will live, `start(op&)`,
and exactly one completion call on the receiver.  `get_stop_token(R const&)`
is the receiver's whole environment.  A Future is a sender
(`FutureOperationState`, section 6) and a sender is awaitable
(`SenderAwaitable`, section 5).  Algorithms: `just`, `stopped_as_optional`
and `shield` (receiver adaptors), `when_any` (the race, section 7.2) and
`sync_wait` (section 7.3).

### 1.6 Cancellation is destruction

A suspended frame is cancelled by destroying it, never by resuming it with
an error.  The destroy runs the frame's live locals' destructors and then
`~BasicPromise`, which finds the delegate still set and calls
`unhandled_stopped()`, which destroys or notifies the frame's awaiter, whose
`~BasicPromise` does the same, and so on up the chain until a delegate
absorbs the stop (an adaptor, a Nursery under an unrequested outer token)
or a root records it.  Test `coroutine_unwind_chain` (ext/coroutine.cpp)
pins the shape: three frames, each probe destroyed exactly once, no code
after any `co_await` runs.

Nothing in the chain polls.  Only leaves -- the awaitables that actually
park a frame -- look at the stop token, and they do so by registering a
`std::stop_callback` that destroys the parked frame.  `SuspendAndCancel` is
the degenerate leaf: it destroys its own frame unconditionally.

## 2. The delegate protocol

### 2.1 Ask for the continuation before the destroy

`BasicPromise::FinalAwaitable::await_suspend` does, in order:

1. `take(_delegate)`, so the coming `~BasicPromise` does not report a stop;
2. `continuation = delegate->final_await_suspend()`;
3. `handle.destroy()`;
4. `return continuation` (symmetric transfer).

The order of 2 and 3 is load-bearing because a delegate may be PARKED in
the frame being destroyed (`BasicPromise::_delegate_storage`, section 2.3).
Before 5dd19b5 (2026-09-11) the code destroyed first and then called
`delegate->final_await_suspend()`; for a nursery child with an Outcome
target that is a virtual call through freed frame memory.

The hook only NAMES the continuation.  Completion's side effects -- a
nursery's retire, a receiver's `set_value` -- belong to that continuation's
word 0, which runs after step 3, when the child's frame (its parameter
copies included) is gone.  See section 8.2 for why the Nursery relies on
that, and 2.4 for the stopped path, where it does not hold.

The value itself has already been delivered by `return_value` /
`return_void` into whatever Outcome the delegate names
(`OutcomeDelegate<Derived, T>` routes both, and `unhandled_exception`, into
`Derived::outcome()`).  Those are plain writes; they are published to the
party that reads the Outcome by whatever edge carries the continuation
(symmetric transfer on one thread, the Nursery's counter, the
`SenderAwaitable` exchange, a semaphore).

### 2.2 The stopped hook is the destroy word and must forward

`unhandled_stopped()` is called from `~BasicPromise` -- that is, from inside
the frame's destruction.  It is the stopped channel's only signal, so every
delegate must forward it to whoever is waiting, or that party waits forever.
`Delegate`'s default aborts, which is the right default: a delegate that
cannot forward should not silently drop.  The forwarding delegates on main:

| delegate | forwards stopped by |
|---|---|
| `FutureAwaitable` | `set_stopped(_outcome)`, then `destroy()` the outer frame (continues the cascade) |
| `FutureOperationState` | `set_stopped(_frame._outcome)`, then `Frame::_static_destroy` -> `set_stopped(receiver)` |
| `Nursery::Inner` | `Nursery::_static_destroy` (count it cancelled, retire) |
| `NurseryChildDelegate<T>` | `set_stopped(*_target)`, then `Nursery::_static_destroy` |
| `WithDeadlineAwaitable` | `set_stopped(_outcome)`, then destroy the outer if the outer's token is requested, else resume it (absorb) |
| `UnwindFence` (test) | records 2; it is a root, nothing above it |
| `NurseryChildValueDelegate<T>`, `Delegate` default | abort (a bald target has no stopped channel) |

`OutcomeDelegate` deliberately leaves `unhandled_stopped` to `Derived`:
what "forward" means differs per delegate (the comment on
`BasicOutcomeDelegate`).

### 2.3 No virtual destructor; parked delegates are trivially destructible

A delegate is never owned through `Delegate<T>*`.  It lives in an awaitable
in the outer frame (`FutureAwaitable`, `WithDeadlineAwaitable`), an
operation state (`FutureOperationState`), a member of a longer-lived object
(`Nursery::_inner`), a test local (`UnwindFence`), or the child promise's
own parking space.  So `Delegate` has no virtual destructor and nobody
deletes through it.

The parking space is `BasicPromise::_delegate_storage`, three pointers wide,
used by `BasicPromise::emplace_delegate<D>` for a delegate whose only home of
the right lifetime is the child itself (`NurseryChildDelegate`,
`NurseryChildValueDelegate`: the fork awaitable is gone once the child is
running).  `emplace_delegate` `static_assert`s size, alignment and
`std::is_trivially_destructible_v<D>`: the storage is a byte array that
nothing ever destroys, so a parked delegate must have nothing to destroy.
(Three words is a vptr plus two pointers, exactly `NurseryChildDelegate`;
a delegate holding a `std::stop_token`, say, is not trivially destructible
and cannot be parked.)

### 2.4 Where the stopped hook runs, and what is still alive

Because the hook runs inside `~BasicPromise`, everything the hook does --
destroying the awaiter, retiring a nursery, resuming an absorbing awaiter
-- runs NESTED inside the dying frame's destruction:

- the frame's locals are already destroyed (they die before the promise);
- its parameter copies are NOT yet destroyed (their lifetime ends
  immediately after the promise's) and its storage is not yet freed;
- the native stack grows by one destroy per level of the cascade, and an
  absorbing delegate (`WithDeadlineAwaitable`, a Nursery whose joiner
  resumes) runs the awaiter's code on top of that stack until the awaiter
  next suspends.

This is the asymmetry with section 2.1: on the value path the continuation
runs after the frame is gone; on the stopped path it runs before the
frame's parameter copies die.  A cancellable coroutine must therefore not
have a parameter whose destructor touches state that its awaiter or joiner
may free on resumption.  The tests put their probes in body locals for
nursery children (`UnwindProbe`) for this reason among others; the reactor
tests put theirs in a callable (`CancelableProbe`) because a never-started
frame has only parameters.  (Listed as an open question in section 12.)

## 3. Cancellation

### 3.1 Tokens flow down through delegates

A frame's token is `delegate->get_stop_token()` (`co_await GetStopToken{}`
yields it).  Each delegate chooses: `FutureAwaitable` forwards the outer
promise's; `FutureOperationState` the receiver's environment; the Nursery
hands its children its interior source's token; `WithDeadlineAwaitable` its
interior source's; `StoppedAsOptionalReceiver` the outer's or, for
`shield`, the caller's (default: a detached token nobody can request).  The
lookup walks the delegate chain to the root on every leaf await (open item
in docs/todo.md: cache it in the promise if depth grows).

Interior sources are joined to the outer token by a bridge, a
`std::stop_callback` on the outer token whose callback requests the
interior source: `Nursery::Callback` (registered in the Nursery
constructor), `RaceBase::Bridge` (registered in `start` of the race),
`WithDeadlineAwaitable::Callback` (registered in `await_suspend`), and
`Bridge` in `io/transport.cpp`.  Registration fires the callback inline if
the outer is already requested, which is why `execution.hpp` says
registering a callback "belongs in start".

### 3.2 The pinned-requester contract

`std::stop_source::request_stop` runs the registered callbacks
synchronously on the requesting thread, and a leaf's callback destroys the
parked frame there (`CancelableKEventFrame::Callback` via `release` and
`~ContinuationThunk`; `WaitState::Callback`; `SingleConsumerEvent::Callback`;
the MPSC queue's `BasicAwaitable::Callback`).  The destroy cascade of
section 1.6 then runs frame destructors, which shade.  Hence:

> Whoever requests a token that a parked chain can observe must be a pinned
> mutator thread, for the duration of the call.

The cases on main: pool workers (pinned while running tasks; timer
callables from `cancelable_after` run there); main, which calls
`root.request_stop()` while still pinned and only then `mutator_unpin()`
(`core/main.mm`, headless and GUI paths).  Foreign threads never request: the reactor only schedules,
and the Metal block only resets a `stop_callback` (section 10.3), which can
block but never destroys.

The destroy is not always on the requester.  In the reactor's fire-won
window (section 9.3) and its arming window, the frame's last reference is
dropped by the pool's `static_resume` or by `spawn` on the awaiting thread,
after `request_stop` has returned.  Both are pinned too, so the contract is
"every party that can drop the last reference is pinned", and the requester
is one of them.

### 3.3 The request_stop keepalive rule

A bridge callback requests an interior source that is a member of the very
object the cascade may destroy: the children it unwinds can retire the
nursery or the race, whose last retire resumes or destroys the owner frame,
which destroys the Nursery (or race state, or `WithDeadlineAwaitable`) --
and with it the `std::stop_callback` that is executing, its functor, and
the `std::stop_source` the functor holds.  The standard permits destroying
a `stop_callback` from inside its own callback on the same thread (the
destructor does not wait in that case), but the source object the callback
is iterating through would be gone.

> A callback that requests a stop source first copies (or moves) the
> source into a local, and requests through the local.

The local shares the stop state, so the state outlives the iteration even
when every other owner dies under it.  Instances: `Nursery::Callback`
(copy), `RaceBase::Bridge` (move), `WithDeadlineAwaitable::Callback` (move),
the timer callable in `WithDeadlineAwaitable::await_suspend` (copy of a
copy), the `Bridge` and kick-grace timers in `io/transport.cpp` (copy).

The concrete interleaving (the comment on `Nursery::Callback`): a child
parked in `recv_some` under a Nursery whose outer token is requested.  The
outer `request_stop` runs `Nursery::Callback`, which requests
`_inner_stop_source`; that runs the leaf's `CancelableKEventFrame::Callback`,
which destroys the child frame; `~BasicPromise` calls
`Nursery::_static_destroy`, the last retire sees the outer requested and
destroys the joiner frame, which holds the Nursery.  Without the local, the
inner source's iteration continues over a freed `stop_source`.

`RaceBase::hasten` is the exception that proves the rule: the winner
requests `_stop_source` directly, because the winner has not retired yet,
so `_live` cannot reach zero inside the call and the race state cannot die.

## 4. The last-touch rule

> A completion call (`set_value`, `set_error`, `set_stopped` on a
> receiver), a `start`, a schedule of a frame, and a resume or destroy of
> a frame are each the LAST touch, by the caller, of every object the
> callee may destroy.

The callee may destroy the operation state, the awaitable, the event, the
nursery, before the call returns, so the caller reads anything it still
needs into locals first and touches nothing afterwards.  The code marks
these sites "last touch":

- `start(just_operation_state_t&)`, `start(FutureOperationState&)` (the
  coroutine may run to completion inline), the race's `start` (the last
  entrant's start), `sync_wait`'s `Starter::_static_resume`;
- the receivers' completion functions (`set_value(AwaitableReceiver...)`
  and friends, `RaceReceiver`'s `retire`), `FutureOperationState::Frame`'s
  two words, `RaceBase::deliver`;
- `AwaitableBase::_continue_as_second` and `_receiver_arrives`;
- `Nursery::_static_resume`, which `take`s `_continuation` into a local and
  tail-calls (`[[clang::musttail]]`) the resume or destroy;
- `SingleConsumerEvent::set` (the exchange is the last touch of the event);
- every await_suspend that hands the frame to another party: after the
  hand-off ("no *this past here") the awaitable is in the frame and may be
  gone.

`RaceBase::retire` and `Nursery::_static_resume` show the shape: every
caller but the last decrements and returns; the last reads what it needs,
then makes one call.

## 5. Sender as awaitable: who continues the awaiting frame

`SenderAwaitable<OuterPromise, S>` connects `S` to an `AwaitableReceiver`
that points at its `AwaitableBase` (which breaks the awaitable ->
operation state -> receiver type cycle).  Two parties finish with it: the
receiver (once, from inside `start` or later on any pinned thread) and
`await_suspend` (once `start` returns).  Since 7b90f62 (2026-11-08) each
performs one `_arrived.exchange_acq_rel(true)`; the one that reads `true`
arrived second and continues the frame (`_continue_as_second`: resume for
value or error, destroy for stopped); the one that reads `false` touches
nothing of the awaitable afterwards.

| case | receiver | await_suspend |
|---|---|---|
| inline (Just, a race of Justs) | records, reads false, returns | reads true, returns the frame's handle: symmetric transfer back |
| async (a leaf parks) | later: reads true, resumes the frame nested in its own call | reads false, returns `noop_coroutine` |
| async racing start | either order in `_arrived`'s modification order | the other |
| stopped, second is await_suspend | records | `continuation.destroy()`, returns `noop_coroutine` |

Orderings: there are exactly two RMWs on `_arrived`, so the second reads the
first's value; the first is the release and the second the acquire of one
synchronizes-with edge.  Receiver first: the outcome's plain writes (and
the operation's work) happen-before the awaiting thread's read of
`_outcome` and `await_resume`.  `await_suspend` first: everything the
awaiting thread did before and during `start` happens-before the receiver's
resume of the frame on its own thread.  `_outer_promise` is read by
`get_stop_token(AwaitableReceiver)` while the operation runs and is taken
only by the second party, after the edge.

The motivating failure: before 7b90f62 the receiver resumed the frame
itself, so a completion inside `start` nested a resume inside `start`, and
a loop over inline senders grew the native stack by one resume per await
(about 2.9 KiB per await under ASan; a stack overflow at 768 awaits).
Tests: `coroutine_sender_inline_stack` (768 inline awaits, spread asserted
under 4096 bytes) and `coroutine_sender_arrival` (pool completions racing
`start`'s return, value and stopped, both orders).

`FutureAwaitable` (the direct path for `co_await future`) needs none of
this: the inner frame cannot complete before `await_suspend` returns it,
since `await_suspend` IS what starts it (it returns the inner handle), and
the inner's final suspend transfers back to the outer.

## 6. Future as a sender: the operation state is a fake frame

`FutureOperationState<T, R>` is both the child's delegate and, through its
`Frame` member, a fake frame whose two words split the channels:
`final_await_suspend` names `&_frame` as the continuation, so normal
completion arrives at `Frame::_static_resume` (value or error, by the
Outcome's index, to the receiver); `unhandled_stopped` calls
`Frame::_static_destroy` directly (stopped to the receiver).  Each word's
receiver call is its last touch of the state.  `start` takes the promise,
sets the delegate, and resumes the coroutine, which may run to completion
inline; tests `coroutine_sender_seam` and `coroutine_sender_seam_cancel`
(parked cancel crossing two seams synchronously inside `request_stop`,
cancel before park, normal completion on a worker).

## 7. Adaptors

### 7.1 stopped_as_optional and shield

Receiver adaptors with no operation state of their own
(`StoppedAsOptional`, `StoppedAsOptionalReceiver`): stopped becomes a value
`nullopt`, value becomes engaged, error passes through.  The inner's token
is the outer's (`stopped_as_optional`) or a caller-supplied one (`shield`,
default detached).  A shielded inner can still stop of its own accord, so
both yield `optional`.  A shield with a budget is
`shield(f, s.get_token())` plus a `cancelable_after` that requests `s`
(test `execution_stopped_as_optional`; the composed helper is unwritten for
want of a user).  The GUI test runner is `shield(run_tests(...))`
(`core/main.mm`), which is why tests see a detached token.

### 7.2 when_any (the race)

`RaceOperationState` holds the entrants' operation states by value
(`InPlaceTuple`, so each `connect` prvalue is elided into its slot), each
connected to a `RaceReceiver<T, R, I>` pointing at `RaceBase`.  The entrants
run under an interior `_stop_source`; `start` registers the `Bridge` from
the outer token, then starts every entrant.

- `claim(index)`: `_winner.compare_exchange_strong_relaxed_relaxed(-1,
  index)`.  Mutual exclusion only; it publishes nothing.
- The winner writes `_outcome`, calls `hasten()` (interior `request_stop`:
  parked losers unwind and retire inside this call), then `retire()`.  A
  stopped entrant only retires; a tie-loser's value or error is dropped.
- `retire()`: `_live.sub_fetch_release(1)`; the caller that sees zero does
  `_live.load_acquire()` and `deliver()`s.  The acquire load reads the last
  decrement, an RMW in the release sequence headed by every earlier
  retire's release decrement (RMWs extend release sequences), so each
  earlier retire -- the winner's `_outcome` write before it, each loser's
  unwinding -- happens-before the delivery.
- No entrant can be last before the last entrant has started, so the last
  `start` in the fold is the race's last touch of `op`.
- All entrants stopped: `_outcome` is empty, delivery is `set_stopped`.

Tests `execution_when_any`, `execution_when_any_cancel`.  The entrant pack
is compile-time; a runtime-N race is a Nursery.

### 7.3 sync_wait

`execution::sync_wait(sender, token)` connects to a `SyncWaitReceiver`
(records into a local Outcome, `std::binary_semaphore::release`), hands
`start` to the pool through the two-word `Starter`, and `acquire`s.  The
semaphore's release/acquire is the edge that publishes the outcome.
Starting on the pool means the operation always runs on a pinned worker,
and a worker that calls `sync_wait` cannot recurse into its own queue.
`coroutine::sync_wait(awaitable)` lifts a non-sender through
`lift_awaitable` (a Future that awaits the caller's object through a
pointer; the object outlives the call because the call blocks).  The
caller's token is the receiver's environment; a caller outside any task
passes none and the work runs under a detached token (open item in
docs/todo.md).

## 8. The Nursery

### 8.1 The counter

`Nursery::_counter` (`Atomic<ptrdiff_t>`, initially 0) counts unretired
children plus one for the unarmed join:

| event | operation | notes |
|---|---|---|
| `fork` / `soon` | `fetch_add_relaxed(1)`, assert old >= 0 | before the child can run |
| child retires (`_static_resume`) | `fetch_sub_release(1)` | old == 0 means "last, and the join is armed" |
| join arms (`await_suspend`) | store `_continuation`, then `fetch_sub_release(1)` | old == 0 means "all children already retired" |
| join fast path (`await_ready`) | `load_relaxed() == 0`, then `exchange_acquire(-1)` | only if the outer token is unrequested |

After the join the counter is -1 for good, which makes the Nursery
single-use by construction: a fork after it trips the `count >= 0` assert,
and `~Nursery` asserts the counter is -1 (joined exactly once).  So the
interior `request_stop` being sticky is not a leak hazard (comment on
`Nursery::request_stop`, ec911ca).

Edges.  The joiner arms with a release RMW after writing `_continuation`;
the last child's decrement is an RMW in that release sequence and its
`load_acquire` reads it, so `_continuation` (and the joiner's prior work)
happens-before the child's take and resume.  Conversely each child's
release decrement heads a release sequence the last party's acquire reads
from, so every child's writes -- its Outcome target, its `_cancelled_count`
relaxed increment -- happen-before the joiner's `await_resume`, which reads
`_cancelled_count.nonatomic_load()`.

### 8.2 Retire ordering

A child retires through its continuation, never in a hook: on the value
path the child's delegate names the Nursery as the continuation
(`final_await_suspend` returns `from_address(_nursery)`), the child's frame
is destroyed (section 2.1), and only then does the symmetric transfer reach
`Nursery::_static_resume`.  The last retire may resume the joiner, which may
destroy the Nursery and the Outcome targets; the child must be completely
gone by then.

`_static_resume`'s last party decides the joiner's fate by the OUTER token:
unrequested, resume it (an interior `request_stop` or a child's own stop is
absorbed and reported as the count `join` returns); requested, destroy it
(the cancellation continues up through the joiner).  The same check is made
on the join's other two paths -- `await_ready` refuses the fast path when
the outer is requested, and `await_suspend`'s zero branch destroys the
joiner in place -- so every interleaving of "last child retires" against
"join arms" reaches the same verdict (tests `nursery_scope_cancel`,
`nursery_scope_cancel_empty`, `nursery_outer_bridge`,
`nursery_interior_cancel`).

On the stopped path the retire is NOT after the frame is gone: it is called
from `NurseryChildDelegate::unhandled_stopped` / `Inner::unhandled_stopped`
inside the child's `~BasicPromise` (section 2.4).

### 8.3 The fork-to-join rule

The name is not defined in the code; this is the rule the counter
protocol needs.

> Every fork or soon into a Nursery happens-before its join arms: it is
> made by the owner before `co_await join()`, or by a party that holds a
> count in that Nursery (a live child of it).  A child's results (its
> Outcome or bald target) are read only after the join returns.

The first half is why `fetch_add_relaxed` suffices: a fork by the owner is
sequenced before its own arm, and a fork by a live child is sequenced
before that child's release decrement, so the counter cannot reach the
armed zero between them.  A fork from an unrelated thread concurrent with
the join is a race the counter cannot detect (it may read 0 at
`await_ready` and then find a new child alive).  The second half is the
edge of section 8.1 ("it is racy to access y until the nursery has been
joined", comment on `fork(Outcome<T>&, Future<T>&&)`).

`fork` starts the child on the current thread by symmetric transfer and
schedules the forker on the pool; `soon` schedules the child and continues
the caller, who need not be a coroutine.

## 9. The reactor

### 9.1 The reactor contract

(The banner "Reactor contract" in `core/kqueue_reactor.cpp`.)

- The reactor thread blocks in `kevent` and is protocol-free: every knote
  other than the shutdown doorbell (EVFILT_USER, ident 0) carries as udata a
  frame whose first two words are a `Header`, and the reactor's only action
  is `global_work_queue_schedule(udata)`.  Filters, idents and completion
  semantics are the frame's business.
- Nothing resumes, destroys, or runs GC code on the reactor thread.
- Knotes are `EV_ONESHOT | EV_UDATA_SPECIFIC`, keyed by (ident, filter,
  udata): concurrent frames on one fd never collide, and a frame's
  `EV_DELETE` can only remove its own knote.  Changes use `EV_RECEIPT`, so
  each `kevent` call reports exactly the change's result in `event.data`
  (0, `ENOENT`, `EBADF`, ...).
- ORDER: the kernel carries only the udata pointer, an edge invisible to
  the C++ model and to TSan.  The code treats `kevent` as a mutex: an ADD
  synchronizes-with the delivery of that knote and with a DELETE that
  receipts 0.  `g_reactor_tsan_helper` makes the edge visible to TSan: a
  release `fetch_or(0)` by every publisher before its `kevent`
  (`CancelableKEventFrame::_kevent`), an acquire load by the reactor after
  each harvest.  It over-approximates the per-knote edge and could mask a
  race (docs/tsan_2026-11-08.md).

### 9.2 CancelableKEventFrame

One heap object per armed knote: `_header` (`static_resume`, `nullptr`), the
token, a `stop_callback`, the `kevent` change, a state word `_state`
(RESUMED = 1, STOPPED = 2, STARTED = 4) and a reference count
(`_reference_count_minus_one`, initially 2: three owners).

| owner | its reference is dropped by |
|---|---|
| `spawn` (the arming thread) | the end of `spawn` |
| the fire path (`static_resume`) | `static_resume`, or by whoever proves it will never run: the callback or `spawn` on a DELETE that receipts 0, or `spawn` on a failed ADD |
| the stop callback | the callback, or by whoever proves it will never run: `static_resume` after resetting it un-run, `spawn` after a failed ADD |

`release()` is `fetch_sub(1, release)`; the caller that sees zero does a
`load(acquire)` (reading its own RMW, in the release sequence of every
earlier decrement) and `delete`s.  The callable `_f` dies with the frame, so
for `ContinuationThunk` the coroutine is destroyed by whoever drops the
last reference: cancellation as destruction, on that thread.

The callable runs exactly once or never.  `static_resume` invokes it if it
sees STARTED and not STOPPED; `spawn` invokes it if its `fetch_or(STARTED,
release)` sees RESUMED (the fire beat `spawn` to the state word) and not
STOPPED.  Frame atomics are otherwise relaxed: `static_resume` and the
callback `fetch_or` relaxed, and the callback adds an acquire re-load of
`_state` when it saw STARTED, which reads from the release sequence headed
by `spawn`'s release `fetch_or`.  Whether that garnish is load-bearing
beyond the reference count's own release/acquire is unproved; the
exhaustive-interleaving check of this frame is the open half of a
docs/todo.md item.

### 9.3 The interleavings, and the fire-won race

`static_resume` begins with `_stop_callback.reset()`.  `~stop_callback`
either deregisters a callback that has not started (it then never runs) or,
if it is running on another thread, waits for it to return ("~_stop_callback
and request_stop are mutually excluded").  That single step settles most
cases:

1. Stop first, knote not yet fired: the callback sets STOPPED, its DELETE
   receipts 0 (the knote is withdrawn, `static_resume` will never run),
   it drops the fire path's reference and its own.  The frame dies inside
   `request_stop` if `spawn` has finished; the coroutine is destroyed on the
   requesting thread.  This is the case the tests mean by "cancellation is a
   synchronous join" (`kqueue_cancelable_after`, the cancel path).
2. Fire first, stop later: `static_resume` resets the callback before any
   request, so the request never reaches this frame; `_f` runs and
   schedules the continuation, which observes the stop at its next leaf.
   This is why `cancelable_after`'s callable "must tolerate a late stop".
3. The fire-won race: the knote has fired (the reactor scheduled
   `static_resume`) when the stop arrives, and the callback runs first.
   Its DELETE receipts `ENOENT` -- it cannot tell "fired" from "not yet
   armed" -- so it does not drop the fire path's reference; it drops its
   own and returns.  `static_resume` then runs (or was blocked in the
   reset until the callback returned), sees STOPPED, does not invoke `_f`,
   and drops the last reference: the coroutine is destroyed on the POOL,
   after `request_stop` has returned.  The stop wins the state word; the
   fire wins the kernel; the fire path does the destroy.
4. Stop before arming (token already requested at `spawn`): the callback
   runs inline in `_stop_callback.emplace`, sets STOPPED, DELETE receipts
   `ENOENT`, drops its reference.  `spawn` adds the knote anyway ("no
   early-out"), sees STOPPED at its `fetch_or(STARTED)` and DELETEs the
   doomed knote at once (receipt 0: drop the fire path's reference;
   `ENOENT`: it fired already and `static_resume` will drop it).  In the
   common case the frame is gone before `spawn` returns (the
   cancel-before-spawn case of `kqueue_cancelable_after`); the coroutine is
   destroyed inside the awaitable's `await_suspend`, which then returns
   `noop_coroutine` without touching `*this`.  If the request lands on
   another thread between `emplace` and the ADD, the same thing happens
   with `spawn` doing the destroy after `request_stop` returned.
5. A failed ADD (bad fd): `spawn` drops the fire path's reference, resets
   the callback, and returns the callable inside the `std::expected`'s
   error (`Unarmed`) rather than letting it die, so
   `CancelableReadinessAwaitable` can `release()` the handle back and
   resume at once with the errno (test `kqueue_recv_bad_fd`: a
   registration error is a completion, not a timeout).

So the synchronous-join property holds for a settled parked frame (case 1)
and not in the windows of cases 3 and 4, where the destroy is deferred to
the other party.  Correctness does not depend on it -- a Nursery's join
waits for the deferred retire -- but anything that assumes "the frame is
gone when `request_stop` returns" must not be built on a reactor leaf.
`shared_event_wait` (section 10.3) has the stronger property.

### 9.4 The leaves built on it

- `ContinuationThunk`: the callable for "continue this coroutine":
  `operator()` schedules the handle on the pool (a second hop: the reactor
  already scheduled `static_resume`); the destructor destroys the handle if
  it was never taken; `release()` takes it back.
- `CancelableReadinessAwaitable` (and the `Readable` / `Writable`
  specializations): one knote on (fd, EVFILT_READ or EVFILT_WRITE); at most
  one waiter per (fd, direction).  `recv_some`, `send_some`, `accept_one`,
  `connect_to` wait for readiness and make one syscall, returning kernel
  convention (`-errno`).
- `CancelableSleepAwaitable` / `cancelable_sleep`: an EVFILT_TIMER knote
  whose ident is the frame's own address.
- `cancelable_after(token, duration, f)`: the same frame with an arbitrary
  callable, invoked on the pool at the deadline or destroyed unrun on a
  stop.  An arm made outside any task under a detached token is untracked:
  its frame leaks at `close(kq)` in `global_reactor_stop` (docs/todo.md,
  "Pending-arm accounting").

### 9.5 with_deadline (as on main)

`with_deadline(deadline, future)` returns a `WithDeadlineSender`, which is
a sender in name only (no `connect`); `transform_await` turns it into a
`WithDeadlineAwaitable`, the last delegate-based adaptor.  `await_suspend`
sets itself as the inner's delegate, bridges the outer token into
`_inner_stop_source` (`Callback`), arms a `cancelable_after` on the interior
token whose callable requests it (keepalive copy), and transfers into the
inner.  The inner runs under the interior token.  Outcomes:

- inner completes: `final_await_suspend` resumes the outer;
  `await_resume` requests the interior source, which withdraws the timer
  promptly (test `kqueue_recv_early_completion_drains`: not at the
  deadline), and returns the engaged optional;
- inner stopped by the timer: `unhandled_stopped` absorbs it, resuming the
  outer with `nullopt` -- unless the outer's own token is requested, in
  which case it destroys the outer and the cancellation continues (test
  `kqueue_recv_cancel`).

A rebuild of `with_deadline` as a two-entrant race over the sender seam is
under review on a branch and is not on main.

### 9.6 Shutdown

`global_reactor_stop` triggers the doorbell; the reactor switches to
polling and exits once a harvest returns fewer than `NEVENTS` events, then
main closes the kqueue.  Every waiting task must have finished first.  Since
2026-10-18 that is arranged by the root Nursery (`root` in `core/main.mm`):
main requests `root.request_stop()` while pinned, unpins, and
`sync_wait(root.join())`s before stopping the reactor.

## 10. Other leaves

### 10.1 SingleConsumerEvent (`core/single_consumer_event.hpp`)

One atomic word: `nullptr` (unset, nobody waiting), a frame address
(parked), `this` (set), `&g_cancelled` (the waiter's stop fired before it
published).  The waiter registers its callback FIRST and publishes its
handle with `compare_exchange_strong(nullptr, handle, release, acquire)`;
the callback withdraws with a CAS back to `nullptr` (acq_rel) and only the
winner touches the frame; `set()` exchanges in `this` (acq_rel) and
schedules a parked frame, the exchange being its last touch of the event.
A requested token wins over a set in `await_suspend`.

### 10.2 MultiProducerSingleConsumerQueue (`container/mpsc_queue.hpp`)

Mutex-protected: `emplace` takes `_pop_continuation` under the mutex and
schedules it; the callback takes it under the mutex and destroys it outside;
`await_suspend` registers the callback, then parks under the mutex unless
the token is requested (destroy without parking, even with items waiting)
or items are present (transfer straight back).

### 10.3 shared_event_wait (`render/metal_event.mm`)

Metal calls a block (`notifyListener:atValue:block:`) on the listener's
serial dispatch queue and cannot withdraw it.  So the block holds a small
`std::shared_ptr<WaitState>` (continuation, `stop_callback`, claim word),
never the coroutine.  Two parties, one claim:

- stop: `WaitState::Callback` (on the requester, or inline in `emplace`)
  `fetch_or(STOPPED, acq_rel)` and destroys the continuation there;
- fire: `WaitState::fire` on the listener queue FIRST resets the
  `stop_callback`, THEN `fetch_or(FIRED, acq_rel)`, and schedules the
  continuation on the pool if STOPPED was not set.

The reset settles the race: after it the callback has either never run and
never will, or has run to completion and its return synchronizes-with the
reset's return (libc++: the invoking thread's release store of the
callback's completion flag, the destructor's acquire wait).  The claim
RMWs would be correct relaxed on that edge alone; they are acq_rel as a
second, TSan-visible witness, and the callback asserts it never sees FIRED.
Unlike the reactor leaf there is no deferred window: a stop that reaches a
parked wait always destroys the frame inside `request_stop`.  The block
may wait in `~stop_callback` for a requester destroying a tower, delaying
the blocks queued behind it on the serial queue.  A stopped wait on an
event that never reaches the value leaks its `WaitState` (measured: Metal
keeps the block); an owner can retire such waits by signalling a terminal
value.  Tests: `metal_event_cpu_signal`, `metal_event_gpu_signal`,
`metal_event_deadline`, `metal_event_cancel_before_park`,
`metal_event_cancel_parked`, `metal_event_fire_stop_race`.

### 10.4 DebugWaitForCollectionCycles

`co_await DebugWaitForCollectionCycles{k}` (`ext/coroutine.hpp`) registers
the raw handle with `collector_register_cycle_callback(k, handle)`
(`core/garbage_collected.cpp`; `k == 0` schedules at once).  The collector
keeps waiters in `_cycle_waiters` under `_cycle_waiters_mutex`:
`_on_cycle_started(bit)` ORs the starting bit into every waiter's tag, and
`_on_cycle_completed(bit)` -- called when bit k recycles (CLEARING ->
UNUSED), i.e. after its sweep -- decrements `target` for each waiter whose
tag contains the bit and CLEARS the whole tag.  So a count of k means k
completed cycles, each of which started after the previous counted
completion (the first after registration): a serial chain, not k of the
up to 16 overlapping cycles.  Ready waiters are scheduled on the pool; the
mutex is the edge from registration to the collector, the work queue the
edge from the collector to the resumed frame.

It is a test-only leaf and is NOT cancellable: it registers no
`stop_callback`, so a stop request cannot reach a frame parked in it, and
nothing can withdraw the registration.

The "cycles >= 3" rule: a test that needs an object it has just dropped to
be FREED (not merely observed) waits for at least three cycles
(`[string interning]` in `container/HeapString.cpp` waits 3 before asserting
that a re-intern emplaces).  One counted cycle is not enough by the code
above alone: the GC doc records that floating garbage survives one to two
cycles (garbage_collection.md, "floating garbage survives 1-2 cycles";
garbage born or shaded while a cycle is in flight is snapshot-protected and
sweeps in a later cycle).  Why exactly three, rather than two plus margin
for the weak interning entry, is not argued in the code; treat 3 as the
floor established empirically, and 1 as sufficient only for "the collector
has run over this work" (`ctrie_commutative` in `container/ctrie.cpp`;
`gc_root_churn`'s 4 is for sweeps under the violation checks).

## 11. Rules for writing a new leaf awaitable

A leaf is an awaitable that parks the awaiting frame on something that
will later continue it.  Follow each rule, or say in the header why not.

1. **Get the token from the continuation.**  `await_suspend` takes
   `std::coroutine_handle<OuterPromise>` and calls
   `get_stop_token(continuation)`; the token is the chain's, through the
   delegates (3.1).
2. **Cancellation is destruction.**  On a stop, destroy the parked frame
   (`handle.destroy()`); never resume it with a flag.  The cascade does
   the rest (1.6).  A leaf that cannot be cancelled says so
   (`DebugWaitForCollectionCycles`, 10.4).
3. **Register the callback before you publish the handle.**  A request
   landing between the two must be seen by the publish
   (`SingleConsumerEvent`'s cancel mark; `shared_event_wait` checks STOPPED
   after `emplace` and skips the registration; `CancelableKEventFrame`
   adds the knote and then DELETEs it).  Registration can run the callback
   inline: handle the already-requested token through the same path, and
   prefer "requested wins" over "ready wins".
4. **Exactly one party disposes of the continuation.**  Settle it with one
   claim (an RMW, a CAS, a mutex-guarded take, or a reference count) between
   the stop callback and the completion path, and state the edge that
   publishes the frame's state to whichever party wins.  If the completion
   path can run while the callback is running, reset the `stop_callback`
   on the completion path before claiming (`WaitState::fire`,
   `CancelableKEventFrame::static_resume`), and know that this may block.
5. **No *this after the hand-off.**  Once the handle is published (or the
   callback registered and possibly fired), the frame -- and the awaitable
   in it -- may be gone.  Copy what you need into locals first
   (`SharedEventAwaitable::await_suspend` copies the event and value), and
   return `noop_coroutine` (or `void`) without touching members.
6. **Never resume or destroy on a foreign thread.**  A reactor, a dispatch
   queue, a driver callback: schedule the handle on the pool
   (`global_work_queue_schedule`) instead (1.2).  The stop callback runs on
   the requester, which the contract makes pinned (3.2).  Whatever the
   foreign thread does hold must be destructible there: no GC objects, no
   frames (`~WaitState`).
7. **Keep what a foreign party holds small.**  If the foreign side cannot
   be withdrawn, it holds a shared state record, not the coroutine; that
   record outlives a stop and dies when the foreign side lets go
   (`WaitState`).  If it can be withdrawn, withdraw it promptly on stop
   (`EV_DELETE`) and on early completion of a composition
   (`WithDeadlineAwaitable::await_resume`).
8. **If your callback requests a stop source, keepalive.**  Copy or move
   the source into a local first (3.3).
9. **Completion is the last touch.**  The call that schedules, resumes or
   destroys the frame, or completes a receiver, is your last access to
   anything the frame owns (4).
10. **Ready-path and error-path completions do not park.**  An
    `await_ready` that can see the result completes without a state
    (`SharedEventAwaitable::await_ready`); a registration that fails
    resumes at once with the error (`CancelableReadinessAwaitable` via
    `Unarmed`).
11. **Count your live states for tests.**  A relaxed increment at
    construction and a release decrement at destruction, read with
    acquire by a bounded drain loop (`g_cancelable_kevent_frame_count` and
    `FrameCensus`; `g_shared_event_wait_state_count` and
    `wait_states_drained`), so a test can prove the state dies promptly and
    fail rather than hang.
12. **Test the race.**  Cancel before park, cancel while parked, completion
    without stop, and a sweep of stop against completion landing close
    together, asserting exactly one of resume or destroy per wait
    (`metal_event_fire_stop_race`, `coroutine_sender_arrival`,
    `kqueue_cancelable_after`'s early-fire loop).

Cross-check against `render/metal_event.mm`: rule 1 `get_stop_token
(continuation)`; 2 `take(_continuation).destroy()` in `Callback`; 3
`emplace` then the STOPPED check then `notifyListener`; 4 the claim word
plus the reset-before-claim in `fire`; 5 the `event` / `value` locals and
the comment "past the emplace, *this may be gone"; 6 `fire` schedules,
`~WaitState` touches no GC object; 7 the block captures
`shared_ptr<WaitState>`; 8 not applicable (it requests nothing); 9
`global_work_queue_schedule(take(_continuation))` is `fire`'s last act on
the frame; 10 `await_ready` on `signaledValue`; 11 and 12 the six tests.

## 12. Known gaps and open questions

- Stopped-path ordering (2.4, 8.2): a stopped hook runs inside the dying
  frame's `~BasicPromise`, so a nursery's last retire, and an absorbing
  `WithDeadlineAwaitable`'s resume of its outer, run before the cancelled
  frame's parameter copies are destroyed.  No current coroutine is known to
  have a parameter whose destructor this would break; nothing enforces it.
- Reactor leaves are synchronous on cancel only outside the fire-won and
  arming windows (9.3); `metal_event.hpp`'s "as the reactor's leaves do"
  holds for the settled case.
- `CancelableKEventFrame`'s relaxed-RMW plus acquire-garnish argument has
  not been model-checked (docs/todo.md).
- `get_stop_token` walks the delegate chain per leaf await (docs/todo.md).
- `with_deadline`'s sender rebuild is in review, not on main (9.5).
- An arm outside any task leaks its frame at shutdown (9.4).

## File map

```
ext/coroutine.hpp             Header, Promise, Delegate, OutcomeDelegate, Future,
                              FutureAwaitable, FutureOperationState,
                              SenderAwaitable/AwaitableBase, lift_awaitable,
                              DebugWaitForCollectionCycles, SuspendAndCancel
ext/coroutine.cpp             TransferToBlockableExecutor, destroy thunk, tests
                              (unwind chain, sender seam, inline stack, arrival)
ext/coroutine_test.hpp        UnwindFence, UnwindProbe
ext/execution.hpp             Outcome, just, stopped_as_optional, shield,
                              when_any (RaceBase, RaceOperationState), sync_wait
ext/execution.cpp             adaptor, race and sync_wait tests
core/nursery.{hpp,cpp}        Nursery, NurseryChildDelegate, NurseryChildValueDelegate
core/kqueue_reactor.{hpp,cpp} reactor thread, CancelableKEventFrame, ContinuationThunk,
                              readiness and timer leaves, cancelable_after,
                              WithDeadlineAwaitable
core/single_consumer_event.hpp  SingleConsumerEvent
container/mpsc_queue.hpp      MultiProducerSingleConsumerQueue
render/metal_event.{hpp,mm}   shared_event_wait (WaitState)
core/global_work_queue.{hpp,cpp}  the pool; pinning cadence
core/garbage_collected.cpp    collector_register_cycle_callback, _cycle_waiters
```
