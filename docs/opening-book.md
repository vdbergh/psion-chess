# Psion's opening book

Psion Chess 2.13 has a small opening book of 3,691 moves in 759 lines,
at most 23 plies long. It is built into the engine overlay `Chess2.ovl`
(not `Psion.grt`, which Psion does not even open while it runs). This
note describes its format and how Psion uses it; the whole book follows
at the end, and is also in [psion-book.pgn](psion-book.pgn).
`tools/psion_book.py` writes both from `Chess2.ovl`.

## Where it is

In the load image of `Chess2.ovl` (the file after its 512-byte header),
from offset `A4DC` to `C1C0`, right after the evaluation tables. In
memory it is at `DS:095C` (with `DS` = `0B2E`, as for the other data in
[search-internals.md](search-internals.md)). The lookup routine is at
`CS:3D29` (`CS` = `0259`).

## Format

Each entry is a move in two bytes: the to-square, then the from-square.
A square is its offset on Psion's search board (see
[search-internals.md](search-internals.md)), `(row * 16 + file) * 2`
with row 0 = rank 8, so e.g. `C8` is e2 and `88` is e4. Such an offset
never has bits `01` and `10` set, so these carry flags (the code masks
moves with `EEEE`):

| Byte | Bit | Meaning |
|---|---|---|
| from | `10` | A sibling follows after this move's subtree: the position before this move is pushed |
| from | `01` | The line ends after this move: the last pushed position is popped |
| to | `01` | Psion recognises the move, but never plays it itself |
| to | `10` | Psion prefers the move |

The book is one tree in depth-first order. The first entries, `88 D8`
`64 34` `BA FC` `44 12`, read 1.e4 (with a sibling) c5 (sibling) 2.Nf3
(preferred, sibling) Nc6 (sibling); the first line ends with 9...exd5
(`66 49`: d5 from e6, end of line). The 1.e4 lines come first (1...c5,
1...e5, 1...e6, 1...c6, then the replies Psion does not play), then the
other first moves.

408 moves are only recognised, 50 are preferred. Promotions do not
occur. 17 moves occur twice from the same position, in different
places of the tree, sometimes with different flags (3.d4 after 1.e4 c5
2.Nf3 d6 is preferred in one place and only recognised in the other);
the dump keeps both.

One entry is impossible: `26 F2` at `B268` would be b1–d7, in the
position after 1.e4 c6 2.d4 d5 3.Nc3 dxe4 4.Nxe4 Nf6 5.Nxf6+ exf6 6.Bc4,
as a sibling of 6...Qe7+. Probably 6...Nd7 (b8–d7 would be `26 12`) was
meant, but its continuation 7.Be2 Bd6 8.O-O O-O does not fit either
(the bishop is on c4). Psion never finds this move among the legal
moves, so it is never played; in the position Psion plays only
6...Qe7+. The dump leaves it out, with the 7 entries of its subtree.

## How Psion uses it

Read from the code (`3D29`..`3E50`) and checked by playing:

- The book is used only while the ply counter at `7260` is at most 24
  (the longest line has 23 plies) and the word at `7256` is 0. After a
  position set up from a FEN (other than the start position, for which
  the adapter starts a new game) Psion does not use the book: set up
  after 1.e4, it searched instead of answering from the book.
- A book move counts as played if it occurs anywhere among the game's
  moves so far (the list at `7276`), in any order, so the lines also
  match transpositions. The routine then checks further
  (`3DB2`..`3DEB`); not decoded.
- A move flagged "recognised only" is skipped as a choice (`3DAD`). For
  a preferred move 2 is added to a value used in the selection
  (`3DF5`). The book moves found are moved in the list of generated
  moves at `[2642]` (`3E00`..`3E26`); how the move is then chosen was not
  decoded.
- Psion chooses at random among the moves it may play. The frequencies
  fit this rule: going through those moves in book order, take each with
  probability 1/2 (3/4 if preferred), the last one with what is left.
  In 60 tries each:

  | Position | Moves (^ = preferred) | Rule | Seen |
  |---|---|---|---|
  | Start | e4, d4, c4, Nf3 | 30, 15, 7.5, 7.5 | 27, 14, 10, 9 |
  | 1.d4 | d5^, Nf6 | 45, 15 | 47, 13 |
  | 1.e4 e5 | Nf3^, Nc3, Bc4 | 45, 7.5, 7.5 | 45, 6, 9 |

- A book move is reported with `info depth 1 time T nodes 1 pv` and the
  move, without a score.

## Overview

First moves (~ = recognised only, ^ = preferred), and the replies to
the four Psion plays:

| | Moves |
|---|---|
| Start | e4 d4 c4 Nf3 b3~ f4~ g3~ Nc3~ d3~ e3~ a3~ h3~ a4~ f3~ c3~ h4~ b4~ |
| 1.e4 | c5 e5 e6 c6 Nf6~ d5~ Nc6~ g6~ d6~ b6~ a6~ |
| 1.d4 | d5^ Nf6 f5~ c5~ a6~ e6~ d6~ Nc6~ |
| 1.c4 | e5 c5 Nf6 e6 g6~ b6~ Nc6~ |
| 1.Nf3 | d5 Nf6 f5~ c5~ Nc6~ g6~ e6~ d6~ |

Lines by first move: 1.e4 366, 1.d4 283, 1.c4 50, 1.Nf3 39, the
others 21 (mostly a single reply). The moves Psion does not play
are mostly there to recognise the opponent's choice, with a short
answer.

## The book

All 759 lines, in book order (`python3 tools/psion_book.py --lines`),
with ~ after a move Psion only recognises and ^ after one it prefers.
In [psion-book.pgn](psion-book.pgn) the same tree is one game with
variations, with ?! for "recognised only" and ! for "preferred".

<details>
<summary>All lines</summary>

```
1.e4 c5 2.Nf3^ Nc6 3.d4^ cxd4 4.Nxd4 Nf6^ 5.Nc3 e6 6.Ndb5 Bb4 7.a3 Bxc3+ 8.Nxc3 d5 9.exd5 exd5
1.e4 c5 2.Nf3^ Nc6 3.d4^ cxd4 4.Nxd4 Nf6^ 5.Nc3 e6 6.Ndb5 d6 7.Bf4 e5 8.Bg5
1.e4 c5 2.Nf3^ Nc6 3.d4^ cxd4 4.Nxd4 Nf6^ 5.Nc3 e6 6.Nxc6 bxc6 7.e5 Nd5
1.e4 c5 2.Nf3^ Nc6 3.d4^ cxd4 4.Nxd4 Nf6^ 5.Nc3 e6 6.Be2~ Bb4 7.O-O Bxc3
1.e4 c5 2.Nf3^ Nc6 3.d4^ cxd4 4.Nxd4 Nf6^ 5.Nc3 e5 6.Ndb5 d6 7.Bg5 a6 8.Na3 b5 9.Nd5 Be7 10.Bxf6 Bxf6
1.e4 c5 2.Nf3^ Nc6 3.d4^ cxd4 4.Nxd4 Nf6^ 5.Nc3 e5 6.Ndb5 d6 7.Bg5 a6 8.Na3 b5 9.Bxf6 Qxf6
1.e4 c5 2.Nf3^ Nc6 3.d4^ cxd4 4.Nxd4 Nf6^ 5.Nc3 e5 6.Ndb5 d6 7.Bg5 Be6~ 8.Nd5 Rc8 9.Bxf6
1.e4 c5 2.Nf3^ Nc6 3.d4^ cxd4 4.Nxd4 Nf6^ 5.Nc3 e5 6.Ndb5 d6 7.Nd5 Nxd5 8.exd5 Ne7 9.c4
1.e4 c5 2.Nf3^ Nc6 3.d4^ cxd4 4.Nxd4 Nf6^ 5.Nc3 e5 6.Ndb5 d6 7.a4~ Nb4
1.e4 c5 2.Nf3^ Nc6 3.d4^ cxd4 4.Nxd4 Nf6^ 5.Nc3 e5 6.Nde2 Bc5 7.Ng3 d6 8.Be2 Be6 9.O-O a6
1.e4 c5 2.Nf3^ Nc6 3.d4^ cxd4 4.Nxd4 Nf6^ 5.Nc3 e5 6.Nxc6~ bxc6 7.Bc4 Bb4
1.e4 c5 2.Nf3^ Nc6 3.d4^ cxd4 4.Nxd4 Nf6^ 5.Nc3 e5 6.Nf5~ d5 7.exd5 Bxf5
1.e4 c5 2.Nf3^ Nc6 3.d4^ cxd4 4.Nxd4 Nf6^ 5.Nc3 e5 6.Nf3~ Bb4 7.Bc4 Nxe4
1.e4 c5 2.Nf3^ Nc6 3.d4^ cxd4 4.Nxd4 Nf6^ 5.Nc3 d6 6.Bg5 e6 7.Qd2 a6 8.O-O-O
1.e4 c5 2.Nf3^ Nc6 3.d4^ cxd4 4.Nxd4 Nf6^ 5.Nc3 d6 6.Bc4 e6 7.Be3 Be7
1.e4 c5 2.Nf3^ Nc6 3.d4^ cxd4 4.Nxd4 Nf6^ 5.Nc3 d6 6.Be2 e5 7.Nf3 h6
1.e4 c5 2.Nf3^ Nc6 3.d4^ cxd4 4.Nxd4 e5 5.Nb5 a6 6.Nd6+ Bxd6 7.Qxd6 Qf6 8.Qd1 Qg6
1.e4 c5 2.Nf3^ Nc6 3.d4^ cxd4 4.Nxd4 e5 5.Nb5 d6~ 6.c4
1.e4 c5 2.Nf3^ Nc6 3.d4^ cxd4 4.Nxd4 e5 5.Nxc6~ bxc6 6.Bc4 Ba6
1.e4 c5 2.Nf3^ Nc6 3.d4^ cxd4 4.Nxd4 e6 5.Nb5 d6 6.c4 Nf6 7.N1c3 a6 8.Na3 Be7 9.Be2
1.e4 c5 2.Nf3^ Nc6 3.d4^ cxd4 4.Nxd4 e6 5.Nc3 a6 6.Nxc6 bxc6
1.e4 c5 2.Nf3^ Nc6 3.d4^ cxd4 4.Nxd4 g6~ 5.c4 Bg7 6.Be3 Nf6 7.Nc3
1.e4 c5 2.Nf3^ Nc6 3.d4^ cxd4 4.Nxd4 g6~ 5.c4 Nf6 6.Nc3 Nxd4 7.Qxd4
1.e4 c5 2.Nf3^ Nc6 3.d4^ cxd4 4.Nxd4 g6~ 5.Nc3 Bg7 6.Be3 Nf6 7.Bc4 Qa5
1.e4 c5 2.Nf3^ Nc6 3.Bb5 Nf6 4.Nc3 Nd4 5.e5 Nxb5 6.Nxb5 Nd5 7.O-O a6 8.c4 Nb6
1.e4 c5 2.Nf3^ Nc6 3.Bb5 Nf6 4.Nc3 Nd4 5.e5 Nxb5 6.exf6~ Nxc3 7.fxg7
1.e4 c5 2.Nf3^ Nc6 3.Bb5 Nf6 4.Nc3 a6~ 5.Bxc6 dxc6 6.O-O
1.e4 c5 2.Nf3^ Nc6 3.Bb5 Nf6 4.e5 Nd5 5.Nc3 Nc7
1.e4 c5 2.Nf3^ Nc6 3.Bb5 Nf6 4.e5 Ng4~ 5.Bxc6 dxc6 6.O-O
1.e4 c5 2.Nf3^ Nc6 3.Bb5 Nf6 4.Qe2~ e5
1.e4 c5 2.Nf3^ Nc6 3.Bb5 Nf6 4.Bxc6~ dxc6 5.Nc3 Bg4 6.h3 Bh5 7.d3 e6
1.e4 c5 2.Nf3^ Nc6 3.Bb5 e6 4.O-O Nge7 5.Re1 a6 6.Bxc6 Nxc6 7.d4
1.e4 c5 2.Nf3^ Nc6 3.Bb5 e6 4.O-O Nge7 5.c3 a6 6.Ba4 d5
1.e4 c5 2.Nf3^ Nc6 3.Bb5 e6 4.O-O Nge7 5.b3~ a6
1.e4 c5 2.Nf3^ Nc6 3.Bb5 e6 4.Bxc6 bxc6 5.O-O Ne7
1.e4 c5 2.Nf3^ Nc6 3.Bb5 e6 4.Nc3 Nge7 5.O-O a6
1.e4 c5 2.Nf3^ Nc6 3.Bb5 g6~ 4.c3 Nf6 5.Qe2
1.e4 c5 2.Nf3^ Nc6 3.Bb5 g6~ 4.c3 a6 5.Bxc6 dxc6 6.d3
1.e4 c5 2.Nf3^ Nc6 3.Bb5 g6~ 4.c3 Bg7 5.d4
1.e4 c5 2.Nf3^ Nc6 3.Bb5 g6~ 4.c3 Qb6 5.Na3 Bg7 6.O-O
1.e4 c5 2.Nf3^ Nc6 3.Bb5 Qb6~ 4.a4 e6 5.O-O
1.e4 c5 2.Nf3^ Nc6 3.Bb5 Qc7~ 4.c3
1.e4 c5 2.Nf3^ d6 3.d4^ cxd4 4.Nxd4 Nf6 5.Nc3 Nc6 6.Be2 e5 7.Nb3 Be7 8.O-O O-O
1.e4 c5 2.Nf3^ d6 3.d4^ cxd4 4.Nxd4 Nf6 5.Nc3 Nc6 6.Be3 e5 7.Ndb5 a6 8.Na3
1.e4 c5 2.Nf3^ d6 3.d4^ cxd4 4.Nxd4 Nf6 5.Nc3 Nc6 6.Bc4 Qb6 7.Nb3 e6 8.O-O
1.e4 c5 2.Nf3^ d6 3.d4^ cxd4 4.Nxd4 Nf6 5.Nc3 Nc6 6.Bc4 e6 7.Be3 Be7 8.Qe2
1.e4 c5 2.Nf3^ d6 3.d4^ cxd4 4.Nxd4 Nf6 5.Nc3 Nc6 6.Bg5 e6 7.Qd2 Be7
1.e4 c5 2.Nf3^ d6 3.d4^ cxd4 4.Nxd4 Nf6 5.Nc3 a6 6.Be2 e5 7.Nb3 Be7 8.O-O Be6
1.e4 c5 2.Nf3^ d6 3.d4^ cxd4 4.Nxd4 Nf6 5.Nc3 a6 6.Bc4 e6 7.Bb3 b5 8.O-O Be7
1.e4 c5 2.Nf3^ d6 3.d4^ cxd4 4.Nxd4 Nf6 5.Nc3 a6 6.Bg5 e6 7.f4 Be7
1.e4 c5 2.Nf3^ d6 3.d4^ cxd4 4.Nxd4 Nf6 5.Nc3 e6 6.Be2 Be7
1.e4 c5 2.Nf3^ d6 3.d4^ cxd4 4.Nxd4 Nf6 5.Nc3 g6 6.Be2^ Bg7 7.Be3 Nc6 8.O-O O-O 9.Nb3
1.e4 c5 2.Nf3^ d6 3.d4^ cxd4 4.Nxd4 Nf6 5.Nc3 g6 6.Be3 Bg7 7.Be2 O-O
1.e4 c5 2.Nf3^ d6 3.d4^ cxd4 4.Nxd4 Nf6 5.Nc3 g6 6.f4~ Nc6
1.e4 c5 2.Nf3^ d6 3.Bb5+ Nd7 4.Bxd7+ Qxd7 5.c4
1.e4 c5 2.Nf3^ d6 3.Bb5+ Nc6 4.c3
1.e4 c5 2.Nf3^ d6 3.Bb5+ Bd7 4.Bxd7+ Qxd7 5.O-O
1.e4 c5 2.Nf3^ d6 3.d4~ cxd4 4.Qxd4 Nc6
1.e4 c5 2.Nf3^ d6 3.Bc4~ e5 4.O-O Nc6
1.e4 c5 2.Nf3^ e6 3.d4 cxd4 4.Nxd4 Nf6 5.Nc3 d6^ 6.Be2 a6 7.O-O Qc7
1.e4 c5 2.Nf3^ e6 3.d4 cxd4 4.Nxd4 Nf6 5.Nc3 d6^ 6.f4~ Nc6 7.Be3 Be7
1.e4 c5 2.Nf3^ e6 3.d4 cxd4 4.Nxd4 Nf6 5.Nc3 d6^ 6.g4~ a6
1.e4 c5 2.Nf3^ e6 3.d4 cxd4 4.Nxd4 Nf6 5.Nc3 d6^ 6.g3~ Be7 7.Bg2 O-O
1.e4 c5 2.Nf3^ e6 3.d4 cxd4 4.Nxd4 Nf6 5.Nc3 Nc6 6.Ndb5 Bb4
1.e4 c5 2.Nf3^ e6 3.d4 cxd4 4.Nxd4 Nc6 5.Nc3 Qc7 6.Be3 a6
1.e4 c5 2.Nf3^ e6 3.d4 cxd4 4.Nxd4 Nc6 5.Nb5 d6 6.Bf4 e5 7.Be3
1.e4 c5 2.Nf3^ e6 3.d4 cxd4 4.Nxd4 a6 5.Bd3^ Bc5 6.Nb3 Bb6 7.O-O Ne7 8.Qe2 Nbc6 9.Be3 Ne5
1.e4 c5 2.Nf3^ e6 3.d4 cxd4 4.Nxd4 a6 5.Nc3 Qc7 6.Be2 b5 7.Bf3
1.e4 c5 2.Nf3^ e6 3.d4 cxd4 4.Nxd4 a6 5.c4 Nf6 6.Nc3 Bb4
1.e4 c5 2.Nf3^ Nf6~ 3.e5 Nd5 4.Nc3 e6 5.Nxd5
1.e4 c5 2.Nf3^ d5~ 3.exd5 Qxd5 4.Nc3
1.e4 c5 2.Nf3^ a6~ 3.c4 Nc6 4.d4 cxd4
1.e4 c5 2.c3 d5^ 3.exd5 Qxd5 4.d4^ e6 5.Nf3 Nf6 6.Bd3 cxd4 7.cxd4 Nc6
1.e4 c5 2.c3 d5^ 3.exd5 Qxd5 4.d4^ e6 5.Nf3 Nf6 6.Bd3 Nc6~ 7.O-O
1.e4 c5 2.c3 d5^ 3.exd5 Qxd5 4.Nf3 e6 5.d4
1.e4 c5 2.c3 Nf6 3.e5 Nd5 4.d4 cxd4 5.cxd4 d6 6.Nf3 Nc6 7.Bc4 Nb6
1.e4 c5 2.c3 Nf6 3.d4~ cxd4
1.e4 c5 2.c3 e6~ 3.d4 d5 4.exd5
1.e4 c5 2.Nc3~ Nc6 3.g3 g6 4.Bg2 Bg7 5.d3 d6
1.e4 c5 2.Nc3~ Nc6 3.f4 e6 4.Nf3 d5 5.Bb5 Ne7
1.e4 c5 2.Nc3~ Nc6 3.Nf3 Nf6
1.e4 c5 2.d4~ cxd4 3.c3 dxc3
1.e4 c5 2.d4~ cxd4 3.Nf3~ Nc6
1.e4 c5 2.f4~ e6 3.Nf3 d5
1.e4 c5 2.f4~ e6 3.Bb5~ a6
1.e4 c5 2.d3~ e5
1.e4 c5 2.Bb5~ a6 3.Bc4 Nc6
1.e4 c5 2.b4~ cxb4 3.Bb2 Nf6
1.e4 c5 2.b3~ e6 3.Bb2 Nf6
1.e4 c5 2.g3~ d5 3.exd5 Qxd5 4.Nf3 Bg4
1.e4 c5 2.Ne2~ Nc6 3.Nbc3 Nf6
1.e4 c5 2.Bc4~ Nc6
1.e4 e5 2.Nf3^ Nc6 3.Bb5 a6 4.Ba4 Nf6^ 5.O-O^ Be7 6.Re1^ b5 7.Bb3 d6 8.c3 O-O 9.h3^ Na5 10.Bc2 c5 11.d4
1.e4 e5 2.Nf3^ Nc6 3.Bb5 a6 4.Ba4 Nf6^ 5.O-O^ Be7 6.Re1^ b5 7.Bb3 d6 8.c3 O-O 9.h3^ h6 10.d4 Re8 11.Nbd2
1.e4 e5 2.Nf3^ Nc6 3.Bb5 a6 4.Ba4 Nf6^ 5.O-O^ Be7 6.Re1^ b5 7.Bb3 d6 8.c3 O-O 9.h3^ Bb7~ 10.d4
1.e4 e5 2.Nf3^ Nc6 3.Bb5 a6 4.Ba4 Nf6^ 5.O-O^ Be7 6.Re1^ b5 7.Bb3 d6 8.c3 O-O 9.h3^ Nd7~ 10.d4
1.e4 e5 2.Nf3^ Nc6 3.Bb5 a6 4.Ba4 Nf6^ 5.O-O^ Be7 6.Re1^ b5 7.Bb3 d6 8.c3 O-O 9.h3^ Nb8~ 10.d4
1.e4 e5 2.Nf3^ Nc6 3.Bb5 a6 4.Ba4 Nf6^ 5.O-O^ Be7 6.Re1^ b5 7.Bb3 d6 8.c3 O-O 9.d4 Bg4 10.Be3 exd4
1.e4 e5 2.Nf3^ Nc6 3.Bb5 a6 4.Ba4 Nf6^ 5.O-O^ Be7 6.Re1^ b5 7.Bb3 d6 8.c3 O-O 9.h4~ Bg4
1.e4 e5 2.Nf3^ Nc6 3.Bb5 a6 4.Ba4 Nf6^ 5.O-O^ Be7 6.Re1^ b5 7.Bb3 O-O 8.a4 Bb7 9.d3 d6 10.Nc3
1.e4 e5 2.Nf3^ Nc6 3.Bb5 a6 4.Ba4 Nf6^ 5.O-O^ Be7 6.Re1^ b5 7.Bb3 O-O 8.a4 Bb7 9.c3~ d5
1.e4 e5 2.Nf3^ Nc6 3.Bb5 a6 4.Ba4 Nf6^ 5.O-O^ Be7 6.Re1^ b5 7.Bb3 O-O 8.a4 b4~ 9.d4
1.e4 e5 2.Nf3^ Nc6 3.Bb5 a6 4.Ba4 Nf6^ 5.O-O^ Be7 6.Re1^ b5 7.Bb3 O-O 8.a4 Ra7~ 9.axb5
1.e4 e5 2.Nf3^ Nc6 3.Bb5 a6 4.Ba4 Nf6^ 5.O-O^ Be7 6.Re1^ b5 7.Bb3 O-O 8.c3 d5 9.exd5 Nxd5 10.Nxe5 Nxe5 11.Rxe5 c6
1.e4 e5 2.Nf3^ Nc6 3.Bb5 a6 4.Ba4 Nf6^ 5.O-O^ Be7 6.Re1^ b5 7.Bb3 O-O 8.c3 d5 9.exd5 e4~ 10.dxc6 exf3 11.d4
1.e4 e5 2.Nf3^ Nc6 3.Bb5 a6 4.Ba4 Nf6^ 5.O-O^ Be7 6.Re1^ b5 7.Bb3 O-O 8.c3 d5 9.d4~ exd4
1.e4 e5 2.Nf3^ Nc6 3.Bb5 a6 4.Ba4 Nf6^ 5.O-O^ Be7 6.Re1^ b5 7.Bb3 O-O 8.d4~ d6 9.c3 Bg4
1.e4 e5 2.Nf3^ Nc6 3.Bb5 a6 4.Ba4 Nf6^ 5.O-O^ Be7 6.Re1^ b5 7.Bb3 O-O 8.a4~ Bb7 9.d3 d6
1.e4 e5 2.Nf3^ Nc6 3.Bb5 a6 4.Ba4 Nf6^ 5.O-O^ Be7 6.Re1^ O-O~ 7.Bxc6
1.e4 e5 2.Nf3^ Nc6 3.Bb5 a6 4.Ba4 Nf6^ 5.O-O^ Be7 6.d4 exd4 7.e5 Ne4 8.Nxd4 O-O
1.e4 e5 2.Nf3^ Nc6 3.Bb5 a6 4.Ba4 Nf6^ 5.O-O^ Be7 6.d4 exd4 7.Re1 O-O 8.e5 Ne8
1.e4 e5 2.Nf3^ Nc6 3.Bb5 a6 4.Ba4 Nf6^ 5.O-O^ Be7 6.d4 exd4 7.Re1 b5~ 8.e5 Nxe5 9.Rxe5
1.e4 e5 2.Nf3^ Nc6 3.Bb5 a6 4.Ba4 Nf6^ 5.O-O^ Be7 6.Nc3~ b5 7.Bb3 d6
1.e4 e5 2.Nf3^ Nc6 3.Bb5 a6 4.Ba4 Nf6^ 5.O-O^ Be7 6.d3~ b5 7.Bb3 d6
1.e4 e5 2.Nf3^ Nc6 3.Bb5 a6 4.Ba4 Nf6^ 5.O-O^ Be7 6.Qe2~ b5 7.Bb3 O-O
1.e4 e5 2.Nf3^ Nc6 3.Bb5 a6 4.Ba4 Nf6^ 5.O-O^ Be7 6.Bxc6~ dxc6 7.Nc3 Bg4
1.e4 e5 2.Nf3^ Nc6 3.Bb5 a6 4.Ba4 Nf6^ 5.O-O^ Be7 6.Bxc6~ dxc6 7.d3 Nd7
1.e4 e5 2.Nf3^ Nc6 3.Bb5 a6 4.Ba4 Nf6^ 5.O-O^ Nxe4~ 6.d4 b5 7.Bb3 d5 8.dxe5 Be6 9.c3 Bc5 10.Nbd2
1.e4 e5 2.Nf3^ Nc6 3.Bb5 a6 4.Ba4 Nf6^ 5.O-O^ Nxe4~ 6.d4 b5 7.Bb3 d5 8.dxe5 Be6 9.Nbd2 Nc5 10.c3
1.e4 e5 2.Nf3^ Nc6 3.Bb5 a6 4.Ba4 Nf6^ 5.O-O^ Nxe4~ 6.d4 b5 7.Bb3 d5 8.dxe5 Be6 9.Qe2 Be7 10.Rd1 O-O
1.e4 e5 2.Nf3^ Nc6 3.Bb5 a6 4.Ba4 Nf6^ 5.O-O^ Nxe4~ 6.d4 Be7 7.Qe2 f5 8.dxe5
1.e4 e5 2.Nf3^ Nc6 3.Bb5 a6 4.Ba4 Nf6^ 5.O-O^ Nxe4~ 6.d4 exd4~ 7.Re1 d5 8.Nxd4
1.e4 e5 2.Nf3^ Nc6 3.Bb5 a6 4.Ba4 Nf6^ 5.O-O^ d6~ 6.Bxc6+ bxc6 7.d4
1.e4 e5 2.Nf3^ Nc6 3.Bb5 a6 4.Ba4 Nf6^ 5.O-O^ b5~ 6.Bb3 Bb7 7.d4
1.e4 e5 2.Nf3^ Nc6 3.Bb5 a6 4.Ba4 Nf6^ 5.O-O^ b5~ 6.Bb3 Be7 7.a4
1.e4 e5 2.Nf3^ Nc6 3.Bb5 a6 4.Ba4 Nf6^ 5.d4 exd4 6.O-O Be7 7.e5 Ne4 8.Nxd4
1.e4 e5 2.Nf3^ Nc6 3.Bb5 a6 4.Ba4 Nf6^ 5.d4 Nxe4~ 6.Qe2 f5 7.d5
1.e4 e5 2.Nf3^ Nc6 3.Bb5 a6 4.Ba4 Nf6^ 5.Bxc6~ dxc6 6.d3 Bd6
1.e4 e5 2.Nf3^ Nc6 3.Bb5 a6 4.Ba4 Nf6^ 5.d3~ d6 6.c3 g6
1.e4 e5 2.Nf3^ Nc6 3.Bb5 a6 4.Ba4 Nf6^ 5.Nc3~ b5
1.e4 e5 2.Nf3^ Nc6 3.Bb5 a6 4.Ba4 Nf6^ 5.Qe2~ b5
1.e4 e5 2.Nf3^ Nc6 3.Bb5 a6 4.Ba4 d6 5.c3 Bd7 6.d4 Nge7 7.Bb3 h6 8.Nbd2
1.e4 e5 2.Nf3^ Nc6 3.Bb5 a6 4.Ba4 d6 5.c3 Bd7 6.d4 g6~ 7.O-O Bg7 8.dxe5
1.e4 e5 2.Nf3^ Nc6 3.Bb5 a6 4.Ba4 d6 5.c3 f5~ 6.exf5 Bxf5 7.O-O
1.e4 e5 2.Nf3^ Nc6 3.Bb5 a6 4.Ba4 d6 5.d4 b5 6.Bb3 Nxd4 7.Nxd4 exd4 8.c3
1.e4 e5 2.Nf3^ Nc6 3.Bb5 a6 4.Ba4 d6 5.d4 b5 6.Bb3 exd4~ 7.Bd5
1.e4 e5 2.Nf3^ Nc6 3.Bb5 a6 4.Ba4 d6 5.c4~ Bg4
1.e4 e5 2.Nf3^ Nc6 3.Bb5 a6 4.Ba4 d6 5.O-O~ Bg4 6.h3 Bh5 7.c3 Nf6
1.e4 e5 2.Nf3^ Nc6 3.Bb5 a6 4.Ba4 d6 5.Bxc6+~ bxc6
1.e4 e5 2.Nf3^ Nc6 3.Bb5 a6 4.Ba4 Bc5~ 5.O-O Nf6 6.c3
1.e4 e5 2.Nf3^ Nc6 3.Bb5 a6 4.Ba4 f5~ 5.d4 exd4 6.e5
1.e4 e5 2.Nf3^ Nc6 3.Bb5 a6 4.Bxc6~ dxc6 5.Nxe5 Qd4
1.e4 e5 2.Nf3^ Nc6 3.Bb5 a6 4.Bxc6~ dxc6 5.O-O f6 6.d4 exd4
1.e4 e5 2.Nf3^ Nc6 3.Bb5 a6 4.Bxc6~ dxc6 5.d4 exd4 6.Qxd4 Qxd4
1.e4 e5 2.Nf3^ Nc6 3.Bb5 f5~ 4.Nc3 fxe4 5.Nxe4
1.e4 e5 2.Nf3^ Nc6 3.Bb5 f5~ 4.Nc3 Nd4 5.Ba4
1.e4 e5 2.Nf3^ Nc6 3.Bb5 f5~ 4.Nc3 Nf6 5.exf5
1.e4 e5 2.Nf3^ Nc6 3.Bb5 Bc5~ 4.c3 Nf6 5.d4 exd4 6.e5
1.e4 e5 2.Nf3^ Nc6 3.Bb5 Bc5~ 4.c3 f5~ 5.d4
1.e4 e5 2.Nf3^ Nc6 3.Bb5 Nge7~ 4.O-O
1.e4 e5 2.Nf3^ Nc6 3.Bb5 d6~ 4.d4 Bd7 5.Nc3
1.e4 e5 2.Nf3^ Nc6 3.Bb5 Nf6~ 4.O-O Nxe4 5.d4 Nd6 6.dxe5
1.e4 e5 2.Nf3^ Nc6 3.Bb5 Nf6~ 4.O-O Nxe4 5.d4 Be7 6.dxe5
1.e4 e5 2.Nf3^ Nc6 3.Bb5 Nf6~ 4.O-O d6 5.d4 Bd7 6.Nc3
1.e4 e5 2.Nf3^ Nc6 3.Bb5 Nf6~ 4.O-O Bc5~ 5.Nxe5 Nxe4 6.Qe2
1.e4 e5 2.Nf3^ Nc6 3.Bb5 Nd4~ 4.Nxd4 exd4 5.O-O c6 6.Bc4
1.e4 e5 2.Nf3^ Nc6 3.Bb5 g6~ 4.c3
1.e4 e5 2.Nf3^ Nc6 3.Bc4 Bc5 4.c3^ Nf6 5.d4 exd4 6.cxd4 Bb4+ 7.Nc3 Nxe4 8.O-O Bxc3 9.d5 Bf6 10.Re1 Ne7
1.e4 e5 2.Nf3^ Nc6 3.Bc4 Bc5 4.c3^ Nf6 5.d4 exd4 6.cxd4 Bb4+ 7.Nc3 Nxe4 8.O-O Bxc3 9.d5 Ne5~ 10.bxc3 Nxc4 11.Qd4 Ncd6 12.Qxg7
1.e4 e5 2.Nf3^ Nc6 3.Bc4 Bc5 4.c3^ Nf6 5.d4 exd4 6.cxd4 Bb4+ 7.Nc3 Nxe4 8.O-O Nxc3~ 9.bxc3 Bxc3 10.Ba3
1.e4 e5 2.Nf3^ Nc6 3.Bc4 Bc5 4.c3^ Nf6 5.d4 exd4 6.cxd4 Bb4+ 7.Nc3 d5~ 8.exd5 Nxd5 9.O-O
1.e4 e5 2.Nf3^ Nc6 3.Bc4 Bc5 4.c3^ Nf6 5.d4 exd4 6.cxd4 Bb4+ 7.Bd2~ Bxd2+ 8.Nbxd2 d5
1.e4 e5 2.Nf3^ Nc6 3.Bc4 Bc5 4.c3^ Nf6 5.d4 exd4 6.cxd4 Bb6~ 7.d5 Ne7 8.e5 Ng4 9.d6 Nxf2 10.Qb3 Nxh1 11.Bxf7+ Kf8 12.Ng5
1.e4 e5 2.Nf3^ Nc6 3.Bc4 Bc5 4.c3^ Qe7~ 5.d4 Bb6 6.O-O Nf6 7.Re1
1.e4 e5 2.Nf3^ Nc6 3.Bc4 Bc5 4.c3^ d6~ 5.d4 exd4 6.cxd4
1.e4 e5 2.Nf3^ Nc6 3.Bc4 Bc5 4.d3 Nf6 5.Nc3 d6 6.Bg5 h6
1.e4 e5 2.Nf3^ Nc6 3.Bc4 Bc5 4.b4~ Bxb4 5.c3 Be7
1.e4 e5 2.Nf3^ Nc6 3.Bc4 Nf6 4.Ng5 d5 5.exd5 Na5 6.Bb5+ c6 7.dxc6 bxc6 8.Be2 h6 9.Nf3 e4 10.Ne5 Bd6
1.e4 e5 2.Nf3^ Nc6 3.Bc4 Nf6 4.Ng5 d5 5.exd5 Na5 6.d3~ h6 7.Nf3 e4 8.dxe4 Nxc4
1.e4 e5 2.Nf3^ Nc6 3.Bc4 Nf6 4.Ng5 d5 5.exd5 Nd4~ 6.c3 b5 7.Bf1 Nxd5 8.Ne4 Ne6
1.e4 e5 2.Nf3^ Nc6 3.Bc4 Nf6 4.Ng5 Bc5~ 5.Bxf7+ Ke7 6.Bd5 Rf8 7.Nf3
1.e4 e5 2.Nf3^ Nc6 3.Bc4 Nf6 4.d4 exd4 5.O-O^ Nxe4^ 6.Re1 d5 7.Bxd5 Qxd5 8.Nc3 Qa5 9.Nxe4 Be6 10.Bd2 Qd5 11.Bg5
1.e4 e5 2.Nf3^ Nc6 3.Bc4 Nf6 4.d4 exd4 5.O-O^ Nxe4^ 6.Re1 d5 7.Bxd5 Qxd5 8.Nc3 Qa5 9.Rxe4+~ Be6
1.e4 e5 2.Nf3^ Nc6 3.Bc4 Nf6 4.d4 exd4 5.O-O^ Bc5 6.e5 d5 7.exf6 dxc4 8.Re1+ Be6 9.Ng5 Qd5 10.Nc3 Qf5 11.Nce4
1.e4 e5 2.Nf3^ Nc6 3.Bc4 Nf6 4.d4 exd4 5.O-O^ Bc5 6.e5 Ng4 7.Bf4 O-O 8.h3 Nf6 9.Bg5
1.e4 e5 2.Nf3^ Nc6 3.Bc4 Nf6 4.d4 exd4 5.e5 d5 6.Bb5 Ne4 7.Nxd4 Bc5 8.O-O O-O 9.Bxc6
1.e4 e5 2.Nf3^ Nc6 3.Bc4 Nf6 4.d4 exd4 5.e5 d5 6.Bb5 Ne4 7.Nxd4 Bc5 8.Nxc6~ Bxf2+ 9.Kf1 Qh4
1.e4 e5 2.Nf3^ Nc6 3.Bc4 Nf6 4.d4 exd4 5.e5 d5 6.Bb5 Ne4 7.Nxd4 Bd7~ 8.Bxc6
1.e4 e5 2.Nf3^ Nc6 3.Bc4 Nf6 4.d4 exd4 5.e5 Ng4~ 6.Qe2 Qe7 7.Bf4
1.e4 e5 2.Nf3^ Nc6 3.Bc4 Nf6 4.Nc3~ Bc5
1.e4 e5 2.Nf3^ Nc6 3.Bc4 Nf6 4.d3~ Be7 5.O-O O-O 6.Nbd2 d6
1.e4 e5 2.Nf3^ Nc6 3.Bc4 Nf6 4.O-O~ Nxe4 5.Nc3 Nxc3
1.e4 e5 2.Nf3^ Nc6 3.Bc4 Be7 4.d4 d6 5.Nc3 Nf6 6.h3 O-O 7.O-O
1.e4 e5 2.Nf3^ Nc6 3.d4 exd4 4.Nxd4^ Nf6 5.Nc3^ Bb4^ 6.Nxc6 bxc6
1.e4 e5 2.Nf3^ Nc6 3.d4 exd4 4.Nxd4^ Nf6 5.Nc3^ d6 6.Bb5 Bd7 7.O-O Be7 8.Re1
1.e4 e5 2.Nf3^ Nc6 3.d4 exd4 4.Nxd4^ Nf6 5.Nxc6 bxc6 6.e5 Qe7
1.e4 e5 2.Nf3^ Nc6 3.d4 exd4 4.Nxd4^ Bc5 5.Nb3^ Bb6 6.a4 a6
1.e4 e5 2.Nf3^ Nc6 3.d4 exd4 4.Nxd4^ Bc5 5.Be3 Qf6 6.c3 Nge7
1.e4 e5 2.Nf3^ Nc6 3.d4 exd4 4.Nxd4^ Qf6 5.Nb5 Bc5
1.e4 e5 2.Nf3^ Nc6 3.d4 exd4 4.Nxd4^ Qh4 5.Nf3 Qxe4+ 6.Be2
1.e4 e5 2.Nf3^ Nc6 3.d4 exd4 4.Bc4 Bc5 5.Ng5 Nh6
1.e4 e5 2.Nf3^ Nc6 3.d4 exd4 4.c3~ dxc3 5.Nxc3 d6
1.e4 e5 2.Nf3^ Nc6 3.Nc3 Nf6 4.d4 exd4 5.Nd5 Be7 6.Bf4^ d6 7.Nxd4 Nxd5 8.exd5 Nxd4 9.Qxd4 Bf6
1.e4 e5 2.Nf3^ Nc6 3.Nc3 Nf6 4.d4 exd4 5.Nd5 Be7 6.Bc4 O-O 7.O-O d6 8.Nxd4 Nxd4 9.Qxd4 Nxd5 10.Bxd5
1.e4 e5 2.Nf3^ Nc6 3.Nc3 Nf6 4.d4 exd4 5.Nd5 Nxe4~ 6.Qe2 f5 7.Ng5 d3 8.cxd3 Nd4 9.Qh5+
1.e4 e5 2.Nf3^ Nc6 3.Nc3 Nf6 4.d4 exd4 5.Nxd4 Bb4 6.Nxc6 bxc6 7.Bd3 d5 8.exd5 cxd5 9.O-O O-O 10.Bg5 c6
1.e4 e5 2.Nf3^ Nc6 3.Nc3 Nf6 4.d4 Bb4~ 5.d5 Ne7 6.Qd3
1.e4 e5 2.Nf3^ Nc6 3.Nc3 Nf6 4.d4 Bb4~ 5.d5 Nxe4~ 6.Qd3
1.e4 e5 2.Nf3^ Nc6 3.Nc3 Nf6 4.d4 Bb4~ 5.d5 Bxc3+~ 6.bxc3
1.e4 e5 2.Nf3^ Nc6 3.Nc3 Nf6 4.Bb5 Bb4 5.O-O O-O 6.d3 d6 7.Bg5 Bxc3 8.bxc3 Qe7 9.Re1
1.e4 e5 2.Nf3^ Nc6 3.Nc3 Nf6 4.Bb5 Nd4 5.Nxd4 exd4 6.e5 dxc3 7.exf6 Qxf6 8.dxc3 Qe5+ 9.Qe2 Qxe2+ 10.Bxe2
1.e4 e5 2.Nf3^ Nc6 3.Nc3 Nf6 4.Bb5 Nd4 5.Nxe5~ Qe7 6.Nf3 Nxb5 7.Nxb5 Qxe4+
1.e4 e5 2.Nf3^ Nc6 3.Nc3 Nf6 4.Bb5 Nd4 5.Nxe5~ Qe7 6.f4~ Nxb5 7.Nxb5 d6
1.e4 e5 2.Nf3^ Nc6 3.Nc3 Nf6 4.Bb5 Bc5~ 5.O-O O-O 6.Nxe5 Nxe5 7.d4
1.e4 e5 2.Nf3^ Nc6 3.c3 Nf6 4.d4 Nxe4 5.d5 Ne7 6.Nxe5 Ng6 7.Bd3 Nxe5
1.e4 e5 2.Nf3^ Nc6 3.c3 Nf6 4.d4 Nxe4 5.d5 Nb8~ 6.Bd3
1.e4 e5 2.Nf3^ Nc6 3.c3 Nf6 4.d4 exd4~ 5.e5
1.e4 e5 2.Nf3^ Nc6 3.c3 d5 4.Qa4 dxe4 5.Nxe5 Qd5 6.Nxc6 bxc6 7.Bc4 Qd7 8.O-O Nf6
1.e4 e5 2.Nf3^ Nc6 3.c3 d5 4.Qa4 Nf6 5.Nxe5 Bd6 6.Nxc6 bxc6 7.d3 O-O
1.e4 e5 2.Nf3^ Nc6 3.Be2~ Nf6 4.d3 d5
1.e4 e5 2.Nf3^ Nf6 3.d4 exd4^ 4.e5 Ne4 5.Qxd4 d5 6.exd6 Nxd6 7.Nc3 Nc6 8.Qf4 g6 9.Bd3 Bg7
1.e4 e5 2.Nf3^ Nf6 3.d4 exd4^ 4.e5 Ne4 5.Qxd4 d5 6.exd6 Nxd6 7.Bd3 Nc6 8.Qf4 g6 9.Nc3
1.e4 e5 2.Nf3^ Nf6 3.d4 exd4^ 4.e5 Ne4 5.Qxd4 d5 6.exd6 Nxd6 7.Bg5~ Nc6 8.Qe3+ Be7
1.e4 e5 2.Nf3^ Nf6 3.d4 exd4^ 4.Bc4
1.e4 e5 2.Nf3^ Nf6 3.d4 Nxe4 4.Bd3 d5 5.Nxe5 Be7 6.O-O Nd7 7.c4 Nxe5 8.dxe5 c6
1.e4 e5 2.Nf3^ Nf6 3.d4 Nxe4 4.Bd3 d5 5.Nxe5 Be7 6.O-O O-O 7.c4 Nf6 8.Bg5
1.e4 e5 2.Nf3^ Nf6 3.d4 Nxe4 4.Bd3 d5 5.Nxe5 Bd6 6.O-O O-O 7.c4
1.e4 e5 2.Nf3^ Nf6 3.Nxe5 d6 4.Nf3 Nxe4 5.d4 d5 6.Bd3 Be7 7.O-O Nc6
1.e4 e5 2.Nf3^ Nf6 3.Nxe5 d6 4.Nf3 Nxe4 5.d4 d5 6.Bd3 Bd6 7.O-O O-O 8.c4
1.e4 e5 2.Nf3^ Nf6 3.Nxe5 d6 4.Nf3 Nxe4 5.Qe2 Qe7 6.d3 Nf6 7.Bg5 Qxe2+ 8.Bxe2
1.e4 e5 2.Nf3^ Nf6 3.Nxe5 Nxe4~ 4.Qe2 Qe7 5.Qxe4 d6 6.d4
1.e4 e5 2.Nf3^ Nf6 3.Nc3~ Nc6
1.e4 e5 2.Nf3^ Nf6 3.Bc4~ Nxe4 4.Nc3 Nc6 5.Nxe4 d5
1.e4 e5 2.Nf3^ d6 3.d4 Nd7 4.Bc4 c6 5.O-O Be7 6.dxe5 dxe5 7.Ng5 Bxg5 8.Qh5 g6 9.Qxg5
1.e4 e5 2.Nf3^ d6 3.d4 Nd7 4.Bc4 Ngf6~ 5.dxe5 Nxe5 6.Nxe5
1.e4 e5 2.Nf3^ d6 3.d4 Nd7 4.Bc4 Ngf6~ 5.dxe5 dxe5~ 6.Ng5
1.e4 e5 2.Nf3^ d6 3.d4 Nf6 4.Nc3 Nbd7^ 5.Bc4 Be7 6.O-O O-O 7.Qe2 c6 8.a4 Qc7
1.e4 e5 2.Nf3^ d6 3.d4 Nf6 4.Nc3 Nbd7^ 5.Bc4 Be7 6.Ng5 O-O 7.Bxf7+ Rxf7 8.Ne6 Qe8 9.Nxc7 Qd8 10.Nxa8 b5
1.e4 e5 2.Nf3^ d6 3.d4 Nf6 4.Nc3 Nbd7^ 5.Bc4 Be7 6.dxe5~ dxe5
1.e4 e5 2.Nf3^ d6 3.d4 Nf6 4.Nc3 exd4 5.Nxd4 Be7 6.Be2 O-O 7.O-O Nc6
1.e4 e5 2.Nf3^ d6 3.d4 Nf6 4.Nc3 exd4 5.Nxd4 Be7 6.Bf4~ O-O
1.e4 e5 2.Nf3^ d6 3.d4 Nf6 4.Nc3 exd4 5.Nxd4 g6~ 6.Bf4
1.e4 e5 2.Nf3^ d6 3.d4 Nf6 4.Nc3 exd4 5.Qxd4~ Be7
1.e4 e5 2.Nf3^ d6 3.d4 Nf6 4.dxe5~ Nxe4 5.Nbd2 Nc5 6.Nc4 d5 7.Bg5
1.e4 e5 2.Nf3^ d6 3.d4 Nf6 4.dxe5~ Nxe4 5.Bc4 c6 6.exd6 Nxd6
1.e4 e5 2.Nf3^ d6 3.d4 f5~ 4.Bc4 fxe4 5.Ng5 Nh6 6.O-O
1.e4 e5 2.Nf3^ d6 3.d4 f5~ 4.Bc4 fxe4 5.Nxe5 dxe5 6.Qh5+
1.e4 e5 2.Nf3^ d6 3.d4 f5~ 4.Bc4 fxe4 5.Nxe5 d5 6.Qh5+ g6 7.Nxg6
1.e4 e5 2.Nf3^ d6 3.d4 exd4~ 4.Nxd4
1.e4 e5 2.Nf3^ d5~ 3.exd5 e4 4.Qe2 Qe7 5.Nd4
1.e4 e5 2.Nf3^ d5~ 3.exd5 e4 4.Qe2 Nf6 5.Nc3
1.e4 e5 2.Nf3^ f5~ 3.Nxe5 Qf6 4.d4 d6 5.Nc4
1.e4 e5 2.Nc3 Nf6 3.Bc4 Nxe4 4.Qh5 Nd6 5.Bb3 Nc6 6.Nb5 g6 7.Qf3 f5 8.Qd5 Qe7
1.e4 e5 2.Nc3 Nf6 3.Bc4 Nxe4 4.Qh5 Nd6 5.Bb3 Be7 6.Nf3 Nc6 7.Nxe5 O-O 8.Nd5 Nd4
1.e4 e5 2.Nc3 Nf6 3.Bc4 Nxe4 4.Qh5 Nd6 5.Bb3 Be7 6.Nf3 O-O~ 7.h4
1.e4 e5 2.Nc3 Nf6 3.Bc4 Nxe4 4.Qh5 Nd6 5.Qxe5+~ Qe7
1.e4 e5 2.Nc3 Nf6 3.Bc4 Nc6 4.d3 Bc5 5.Bg5 h6 6.Bh4 d6 7.Na4 Bb6 8.Nxb6
1.e4 e5 2.Nc3 Nf6 3.Bc4 Nc6 4.d3 Na5 5.Nge2 Nxc4 6.dxc4
1.e4 e5 2.Nc3 Nf6 3.f4~ d5 4.fxe5 Nxe4 5.Nf3 Be7 6.d4 O-O
1.e4 e5 2.Nc3 Nf6 3.f4~ d5 4.fxe5 Nxe4 5.Qf3~ Nc6 6.Bb5 Nxc3
1.e4 e5 2.Nc3 Nf6 3.f4~ d5 4.fxe5 Nxe4 5.d3~ Nxc3 6.bxc3 d4
1.e4 e5 2.Nc3 Nc6 3.Nf3 Nf6
1.e4 e5 2.Nc3 Nc6 3.Bc4 Nf6 4.d3 Bb4 5.Bg5 h6
1.e4 e5 2.Nc3 Nc6 3.f4~ Bc5 4.fxe5 d6 5.exd6 Qxd6
1.e4 e5 2.Bc4 Nf6 3.d4 exd4 4.Nf3 d5 5.exd5 Bb4+ 6.c3 Qe7+ 7.Kf1 dxc3
1.e4 e5 2.Bc4 Nf6 3.d4 exd4 4.Nf3 Nxe4~ 5.Qxd4 Nf6 6.Bg5 Be7 7.Nc3 c6 8.O-O-O
1.e4 e5 2.Bc4 Nf6 3.d4 Nxe4~ 4.Bd5 Nf6 5.dxe5 Nxd5 6.Qxd5
1.e4 e5 2.Bc4 Nf6 3.d4 Nxe4~ 4.Bd5 Qh4~ 5.Be3 Nf6 6.Nf3
1.e4 e5 2.Bc4 Nf6 3.d3~ c6 4.Nf3 d5 5.exd5 cxd5
1.e4 e5 2.Bc4 Nf6 3.f4~ Nxe4 4.d3 Nd6 5.Bb3 e4
1.e4 e5 2.Bc4 Nc6~ 3.Nc3 Bb4 4.a3
1.e4 e5 2.Bc4 Bc5~ 3.c3 Nf6 4.d4 exd4 5.e5
1.e4 e5 2.Bc4 f5~ 3.d3
1.e4 e5 2.Bc4 c6~ 3.d4 d5 4.exd5
1.e4 e5 2.d4~ exd4 3.Qxd4 Nc6 4.Qe3 Nf6 5.Nc3 Bb4 6.Bd2 O-O 7.O-O-O Re8
1.e4 e5 2.d4~ exd4 3.Qxd4 Nc6 4.Qe3 Nf6 5.e5 Ng4 6.Qe4 d5
1.e4 e5 2.d4~ exd4 3.Qxd4 Nc6 4.Qe3 Nf6 5.e5 Ng4 6.Qe2 d6
1.e4 e5 2.d4~ exd4 3.Qxd4 Nc6 4.Qe3 Nf6 5.Bd2 d6
1.e4 e5 2.d4~ exd4 3.Qxd4 Nc6 4.Qe3 d6 5.Nc3 Nf6 6.Bd2 Be7
1.e4 e5 2.d4~ exd4 3.c3 dxc3 4.Bc4 cxb2
1.e4 e5 2.d4~ exd4 3.c3 dxc3 4.Nxc3 Nc6
1.e4 e5 2.f4~ exf4 3.Nf3 d5 4.exd5 Nf6 5.Bb5+ c6 6.dxc6 bxc6
1.e4 e5 2.f4~ exf4 3.Bc4~ Nf6 4.Nc3 c6
1.e4 e5 2.f4~ exf4 3.Nc3~ Qh4+ 4.Ke2 d5 5.Nxd5 Bg4+ 6.Nf3 Bd6
1.e4 e5 2.f4~ exf4 3.Be2~ d5 4.exd5 Nf6
1.e4 e5 2.f4~ Bc5 3.Nf3 d6 4.c3 Nf6 5.fxe5 dxe5 6.Nxe5 Qe7 7.d4 Bd6
1.e4 e5 2.f4~ Bc5 3.Nf3 d6 4.Nc3 Nf6 5.Bc4 Nc6 6.d3 Bg4
1.e4 e5 2.d3~ Nc6
1.e4 e6 2.d4 d5 3.Nc3 Bb4 4.e5 c5 5.a3 Bxc3+ 6.bxc3 Ne7 7.Qg4 Nf5 8.Bd3
1.e4 e6 2.d4 d5 3.Nc3 Bb4 4.e5 c5 5.a3 Bxc3+ 6.bxc3 Ne7 7.Nf3 Qc7 8.a4 b6
1.e4 e6 2.d4 d5 3.Nc3 Bb4 4.e5 c5 5.a3 Bxc3+ 6.bxc3 Qc7~ 7.Qg4 Ne7 8.Qxg7
1.e4 e6 2.d4 d5 3.Nc3 Bb4 4.e5 c5 5.a3 Ba5~ 6.b4 cxd4 7.Nb5
1.e4 e6 2.d4 d5 3.Nc3 Bb4 4.e5 Nc6~ 5.Qg4
1.e4 e6 2.d4 d5 3.Nc3 Bb4 4.e5 b6~ 5.Qg4
1.e4 e6 2.d4 d5 3.Nc3 Bb4 4.e5 Qd7~ 5.Bd2
1.e4 e6 2.d4 d5 3.Nc3 Bb4 4.Bd3 c5 5.exd5 Qxd5
1.e4 e6 2.d4 d5 3.Nc3 Bb4 4.Bd2~ dxe4
1.e4 e6 2.d4 d5 3.Nc3 Bb4 4.a3~ Bxc3+ 5.bxc3 dxe4
1.e4 e6 2.d4 d5 3.Nc3 Bb4 4.exd5~ exd5
1.e4 e6 2.d4 d5 3.Nc3 Bb4 4.Ne2~ dxe4 5.a3 Be7
1.e4 e6 2.d4 d5 3.Nc3 Nf6 4.Bg5 Be7 5.e5 Nfd7 6.Bxe7 Qxe7 7.f4 O-O 8.Nf3 c5 9.dxc5 Nc6
1.e4 e6 2.d4 d5 3.Nc3 Nf6 4.Bg5 Be7 5.e5 Nfd7 6.Bxe7 Qxe7 7.Qd2~ O-O
1.e4 e6 2.d4 d5 3.Nc3 Nf6 4.Bg5 Be7 5.e5 Nfd7 6.h4~ a6
1.e4 e6 2.d4 d5 3.Nc3 Nf6 4.Bg5 dxe4 5.Nxe4 Be7 6.Bxf6 Bxf6 7.Nf3
1.e4 e6 2.d4 d5 3.Nc3 Nf6 4.Bg5 Bb4 5.e5 h6 6.Bd2 Bxc3 7.bxc3 Ne4 8.Qg4 g6
1.e4 e6 2.d4 d5 3.Nc3 Nf6 4.Bg5 Bb4 5.Ne2~ dxe4 6.a3 Be7
1.e4 e6 2.d4 d5 3.Nc3 Nf6 4.e5~ Nfd7 5.f4 c5 6.dxc5 Nc6
1.e4 e6 2.d4 d5 3.Nc3 Nf6 4.e5~ Nfd7 5.Nf3~ c5 6.dxc5 Nc6
1.e4 e6 2.d4 d5 3.Nc3 dxe4~ 4.Nxe4 Nd7 5.Nf3
1.e4 e6 2.d4 d5 3.Nd2 c5 4.exd5 exd5 5.Bb5+ Nc6 6.Ngf3^ Bd6 7.O-O Ne7 8.dxc5 Bxc5 9.Nb3 Bb6
1.e4 e6 2.d4 d5 3.Nd2 c5 4.exd5 exd5 5.Bb5+ Nc6 6.Qe2+ Be7 7.dxc5 Nf6 8.Nb3 O-O 9.Nf3
1.e4 e6 2.d4 d5 3.Nd2 c5 4.exd5 exd5 5.Bb5+ Nc6 6.Ne2~ Bd6 7.dxc5 Bxc5
1.e4 e6 2.d4 d5 3.Nd2 c5 4.exd5 exd5 5.Bb5+ Bd7~ 6.Qe2+
1.e4 e6 2.d4 d5 3.Nd2 c5 4.exd5 exd5 5.Ngf3 c4 6.Be2 Nc6 7.O-O
1.e4 e6 2.d4 d5 3.Nd2 c5 4.exd5 exd5 5.Ngf3 Nc6 6.Bb5 Bd6 7.O-O
1.e4 e6 2.d4 d5 3.Nd2 c5 4.exd5 Qxd5~ 5.Ngf3 cxd4 6.Bc4
1.e4 e6 2.d4 d5 3.Nd2 Nc6 4.Ngf3 Nf6 5.e5 Nd7 6.Nb3 Be7 7.Bb5 a6
1.e4 e6 2.d4 d5 3.Nd2 Nc6 4.c3 e5 5.exd5 Qxd5 6.Ngf3 exd4 7.Bc4 Qf5
1.e4 e6 2.d4 d5 3.Nd2 Nf6 4.e5 Nfd7 5.Bd3 c5 6.c3 Nc6 7.Ngf3 Qb6 8.O-O
1.e4 e6 2.d4 d5 3.Nd2 Nf6 4.e5 Nfd7 5.f4~ c5
1.e4 e6 2.d4 d5 3.Nd2 dxe4 4.Nxe4 Nf6 5.Nxf6+ gxf6
1.e4 e6 2.d4 d5 3.Nd2 a6~ 4.Ngf3 c5 5.dxc5
1.e4 e6 2.d4 d5 3.Nd2 f5~ 4.exf5
1.e4 e6 2.d4 d5 3.e5 c5 4.c3 Qb6 5.Nf3 Nc6 6.Bd3^ cxd4 7.cxd4 Bd7 8.Nc3
1.e4 e6 2.d4 d5 3.e5 c5 4.c3 Qb6 5.Nf3 Nc6 6.a3 c4 7.g3
1.e4 e6 2.d4 d5 3.e5 c5 4.c3 Nc6 5.Nf3 Qb6
1.e4 e6 2.d4 d5 3.e5 c5 4.Qg4 Nc6 5.Nf3 Nge7 6.c3 Nf5
1.e4 e6 2.d4 d5 3.e5 c5 4.Nf3 cxd4 5.Qxd4 Nc6 6.Qf4 f5
1.e4 e6 2.d4 d5 3.e5 c5 4.Nf3 Nc6~ 5.Bd3
1.e4 e6 2.d4 d5 3.e5 c5 4.dxc5 Nc6 5.Nf3 Bxc5
1.e4 e6 2.d4 d5 3.exd5~ exd5 4.Bd3 Nc6 5.Ne2 Bd6
1.e4 e6 2.d4 d5 3.exd5~ exd5 4.Bd3 Nc6 5.Nf3 Bg4
1.e4 e6 2.d4 d5 3.exd5~ exd5 4.Bd3 Bd6 5.Nf3 Nf6 6.O-O
1.e4 e6 2.c4~ c5
1.e4 e6 2.Qe2~ c5 3.f4 Nc6
1.e4 e6 2.d3~ d5 3.Nd2 Nf6 4.g3 c5
1.e4 e6 2.b3~ d5 3.Bb2 dxe4
1.e4 e6 2.Nc3~ d5
1.e4 e6 2.Nf3~ d5 3.Nc3 Nf6
1.e4 e6 2.e5~ d5
1.e4 c6 2.d4 d5 3.Nc3^ dxe4 4.Nxe4 Bf5 5.Ng3 Bg6 6.h4 h6 7.Nf3 Nd7 8.h5 Bh7 9.Bd3
1.e4 c6 2.d4 d5 3.Nc3^ dxe4 4.Nxe4 Bf5 5.Ng3 Bg6 6.Bc4 e6 7.N1e2 Nf6 8.Nf4
1.e4 c6 2.d4 d5 3.Nc3^ dxe4 4.Nxe4 Nd7 5.Nf3 Ngf6 6.Nxf6+ Nxf6 7.Ne5 Bf5 8.c3
1.e4 c6 2.d4 d5 3.Nc3^ dxe4 4.Nxe4 Nd7 5.Nf3 Ngf6 6.Ng3 e6 7.Bd3
1.e4 c6 2.d4 d5 3.Nc3^ dxe4 4.Nxe4 Nd7 5.Bc4 Ngf6 6.Ng5 e6 7.Qe2 Nb6 8.Bd3 h6
1.e4 c6 2.d4 d5 3.Nc3^ dxe4 4.Nxe4 Nf6 5.Nxf6+ exf6 6.Bc4 Qe7+ 7.Qe2 Be6 8.Bb3
1.e4 c6 2.d4 d5 3.Nc3^ dxe4 4.Nxe4 Nf6 5.Nxf6+ exf6 6.Nf3 Bd6 7.Be2 Na6 8.O-O Nc7
1.e4 c6 2.d4 d5 3.Nc3^ dxe4 4.Nxe4 Nf6 5.Nxf6+ gxf6~ 6.Nf3 Bf5 7.Be2
1.e4 c6 2.d4 d5 3.Nc3^ dxe4 4.Nxe4 Nf6 5.Ng3 Nbd7 6.Nf3
1.e4 c6 2.d4 d5 3.exd5 cxd5 4.c4 Nf6 5.Nc3 e6 6.Nf3 Be7
1.e4 c6 2.d4 d5 3.exd5 cxd5 4.c4 Nf6 5.Nc3 g6~ 6.Qb3
1.e4 c6 2.d4 d5 3.e5 Bf5 4.Bd3 Bxd3
1.e4 c6 2.d4 d5 3.Nd2~ dxe4 4.Nxe4 Nf6
1.e4 c6 2.Nc3~ d5 3.Nf3 Bg4 4.h3 Bxf3
1.e4 c6 2.c4~ d5 3.cxd5 cxd5 4.exd5 Nf6 5.Nc3 Nxd5
1.e4 c6 2.c4~ d5 3.exd5 cxd5
1.e4 c6 2.d3~ d5 3.Nd2 g6 4.Ngf3 Bg7
1.e4 c6 2.Ne2~ d5 3.e5 c5
1.e4 c6 2.Nf3~ d5
1.e4 Nf6~ 2.e5 Nd5 3.d4^ d6 4.Nf3 Bg4 5.Be2 Nc6 6.O-O e6 7.c4 Nb6
1.e4 Nf6~ 2.e5 Nd5 3.d4^ d6 4.Nf3 Bg4 5.Be2 e6 6.O-O Be7 7.c4
1.e4 Nf6~ 2.e5 Nd5 3.d4^ d6 4.Nf3 Bg4 5.Be2 e6 6.O-O dxe5 7.Nxe5
1.e4 Nf6~ 2.e5 Nd5 3.c4 Nb6 4.c5 Nd5 5.Bc4 e6 6.Nc3 Nxc3 7.dxc3 Nc6 8.Bf4
1.e4 d5~ 2.exd5 Nf6 3.d4 Nxd5 4.c4
1.e4 d5~ 2.exd5 Qxd5 3.Nc3 Qa5 4.d4
1.e4 Nc6~ 2.d4 d5 3.Nc3 dxe4 4.d5 Ne5 5.Bf4
1.e4 Nc6~ 2.d4 e5 3.dxe5 Nxe5 4.Bf4 d6 5.Nc3
1.e4 Nc6~ 2.d4 e6~ 3.Nc3 Nf6 4.e5
1.e4 Nc6~ 2.d4 Nf6 3.e5
1.e4 g6~ 2.d4 Bg7 3.Nc3 d6 4.Be3^ a6 5.Qd2 b5 6.f3
1.e4 g6~ 2.d4 Bg7 3.Nc3 d6 4.Nf3 Bg4 5.Be2
1.e4 g6~ 2.d4 d6 3.Nc3
1.e4 d6~ 2.d4 Nf6 3.Nc3 g6 4.Nf3 Bg7 5.Be2 O-O 6.O-O Bg4 7.Be3 Nc6 8.Qd3
1.e4 d6~ 2.d4 e5 3.dxe5 dxe5 4.Qxd8+
1.e4 b6~ 2.d4
1.e4 a6~ 2.d4 g6 3.Nf3
1.d4 d5^ 2.c4^ c6 3.Nf3 Nf6 4.Nc3 dxc4 5.a4 Bf5 6.e3 e6 7.Bxc4 Bb4 8.O-O O-O 9.Qe2
1.d4 d5^ 2.c4^ c6 3.Nf3 Nf6 4.Nc3 dxc4 5.a4 Bf5 6.Ne5~ e6 7.f3 Bb4
1.d4 d5^ 2.c4^ c6 3.Nf3 Nf6 4.Nc3 dxc4 5.a4 Na6~ 6.e4 Bg4 7.Bxc4
1.d4 d5^ 2.c4^ c6 3.Nf3 Nf6 4.Nc3 dxc4 5.a4 Bg4~ 6.Ne5
1.d4 d5^ 2.c4^ c6 3.Nf3 Nf6 4.Nc3 dxc4 5.e3~ b5 6.a4 b4
1.d4 d5^ 2.c4^ c6 3.Nf3 Nf6 4.Nc3 dxc4 5.e4~ b5
1.d4 d5^ 2.c4^ c6 3.Nf3 Nf6 4.Nc3 e6~ 5.e3 Nbd7 6.Bd3 dxc4 7.Bxc4 b5 8.Bd3
1.d4 d5^ 2.c4^ c6 3.Nf3 Nf6 4.cxd5 cxd5 5.Nc3 Nc6 6.Bf4 Bf5 7.e3 e6 8.Bb5
1.d4 d5^ 2.c4^ c6 3.Nf3 Nf6 4.cxd5 cxd5 5.Nc3 Nc6 6.Bf4 e6 7.e3 Be7 8.Bd3
1.d4 d5^ 2.c4^ c6 3.Nf3 Nf6 4.e3~ Bf5 5.Bd3 e6 6.Nc3 Bxd3
1.d4 d5^ 2.c4^ c6 3.Nf3 Nf6 4.e3~ Bf5 5.Qb3 Qc7 6.Nc3 e6
1.d4 d5^ 2.c4^ c6 3.Nf3 dxc4~ 4.e4
1.d4 d5^ 2.c4^ c6 3.cxd5 cxd5 4.Nc3^ Nc6 5.Bf4 Bf5 6.e3 e6
1.d4 d5^ 2.c4^ c6 3.cxd5 cxd5 4.Nf3 Nf6 5.Nc3 Nc6
1.d4 d5^ 2.c4^ c6 3.Nc3 Nf6 4.Nf3 dxc4 5.e4 b5 6.e5
1.d4 d5^ 2.c4^ c6 3.Nc3 Nf6 4.Nf3 dxc4 5.a4 Bf5 6.e3
1.d4 d5^ 2.c4^ c6 3.Nc3 e6 4.e4 dxe4 5.Nxe4 Bb4+ 6.Bd2 Qxd4 7.Bxb4 Qxe4+ 8.Be2
1.d4 d5^ 2.c4^ dxc4 3.Nf3^ Nf6 4.e3 e6 5.Bxc4 c5 6.O-O a6 7.Qe2 b5 8.Bb3 Bb7
1.d4 d5^ 2.c4^ dxc4 3.Nf3^ Nf6 4.e3 e6 5.Bxc4 c5 6.O-O a6 7.a4 Nc6 8.Qe2 cxd4
1.d4 d5^ 2.c4^ dxc4 3.Nf3^ Nf6 4.e3 e6 5.Bxc4 c5 6.O-O cxd4~ 7.exd4
1.d4 d5^ 2.c4^ dxc4 3.Nf3^ Nf6 4.e3 Bg4 5.Bxc4 e6 6.h3 Bh5 7.Nc3
1.d4 d5^ 2.c4^ dxc4 3.Nf3^ Nf6 4.e3 Bg4 5.Bxc4 e6 6.Qb3~ Bxf3
1.d4 d5^ 2.c4^ dxc4 3.Nf3^ Nf6 4.e3 b5~ 5.a4
1.d4 d5^ 2.c4^ dxc4 3.Nf3^ Nf6 4.Qa4+~ Nc6 5.Qxc4 Be6
1.d4 d5^ 2.c4^ dxc4 3.Nf3^ Nf6 4.Qa4+~ Nc6 5.Nc3 Nd5
1.d4 d5^ 2.c4^ dxc4 3.Nf3^ Nf6 4.Nc3~ a6 5.e4 b5 6.e5 Nd5
1.d4 d5^ 2.c4^ dxc4 3.Nf3^ a6 4.e3 Bg4 5.Bxc4 e6 6.h3 Bh5 7.Nc3
1.d4 d5^ 2.c4^ dxc4 3.Nf3^ a6 4.e3 Nf6 5.Bxc4 e6 6.O-O
1.d4 d5^ 2.c4^ dxc4 3.Nf3^ a6 4.e3 b5~ 5.a4 c6 6.b3
1.d4 d5^ 2.c4^ dxc4 3.Nf3^ b5~ 4.a4 c6 5.e3
1.d4 d5^ 2.c4^ dxc4 3.Nc3 e5 4.d5 a6
1.d4 d5^ 2.c4^ dxc4 3.e4~ e5 4.Nf3 Bb4+ 5.Bd2 Bxd2+
1.d4 d5^ 2.c4^ dxc4 3.e3~ e5
1.d4 d5^ 2.c4^ Nc6 3.Nc3 Nf6 4.Bg5 Ne4 5.cxd5 Nxc3
1.d4 d5^ 2.c4^ Nc6 3.Nc3 Nf6 4.cxd5~ Nxd5 5.Nxd5 Qxd5
1.d4 d5^ 2.c4^ Nc6 3.Nc3 dxc4 4.d5 Na5 5.Qa4+ c6 6.b4 b5 7.Qxa5
1.d4 d5^ 2.c4^ Nc6 3.Nc3 dxc4 4.d5 Ne5 5.Bf4 Ng6 6.Bg3
1.d4 d5^ 2.c4^ Nc6 3.Nc3 dxc4 4.e3 Nf6 5.Bxc4 e6 6.Nf3 Bb4 7.Qc2
1.d4 d5^ 2.c4^ Nc6 3.Nc3 dxc4 4.e3 e5~ 5.d5
1.d4 d5^ 2.c4^ Nc6 3.Nf3 Bg4 4.cxd5^ Bxf3 5.dxc6 Bxc6 6.Nc3
1.d4 d5^ 2.c4^ Nc6 3.Nf3 Bg4 4.Qa4 Bxf3 5.exf3
1.d4 d5^ 2.c4^ e6 3.Nc3 Nf6^ 4.Bg5^ Be7^ 5.e3 O-O 6.Nf3 h6^ 7.Bh4 Ne4 8.Bxe7 Qxe7 9.cxd5 Nxc3 10.bxc3
1.d4 d5^ 2.c4^ e6 3.Nc3 Nf6^ 4.Bg5^ Be7^ 5.e3 O-O 6.Nf3 h6^ 7.Bh4 Ne4 8.Bxe7 Qxe7 9.Rc1~ c6
1.d4 d5^ 2.c4^ e6 3.Nc3 Nf6^ 4.Bg5^ Be7^ 5.e3 O-O 6.Nf3 h6^ 7.Bh4 Ne4 8.Bxe7 Qxe7 9.Qc2~ Nxc3
1.d4 d5^ 2.c4^ e6 3.Nc3 Nf6^ 4.Bg5^ Be7^ 5.e3 O-O 6.Nf3 h6^ 7.Bh4 b6~ 8.Be2 Bb7 9.Bxf6
1.d4 d5^ 2.c4^ e6 3.Nc3 Nf6^ 4.Bg5^ Be7^ 5.e3 O-O 6.Nf3 h6^ 7.Bxf6~ Bxf6
1.d4 d5^ 2.c4^ e6 3.Nc3 Nf6^ 4.Bg5^ Be7^ 5.e3 O-O 6.Nf3 Nbd7 7.Rc1 c6 8.Bd3 dxc4 9.Bxc4
1.d4 d5^ 2.c4^ e6 3.Nc3 Nf6^ 4.Bg5^ Be7^ 5.e3 O-O 6.Nf3 Nbd7 7.Rc1 c6 8.Qc2~ a6
1.d4 d5^ 2.c4^ e6 3.Nc3 Nf6^ 4.Bg5^ Be7^ 5.e3 O-O 6.Nf3 Nbd7 7.Rc1 a6~ 8.c5
1.d4 d5^ 2.c4^ e6 3.Nc3 Nf6^ 4.Bg5^ Be7^ 5.e3 O-O 6.Nf3 Nbd7 7.Qc2~ c5
1.d4 d5^ 2.c4^ e6 3.Nc3 Nf6^ 4.Bg5^ Be7^ 5.e3 O-O 6.Rc1 h6 7.Bh4 b6
1.d4 d5^ 2.c4^ e6 3.Nc3 Nf6^ 4.Bg5^ Nbd7 5.e3 c6 6.Nf3 Qa5 7.Nd2 Bb4 8.Qc2
1.d4 d5^ 2.c4^ e6 3.Nc3 Nf6^ 4.Bg5^ Nbd7 5.e3 c6 6.Nf3 Qa5 7.cxd5~ Nxd5 8.Qd2 N7b6
1.d4 d5^ 2.c4^ e6 3.Nc3 Nf6^ 4.Bg5^ c5~ 5.cxd5 cxd4 6.Qxd4
1.d4 d5^ 2.c4^ e6 3.Nc3 Nf6^ 4.Bg5^ c5~ 5.cxd5 Qb6~ 6.Bxf6 Qxb2 7.Rc1
1.d4 d5^ 2.c4^ e6 3.Nc3 Nf6^ 4.Nf3 c5 5.cxd5 Nxd5 6.e4 Nxc3 7.bxc3 cxd4 8.cxd4 Bb4+ 9.Bd2 Bxd2+
1.d4 d5^ 2.c4^ e6 3.Nc3 Nf6^ 4.Nf3 c5 5.cxd5 Nxd5 6.e3~ Nc6 7.Bd3 cxd4
1.d4 d5^ 2.c4^ e6 3.Nc3 Nf6^ 4.Nf3 c5 5.cxd5 Nxd5 6.g3~ Nc6 7.Bg2 Be7
1.d4 d5^ 2.c4^ e6 3.Nc3 Nf6^ 4.Nf3 c5 5.e3~ Nc6 6.a3 cxd4
1.d4 d5^ 2.c4^ e6 3.Nc3 Nf6^ 4.Nf3 c5 5.e3~ Nc6 6.cxd5 exd5
1.d4 d5^ 2.c4^ e6 3.Nc3 Nf6^ 4.Nf3 Bb4 5.cxd5 exd5 6.Bg5 h6
1.d4 d5^ 2.c4^ e6 3.Nc3 Nf6^ 4.Nf3 Bb4 5.Qa4+~ Nc6 6.cxd5 exd5
1.d4 d5^ 2.c4^ e6 3.Nc3 Nf6^ 4.Nf3 Bb4 5.Bg5~ h6
1.d4 d5^ 2.c4^ e6 3.Nc3 Nf6^ 4.Nf3 Be7~ 5.Bf4 O-O 6.e3
1.d4 d5^ 2.c4^ e6 3.Nc3 Nf6^ 4.Bg5 c5 5.cxd5 cxd4 6.Qxd4 Be7
1.d4 d5^ 2.c4^ e6 3.Nc3 Nf6^ 4.cxd5 exd5 5.Bg5 c6 6.Qc2 Be7
1.d4 d5^ 2.c4^ e6 3.Nc3 c5 4.cxd5 exd5 5.Nf3 Nc6 6.e3 Nf6 7.Be2 cxd4 8.Nxd4 Bd6 9.O-O O-O
1.d4 d5^ 2.c4^ e6 3.Nc3 c5 4.cxd5 exd5 5.Nf3 Nc6 6.g3 Nf6 7.Bg2 Be7 8.O-O O-O
1.d4 d5^ 2.c4^ e6 3.Nc3 c5 4.cxd5 exd5 5.Nf3 Nc6 6.g3 c4~ 7.Bg2
1.d4 d5^ 2.c4^ e6 3.Nc3 c5 4.cxd5 cxd4~ 5.Qa4+ Bd7 6.Qxd4
1.d4 d5^ 2.c4^ e6 3.Nc3 Be7 4.cxd5 exd5 5.Bf4 Nf6 6.e3 O-O 7.Qc2 c6
1.d4 d5^ 2.c4^ e6 3.Nc3 Be7 4.cxd5 exd5 5.Bf4 c6 6.e3 Bf5 7.Nge2
1.d4 d5^ 2.c4^ e5~ 3.dxe5 d4 4.Nf3 Nc6 5.g3 Bg4 6.Bg2
1.d4 d5^ 2.c4^ c5~ 3.cxd5
1.d4 d5^ 2.c4^ Nf6~ 3.cxd5 Nxd5 4.e4
1.d4 d5^ 2.c4^ Bf5~ 3.Qb3
1.d4 d5^ 2.Nf3 Nf6 3.e3 e6 4.Bd3 c5 5.c3 Nc6
1.d4 d5^ 2.Nf3 Nf6 3.c4
1.d4 d5^ 2.Nf3 e6~ 3.Nc3
1.d4 d5^ 2.Nf3 Nc6~ 3.c4 dxc4~ 4.d5
1.d4 d5^ 2.Nc3~ Nf6 3.Bg5 Nbd7 4.Nf3 h6 5.Bh4 e6
1.d4 d5^ 2.Nc3~ Nf6 3.Bg5 Nbd7 4.Qd3 c5 5.e4 cxd4
1.d4 d5^ 2.Nc3~ Nf6 3.Bg5 Nbd7 4.f3 c6 5.e4 dxe4
1.d4 d5^ 2.Nc3~ Nf6 3.Bg5 Bf5 4.Bxf6 exf6
1.d4 d5^ 2.e4~ dxe4 3.Nc3 Nf6
1.d4 d5^ 2.e3~ Nc6
1.d4 d5^ 2.Bf4~ Nf6
1.d4 d5^ 2.Bg5~ h6
1.d4 Nf6 2.c4 e6^ 3.Nc3^ Bb4 4.e3 O-O 5.Bd3 d5^ 6.Nf3 c5 7.O-O Nc6 8.a3 Bxc3 9.bxc3 Qc7 10.cxd5
1.d4 Nf6 2.c4 e6^ 3.Nc3^ Bb4 4.e3 O-O 5.Bd3 d5^ 6.Nf3 c5 7.O-O Nc6 8.a3 Bxc3 9.bxc3 dxc4 10.Bxc4 Qc7 11.Bd3
1.d4 Nf6 2.c4 e6^ 3.Nc3^ Bb4 4.e3 O-O 5.Bd3 d5^ 6.Nf3 c5 7.O-O Nc6 8.a3 Bxc3 9.bxc3 b6~ 10.cxd5
1.d4 Nf6 2.c4 e6^ 3.Nc3^ Bb4 4.e3 O-O 5.Bd3 d5^ 6.Nf3 c5 7.O-O Nc6 8.a3 dxc4~ 9.Bxc4
1.d4 Nf6 2.c4 e6^ 3.Nc3^ Bb4 4.e3 O-O 5.Bd3 d5^ 6.Nf3 c5 7.O-O dxc4~ 8.Bxc4 Nbd7 9.a3
1.d4 Nf6 2.c4 e6^ 3.Nc3^ Bb4 4.e3 O-O 5.Bd3 d5^ 6.Nf3 c5 7.O-O dxc4~ 8.Bxc4 b6 9.a3
1.d4 Nf6 2.c4 e6^ 3.Nc3^ Bb4 4.e3 O-O 5.Bd3 d5^ 6.Nf3 b6~ 7.O-O Bb7 8.cxd5
1.d4 Nf6 2.c4 e6^ 3.Nc3^ Bb4 4.e3 O-O 5.Bd3 d5^ 6.Nf3 Nc6~ 7.O-O
1.d4 Nf6 2.c4 e6^ 3.Nc3^ Bb4 4.e3 O-O 5.Bd3 c5 6.d5 exd5 7.cxd5 d6
1.d4 Nf6 2.c4 e6^ 3.Nc3^ Bb4 4.e3 O-O 5.Bd3 c5 6.d5 b5~ 7.dxe6
1.d4 Nf6 2.c4 e6^ 3.Nc3^ Bb4 4.e3 O-O 5.Ne2~ d5 6.a3 Be7
1.d4 Nf6 2.c4 e6^ 3.Nc3^ Bb4 4.e3 c5 5.Ne2 d5 6.a3 Bxc3+ 7.Nxc3 cxd4 8.exd4
1.d4 Nf6 2.c4 e6^ 3.Nc3^ Bb4 4.e3 c5 5.Ne2 d5 6.a3 Ba5~ 7.dxc5
1.d4 Nf6 2.c4 e6^ 3.Nc3^ Bb4 4.e3 c5 5.Ne2 cxd4 6.exd4 O-O 7.a3 Be7 8.d5
1.d4 Nf6 2.c4 e6^ 3.Nc3^ Bb4 4.e3 c5 5.Ne2 cxd4 6.exd4 d5~ 7.c5
1.d4 Nf6 2.c4 e6^ 3.Nc3^ Bb4 4.e3 c5 5.Bd3 Nc6 6.Nf3 Bxc3+ 7.bxc3 d6 8.e4
1.d4 Nf6 2.c4 e6^ 3.Nc3^ Bb4 4.e3 c5 5.Bd3 Nc6 6.Ne2~ cxd4
1.d4 Nf6 2.c4 e6^ 3.Nc3^ Bb4 4.e3 b6 5.Ne2 Ba6 6.a3 Be7 7.Nf4 d5 8.cxd5 Bxf1 9.Kxf1
1.d4 Nf6 2.c4 e6^ 3.Nc3^ Bb4 4.e3 b6 5.Ne2 Ba6 6.a3 Bxc3+~ 7.Nxc3
1.d4 Nf6 2.c4 e6^ 3.Nc3^ Bb4 4.e3 b6 5.Ne2 Bb7 6.a3 Be7 7.d5 O-O 8.e4 d6
1.d4 Nf6 2.c4 e6^ 3.Nc3^ Bb4 4.e3 b6 5.Ne2 Bb7 6.a3 Bxc3+~ 7.Nxc3
1.d4 Nf6 2.c4 e6^ 3.Nc3^ Bb4 4.e3 b6 5.Ne2 Ne4~ 6.Bd2
1.d4 Nf6 2.c4 e6^ 3.Nc3^ Bb4 4.e3 b6 5.Bd3 Bb7 6.Nf3 O-O 7.O-O c5
1.d4 Nf6 2.c4 e6^ 3.Nc3^ Bb4 4.e3 b6 5.Bd3 Bb7 6.Nf3 Ne4 7.O-O Bxc3 8.bxc3
1.d4 Nf6 2.c4 e6^ 3.Nc3^ Bb4 4.e3 b6 5.Bd3 Bb7 6.Nf3 Bxc3+~ 7.bxc3
1.d4 Nf6 2.c4 e6^ 3.Nc3^ Bb4 4.e3 b6 5.f3~ c5
1.d4 Nf6 2.c4 e6^ 3.Nc3^ Bb4 4.e3 Nc6 5.Nf3 d6 6.Be2 e5 7.O-O
1.d4 Nf6 2.c4 e6^ 3.Nc3^ Bb4 4.e3 Nc6 5.Ne2 d5 6.a3 Be7 7.cxd5 exd5
1.d4 Nf6 2.c4 e6^ 3.Nc3^ Bb4 4.e3 d5~ 5.a3
1.d4 Nf6 2.c4 e6^ 3.Nc3^ Bb4 4.e3 Bxc3+~ 5.bxc3
1.d4 Nf6 2.c4 e6^ 3.Nc3^ Bb4 4.a3 Bxc3+ 5.bxc3 O-O 6.f3 d5 7.cxd5 exd5 8.e3 Bf5
1.d4 Nf6 2.c4 e6^ 3.Nc3^ Bb4 4.a3 Bxc3+ 5.bxc3 O-O 6.f3 d6~ 7.e4
1.d4 Nf6 2.c4 e6^ 3.Nc3^ Bb4 4.a3 Bxc3+ 5.bxc3 O-O 6.e3 c5 7.Bd3 Nc6 8.Ne2 b6 9.e4
1.d4 Nf6 2.c4 e6^ 3.Nc3^ Bb4 4.a3 Bxc3+ 5.bxc3 c5 6.f3 d5 7.cxd5 Nxd5 8.dxc5
1.d4 Nf6 2.c4 e6^ 3.Nc3^ Bb4 4.a3 Bxc3+ 5.bxc3 c5 6.e3 O-O
1.d4 Nf6 2.c4 e6^ 3.Nc3^ Bb4 4.a3 Bxc3+ 5.bxc3 b6 6.f3 Nc6 7.e4 Ba6 8.e5 Ng8 9.Nh3
1.d4 Nf6 2.c4 e6^ 3.Nc3^ Bb4 4.a3 Bxc3+ 5.bxc3 Ne4~ 6.Qc2
1.d4 Nf6 2.c4 e6^ 3.Nc3^ Bb4 4.a3 Bxc3+ 5.bxc3 d6~ 6.f3
1.d4 Nf6 2.c4 e6^ 3.Nc3^ Bb4 4.Bg5 h6 5.Bh4 c5 6.d5 d6 7.e3
1.d4 Nf6 2.c4 e6^ 3.Nc3^ Bb4 4.Bg5 h6 5.Bh4 c5 6.d5 b5~ 7.dxe6
1.d4 Nf6 2.c4 e6^ 3.Nc3^ Bb4 4.Bg5 h6 5.Bh4 g5~ 6.Bg3
1.d4 Nf6 2.c4 e6^ 3.Nc3^ Bb4 4.Bg5 c5 5.d5 d6
1.d4 Nf6 2.c4 e6^ 3.Nc3^ Bb4 4.Bg5 c5 5.e3 Qa5
1.d4 Nf6 2.c4 e6^ 3.Nc3^ Bb4 4.Qc2 c5 5.dxc5 O-O 6.Nf3 Nc6 7.Bf4 Bxc5 8.a3 d5
1.d4 Nf6 2.c4 e6^ 3.Nc3^ Bb4 4.Qc2 c5 5.dxc5 O-O 6.Nf3 Na6~ 7.Bd2
1.d4 Nf6 2.c4 e6^ 3.Nc3^ Bb4 4.Qc2 c5 5.dxc5 O-O 6.Bf4~ Na6
1.d4 Nf6 2.c4 e6^ 3.Nc3^ Bb4 4.Qc2 c5 5.dxc5 Nc6 6.Nf3 O-O
1.d4 Nf6 2.c4 e6^ 3.Nc3^ Bb4 4.Qc2 c5 5.dxc5 Na6~ 6.a3
1.d4 Nf6 2.c4 e6^ 3.Nc3^ Bb4 4.Qc2 Nc6 5.Nf3 d6 6.Bd2 Qe7 7.a3 Bxc3 8.Bxc3
1.d4 Nf6 2.c4 e6^ 3.Nc3^ Bb4 4.Qc2 d5~ 5.a3 Bxc3+ 6.Qxc3 Ne4 7.Qc2
1.d4 Nf6 2.c4 e6^ 3.Nc3^ Bb4 4.Qc2 b6~ 5.e4
1.d4 Nf6 2.c4 e6^ 3.Nc3^ Bb4 4.Qc2 d6~ 5.Nf3
1.d4 Nf6 2.c4 e6^ 3.Nc3^ Bb4 4.Nf3 c5 5.g3
1.d4 Nf6 2.c4 e6^ 3.Nc3^ Bb4 4.Nf3 d5 5.e3 O-O 6.Bd3
1.d4 Nf6 2.c4 e6^ 3.Nc3^ Bb4 4.Nf3 O-O 5.Bg5 h6 6.Bh4 c5
1.d4 Nf6 2.c4 e6^ 3.Nc3^ Bb4 4.Qb3 c5 5.dxc5 Nc6 6.Nf3 Ne4 7.Bd2
1.d4 Nf6 2.c4 e6^ 3.Nc3^ Bb4 4.Qb3 Nc6 5.Nf3 d5 6.a3 dxc4 7.Qxc4 Bd6
1.d4 Nf6 2.c4 e6^ 3.Nc3^ Bb4 4.Qb3 Nc6 5.Nf3 d5 6.Bg5~ h6
1.d4 Nf6 2.c4 e6^ 3.Nc3^ Bb4 4.f3 d5 5.a3 Be7 6.e4 dxe4 7.fxe4 e5
1.d4 Nf6 2.c4 e6^ 3.Nc3^ Bb4 4.g3 c5 5.Nf3 cxd4 6.Nxd4 Ne4 7.Qd3
1.d4 Nf6 2.c4 e6^ 3.Nc3^ Bb4 4.g3 O-O 5.Bg2 d5 6.Nf3 dxc4 7.O-O
1.d4 Nf6 2.c4 e6^ 3.Nc3^ Bb4 4.e4~ Nxe4 5.Qg4 Nxc3
1.d4 Nf6 2.c4 e6^ 3.Nc3^ Bb4 4.Qd3~ c5 5.d5 O-O
1.d4 Nf6 2.c4 e6^ 3.Nc3^ Bb4 4.Bd2~ O-O 5.Nf3 b6
1.d4 Nf6 2.c4 e6^ 3.Nc3^ c5~ 4.d5 exd5 5.Nxd5 Nxd5 6.Qxd5
1.d4 Nf6 2.c4 e6^ 3.Nc3^ c5~ 4.d5 exd5 5.cxd5 d6 6.e4 g6 7.Nf3
1.d4 Nf6 2.c4 e6^ 3.Nf3 b6^ 4.g3 Bb7 5.Bg2 Be7^ 6.O-O O-O 7.Nc3 d5 8.Ne5 Na6 9.Bg5 c5
1.d4 Nf6 2.c4 e6^ 3.Nf3 b6^ 4.g3 Bb7 5.Bg2 Be7^ 6.O-O O-O 7.Nc3 Ne4 8.Qc2 Nxc3 9.Qxc3 c5 10.Rd1
1.d4 Nf6 2.c4 e6^ 3.Nf3 b6^ 4.g3 Bb7 5.Bg2 Be7^ 6.O-O O-O 7.Nc3 Ne4 8.Bd2 Bf6 9.Qc2 Nxd2 10.Qxd2
1.d4 Nf6 2.c4 e6^ 3.Nf3 b6^ 4.g3 Bb7 5.Bg2 Be7^ 6.O-O O-O 7.Nc3 Ne4 8.Nxe4 Bxe4 9.Ne1 Bxg2 10.Nxg2 d5
1.d4 Nf6 2.c4 e6^ 3.Nf3 b6^ 4.g3 Bb7 5.Bg2 Bb4+ 6.Bd2 Bxd2+^ 7.Qxd2 O-O 8.Nc3 d6
1.d4 Nf6 2.c4 e6^ 3.Nf3 b6^ 4.g3 Bb7 5.Bg2 Bb4+ 6.Bd2 Be7 7.O-O Na6 8.Nc3
1.d4 Nf6 2.c4 e6^ 3.Nf3 b6^ 4.g3 Ba6~ 5.b3 Bb4+ 6.Bd2 Be7 7.Nc3
1.d4 Nf6 2.c4 e6^ 3.Nf3 b6^ 4.Nc3 Bb7 5.Bg5
1.d4 Nf6 2.c4 e6^ 3.Nf3 b6^ 4.Nc3 Bb4 5.e3
1.d4 Nf6 2.c4 e6^ 3.Nf3 b6^ 4.Bf4 Bb7 5.e3 Be7 6.h3 O-O 7.Nc3 d5
1.d4 Nf6 2.c4 e6^ 3.Nf3 b6^ 4.Bf4 Bb7 5.e3 Be7 6.Nc3~ Nh5
1.d4 Nf6 2.c4 e6^ 3.Nf3 b6^ 4.Bf4 Bb7 5.e3 Bb4+~ 6.Nfd2 O-O 7.Bd3
1.d4 Nf6 2.c4 e6^ 3.Nf3 b6^ 4.e3~ Bb7 5.Bd3 Be7 6.Nc3 d5
1.d4 Nf6 2.c4 e6^ 3.Nf3 b6^ 4.a3~ Bb7 5.Nc3 d5
1.d4 Nf6 2.c4 e6^ 3.Nf3 Bb4+~ 4.Bd2 Qe7 5.g3 O-O 6.Bg2 Bxd2+ 7.Nbxd2
1.d4 Nf6 2.c4 e6^ 3.Nf3 Bb4+~ 4.Bd2 Qe7 5.e3 Nc6 6.Bd3
1.d4 Nf6 2.c4 e6^ 3.g3~ d5 4.Bg2 dxc4 5.Nf3 a6 6.O-O b5
1.d4 Nf6 2.c4 g6 3.Nc3 Bg7 4.e4 d6 5.Nf3 O-O 6.Be2 e5^ 7.Be3 Nbd7 8.O-O c6 9.Qc2
1.d4 Nf6 2.c4 g6 3.Nc3 Bg7 4.e4 d6 5.Nf3 O-O 6.Be2 e5^ 7.Be3 Ng4~ 8.Bg5
1.d4 Nf6 2.c4 g6 3.Nc3 Bg7 4.e4 d6 5.Nf3 O-O 6.Be2 e5^ 7.Be3 Nc6~ 8.dxe5
1.d4 Nf6 2.c4 g6 3.Nc3 Bg7 4.e4 d6 5.Nf3 O-O 6.Be2 e5^ 7.d5 Nbd7 8.Bg5 h6 9.Bh4
1.d4 Nf6 2.c4 g6 3.Nc3 Bg7 4.e4 d6 5.Nf3 O-O 6.Be2 e5^ 7.d5 a5~ 8.Bg5 h6 9.Bh4
1.d4 Nf6 2.c4 g6 3.Nc3 Bg7 4.e4 d6 5.Nf3 O-O 6.Be2 e5^ 7.d5 Na6~ 8.Bg5
1.d4 Nf6 2.c4 g6 3.Nc3 Bg7 4.e4 d6 5.Nf3 O-O 6.Be2 e5^ 7.d5 c5~ 8.Bg5 h6 9.Bh4
1.d4 Nf6 2.c4 g6 3.Nc3 Bg7 4.e4 d6 5.Nf3 O-O 6.Be2 e5^ 7.dxe5 dxe5 8.Qxd8 Rxd8 9.Bg5 Re8
1.d4 Nf6 2.c4 g6 3.Nc3 Bg7 4.e4 d6 5.Nf3 O-O 6.Be2 e5^ 7.O-O~ Nbd7 8.d5 Nc5
1.d4 Nf6 2.c4 g6 3.Nc3 Bg7 4.e4 d6 5.Nf3 O-O 6.Be2 e5^ 7.O-O~ Nbd7 8.Re1 c6
1.d4 Nf6 2.c4 g6 3.Nc3 Bg7 4.e4 d6 5.Nf3 O-O 6.Be2 Bg4~ 7.Be3 Nfd7 8.Ng1
1.d4 Nf6 2.c4 g6 3.Nc3 Bg7 4.e4 d6 5.Nf3 O-O 6.Be2 c5~ 7.O-O
1.d4 Nf6 2.c4 g6 3.Nc3 Bg7 4.e4 d6 5.Be2 O-O 6.Bg5 c5 7.d5 h6 8.Bf4 e6
1.d4 Nf6 2.c4 g6 3.Nc3 Bg7 4.e4 d6 5.Be2 O-O 6.Bg5 h6 7.Be3 e5 8.d5
1.d4 Nf6 2.c4 g6 3.Nc3 Bg7 4.e4 d6 5.Be2 O-O 6.Bg5 e5~ 7.dxe5 dxe5 8.Qxd8
1.d4 Nf6 2.c4 g6 3.Nc3 Bg7 4.e4 d6 5.Be2 O-O 6.Bg5 Nbd7~ 7.Qd2 c5 8.d5
1.d4 Nf6 2.c4 g6 3.Nc3 Bg7 4.e4 d6 5.Be2 O-O 6.Bg5 c6~ 7.Nf3
1.d4 Nf6 2.c4 g6 3.Nc3 Bg7 4.e4 d6 5.g3~ O-O 6.Bg2 e5
1.d4 Nf6 2.c4 g6 3.Nc3 Bg7 4.e4 d6 5.h3~ O-O 6.Bg5 h6
1.d4 Nf6 2.c4 g6 3.Nc3 Bg7 4.e4 d6 5.Nge2~ O-O 6.Ng3 c5
1.d4 Nf6 2.c4 g6 3.Nc3 Bg7 4.e4 d6 5.Bd3~ O-O 6.Nge2 e5
1.d4 Nf6 2.c4 g6 3.Nc3 Bg7 4.e4 d6 5.Bg5~ h6 6.Bh4 c5 7.d5 e6
1.d4 Nf6 2.c4 g6 3.Nc3 Bg7 4.e4 d6 5.f4~ O-O 6.Nf3 c5
1.d4 Nf6 2.c4 g6 3.Nc3 Bg7 4.e4 d6 5.f3~ O-O 6.Be3 c6 7.Bd3 a6
1.d4 Nf6 2.c4 g6 3.Nc3 Bg7 4.e4 d6 5.f3~ O-O 6.Be3 Nc6 7.Nge2 a6
1.d4 Nf6 2.c4 g6 3.Nc3 Bg7 4.e4 d6 5.f3~ O-O 6.Be3 e5 7.Nge2 c6
1.d4 Nf6 2.c4 g6 3.Nc3 Bg7 4.e4 d6 5.f3~ O-O 6.Be3 e5 7.d5 c6
1.d4 Nf6 2.c4 g6 3.Nc3 Bg7 4.e4 d6 5.f3~ O-O 6.Bg5 c5 7.d5 e6
1.d4 Nf6 2.c4 g6 3.Nc3 Bg7 4.e4 O-O~ 5.Be3 d6 6.f3
1.d4 Nf6 2.c4 g6 3.Nc3 Bg7 4.Nf3 d6 5.g3^ O-O 6.Bg2 c6 7.O-O Qa5 8.h3 e5 9.e4 exd4
1.d4 Nf6 2.c4 g6 3.Nc3 Bg7 4.Nf3 d6 5.g3^ O-O 6.Bg2 c6 7.O-O Qa5 8.h3 Be6 9.d5 cxd5 10.Nd4
1.d4 Nf6 2.c4 g6 3.Nc3 Bg7 4.Nf3 d6 5.g3^ O-O 6.Bg2 Nc6 7.O-O e5 8.d5 Ne7 9.c5
1.d4 Nf6 2.c4 g6 3.Nc3 Bg7 4.Nf3 d6 5.g3^ O-O 6.Bg2 Nc6 7.O-O e5 8.dxe5 Nxe5 9.Nxe5 dxe5
1.d4 Nf6 2.c4 g6 3.Nc3 Bg7 4.Nf3 d6 5.g3^ O-O 6.Bg2 Nc6 7.O-O a6 8.h3 Rb8 9.Be3 b5
1.d4 Nf6 2.c4 g6 3.Nc3 Bg7 4.Nf3 d6 5.g3^ O-O 6.Bg2 Nc6 7.O-O a6 8.h3 Rb8 9.e4 b5
1.d4 Nf6 2.c4 g6 3.Nc3 Bg7 4.Nf3 d6 5.g3^ O-O 6.Bg2 Nc6 7.O-O a6 8.h3 e5 9.d5
1.d4 Nf6 2.c4 g6 3.Nc3 Bg7 4.Nf3 d6 5.g3^ O-O 6.Bg2 Nc6 7.O-O a6 8.d5 Na5 9.Nd2 c5
1.d4 Nf6 2.c4 g6 3.Nc3 Bg7 4.Nf3 d6 5.g3^ O-O 6.Bg2 Nc6 7.O-O a6 8.b3~ Rb8 9.Bb2 b5
1.d4 Nf6 2.c4 g6 3.Nc3 Bg7 4.Nf3 d6 5.g3^ O-O 6.Bg2 Nc6 7.O-O Bg4~ 8.d5 Bxf3 9.exf3
1.d4 Nf6 2.c4 g6 3.Nc3 Bg7 4.Nf3 d6 5.g3^ O-O 6.Bg2 Nc6 7.O-O Bf5~ 8.d5
1.d4 Nf6 2.c4 g6 3.Nc3 Bg7 4.Nf3 d6 5.g3^ O-O 6.Bg2 Nbd7 7.O-O e5 8.e4 c6 9.h3 Re8
1.d4 Nf6 2.c4 g6 3.Nc3 Bg7 4.Nf3 d6 5.g3^ O-O 6.Bg2 Nbd7 7.O-O e5 8.e3~ Re8 9.b3 c6
1.d4 Nf6 2.c4 g6 3.Nc3 Bg7 4.Nf3 d6 5.g3^ O-O 6.Bg2 c5 7.O-O Nc6 8.dxc5 dxc5
1.d4 Nf6 2.c4 g6 3.Nc3 Bg7 4.Nf3 d6 5.g3^ O-O 6.Bg2 c5 7.d5 Na6 8.O-O Nc7
1.d4 Nf6 2.c4 g6 3.Nc3 Bg7 4.Nf3 d6 5.g3^ O-O 6.Bg2 Bg4~ 7.Qb3
1.d4 Nf6 2.c4 g6 3.Nc3 Bg7 4.Nf3 d6 5.g3^ O-O 6.Bg2 Bf5~ 7.Qb3
1.d4 Nf6 2.c4 g6 3.Nc3 Bg7 4.Nf3 d6 5.Bg5 O-O 6.e3 Bf5 7.Be2 Ne4 8.Nxe4
1.d4 Nf6 2.c4 g6 3.Nc3 Bg7 4.Nf3 d6 5.Bg5 h6~ 6.Bh4 g5 7.Bg3
1.d4 Nf6 2.c4 g6 3.Nc3 Bg7 4.Nf3 d6 5.e3~ O-O 6.Be2 Nbd7
1.d4 Nf6 2.c4 g6 3.Nc3 Bg7 4.Nf3 d6 5.Bf4~ O-O 6.h3 Nbd7
1.d4 Nf6 2.c4 g6 3.Nc3 d5 4.cxd5 Nxd5 5.e4 Nxc3 6.bxc3 Bg7 7.Bc4 O-O 8.Ne2 c5 9.O-O Nc6 10.Be3
1.d4 Nf6 2.c4 g6 3.Nc3 d5 4.cxd5 Nxd5 5.e4 Nxc3 6.bxc3 Bg7 7.Bc4 O-O 8.Ne2 Nc6 9.O-O b6 10.Be3
1.d4 Nf6 2.c4 g6 3.Nc3 d5 4.cxd5 Nxd5 5.e4 Nxc3 6.bxc3 Bg7 7.Bc4 O-O 8.Ne2 Qd7~ 9.O-O
1.d4 Nf6 2.c4 g6 3.Nc3 d5 4.cxd5 Nxd5 5.e4 Nxc3 6.bxc3 Bg7 7.Nf3 c5 8.Bb5+ Nc6 9.O-O
1.d4 Nf6 2.c4 g6 3.Nc3 d5 4.cxd5 Nxd5 5.e4 Nxc3 6.bxc3 Bg7 7.Nf3 c5 8.Be3 O-O 9.Rc1
1.d4 Nf6 2.c4 g6 3.Nc3 d5 4.Nf3 Bg7 5.Qb3 dxc4 6.Qxc4 O-O 7.e4 Bg4 8.Be3 Nfd7
1.d4 Nf6 2.c4 g6 3.Nc3 d5 4.Nf3 Bg7 5.Qb3 dxc4 6.Qxc4 O-O 7.e4 a6~ 8.Qb3
1.d4 Nf6 2.c4 g6 3.Nc3 d5 4.Nf3 Bg7 5.Qb3 dxc4 6.Qxc4 O-O 7.e4 Na6~ 8.Be2
1.d4 Nf6 2.c4 g6 3.Nc3 d5 4.Nf3 Bg7 5.Qb3 dxc4 6.Qxc4 O-O 7.e4 c6~ 8.Qb3
1.d4 Nf6 2.c4 g6 3.Nc3 d5 4.Nf3 Bg7 5.Qb3 c6~ 6.cxd5 cxd5 7.Bg5
1.d4 Nf6 2.c4 g6 3.Nc3 d5 4.Nf3 Bg7 5.Bf4 O-O 6.e3 c5 7.dxc5 Qa5
1.d4 Nf6 2.c4 g6 3.Nc3 d5 4.Nf3 Bg7 5.Bf4 O-O 6.e3 c6~ 7.Qb3
1.d4 Nf6 2.c4 g6 3.Nc3 d5 4.Nf3 Bg7 5.Bf4 O-O 6.Rc1~ c5
1.d4 Nf6 2.c4 g6 3.Nc3 d5 4.Nf3 Bg7 5.Bg5 Ne4 6.cxd5 Nxg5 7.Nxg5 e6
1.d4 Nf6 2.c4 g6 3.Nc3 d5 4.Nf3 Bg7 5.e3~ O-O 6.Bd2 c5 7.dxc5 Na6 8.cxd5
1.d4 Nf6 2.c4 g6 3.Nc3 d5 4.Nf3 Bg7 5.Qa4+~ Bd7 6.Qb3 dxc4
1.d4 Nf6 2.c4 g6 3.Nc3 d5 4.Nf3 Bg7 5.cxd5~ Nxd5 6.e4 Nxc3
1.d4 Nf6 2.c4 g6 3.Nc3 d5 4.Bf4 Bg7 5.e3 O-O 6.cxd5 Nxd5 7.Nxd5 Qxd5
1.d4 Nf6 2.c4 g6 3.Nc3 d5 4.Bf4 Bg7 5.e3 O-O 6.Nf3
1.d4 Nf6 2.c4 g6 3.Nc3 d5 4.Bg5 Ne4 5.Bh4 Nxc3 6.bxc3 Bg7 7.e3 c5
1.d4 Nf6 2.c4 g6 3.Nc3 d5 4.Bg5 Ne4 5.Bh4 c5 6.cxd5 Nxc3 7.bxc3 Qxd5
1.d4 Nf6 2.c4 g6 3.Nf3~ Bg7
1.d4 Nf6 2.c4 g6 3.f3~ d5 4.cxd5 Nxd5 5.e4 Nb6
1.d4 Nf6 2.c4 g6 3.g3~ d5 4.Bg2 Bg7 5.cxd5 Nxd5
1.d4 Nf6 2.c4 g6 3.g3~ d5 4.Bg2 Bg7 5.Nf3 O-O
1.d4 Nf6 2.c4 c5 3.d5 e6 4.Nc3 exd5 5.cxd5 d6 6.e4 g6 7.Nf3 Bg7 8.Be2 O-O 9.O-O Re8 10.Qc2
1.d4 Nf6 2.c4 c5 3.d5 e6 4.Nc3 exd5 5.cxd5 d6 6.e4 g6 7.Nf3 Bg7 8.Bg5 h6 9.Bh4
1.d4 Nf6 2.c4 c5 3.d5 e6 4.Nc3 exd5 5.cxd5 d6 6.e4 g6 7.Bd3 Bg7 8.Nge2 O-O 9.O-O
1.d4 Nf6 2.c4 c5 3.d5 e6 4.Nc3 exd5 5.cxd5 d6 6.e4 g6 7.f3~ Bg7
1.d4 Nf6 2.c4 c5 3.d5 e6 4.Nc3 exd5 5.cxd5 d6 6.e4 g6 7.f4~ Bg7 8.e5 Nfd7 9.Nb5
1.d4 Nf6 2.c4 c5 3.d5 e6 4.Nc3 exd5 5.cxd5 d6 6.e4 g6 7.f4~ Bg7 8.Bb5+ Nfd7
1.d4 Nf6 2.c4 c5 3.d5 e6 4.Nc3 exd5 5.cxd5 d6 6.e4 g6 7.f4~ Bg7 8.Nf3 O-O
1.d4 Nf6 2.c4 c5 3.d5 e6 4.Nc3 exd5 5.cxd5 d6 6.Nf3 g6 7.g3 Bg7 8.Bg2 O-O 9.O-O Re8
1.d4 Nf6 2.c4 c5 3.d5 e6 4.Nc3 exd5 5.cxd5 d6 6.Nf3 g6 7.Bg5 h6 8.Bh4 g5 9.Bg3 Nh5
1.d4 Nf6 2.c4 c5 3.d5 e6 4.Nc3 exd5 5.cxd5 d6 6.Nf3 g6 7.Bg5 Bg7 8.e4
1.d4 Nf6 2.c4 c5 3.d5 e6 4.Nc3 exd5 5.cxd5 d6 6.Nf3 g6 7.Nd2 Bg7 8.Nc4 O-O
1.d4 Nf6 2.c4 c5 3.d5 e6 4.Nc3 exd5 5.cxd5 d6 6.Nf3 g6 7.Bf4~ a6 8.a4 Bg7
1.d4 Nf6 2.c4 c5 3.d5 e6 4.Nc3 exd5 5.cxd5 d6 6.e4
1.d4 Nf6 2.c4 c5 3.d5 e5 4.Nc3 d6 5.e4 Be7 6.Nf3 O-O 7.Qe2
1.d4 Nf6 2.c4 c5 3.d5 e5 4.Nc3 d6 5.e4 Be7 6.Bd3 O-O 7.Nge2
1.d4 Nf6 2.c4 c5 3.d5 b5~ 4.cxb5 a6 5.Nc3 axb5 6.e4
1.d4 Nf6 2.c4 e5~ 3.dxe5 Ng4 4.Nf3 Bc5 5.e3
1.d4 Nf6 2.c4 e5~ 3.dxe5 Ne4 4.Nf3 Nc6 5.Nbd2
1.d4 Nf6 2.c4 d6~ 3.Nc3 Bf5 4.f3 e5 5.e4
1.d4 Nf6 2.c4 d6~ 3.Nc3 e5 4.Nf3
1.d4 Nf6 2.c4 b6~ 3.Nc3 Bb7 4.Qc2
1.d4 Nf6 2.Nc3~ d5 3.Bg5 Nbd7 4.Nf3 h6 5.Bh4 e6
1.d4 Nf6 2.Nc3~ d5 3.Bg5 Nbd7 4.Qd3 c5 5.e4 cxd4
1.d4 Nf6 2.Nc3~ d5 3.Bg5 Nbd7 4.f3 c6 5.e4 dxe4
1.d4 Nf6 2.Nf3~ d5
1.d4 Nf6 2.Bg5~ Ne4 3.Bh4 g5 4.f3 gxh4
1.d4 Nf6 2.Bg5~ Ne4 3.Bf4 d5 4.f3 Nf6
1.d4 Nf6 2.Bf4~ d5
1.d4 Nf6 2.e3~ d5
1.d4 f5~ 2.c4 Nf6 3.g3 e6 4.Bg2 Be7 5.Nf3
1.d4 f5~ 2.c4 Nf6 3.g3 g6 4.Bg2 Bg7 5.Nf3
1.d4 c5~ 2.d5 d6 3.Nc3 g6 4.e4
1.d4 c5~ 2.d5 e5 3.e4 d6 4.c4
1.d4 a6~ 2.e4
1.d4 e6~ 2.e4
1.d4 d6~ 2.e4
1.d4 Nc6~ 2.d5 Ne5 3.e4
1.c4 e5 2.Nc3 Nf6 3.Nf3 Nc6 4.e3^ Bb4 5.Qc2 O-O 6.Nd5 Re8 7.Bd3 g6
1.c4 e5 2.Nc3 Nf6 3.Nf3 Nc6 4.g3 Bb4 5.Nd5 e4 6.Nh4 Bc5 7.Bg2 d6 8.O-O Be6 9.Nxf6+
1.c4 e5 2.Nc3 Nf6 3.Nf3 Nc6 4.g3 Bb4 5.Nd5 e4 6.Nh4 Bc5 7.Bg2 d6 8.Qc2~ O-O
1.c4 e5 2.Nc3 Nf6 3.Nf3 Nc6 4.g3 Bb4 5.Bg2 O-O 6.O-O e4 7.Ne1 Bxc3 8.dxc3 h6
1.c4 e5 2.Nc3 Nf6 3.Nf3 Nc6 4.d4~ exd4 5.Nxd4 Bb4
1.c4 e5 2.Nc3 Nf6 3.Nf3 e4~ 4.Ng5 b5 5.d3 exd3 6.cxb5
1.c4 e5 2.Nc3 Nf6 3.g3~ Bb4 4.Bg2 O-O 5.Nf3 Re8
1.c4 e5 2.Nc3 d6~ 3.g3 c5 4.Bg2 Nc6 5.d3
1.c4 e5 2.Nc3 d6~ 3.Nf3
1.c4 e5 2.Nc3 Nc6~ 3.Nf3 Nf6 4.d4 exd4 5.Nxd4 Bb4 6.g3
1.c4 e5 2.Nc3 Nc6~ 3.Nf3 Nf6 4.d4 e4 5.Nd2
1.c4 e5 2.Nc3 Nc6~ 3.Nf3 f5~ 4.d4 e4 5.d5
1.c4 e5 2.Nc3 Nc6~ 3.Nf3 g6 4.d4 exd4 5.Nxd4
1.c4 e5 2.Nf3~ e4 3.Nd4 Nc6
1.c4 e5 2.d4~ exd4
1.c4 e5 2.g3~ Nf6
1.c4 c5 2.Nf3^ Nf6 3.Nc3 Nc6 4.d4 cxd4 5.Nxd4 e6 6.g3 Qb6
1.c4 c5 2.Nf3^ Nf6 3.Nc3 e6 4.d4 cxd4 5.Nxd4 Nc6
1.c4 c5 2.Nf3^ Nf6 3.d4 cxd4 4.Nxd4 Nc6 5.Nc3 e6
1.c4 c5 2.Nf3^ Nf6 3.d4 cxd4 4.Nxd4 Nc6 5.Nxc6~ bxc6
1.c4 c5 2.Nc3 Nc6 3.g3 g6 4.Bg2 Bg7 5.a3 a6 6.Rb1 Rb8 7.b4
1.c4 c5 2.Nc3 Nc6 3.g3 g6 4.Bg2 Bg7 5.e3 e6 6.Nge2 Nge7 7.O-O
1.c4 c5 2.b3~ Nc6
1.c4 Nf6 2.Nc3 e6 3.e4 c5 4.e5 Ng8 5.d4^ cxd4 6.Qxd4 Nc6 7.Qe4 d6
1.c4 Nf6 2.Nc3 e6 3.e4 c5 4.e5 Ng8 5.Nf3 Nc6 6.d4 cxd4 7.Nxd4
1.c4 Nf6 2.Nc3 e6 3.e4 d5 4.e5 d4 5.exf6 dxc3 6.bxc3 Qxf6 7.d4
1.c4 Nf6 2.Nc3 e6 3.e4 d5 4.e5 Ne4~ 5.Nxe4 dxe4 6.Qg4
1.c4 Nf6 2.Nc3 e6 3.Nf3 d5 4.d4 Be7 5.Bg5 h6 6.Bh4 O-O 7.e3 b6 8.Rc1 Bb7 9.Bd3 c5
1.c4 Nf6 2.Nc3 e6 3.Nf3 d5 4.d4 Be7 5.Bg5 h6 6.Bh4 O-O 7.Rc1 b6 8.Bxf6 Bxf6 9.cxd5
1.c4 Nf6 2.Nc3 e6 3.Nf3 d5 4.d4 Be7 5.Bf4 O-O 6.e3 c5 7.dxc5
1.c4 Nf6 2.Nc3 e6 3.Nf3 d5 4.d4 Be7 5.g3~ O-O
1.c4 Nf6 2.Nc3 e6 3.d4~ Bb4
1.c4 Nf6 2.Nc3 e6 3.g3~ Bb4
1.c4 Nf6 2.Nc3 e5~ 3.Nf3
1.c4 Nf6 2.Nc3 d5~ 3.cxd5
1.c4 Nf6 2.d4 e6 3.Nc3 Bb4 4.e3 c5 5.Ne2^ cxd4 6.exd4 d5 7.c5 Ne4 8.Bd2 Nxd2
1.c4 Nf6 2.d4 e6 3.Nc3 Bb4 4.e3 c5 5.Ne2^ d5 6.a3 Bxc3+ 7.Nxc3 cxd4 8.exd4 dxc4 9.Bxc4 Nc6
1.c4 Nf6 2.d4 e6 3.Nc3 Bb4 4.e3 c5 5.Bd3 Nc6 6.Nf3 Bxc3+ 7.bxc3
1.c4 Nf6 2.g3~ e6 3.Bg2 d5 4.Nc3 Be7 5.d4 O-O
1.c4 e6 2.Nc3 d5 3.d4
1.c4 e6 2.Nf3 c5 3.e4 Nc6 4.Nc3 Nf6 5.d4 cxd4 6.Nxd4 Bb4 7.Nxc6
1.c4 e6 2.g3~ d5
1.c4 e6 2.d4~ d5
1.c4 e6 2.e3~ c5
1.c4 e6 2.d3~ c5
1.c4 g6~ 2.e4 e5 3.d4 Nf6 4.Nf3 Bb4+ 5.Bd2
1.c4 g6~ 2.e4 Bg7 3.d4 d6 4.Nc3
1.c4 b6~ 2.d4 e6 3.e4 Bb7 4.Nc3 Bb4 5.Bd3
1.c4 b6~ 2.d4 Bb7 3.d5 e6 4.a3
1.c4 Nc6~ 2.d4
1.Nf3 d5 2.c4 d4 3.e3 Nc6 4.exd4 Nxd4 5.Nxd4 Qxd4 6.Nc3 e5 7.d3 Ne7
1.Nf3 d5 2.c4 d4 3.e3 c5 4.a3 Nc6 5.d3 e5 6.Nbd2
1.Nf3 d5 2.c4 d4 3.e3 c5 4.a3 Nc6 5.d3 Nf6~ 6.b4 Bg4 7.bxc5
1.Nf3 d5 2.c4 d4 3.e3 c5 4.a3 Nf6~ 5.d3
1.Nf3 d5 2.c4 d4 3.e3 c5 4.d3~ Nc6
1.Nf3 d5 2.c4 d4 3.e3 c5 4.Bd3~ Nc6
1.Nf3 d5 2.c4 d4 3.e3 c5 4.Be2~ Nc6
1.Nf3 d5 2.c4 d4 3.e3 c5 4.exd4~ cxd4
1.Nf3 d5 2.c4 d4 3.e3 c5 4.e4~ Nc6
1.Nf3 d5 2.c4 d4 3.b4~ g6 4.Bb2 Bg7
1.Nf3 d5 2.c4 d4 3.d3~ c5
1.Nf3 d5 2.c4 d4 3.g3~ c5 4.Bg2 Nc6 5.d3 e5
1.Nf3 d5 2.c4 c6 3.b3 Nf6 4.Bb2 Bf5 5.g3 e6 6.Bg2 h6 7.O-O
1.Nf3 d5 2.c4 e6~ 3.d4 Nc6 4.cxd5
1.Nf3 d5 2.c4 e6~ 3.d4 dxc4 4.Qa4+
1.Nf3 d5 2.c4 e6~ 3.d4 Nf6 4.cxd5
1.Nf3 d5 2.c4 dxc4~ 3.Na3 Nf6 4.Nxc4 g6 5.Nce5
1.Nf3 d5 2.d4
1.Nf3 d5 2.g3~ c5 3.Bg2 Nc6 4.O-O e6
1.Nf3 d5 2.g3~ c5 3.Bg2 Nc6 4.d4 e6
1.Nf3 Nf6 2.d4 d5 3.c4 dxc4
1.Nf3 Nf6 2.c4 c5
1.Nf3 Nf6 2.c4 Nc6~ 3.d4
1.Nf3 Nf6 2.c4 d5~ 3.cxd5
1.Nf3 Nf6 2.c4 d6~ 3.d4
1.Nf3 Nf6 2.g3 g6 3.Bg2 Bg7 4.c4
1.Nf3 Nf6 2.g3 d5 3.Bg2 c5 4.O-O
1.Nf3 Nf6 2.g3 b6~ 3.Bg2 Bb7 4.O-O e6 5.c4
1.Nf3 Nf6 2.g3 b5~ 3.Bg2 Bb7 4.O-O
1.Nf3 f5~ 2.d4 Nf6 3.Nc3
1.Nf3 c5~ 2.e4 Nf6 3.e5 Nd5 4.c4
1.Nf3 Nc6~ 2.e4
1.Nf3 g6~ 2.e4
1.Nf3 e6~ 2.c4 Nc6 3.Nc3
1.Nf3 e6~ 2.c4 d6 3.d4
1.Nf3 e6~ 2.c4 Nf6 3.d4
1.Nf3 d6~ 2.d4 Nf6 3.c4
1.Nf3 d6~ 2.d4 Nc6 3.d5
1.Nf3 d6~ 2.d4 Bf5 3.c4
1.b3~ e5 2.Bb2 Nc6
1.f4~ d5 2.Nf3 Nf6 3.e3 Bg4
1.f4~ d5 2.e3 Nf6 3.Nf3 Bg4
1.g3~ c5 2.Bg2 Nc6 3.e4 g6 4.d3 Bg7
1.Nc3~ e5 2.Nf3 Nc6 3.e3 d5
1.Nc3~ e5 2.d3 d5
1.Nc3~ e5 2.d4 exd4
1.Nc3~ e5 2.e3 d5
1.d3~ d5 2.Bf4 Nf6
1.d3~ d5 2.e3 e5
1.d3~ d5 2.e4 dxe4
1.e3~ e5 2.d4 exd4
1.e3~ e5 2.Nf3 e4 3.Nd4 c5
1.e3~ e5 2.d3 d5
1.a3~ e5
1.h3~ e5
1.a4~ e5
1.f3~ e5
1.c3~ c5
1.h4~ e5
1.b4~ d5 2.Bb2 Nf6
```

</details>
