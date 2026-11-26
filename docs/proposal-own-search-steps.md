# Proposal: let Psion organize its own search

Status: implemented as experiment 2 below, and the default since
2026-11-25 (v3.6b), after the match in "The match" below.

## Why

The engine the adapter presents as Psion is not quite the engine
Richard Lang intended. The evaluation, the move generator and the
search code are Psion's, but how the iterations follow each other is
not (see "Infinite is not the limit of the timed levels" in
[search-internals.md](search-internals.md#iterative-deepening-the-iteration-counter-5a67)):

- **Since v3.4** the adapter uses level *Infinite* and ends each search
  with MOVE-NOW. That is pure d = 3: every iteration uses the variant
  with d = 3, and Psion's own time rules are skipped.
- **Up to v3.3** the adapter used level 11. At today's speed the steps of
  2 almost never came, so that was pure d = 1, which was a side effect of
  the speed-up.
- **What Lang intended**, at a timed level on a PC of 1985, was a mix.
  Steps of 4 come first. Once t ≥ 3S/32 the steps are 2, so each new
  depth is searched first with d = 3 and then with d = 1. No new
  iteration starts after S/2, the search ends at a root move after about
  1.375 S unless the score has dropped, and H is a hard limit.

The aim is authenticity rather than strength: Psion should organize its
search as its authors designed it, without becoming much weaker than
the current version. Pure d = 3 is +41 Elo over pure d = 1, and better
on WAC. The mix has never been tested.

## How

Psion computes its time for a move once, in `0EB1` before each search:
the target S (`5A78`) and the hard limit H (`5A7A`), from the level
(`725A`), the account (`7268`) and the move counter (`7260`). After that
it runs by itself. So:

1. Set a timed level instead of *Infinite* (`0D`).
2. Overwrite `5A78` (S) and `5A7A` (H) with the adapter's values right
   after `0EB1` returns. This needs a hook on that address in DOSBox (an
   emulator change). Going through `0EB1`'s formula with a made-up account
   is messier and overflows (below).
3. Keep MOVE-NOW as a safety net against the clock.

Psion's code is not changed, but its memory is written, which the
adapter has not done so far for the search: "the adapter only reads
Psion's memory and decides when to press MOVE-NOW" (time-management.md).
It is like choosing a level with a time of our own, and is accepted for
this experiment (see "Plan").

## Problems

- **Clock resolution.** Psion counts in its seconds (19 timer ticks,
  1.044 s). With searches of 0.5 to 5 s, S would be 0 to 5. With integer
  arithmetic 3S/32 is then 0, so the steps are 2 from the first
  iteration, and S/2 is counted in whole seconds. Psion's rules were made
  for searches of a minute or more.
- **Fix: a faster clock.** DOSBox could make the 1Ch timer, or only the
  count of ticks to Psion's second, k times faster. With k about 250 to
  300 (the speed-up over a PC of 1985), Psion sees the proportions of
  1985 in nodes, which is what its rules were made for. This is now
  `--clock-factor K`, which runs counter 0 of the emulated PIT (IRQ 0,
  the BIOS tick count and 1Ch) K times faster. In a 5 s search the BIOS
  count went up by 91, 903 and 27,141 ticks at K = 1, 10 and 300
  (90, 904 and 27,130 expected), and K = 300 cost about 2% in nodes.
- **16-bit overflow.** With a fast clock, S, `5A7C`, the account and
  A·(n + 3)/4 become k times larger. That is one more reason to write S
  and H directly.
- **Pondering.** Psion's own model sets `5A6B` to `FF` while pondering.
  On a ponderhit `5A6B` becomes `80`, the time pondered goes to `713C`,
  and `5A7C` starts again at 0. The adapter could copy this by writing
  memory, but a first test can do without ponder.
- **The 15-ply limit.** At a timed level the loop stops after the
  iteration at `1B`. Psion then moves at once, as it does now in locked
  endings.

## Plan (2026-11-25)

The adapter computes T and its maximum as now (`Move Overhead`,
`Clock Buffer`, the 75% cap and so on), writes them into Psion as S and
H, and lets Psion run the search with its own rules, with MOVE-NOW only
as a fallback. The two time managements are separate modules, so that
the current one stays available. A match then pits Psion's time
management against the current one. If Psion's costs too much Elo, it
goes behind an option such as `--authentic-tm`; otherwise it becomes the
default.

This is experiment 2 below. Experiment 1 can still separate the effect
of the d = 3 / d = 1 mix from that of the stopping rules, if needed.

## Implementation: Psion's own time management

Psion's own time management is the default since 2026-11-25 (it was
`--authentic-tm` at first; `--alt-tm` brings back the adapter's). A
search with a clock, also a ponder search, but not `go movetime` or
`go infinite`, runs this way:

- **The times.** S is T, the adapter's target. H is 3T, as Psion's own
  hard limit is at most 3S + 1, within the same caps as the adapter's
  maximum: 75% of the remaining time and the remaining time less
  `Clock Buffer`. Both count from Psion's start, as the adapter's do
  (the setup time up to 30 ms is charged to them).
- **The clock.** Psion's clock runs K times faster than the wall clock,
  K = 300 by default (`--clock-factor` sets it). For a long search K is
  lowered, so that H stays at most 20,000 of Psion's seconds and its sums
  (S + 3S/8, 3S, the time pondered plus the search time) fit in a signed
  word. At a lower `Speed` the emulated clock runs slower, which is taken
  into account.
- **Into Psion.** The adapter sets level 11 instead of *Infinite*, and
  the memory handler that watches Psion's search (nodes.cpp) replaces
  Psion's own writes of S (`5A78`, at `0F00`) and H (`5A7A`, at `0F17`)
  with the adapter's values: the first write of each in a search. When
  Psion then lowers S by a quarter (S − S/4 + 1 at `0F34`..`0F40`, step 6
  of "The time for a move"), that second write is kept, so the reduction
  applies to the adapter's S.
- **Fallback.** MOVE-NOW is pressed only if Psion has not moved 30 ms
  after H.
- **Pondering.** Psion's own ponder search (doc `2518`..`2521`) runs at
  its timed level with `5A6B` = 1: bit `80` clear, so no stop, and from
  t ≥ 3S/32 on steps of 2. The adapter's ponder search runs at level 11
  with S and H as above, but `5A6B` is `FF` (clearing bit `80` would also
  turn Psion's keys into move input, doc `0644`). Instead the memory
  handler keeps `5A7C` at most S/4 while pondering: the steps are the
  same (2 from 3S/32 on), and no stop is reached.
- **Ponderhit.** The adapter does what Psion's own ponderhit does (doc
  `06DD`..`06E6`): it lifts the cap, writes S and H, sets `713C` to the
  time pondered (at most S) and restarts `5A7C` at 0.

**First checks** (2026-11-25, 40 plies of self-play at 10+0.1). Out of
book, Psion moved after 160 to 597 ms with S of about 300 to 415 ms: it
started no iteration after S/2, stopped at a root move after 1.375 S,
and the fallback was never needed. With the fallback moved to 100 ms by
a temporary build, MOVE-NOW came in time.

After `ponderhit` Psion can use much more time than with the adapter's
time management. (Measured with the ponder search still at *Infinite*;
see below for the timed level.) In a test position with 3 s of pondering, the
iteration running at the ponderhit had started 2.7 s before, and its
first root move took more than 5 s. Psion checks the stop at 1.375 S only
when it starts a root move, so it went on to H = 3T (2.4 s after the
ponderhit, against 0.8 s with the adapter's time management). Psion's own
ponderhit works the same way. What differs is the ponder search before
it: at *Infinite* all steps are 4, so after a few seconds of pondering
Psion is usually in one long iteration, each iteration costing about 15
times the one before. Psion's own ponder search ran at its timed level,
where the steps become 2 once t ≥ 3S/32, so its iterations would grow
less from one to the next. How Psion ponders with `5A6B` = `FF` (whether
it starts no iteration after S/2 then) is not decoded.

With the ponder search at the timed level (2026-11-25, same position,
20 s + 0.2 s, so T ≈ 0.8 s and H ≈ 2.4 s):

| Ponderhit after | Bestmove after the ponderhit |
|---|---|
| 0.2 s | 0.28 s |
| 1 s | 0.58 s |
| 3 s | 2.4 s (H) |
| 10 s | 0.56 s |

At 3 s the iteration running at the ponderhit had started at 1.1 s
(steps of 2 now), but it still ran to H: as with Psion's own ponderhit,
the time pondered counts only for starting an iteration.

## The match

Psion's own time management (`--authentic-tm` at the time) against the
adapter's, both from the same build, at 10+0.1 on pl with the 8moves_v3
openings, without pondering (2026-11-25/26): **+28.8 ± 6.4 Elo** over
8,000 games (54.1%, +3206 −2544 =2250, pentanomial [376, 719, 1402,
873, 630]; 95%, from the per-opening pairs), with no time losses and no
fallbacks. Both used about the same time per move (0.205 s against
0.201 s on average, over all games), so the gain does not come from a
longer search.

## Experiments

They would run in this order and only after a go-ahead, each first on
positions with Stockfish as the oracle and then, if worth it, as a match
on pl against the current version.

1. **The mix alone.** Keep the adapter's time management and MOVE-NOW.
   Set a timed level, write S = the adapter's nominal time, use a faster
   clock, and clear bit `80` of `5A6B`. Psion then never stops by itself:
   past S/2 it goes on with steps of 2, and there is no hard limit. Still
   to check: whether the stop at 1.375 S at a root move also depends on
   that bit.
2. **Psion's stopping rules too.** Leave bit `80` set, so S/2, 1.375 S
   and H decide when the search ends. The adapter only chooses S (and H)
   from the clock and keeps MOVE-NOW as a safety net.

If the mix is about as strong as pure d = 3, it could become the
default, or an option ("authentic" against the current behaviour).

## Open questions

- Should the time after `ponderhit` be capped lower than 3T (see
  above)?
