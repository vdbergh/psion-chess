"""Search memory dumps of known plies for side-to-move flags and
ply/move counters.

usage: python3 -I find_state.py DS_SEGMENT PLY:mem_file PLY:mem_file ...
"""
import sys

ds = int(sys.argv[1], 16) * 16
plies, dumps = [], []
for arg in sys.argv[2:]:
    ply, path = arg.split(':', 1)
    plies.append(int(ply))
    dumps.append(open(path, 'rb').read())
size = min(len(d) for d in dumps)


def loc(a):
    if ds <= a < ds + 0x10000:
        return '0x%05X (DS:%04X)' % (a, a - ds)
    return '0x%05X' % a


def linear(xs, ys, mod):
    """Return (base, step) if ys == base + step*xs (mod), small step."""
    for step in list(range(1, 16)) + list(range(-15, 0)):
        base = (ys[0] - step * xs[0]) % mod
        if all((base + step * x) % mod == y for x, y in zip(xs, ys)):
            return base, step
    return None


print('== side to move: value depends only on ply parity')
for a in range(size):
    s = [d[a] for d in dumps]
    white = {v for p, v in zip(plies, s) if p % 2 == 0}
    black = {v for p, v in zip(plies, s) if p % 2 == 1}
    if len(white) == 1 and len(black) == 1 and white != black:
        print('  %s  white=%02X black=%02X' % (loc(a), min(white), min(black)))

moves = [p // 2 for p in plies]
for width in (1, 2):
    mod = 1 << (8 * width)
    print('== %s counters' % ('byte' if width == 1 else 'word'))
    for a in range(size - width + 1):
        s = [int.from_bytes(d[a:a + width], 'little') for d in dumps]
        for name, xs in (('ply', plies), ('full move', moves)):
            fit = linear(xs, s, mod)
            if fit:
                print('  %s  %s: value = 0x%X %+d*%s  %s'
                      % (loc(a), name, fit[0], fit[1], name, s))
                break
