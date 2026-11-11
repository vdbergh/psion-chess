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
| `5A72` | Side flag (bit `80`, toggled per ply) and evaluation mode: bit `40` turns on the passed-pawn code, bits `03` scale the mobility down (`61` in pawn endings, `00` in the opening) |
| `6740` | Piece list: per piece index, its square (low byte of the 0x88 address); with related tables at `6700` and `6720` |
| `675E`, `67DE` | King squares (White, Black) |
| `055C` | Ray table used to check whether a move leaves the king in check |
| `6FFE` + ply | A move per ply, tried early (probably the killer move) |
| `7076` | Current ply (times 2, used as a word index) |
| `5A92` + ply | Per-ply table of search routines, called with `call [di]` |
| `5A67` | Iteration counter: the root depth (see "Iterative deepening") |
| `5A66` | `5A67 & 1C`: moves are pre-sorted at plies up to `5A66` / 2 |
| `714C` | Extension counter, set per iteration from the table at `0408` |

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
- A promotion (`1D01`..`1D59`): the piece comes from bits `11` of the
  move's from byte (`00` queen, `10` bishop, `01` rook, `11` knight) and
  gets the code `57`, `1C`, `0E` or `64`, XOR `84` for Black (`D3`, `98`,
  `8A`, `E0`). The White codes and the Black rook are the usual ones with
  bit `40` or `04`; the other Black ones are not, until the piece moves
  and make sets bit `04` (`1CDC`, with bit `40` from the parity). Seen on
  the search board for typed promotions and Psion's own ones.
- The game board at `28AC` is a copy made for the display (by the
  `rep movsw` at `4BBB`; `2953` copies the search board to `5A00`, which
  is probably its source): while the promotion prompt is up, the display
  cycles the square through `08`, `10`, `18`, `20` (`+80` for Black), and
  after a typed promotion the last of these can stay there until the
  board is redrawn (seen: `90` for a Black queen).
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
static: make is not used for it (see the callers above).

The "captures" at plies 0 and 1 are not moves. They come from the board
setup at the start of a search: `34B6` writes a piece's code with
`stosb`, then `34D9` writes its index into the high byte, over a
non-zero word. In the pawn ending all 12 at ply 1 come from `34D9`: the
6 pieces, set up twice while the counter is 2. The 31 at ply 0 are the
32 pieces of the start position (the white king has index 00, so its
write is 0). The `3412` clears that come with these setups also add 32
"nodes" each at those plies.

The setup runs when Psion starts a game (at start-up and on a new
game), when a set-up position is finished, and at the start of each
search. The counter is 0 only at start-up: the first setup comes 3 ms
before `23E4` writes 2 to it, the value at the root. It is never set
back to 0. During the search it goes up by 2 (at `131C`) and back down
(at `1470` and `14D4`, among others). So every later setup is counted
at ply 1.

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

## Evaluation and static exchange evaluation (`2974`)

Found with a profile: a temporary tick handler records CS:IP once per
emulated millisecond while Psion thinks (`git stash` "CS:IP profiler
(PSION_PROF) and PSION_CORE", normal core). The code was disassembled
from a `PSION_MEMDUMP` dump (linear `02590`, offset 0 = `0259:0000`).
In a 60 s search of the Kiwipete-like middlegame (59,779 samples), the
samples were spread over these routines (counted from the nearest
`call` target):

| Routine | Share | What it is |
|---|---|---|
| `2E20` (`2E2E`..`2FFF` and on) | 43% | ray walks for the attack tables (below) |
| `2974` | 34% | evaluation: attack tables, exchanges, final score |
| `1C5C` make | 6% | |
| `1267` | 4% | |
| `312D` | 2% | |
| `1FB8` unmake | 2% | |

So about three quarters of the time goes into the evaluation. It is
called from the search routines in the per-ply table at `5A92` (`175A`,
`1778`, `1796`, `186D`, `1895`, `1633`, ...).

**Attack tables.** For every piece, the evaluation walks its moves and
adds the piece to an attack byte for each square it attacks (`add
[bx+0x180],dh` at `2E5D`). The tables lie at fixed offsets from the
square's search-board address: `+180`/`+190` and `+300`/`+480`, one pair
per side. The walk also counts mobility (`inc bp` per empty square). It
also adds pieces lined up behind an attacker of the same kind (`+181`,
x-rays and batteries), and a piece pinned against the king (`cmp
ah,[67DE]`).

The attack byte counts attackers per kind. This is read off the tables:
`6800 + byte` gives the value of the least valuable attacker, and `6900
+ byte` gives the byte with that attacker removed:

| Bits | Attackers |
|---|---|
| `C0` | pawns (count 1–3) |
| `03` | knights and bishops (count) |
| `30` | rooks (count) |
| `0C` | queens (count) |

The piece values (at piece-list entry + `20`, e.g. `6760` for White's
pawns) are pawn `0D` (13), knight and bishop `28` (40), rook `41` (65),
queen `74` (116) and king 0.

**Exchanges (`2B11`..`2C48`).** For each piece of each side (the piece
lists at `5A44` = `6740` and `5A46` = `67C0`), the evaluation reads the
attack and defence bytes on its square. If the piece is attacked, it
plays out the captures on that square. Each side always captures with
its least valuable attacker (`sub al,[bx]` with the `6800` table, next
set from `6900`, at `2BA6`..`2BDE`). The pieces behind them (`+181`) join
when the front line runs out. The two bounds kept in `ah` and `dl` let
either side stop capturing when that is better. This is a static
exchange evaluation: no moves are made on the board. The result is what
the attacker wins on that square. For each side the evaluation keeps the
two largest results (`702`/`703` and `704`/`705`) and the square of the
largest (`706`).

**Final score.** Read from the code (`2974`..`2CDD`) and checked by
running it (see "Check of the formula" below). Two sides matter:

- Q is the side that just moved: White if bit `80` of `5A72` is clear,
  Black if it is set.
- T is the side to move.

The score is computed in White's view first and returned in `bp` from
Q's view; the callers negate it (`neg bp` at `1799`, `1824`, `1870`).
Score units: the piece-square tables give a pawn about 16–28, a knight
or bishop about 66–81, a rook about 123–128 and a queen about 219–230.
Exchange values are about half of that (pawn 13), so twice an exchange
gain is its value in score units.

1. **Static part.** It is the sum of:
   - `5A42`: the piece-square sums, kept up to date by make (tables
     through `0744`: pawn `6200`, knight `6100`, bishop `6300`, rook
     `6400`, queen `6500`, king `6600`, Black's at `+10`, negative).
   - The pawn term: a signed byte cached in `5A60`, recomputed only when
     `5A61` is non-zero. It is recomputed in one of two ways, depending on
     bit `40` of `5A72`: the tables at `6A00`/`6B00` (`2AE7`), or the
     detailed code at `2AA6` with table `6D00`.
   - Slider mobility: +1 per empty square a White slider sees and −1 for
     Black. Only diagonal and vertical rays count; the horizontal rays of
     rooks and queens are walked but counted into a value that is thrown
     away (`xchg bp,ax` at `2E41`/`2F65`).
   - +2 for each line from a slider through an enemy piece to the enemy
     king, or (not for a queen) to the enemy queen: pins (`2EC1`).
   - Check: +2 if Q's move gives check (`2A56`), and +8 more if two
     pieces give check (`2CF6`). Here `709` gets bit `80`.
   - The mobility and pin terms so far are shifted right by `5A72 & 3`
     (`sar bp,cl` at `2A42`), e.g. halved in pawn endings (`5A72` =
     `61`). The check terms come after the shift.
   - Passed pawns (`2D13`..`2E1D`), only with bit `40` of `5A72`, from
     the passed-pawn file masks at `5A52`/`5A5D`. For each passed pawn
     whose promotion square is empty, there are terms of 6, 32 (`0x20`)
     and 72 (`0x48`) units, the 72 when only pawns are left. Which apply
     depends on whether the side has pieces, which side is to move, and
     which kings and pieces attack the promotion square (and `2E20` /
     `1B69` for the square in front of it). Not decoded in full. Unlike the
     pawn term, these are not cached.
2. **Exchange step costs.** For each attacked piece, its owner loses:
   - `[714A]` if no pawn defends it (`714A` is 0 in the dumps, and no
     instruction writes it directly);
   - 1 if it is defended only by pieces behind others (`2BA3`);
   - 1 for each further round of captures (`2BBA`, `2BD7`).
3. **Exchange gains.** The four largest gains, in exchange units:
   - `m1` and `m2`: the two largest gains T can make against Q's pieces
     (`702`/`703`). If the piece that just moved (on the to-square kept in
     `708`) is one of them, its gain counts 2 more (`2BF2`) and `709` gets
     bit `40`.
   - `t1` and `t2`: the two largest gains Q can make against T's pieces
     (`704`/`705`).
4. **The returned score** (`2C4A`..`2CCA`), with S the static part in
   Q's view:

   ```
   bp = S
   if m1 > 0:  bp -= 2*m1 + m2          # T takes the biggest, half of the next
   if t1 > 0 and not (m1 > 0 and moved piece en prise):
       bp += 2                          # Q threatens something
       if T in check and m1 == 0: bp += 1
       if t2 > 0:   bp += 3             # Q threatens two things
       if t2 >= 16: bp += 2             #   the second worth more than a pawn
   ```

   T is to move, so it is assumed to win the most valuable piece of Q's
   that is en prise, in full. The second counts half, as T can win only
   one of them at once. Q's own threats get only a small bonus (2–7
   units, at most about a third of a pawn), because T can answer them.

The routine also returns other values:

- `cx`: the score before step 4 (popped at `2CCB`). Callers use it for
  margins (`mov di,0xa; sub di,cx` at `18B9`).
- `di = bp + 2*t1` (computed after the `m` terms, before the bonus); the
  callers above do not use it.
- `dx`: the bound for this ply, `[704C + 7076 - 2]`.
- Flags written into the move: `[si+2]` gets the square of T's best
  capture, with `10` set if `m1 > 0`; `[si+3]` gets bits `30` when Q
  threatens something (bit `20` cleared if `5A70 & 10`).

**Check of the formula.** Psion was run on the normal core with a
temporary page handler on all of DS (`git stash` "evaluation check
(PSION_EVALCHECK)"). The handler reads the registers when the
evaluation writes into memory at known instructions:

- At `2CCD` (`mov [si+2],bl`), `bp` is the returned score and `cx` the
  score before step 4. Recomputing step 4 from `cx`, `702`..`705` and
  `709` gives `bp` in all of over 3 million evaluations, from searches
  of six positions (the three above, and three with passed pawns).
- At the first write of the exchange loop (`2B2B`), `bp` is the static
  part before the exchange costs. A literal C++ copy of the slider walks
  (`2E2E`..`30AD`), shifted by `5A72 & 3`, together with the check terms,
  `5A42` and the pawn term, gives it exactly in every evaluation where the
  passed-pawn code did not run. Where it did run, the differences are
  sums of 6, 32 and 72. (Differences of −8, and 64 = 72 − 8, are an error
  of the check itself: the passed-pawn code sets bit `10` of `709`, which
  the check takes as a double check, +10 instead of +2.)
- The exchange step costs (step 2) and the exchange gains (step 3) were
  not checked separately. They come in through `cx` and `702`..`705`.

## Search algorithm: alpha-beta with an aspiration window

Read from the code; not checked by running it.

**In the tree: plain alpha-beta (negamax).** There is one word per ply
in the table at `704C` (index: the ply counter `7076`). It holds the best
score found so far at that ply, from the view of the side to move there.
Scores are stored with a bias of `8000`, so they compare unsigned and
`neg` turns a score round.

- Going down a ply (`12D9`), the child's entry starts as the entry two
  plies up: the child's alpha is the grandparent's best. Its beta is
  implicit: minus the parent's best. This is the usual negamax window
  passing, written with a table instead of arguments. With
  `search(child, -beta, -alpha)`, the child's alpha is minus the
  parent's beta, which is the grandparent's alpha. The child's beta is
  minus the parent's alpha, which changes only between children, so it
  is fixed while the child is searched.
- Coming back up (`1493`..`14B0`): if the child's best did not get above
  its starting value, the child failed low. The parent's move is then at
  least beta, so the parent stops (`14CD`, through `15FF`, probably the
  killer update) and returns as well. Otherwise, if minus the child's
  best beats the parent's best, it becomes the parent's new best (`14B0`,
  then `15CE`).

Every move gets this same window (alpha, beta). Nothing searches a move
with a null window and then searches it again, so this is not PVS or
NegaScout.

**At the root: an aspiration window** (`0FD8`, called from the iteration
loop at `107E`..`10C1`). In every iteration (`5A67 & 3` non-zero: the
counter is `05`, `09`, ... or `03`, `07`, ..., see "Iterative
deepening"), the window is the previous root score v (`704E`) ± 20 units,
0.83 pawn (24 units per pawn): alpha = v − 20 (also kept in `713A`),
beta = v + 20 (`704C` = −(v + 20)). For the first iteration v comes from
the pass over the root moves before it (counter `00`), which is the only
one with `5A67 & 3` = 0: there `704E` is set to `F000` and `704C` to 0
(beta = +∞).

- Fail low (the root best is still alpha, `10AA`, once all root moves
  have been searched against the window): `1125` sets alpha to −∞ (0)
  and searches all of them again (`jmp 1089`), from the first. A first
  move below the window does not stop the pass: the other moves are
  searched against the same alpha, and only if none of them reaches it
  is it a fail low. Until the first move of the new search is done, the
  move Psion would play is still the previous best, the one that failed
  low (see "The move played").
- Fail high (a cutoff at the root brings the ply back to 0, `14D2` →
  `1466` → `1147`): `114C` sets beta to +∞ and searches again from the
  old alpha.

**How often.** Writes of 0 to `713A` (fail low) and to `704C` (fail high)
counted per search, with the iteration counter (experiment not kept).
A first count, in one self-play game from the start position (80 plies
at each movetime), found almost none, but that was too small a sample
(see the probe matches below). Psion against Fairy-Max 5.0b (H.G. Muller's micro-Max derivative,
through a small UCI front end), 40 games at 10+0.1 from the UHO openings
(19.5 to 20.5):

| Games | Searches | Root fail lows | ... in the last iteration | Root fail highs |
|---|---|---|---|---|
| Self-play, movetime 300 ms | 80 | 1 (counter `05`, depth 3) | 0 | 0 |
| Self-play, movetime 1000 ms | 80 | 1 (counter `09`, depth 5) | 0 | 0 |
| Self-play, movetime 3000 ms | 80 | 1 (counter `05`, depth 3) | 0 | 0 |
| Against Fairy-Max, 10+0.1 | 2,548 | 386 (15%) | 88 (3.5%) | 210 (8%) |

Against Fairy-Max the fail lows came at depth 3 (231), 5 (117), 7 (34)
and 9 (4), the fail highs at depth 3 (101), 5 (84) and 7 (25). In self-play
the scores of successive iterations mostly differ by 1 to 9 units, far
inside the window. A fail low in the last iteration, where MOVE-NOW can
stop the second search, came in about one search in 29 against
Fairy-Max; how often MOVE-NOW actually cut that search short, and whether
finishing it would change the move, these counts do not show.

Probe matches at 10+0.1 (`TM Soft Stop` 40, no fail-low extension; a
fail low is counted when it follows a line of an earlier iteration;
"open at S" means no line had come at the target):

| Games | Searches | Fail lows | ... in the last iteration | ... open at S |
|---|---|---|---|---|
| Self-play, 100 games, 8moves_v3 | 13,876 | 895 (6.5%) | 277 (2.0%) | 124 (0.9%) |
| Self-play, 100 games, UHO_XXL_+1.00_+1.29 | 12,648 | 847 (6.7%) | 277 (2.2%) | 101 (0.8%) |
| Self-play, 100 games, 8moves_v3, 10+0.1 against 40+0.4: side at 10+0.1 | 6,451 | 820 (12.7%) | 262 (4.1%) | 95 (1.5%) |
| ... side at 40+0.4 (scored 76.5%) | 6,477 | 311 (4.8%) | 72 (1.1%) | 23 (0.4%) |
| Against Fairy-Max, 400 games, 8moves_v3 (Psion scored 32%) | 24,430 | 2,621 (10.7%) | 829 (3.4%) | 340 (1.4%) |

Fail lows come mostly to the weaker side, which finds out late what the
stronger one has seen: twice as often as in self-play, against Fairy-Max
or against Psion with four times the time alike. The unbalanced UHO
openings change nothing. Most fail lows come early in the search
(median 22 to 49 ms after `go`) and show the re-search's first line
within a few tens of ms; when that line came before S and the search
went on, the move changed in about a quarter of the cases.

**Cost of the search again.** In the same probe matches, for the fail
lows whose first pass (from the start of the iteration to the fail low)
took at least 20 ms and whose search again showed a line, the time to
that line against the first pass:

| Games | Fail lows | Median | 75th percentile | 90th percentile |
|---|---|---|---|---|
| Against Fairy-Max | 1,019 | 0.72 | 1.47 | 3.13 |
| Self-play, 8moves_v3 | 242 | 0.51 | 0.98 | 1.84 |
| Self-play, side at 10+0.1 against 40+0.4 | 221 | 0.63 | 1.16 | 1.81 |

The line usually comes sooner than the first pass took, although
nothing is kept from the first pass but killers (if `15FF` is the killer
update) and the window is wide. That is because the line needs only the
old best, searched exactly, while the first pass searched all root
moves. Per move:

| Games | Fail lows | Root moves in the first pass (median) | Old best's share of the first pass (median) | Old best searched again / its search in the first pass |
|---|---|---|---|---|
| Against Fairy-Max | 709 | 18 | 27% | median 2.9 (quartiles 1.6 to 6.3) |
| Self-play, 8moves_v3 | 161 | 12 | 26% | median 2.6 (quartiles 1.4 to 3.7) |

(Old best's search at least 5 ms, first pass at least 20 ms.) Searched
exactly, the old best costs about 2.6 to 2.9 times its search in the
first pass, where it only had to be shown below alpha (one good reply at
each node of the opponent). As it took about a quarter of the first
pass, the line comes after about 0.7 times the first pass. These counts
include only the fail lows whose search again showed a line; those cut
off before it would raise the ratios somewhat.

**In what positions.** Psion's score (pawns, from its side) before the
fail low and at the first line of the search again, in the same probe
matches, for the fail lows whose search again showed a line:

| Games | Fail lows | Before (median) | At the line (median) | Before ≤ −3 | At the line ≤ −3 | Level before (within ±1), ≤ −2 at the line | At the line ≥ −1 |
|---|---|---|---|---|---|---|---|
| Against Fairy-Max, all | 2,615 | −4.3 | −7.8 | 57% | 74% | 6% | 14% |
| ... in the last iteration | 823 | −7.3 | −12.9 | 69% | 83% | 5% | 8% |
| ... open at S | 335 | −7.0 | −9.8 | 71% | 82% | 3% | 9% |
| Self-play, 8moves_v3, all | 772 | −4.6 | −8.6 | 59% | 76% | 8% | 11% |
| ... in the last iteration | 154 | −8.7 | a mate | 69% | 83% | 14% | 6% |
| Self-play, side at 10+0.1 against 40+0.4, all | 726 | −5.6 | −9.5 | 67% | 82% | 4% | 6% |
| ... in the last iteration | 168 | −10.2 | a mate | 80% | 89% | 2% | 2% |

Most fail lows come when Psion already sees itself lost: in the last
iteration, 70 to 80% of them come at 3 pawns down or worse, and the
search again only shows the loss to be larger, often a mate. A level
position that turns bad, where a better choice could still matter, makes
only about 3 to 14% of the fail lows, which are themselves in under 1%
of the searches at S. This is why the extensions after a fail low
showed no gain in the matches (see "Results" in time-management.md).

The fail lows that are seen are biased towards lost positions, because
they are the quick ones: when the opponent has a threat that refutes
every root move, the first pass (from the start of the iteration to the
fail low) is over in a few ms. By the length of the first pass, with
Psion's score before the fail low (pawns, from its side; "level" is
within ±1):

| First pass | Against Fairy-Max: fail lows | median | level | ≤ −3 | Self-play, 8moves_v3: fail lows | median | level | ≤ −3 |
|---|---|---|---|---|---|---|---|---|
| under 10 ms | 1,133 | −6.3 | 8% | 70% | 413 | −7.5 | 8% | 71% |
| 10 to 30 ms | 713 | −4.1 | 15% | 57% | 202 | −5.0 | 15% | 58% |
| 30 to 60 ms | 376 | −2.4 | 20% | 46% | 132 | −3.4 | 21% | 53% |
| 60 to 120 ms | 299 | −1.4 | 27% | 36% | 107 | −2.0 | 21% | 47% |
| over 120 ms | 100 | +0.2 | 43% | 18% | 41 | −0.3 | 46% | 31% |

(The side at 10+0.1 against 40+0.4 shows the same, less sharply: level
from 10% to 32%.) The slow first passes, in more level positions, are
the ones MOVE-NOW most often cuts off before all root moves are
searched: they end as a first move below the window with no fail low
yet, or are not seen at all. Searching on until a root move is inside
the window (`TM Min Moves Window`, see time-management.md) lets more of
them finish.

## Move ordering

Read from the code; not checked by running it. At each node the moves
come in stages, kept in the stage byte `[si+3]` (1, 2, 3) of the move
slot (`1337`..`1413`):

1. In some cases (bit `80` of `dh` at `133B`) a move from `206B` is
   tried first (`1363`). Not decoded.
2. **Capture of the piece that just moved** (`136C`..`1388`). The square
   comes from `5A70`. `1B46` scans the piece list of the side to move
   from the start: pawns, then knights and bishops, rooks, queen and
   king. It takes the first piece that attacks the square, i.e. the least
   valuable one.
3. **Killer move** (`13AC`..`13C1`): one per ply, at `6FFE` + ply, checked
   for legality by `1A51` and tried if legal.
4. **All other moves.** `632` calls `30B4`, which generates the moves
   piece by piece (handler table at `6720`/`67A0`: `3213` pawns, `31C8`
   knights, `3153`, `314B`, `3146`, `3325`/`3320` king). Each ply has
   its own move list, with its top at the word `7078` + ply·2 (e.g.
   `9074` for the root, `9000` for ply 2, in a dump taken at ply 4). The
   generator points `sp` at that address (`30B7`..`30BE`, the old `sp`
   kept in `6FE`; `ss` = `ds`) and pushes the moves (`push cx; push ax`),
   so a list runs down from its top, 4 bytes per move (the move, then a
   word of flags), and ends at a 0 move. The lists lie below the call
   stack, which starts at `947C` (`2420`). It leaves out the moves already tried in stages 2 and 3 (`cmp ax,bp`,
   `cmp ax,[6FC]`).
   - Near the root (ply ≤ `5A66` / 2; `5A66` was 8, so plies 1–4; it changes per
     iteration, see "Iterative deepening") they
     are sorted first (`0D92`..`0E60`). Every move is made, evaluated
     statically (`5A94` = `1633`, which calls `2974`) and unmade. A
     capture or a move flagged `80` (probably check) gets +8. The 10
     best go into an insertion-sorted list at `03E0`..`0407`, and are
     then put first in that order. The rest follow in generation order.
   - Deeper (`14FE`), the moves are tried in generation order. When bit
     `04` of `5A71` is set, only captures, promotions and checks are
     searched (see "Selective filter" below).

**Killer update** (`15FF`, on a beta cutoff): if the move came from
stage 4 (`[si+3] & 3` = 0), it becomes the killer for this ply (`6FFE` +
ply). Up to ply 8 (counter below `11`) it is also written to the
first word of this ply's row in the PV table at `7090` (see "The
principal variation").

The root moves are in the root's move list (top at `[7078]`), and also
in a table at `6E00` (12 bytes per move, filled at `1068`). When a
root move becomes the new best, `11A6` moves it to the front of both and
shifts the moves before it down one place (`11B6`..`11CC` in the list,
`11D6`..`11E1` in `6E00`), so the next iteration starts with the best
move so far. There is no history table,
and no hash move, as no hash table has turned up.

## Selective filter at the deep plies (`155F`)

`5A6E`..`5A71` hold the move that led to the current node: `12F4`..`1304`
copy its slot there when going down a ply. `5A6E` is the move and
`5A70`/`5A71` are its bytes `[si+2]`/`[si+3]`. So bit `04` of `5A71` is a
flag on the parent's move.

When that flag is set, the loop at `14FE` (stage 4 beyond ply `5A66`/2)
searches a move only if one of these holds:

- it is a capture (`dl & 1`);
- it is flagged `20` in `[si+3]`, which the generator sets for pawn moves
  to the last rank, i.e. promotions;
- it gives a direct check (`156A`..`15C9`). The vector from the
  to-square to the enemy king indexes the ray table at `055D`. That byte
  gives the kinds of piece that attack along the vector, and the byte
  before it the step. For a slider of a matching kind, the squares
  between must be empty (`15B5`..`15C9`). A knight matches only on a
  knight vector (step 0). A pawn matches if the step points the right way
  for its colour and the vector is a pawn capture (`159C`..`15A7`). The
  king never matches. Discovered checks are not looked for.

All other moves are skipped (`jmp 14FE`). Stages 2 and 3 (capture of the
moved piece, killer) come before this loop and are not filtered. So a
node under the flag does a capture search with promotions and checks
added, much like a quiescence search with checks.

**Where the flag is set.** It is set on a move M by the per-ply routines
in the table at `5A92`, which run after M is made: `or [si+3],4` at
`183F`, `1889`, `18C2` and `18F4`. Which routine runs depends on the
ply (the table is rebuilt per iteration). The conditions combine the
static score of the position after M with margins of a few units
against the bound (e.g. `10 - cx` against `dx` at `18B9`) and the flags of
the parent's move in `5A71`:

- `80`: it gave check;
- `40`: the piece it moved is en prise;
- `20`/`10`: it created a threat;
- `08`: from the passed-pawn code.

Roughly, M gets the flag when the position is not quiet and the score is
close to the window. The exact conditions per routine are not decoded.

**How often.** Makes counted in searches of 8 s (normal core; experiment
not kept), by the return address of make:

| Position | All makes | From the deep loop (`1511`) | With the filter on | Captures / promotions / checks |
|---|---|---|---|---|
| 1.a4 g5 | 426,325 | 44,994 | 2,856 (0.7%) | 2,408 / 0 / 448 |
| Kiwipete-like middlegame | 278,089 | 207,397 | 35,959 (13%) | 33,200 / 89 / 2,670 |
| Locked pawn ending | 814,463 | 153,973 | 0 | |

**Not inherited.** No routine passes bit `04` on to the child's moves.
The routines test the parent's flags with masks that leave out bit `04`
(`F8`, `88`, `40`, `A8`, `C0`, `B8`), so every move must earn the flag
again. Counted over the same 8 s searches (experiment not kept), as
nodes that made at least one move:

| Position | Nodes | Under the filter | Parent also under the filter |
|---|---|---|---|
| 1.a4 g5 | 118,403 | 2,666 | 0 |
| Kiwipete-like middlegame | 42,400 | 10,557 | 216 (2%): 176 runs of 2 plies, 35 of 3, 5 of 4 |

So the filter nearly always covers one ply: the node after a flagged
move searches only its captures, promotions and checks, and the nodes
below those are almost never filtered again. A filtered node
that made no move at all is not in these counts.

**Which routines set the flag.** Counted per ply over the same searches
(the routine is `[5A92 + ply]` at the time of the make; the table changes
between iterations, so a ply can have several):

| Ply | 1.a4 g5: routine, flags / makes | Middlegame: routine, flags / makes |
|---|---|---|
| 2 | `1895`: 2 / 68 | `1895`: 89 / 340 |
| 3 | `1778`, `18FC`, `1919`: 0 | `1778`: 5 / 1,059; `18FC`, `1919`: 0 |
| 4 | `1895`: 43 / 2,313 | `1895`: 1,578 / 3,834; `186D`: 43 / 172 |
| 5 | `1778`, `18FC`, `1919`: 0 | `1778`: 189 / 10,157; `175A`: 8 / 1,114; `18FC`: 0 |
| 6 | `1895`: 2,628 / 32,444 | `186D`: 1,097 / 9,370; `1895`: 495 / 21,889 |
| 7 | `1778`, `18FC`: 0 | `1778`: 1,093 / 22,373; `175A`: 197 / 23,907; `1796`: 0 |
| 8 | `1895`: 3,162 / 161,326 | `186D`: 3,157 / 60,185 |
| 9 | `1778`: 0 | `175A`: 1,146 / 29,174; `1796`: 0 |
| 10 | | `186D`: 1,426 / 17,626 |

`1919`, `18FC`, `1796` and `1633` (the pre-sort) never set it. `1895`
and `186D` set it at the even plies. `175A` and `1778` set it only through
`183F`, the code of `1821`. They reach it only when the counter at
`714C`, lowered by 1 or 2 for the move's own flags, is negative: for
`175A` after adding 4 (`175D`..`1775`), for `1778` as it is
(`177B`..`1793`). `714C` goes down by 1 or 2 for each move along the path
that is flagged `80` or `20` in `dh` (`1306`..`1317`); it is saved and
restored per ply, and its start comes from a table (`0F62`). Values seen
at makes were 0 to 15 in the quiet opening and mostly −9 to 5 in the
middlegame.

So in the quiet opening only even plies set the flag, and a filtered node
(odd ply) never has a filtered parent. In the middlegame the odd plies
set it as well, once `714C` has run low, but still only 2% of the
filtered nodes have a filtered parent. Why, is not decoded: it depends on
the conditions in `1821`..`1869`.

This is the capture search that we did not see in the capture share per
ply. It runs only after tactical moves and makes up a small part of the
tree. The evaluation's static exchange evaluation still stands in for it
at all other leaves.

## Iterative deepening: the iteration counter (`5A67`)

The root depth is the byte at `5A67`. `0EB1` sets it to 0 before the
search. Before each iteration the loop at `107E` calls `0F46`, which
sets the next value, and `195B`, which rebuilds the per-ply routine
table (`5A94` for ply 1 on) from it. Its bits `03` are a variant d (1
or 3), its bits `1C` a number A, which also goes to `5A66`.

`195B` copies 19 routines from the table at `0416` + 16·d, starting A − 8
bytes before that base; entries before the base become `1633`. With
h = A/2 + 1 for d = 1 and h = A/2 + 3 for d = 3, the plies get:

| Plies | d = 1 | d = 3 |
|---|---|---|
| 1 .. h − 5 | `1633` | `1633` |
| h − 4 .. h − 1 | `1919 1919 18FC 1895` | `1934 18DA 1821 186D` |
| h | `1778` | `1778` |
| h + 1 .. h + 4 | `186D 175A 186D 1796` | `186D 175A 186D 1796` |

(for small h the first entries are left out). `1778` at ply h evaluates
the position and stops there (the leaf test at `1799`), unless the
extension counter `714C` has gone negative (see "Which routines set the
flag"). So h is the nominal depth. The plies beyond h are reached only
through that extension, and `1796` at ply h + 4 always stops.

As a formula, h = 2·⌊(`5A67` + 1)/4⌋ + 1, so the nominal depth is always
odd:

| `5A67` | 03 | 05 | 07 | 09 | 0B | 0D | 0F | 11 | 13 | 15 | 17 | 19 | 1B |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| h (plies) | 3 | 3 | 5 | 5 | 7 | 7 | 9 | 9 | 11 | 11 | 13 | 13 | 15 |
| `714C` at start | 09 | 05 | 07 | 07 | 0B | 0B | 0F | 0F | 11 | 11 | 15 | 15 | 19 |

The two variants with the same h differ in the four plies before the
horizon and in the pre-sort depth (`5A66` / 2 is h − 1 for d = 1 and
h − 3 for d = 3). How the routines of d = 3 differ is not decoded.

**The steps** (`0F72`..`0FD6`). The first iteration is `05`, or `03`
when the byte at `725A` (`0B` in our runs) is 0, 1 or `0D`. After that
`5A67` normally goes up by 4: the same variant, two plies deeper. With
t = `5A7C` + `713C` (probably the time used) and T = `5A78` (probably the
time allotted, set at `0F00`; `F2` and `114` in the runs below):

- t < 3T/32: + 4 (but + 2 from `09` once t ≥ 3T/64);
- 3T/32 ≤ t < T/2: + 2;
- t ≥ T/2: stop if bit `80` of `5A6B` is set, otherwise + 2 (and
  `5A6C` = 2).

A step of 2 goes from d = 1 to d = 3 two plies deeper (`09` → `0B`), or
from d = 3 to d = 1 at the same depth (`0B` → `0D`). The value is capped at
`1B`, and the loop stops after the iteration at `1B` (`10BC`) unless
`725A` is `0D`. So Psion never searches more than 15 plies nominally.
With the extensions, `seldepth` could reach 19 (h + 4, see below).

In a locked pawn ending such as Fine 70 (`8/k7/3p4/p2P1p2/P2P1P2/8/8/K7
w`) Psion therefore runs through all its iterations at once and moves,
whatever the time left: with `go movetime 60000` it showed depth 15
after 0.7 s and played `a1b2` after 0.8 s. There are hardly any captures
or checks to extend on, so `seldepth` stays at the depth (15/15). The
winning `Kb1` needs a
far deeper search (usually given as about 25 plies or more), which Psion
cannot do; raising the limit would mean patching its search, which we do
not do.

**Measured** (searches of 15 s, normal core; experiment not kept), with
makes counted per value of `5A67` and the deepest ply made:

| `5A67` | h | 1.a4 h5 | Middlegame | Pawn ending |
|---|---|---|---|---|
| 00 | | 188, ply 1 | 217, ply 1 | 126, ply 1 |
| 05 | 3 | 188, ply 3 | 6,381, ply 7 | 33, ply 3 |
| 09 | 5 | 5,305, ply 5 | 108,269, ply 9 | 348, ply 5 |
| 0D | 7 | 57,058, ply 7 | 914,057, ply 11 | 1,738, ply 7 |
| 11 | 9 | 1,876,589, ply 9 | 594,256, ply 13 (stopped) | 8,874, ply 9 |
| 15 | 11 | 604,301, ply 11 (stopped) | | 75,592, ply 11 |
| 19 | 13 | | | 270,772, ply 13 |
| 1B | 15 | | | 517,273, ply 15 |

(Middlegame is the Kiwipete-like position, pawn ending the locked one
from "Search depth".) The makes at `00` are the root moves, made once
before the first iteration. In the quiet opening and the pawn ending the
deepest ply is exactly h. In the middlegame it is h + 4: the extension
reaches `1796`. The pawn ending ran through all iterations up to `1B` and
stopped after 2.1 s, which is why Psion moves fast in locked endings.
Within 15 s Psion finished a nominal 9 plies in the opening and 7 in the
middlegame. Our info lines showed 8 for all of these, as the depth was
then the length of the PV, which is capped at 8 moves. Since v2.7 they
show h as `depth` and the deepest ply made so far as `seldepth`.

**Selective depth in play.** The info lines of the Lichess bot's first
games with v2.7 (2026-11-09, lichess-bot's log on the VPS): of 530 lines,
249 had `seldepth` equal to `depth` and 281 (53%) a larger one:

| `seldepth` − `depth` | Lines |
|---|---|
| 0 | 249 |
| +1 | 76 |
| +2 | 67 |
| +3 | 39 |
| +4 | 99 |

Never more than 4: past ply h the table holds only `186D 175A 186D
1796`, and `1796` at h + 4 always stops. In the searches of 15 s above,
the middlegame showed +4 in every line (3/7, 5/9, 7/11), the opening and
the pawn ending 0.

## Time use of the iterations

Measured to see where time management could help; nothing here is used
by the adapter. A probe build logged, with the time since `go`, every
change of `5A67`, every new root line (score and first move from the
copy at `7150`), every root fail low (0 written to `713A`) and fail high
(0 written to `704C`), and when the adapter pressed MOVE-NOW (experiment
not kept). The games were against Fairy-Max 5.0b at 10+0.1 from the UHO
openings, as in "How often" above: 40 games, 2,106 searches. Below,
"budget" is the time from `go` to MOVE-NOW, and only the 2,012 searches
that MOVE-NOW ended after at least two iterations are counted (not book
moves, nor searches Psion ended itself).

**Cost of an iteration.** From the start of one iteration to the start
of the next, each finished iteration took a median 15.5 times as long as
the one before (quartiles 11 to 23) at `09` → `0D` (h 5 → 7; 572
searches), and 6.5 times at `0D` → `11` (58 searches, those that
finished h = 7 within the budget, so biased towards the cheap ones).

**Cost of the root moves.** Within an iteration the first root move (the
previous best) is the expensive one: the others only have to be refuted
against the best score so far (alpha, raised by the first move when it
comes back inside the window), which takes one good reply at the next
ply. From the root moves logged in the matches of "What an extension
after a drop buys" below (finished iterations from `09` on; each of
moves 2–5 is the time from the start of move 2 to that of move 6, over
4):

| First line of the iteration | Iterations | Move 1, share of the iteration | Each of moves 2–5, × move 1 (median, quartiles) | New best among moves 2–5 |
|---|---|---|---|---|
| no drop (< 6 units) | 4,654 | 35% | 0.12 (0.06–0.28) | 17% |
| a drop of 6–20 units | 1,876 | 26% | 0.28 (0.11–0.50) | 47% |
| a fail low | 264 | 5% | 0.38 (0.12–0.75) | – |

A move that becomes the new best is searched exactly and costs about
three times as much as one that is refuted (0.36 against 0.10 × move 1
without a drop). After a drop alpha is raised less, more of the other
moves come close to it, and they cost twice as much. After a fail low
the first move did not raise alpha at all: it was only refuted itself,
and so cheap (5% of the iteration), and the moves after it are searched
against the same bound, at a cost much closer to its own. The expensive
part of such an iteration is the search of all the moves again with
alpha = −∞.

**Depth reached in play.** The depth of the last line of each search
(the deepest iteration that finished at least one root move), from the
first 80 games of a self-play match at 10+0.1 from the 8moves_v3
openings (book moves left out; time is the mean per move). One side
used the time management as it is (base), the other the soft stop at
x = 0.4 described below (`TM Soft Stop 40`):

| Depth | base | | | soft stop | | |
|---|---|---|---|---|---|---|
| 3 | 254 | 5.2% | 0.12 s | 242 | 5.0% | 0.11 s |
| 5 | 3,803 | 78.3% | 0.24 s | 3,679 | 75.7% | 0.22 s |
| 7 | 733 | 15.1% | 0.17 s | 868 | 17.9% | 0.19 s |
| 9 | 64 | 1.3% | 0.15 s | 69 | 1.4% | 0.16 s |
| 11 | 2 | 0.0% | 0.14 s | 2 | 0.0% | 0.17 s |

At this time control Psion mostly gets to depth 5; depth 7 and more
come mostly in simpler positions (endgames), where the iterations are
cheap, which is why they took less time. The soft stop moved about 3%
of the moves from depth 5 to 7 (mean depth 5.25 → 5.32).

By the phase of the game (218 games of the same match; mean time and
depth per move, book moves left out):

| Moves | base: time | depth | soft stop: time | depth |
|---|---|---|---|---|
| 1–20 | 0.362 s | 5.01 | 0.300 s | 5.01 |
| 21–40 | 0.253 s | 5.11 | 0.239 s | 5.18 |
| 41–60 | 0.175 s | 5.61 | 0.176 s | 5.79 |
| 61–80 | 0.132 s | 5.69 | 0.136 s | 5.78 |
| 81+ | 0.110 s | 5.42 | 0.113 s | 5.38 |

Up to move 20 the soft stop saved 17% of the time at the same depth:
that time went into iterations that would not have finished a move.
From move 21 to 80 it reached more depth with about the same time per
move: the time left on the clock went to the searches whose next
iteration could still finish its first move. From move 81 both sides
live mostly on the increment, and the difference is gone.

**The last iteration** got its first move searched in only 36% of the
searches, and its move differed from the previous iteration's in 11%.
How much it does depends on when it started:

| Started at (budget) | Searches | First move done | Move changed |
|---|---|---|---|
| 0–10% | 23 | 96% | 70% |
| 10–20% | 265 | 91% | 37% |
| 20–30% | 286 | 81% | 22% |
| 30–40% | 296 | 44% | 7% |
| 40–50% | 277 | 18% | 2% |
| 50–60% | 239 | 8% | 1% |
| 60–80% | 351 | 7% | 3% |
| 80–100% | 274 | 1% | 0% |

An iteration started after 40% of the budget almost never changes the
move. Sending MOVE-NOW as soon as an iteration starts after a fraction x
of the budget (a soft limit) would have:

| x | Searches stopped early | Time saved | Move changes lost (of 216) |
|---|---|---|---|
| 0.3 | 71% | 27% | 40 |
| 0.4 | 57% | 19% | 18 |
| 0.5 | 43% | 12% | 13 |
| 0.6 | 31% | 7% | 10 |

**Score drops.** The score of the first move of an iteration, minus the
previous iteration's final score (units, 24 per pawn; iterations that
failed high and mate scores left out). After a fail low (below −20) the
first score is that of the re-search with alpha = −∞. "Move 1 done" is
when the first move's line came, as a share of the iteration's time (to
MOVE-NOW for the last iteration).

| Change | Finished iterations | Move changed | Move 1 done | Last iterations | Move changed | Budget left after move 1 |
|---|---|---|---|---|---|---|
| below −20 | 61 | 33% | 75% | 29 | 17% | 20% |
| −20 to −11 | 116 | 61% | 44% | 84 | 51% | 42% |
| −10 to −6 | 204 | 41% | 37% | 142 | 50% | 46% |
| −5 to −1 | 564 | 26% | 39% | 246 | 26% | 38% |
| 0 | 139 | 15% | 41% | 60 | 15% | 39% |
| +1 to +4 | 203 | 12% | 43% | 80 | 16% | 25% |
| +5 to +9 | 71 | 13% | 45% | 32 | 19% | 37% |
| +10 to +19 | 29 | 10% | 41% | 13 | 15% | 29% |

A drop of 6 to 20 units (¼ to ¾ pawn) inside the window is a good sign
that another move will take over: about half the time, against about
15% when the score holds or rises, and it comes with a median of 40% of
the budget still left. Fail lows are different: all root moves are
first searched against the window, and only then all again with alpha =
−∞, from the old best, so the first line comes at 75% of the iteration
and the move changes less often. Of the 67 fail lows in
the last iteration, 33 were stopped before the re-search had finished
its first move; of the other 34, five changed the move. The last
iteration also failed high 65 times.

**At 60+0.6** (12 games, 748 searches, 710 counted) the picture is the
same, shifted to deeper iterations: each took 18 times the one before at
`09` → `0D`, 10.7 at `0D` → `11` (414 searches) and 8.9 at `11` → `15`
(22). The last iteration finished its first move in 40% of the searches
and changed the move in 7%:

| Started at (budget) | Searches | First move done | Move changed |
|---|---|---|---|
| 0–10% | 50 | 94% | 30% |
| 10–20% | 172 | 85% | 16% |
| 20–30% | 118 | 53% | 5% |
| 30–40% | 74 | 26% | 4% |
| 40–50% | 49 | 10% | 0% |
| 50–100% | 247 | 1% | 0% |

A soft limit at x = 0.3 would have saved 18% of the time and lost 3 of
the 51 move changes, at x = 0.4 12% and none. After a drop of 6 to 20
units the move changed in 55% of the finished iterations (284), against
11% when the score held or rose (234). The last iteration failed low 18
times: 11 were stopped before the re-search had finished its first
move; of the other 7, three changed the move.

**What an extension after a drop buys.** To see what extra time after a
drop is used for, the probe also logged every root move: the root node
is ply 1, so a root move is made when the ply counter `7076` goes from
2 to 4 (ply 1 → 2), and `5A6E` then holds the move. After a fail low all
root moves are searched again, so each root move was counted once per
iteration, when it was first made. Two matches against Fairy-Max at 10+0.1 from the
8moves_v3 openings ran side by side on one machine, 60 games each with
the same openings, both with the soft stop (`TM Soft Stop 40`): one with
`TM Drop Extension 160` (`TM Drop 6`, `TM Max 300`, `TM Max Share 25`),
the other without. Counted are the searches in which, when the
unextended target S was reached, the first line of the running iteration
had dropped by 6 units or more; up to S the two matches play by the same
rules, so these are like searches in both:

| | With extension | Without |
|---|---|---|
| Drop searches (of all searches) | 228 (of 3,410) | 228 (of 3,311) |
| Root moves in the position (median) | 26 | 31 |
| Root moves of the iteration reached by S (mean) | 13.0 | 13.3 |
| Time after S (median) | 93 ms | 9 ms |
| Root moves started after S (mean, median) | 9.4, 7 | 0.1, 0 |
| Iteration finished | 65% | 0% |
| Another move played than the one that dropped | 54% | 33% |

Without the extension Psion stops at S, with about half of the root
moves searched. With it, it searches another third of them and finishes
the iteration in two searches out of three; the move played differs from
the one that dropped in 54% of these searches instead of 33%, so in
about one in five the extension found another move. The median extra
time is well below the 60% allowed: once the iteration has finished, the
next one starts after S and the soft stop ends the search. The cost over
all searches is small (215 ms against 213 ms per search), as drops at S
came in only 7% of them. A third run with the extension, on another
machine (346 drop searches, median 37 root moves), gave 16 root moves
started after S, 67% finished and 58% another move.

Up to S the two matches are alike, so the extension's effect is in the
changes that came after S. In the match with it, 60 drop searches (26%)
had another move by S, 63 (28%) still had the dropped move at S and
played another, and 105 (46%) played the dropped move; without it, 75
(33%) changed by S and none after (the two runs diverge after the first
extension, so they hold different positions). The root moves searched by
S were a median 7 (quartiles 3 and 23; the mean of 13 above comes from a
few long iterations), and the move that took over after S was mostly the
one being searched at S or one of the next two.

How much these changes were worth, by Psion's own scores: the score of
the move played minus that of the dropped move (the first line of the
iteration), in the same iteration, both runs with the extension (mates
left out):

| Better by | Changes after S | Changes by S |
|---|---|---|
| < ¼ pawn | 68 | 95 |
| ¼–½ pawn | 38 | 67 |
| ½–1 pawn | 18 | 18 |
| 1–2 pawns | 1 | 8 |
| 2 pawns or more | 6 | 4 |
| Median | 0.21 pawn | 0.25 pawn |

Most changes improve on the dropped move by a quarter of a pawn or less,
but one in five by half a pawn or more: there the extension kept Psion
from playing a move its own deeper search had just found worse. With a
drop at S in about 5% of the moves, about 28% of them changing the move
only after S, and about 65 moves per side, that is about one such save
every five or six games. The changes after S are of the same size as
those Psion finds by S anyway: the extension adds more of them where the
search would otherwise have been cut off.

A changed move is a different decision, not necessarily a better one;
whether spending time this way wins games only a match can show.

## Mate scores

A mate score counts the plies to the mate, 2 units per ply (the ply
counter `7076` holds ply × 2), from the top of the scale for the side
that mates and from the bottom for the side that is mated. Seen in the
copies of the root score at `7150` (positions checked with python-chess,
the mates in 3 and the mated in 1 found by a brute-force solver):

| Position | Plies to the mate | Score |
|---|---|---|
| The side to move mates in 1 | 1 | `FFFE` = `10000` − 2·1 |
| Mates in 2 (Legal's mate) | 3 | `FFFA` = `10000` − 2·3 |
| Mates in 3 (`8/8/6k1/8/8/8/1K2R3/7Q w`) | 5 | `FFF6` = `10000` − 2·5 |
| Is mated in 1 (`8/8/8/1Q6/4R3/8/k1K5/8 b`) | 2 | `0004` = 2·2 |

So a mate in n moves is `10000` − 2(2n − 1), and being mated in n moves
is 4n. Until the search reaches the mate, the score is an ordinary one:
K+Q against K about +8 pawns (`80C0`), the mate in 3 +13 pawns until
depth 5 found it, and a mated in 2 (`8/7Q/8/8/3K4/8/3k4/4R3 b`) stayed at
−8.6 pawns within 3 s. Ordinary scores stay within a few hundred units
of `8000`, so the adapter takes 256 pawns (6144 units) or more from it as
a mate.

**Mates at the horizon.** A mate needs the mated side's next ply: with no
legal move there, the score becomes a mate score. At the horizon (`1778`
at ply h, `1796` at h + 4) there is no such ply, and the leaf test at
`1799` looks for a mate only after the static score: it first compares
the score after the move with the bound and cuts the move if it is not
better (`neg bp; cmp bp,dx; jbe 1754`), and only then, for a checking
move, asks `206B` whether the opponent has a reply (`17A1`..`17B4`). So a
mate by the horizon move counts only when the mating side is ahead
anyway, and a mate after a sacrifice is found only in an iteration in
which it lies inside the tree, or through an extension (`714C`).
(Read from the code, not instrumented.)

Example, a mate in 2 (3 plies) with a queen sacrifice,
`r4br1/3b1kpp/1q1P4/1pp1RP1N/p7/6Q1/PPB3PP/2KR4 w`: 1. Qg6+ hxg6 2. fxg6#.
At depth 3, fxg6# is the horizon move, a queen down, and is cut; the
lines show seldepth 3. Depth 5 searches ply 4, finds no reply and scores
`FFFA` (mate in 2), after 278 ms (seldepth 9):

```
info depth 3 seldepth 3 time 11 nodes 641 ... score cp 17 pv g3d3 a8a6 e5d5
info depth 5 seldepth 9 time 164 nodes 10506 ... score cp 54 pv c2e4 d7c6 e4c6 b6c6 d6d7
info depth 5 seldepth 9 time 278 nodes 18078 ... score mate 2 pv g3g6 h7g6 f5g6
```

Legal's mate (1. Bxf7+ Ke7 2. Nd5#) is found at depth 3, but with
seldepth 5: the extension after the checks took the line past the
horizon. The same blindness at the leaf makes Psion miss a stalemate at
the horizon: the leaf evaluates statically and asks about replies only
after a check.

## The principal variation

**The PV table** at `7090` is a triangular table: one row of 9 words per
ply, row k at `7090` + 18k (so ply 1 at `70A2`), each a list of moves
ending in 0. It is kept only up to ply 8 (`cmp al,11` at `15CE`,
`15FF` and `17D6`); deeper plies write nothing.

- When a searched move at ply k becomes the node's new best (`14B0`),
  `15CE` writes it into row k and copies row k + 1 (the child's line)
  after it, up to the 0. At ply 8 only the move is written.
- When a leaf at ply k (the leaf test at `1799`, see "Iterative
  deepening") beats the node's best, `17E4` writes the move and a 0 into
  row k, and clears row k + 1.
- A node with no legal move clears row k + 1 (`1452`).

A child improves its parent only if it raised its own alpha (a child that
returns its alpha makes the parent cut off), so the row it passes up is
always fresh. Row 1 is the PV of the root. Its length is h plus the
extensions on that line, capped at 8. This is why our PVs, and the depth
we derive from them, never pass 8.

**When it is shown.** When a root move becomes the new best (ply 1 at
`14B6`), `0AEF` calls `043E` with the new root best (`704E`), provided
bit `80` of `5A6B` is set (it was `FF` in all our searches). `043E`
copies the score to `7150` and the first 8 words of row 1 to `7152`. If
the analysis display is on (`7259`, toggled by the `a` key at `0A64`) and
the score and first move are non-zero, it draws the line on the screen.
That screen line is what our info lines are parsed from. The end of the
search (`06F7`) shows it once more, with `5A76`.

So Psion shows a new line each time the root best changes. Because the
best move so far is searched first in each iteration, that is at least
once per iteration (as soon as the first root move has been searched),
and again whenever another root move beats it. A root fail high
(`114C`) shows nothing (the call at `1160` goes to a bare `ret`); the
re-search with beta = +∞ that follows does. After a fail low (`1125`)
alpha is −∞, so the first move searched again shows a line.

Logged at each write of the last PV word at `7160` and of row 1 at ply 1
(15 s from 1.a4 h5, normal core; experiment not kept):

| Nodes | `5A67` | Score (`7150`) | Moves in row 1 | Our info line |
|---|---|---|---|---|
| 328 | 05 | `7FFF` | 3 (e2e4 …) | none (not stable for long enough) |
| 2,304 | 09 | `7FF7` | 5 (e2e4 …) | none |
| 4,579 | 09 | `7FFE` | 5 (d2d4 …) | depth 5, cp 0 |
| 32,653 | 0D | `7FFB` | 7 (d2d4 …) | depth 7, cp −20 |
| 633,463 | 11 | `7FF7` | 8 (d2d4 …) | depth 8, cp −40 |
| 1,341,779 | 11 | `7FF9` | 8 (e2e4 …) | depth 8, cp −30 |

The lines shown at 328 and 2,304 nodes were replaced within a few
milliseconds, before the screen parser we used then took them (it waited
for the row to be unchanged for 50 ms). We now read these copies from
memory instead (see implementation.md). In the
Kiwipete-like middlegame each iteration showed one line, all with e2a6,
of 7, 7 and 8 moves at h = 3, 5 and 7: the extensions made them longer
than h.

**The move played.** After the search (`244A`, `call 0FFD`), the
think routine plays `[si]`, the first move of the root's move list
(`DS:9070` in our dump: the top `9074` minus 4) (`2465`: make on the search board; `246F`..`2471`: the game).
That is the move `11A6` put there last, which is the first move of the
last line shown, with one exception: a root fail high also calls `11A6`
(`1147`), with no line shown. If the search ends during the re-search
that follows, before that move becomes the new best, Psion plays it
although no line had it. Our last `info` line then gives it as a lower
bound, which is right: it scored at least beta. In self-play of 80
plies at 600 ms and 150 plies at 250 ms per move (217 searches), the
played move was always the first move of the last line.

`1166` also copies the first 5 words of row 2 into the current root
move's entry in `6E00`, and `11E4` copies 6 words of row 1 into the
entry of a new best. What reads these stored lines is not decoded.

## Playing a move

When Psion plays a move, its own after a search or one typed, `28D2`
first adds 1 to the ply counter `7260` (and moves the pointer `7264`).
Within about 1 ms the routine at `4BBB` then copies the whole board to
the game board at `28AC` (64 bytes, as words, a8 first) twice: first
with the piece lifted (for e2e4 both e2 and e4 are empty), then with the
move done. Nothing else is written there until the next move. `725B`
(the side to move) is not written when Psion plays its own move.

Logged for 7 of Psion's moves and the moves the adapter typed in between
(searches of 500 ms; experiment not kept). Before, the adapter accepted
each move 21 to 30 ms after the last copy: the 20 ms it waited for the
board to stay unchanged, and its polling. It now accepts a move at once
when the board differs by one move, the ply counter has changed and no
copy is under way (the node counter's write handler watches `28AC`).
Castling, en passant and promotions were not logged and still wait: a
copy with only the king, or only the pawn, moved might exist. With
`Move Overhead` 0 and `isready` before each `go`, 20 moves in a row took
(median) 209 ms instead of 230 ms at `go movetime 200`, and 60 ms
instead of 79 ms at `go movetime 0`.

## Repetitions in the search

Psion's only repetition test in the search is in make (`1DE0`..`1E89`),
and it compares moves, not positions:

- The moves of the game and of the current search path are in one list:
  `[7264]` points just past the game's last move (`28F4`..`28F6` advance
  it when a move is played), and make stores the move at ply k at
  `[7264]` + 2k − 2 (`1DE8`).
- If the move reverses the same side's move two plies earlier (from and
  to swapped, `1DED`), and the opponent's move in between has none of the
  flag bits `1111` (capture, double step, promotion, ...; `1E6B`), make
  checks with `1A51` whether the opponent could reverse its move as well
  (side flipped, `1E73`). It returns BP = `0002` or `FF02` (sign set)
  instead of 2, and sets `5A68`. How the callers score that (`1274` at
  the root jumps to `1255` on a negative BP) is not decoded.

So only the shortest cycle is seen, a piece going back and forth while the
opponent does the same (the position four plies earlier, or the start
of one), also across the game's last moves. Longer cycles, such as two
pieces of one side moving in turn, are not, and occurrences are not
counted. The threefold repetition rule itself Psion applies only to the
game: it announces the draw and stops (see implementation.md, "Quirks").

**In the game** Psion counts positions, not moves, but without the
castling and en passant rights. Tried through the adapter (`position`,
then `go`; "DRAW" means `info string Game over (Psion shows DRAW)`):

| Moves | FIDE | Psion |
|---|---|---|
| `g1f3 g8f6 f3g1 f6g8`, twice (start position a third time) | draw | DRAW |
| the same once (twice) | no draw | plays on |
| `7k/8/8/8/8/8/8/R3K3 w`: rook a1-a2-b2-b1-a1, king h8-g8-h8, twice (a third time, 8-ply cycle) | draw | DRAW |
| the same once | no draw | plays on |
| `g1f3 g8f6`, then `h1g1 h8g8 g1h1 g8h8` twice: the third time, but the first had castling rights | no draw | DRAW |
| `e2e4 b8c6 e4e5 d7d5`, then `g1f3 c6b8 f3g1 b8c6` twice: the third time, but the first allowed `e5d6` | no draw | DRAW |

So Psion can declare a draw where the rules do not, after a king or rook
shuffles back or after a double pawn step that allowed en passant. It then
takes no more moves, and a `go` gets `bestmove 0000`.

In a match at 5+0.05 (current main against v2.6.1, both affected) one
game produced fastchess warnings "PV continues after threefold
repetition": after `... a2a4 c7d8 f2f4 d8c7 f4f3 c8f8 f1f2 c7d8 f3f4
f8e8 a4a2`, Black's PV `d8c7 f4f3 b7b5` (score −4) passes through the
third occurrence of a position: after `d8c7 f4f3` it is the position
of plies 107 and 115. The cycle there is 8 plies long, with queen and
rook moves of both sides in turn (`d8c7` follows Black's rook move
`f8e8`, not the queen move it reverses), so the test above does not see
it.
