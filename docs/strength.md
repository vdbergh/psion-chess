# Strength

Matches with [fastchess](https://github.com/Disservin/fastchess), on the
i5-1135G7, with the balanced book `8moves_v3.pgn` (each opening played
with both colours); Elo with 95% confidence intervals, from the
pentanomial statistics.

| Match | Time control | Games | Result |
|---|---|---|---|
| Full speed against `--cycles 2000` (about 90 times slower) | 120+1.2 | 100 | +576 ± 192 Elo (94 wins, 1 loss, 5 draws) |
| Full speed against `--cycles 48000` (about 3.6 times slower) | 120+1.2 | 300 | +170 ± 35 Elo |
| Against micro-Max 4.8 (CCRL Blitz 1855) | 120+1 | 300 | −154 ± 38 Elo |
| Pondering against not pondering (book `UHO_4060_v4.epd`)\* | 30+0.3 | 100 | +70 ± 56 Elo |
| Pondering and not pondering, each against micro-Max 4.8\*\* | 30+0.3 | 400 | pondering −162 ± 50, not pondering −217 ± 48; difference +55 ± 57 Elo |

\* Not with fastchess, which does not support pondering, but through
python-chess, which sends `ponderhit` (or `stop` and the new position) as
lichess-bot does. 59% of the moves pondered on were played.

\*\* Also through python-chess. Both versions played the same 100
openings, each with both colours; the error of the difference is computed
from the differences per opening (the two versions' results correlate).
Against micro-Max, 48.5% of the moves pondered on were played.

- Psion gains about 90 Elo per doubling of the speed (1.83 doublings in
  the second match). So the engine is now some hundreds of Elo stronger
  than on the hardware of its time.
- Against micro-Max, Psion rates about 1700 on the CCRL Blitz scale. (Five
  of its 52 wins came from under-promotions to a knight, which micro-Max
  cannot represent: it resigns, also under WinBoard at CCRL. Without
  them the result would be about −167.) For comparison, the Swedish
  rating list (SSDF) has Psion on an 8 MHz Atari at 1791, a list once
  calibrated against human players; CCRL ratings are generally taken to
  be some hundreds of Elo below human ratings.
- Pondering gains about 55 to 70 Elo, about as much as 1.5 to 1.7 times
  the speed would (at 90 Elo per doubling); less against another engine,
  whose moves Psion predicts less often than its own. A move pondered on
  in vain costs about 0.05 seconds (the takebacks), which the time gained
  on the others more than makes up for.
- All these games in one rating list, computed with
  [Ordo](https://github.com/michiguel/Ordo) (errors are 95% intervals,
  relative to the anchor). The time controls are kept apart: Psion and
  micro-Max at 30+0.3 are other players than at 120+1. Both groups are
  anchored at micro-Max's CCRL Blitz rating of 1855, which CCRL measures
  at about 120+1, so for 30+0.3 this is only an approximation. Matches of
  identical engines are left out.

  | 120+1 (700 games) | Rating | Error |
  |---|---|---|
  | micro-Max 4.8 | 1855 | anchor |
  | Psion | 1699 | ± 38 |
  | Psion, 3.6 times slower | 1527 | ± 52 |
  | Psion, 90 times slower | 1117 | ± 163 |

  | 30+0.3 (500 games) | Rating | Error |
  |---|---|---|
  | micro-Max 4.8 | 1855 | anchor |
  | Psion, pondering | 1695 | ± 40 |
  | Psion | 1631 | ± 42 |

  In the second list the difference between pondering and not pondering
  is 64 ± 44 Elo (Ordo, from all 500 games, as independent games).
- For `UCI_Elo`, Psion at fixed speeds against two weaker engines on the
  CCRL Blitz list, TSCP 1.83b (1611) and Sargon 1978 1.01b (1167), at
  120+1 with `8moves_v3.pgn`, 300 games per match, four at a time. CCRL
  plays on an i7-4770K and scales the time control to each tester's
  machine by Stockfish 10's `bench`\*\*; here it gives about 2.1 million
  nodes per second (four at a time) against 1.92 million on the 4770K, so
  the opponents had about 1.1 times their CCRL time, worth some 8 Elo, and
  their ratings are raised by that much. Psion at a fixed speed does not
  depend on the host, as long as the host keeps up. Rated with Ordo,
  with TSCP, Sargon and micro-Max as loose anchors (± 21):

  | Psion | Opponent | Score | Rating | Error |
  |---|---|---|---|---|
  | 2,500 cycles | Sargon | 43.5% | 1122 | ± 51 |
  | 16,000 cycles | TSCP and Sargon | 20.0% and 79.0% | 1392 | ± 40 |
  | 100,000 cycles | TSCP | 43.3% | 1580 | ± 50 |
  | 187,000 cycles set, about 120,000 reached\* | TSCP | 52.7% (295 games) | 1646 | ± 50 |
  | `highest` (the micro-Max match above) | micro-Max | 29.2% | 1707 | ± 55 |

  A line through the first three gives Elo = 1597 + 86 × log2(cycles /
  100,000), which `UCI_Elo` uses; the other two fit it within their
  errors (`highest` is about 190,000 to 230,000 cycles here).

  \* With four games at a time the i5-1135G7 could not keep up with
  187,000 cycles in long searches: the search depth reached corresponds
  to about 120,000. It does keep up with 100,000.

  \*\* The reference: Stockfish 10's `bench` without arguments (16 MB
  hash, 1 thread, depth 13, the built-in positions) takes 2,054 ms for
  3,939,338 nodes on the i7-4770K. A time control is scaled by the bench
  time on one's own machine divided by 2,054 ms (40/15 becomes 40 in
  15 minutes times that factor), with the bench run under the same load
  as the games, e.g. four at a time. From CCRL's announcement "Modernising
  Our Lists Stage 2" (January 2020).
- Psion has no transposition table: during a search only about 4 KB of
  memory changes, in small areas such as move lists, and nothing that
  fills up as the search proceeds. Every search starts afresh, which
  costs it most in endgames. Compared with native engines it is also
  handicapped by the emulation: the emulated PC is much slower than a
  native program on the same machine.
