# Implementation

`src/psion_mods/` holds everything that is not DOSBox:

| File | Contents |
|---|---|
| `psionmain.cpp` | `main()` and the headless video, mapper and joystick stubs. |
| `psion.cpp` | Starting Psion, keyboard input, reading the game state, the UCI controller. |
| `chess.cpp` | Logic that does not depend on the emulator (move derivation, UCI parsing, time allocation, syncing). Unit tested. |
| `psionfiles.cpp` | Checking Psion's files against their SHA-256 before they are run; unzipping. Unit tested. |
| `download.cpp` | `psion -d`: downloading and installing Psion's files. |
| `ocr.cpp` | Reading text and pieces from the screen. |
| `memscan.cpp` | A tool that locates Psion's board in memory (see [Other modes and tools](../README.md#other-modes-and-tools)). |

The adapter never modifies Psion or its memory. It gets its data in two
ways:

- **The game** (board, side to move, ply) is read from Psion's memory.
  Moves are derived from changes of the board, not from the screen: memory
  is exact and changes even when the screen is not redrawn.
- **Everything else** is read from the screen: the level, the modes,
  prompts and the analysis display.

The rule: what must be exactly right to keep the game in sync comes from
memory; what is only checked, or shown to the user, may come from the
screen. The board on the screen proved unreliable for the game: the
cursor, highlighted squares and flashing kings cause misread squares,
and the screen shows neither the whole move list nor reliably whose
turn it is.

Psion is controlled only through simulated keypresses. The adapter checks
the effect of each key, in memory or on the screen, before it presses the
next one, and retries a few times if a key had no effect.

UCI is stateless: each `position` command gives the whole game. Psion,
on the other hand, keeps one game on its board, and the adapter is not
stateless internally: it compares the GUI's position with Psion's game
and changes the board incrementally whenever possible. Usually it only
enters the opponent's new move; otherwise it takes moves back (up to 100
plies) and enters the new ones. Only for another start position, a longer
takeback or `ucinewgame` does it start a new game (see [Controlling
Psion](#controlling-psion)). To the GUI this is invisible, apart from the
time it takes: a `go` without a new `position`, for example, first takes
back the move Psion has just played.

Psion keeps no node count, and its search depth is not found in memory
or on the screen. So `info` lines have no `nodes` or `nps`, the depth is
the length of the longest principal variation seen, and the time is
measured by the adapter itself: from when Psion started to think, so
without entering the opponent's move.

## Psion's memory

These offsets are relative to Psion's data segment. The adapter locates
it once, while Psion shows the start position: it tries DS first, and
otherwise scans memory for the start position. The segment is not
searched again later, because other board-like data in memory could be
taken for the game.

| Offset | Contents |
|---|---|
| `28AC` | The board: 64 bytes, a8..h8, a7..h7, ..., a1..h1. |
| `725B` | Side to move: `00` White, `FF` Black. Unreliable while the engine searches its own move. |
| `7260` | Ply counter (word). While pondering it includes the predicted reply. After set-up mode it starts at 20 (21 with Black to move). |

Square codes: empty `00`, pawn `2C`, knight `24`, bishop `1C`, rook `0A`,
queen `17`, king `01`; bit `80` is set for Black and bits `40`/`04` are
flags that do not change the piece. Promoted pieces get codes that are not
known in advance (seen: `10` and `20`); the adapter learns them from the
piece it typed at the promotion prompt or from the screen. Bit `04` (and
`40`) marks a king or rook that has moved. Psion's own record of that
(whether it may castle) is elsewhere, e.g. in its piece list (from `5BC0`
for White); a king or rook typed in set-up mode is marked as moved there,
while the board at `28AC` still shows it as unmoved.

The adapter reads these every 10 ms and derives each move from the change
of the board. It accepts a memory state once it has been unchanged for a
while: 20 ms if it differs from the last one by a single move (the usual
case, also for the position a takeback leads to), and 200 ms for any other
change, such as a new game, the position after set-up mode or a
resynchronization. The states Psion passes through while it plays a move,
such as a piece lifted, are not taken for positions. While Psion searches
its own move, its board does not change, but its side-to-move byte is
unreliable: the adapter counts the plies itself instead. If no legal move
explains a change, it resynchronizes to the state in memory. Reads that
fail are ignored: Psion flashes the kings when it announces a draw or
mate.

## The screen

DOSBox renders each screen update into a 640x400 frame, which
`ProcessScreen` hands to `ocr.cpp`. `ocr()` turns it into 80x25 text by
matching each 8x8 cell against Psion's font, and `ocr_board()` reads the
pieces off the graphical board. Each row keeps its last non-blank
contents, because some rows blink. The adapter uses:

| Row | Used for |
|---|---|
| 6 | `PLAYER PLAYER`: 2-player mode is on. |
| 20 | `SET-UP POSITION`: set-up mode; `DRAW`, `CHECKMATE` or `STALEMATE` instead of `YOUR MOVE`: Psion considers the game over (only as long as the board is the one shown with the text, which stays a little after a takeback). |
| 22 | The status line: level 11 (`LEVEL 11  4 Min`), easy mode (`LEVEL E`), `ANALYSIS ON`. |
| 24 | The analysis display (see below), and the `PROMOTE TO` and `ILLEGAL` prompts. |

The board read from the screen is used only to identify promoted pieces
whose codes are unknown (see above), and as the reference for `memscan`.

Reading the screen is the only way to get its text: Psion runs in CGA
mode 6 (640x200, two colours) and draws its text itself, as pixels,
without the BIOS. It is not a bottleneck. The OCR runs only when DOSBox
hands over a frame that has changed, and takes about 0.3 ms per frame.
DOSBox's drawing of the CGA screen into the frame is cheap too. During a
16-second search on an i5-1135G7, the drawing took 0.10 s and the OCR
0.016 s (56 frames), against 18.4 s of CPU time for the whole program:
about 0.6%. Nearly all the time goes to emulating the CPU.

## Controlling Psion

- Level: set to 11 (4 minutes per move) at start-up, so Psion moves
  when told to (`m`, below). Not *Infinite*: at that level Psion stops
  reading the keyboard once its search reaches the maximum depth, which
  happens almost at once in a locked pawn ending.
- `e` switches easy mode on: Psion does not think on the opponent's
  time. (Pondering, too, can hang in such positions.)
- `a` switches analysis mode on: while Psion thinks, the bottom row shows
  its score (in pawns, for the side to move) and principal variation, e.g.
  `-00.4 E2-E4 D7-D5 B1-C3 D5xE4`, with 99.9 for a mate. These become
  `info` lines. A row is used once it has not changed for 50 ms (Psion
  erases the characters that change before drawing new ones) and if its
  moves are legal on the board (castling rights are not checked).
  Promotions are shown without the piece: the first of queen, knight, rook
  and bishop with which the rest of the line is legal is assumed. If Psion
  then plays the promotion to another piece, a last `info` line repeats
  the line and its score with the piece played, if all of the line is
  still legal with it (later promotions are guessed again). Psion shows a
  line at the start of each iteration (for the previous best move) and for
  each better move. Stopped before it could show its line, it plays the
  new best move: then a last `info` line gives that move alone, with the
  last score shown as a lower bound, e.g. `info depth 7 time 2950 score cp
  -10 lowerbound pv f3e5`. So does a promotion with which the line shown
  does not work.
- `2` sets 2-player mode, in which moves for both sides can be entered.
- `c` (Compute) starts a search, `m` (Move now) ends it when the time
  allocated to the move is used up.
- `t` takes back one ply, `$` starts a new game. Several plies are taken
  back with one `t` each, pressed at once; the position they lead to is
  accepted as soon as it is in memory. Up to 100 plies are taken back
  this way (in well under a second); beyond that, from another start
  position, or after `ucinewgame`, a new game is started and the moves
  are entered again, which takes about 0.35 s a ply.
- F3 enters and leaves set-up mode. For a FEN, the adapter starts a new
  game and enters set-up mode. Without castling rights it empties the
  board (`e`); with castling rights it keeps the start position, empties
  the squares that change (`0`) and types a rook again to remove its
  right. It moves the cursor to each square and types the piece, with `c`
  to change the colour, and leaves set-up mode with the side to move as
  the colour last set up. The kings come last, so that the board in
  memory is not taken for a position before it is complete. A new game
  first, for the start position, and because entered with Black to move,
  Psion leaves set-up mode with an invalid side to move. Psion treats a
  king or rook typed in set-up mode as moved, which is why those with
  castling rights are left where they stand; castling rights whose king
  or rook is not on its square are ignored. For an en passant square, the
  position before the pawn's double step is set up, and the step is then
  played.
- Pondering is done by the adapter, not with Psion's own pondering (see
  easy mode above): after `go ponder` the move to ponder on is entered and
  Psion searches as for `go infinite`; after `ponderhit` the time for the
  move counts from then on, while Psion goes on with its search. After
  `stop` (the opponent played another move) the adapter takes back that
  move and Psion's reply.
- After announcing a draw or mate, Psion stops accepting moves. In that
  case the adapter replies to `go` commands with `bestmove 0000`, preceded
  by an appropriate diagnostic message.
- Moves are entered by moving the cursor; F3 F3 (into and out of set-up
  mode) resets it to a known square.
- While Psion waits for the GUI, the emulation is stopped until the next
  command arrives, so an idle engine uses no CPU (unless it ponders).
  This starts once Psion has been idle for a moment, so that it has
  finished drawing the screen. Psion's own clock stands still meanwhile,
  which does not matter: the adapter keeps the time.

## Time management

The basic time allocation per move is either given by the `movetime`
parameter of the `go` command or else by

    min(1/n * remaining_time + 3/4 * increment, 3/4 * remaining_time)

where `remaining_time` is given by the appropriate `wtime`/`btime`
parameter, `increment` by `winc`/`binc`, and `n` by the `movestogo`
parameter, if present. Otherwise `n` is 30 if the increment is non-zero
and 60 otherwise. (Without `movetime` or a clock, e.g. `go depth`, the
allocation is 5 seconds; see [Limitations](../README.md#limitations).)

There are several complications, however, that have to be dealt with:

- Many GUIs send `position` and `go` together, a few microseconds apart.
  So entering the opponent's move (about 40 ms) has barely begun when
  `go` arrives. This is even worse if a new position has to be set up
  via `position fen`. This "setup time", i.e. the time still needed to
  set up the position *after the `go` command is received*, is counted
  from `go` as well, so it comes out of the allocated time. For GUIs or
  match runners (such as fastchess) that send `isready` after the
  `position` command, this setup time is of course zero.
- But the adapter ensures that at most 200 ms of the setup time comes
  out of the allocated time. Setting up a new position from scratch,
  from a FEN or by replaying the moves (e.g. after `ucinewgame`), takes
  much longer (0.6 to 0.9 s for a FEN on an i5-1135G7) and would otherwise
  leave Psion hardly any time to think.
- The adapter makes sure that Psion thinks at least 50 ms, for otherwise
  Psion plays very weak moves.
- Once Psion is told to move, it takes some time to play the move and
  for the adapter to report it (about 30 ms on an i5-1135G7), and then
  the move still has to reach the opponent via the GUI or the network.
  This time is estimated by the `Move Overhead` option, and has to be
  subtracted from the allocated time as well.

The result is that after receiving `go`, Psion is told to move at

    setup_time + max(allocated_time - min(setup_time, 200 ms)
                     - move_overhead, 50 ms)

For a setup time of at most 200 ms, the usual case and the one assumed
from here on, this can also be written more compactly as

    max(allocated_time - move_overhead, setup_time + 50 ms)

The actual time a move takes is then

    max(allocated_time - move_overhead, setup_time + 50 ms) + real_overhead

where `real_overhead` is the time the move really takes from then on.
Assuming that `move_overhead` and `real_overhead` coincide, this
simplifies to

    max(allocated_time, setup_time + move_overhead + 50 ms)

With the default `move_overhead` of 30 ms and a setup time of 40 ms this
becomes

    max(allocated_time, 120 ms)

On the other hand, for a GUI that sends `isready` first, the setup time
is zero, so that we get

    max(allocated_time, 80 ms)

The following table summarizes this: the time a move takes by the
formulas, and in parentheses the median measured on an i5-1135G7.

| | With `isready` | Without |
|---|---|---|
| `go movetime 0` | 80 ms (81) | 120 ms (121) |
| `go movetime 100` | 100 ms (100) | 120 ms (120) |
| `go movetime 120` | 120 ms | 120 ms |
| `go movetime 150` | 150 ms | 150 ms (151) |

Note that Psion can also move earlier: from its book, or when it finds
the move at once (a mate).

The case of `ponderhit` is similar, with the time counted from
`ponderhit` instead of `go`, except that Psion has been thinking all
along: the setup time is zero and the 50 ms minimum plays no part
(unless `ponderhit` comes before the move pondered on has even been
entered).

## Quirks

How the engine handles what the UCI protocol leaves open or forbids:

- `go` without a new `position` searches the same position again, also
  after a search: the move Psion has just played is taken back first.
  Before any `position`, and after `ucinewgame`, that is the start
  position.
- `position`, `go` and `ucinewgame` that arrive during a search, or while
  a `go` is still setting up its position, wait until that search has
  sent its `bestmove`; so do the commands after them, to keep their order.
  Polyglot, for example, sends `position` and `go` right after `stop`
  after a ponder miss, without waiting for the `bestmove`.
- `stop` and `ponderhit` apply to the current search at once, also when
  other commands wait, unless a `go` waits: then they apply to the
  search of that `go`. `quit` always takes effect at once, and `isready`
  during a search is answered at once (unless commands wait before it).
- `stop` before the search has started (the position is still being set
  up): after `go ponder` the answer is `bestmove 0000` at once; otherwise
  Psion searches briefly once it can.
- `go` in a position Psion considers over (mate, stalemate, or a draw by
  repetition or the fifty-move rule) is answered at once with
  `bestmove 0000`, after `info string Game over (Psion shows DRAW)` (or
  `CHECKMATE`, `STALEMATE`). After `go infinite` or `go ponder` the
  answer comes with `stop` or `ponderhit`, as for any move found early.
- A position with an illegal move is reported at once with `info string
  Illegal move a2a4` (with `(checkmate)` or `(stalemate)` if the game
  was already over before it), one that cannot be set up with `info
  string Invalid FEN`, `info string Psion does not accept e1g1` or `info
  string Cannot set up the position`. Psion ends a game at a threefold
  repetition (or by the fifty-move rule) and then takes no more moves:
  moves after that are reported as soon as Psion shows the result, e.g.
  `info string Psion does not accept g1f3: game over (Psion shows
  DRAW)`. A `go` in such a position is answered with `bestmove 0000`.
