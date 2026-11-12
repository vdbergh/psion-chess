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

**Minimum root moves: the window** (implemented, `TM Min Moves Window`,
+9 ± 10 Elo in a first match, +2 ± 7 in a second, see "Results"). In
about 1% of the searches the last iteration's first move has come back below the
window when the search stops (see "How often" in the search notes): the
old best has dropped by more than 20 units, but no line shows it, so
neither the drop extension nor the fail-low extension applies, and
Psion would play that move. With the window rule the search also goes
on, up to H, until a root move has come back inside the window: another
move with a new line, or, once all have failed and Psion has declared
the fail low, the first line of the search again (after which the N
root moves are counted from the fail low). A fail low open at the
target counts as a first move done, so the rule also covers what `TM
Fail Low Extension` was for.

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

**After a fail low: on to H** (implemented, `TM Fail Low Max`, +12 ± 7
Elo, see "Results"). After a fail low Psion's move ordering is probably
broken: the old best, searched first, has just been refuted, and the
moves that follow were ordered for a position Psion judged better. So
when the target is reached in an iteration that has failed low, whether
the re-search has produced its line or not, the search goes on until H,
or until a new iteration starts. `TM Fail Low Cap` m_low raises H for
such an iteration to min(m_low · T, r · remaining time), if m_low is
larger than m (some engines allow four times the usual time in an
emergency); it has not been measured yet.

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
| | `TM Min Moves Window` | 0 | 0–1 | 1 | 1: also go on until a root move has come back inside the window |
| | `TM Fail Low Max` | 0 | 0–1 | 1 | 1: at the target, an iteration that has failed low goes on until H |
| m_low | `TM Fail Low Cap` | 0 | 0–1000 | 0 | H for an iteration that has failed low, as a share of T, if larger than m (0: m) |

As H caps everything, `TM Target` and the extensions only take effect
with `TM Max` raised as well. With `--verbose` the adapter logs why it
pressed MOVE-NOW: "Time up after N ms: target E%" (with ", drop" or ",
fail low" when extended), or "iteration NN started after N ms" for the
soft stop; with `TM Min Moves` it logs "Target after N ms with K root
moves searched" when it goes on (with ", no line" when no move is
inside the window yet, ", fail low" when the fail low is open, and ",
failed low" when it has been resolved but `TM Fail Low Max` applies),
and ", min moves done" (or ", min moves cut" if H came first) at the
end. In 8 games against Fairy-Max at 10+0.1 with the starting values, 226 searches ended by the soft stop, 208 at the target, 24
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
  with alpha = −∞, so the count starts again at the fail low;
- whether a root move has come back inside the window: a line in the
  current iteration, or since the last fail low (`Nodes_HaveLine()`).

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
came. The rules cost nothing.

As fail lows come mostly to the weaker side, the same two versions
then played a gauntlet against a stronger opponent: each at 10+0.1
against the reference's version at 20+0.2 (about 100 Elo stronger),
from the same openings, each with both colours, on two machines. The
difference of the two is taken opening by opening (both versions played
each opening against the opponent, so the pairs of pairs match):

| Version | Pairs | Score against the opponent | Elo against the opponent | Time per move |
|---|---|---|---|---|
| Test (both stages) | 1000 | 35.6% | −103 ± 13 | 0.208 s |
| Reference | 1000 | 35.9% | −101 ± 13 | 0.206 s |
| **Test − reference** | **1000 openings** | **−0.4% ± 2.1%** | **−3 ± 16** | |

All 4,000 games ended normally. Against a stronger opponent no effect
shows either. The search notes say why ("How often", "In what
positions"): most of the fail lows that are seen come in positions
already lost, and those in level positions are the slow ones, mostly
cut off before Psion declares the fail low. As changes are only kept
when a match shows a gain, `TM Fail Low Extension` is not to be kept;
the minimum root moves with the window (above) are tested instead, as
they let more of the slow first passes finish.

### The window

The same kind of match again: the test has the window rule (`TM Min
Moves Window` 1) on top of both changes above (stage 2, and the count
of root moves done that also takes the first line of the search into
account); the reference is again the version of the first match
(`TM Min Moves` 4 without the reset). Both have `TM Soft Stop` 40, `TM
Min Moves` 4, `TM Max` 200 and `TM Max Share` 75, and neither uses `TM
Fail Low Extension`:

| Machine | Pairs | Pentanomial (0 to 2 points) | Score | Elo | Time per move (test / reference) |
|---|---|---|---|---|---|
| 1 | 373 | 37, 48, 188, 61, 39 | 51.1% | +8 ± 19 | 0.206 / 0.205 s |
| 2 | 627 | 29, 61, 421, 80, 36 | 51.3% | +9 ± 11 | 0.204 / 0.203 s |
| **Both** | **1000** | **66, 109, 609, 141, 75** | **51.25%** | **+9 ± 10** | **0.205 / 0.204 s** |

All 2,000 games ended normally. The result is borderline (LOS 96%), and
it comes from the whole package, not from the window rule alone, though
the match for stage 2 above (+4 ± 10) suggests most of it is the window.
The rule costs about 0.5% more time per move. In the logs of the second
machine, the test went on at the target for the window in 1,545 of its
about 84,000 searches (1.8%), 632 of them with the fail low already
open; 259 of those 632 were cut off at H, the other 373 came to an end
before it. The reference stopped with a fail low open in 568 searches,
most of them at the target, playing the old best without knowing how
far it had dropped.

As adding games to a match after seeing a borderline result would bias
it, a second match of 2,000 pairs with new openings was run as an
independent check (twice as long, for more power), and the decision
rests on that one:

| Machine | Pairs | Pentanomial (0 to 2 points) | Score | Elo | Time per move (test / reference) |
|---|---|---|---|---|---|
| 1 | 746 | 75, 115, 379, 108, 69 | 49.4% | −4 ± 13 | 0.205 / 0.204 s |
| 2 | 1254 | 67, 124, 845, 138, 80 | 50.8% | +6 ± 8 | 0.205 / 0.203 s |
| **Both** | **2000** | **142, 239, 1224, 246, 149** | **50.26%** | **+2 ± 7** | **0.205 / 0.203 s** |

All games but one ended normally: in one, Psion declared a draw by
repetition that ignored the castling rights (see the README), and the
adapter sent `0000`. The second match shows no gain (LOS 70%), so the
window is not shown to gain on its own.

### On to H after a fail low

The window package of the matches above, plus `TM Fail Low Max` 1 (at
the target, an iteration that has failed low goes on until H), against
the same reference (`TM Min Moves` 4 without the reset). Both have `TM
Soft Stop` 40, `TM Min Moves` 4, `TM Max` 200 and `TM Max Share` 75, so
H = 2 · T:

| Machine | Pairs | Pentanomial (0 to 2 points) | Score | Elo | Time per move (test / reference) |
|---|---|---|---|---|---|
| 1 | 746 | 73, 100, 358, 129, 86 | 51.8% | +13 ± 13 | 0.204 / 0.203 s |
| 2 | 1254 | 58, 131, 820, 159, 86 | 51.7% | +12 ± 8 | 0.206 / 0.204 s |
| **Both** | **2000** | **131, 231, 1178, 288, 172** | **51.74%** | **+12 ± 7** | **0.205 / 0.204 s** |

All 4,000 games ended normally (LOS above 99.9%). The rule costs about
0.5% more time per move. As the window alone showed +2 ± 7, most of the gain
is that of going on to H. In the logs of the second machine, the test
went on at the target after a fail low in 2,460 of its 172,475 searches
(1.4%): 1,363 with the fail low still open, of which 551 (40%) were cut
off at H, and 1,097 after the line had come, of which 694 (63%) reached
H.

The gain does not seem to come from playing fewer moves that had failed
low. In the same logs, the test played such a move in 1,086 searches (0.63%):
551 with the fail low still open, and 535 where the first move had come
back below the window but the search stopped before Psion declared the
fail low. The reference played one with the fail low open in 1,151
searches (0.67%); its second kind does not show in its logs. In both,
about 27% of the games had such a move (for the test, 6% had one in the
first 30 moves). The gain more likely comes from the extra time itself:
after a fail low the position is uncertain, and the move chosen with up
to twice the time is better more often.

This version (`TM Soft Stop` 40, `TM Min Moves` 4, `TM Min Moves Window`
1, `TM Fail Low Max` 1, `TM Max` 200, `TM Max Share` 75) is the
reference for the next matches.
