"""Prints the UCI position command for a position in a Lichess game, with
the moves of the game instead of a FEN.

usage: python3 lichess_position.py URL
       python3 lichess_position.py GAME_ID PLY
       python3 lichess_position.py --pgn FILE PLY

e.g.   python3 lichess_position.py https://lichess.org/game/z2JNAv5h/white#36

Needs python-chess (pip install chess) and curl. PLY is the number of
half-moves played, as after the # in a Lichess link (#36 is the position
before White's 19th move); without it, the final position is taken.

Psion searches a position set up from a FEN differently from the same
position reached by playing the moves (probably because its piece list
is then in another order), so only the moves reproduce the search of a
game. The PGN is fetched with curl, since Lichess blocks Python's own
HTTP clients.
"""
import argparse
import io
import re
import subprocess
import sys

import chess
import chess.pgn


def fetch_pgn(game_id):
    url = f"https://lichess.org/game/export/{game_id}?clocks=false&evals=false"
    result = subprocess.run(["curl", "-sSf", url], capture_output=True,
                            text=True)
    if result.returncode != 0:
        sys.exit(f"curl failed for {url}: {result.stderr.strip()}")
    return result.stdout


def parse_url(url):
    m = re.search(r"lichess\.org/(?:game/)?([A-Za-z0-9]{8})"
                  r"(?:[A-Za-z0-9]{4})?(?:/(?:white|black))?(?:#(\d+))?", url)
    if not m:
        sys.exit(f"not a Lichess game link: {url}")
    return m.group(1), None if m.group(2) is None else int(m.group(2))


def main():
    parser = argparse.ArgumentParser(
        description="Prints the UCI position command for a position in a "
                    "Lichess game, with the moves of the game.")
    parser.add_argument("--pgn", help="read the game from this PGN file")
    parser.add_argument("game", nargs="?", help="Lichess link or game id")
    parser.add_argument("ply", nargs="?", type=int)
    args = parser.parse_args()

    ply = args.ply
    if args.pgn:
        with open(args.pgn) as f:
            text = f.read()
        if args.game is not None:
            ply = int(args.game)
    elif args.game is None:
        parser.error("give a Lichess link, a game id or --pgn")
    else:
        game_id, url_ply = parse_url(args.game) if "/" in args.game \
            else (args.game, None)
        ply = url_ply if ply is None else ply
        text = fetch_pgn(game_id)

    game = chess.pgn.read_game(io.StringIO(text))
    if game is None:
        sys.exit("no game in the PGN")
    moves = list(game.mainline_moves())
    if ply is None:
        ply = len(moves)
    if not 0 <= ply <= len(moves):
        sys.exit(f"ply {ply} is outside the game (0 to {len(moves)})")

    board = game.board()
    start = "startpos" if board.fen() == chess.STARTING_FEN \
        else f"fen {board.fen()}"
    for move in moves[:ply]:
        board.push(move)
    uci_moves = " ".join(m.uci() for m in moves[:ply])
    print(f"position {start}" + (f" moves {uci_moves}" if uci_moves else ""))
    print(f"# {board.fen()}", file=sys.stderr)


if __name__ == "__main__":
    main()
