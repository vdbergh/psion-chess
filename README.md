# Psion Chess as a UCI engine

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
  pass with it under Wine; on Windows itself, GitHub Actions only checks
  that it starts (Psion's files are not available there).

Unpack the package and add Psion's files to its `data/` directory.

To make a release, push a version tag, e.g.
`git tag v1.0 && git push origin v1.0`.

## Running

Use `./psion` (`psion.exe` on Windows) as the engine command in your GUI.
Psion's files are read from `data/` next to the executable; set `PSION_DIR`
to use another directory. Without them the engine reports where it looked
and exits.

    $ ./psion
    uci
    id name Psion Chess 1985
    id author Richard Lang
    uciok
    isready
    readyok
    position startpos moves e2e4
    go movetime 2000
    bestmove c7c5
    position startpos moves g2g4 a7a6 h2h4 h7h6
    go movetime 2000
    info time 108 score cp -40 pv g1f3 g8f6 d2d4 f6g4 e2e4
    info time 125 score cp 10 pv e2e4 d7d5 b1c3 d5e4 c3e4
    bestmove e2e4

Only UCI output is written by default. With `--verbose` (or `-v`), or
after the UCI command `debug on`, the engine also writes diagnostics to
stderr: the moves and board after each move, start-up messages, etc.

### Supported UCI commands

`uci`, `isready`, `ucinewgame`, `position startpos [moves ...]`, `go`
(`wtime`, `btime`, `winc`, `binc`, `movestogo`, `movetime`, `infinite`),
`stop`, `debug on|off` and `quit`.

### Limitations

- FEN positions other than the start position are not supported: the engine
  answers `info string FEN positions are not supported` and `bestmove 0000`.
  (Psion's set-up mode cannot express castling or en passant rights.)
- `info` lines have the time searched (since Psion started to think, so
  without entering the opponent's move), a score and a principal
  variation, but no depth or node count: Psion's analysis display does
  not show those. The score
  has a resolution of 10 centipawns. Book moves and moves found
  instantly come without `info`.
- A mate is shown by Psion as a score of 99.9; the number of moves to mate
  is derived from the length of the principal variation.
- `go depth`/`nodes`/`mate` are not supported; such searches take 5 seconds.
- Pondering is not offered; `go ponder` is treated as `go infinite`.
- Moves are entered through the keyboard, which takes about 0.7 seconds per
  move. This time is taken from the engine's own clock, so very fast time
  controls leave little time for thinking.
- After `ucinewgame`, or when the GUI's move list diverges by more than four
  plies, the game is replayed from the start (about 0.7 s per ply).
- Psion applies its own draw rules and may announce a draw (e.g. by
  repetition) that the GUI does not see. Psion then stops accepting moves,
  so if the GUI continues the game, it is replayed in a new game.

## How it works

`src/psion_mods/` holds everything that is not DOSBox:

| File | Contents |
|---|---|
| `psionmain.cpp` | `main()` and the headless video, mapper and joystick stubs. |
| `psion.cpp` | Starting Psion, keyboard input, reading the game state, the UCI controller. |
| `chess.cpp` | Logic that does not depend on the emulator (move derivation, UCI parsing, time allocation, syncing). Unit tested. |
| `ocr.cpp` | Reading text and pieces from the screen. |
| `memscan.cpp` | A tool that locates Psion's board in memory (see below). |

### Psion's memory

These offsets are relative to Psion's data segment, which the adapter
locates once at start-up by scanning memory for the start position:

| Offset | Contents |
|---|---|
| `28AC` | The board: 64 bytes, a8..h8, a7..h7, ..., a1..h1. |
| `725B` | Side to move: `00` White, `FF` Black. Unreliable while the engine searches its own move. |
| `7260` | Ply counter (word). While pondering it includes the predicted reply. |

Square codes: empty `00`, pawn `2C`, knight `24`, bishop `1C`, rook `0A`,
queen `17`, king `01`; bit `80` is set for Black and bits `40`/`04` are
flags that do not change the piece. Promoted pieces get codes that are not
known in advance (seen: `10` and `20`); the adapter learns them from the
piece it typed at the promotion prompt or from the screen.

The adapter accepts a memory state once it has been unchanged for 200 ms
and derives each move from the change of the board. Reads that fail are
ignored: Psion flashes the kings when it announces a draw or mate.

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
  that change before drawing new ones) and if its moves can be played on
  the board. Promotions are shown without the piece; a queen is assumed.
- `2` sets 2-player mode, in which moves for both sides can be entered.
- `c` (Compute) starts a search, `m` (Move now) ends it when the time
  allocated to the move is used up.
- `t` takes back one ply, `$` starts a new game.
- Moves are entered by moving the cursor; F3 F3 (into and out of set-up
  mode) resets it to a known square.

## Other modes and tools

Without the `uci` command the program reads moves like `e2e4` from stdin,
and Psion replies to them; diagnostics then go to stdout. Environment variables for experiments:

| Variable | Effect |
|---|---|
| `PSION_DIR` | Directory with Psion's files. |
| `PSION_TWOPLAYER` | Stay in 2-player mode: moves for both sides from stdin. |
| `PSION_MEMSCAN` | Locate the board in memory by correlating memory snapshots with the board on screen. |
| `PSION_MEMDUMP` | Directory to write memory snapshots to (with `PSION_MEMSCAN`). |

`tools/find_state.py` searches such snapshots for side-to-move flags and
counters; `tools/match.py` plays two engines against each other.

## Licence

The emulator is DOSBox (GPL, see `COPYING` and `AUTHORS`), reduced to
what Psion needs. Psion Chess itself is copyrighted by its authors; it is
not part of this repository and not covered by that licence.
