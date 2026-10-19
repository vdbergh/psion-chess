"""Minimal UCI match driver: two engine instances play each other.

usage: python3 -I match.py ENGINE_CMD MAX_PLIES BASE_MS INC_MS
"""
import subprocess
import sys
import time

cmd, max_plies, base, inc = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), int(sys.argv[4])


class Engine:
    def __init__(self, name):
        self.name = name
        self.p = subprocess.Popen(cmd, shell=True, stdin=subprocess.PIPE,
                                  stdout=subprocess.PIPE,
                                  stderr=open(name + '.log', 'w'),
                                  text=True, bufsize=1)

    def send(self, line):
        self.p.stdin.write(line + '\n')
        self.p.stdin.flush()

    def wait_for(self, prefix):
        while True:
            line = self.p.stdout.readline()
            if not line:
                raise RuntimeError(self.name + ' died')
            line = line.strip()
            if line.startswith(prefix):
                return line
            print('  %s> %s' % (self.name, line))


engines = [Engine('white'), Engine('black')]
for e in engines:
    e.send('uci')
    e.wait_for('uciok')
for e in engines:
    e.send('ucinewgame')
    t = time.time()
    e.send('isready')
    e.wait_for('readyok')
    print('%s ready after %.1fs' % (e.name, time.time() - t))

moves = []
clock = [base, base]
for ply in range(max_plies):
    side = ply % 2
    e = engines[side]
    e.send('position startpos moves ' + ' '.join(moves) if moves else 'position startpos')
    t = time.time()
    e.send('go wtime %d btime %d winc %d binc %d' % (clock[0], clock[1], inc, inc))
    best = e.wait_for('bestmove').split()[1]
    used = int((time.time() - t) * 1000)
    clock[side] += inc - used
    print('%3d %-6s %-5s used %5d ms  clock %6d' % (ply + 1, e.name, best, used, clock[side]))
    if best == '0000' or clock[side] < 0:
        print('game over:', 'no move' if best == '0000' else 'time forfeit')
        break
    moves.append(best)
for e in engines:
    e.send('quit')
print('moves:', ' '.join(moves))
