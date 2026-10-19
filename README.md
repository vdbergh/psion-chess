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
    make integration  # UCI tests against ./psion (about a minute)

### Prebuilt executable

Every push is built by GitHub Actions. The package
`psion-chess-linux-x86_64` (on the run's page under *Actions*, *Artifacts*)
contains a statically linked executable that runs on any x86-64 Linux.
Unpack it and add Psion's files to its `data/` directory.

## Running

Use `./psion` as the engine command in your GUI. Psion's files are read from
`data/` next to the executable; set `PSION_DIR` to use another directory.

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

Diagnostics go to stderr.

### Supported UCI commands

`uci`, `isready`, `ucinewgame`, `position startpos [moves ...]`, `go`
(`wtime`, `btime`, `winc`, `binc`, `movestogo`, `movetime`, `infinite`),
`stop` and `quit`.

### Limitations

- FEN positions other than the start position are not supported: the engine
  answers `info string FEN positions are not supported` and `bestmove 0000`.
  (Psion's set-up mode cannot express castling or en passant rights.)
- No `info` output (depth, score, principal variation).
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

- Level: set to *Infinite* at start-up, so Psion only moves when told to.
- `2` sets 2-player mode, in which moves for both sides can be entered.
- `c` (Compute) starts a search, `m` (Move now) ends it when the time
  allocated to the move is used up.
- `t` takes back one ply, `$` starts a new game.
- Moves are entered by moving the cursor; F3 F3 (into and out of set-up
  mode) resets it to a known square.

## Other modes and tools

Without the `uci` command the program reads moves like `e2e4` from stdin,
and Psion replies to them. Environment variables for experiments:

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
