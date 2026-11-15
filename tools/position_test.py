#!/usr/bin/env python3
"""Tests a time management idea on positions instead of in a match: does
more time pay more in a selected kind of position than in others?

usage: position_test.py collect [--player NAME] [--min-ply N] [--sample N]
                                [--seed N] SELECTED CONTROL PGN...
       position_test.py play [--times T,T,...] [--workers N]
                             POSITIONS OUT ENGINE [ARGS...]
       position_test.py judge [--nodes N] [--workers N] [--stockfish CMD]
                              IN OUT
       position_test.py report [--undecided LO,HI] SELECTED CONTROL

collect  Takes the positions from the games (after the first --min-ply
         plies, not in check, more than one legal move, each position
         once) and writes those with the opponent's pawn on its 7th rank,
         the square in front empty, to SELECTED, and one other position
         per game to CONTROL, both cut to --sample positions. In games of
         --player only the positions with that player to move are used.
         The selection is select() below: change it for another idea.
play     Lets the engine move in each position at each --times (movetime,
         in seconds; a new game each time), and writes the moves and
         depths as JSON lines.
judge    Lets Stockfish (--nodes per search, one thread per worker)
         score the engine's moves, and its own best move, as the win
         chance of the side to move (the Lichess formula: mates are 0 or
         100).
report   For each doubling of the time: how often the move changed, the
         mean gain in win chance (points, with a 95% interval) and the
         share of moves 10 points better or worse, for all positions and
         for those where Stockfish's best move gives LO to HI%.

play and judge append to OUT and skip the positions already in it, so
an interrupted run can be resumed. Needs python-chess (pip install chess).

e.g.   position_test.py collect --player psion-chess --sample 3000 \\
           sel.txt ctl.txt match.pgn bot.pgn
       position_test.py play sel.txt sel.jsonl ./psion
       position_test.py judge sel.jsonl sel_sf.jsonl
       (the same for ctl), then position_test.py report sel_sf.jsonl ctl_sf.jsonl
"""
import argparse
import concurrent.futures
import json
import math
import random
import statistics
import sys
import threading

import chess
import chess.engine
import chess.pgn


def select(board):
    """The opponent of the side to move has a pawn on its 7th rank with
    the square in front empty."""
    side = not board.turn
    rank, step = (6, 8) if side == chess.WHITE else (1, -8)
    return any(chess.square_rank(s) == rank and board.piece_at(s + step) is None
               for s in board.pieces(chess.PAWN, side))


def collect(args):
    rng = random.Random(args.seed)
    selected, control, seen = [], [], set()
    for name in args.pgn:
        with open(name) as f:
            while (game := chess.pgn.read_game(f)):
                h = game.headers
                if h.get('Variant', 'Standard') != 'Standard' or 'FEN' in h:
                    continue
                only = {h['White']: chess.WHITE, h['Black']: chess.BLACK}.get(args.player)
                board, others = game.board(), []
                for move in game.mainline_moves():
                    if (board.ply() >= args.min_ply and only in (None, board.turn)
                            and not board.is_check() and board.legal_moves.count() > 1):
                        key = board.epd()
                        if key not in seen:
                            seen.add(key)
                            (selected if select(board) else others).append(board.fen())
                    board.push(move)
                if others:
                    control.append(rng.choice(others))
    for fens, out in ((selected, args.selected), (control, args.control)):
        rng.shuffle(fens)
        with open(out, 'w') as f:
            f.writelines(fen + '\n' for fen in fens[:args.sample])
    print('%d selected, %d control positions (before --sample)'
          % (len(selected), len(control)))


def run_parallel(items, out_name, workers, start, work):
    """Runs work(engine, item) for the items not yet in out_name, one
    engine (from start()) per thread, appending the results."""
    try:
        with open(out_name) as f:
            done = {json.loads(line)['fen'] for line in f}
    except FileNotFoundError:
        done = set()
    out = open(out_name, 'a')
    lock, local, engines = threading.Lock(), threading.local(), []

    def task(item):
        if not hasattr(local, 'engine'):
            local.engine = start()
            with lock:
                engines.append(local.engine)
        result = work(local.engine, item)
        with lock:
            out.write(json.dumps(result) + '\n')
            out.flush()

    try:
        with concurrent.futures.ThreadPoolExecutor(workers) as ex:
            list(ex.map(task, [i for i in items if i['fen'] not in done]))
    finally:
        for engine in engines:
            engine.quit()


def play(args):
    times = [float(t) for t in args.times.split(',')]
    with open(args.positions) as f:
        items = [{'fen': line.split('\t')[0].strip()} for line in f if line.strip()]

    def work(engine, item):
        board, moves = chess.Board(item['fen']), {}
        for t in times:
            r = engine.play(board, chess.engine.Limit(time=t),
                            info=chess.engine.INFO_BASIC, game=object())
            moves[str(t)] = [r.move.uci(), r.info.get('depth')]
        return {'fen': item['fen'], 'moves': moves}

    run_parallel(items, args.out, args.workers,
                 lambda: chess.engine.SimpleEngine.popen_uci(args.engine), work)


def win_chance(score):
    if score.is_mate():
        return 100.0 if score.mate() > 0 else 0.0
    return 50 + 50 * (2 / (1 + math.exp(-0.00368208 * score.score())) - 1)


def judge(args):
    with open(args.inp) as f:
        items = [json.loads(line) for line in f]
    limit = chess.engine.Limit(nodes=args.nodes)

    def start():
        engine = chess.engine.SimpleEngine.popen_uci(args.stockfish)
        engine.configure({'Threads': 1, 'Hash': 64})
        return engine

    def work(engine, item):
        board = chess.Board(item['fen'])
        moves = sorted({m for m, _ in item['moves'].values()})
        infos = engine.analyse(board, limit, multipv=len(moves), game=object(),
                               root_moves=[chess.Move.from_uci(m) for m in moves])
        best = engine.analyse(board, limit, game=object())
        item['eval'] = {i['pv'][0].uci(): win_chance(i['score'].relative) for i in infos}
        item['best'] = [best['pv'][0].uci(), win_chance(best['score'].relative)]
        return item

    run_parallel(items, args.out, args.workers, start, work)


def report(args):
    lo, hi = (float(x) for x in args.undecided.split(','))
    samples = {}
    for name in (args.selected, args.control):
        with open(name) as f:
            samples[name] = [json.loads(line) for line in f]
    times = list(samples[args.selected][0]['moves'])
    for label, keep in (('all positions', lambda _: True),
                        ('undecided (best move %g-%g%%)' % (lo, hi),
                         lambda r: lo <= r['best'][1] <= hi)):
        print('==', label)
        for name in (args.selected, args.control):
            rs = [r for r in samples[name] if keep(r)]
            print('%s: %d positions' % (name, len(rs)))
            if len(rs) < 2:
                continue

            def wc(r, t):
                return r['eval'][r['moves'][t][0]]

            for a, b in zip(times, times[1:]):
                d = [wc(r, b) - wc(r, a) for r in rs]
                changed = sum(r['moves'][a][0] != r['moves'][b][0] for r in rs)
                print('  %5s -> %-5s s  changed %4.1f%%  gain %+5.2f +/- %4.2f'
                      '  better/worse by 10+ %4.1f%% / %4.1f%%'
                      % (a, b, 100 * changed / len(rs), statistics.mean(d),
                         1.96 * statistics.stdev(d) / math.sqrt(len(d)),
                         100 * sum(x >= 10 for x in d) / len(rs),
                         100 * sum(x <= -10 for x in d) / len(rs)))
            print('  loss against the best move: %s' % '  '.join(
                '%s s %.2f' % (t, statistics.mean(r['best'][1] - wc(r, t) for r in rs))
                for t in times))


def main():
    parser = argparse.ArgumentParser(
        description='Tests a time management idea on positions, with Stockfish as judge.')
    sub = parser.add_subparsers(dest='command', required=True)
    p = sub.add_parser('collect')
    p.add_argument('--player')
    p.add_argument('--min-ply', type=int, default=16)
    p.add_argument('--sample', type=int, default=3000)
    p.add_argument('--seed', type=int, default=1)
    p.add_argument('selected')
    p.add_argument('control')
    p.add_argument('pgn', nargs='+')
    p = sub.add_parser('play')
    p.add_argument('--times', default='0.25,0.5,1,2')
    p.add_argument('--workers', type=int, default=6)
    p.add_argument('positions')
    p.add_argument('out')
    p.add_argument('engine', nargs='+')
    p = sub.add_parser('judge')
    p.add_argument('--nodes', type=int, default=2_000_000)
    p.add_argument('--workers', type=int, default=8)
    p.add_argument('--stockfish', default='stockfish')
    p.add_argument('inp')
    p.add_argument('out')
    p = sub.add_parser('report')
    p.add_argument('--undecided', default='10,90')
    p.add_argument('selected')
    p.add_argument('control')
    args = parser.parse_args()
    {'collect': collect, 'play': play, 'judge': judge, 'report': report}[args.command](args)


if __name__ == '__main__':
    sys.exit(main())
