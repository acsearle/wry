# Project: wry

## Domain Context

- C++20 game/runtime project with substantial breadth: systems plumbing (concurrent allocators, lock-free containers, coroutines, atomic primitives, an epoch-based GC), simulation, rendering, audio, IO and wire-format parsing. Concurrent-systems work is a significant area but not the whole codebase — much else hasn't been engaged with yet.
- For concurrent code anywhere in the project, be precise about std::memory_order semantics — cite the specific ordering and the synchronizes-with / happens-before edge being relied on. Prefer concrete interleaving examples over hand-wavy claims about correctness.
- Fundamentals first: when in doubt, get the underlying primitives and contracts right before engaging with higher-level features that depend on them.
- The collector lives in `client/client/core/garbage_collected.{hpp,cpp}` and is documented in `client/client/core/docs/garbage_collection.md`. It is a DLG-family concurrent tracing collector with a Yuasa-style deletion barrier and ragged epoch-based phase transitions. Mutators never block waiting on the collector; this is a load-bearing invariant. The 16-bit gray/black words support up to 16 overlapping concurrent collections distinguished by bit position.

## Code Review Discipline

- Before flagging an issue, trace the logic end-to-end and state the concrete failure scenario (inputs, interleaving, or call site) that would trigger it.
- Double-check loop/wait conditions by reasoning about both the true and false branches before claiming correctness.
- Distinguish carefully between debug-only assertions/instrumentation and production memory-ordering choices; do not generalize seq_cst usage from one to the other.
- When uncertain, say so explicitly rather than asserting a finding.

## Plan Mode

While in plan mode, do not call Edit/Write/Bash-with-side-effects under any circumstances. If the user appears to have already applied fixes, confirm before suggesting further edits and remain in plan mode until explicitly exited.

## Shell (zsh)

- The Bash tool runs under zsh, which has `nomatch` on by default: an unquoted argument containing a glob metacharacter (`*` `?` `[` `]` `(` `)` `{` `}` `~`) that doesn't match a file aborts the whole command with `zsh: no matches found`, rather than passing the literal through the way bash does. This bites C++ work constantly — template/array identifiers (`Foo[Bar]`, `vector<T>`), grep patterns, and test substrings all carry these characters.
- Fix: single-quote any argument that contains a glob metacharacter and is not meant as a glob, e.g. `grep -n 'operator[]' file`, `./binary --test-only 'Table[basic]'`, `git show 'HEAD~2'`. When you do want expansion, leave it unquoted. Reach for single quotes by default for literal patterns.

## File Contents

- Strong default: ASCII only when editing files. Box-drawing characters for section breaks (e.g. `──`), fancy bullets, em-dashes, and similar decorative non-ASCII are gratuitous and break grep, diff tooling, and some terminals. Use `//`, `// ----`, or `// ====` for section dividers; ASCII art for diagrams. Exceptions are defensible (mathematical symbols where ASCII spelling is awkward, canonical non-English names, user-visible strings) but should be the rare conscious choice, not the default.
