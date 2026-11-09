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
loop at `107E`..`10C1`). After the first iteration (`5A67 & 3` non-zero),
the window is the previous root score v (`704E`) ± 20 units, about one
pawn: alpha = v − 20 (also kept in `713A`), beta = v + 20 (`704C` =
−(v + 20)). In the first iteration `704E` is set to `F000` and `704C`
to 0 (beta = +∞); with the bias, `F000` would be a very high alpha, so
that iteration may work differently (`11A6` also branches on `5A67 & 3`).

- Fail low (the root best is still alpha, `10AA`): `1125` sets alpha to
  −∞ (0) and searches again (`jmp 1089`).
- Fail high (a cutoff at the root brings the ply back to 0, `14D2` →
  `1466` → `1147`): `114C` sets beta to +∞ and searches again from the
  old alpha.

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
   knights, `3153`, `314B`, `3146`, `3325`/`3320` king). The generator
   pushes them onto a stack used as the move list (`push cx; push ax`),
   leaving out the moves already tried in stages 2 and 3 (`cmp ax,bp`,
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
ply). Up to ply 8 (counter below `11`) it is also stored in a table of 9 bytes per
ply at `7090` (purpose not known).

The root moves are kept in a table at `6E00` (12 bytes per move, filled
at `1068`), and `11A6` works through it at each iteration; how they are
reordered between iterations is not decoded. There is no history table,
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
middlegame. Our info lines show 8 for all of these, as the PV is capped
at 8 moves.
