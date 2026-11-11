# Time management: a proposal

A proposal for better use of the clock by the adapter, based on the
measurements in [search-internals.md](search-internals.md#time-use-of-the-iterations).
The rules are implemented behind temporary UCI options (see "The
options" below), whose defaults leave the time management as it was.
All the numbers below are starting points, to be measured and tuned. As always, Psion's search is not changed: the
adapter only reads Psion's memory and decides when to press MOVE-NOW.

## What we have now

The adapter computes one allocated time T per move (see
[implementation.md](implementation.md#time-management)), roughly
`remaining/n + 3/4 * increment` with n = 30 or 60, and presses MOVE-NOW
when T (less the move overhead) has passed. Psion then plays the best
move found so far. This ignores what the search is doing at that moment.

## What the measurements say

From 2,012 searches against Fairy-Max at 10+0.1 (710 at 60+0.6 gave
the same picture, with the soft stop even cheaper: at x = 0.4 it lost
none of the 51 move changes):

1. **An iteration costs about 15 times the previous one.** So the last
   iteration rarely finishes: its first move (the previous best,
   searched again) was done in only 36% of the searches.
2. **A late iteration is wasted time.** An iteration started after 40% of
   T finished its first move in fewer than 20% of the cases and changed
   the move in about 2%. While its first move is being searched, the
   move Psion would play is that of the previous iteration (see "The
   move played" in the search notes).
3. **A score drop predicts a change of move.** When the first move of an
   iteration comes back 6 to 20 units (¼ to ¾ pawn) below the previous
   iteration's score, another move took over about half of the time,
   against 15% when the score held or rose. The drop is known with a
   median of 40% of T still left.
4. **A fail low is expensive to resolve.** When all root moves come back
   below the window (more than 20 units under the previous score), Psion
   searches them all again with alpha = −∞, from the old best. Of 67
   fail lows in the last iteration, 33 were cut off before that
   re-search finished even the first move.

## Proposal

Every search gets two times, both from the start of the search (from
`go`, or from `ponderhit`):

- the **target** S = c · T, the time a quiet search uses;
- the **maximum** H = min(m · T, r · remaining time), which is never
  exceeded.

The adapter then presses MOVE-NOW at the first of these moments:

1. **Soft stop at the start of an iteration.** `5A67` changes to a new
   iteration while the elapsed time is more than x · S. The previous
   iteration is complete, and the new one would most likely not get
   anywhere.
2. **Target reached, search stable.** The elapsed time reaches S and
   none of the conditions in 3 holds.
3. **Extended target reached.** The elapsed time reaches S · e, with e
   the largest factor that applies:
   - e = e_drop if the current iteration's first move came back at
     least d units below the previous iteration's score;
   - e = e_low if the current iteration failed low and the re-search
     has not yet produced a line (the move Psion would play is the one
     just refuted).
4. **Maximum reached.** The elapsed time reaches H.

**Minimum root moves** (added after the first measurements, see "What an
extension after a drop buys" in the search notes). The point of a new
iteration is to see whether another move takes over, and in finished
iterations the new best move came mostly among the first four or five
root moves, which cost little after the first. So when the target (S, or
S · e) is reached while the iteration has searched its first move but
fewer than N root moves in all, the search goes on until it has
searched N, or H is reached, or a new iteration starts. If the first
move (the previous best, searched again) is not done at the target, the
search stops as before: finishing it would mostly confirm the move Psion
already plays, and is the expensive case. With N = 4 this gained about
50 Elo over the soft stop alone (see "Results").

**After a fail low** (stage 2 implemented, see below). A fail low comes
only when all root moves have come back below the window, more than 20
units under the previous score: the position has just turned out worse
than Psion thought, the moment with the most risk of a blunder. Psion
then knows only upper bounds for its moves, and searches them all again
with alpha = −∞, first the old best (exactly, and so expensively), then
the others against its score (cheaply, unless one is better). Until that
first line comes, the move Psion would play is one just refuted, of
unknown score. Two stages:

1. While the fail low is open (no line from the re-search yet), go on up
   to the maximum H: `TM Fail Low Extension` set high (such as 300, with
   H = min(3 · T, 25% of the remaining time)). Without that line the
   choice is blind.
2. Once the line has come, treat the re-search as a new iteration for
   the minimum root moves: alternatives are more likely than usual, and
   the moves after the first are cheap. As all root moves were made in
   the first pass, the count of root moves starts again at the fail low,
   so `TM Min Moves` gives the re-search its N root moves too.

A fail low still open at S is rare: about 0.9% of the searches in
self-play at 10+0.1, 1.4% for Psion against Fairy-Max, and 1.5% for the
weaker side in self-play with one side at four times the time (see "How
often" in the search notes). So even long extensions cost little time
overall, but they also act in few searches, and any gain is hard to
measure.

A probe match against Fairy-Max at 10+0.1 (400 games, `TM Soft Stop`
40, `TM Fail Low Extension` 300, `TM Max` 300, `TM Max Share` 25, no
`TM Min Moves`) measured both stages. Psion lost most of the games, so
fail lows were common: 2,621 in 24,430 searches, but most of them came
early (median 49 ms after `go`) and showed the re-search's first line
after a median of 15 ms. Only 340 searches (1.4%) still had the fail low
open at S. Stage 1 waited for the line in 335 of them, a median of 51 ms
(27% of S) beyond S, and 5 were cut at H. But it never changed the move
(0 of 335): the first line is that of the old best move, and the search
stopped as soon as it came. When the line had come before S and the
search went on (297 cases, a median of 56 ms more), the move changed in
86 (29%), about twice as often as in an iteration without a fail low.
So stage 1 only pays with stage 2. Both together showed no effect in
self-play, where they act in too few searches (see "Results").

A fail high gets no extension: the move Psion would play is the one that
failed high, which scored at least beta.

Because the soft stop saves time in most searches (about 19% of the
time at x = 0.4 in the measurements), c can be above 1: the time saved
in quiet searches pays for the extensions and for a larger target. c
must be tuned so that the clock lasts the game; the share n of the
remaining time can be kept as it is.

### The options

One temporary UCI option per parameter, all spins, shares in percent.
The defaults give the time management as it was (S = H = T, no soft
stop, no extensions); the starting values are those proposed here:

| Parameter | Option | Default | Range | Start | Meaning |
|---|---|---|---|---|---|
| x | `TM Soft Stop` | 100 | 1–100 | 40 | soft stop: no new iteration after x · S |
| c | `TM Target` | 100 | 10–1000 | 120 | target S = c · T |
| d | `TM Drop` | 6 | 1–240 | 6 | drop (units, 24 per pawn) that counts as instability |
| e_drop | `TM Drop Extension` | 100 | 100–1000 | 160 | extension after a drop |
| e_low | `TM Fail Low Extension` | 100 | 100–1000 | 250 | extension after a fail low |
| m | `TM Max` | 100 | 100–1000 | 300 | maximum H, as a share of T |
| r | `TM Max Share` | 100 | 1–100 | 25 | maximum H, as a share of the remaining time |
| N | `TM Min Moves` | 0 | 0–100 | 4 | at the target, go on until N root moves are searched (0: off) |

As H caps everything, `TM Target` and the extensions only take effect
with `TM Max` raised as well. With `--verbose` the adapter logs why it
pressed MOVE-NOW: "Time up after N ms: target E%" (with ", drop" or ",
fail low" when extended), or "iteration NN started after N ms" for the
soft stop; with `TM Min Moves` it logs "Target after N ms with K root
moves searched" when it goes on, and ", min moves done" (or ", min
moves cut" if H came first) at the end. In 8 games against Fairy-Max
at 10+0.1 with the starting values, 226 searches ended by the soft stop, 208 at the target, 24
extended after a drop and 1 after a fail low, with no loss on time.

### When the rules apply

- With a clock (`wtime`/`btime`), after `go` or after `ponderhit`. While
  pondering nothing changes: Psion searches until `ponderhit` or `stop`.
  After `ponderhit`, the iteration that is running may have started
  before it; the soft stop only applies to iterations that start after
  `ponderhit`.
- Not with `go movetime`: the GUI asked for that time, so the adapter
  keeps to it (and Psion moves at the time asked, as now).
- Not with `go infinite`, nor when only depth, nodes or mate is given.
  Those limits still end the search together with the time, as now.
- The 50 ms minimum, the setup time and `Move Overhead` are treated as
  now: S, S · e and H replace T in the formula of implementation.md.

### What the adapter uses

All signals come from the write handlers in `nodes.cpp`:

- the start of an iteration: a change of `5A67` (also used for
  `depth`), seen at the next tick (10 ms);
- the first move's score in an iteration: the first PV copy at `7150`
  in that iteration, compared with the last one before it
  (`Nodes_FirstLineDrop()`; not when either is a mate score);
- a fail low: a write of 0 to `713A`, open until the next PV copy
  (`Nodes_FailLowOpen()`). In the probe matches `713A` was set to 0
  only by fail lows, from iteration `05` on;
- the root moves searched in the current iteration: the ply counter
  `7076` going from ply 1 (the root) to ply 2 makes a root move, which
  `5A6E` then holds; each is counted once per iteration
  (`Nodes_RootMovesDone()`, not counting the move being searched; the
  first counts as searched once its line has come, even before the
  next move is made). After a fail low all root moves are searched again
  with alpha = −∞, so the count starts again at the fail low.

## Measuring and tuning

1. **Temporary UCI options** (done, see "The options"). Remove them
   once the values are settled.
2. **Check the time use.** Log the time per move and the clock at the
   end of each game; the new rules must not lose on time, nor end games
   with much more time left than now.
3. **Test one rule at a time**, in this order, each against the current
   version:
   - the soft stop with c raised so that the total time use is about the
     same (expected to be the main gain: deeper last iterations, less
     waste);
   - the extension after a drop;
   - the extension after a fail low.
4. **Opponents.** Fail lows come mostly to the weaker side: against a
   stronger opponent (another engine, or Psion with more time) Psion
   fails low in about twice as many searches as in self-play, but a
   fail low open at S stays rare (1.5% against 0.9%, see "How often" in
   the search notes). UHO openings do not change that. Other engines,
   such as Fairy-Max, TSCP and Sargon, played in pairs, remain a check
   that a gain is not one of self-play only.
5. **Time controls.** 10+0.1 for the number of games, and a longer one
   such as 60+0.6 to check that the gain is not lost when Psion searches
   deeper. The bot's own time controls matter most.
6. **Number of games.** Any gain is likely to be some tens of Elo at
   most, so each step needs a few thousand games to show it; the error
   comes from the per-opening differences of the pairs.

Changes to time management are only kept if a match shows they gain.

## Results

### Minimum root moves

Self-play at 10+0.1 from `8moves_v3.pgn`, 1,000 pairs of games (each
opening with both colours), on two machines. The reference is the soft
stop alone (`TM Soft Stop` 40, everything else at the default); the test
adds `TM Min Moves` 4, with `TM Max` 200 and `TM Max Share` 75 for H. The
error is from the per-opening differences of the pairs (95%):

| Machine | Pairs | Pentanomial (0 to 2 points) | Score | Elo | Time per move (test / reference) |
|---|---|---|---|---|---|
| 1 | 373 | 31, 40, 167, 60, 75 | 57.2% | +51 ± 21 | 0.206 / 0.202 s |
| 2 | 627 | 25, 61, 333, 123, 85 | 57.3% | +51 ± 13 | 0.205 / 0.202 s |
| **Both** | **1000** | **56, 101, 500, 183, 160** | **57.3%** | **+51 ± 11** | **0.205 / 0.202 s** |

All 2,000 games ended normally, with no loss on time. The rule costs
about 1.5% more time per move. In the logs of the second machine, it
went on at the target in 12% of the searches (9,966 of about 81,500); 85%
of those reached 4 root moves, and 15% were cut off at H first. The time
spent on root moves 2 to 4 of an iteration whose first move is done is
where new best moves mostly come from (see "What an extension after a
drop buys" in the search notes), and it pays off far beyond its share of
the clock.

### After a fail low

The same kind of match, with both stages on top of the minimum root
moves: the test has `TM Min Moves` 4 with the count started again at a
fail low (stage 2) and `TM Fail Low Extension` 300 (stage 1), the
reference is the version of the match above (`TM Min Moves` 4 without
the reset). Both have `TM Soft Stop` 40, `TM Max` 200 and `TM Max
Share` 75, so with H = 2 · T the extension means "up to H":

| Machine | Pairs | Pentanomial (0 to 2 points) | Score | Elo | Time per move (test / reference) |
|---|---|---|---|---|---|
| 1 | 373 | 38, 73, 159, 65, 38 | 49.5% | −4 ± 19 | 0.204 / 0.203 s |
| 2 | 627 | 36, 49, 433, 64, 45 | 51.3% | +9 ± 11 | 0.204 / 0.204 s |
| **Both** | **1000** | **74, 122, 592, 129, 83** | **50.6%** | **+4 ± 10** | **0.204 / 0.204 s** |

All 2,000 games ended normally. No effect shows, and little could be
expected: the rules act only in the searches with a fail low still open
at S, about 0.9% of them in self-play (see "How often" in the search
notes). In the logs of the second machine, 148 of the test's 83,279
searches (0.18%) were still waiting for the line of a fail low when H
came. The rules cost nothing. A stronger opponent would make those
searches only about 1.7 times as common, so a test with clearly more
power is out of reach; the rules are kept because they cost nothing and
the measurements say they should help.
