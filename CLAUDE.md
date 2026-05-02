# Project: wry

## Domain Context

- C++20 game/runtime project with substantial breadth: systems plumbing (concurrent allocators, lock-free containers, coroutines, atomic primitives, an epoch-based GC), simulation, rendering, audio, IO and wire-format parsing. Concurrent-systems work is a significant area but not the whole codebase — much else hasn't been engaged with yet.
- For concurrent code anywhere in the project, be precise about std::memory_order semantics — cite the specific ordering and the synchronizes-with / happens-before edge being relied on. Prefer concrete interleaving examples over hand-wavy claims about correctness.
- Fundamentals first: when in doubt, get the underlying primitives and contracts right before engaging with higher-level features that depend on them.

## Code Review Discipline

- Before flagging an issue, trace the logic end-to-end and state the concrete failure scenario (inputs, interleaving, or call site) that would trigger it.
- Double-check loop/wait conditions by reasoning about both the true and false branches before claiming correctness.
- Distinguish carefully between debug-only assertions/instrumentation and production memory-ordering choices; do not generalize seq_cst usage from one to the other.
- When uncertain, say so explicitly rather than asserting a finding.

## Plan Mode

While in plan mode, do not call Edit/Write/Bash-with-side-effects under any circumstances. If the user appears to have already applied fixes, confirm before suggesting further edits and remain in plan mode until explicitly exited.
