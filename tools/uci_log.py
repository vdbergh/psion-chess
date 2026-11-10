#!/usr/bin/env python3
"""Runs the engine with --verbose and logs its UCI input (">"), output
("<") and diagnostics ("!"), timestamped, to logs/<pid>.log in the
current directory. The GUI sees only the UCI output. Meant for matches,
to see afterwards why an engine instance misbehaved.

usage: uci_log.py ENGINE [ARGS...]
e.g. fastchess -engine cmd=tools/uci_log.py args=./psion ...
"""
import os
import signal
import subprocess
import sys
import threading
import time


def main():
    os.makedirs('logs', exist_ok=True)
    log = open(os.path.join('logs', '%d.log' % os.getpid()), 'a', buffering=1)
    lock = threading.Lock()

    def note(tag, line):
        t = time.time()
        stamp = time.strftime('%H:%M:%S', time.localtime(t))
        with lock:
            log.write('%s.%03d %s %s\n' % (stamp, int(t % 1 * 1000), tag,
                                          line.rstrip('\n')))

    child = subprocess.Popen(sys.argv[1:] + ['--verbose'],
                             stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                             stderr=subprocess.PIPE, text=True, bufsize=1)
    assert child.stdin and child.stdout and child.stderr
    to_child, from_child, diag = child.stdin, child.stdout, child.stderr

    def pump_in():
        try:
            for line in sys.stdin:
                note('>', line)
                to_child.write(line)
                to_child.flush()
            to_child.close()
        except (BrokenPipeError, ValueError):
            pass

    def pump_out():
        for line in from_child:
            note('<', line)
            try:
                sys.stdout.write(line)
                sys.stdout.flush()
            except BrokenPipeError:
                break

    def pump_err():
        for line in diag:
            note('!', line)

    # fastchess may stop the wrapper with a signal: pass it on.
    for sig in (signal.SIGTERM, signal.SIGINT, signal.SIGHUP):
        signal.signal(sig, lambda sig, _: child.send_signal(sig))
    threading.Thread(target=pump_in, daemon=True).start()
    readers = [threading.Thread(target=f) for f in (pump_out, pump_err)]
    for r in readers:
        r.start()
    code = child.wait()
    for r in readers:
        r.join()
    note('!', 'exit %d' % code)
    sys.exit(code)


if __name__ == '__main__':
    main()
