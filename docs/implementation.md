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
| `memscan.cpp` | A tool that locates Psion's board in memory (see [Other modes and tools](#other-modes-and-tools)). |

The adapter never modifies Psion or its memory. It gets its data in two
ways:

- **The game** (board, side to move, ply) is read from Psion's memory.
  Moves are derived from changes of the board, not from the screen: memory
  is exact and changes even when the screen is not redrawn.
- **Psion's analysis** (score and principal variation) is read from its
  memory too (below).
- **Everything else** is read from the screen: the level, the modes and
  prompts.

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

Psion keeps no node count. The adapter counts the nodes itself, by
counting the writes to the board Psion searches on (`nodes.cpp`, see
[search-internals.md](search-internals.md#node-count)). The depth is the
nominal depth of the iteration that found the line, from Psion's
iteration counter at `5A67` when it copied the line (see
[search-internals.md](search-internals.md#iterative-deepening-the-iteration-counter-5a67)),
and the selective depth the deepest ply searched since `go`, from the
writes to Psion's ply counter at `7076`. The time (also for
`nps`) is measured by the adapter itself: from when Psion started to
think, so without entering the opponent's move. The last `info` line is
repeated before `bestmove` with the time and nodes of the whole search.
When Psion showed no line, as for a book move, the last line is `info
depth 1 time T nodes 1 pv` and the move.

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
flags that do not change the piece. A promoted Black piece has other
codes until it moves, and while Psion's promotion prompt is up the
promotion square cycles through `08`, `10`, `18` and `20` (`+80` for
Black); the last of these can stay there until the board is redrawn. For
such a square the adapter takes the piece from Psion's search board at
`5B00` (0x88, words, the code in the low byte), which holds the position
after the move once Psion has played it: promoted pieces are queen `57`,
rook `0E`, bishop `1C`, knight `64` for White and `D3`, `8A`, `98`, `E0`
for Black (see [search-internals.md](search-internals.md#make-1c5c)).
Bit `04` (and
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
such as a piece lifted, are not taken for positions. A single move is
accepted at once, though, when Psion has finished writing it: Psion
plays a move (its own or one typed) by first counting the ply at `7260`
and then copying its whole board to `28AC` twice, once with the piece
lifted and once with the move done. The node counter's write handler
sees whether such a copy is under way; a board that differs by one
move, read with the ply counter changed and no copy under way, is the
move done (see [search-internals.md](search-internals.md#playing-a-move)).
Castling, en passant and promotions still wait the 20 ms. While Psion searches
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
contents, because some rows blink. The adapter uses the text for:

| Row | Text | Used for |
|---|---|---|
| 6 | `PLAYER PLAYER` | 2-player mode is on (checked after each engine move, and before typing moves). |
| 20 | `SET-UP POSITION`, `WHITE`/`BLACK` | Set-up mode, and the colour being set up. |
| 20 | `DRAW`, `CHECKMATE`, `STALEMATE` (instead of `YOUR MOVE`) | Psion considers the game over (only as long as the board is the one shown with the text, which stays a little after a takeback). |
| 22 | `LEVEL 11  4 Min`, `LEVEL E` | Level 11 is set; easy mode (no pondering) is on. |
| 24 | `PROMOTE TO` | When to type the piece of a promotion. |
| 24 | `ILLEGAL MOVE`, `ILLEGAL POSITION` | A typed move was refused; the set-up failed. |

Row 20 also goes into the diagnostic message when Psion does not play a
move typed. Everything else (the game, the analysis, the depth, the
promoted pieces) comes from memory. The board read from the screen is
used only as the reference for `memscan`, and is read only then.

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
- Analysis mode (`a`) stays off. Each time the best root move changes,
  Psion copies the score and the first 8 moves of its principal variation
  to `DS:7150` for its analysis display, whether that is on or not (see
  [search-internals.md](search-internals.md), "The principal
  variation"). The node counter's write handler sees the copy completed
  and queues it, and each copy becomes an `info` line if its moves are
  legal on the board (castling rights are not checked). The score has 24
  units per pawn and is converted to centipawns (not rounded to tenths
  as on the display); 256 pawns or more is a mate. Its number of moves
  comes from the score too, which counts 2 per ply to the mate (see
  [search-internals.md](search-internals.md#mate-scores)), so also a
  mate beyond the 8 moves of the line is counted right. Moves are stored as from and to
  squares of the search board, without the piece promoted to: the first
  of queen, knight, rook and bishop with which the rest of the line is
  legal is assumed. If Psion then plays the promotion to another piece,
  a last `info` line repeats the line and its score with the piece
  played, if all of the line is still legal with it (later promotions
  are guessed again). The lines come at the start of each iteration (for
  the previous best move) and for each better move; the copy at the end
  of the search, a repeat, is not reported. Should Psion play a move
  other than that of the last line, a last `info` line gives that move
  alone, with the last score as a lower bound, e.g. `info depth 7
  seldepth 11 time 2950 score cp -10 lowerbound pv f3e5`. So does a promotion with which
  the line does not work.
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
and 60 otherwise. Without `movetime` or a clock there is no limit in
time if `go depth`, `go nodes` or `go mate` is given, and 5 seconds if
none is.

`go depth`, `go nodes` and `go mate` apply together with the time: the first limit
reached ends the search (not before `ponderhit`, nor with `go
infinite`), with MOVE-NOW as when the time is up, but without the 50 ms
minimum below: the GUI asked for these limits, and Psion is searching
once they are reached. For the depth the node counter's write handler watches
Psion's iteration counter at `5A67` (see
[search-internals.md](search-internals.md#iterative-deepening-the-iteration-counter-5a67)):
once Psion starts an iteration deeper than the depth asked, rounded up
to an odd depth of at least 3, the iteration that reached it is
complete, and Psion plays its best move. Psion stops by itself after
its iteration at depth 15. The nodes are compared with the node count
every tick (10 ms), so the search goes on a little longer: 20,145 nodes
for `go nodes 20000` and 200,179 for `go nodes 200000` on an i5-1135G7.
`go depth 2` after 1.a4 h5 stops after 10 ms with depth 3 (with the
50 ms minimum it went on into depth 5, and played the move that depth
preferred). `go mate n` is reached by a
line of this search that mates in at most n moves (with n < 0: that is
mated in at most -n moves; `go mate 0` counts as `go mate 1`). A depth or
node count of 0 or less counts as 1, so it is still a limit, not the
5 s fallback. Psion itself ends its
search when it has found a mate, also a longer one: `go mate 2` in a
position with a mate in 3 plays that mate (after 30 ms). Without a mate
the search goes on, like Stockfish's, until another limit, or until
Psion stops after its iteration at depth 15.

There are several complications, however, that have to be dealt with:

- Many GUIs send `position` and `go` together, a few microseconds apart.
  So entering the opponent's move (about 20 ms) has barely begun when
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
  for the adapter to report it (about 10 ms on an i5-1135G7), and then
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

With the default `move_overhead` of 10 ms and a setup time of 20 ms (to
enter the opponent's move) this becomes

    max(allocated_time, 80 ms)

On the other hand, for a GUI that sends `isready` first, the setup time
is zero, so that we get

    max(allocated_time, 60 ms)

The following table summarizes this: the time a move takes by the
formulas, and in parentheses the median measured on an i5-1135G7.

| | With `isready` | Without |
|---|---|---|
| `go movetime 0` | 60 ms (60) | 80 ms (80) |
| `go movetime 100` | 100 ms (100) | 100 ms (101) |
| `go movetime 120` | 120 ms (121) | 120 ms (119) |
| `go movetime 150` | 150 ms (152) | 150 ms (147) |

Note that Psion can also move earlier: from its book, or when it finds
the move at once (a mate).

The case of `ponderhit` is similar, with the time counted from
`ponderhit` instead of `go`, except that Psion has been thinking all
along: the setup time is zero and the 50 ms minimum plays no part
(unless `ponderhit` comes before the move pondered on has even been
entered).

## Quirks

How the engine handles what the UCI protocol leaves open or forbids:

- Values out of range in `go` are clamped rather than rejected. A
  negative clock (`wtime`, `btime`) or `movetime` counts as 0: the time
  used up, so Psion moves after its minimum of 50 ms. Negative
  increments count as 0, and a `movestogo` of 0 or less as not given.
  A `depth` or `nodes` of 0 or less counts as 1, so it is still a limit
  (`go depth -3` searches to Psion's smallest depth, 3). `go mate n`
  keeps its sign: a negative n means being mated in at most -n moves,
  and `go mate 0` counts as `go mate 1`. Values beyond ±2147483647 count
  as ±2147483647, and a missing or non-numeric value as 0 (for `depth`,
  `nodes` and `mate`, then, as the smallest limit).
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
  `CHECKMATE`, `STALEMATE`). A mate or stalemate is seen from the
  position itself, without waiting for Psion to show it (`info string
  Game over (checkmate)`, or `stalemate`); on a slow machine Psion may
  still be drawing the text when `go` comes. If Psion shows a draw only
  after the search was started, it ignores the `c`, and the result it
  shows answers the `go` within about 300 ms (`c` and `m` do not count
  as keys that change the board). After `go infinite` or `go ponder` the
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
  Psion's repetition test ignores castling and en passant rights, so it
  can also end a game the rules do not, e.g. after a rook moves away and
  back, losing the right to castle (see
  [search-internals.md](search-internals.md#repetitions-in-the-search)).

## Other modes and tools

Environment variables for experiments:

| Variable | Effect |
|---|---|
| `PSION_TWOPLAYER` | Stay in 2-player mode: moves for both sides from stdin. |
| `PSION_MEMSCAN` | Locate the board in memory by correlating memory snapshots with the board on screen. |
| `PSION_MEMDUMP` | Directory to write memory snapshots to (with `PSION_MEMSCAN`). |

`tools/find_state.py` searches such snapshots for side-to-move flags and
counters; `tools/match.py` plays two engines against each other;
`tools/lichess_check.py` checks the engine with python-chess, the way
lichess-bot uses it. `tools/lichess-bot-update.sh`, run daily by the workflow
`lichess-bot-update.yml`, opens an issue when upstream lichess-bot is
newer than the version the Lichess bot runs (`deploy/lichess-bot.txt`).
