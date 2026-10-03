#!/bin/sh
# Serves Aether Descent to a web browser on the local network.
#
# ttyd hands each browser connection a real pty running the game and streams
# it over a websocket to xterm.js. A real pty matters: it answers the
# capability queries the notcurses build blocks on at startup, so the
# graphical render modes work here where they don't under a pipe.
#
#   make serve          -- the ncurses build (safe default, works anywhere)
#   make serve-nc       -- the notcurses build, for block/tile graphics
#
# Binds to the LAN, not the internet. Anything on your wifi can reach it;
# nothing outside can unless you forward a port on the router, which this
# does not do.
set -eu

BIN=${1:-bin/aether-descent}
PORT=${PORT:-7681}

[ -x "$BIN" ] || { echo "no $BIN -- run 'make' (or 'make notcurses') first" >&2; exit 1; }
command -v ttyd >/dev/null || { echo "ttyd not installed -- 'brew install ttyd'" >&2; exit 1; }

# First non-loopback IPv4, for the "open this on his tablet" line.
LAN=$(ipconfig getifaddr en0 2>/dev/null || ipconfig getifaddr en1 2>/dev/null || echo 127.0.0.1)

cat <<MSG

  Aether Descent is being served.

    On this Mac        http://localhost:$PORT
    On another device  http://$LAN:$PORT

  Every browser that connects gets its own game process.

  Saves are per player, and the player names themselves in the URL:
    http://$LAN:$PORT?arg=kevin   -- kevin's own save and records
    http://$LAN:$PORT             -- the shared "guest" pool
  Without a name everyone shares one save slot and overwrites each other.

  Add a run seed to hand someone the same dungeon you played:
    http://$LAN:$PORT?arg=kevin&arg=44815
  The seed of a run in progress is on its character sheet.

  Ctrl-C here stops the server.

MSG

# -W        : let the browser send keystrokes (it's a game, not a log viewer)
# -t ...    : big scrollback isn't useful for a full-screen TUI; a readable
#             font and a dark background are
# -a        : pass ?arg=... through to play.sh as the player name. It is
#             untrusted input from a URL, which is why play.sh sanitises it
#             to an alphanumeric slug before it becomes a directory.
exec ttyd -W -a -p "$PORT" \
    -t 'fontSize=16' \
    -t 'theme={"background":"#101014"}' \
    -t 'scrollback=0' \
    -t 'cursorStyle=bar' \
    tools/play.sh "$BIN"
