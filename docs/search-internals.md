# Psion's search internals

Notes from tracing and disassembling Psion Chess 2.13 while it searches,
with the goal of getting a node count. These are working notes, not
polished documentation; they are updated as more is found. Nothing here
changes Psion: it is only observed.

Addresses are `segment:offset`. Code is in segment `0259` (as loaded by
the adapter); data offsets are relative to Psion's data segment (DS, `0B2E`
when loaded by the adapter), as in [implementation.md](implementation.md#psions-memory).
Instruction names are from `objdump -m i8086 -M intel`.

## How this was found

With temporary code (kept in `git stash` on the branch `search-board`) in
`src/psion_mods/psion.cpp` and `psionmain.cpp`:

- `PSION_MEMSCAN=1`: the existing board finder (`memscan.cpp`) lists every
  area of memory that matches the game position at each move.
  `PSION_MEMDUMP=dir` also writes a dump of conventional memory per
  snapshot, from which the code segment was disassembled.
- `PSION_PROBE=1`: reads the candidate boards once per emulated
  millisecond while Psion thinks, and reports how often each changed.
- `PSION_WATCH=1`: gives the 4 KB page that holds the search board a page
  handler that passes writes through to RAM and records the code address
  (CS:IP) of every write to `DS:5B00..5BFF`, and for make and unmake also
  their return address (kept in `DS:5A4C`, see below).
- `PSION_CORE=normal`: runs the interpreter core instead of the
  recompiler, so that CS:IP is exact at every memory access.

Example: `(echo a2a4; echo h2h4) | PSION_CORE=normal PSION_PROBE=1
PSION_WATCH=1 ./psion --verbose`. The reply to 1.a4 comes from the
opening book; the reply to 2.h4 is a search of about 3 seconds.

## Boards

memscan finds four areas that hold the game position whenever Psion is
idle:

| Offset | Layout | During a search |
|---|---|---|
| `28AC` | 8x8 bytes, a8 first | unchanged (changes only while Psion plays its move) |
| `50A3` | 8x8 bytes, a8 first | unchanged |
| `5A00` | 8x8 bytes, a8 first | unchanged |
| `5B00` | 0x88, words, a8 first | changes all the time: **the search board** |

In a 3.3 s search the probe saw the 0x88 board changed in 3,291 of 3,293
samples, with 3,245 different contents; the others changed in 29 samples,
all at the end.

### The search board (`5B00..5BFF`)

- 8 ranks of 16 words (32 bytes), a8 first: a8 = `5B00`, a1 = `5BE0`.
  Square address = `5B00 + 32*(7-rank) + 2*file`.
- Files 8..15 of each rank (the off-board half of 0x88) hold `FFFF`.
- Empty square: `0000`.
- Low byte: the piece code, as at `28AC` (pawn `2C`, knight `24`, bishop
  `1C`, rook `0A`, queen `17`, king `01`, `+80` for Black). Make sets bit
  `04` when a piece moves (the "has moved" flag).
- High byte: the piece's index in the piece list: even values `00..1E`
  for White, `80..9E` for Black.

The note in implementation.md that Psion records castling rights "in its
piece list (from `5BC0` for White)" refers to this board: `5BC0` is its
second rank, and the flag is bit `04` of the square words.

### Other data used by make/unmake

| Offset | Contents |
|---|---|
| `5A42` | Incremental material/position score, updated by make from tables at `0744` |
| `5A4C` | Return address of make/unmake (they keep undo data on the stack); also used as a pointer by the routine at `20B2` |
| `5A72` | Side flag (`00`/`80`, toggled per ply) |
| `6740` | Piece list: per piece index, its square (low byte of the 0x88 address); with related tables at `6700` and `6720` |
| `675E`, `67DE` | King squares (White, Black) |
| `055C` | Ray table used to check whether a move leaves the king in check |
| `6FFE` + ply | A move per ply, tried early (probably the killer move) |
| `7076` | Current ply (times 2, used as a word index) |
| `5A92` + ply | Per-ply table of search routines, called with `call [di]` |

## Writers of the search board

All writes during a 3.2 s search (normal core), 700,187 writes from 11
addresses:

| Address | Count | Instruction | What it does |
|---|---|---|---|
| `1CCF` | 170,472 | `xchg [bx],cx`, cx = 0 | make: lifts the moving piece from the from-square |
| `1CE8` | 170,472 | `mov [bx],cx` | make: puts it on the to-square |
| `1FF4` | 170,465 | `xchg [di],ax`, ax = 0 | unmake: clears the to-square |
| `1FC0` | 187,466 | `mov [di],cx` | unmake: puts back pieces popped from the stack (the moving piece, plus the captured one on a capture) |
| `20BB` | 570 | `xchg [bx],bp`, bp = 0 | lifts a piece via the pointer at `5A4C` (attack test?) |
| `2100`, `2112` | 282, 288 | `mov [bx],bp` | puts it back |
| `1C8B` | 44 | `xchg [bx],cx` | en passant: removes the captured pawn |
| `3412` | 64 | `rep stosw` | clears the board at the start of a search |
| `34B6`, `34D9` | 32, 32 | `stosb`, `mov [di],bl` | sets up the pieces at the start of a search |

`1FC0 - 1CCF` (about 17,000) is the number of captures, 10% of the moves.

## Make (`1C5C`)

- Entry at `1C5C` (pre-checks before that can reject a move with
  `sub bp,bp; ret`). At `1C63` it pops its return address into `5A4C`
  and pushes the old score `5A42`; the undo data stays on the stack.
- The move is in DX (from-square in DH, to-square in DL, 0x88 offsets
  with flag bits in `11`). With a capture or en passant flag, the
  to-square's content and address are pushed first.
- Lifts the piece (`1CCF`), pushes it and its square, updates the piece
  list at `6740`, sets the moved bit (and handles promotion), places it
  (`1CE8`), and updates the score from the tables at `0744`.
- Then checks whether the own king is attacked (rays from `055C`, king
  squares `675E`/`67DE`). If so (`1E1B`): `sub bp,bp; jmp 1FBC`, i.e.
  it undoes the move through the unmake code and returns BP = 0. These
  illegal tries write the board like real makes.
- Compares the move with the one two plies earlier (`1DED`), for
  repetitions; returns BP = 2 normally, or `FF02`/`0002` from `1E7A`.
- Returns with `jmp [5A4C]`.

## Unmake (`1FB8`)

- Pops its return address into `5A4C`, then pops (square, piece) pairs:
  `1FC0` puts the piece back, the piece list entry is restored
  (`xchg [bx],di` at `1FC7`, which also gives the other square), and
  `1FF4` clears the to-square. On a capture a second pair restores the
  captured piece (again through `1FC0`).
- `1FBC` is the entry used by make to undo an illegal move.

## Callers of make

Return addresses (`5A4C`) at the make write `1CCF`, 3.2 s search:

| Return address | Makes | Context |
|---|---|---|
| `0DB6` | 63,865 | move loop (`sub si,4; mov dx,[si]`), child via `call [di]` (table `5A92`) |
| `1270` | 38,636 | move loop, child via `call 41B5` |
| `13C1` | 33,444 | tries the move from `6FFE` + ply (after a check at `1A51`) |
| `1511` | 21,881 | move loop, child via `call [di]` |
| `1388` | 12,041 | tries a specific move first (checked at `1B46`) |
| `1363` | 508 | tries a move from `206B` |
| `246A`, `2509` | 1 each | outside the search (start of the search?) |

Every regular call is followed by the search of the child, so make is
not used for move ordering or exchange evaluation. Illegal tries, undone
inside make, show up as unmake writes with a make return address: 411 of
170,377 makes (0.24%).

Unmake is called from `0D9C`, `0DA5`, `0DCA`, `1264`, `13E9`, `1487`,
`14CA`, `14E7`, `14FE`, `174E` (return addresses).

## Search depth

The word at `7076` (ply times 2) was read at every write of 0000 to the
search board, so each node counts twice, at the value the ply had then
(`git stash` "ply histogram experiment (PSION_PLY)", on `main`). Root
moves are made at value 2, so value 2k is ply k. Searches of 15–20 s,
all of which showed 8-ply PVs (the most Psion shows):

| Position | Highest value | Highest ply | Most writes at |
|---|---|---|---|
| 1.a4 h5 | 22 | 11 | 16 (ply 8) |
| Kiwipete-like middlegame (`r3k2r/p1ppqpb1/...`) | 26 | 13 | 14–18 (plies 7–9) |
| Locked pawn ending (`8/8/4k3/3p1p2/3P1P2/4K3/8/8 w`) | 30 | 15 | 24 (ply 12) |

The depth in our info lines is the PV length, so it says nothing about
how deep Psion searched; it searches past ply 8.

The same experiment also counts captures per ply: a write of a piece
over a piece is make placing a capturing piece (en passant is missed).
Searches of 15 s:

| Ply | 1.a4 g5 | Middlegame | Pawn ending |
|---|---|---|---|
| 5–8 | 4–8% | 27–63% | 0% |
| 9 | 27% | 48% | 1.0% |
| 10–11 | 13–29% | 27–33% | 0.7–1.9% |
| 12–13 | | 25–33% | 5–6% |
| 14–15 | | | 0.4–0.8% |

The deepest plies are mostly quiet moves, so they are not a capture
search (quiescence search). In the pawn ending, Psion's full-width
search simply goes deeper. Any exchange evaluation must therefore be
static: make is not used for it (see the callers above). The counts at
ply 0 are the board setup. The counts at ply 1 in the pawn ending, where
no capture is possible, are unexplained (12 of 134).

## Node count

Implemented in `src/psion_mods/nodes.cpp`: a page handler on the pages
that hold `5B00..5BFF` counts the writes of 0 there; nodes = half of
that. The adapter takes the difference from the start of each search
(when it presses `c`, as for the time), for the `nodes` and `nps` of the
`info` lines and for "Searched ..." in diagnostic mode.

- Nodes = makes (writes at `1CCF`), minus 0.24% illegal tries. About
  1,150 emulated cycles per make (170,472 makes in 195M cycles).
- In the recompiler CS:IP is not exact (the watcher sees 16-18 "writer
  addresses" there instead of 11), so a counter there counts by value:
  writes of `0000` to the search board are 2 per make (`1CCF`, `1FF4`),
  plus the rare `20BB` and `1C8B` writes and the 64 of the board set-up.

Check of zero writes / 2 against the exact count (`PSION_WATCH=1`, input
`a2a4`, `h2h4`, `a1a3`):

| Core | Search | Makes (`1CCF`) | Zero writes / 2 | Emulated cycles |
|---|---|---|---|---|
| normal | 1.a4 (book) | 42 | 74 | 0.3M |
| normal | 2.h4 | 170,342 | 170,679 (+0.20%) | 195.7M |
| dynamic | 2.h4 | - | 172,565 / 172,800 / 173,148 | 196M |
| dynamic | 3.Ra3 | - | 451,708 / 452,411 / 451,700 | 597M |

- In the normal core the zero-write count is 0.2% high (the board set-up
  and the `20BB` writes); with the 0.24% illegal tries in the makes, it is
  about 0.4% above the number of legal moves searched.
- The page handler works with the recompiler too, and its counts agree
  with the normal core (a search stops after a fixed number of emulated
  cycles, about 196M and 597M here, so runs are comparable; the small
  differences come from where the search was cut off).
- Cycles per node depend on the position: about 1,130 for the search
  after 2.h4, about 1,320 after 3.Ra3.
- The handler's cost was not measurable with the recompiler: the wall
  time of these searches varied by ±10% between runs with or without it.
- A book move gives a handful of zero writes (the board set-up), so a
  count below about 100 means no search.

Speed cost of the counter (recompiler, i5-1135G7, searches after
2.h4 and 3.Ra3, 794M emulated cycles together, 6 runs each, alternating):
164.4 Mcycles/s with the counter (one run left out, in which Psion played
another move and searched longer) and 164.1 Mcycles/s without; the runs
vary between 155 and 171. So no measurable cost: the handler is called
for about 1 in 400 emulated cycles. That is about 129,000 nodes per second
on that machine.
