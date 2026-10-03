#!/bin/sh
# Runs bin/screenshot under a pty, because curses will not draw into a pipe.
#
#   tools/screenshot.sh ROWS COLS SCREEN [OUT]
#   tools/screenshot.sh 24 80 all
#
# With "all" it walks every screen and reports which ones overflow the given
# size or lose the frame's right-hand border. That is ROADMAP 2.1e, which
# stayed open for as long as there was no way to look at a screen without a
# human at a terminal.
# Runs whatever bin/screenshot already is. It does NOT rebuild -- use
# `make shot` if the game has changed since the last build, or this reports on
# a binary that does not contain your change. Cost me a confused five minutes
# once already: the key-hint check "failed" against a stale tool and passed the
# moment it was rebuilt.
set -e
ROWS=${1:-30}
COLS=${2:-100}
WHICH=${3:-armory}
OUT=${4:-}

python3 - "$ROWS" "$COLS" "$WHICH" "$OUT" <<'PY'
import os, pty, select, struct, sys, termios, fcntl, time

rows, cols, which, out = int(sys.argv[1]), int(sys.argv[2]), sys.argv[3], sys.argv[4]
argv = ["./bin/screenshot", str(rows), str(cols), which]
if out:
    argv.append(out)

pid, fd = pty.fork()
if pid == 0:
    os.environ["TERM"] = "xterm-256color"
    os.execv(argv[0], argv)

fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack("HHHH", rows, cols, 0, 0))
buf = b""
t0 = time.time()
while time.time() - t0 < 30:
    r, _, _ = select.select([fd], [], [], 0.3)
    if not r:
        continue
    try:
        d = os.read(fd, 65536)
    except OSError:
        break
    if not d:
        break
    buf += d

_, status = os.waitpid(pid, 0)
sys.stdout.write(buf.decode("utf-8", "replace"))
sys.exit(os.waitstatus_to_exitcode(status) if hasattr(os, "waitstatus_to_exitcode") else 0)
PY
