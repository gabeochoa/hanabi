# Allocations: what the frame asks malloc for, and how often

**Why this file exists.** `docs/perf/MEMORY.md` is about memory the app KEEPS.
This one is about memory it borrows and gives back — four thousand times a
frame, sixty times a second, forever. Every gate in the repo was green while it
did, and they were right to be: nothing leaked. The app allocated 3,535 times
per frame sitting still on the Home view and returned every one of them before
the next frame, so the soak gate's slope was flat, RSS was flat, and the live
block count moved by −0.6 KB per thousand frames.

The reported symptom was "gets slower and slower until it freezes". A leak
produces that. So does malloc traffic, and the two are indistinguishable from
the outside.

Everything below was measured on `gabeochoa-mac-GRQ7Y259H4` on 2026-08-25,
against `main` at `ddb391c`, on a box running four other agents' builds with
load averages between 10 and 34. `main` moved four times during the work
(`perf/scroll`, `perf/text`, `perf/retire`, `perf/stress`, `perf/gpu`) and
every number here was re-taken after each rebase; the three arms read
identically at `cc9fae1`, `9ba8bb2` and `ddb391c`, which is worth saying
rather than chasing — none of those branches touched what this one is about,
and the counter is exact enough to prove it.

---

## The headline

| arm | before | after | |
| --- | ---: | ---: | --- |
| 20 sessions, Home | 2,550.0 | **827.0** | −68% |
| 2000 sessions, Home | 3,535.0 | **1,197.0** | −66% |
| 2000 sessions, 480-message thread | 6,687.0 | **2,740.0** | −59% |

Operator-new calls per frame, steady state. At 60 fps the 2000-session Home
view went from 212,100 mallocs a second to 71,820.

---

## The instrument, and why the obvious one does not work

`MallocStackLogging=1` plus `malloc_history <pid> -allBySize` is the tool that
found the Metal autorelease leak in a single run, and `docs/perf/GATES.md`
points at it. **It cannot see any of this.** `-allBySize` walks the blocks that
are LIVE at the moment it attaches; the frame frees everything it allocates, so
against this app mid-soak the top ten rows are Metal device init, the sokol
buffers and the mock catalog — every one of them one-time — and not a single
frame allocation. That was verified on a real run before it was believed, and
it is a property of the tool rather than of this app: it is a leak finder, and
this is not a leak. (`malloc_history -allEvents` is not blind, and emits every
malloc and free in order: ~250,000 records for a 60-frame run.)

What works is three things, in this order.

### 1. How many? — `HANABI_PROF=1`

`src/util/prof.h` counts every `operator new`. `src/util/soak.h` now reports it
per bucket and as a steady-state figure in the verdict:

```
[soak] frame    400  ... live 55907 blocks / 43641 KB  allocs    1197.0 /f
[soak]   allocs/frame  1197.0  operator new calls, steady state   1450  ok  83% of ceiling
```

**This number is deterministic.** Not "stable", not "low-variance" — the same
build on the same fixture reports the same value on every run and in every
bucket of a run, to within one allocation, while the same binary's wall-clock
frame time moved between 1.6 ms and 6.5 ms with the box's load during these
very measurements. That is the property the ratio gates in `GATES.md` were
reaching for, and an allocation count gets it for nothing.

### 2. Where? — `HANABI_PROF_SITES=1` and `scripts/alloc_sites.sh`

Hashes the top eight return addresses at every allocation into a fixed table
and prints the busiest forty. The recorder allocates nothing — an allocating
profiler of allocations recurses on its first call — and walks the frame
pointer chain with bounds checks rather than `__builtin_return_address(1+)`,
which is documented to be allowed to fault.

```bash
scripts/alloc_sites.sh 2000 300             # Home, big catalog
HANABI_SITES_BIG=1 scripts/alloc_sites.sh   # with a 480-message thread open
```

Two details are what make it usable, and both were added after the first
version was useless:

- **Every loaded image is printed with its load address**, so `atos` can be
  pointed at the right one per frame. Without it the largest allocation source
  in the whole app rendered as `0x18561dce8` for a full afternoon, because
  `atos -o hanabi.exe` cannot resolve an address inside libc++.
- **Costs are rolled up to the innermost frame that is hanabi's own code.** The
  raw table is nineteen rows of `string::__init_copy_ctor_external` and one of
  `ComponentConfig::ComponentConfig` — true, and useless. One app function
  reaches the allocator down a dozen different vendor paths and only shows its
  real size once they are collapsed. Every fix after the first was found in the
  rolled-up table.

### 3. Did it stay fixed? — `make alloc-gate`

`scripts/alloc_gate.sh`, in `make test`. Three fixtures, an absolute ceiling on
each, ~20% of headroom. See `docs/perf/GATES.md`.

---

## What was actually costing it

Ordered by what they were worth. Every number is allocations per frame.

### 1. The widget identity hash: 2,164/frame, 64% of everything

`afterhours::ui::imm::mk()` is called once per widget per frame — that is what
gives an immediate-mode widget a stable entity. It derived its key by streaming
the parent id, the absolute source path, the line, the column and the fully
expanded function signature into a `std::stringstream` and hashing the
resulting `std::string`. In this app that string is routinely 200 to 400
characters, built a character at a time through `stringbuf::overflow`.

Worked around with hanabi's own `mk` (`src/ui/mk.h`) hashing the same five
facts with no string at all — file and function by POINTER, which is stable for
the life of the process and is the only property a key that is never persisted
needs. Upstream ask: `afterhours_gaps.md` **#180**.

It stacks with `src/ui/widget_epoch.h`, which was already wrapping `mk` for a
different reason (stamping the frame that built each widget, for retirement).
Two seams on the same function: one makes the key cheap, the other records when
it was used.

### 2. `ComponentConfig` is copied three times per widget: ~472/frame, and unfixable from here

```
div(ctx, ep, ComponentConfig config)                     # by value
  init_component(ctx, ep, config, ...)                   # by reference
    config = overwrite_defaults(ctx, config, ...)        # BY VALUE  -> copy
      config = merge_with_defaults(...)                  #   result = config -> copy
```

Every `std::string`, `std::vector<TextSpan>` and `std::function` the config
carries is re-allocated per copy. A 40-character card title costs 4.28
allocations a frame, and the label is the content — it cannot be shortened
under libc++'s 22-character small-string buffer without changing what is on
screen.

**There is no app-side workaround.** Everything around it was removed instead —
the derivation of the strings, the identity hash, the wrap measurement — and
this is what is left. It is now the largest single source on the Home view.
`afterhours_gaps.md` **#181**.

### 3. A disabled probe's ARGUMENTS: 153/frame, and 0.34 ms

```cpp
hanabi::mprobe::compare("richbody", rich_body_h(shown, textW), y - probeStartY);
```

`compare` returns immediately unless `HANABI_PROBE_MEASURE=1`. Its arguments do
not: `rich_body_h` is a complete second measure pass over the message body, and
it ran for every visible bubble on every frame in every build anyone has run,
to produce a float that was discarded. One `if`.

The general form is worth remembering: **a guard inside the callee does nothing
for the cost of the arguments.** Anything expensive handed to a diagnostic
needs the guard at the CALL site.

### 4. `std::function` captures that do not fit: ~340/frame

libc++'s `std::function` holds 24 bytes inline. A lambda capturing two
`std::string`s and a `shared_ptr` is a malloc for the function plus one per
string — and then the ComponentConfig copies above clone all of it, three more
times. Nine allocations per transcript line per frame.

Fixed by parking the state on the line's ENTITY (`ecs::LineDrawState`) — which
is the stable thing across frames, that being the whole point of `mk()` — and
capturing one pointer to it. Assigning into the component's existing strings
reuses their capacity, so a steady frame allocates nothing.

**The rule this gives:** in a per-widget draw callback, capture ≤ 24 bytes or
capture a pointer to somewhere that outlives the frame. Never capture a string
by value.

### 5. `std::set` nodes for the focusable set: 391/frame

`UIContext::focused_ids` is a `std::set<EntityID>` cleared and refilled every
frame, so every focusable widget costs a red-black-tree node malloc. Widgets
count as focusable if they carry a click listener — which in hanabi included
every transcript LINE (an empty listener, attached only to get hover and press
plumbing for drag-select) and every minimap MARK.

`with_skip_tabbing(true)` takes them out, and tabbing out of a long thread no
longer walks several hundred stops to reach the composer — so the allocation
fix and the behaviour fix are the same change. The ~242 widgets that
legitimately are focusable still pay — 634 before (`afterhours_gaps.md` #183),
392 of them taken out by the skip. `afterhours_gaps.md` **#183**.

### 6. Pure derivations recomputed per row per frame: ~190/frame

`digest_card` rebuilt each card's displayed title from scratch every frame: a
copy to strip a leading `[P] `, a second string to collapse whitespace, a third
to ellipsize. The sidebar had solved this for its rows; the Home cards had not.
They now share `hanabi::text::TextKeyCache` (`src/util/text_cache.h`).

The pattern, and it is the one to reach for first: **key the memo on the whole
argument tuple of a pure function**, look it up heterogeneously so a
`string_view` can search the map, and return by reference. Then a changed input
is a different key rather than a stale entry, and a hit costs a hash and a
compare and no allocation at all.

---

## Where the remaining 2,740 goes (480-message thread)

| | allocs/frame | whose |
| --- | ---: | --- |
| afterhours systems (`HandleTabbing`, `RenderImm`, layout) | ~1,900 | upstream |
| ComponentConfig label copies | ~470 | upstream (#181) |
| hanabi's own remaining build work | ~370 | ours |

**Two thirds of what is left is inside the library**, and #180, #181 and #183
are the three asks that would move it. That is a different position from where
this started, where two thirds of it was hanabi's.

---

## Traps this cost time to learn

**`git stash` is per-REPOSITORY, not per-worktree.** Eighty worktrees share one
stash stack. A `git stash` here and a `git stash pop` there popped another
agent's entry into this tree; it conflicted, which is the only reason their work
survived. Do not use `git stash` in this repo at all — commit to your branch, or
write a patch file.

**zsh does not word-split an unquoted expansion.** A measurement harness that
built `"HANABI_BIG_TRANSCRIPT=1 HANABI_BIG_TURNS=120"` as one string and passed
it as `$4` to `env` sets ONE variable named `HANABI_BIG_TRANSCRIPT` whose value
is `1 HANABI_BIG_TURNS=120`. It is truthy, so the big transcript switched on and
the turn count silently did not — a fixture half the intended size, reported
with the intended size's name, for an afternoon. Pass env assignments as
separate argv entries. Both scripts here do, with a comment saying why.

**A number that is the same twice is not necessarily the same number you meant.**
The above reproduced to the unit across runs, which is exactly what made it
convincing and wrong.

**Re-measure after a rebase.** `main` moved three times during this work
(`perf/scroll`, `perf/text`, `perf/retire`), and each merge changed the
baseline these numbers are against. One of the fixes here — a memo for the
wrapped-line count — was landed by `perf/text` first and better, and was
dropped from this branch rather than reported twice.

---

## How to check a change against this

```bash
make alloc-gate                             # the verdict, ~20 s
scripts/alloc_sites.sh 2000 300             # where, Home
HANABI_SITES_BIG=1 scripts/alloc_sites.sh   # where, with a thread open
```

If the gate goes red the failure text names the four causes this project has
actually had, in the order they turned up, and the command that lists the call
sites. They are, once more, in that order: a `std::string` over 22 characters
built per widget per frame; a pure derivation recomputed instead of memoized; a
`std::function` capture over 24 bytes; a container rebuilt by value or grown
without `reserve()`.
