"""Dumps Psion's opening book, from Chess2.ovl, as PGN with variations or
as a list of lines.

usage: python3 psion_book.py [--lines] [CHESS2_OVL]

e.g.   python3 psion_book.py ../data/Chess2.ovl > psion-book.pgn

Needs python-chess (pip install chess). Without CHESS2_OVL the file is
looked for in $PSION_DIR and in ../data. In the PGN, a move Psion only
recognises (it never plays it itself) gets ?!, a move it prefers gets !.
With --lines, every line of the book is printed on its own, with ~ after
a move Psion only recognises and ^ after one it prefers.

The format is described in docs/opening-book.md.
"""
import argparse
import os
import sys

import chess
import chess.pgn

# The book in the load image of Chess2.ovl (after its 512-byte header).
HEADER = 0x200
BOOK_START = 0xA4DC
BOOK_END = 0xC1C0

PUSH = 0x10      # from-byte: a sibling follows after this move's subtree
END = 0x01       # from-byte: the line ends after this move
NOT_PLAYED = 0x01  # to-byte: Psion recognises the move but never plays it
PREFERRED = 0x10   # to-byte: Psion prefers the move


def square(byte):
    """A square as an offset on Psion's search board: (row * 16 + file) * 2,
    row 0 being rank 8."""
    offset = (byte & 0xEE) // 2
    return chess.square(offset & 7, 7 - (offset >> 4))


def read_book(path):
    with open(path, "rb") as f:
        data = f.read()[HEADER:]
    if data[BOOK_START:BOOK_START + 2] != b"\x88\xd8":
        sys.exit(f"{path}: no book at the expected place")
    return data[BOOK_START:BOOK_END]


def decode(book):
    """Yields (line, move, to_byte) for every playable book move, line being
    the moves before it, in book order. An impossible entry (there is one:
    b1d7 for 6...Nd7 in a Caro-Kann) is skipped with its subtree, as Psion
    never finds it among the legal moves."""
    board = chess.Board()
    stack = []
    junk = None   # stack depth at an impossible entry, while in its subtree
    for i in range(0, len(book), 2):
        to_byte, from_byte = book[i], book[i + 1]
        if junk is None:
            move = chess.Move(square(from_byte), square(to_byte))
            if (board.piece_type_at(move.from_square) == chess.PAWN
                    and chess.square_rank(move.to_square) in (0, 7)):
                move.promotion = chess.QUEEN
            if move in board.legal_moves:
                yield list(board.move_stack), move, to_byte
                if from_byte & PUSH:
                    stack.append(board.copy())
                board.push(move)
            else:
                junk = len(stack)
                if from_byte & PUSH:
                    stack.append(None)
        elif from_byte & PUSH:
            stack.append(None)
        if from_byte & END:
            if not stack:
                return
            board = stack.pop()
            if junk is not None and board is not None and len(stack) <= junk:
                junk = None


def find_ovl():
    here = os.path.dirname(os.path.abspath(__file__))
    for directory in (os.environ.get("PSION_DIR"),
                      os.path.join(here, "..", "data")):
        if directory and os.path.isdir(directory):
            for name in os.listdir(directory):
                if name.lower() == "chess2.ovl":
                    return os.path.join(directory, name)
    sys.exit("Chess2.ovl not found: give its path")


def main():
    parser = argparse.ArgumentParser(
        description="Dumps Psion's opening book as PGN or as lines.")
    parser.add_argument("--lines", action="store_true",
                        help="print every line of the book on its own")
    parser.add_argument("ovl", nargs="?", help="path of Chess2.ovl")
    args = parser.parse_args()
    entries = list(decode(read_book(args.ovl or find_ovl())))

    if args.lines:
        # The entries come in book order, so the moves of a line are the
        # last entries at each depth. A line ends where the next entry
        # does not continue it.
        path = []
        for k, (line, move, to_byte) in enumerate(entries):
            board = chess.Board()
            for m in line:
                board.push(m)
            san = board.san(move)
            if board.turn == chess.WHITE:
                san = f"{board.fullmove_number}.{san}"
            san += ("~" if to_byte & NOT_PLAYED else "") + \
                ("^" if to_byte & PREFERRED else "")
            path = path[:len(line)] + [san]
            nxt = entries[k + 1][0] if k + 1 < len(entries) else []
            if nxt != line + [move]:
                print(" ".join(path))
        return

    game = chess.pgn.Game()
    game.headers["Event"] = "Psion Chess 2.13 opening book"
    game.headers["Site"] = "Chess2.ovl"
    game.headers["White"] = game.headers["Black"] = "?"
    game.headers["Result"] = "*"
    game.comment = (f"{len(entries)} moves. ?! = recognised only (Psion "
                    "never plays it), ! = preferred by Psion")
    nodes: dict[tuple, chess.pgn.GameNode] = {(): game}
    for line, move, to_byte in entries:
        parent = nodes[tuple(line)]
        node = parent.add_variation(move)
        if to_byte & NOT_PLAYED:
            node.nags.add(chess.pgn.NAG_DUBIOUS_MOVE)
        if to_byte & PREFERRED:
            node.nags.add(chess.pgn.NAG_GOOD_MOVE)
        nodes[tuple(line + [move])] = node
    print(game)


if __name__ == "__main__":
    main()
