"""UCI integration tests: drive the real program through the protocol.

usage: python3 -I uci_test.py ENGINE_COMMAND

ENGINE_COMMAND is the program, optionally with a wrapper, e.g.
"wine ../psion.exe". Set PSION_DIR if Psion's files are not in "data"
next to the program.

The engine runs with --verbose; its diagnostics (stderr) are written to
uci_test.log.
"""
import os
import queue
import shlex
import subprocess
import sys
import threading
import time
import unittest

PSION = None
LOG = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'uci_test.log')


class Engine:
    def __init__(self, args=('--verbose',), stderr=None):
        self.log = open(LOG, 'a')
        # Binary, so that "\r\n" line ends stay visible (see _read).
        self.proc = subprocess.Popen(shlex.split(PSION) + list(args),
                                     stdin=subprocess.PIPE,
                                     stdout=subprocess.PIPE,
                                     stderr=stderr or self.log, bufsize=0)
        self.lines = queue.Queue()
        threading.Thread(target=self._read, daemon=True).start()

    def _read(self):
        for raw in self.proc.stdout:
            line = raw.decode('utf-8', 'replace')
            # Lines must end in "\n" only: "\r\n" breaks GUIs that run
            # the Windows engine under Wine.
            if line.endswith('\r\n'):
                line = 'LINE ENDS IN CR: ' + line
            self.lines.put(line.strip())

    def send(self, line):
        self.proc.stdin.write((line + '\n').encode())
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
        self.assertEqual([l for l in e.silent(3) if not l.startswith('info ')],
                         [])
        e.send('isready')       # must be answered during a search
        e.expect('readyok', 1)
        e.send('stop')
        move = bestmove(e, 2)
        self.assertTrue(Board(moves).plausible(move), move)

    def test_info(self):
        """Psion's analysis display is reported as info lines."""
        e = self.engine
        moves = ['g2g4', 'a7a6', 'h2h4', 'h7h6']    # out of Psion's book
        e.send('position startpos moves ' + ' '.join(moves))
        e.send('go movetime 3000')
        line, before = e.expect('bestmove', 15)
        infos = [l.split() for l in before if l.startswith('info ')]
        self.assertTrue(infos, before)
        # The last pv starts with the move played: if Psion did not
        # display its line, "info pv MOVE" says so much.
        self.assertEqual(infos[-1][infos[-1].index('pv') + 1],
                         line.split()[1], infos)
        if infos[-1][:2] == ['info', 'pv']:
            self.assertEqual(len(infos[-1]), 3, infos)
            infos.pop()
        self.assertTrue(infos, before)
        depth = 0
        for info in infos:
            self.assertEqual(info[:2], ['info', 'depth'], info)
            self.assertEqual(info[3], 'time', info)
            self.assertEqual(info[5], 'score', info)
            self.assertIn(info[6], ('cp', 'mate'), info)
            int(info[7])
            self.assertEqual(info[8], 'pv', info)
            pv = info[9:]
            self.assertTrue(pv, info)
            self.assertTrue(Board(moves).plausible(pv[0]), info)
            # the depth: the longest pv so far
            depth = max(depth, len(pv))
            self.assertEqual(int(info[2]), depth, info)
        self.assertTrue(Board(moves).plausible(line.split()[1]), line)

    def test_info_mate(self):
        e = self.engine
        # Legal's mate: 1. Bxf7+ Ke7 2. Nd5#
        moves = ['e2e4', 'e7e5', 'g1f3', 'd7d6', 'f1c4', 'c8g4', 'b1c3',
                 'g7g6', 'f3e5', 'g4d1']
        e.send('position startpos moves ' + ' '.join(moves))
        e.send('go movetime 2000')
        line, before = e.expect('bestmove', 30)
        self.assertEqual(line, 'bestmove c4f7')
        self.assertIn('depth 3 time',
                      ' '.join(l for l in before if l.startswith('info ')))
        self.assertIn('score mate 2 pv c4f7 e8e7 c3d5',
                      ' '.join(l for l in before if l.startswith('info ')))

    def test_locked_pawn_ending(self):
        """Psion used to hang here (at level Infinite): only the kings can
        move, and the search reaches its maximum depth at once."""
        e = self.engine
        moves = (
        'e2e4 c7c6 d2d4 g7g6 b1c3 d7d5 h2h3 h7h5 g1f3 f8g7 c1f4 d5e4 c3e4 '
        'c8f5 e4g3 e7e6 g3f5 e6f5 d1e2 g8e7 e1c1 d8d5 d1e1 g7f6 e2c4 b8d7 '
        'c4d5 c6d5 f1b5 a7a6 b5d3 a8c8 c2c3 e8g8 e1e2 e7c6 d3c2 f8e8 h1e1 '
        'e8e2 e1e2 b7b5 a2a3 d7b6 f3e5 f6e5 f4e5 c8e8 c2d3 c6e5 e2e5 e8e5 '
        'd4e5 b6c4 d3c4 d5c4 c1c2 g6g5 b2b4 f5f4 c2d2 g8g7 g2g4 h5h4 d2e2 '
        'f7f6 e5f6 g7f6 f2f3 f6e5 e2d2 e5d5 d2e2 a6a5 e2d2 a5a4 d2e2 d5d6 '
        'e2d2 d6e6 d2e2 e6d7 e2d2 d7d6 d2e2 d6e6 e2d2 e6e7 d2e2 e7f7 e2d2 '
        'f7g6 d2e2 g6h6 e2d2 h6h7 d2e2 h7g6 e2d1 g6g7 d1e2 g7f8 e2d1 f8e8 '
        'd1d2 e8d7 d2d1 d7c7 d1d2 c7b6 d2e2 b6b7 e2f2 b7c7 f2e1 c7c8 e1d2 '
        'c8b7 d2d1 b7b8 d1e2 b8a8 e2d2 a8a7 d2e1 a7a6 e1d2 ').split()
        e.send('ucinewgame')
        e.send('position startpos moves ' + ' '.join(moves))
        e.send('go movetime 100')
        move = bestmove(e, 150)
        self.assertTrue(Board(moves).plausible(move), move)

    def test_fen_castling_rights(self):
        e = self.engine
        e.send('position fen r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1')
        e.send('go movetime 500')
        line, before = e.expect('bestmove', 5)
        self.assertEqual(line, 'bestmove 0000')
        self.assertIn('info string Castling rights in FEN positions are not'
                      ' supported', before)

    def fen_search(self, fen, timeout=30):
        """Set up the FEN position, search; return the move and the lines
        before it."""
        e = self.engine
        e.send('position fen ' + fen)
        e.send('go movetime 1000')
        line, before = e.expect('bestmove', timeout)
        self.assertNotIn('info string', ' '.join(before))
        return line.split()[1], before

    def test_fen(self):
        # Mate in one, for either side to move: the position must have
        # been set up right.
        self.assertEqual(
            self.fen_search('6k1/5ppp/8/8/8/8/8/R5K1 w - - 0 1')[0], 'a1a8')
        self.assertEqual(
            self.fen_search('r5k1/8/8/8/8/8/5PPP/6K1 b - - 0 1')[0], 'a8a1')
        # Moves after the FEN, then the start position again
        self.assertEqual(self.fen_search(
            '6k1/5ppp/8/8/8/8/8/R5K1 w - - 0 1 moves a1b1 h7h6')[0], 'b1b8')
        self.search(['e2e4'])

    def test_fen_en_passant(self):
        # White has just played d2d4: c4d3 takes en passant.
        move, _ = self.fen_search('4k3/8/8/8/2pP4/8/8/4K3 b - d3 0 1'
                                  ' moves c4d3 e1f2')
        self.assertTrue(move[:2] in ('d3', 'e8') and len(move) >= 4, move)

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

    def test_draw_claim(self):
        # Psion announces a draw (by its own repetition rule) when it plays
        # b2a1 here, flashing the kings; it then ignores '2' until a new
        # game is started. Found in a fastchess match.
        moves = (
        'g1f3 d7d5 c2c4 c7c6 b2b3 g8f6 c1b2 c8f5 g2g3 e7e6 f1g2 h7h6 e1g1 '
        'f8d6 f3d4 f5e4 d2d3 e4g2 g1g2 b8d7 b1d2 d8c7 c4d5 e6d5 d1c2 a8c8 '
        'd4f5 e8g8 a1c1 d6b4 d2f3 f8e8 e2e3 c6c5 a2a3 b4a5 b3b4 a5b6 b4c5 '
        'b6c5 d3d4 c5d6 c2b3 c7b8 b3b5 d7b6 f5d6 b8d6 c1c8 e8c8 f3e5 c8c2 '
        'f1c1 d6c7 c1c2 c7c2 b2a1 c2e4 g2g1 e4c2 g1g2 c2c1 a1b2 c1c2').split()
        self.search(moves, 'go wtime 35891 btime 34794 winc 1000 binc 1000',
                    timeout=120)
        self.engine.send('ucinewgame')
        self.search(['c2c4'], timeout=30)

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

    def test_quiet(self):
        """Without --verbose nothing but UCI is written, until "debug on"."""
        e = Engine(args=(), stderr=subprocess.PIPE)
        e.send('uci')
        e.expect('uciok', 10)
        e.send('position startpos moves e2e4')
        e.send('go movetime 300')
        bestmove(e, 15)
        e.send('debug on')
        e.send('position startpos moves e2e4 e7e5')
        e.send('go movetime 300')
        bestmove(e, 15)
        e.send('quit')
        self.assertEqual(e.proc.wait(10), 0)
        err = e.proc.stderr.read().decode('utf-8', 'replace')
        e.proc.stderr.close()
        e.close()
        # Only the second search was reported: not the first move.
        self.assertNotIn('Ply 1,', err)
        self.assertIn('e2e4 e7e5', err)
        self.assertNotIn('DOSBox', err)

    def test_slow_emulation(self):
        """At about the speed of an IBM PC (300 instructions per ms), the
        states Psion passes through while it plays a move, such as a piece
        lifted, last long enough to look stable. They must not be taken
        for positions: the moves would then no longer follow."""
        e = Engine(args=('--verbose', '--cycles', '300'))
        try:
            e.send('uci')
            e.expect('uciok', 10)
            moves = ['e2e4', 'e7e5', 'g1f3', 'b8c6']
            e.send('position startpos moves ' + ' '.join(moves[:2]))
            e.ready()
            # The moves are entered one at a time, as during a game.
            e.send('position startpos moves ' + ' '.join(moves[:3]))
            e.ready()
            e.send('position startpos moves ' + ' '.join(moves))
            e.send('go movetime 500')
            line, before = e.expect('bestmove', 60)
            self.assertFalse([l for l in before if 'does not accept' in l],
                             before)
            self.assertTrue(Board(moves).plausible(line.split()[1]), line)
        finally:
            e.close()


if __name__ == '__main__':
    PSION = sys.argv.pop(1)
    if os.path.exists(LOG):
        os.remove(LOG)
    unittest.main(verbosity=2)
