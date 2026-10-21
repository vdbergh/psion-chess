"""Checks that the engine works with python-chess the way lichess-bot uses it.

usage: python3 lichess_check.py [--games N] [--tc BASE+INC] ENGINE_COMMAND...

e.g.   python3 lichess_check.py --games 2 --tc 60+1 ../psion

Needs python-chess (pip install chess). Two instances of the engine play
each other: each game starts fresh engines, as lichess-bot does, and the
engines get the clocks with every move (chess.engine.Limit with
white_clock etc.) and are asked for all info (INFO_ALL). As on Lichess,
the clocks start after the first move of each side. After the games,
fresh engines join a game in progress, as when lichess-bot restarts:
they first replay the game, on their clock.

Reported: start-up times, moves that were illegal or came too late,
problems python-chess logged (e.g. info lines it could not parse) and
the commands python-chess sent. The python-chess debug log is written to
lichess_check.log.
"""
import argparse
import collections
import logging
import sys
import time

import chess
import chess.engine

# lichess-bot starts engines with python-chess's default timeout for the
# UCI handshake (10 s).
STARTUP_TIMEOUT = 10.0
MAX_PLIES = 300


class Problems(logging.Handler):
    """Collects what python-chess logs at warning level or above, and the
    commands it sends."""

    def __init__(self):
        super().__init__(logging.DEBUG)
        self.problems = []
        self.commands = collections.Counter()

    def emit(self, record):
        message = record.getMessage()
        if record.levelno >= logging.WARNING:
            self.problems.append(message)
        elif ': << ' in message:
            command = message.split(': << ', 1)[1]
            self.commands[command.split()[0] if command else command] += 1


def start_engine(command):
    t0 = time.monotonic()
    engine = chess.engine.SimpleEngine.popen_uci(
        command, timeout=STARTUP_TIMEOUT, setpgrp=True)
    return engine, time.monotonic() - t0


def play(engines, board, base, inc, report, first_moves_free=True):
    """Plays from board until the game ends; engines[0] plays White.
    Returns the result and the time each side took for its first move."""
    clocks = [base, base]
    first = [None, None]
    while not board.is_game_over(claim_draw=True) and \
            len(board.move_stack) < MAX_PLIES:
        side = 0 if board.turn == chess.WHITE else 1
        limit = chess.engine.Limit(white_clock=clocks[0], black_clock=clocks[1],
                                   white_inc=inc, black_inc=inc)
        t0 = time.monotonic()
        result = engines[side].play(board, limit, info=chess.engine.INFO_ALL,
                                    ponder=False)
        used = time.monotonic() - t0
        if first[side] is None:
            first[side] = used
            if first_moves_free:
                used = 0
        clocks[side] -= used
        if clocks[side] < 0:
            report.append('ply %d: %s lost on time' %
                          (len(board.move_stack) + 1,
                           'White' if board.turn == chess.WHITE else 'Black'))
            return 'time', first
        clocks[side] += inc
        if result.move is None or result.move not in board.legal_moves:
            report.append('ply %d: illegal move %s' %
                          (len(board.move_stack) + 1, result.move))
            return 'illegal', first
        board.push(result.move)
    return board.result(claim_draw=True), first


def main():
    parser = argparse.ArgumentParser(
        description='Checks the engine the way lichess-bot uses it.')
    parser.add_argument('--games', type=int, default=2)
    parser.add_argument('--tc', default='60+1',
                        help='time control in seconds, BASE+INC')
    parser.add_argument('engine', nargs='+', help='engine command')
    args = parser.parse_args()
    base, inc = (float(x) for x in args.tc.split('+'))

    handler = Problems()
    logger = logging.getLogger('chess.engine')
    logger.setLevel(logging.DEBUG)
    logger.addHandler(handler)
    logfile = logging.FileHandler('lichess_check.log', mode='w')
    logfile.setFormatter(logging.Formatter('%(asctime)s %(message)s'))
    logger.addHandler(logfile)

    report = []
    startups = []
    games = []
    for g in range(args.games):
        engines = []
        for _ in range(2):
            engine, t = start_engine(args.engine)
            engines.append(engine)
            startups.append(t)
        board = chess.Board()
        result, _ = play(engines, board, base, inc, report)
        games.append(board)
        print('game %d: %s in %d plies' % (g + 1, result, len(board.move_stack)),
              flush=True)
        for engine in engines:
            engine.quit()

    # A fresh engine joins a game in progress: lichess-bot restarted.
    source = games[0] if games else chess.Board()
    board = chess.Board()
    for move in source.move_stack[:min(40, len(source.move_stack) - 1)]:
        board.push(move)
    engines = []
    for _ in range(2):
        engine, t = start_engine(args.engine)
        engines.append(engine)
        startups.append(t)
    plies = len(board.move_stack)
    result, first = play(engines, board, base, inc, report,
                         first_moves_free=False)
    print('resumed game from ply %d: %s in %d plies; first moves took '
          '%.1f and %.1f s' % (plies, result, len(board.move_stack),
                                first[0] or 0, first[1] or 0), flush=True)
    for engine in engines:
        engine.quit()

    print('start-up: %.1f-%.1f s (timeout %.0f s)' %
          (min(startups), max(startups), STARTUP_TIMEOUT))
    print('commands sent:', ', '.join('%s %d' % c for c in
                                      sorted(handler.commands.items())))
    for line in report:
        print('PROBLEM:', line)
    for line in handler.problems:
        print('LOGGED:', line)
    ok = not report and not handler.problems
    print('OK' if ok else 'FAILED')
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
