# Psion Chess as a UCI engine

<p align="center">
  <img src="screenshot.png">
</p>

Psion Chess (Richard Lang, 1985) is a DOS program. This project runs it in
a stripped-down, headless DOSBox and adds an adapter that speaks the UCI
protocol, so that it can be used from any chess GUI or match runner.

The adapter does not modify the chess program. It reads the game from the
emulated machine's memory and plays moves by pressing keys, as a user would.

## Building

Requirements: Linux on x86-64, g++ and GNU make. No other libraries.

Psion Chess itself is not included: see [data/README.md](data/README.md)
for where to download it.

    make            # builds ./psion
    make check      # unit tests (a second)
    make integration  # UCI tests against ./psion (about two minutes)

### Windows

The Windows version is cross-compiled on Linux with MinGW-w64 (package
`g++-mingw-w64-i686` on Debian/Ubuntu). It is a 32-bit executable, which
also runs on 64-bit Windows: the 64-bit recompiler of this DOSBox version
only supports the Linux calling convention.

    make windows                       # builds ./psion.exe
    make check TARGET=windows          # unit tests, under Wine
    make integration TARGET=windows    # UCI tests, under Wine

The tests need Wine with 32-bit support (`wine32:i386`). If Wine was
installed without it at first, its existing prefix (`~/.wine`) cannot run
32-bit programs; use a new one, e.g. `export WINEPREFIX=~/.wine32`.

### Prebuilt executables

Released versions can be downloaded from the repository's *Releases*
page. In addition, every push is built by GitHub Actions: the packages are
on the run's page under *Actions*, *Artifacts* (for 90 days, and only when
logged in to GitHub).

- `psion-chess-linux-x86_64`: a statically linked executable that runs on
  any x86-64 Linux.
- `psion-chess-windows-x86`: `psion.exe` for Windows. The integration tests
  pass with it under Wine and, in GitHub Actions, on Windows Server 2022
  and 2025.

Unpack the package and add Psion's files to its `data/` directory.

## Running

Use `./psion` (`psion.exe` on Windows) as the engine command in your GUI.
Psion's files are read from `data/` next to the executable; set `PSION_DIR`
to use another directory. Without them the engine reports where it looked
and exits.

    $ ./psion
    uci
    id name Psion Chess 1985
    id author Richard Lang
    option name Ponder type check default false
    option name Move Overhead type spin default 30 min 0 max 5000
    option name Cycles type string default max
    option name UCI_LimitStrength type check default false
    option name UCI_Elo type spin default 1400 min 1150 max 1650
    uciok
    isready
    readyok
    position startpos moves e2e4
    go movetime 2000
    bestmove c7c5
    position startpos moves g2g4 a7a6 h2h4 h7h6
    go movetime 2000
    info depth 5 time 98 score cp -10 pv e2e4 d7d5 b1c3 g8f6 f2f3
    info depth 7 time 364 score cp -20 pv e2e4 d7d5 e4e5 d5d4 f1c4 b8c6 f2f4
    bestmove e2e4 ponder d7d5

Only UCI output is written by default. With `--verbose` (or `-v`), or
after the UCI command `debug on`, the engine also writes diagnostics to
stderr: the moves and board after each move, start-up messages, etc.

`--cycles N` runs the emulated PC at a fixed speed of N instructions per
millisecond instead of as fast as possible (see [Speed](#speed)), e.g.
`--cycles 300` for roughly an original IBM PC, from start-up on; the
`Cycles` option does the same from a GUI.

### Supported UCI commands

`uci`, `isready`, `ucinewgame`, `position startpos|fen ... [moves ...]`, `go`
(`wtime`, `btime`, `winc`, `binc`, `movestogo`, `movetime`, `infinite`,
`ponder`), `ponderhit`, `stop`, `setoption`, `debug on|off` and `quit`.
A `position` and `go` that arrive while a search is still running, e.g.
right after `stop` as Polyglot sends them after a ponder miss, are
carried out once that search has sent its `bestmove`.

Options:

- `Ponder`: for GUIs that only let an engine ponder if it offers it.
- `Move Overhead` (ms, default 30): the estimated time from telling
  Psion to move until the opponent has the move: Psion playing it and
  the adapter reporting it, plus sending it to the opponent via the GUI
  or the network. How this option is used is explained in more detail
  in the [Time allocation](#time-allocation) section below. The default
  of 30 ms covers the engine's own part, measured on an unloaded
  i5-1135G7. Increase `Move Overhead` if the engine loses on time, for
  example when playing fast games over a slow network.
- `Cycles` (default `max`): the speed of the emulated PC, `max` for as
  fast as the host allows or a fixed number of instructions per
  millisecond, e.g. `300` for roughly an original IBM PC (see
  [Speed](#speed)); at most 1,000,000. Values above what the host can do
  give its full speed. The speed applies only while Psion searches:
  setting up the position and entering moves run at full speed, so a
  move costs about as much time as at full speed (40 to 75 ms, against
  about 30 ms, on an i5-1135G7).
- `UCI_LimitStrength` (default false) and `UCI_Elo` (1150 to 1650,
  default 1400): play at a given strength on the CCRL Blitz scale, by
  running the emulated PC at a fixed speed: about 2,400 cycles for 1150,
  100,000 for 1614 and 134,000 for 1650 (86 Elo per doubling; see
  [Strength](#strength)). This overrides `Cycles`; switched off, the
  `Cycles` speed applies again. The scale was measured at 2'+1"; at other
  time controls the strength differs somewhat. A host too slow for the
  speed asked for plays weaker.

### Time allocation

The basic time allocation per move is either given by the `movetime`
parameter of the `go` command or else by

    min(1/n * remaining_time + 3/4 * increment, 3/4 * remaining_time)

where `remaining_time` is given by the appropriate `wtime`/`btime`
parameter, `increment` by `winc`/`binc`, and `n` by the `movestogo`
parameter, if present. Otherwise `n` is 30 if the increment is non-zero
and 60 otherwise. (Without `movetime` or a clock, e.g. `go depth`, the
allocation is 5 seconds; see [Limitations](#limitations).)

There are several complications, however, that have to be dealt with:

- Many GUIs send `position` and `go` together, a few microseconds apart.
  So entering the opponent's move (about 40 ms) has barely begun when
  `go` arrives. This is even worse if a new position has to be set up
  via `position fen`. This "setup time", i.e. the time still needed to
  set up the position *after the `go` command is received*, is counted
  from `go` as well, so it comes out of the allocated time. For GUIs or
  match runners (such as fastchess) that send `isready` after the
  `position` command, this setup time is of course zero.
- But at most 200 ms of the setup time comes out of the allocated time.
  Setting up a new position from scratch, from a FEN or by replaying the
  moves (e.g. after `ucinewgame`), takes much longer (0.6 to 0.9 s for a
  FEN on an i5-1135G7) and would otherwise leave Psion hardly any time
  to think. Such a move takes the rest longer than its allocation.
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

### Limitations

- FEN positions are set up with Psion's set-up mode (see below). Castling
  rights are kept: Psion treats a king or rook typed in set-up mode as
  moved, so those with rights are left where they stand in the start
  position. Castling rights whose king or rook is not on its square are
  ignored, and Chess960 rights are not supported. An en passant capture
  is possible: the position before the pawn's double step is set up, and
  the step is then played. The move counters of a FEN are ignored, so
  Psion's 50-move rule counts from the FEN (the GUI still applies the rule
  itself).
- `info` lines have the time searched (since Psion started to think, so
  without entering the opponent's move), a score and a principal
  variation, but no node count. Psion does not show the search depth
  either; the depth given is the length of the longest principal
  variation so far in the search. The score
  has a resolution of 10 centipawns. Book moves and moves found
  instantly come without `info`.
- A mate is shown by Psion as a score of 99.9; the number of moves to mate
  is derived from the length of the principal variation.
- `go depth`/`nodes`/`mate` are not supported; such searches take 5 seconds.
- Psion searches to a fixed maximum depth and moves as soon as it is
  reached, whatever the time left. In a locked pawn ending, with few moves
  per ply, this takes under a second, so Psion moves almost at once even at
  long time controls (in Fine's position 70 it moves after about 1 second
  with 10 seconds to think). The depth limit is part of Psion's program and
  is left as it is.
- Pondering is done by the adapter, not with Psion's own pondering (see
  easy mode below): after `go ponder` the move to ponder on is entered and
  Psion searches as for `go infinite`; after `ponderhit` the time for the
  move counts from then on, while Psion goes on with its search. After
  `stop` (the opponent played another move) the adapter takes back that
  move and Psion's reply, which costs about 0.05 seconds of the engine's
  clock. The move to ponder on (`bestmove ... ponder ...`) is
  the second move of Psion's principal variation, so there is none after
  a book move or a move found at once. While pondering, the engine uses
  a CPU core all the time.
- Entering the opponent's move and reporting the engine's take about
  70 ms per move on an i5-1135G7. This time is taken from the engine's
  own clock, so very fast time controls leave little time for thinking
  (see [Time allocation](#time-allocation)).
- After `ucinewgame`, or when the GUI's move list diverges by more than
  100 plies, the game is replayed from the start (about 0.35 s per ply).
- Psion applies its own draw rules and may announce a draw (e.g. by
  repetition) that the GUI does not see. Psion then stops accepting moves,
  so if the GUI continues the game, it is replayed in a new game. Asked
  for a move in a position it considers over (a draw by repetition or by
  the fifty-move rule, mate or stalemate), the engine answers
  `bestmove 0000` at once, after
  `info string Game over (Psion shows DRAW)` (or `CHECKMATE`,
  `STALEMATE`). A position that cannot be set up is reported the same
  way (`info string Invalid FEN`, `info string Cannot set up the
  position`). Asking for a move then is a GUI bug.

### Speed

The emulated PC runs as fast as the host allows (DOSBox's `cycles=max`).
On an Intel Core i5-1135G7 this is about 187,000 cycles per millisecond
during a search, i.e. roughly 190 million 8086 instructions per second;
an original IBM PC (8088, 4.77 MHz) managed about 0.3 million.

For comparison, Richard Lang's program won the 1987 World Microcomputer
Chess Championship on a 68020 at 12 MHz, about 2 million instructions
per second, so the engine runs some 60 to 120 times faster. This is only
a rough figure: the 1987 program was the 68000-family version, not this
8086 one, and a 68020 instruction (32-bit) does more work than an 8086
one (16-bit, segmented memory), so the real advantage is somewhat
smaller. At about 90 times the speed, a second here is worth a minute
and a half then: in a 3+2 blitz game (about 6 seconds a move) the engine
effectively thinks longer than at a tournament rate of 40 moves in 2
hours (3 minutes a move).

### Strength

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
  machine by Stockfish 10's `bench`; here that gives about 2.1 million
  nodes per second (four at a time) against an estimated 1.6 million on
  the 4770K, so the opponents had about 1.3 times their CCRL time, worth
  some 25 Elo, and their ratings are raised by that much. Psion at a
  fixed speed does not depend on the host, as long as the host keeps up.
  Rated with Ordo, with TSCP, Sargon and micro-Max as loose anchors
  (± 21):

  | Psion | Opponent | Score | Rating | Error |
  |---|---|---|---|---|
  | 2,500 cycles | Sargon | 43.5% | 1139 | ± 51 |
  | 16,000 cycles | TSCP and Sargon | 20.0% and 79.0% | 1409 | ± 40 |
  | 100,000 cycles | TSCP | 43.3% | 1597 | ± 50 |
  | 187,000 cycles set, about 120,000 reached\* | TSCP | 52.7% (295 games) | 1663 | ± 50 |
  | `max` (the micro-Max match above) | micro-Max | 29.2% | 1723 | ± 55 |

  A line through the first three gives Elo = 1614 + 86 × log2(cycles /
  100,000), which `UCI_Elo` uses; the other two fit it within their
  errors (`max` is about 190,000 to 230,000 cycles here).

  \* With four games at a time the i5-1135G7 could not keep up with
  187,000 cycles in long searches: the search depth reached corresponds
  to about 120,000. It does keep up with 100,000.
- Psion has no transposition table: during a search only about 4 KB of
  memory changes, in small areas such as move lists, and nothing that
  fills up as the search proceeds. Every search starts afresh, which
  costs it most in endgames. Compared with native engines it is also
  handicapped by the emulation: the emulated PC is much slower than a
  native program on the same machine.

## How it works

`src/psion_mods/` holds everything that is not DOSBox:

| File | Contents |
|---|---|
| `psionmain.cpp` | `main()` and the headless video, mapper and joystick stubs. |
| `psion.cpp` | Starting Psion, keyboard input, reading the game state, the UCI controller. |
| `chess.cpp` | Logic that does not depend on the emulator (move derivation, UCI parsing, time allocation, syncing). Unit tested. |
| `ocr.cpp` | Reading text and pieces from the screen. |
| `memscan.cpp` | A tool that locates Psion's board in memory (see below). |

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

Psion keeps no node count, and its search depth is not found in memory
or on the screen. So `info` lines have no `nodes` or `nps`, the depth is
the length of the longest principal variation seen, and the time is
measured by the adapter itself.

### Psion's memory

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

The adapter reads these every 10 ms. It accepts a memory state once it
has been unchanged for 200 ms (while Psion searches, memory changes much
more often) and derives each move from the change of the board. While
Psion searches its own move, the adapter counts the plies itself instead
of trusting the side-to-move byte. If no legal move explains a change,
it resynchronizes to the state in memory. Reads that fail are ignored:
Psion flashes the kings when it announces a draw or mate.

### The screen

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

### Controlling Psion

- Level: set to 11 (4 minutes per move) at start-up, so Psion moves
  when told to (`m`, below). Not *Infinite*: at that level Psion stops
  reading the keyboard once its search reaches the maximum depth, which
  happens almost at once in a locked pawn ending.
- `e` switches easy mode on: Psion does not think on the opponent's
  time. (Pondering, too, can hang in such positions.)
- `a` switches analysis mode on: while Psion thinks, the bottom row shows
  its score (in pawns, for the side to move) and principal variation,
  e.g. `-00.4 E2-E4 D7-D5 B1-C3 D5xE4`. These become `info` lines. A row
  is used once it has not changed for 50 ms (Psion erases the characters
  that change before drawing new ones) and if its moves are legal on the
  board (castling rights are not checked). Promotions are shown without
  the piece: the first of queen, knight, rook and bishop with which the
  rest of the line is legal is assumed. If Psion then plays the
  promotion to another piece, a last `info` line repeats the line and
  its score with the piece played, if all of the line is still legal
  with it (later promotions are guessed again). Psion shows a line at
  the start of each iteration (for the previous best move) and for each
  better move. Stopped before it could show its line, it plays the new
  best move: then a last `info` line gives that move alone, with the
  last score shown as a lower bound, e.g.
  `info depth 7 time 2950 score cp -10 lowerbound pv f3e5`. So does a
  promotion with which the line shown does not work.
- `2` sets 2-player mode, in which moves for both sides can be entered.
- `c` (Compute) starts a search, `m` (Move now) ends it when the time
  allocated to the move is used up.
- `t` takes back one ply, `$` starts a new game. Several plies are taken
  back with one `t` each, pressed at once; the position they lead to is
  accepted as soon as it is in memory. Up to 100 plies are taken back
  this way (in well under a second); beyond that, or from another start
  position, a new game is started and the moves are entered again, which
  takes about 0.35 s a ply.
- F3 enters and leaves set-up mode. For a FEN, the adapter starts a new
  game and enters set-up mode. Without castling rights it empties the
  board (`e`); with castling rights it keeps the start position, empties
  the squares that change (`0`) and types a rook again to remove its
  right. It moves the cursor to each square and types the piece, with `c`
  to change the colour, and leaves set-up mode with the side to move as
  the colour last set up. The kings come last, so that the board in
  memory is not taken for a position before it is complete. A new game
  first, for the start position, and because entered with Black to move,
  Psion leaves set-up mode with an invalid side to move.
- Moves are entered by moving the cursor; F3 F3 (into and out of set-up
  mode) resets it to a known square.
- While Psion waits for the GUI, the emulation is stopped until the next
  command arrives, so an idle engine uses no CPU (unless it ponders).
  This starts once Psion has been idle for a moment, so that it has
  finished drawing the screen. Psion's own clock stands still meanwhile,
  which does not matter: the adapter keeps the time.

## Other modes and tools

Without the `uci` command the program reads moves like `e2e4` from stdin,
and Psion replies to them; with `--verbose` the diagnostics (the moves and
the board after each move) then go to stdout. Environment variables for
experiments:

| Variable | Effect |
|---|---|
| `PSION_DIR` | Directory with Psion's files. |
| `PSION_TWOPLAYER` | Stay in 2-player mode: moves for both sides from stdin. |
| `PSION_MEMSCAN` | Locate the board in memory by correlating memory snapshots with the board on screen. |
| `PSION_MEMDUMP` | Directory to write memory snapshots to (with `PSION_MEMSCAN`). |

`tools/find_state.py` searches such snapshots for side-to-move flags and
counters; `tools/match.py` plays two engines against each other;
`tools/lichess_check.py` checks the engine with python-chess, the way
lichess-bot uses it. `tools/lichess-bot-update.sh`, run daily by the workflow
`lichess-bot-update.yml`, opens an issue when upstream lichess-bot is
newer than the version the Lichess bot runs (`deploy/lichess-bot.txt`).

## Licence

The emulator is DOSBox (GPL, see `COPYING` and `AUTHORS`), reduced to
what Psion needs. Psion Chess itself is copyrighted by its authors; it is
not part of this repository and not covered by that licence.

Richard Lang wrote the chess engine of Psion Chess and kept its
copyright; the graphics and input/output were written by Psion. In
October 2026 he gave permission for his part to be freely distributable.
Psion's part needs permission from its rights holder. Zebra Technologies,
Psion's successor, could not confirm in October 2026 whether it still
holds these rights, and the current owner of the PSION trademark does
not hold them. So Psion Chess is still not included here.

PSION is a trademark of its owner. This project is not affiliated with,
endorsed by or sponsored by the owner of the PSION trademark or by
Psion's successors; the name is used only to identify the original 1985
program it runs.
