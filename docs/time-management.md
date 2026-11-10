# Time management: a proposal

A proposal for better use of the clock by the adapter, based on the
measurements in [search-internals.md](search-internals.md#time-use-of-the-iterations).
None of this is implemented. All the numbers below are starting points,
to be measured and tuned. As always, Psion's search is not changed: the
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
4. **A fail low is expensive to resolve.** After a drop of more than 20
   units Psion first searches the same move again with alpha = −∞. Of 67
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

A fail high gets no extension: the move Psion would play is the one that
failed high, which scored at least beta.

Because the soft stop saves time in most searches (about 19% of the
time at x = 0.4 in the measurements), c can be above 1: the time saved
in quiet searches pays for the extensions and for a larger target. c
must be tuned so that the clock lasts the game; the share n of the
remaining time can be kept as it is.

Starting values:

| Parameter | Start | Meaning |
|---|---|---|
| x | 0.4 | soft stop: no new iteration after x · S |
| c | 1.2 | target S = c · T |
| d | 6 units (¼ pawn) | drop that counts as instability |
| e_drop | 1.6 | extension after a drop |
| e_low | 2.5 | extension after a fail low |
| m | 3 | maximum H, as a multiple of T |
| r | 0.25 | maximum H, as a share of the remaining time |

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

### What the adapter needs

All signals come from the write handlers in `nodes.cpp`, on pages it
already watches:

- the start of an iteration: `5A67` (already used for `depth`);
- the first move's score in an iteration: the first PV copy at `7150`
  after the iteration started, compared with the last one of the
  previous iteration (both already in the PV queue, with the iteration
  counter);
- a fail low: a write of 0 to `713A`; a fail high: a write of 0 to
  `704C` (new, but on the same page as `7150`).

## Measuring and tuning

1. **Implement behind temporary UCI options**, one per parameter, with
   defaults that give the current behaviour (x = 1, c = 1, e = 1,
   H = T). Remove the options once the values are settled.
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
4. **Opponents.** Self-play shows almost no fail lows (one in 80
   searches, see "How often" in the search notes), so it would test the
   extensions poorly. Use other engines instead, such as Fairy-Max, TSCP
   and Sargon, from the UHO openings, played in pairs.
5. **Time controls.** 10+0.1 for the number of games, and a longer one
   such as 60+0.6 to check that the gain is not lost when Psion searches
   deeper. The bot's own time controls matter most.
6. **Number of games.** Any gain is likely to be some tens of Elo at
   most, so each step needs a few thousand games to show it; the error
   comes from the per-opening differences of the pairs.

Changes to time management are only kept if a match shows they gain.
