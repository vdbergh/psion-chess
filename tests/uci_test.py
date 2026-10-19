"""UCI integration tests: drive the real program through the protocol.

usage: PSION_DIR=<data dir> python3 -I uci_test.py PATH_TO_PSION

Engine diagnostics (stderr) are written to uci_test.log.
"""
import os
import queue
import subprocess
import sys
import threading
import time
import unittest

PSION = None
LOG = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'uci_test.log')


class Engine:
    def __init__(self):
        self.log = open(LOG, 'a')
        self.proc = subprocess.Popen([PSION], stdin=subprocess.PIPE,
                                     stdout=subprocess.PIPE, stderr=self.log,
                                     text=True, bufsize=1)
        self.lines = queue.Queue()
        threading.Thread(target=self._read, daemon=True).start()

    def _read(self):
        for line in self.proc.stdout:
            self.lines.put(line.strip())

    def send(self, line):
        self.proc.stdin.write(line + '\n')
        self.proc.stdin.flush()

    def expect(self, prefix, timeout):
        """Return the first line starting with prefix; the lines before
        it are returned too."""
        seen = []
        end = time.time() + timeout
        while True:
            try:
                line = self.lines.get(timeout=max(end - time.time(), 0.01))
            except queue.Empty:
                raise AssertionError('no %r within %ss, got %r'
                                     % (prefix, timeout, seen))
            if line.startswith(prefix):
                return line, seen
            seen.append(line)

    def silent(self, duration):
        """Return the lines output during duration seconds."""
        seen = []
        end = time.time() + duration
        while time.time() < end:
            try:
                seen.append(self.lines.get(timeout=end - time.time()))
            except queue.Empty:
                pass
        return seen

    def ready(self, timeout=60):
        self.send('isready')
        self.expect('readyok', timeout)

    def close(self):
        if self.proc.poll() is None:
            self.send('quit')
            try:
                self.proc.wait(10)
            except subprocess.TimeoutExpired:
                self.proc.kill()
        self.proc.stdin.close()
        self.proc.stdout.close()
        self.log.close()


class Board:
    """Just enough chess to replay a UCI move list (no legality checks)."""

    def __init__(self, moves=()):
        self.sq = {}
        for f, piece in enumerate('RNBQKBNR'):
            self.sq[(f, 0)] = piece
            self.sq[(f, 1)] = 'P'
            self.sq[(f, 6)] = 'p'
            self.sq[(f, 7)] = piece.lower()
        self.white = True
        for m in moves:
            self.play(m)

    @staticmethod
    def parse(m):
        return ((ord(m[0]) - 97, int(m[1]) - 1),
                (ord(m[2]) - 97, int(m[3]) - 1))

    def own(self, square):
        p = self.sq.get(square)
        return p is not None and p.isupper() == self.white

    def play(self, m):
        frm, to = self.parse(m)
        piece = self.sq.pop(frm)
        if piece in 'Kk' and abs(to[0] - frm[0]) == 2:
            rook_from = (7 if to[0] == 6 else 0, frm[1])
            rook_to = (5 if to[0] == 6 else 3, frm[1])
            self.sq[rook_to] = self.sq.pop(rook_from)
        if piece in 'Pp' and to[0] != frm[0] and to not in self.sq:
            del self.sq[(to[0], frm[1])]
        if len(m) == 5:
            piece = m[4].upper() if self.white else m[4]
        self.sq[to] = piece
        self.white = not self.white

    def plausible(self, m):
        """The move takes an own piece to a square without an own piece."""
        if len(m) not in (4, 5) or m == '0000':
            return False
        frm, to = self.parse(m)
        return self.own(frm) and not self.own(to)


def bestmove(engine, timeout):
    line, _ = engine.expect('bestmove', timeout)
    return line.split()[1]


class UciTest(unittest.TestCase):

    @classmethod
    def setUpClass(cls):
        cls.engine = Engine()
        cls.engine.send('uci')
        cls.engine.expect('uciok', 10)
        cls.engine.ready()

    @classmethod
    def tearDownClass(cls):
        cls.engine.close()

    def search(self, moves, go='go movetime 500', timeout=15):
        """Set up the position, search, check the move, return it."""
        e = self.engine
        e.send('position startpos moves ' + ' '.join(moves)
               if moves else 'position startpos')
        e.send(go)
        move = bestmove(e, timeout)
        self.assertTrue(Board(moves).plausible(move),
                        '%s after %s' % (move, ' '.join(moves)))
        return move

    def test_handshake(self):
        e = self.engine
        e.send('uci')
        _, before = e.expect('uciok', 10)
        self.assertIn('id name Psion Chess 1985', before)
        self.assertTrue(any(l.startswith('id author') for l in before))

    def test_movetime(self):
        moves = ['a2a3', 'h7h6', 'h2h3', 'a7a6']   # out of book
        self.engine.send('position startpos moves ' + ' '.join(moves))
        self.engine.ready()
        start = time.time()
        self.engine.send('go movetime 2000')
        move = bestmove(self.engine, 10)
        used = time.time() - start
        self.assertTrue(Board(moves).plausible(move), move)
        self.assertGreater(used, 1.0)
        self.assertLess(used, 2.5)

    def test_clock(self):
        moves = ['b2b3', 'g7g6', 'g2g3', 'b7b6']
        self.engine.send('position startpos moves ' + ' '.join(moves))
        self.engine.ready()
        start = time.time()
        self.engine.send('go wtime 30000 btime 30000')
        move = bestmove(self.engine, 10)
        used = time.time() - start
        self.assertTrue(Board(moves).plausible(move), move)
        self.assertLess(used, 30000 / 30 / 1000 + 0.5)

    def test_infinite_and_stop(self):
        e = self.engine
        moves = ['a2a4', 'h7h5', 'h2h4', 'a7a5']
        e.send('position startpos moves ' + ' '.join(moves))
        e.send('go infinite')
        self.assertEqual(e.silent(3), [])
        e.send('isready')       # must be answered during a search
        e.expect('readyok', 1)
        e.send('stop')
        move = bestmove(e, 2)
        self.assertTrue(Board(moves).plausible(move), move)

    def test_unsupported_fen(self):
        e = self.engine
        e.send('position fen 8/8/8/4k3/8/8/4K3/8 w - - 0 1')
        e.send('go movetime 500')
        line, before = e.expect('bestmove', 5)
        self.assertEqual(line, 'bestmove 0000')
        self.assertIn('info string FEN positions are not supported', before)

    def test_start_fen(self):
        e = self.engine
        e.send('position fen rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR'
               ' w KQkq - 0 1 moves d2d4')
        e.send('go movetime 500')
        move = bestmove(e, 15)
        self.assertTrue(Board(['d2d4']).plausible(move), move)

    def test_invalid_move(self):
        e = self.engine
        e.send('position startpos moves e2e4 nonsense')
        e.send('go movetime 500')
        line, before = e.expect('bestmove', 5)
        self.assertEqual(line, 'bestmove 0000')
        self.assertIn('info string Invalid move nonsense', before)

    def test_takeback_and_new_game(self):
        line = ['e2e4', 'e7e5', 'g1f3', 'b8c6', 'f1b5', 'a7a6']
        self.search(line)
        self.search(line[:4])                 # takes back
        self.search(['d2d4', 'd7d5', 'c2c4'])  # diverges: new game
        self.engine.send('ucinewgame')
        self.search([])

    def test_en_passant_and_castling(self):
        self.search([
            'e2e4', 'd7d5', 'e4e5', 'f7f5', 'e5f6', 'g7f6',   # en passant
            'g1f3', 'b8c6', 'f1e2', 'c8e6', 'e1g1', 'd8d7',   # O-O
            'd2d4', 'e8c8'], timeout=60)                      # O-O-O

    def test_promotion(self):
        self.search([
            'h2h4', 'g7g5', 'h4g5', 'h7h6', 'g5h6', 'g8f6', 'h6h7', 'h8g8',
            'h7g8n', 'f6g8', 'a2a4', 'b7b5', 'a4b5', 'a7a6', 'b5a6', 'c8b7',
            'a6a7', 'b7c6', 'a7b8q'], timeout=60)

    def test_self_play(self):
        moves = []
        self.engine.send('ucinewgame')
        for _ in range(12):
            moves.append(self.search(moves, 'go wtime 20000 btime 20000'))


class QuitTest(unittest.TestCase):

    def test_quit(self):
        e = Engine()
        e.send('uci')
        e.expect('uciok', 10)
        e.send('quit')
        self.assertEqual(e.proc.wait(10), 0)
        e.close()


if __name__ == '__main__':
    PSION = sys.argv.pop(1)
    if os.path.exists(LOG):
        os.remove(LOG)
    unittest.main(verbosity=2)
