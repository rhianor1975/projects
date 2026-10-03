#!/bin/sh
# Launched by ttyd once per browser connection (see tools/serve.sh).
#
# Every session runs as the same OS user, so without this they would all share
# one save slot and quietly overwrite each other's suspended run. The player
# names themselves in the URL:
#
#   http://host:7681                        -> the shared "guest" pool
#   http://host:7681?arg=kevin              -> kevin's own saves and records
#   http://host:7681?arg=kevin&arg=44815    -> ...on run seed 44815
#
# ttyd passes each query argument through in order, after the binary in $1.
# The second one, if present, is a run seed: the same dungeon for everyone
# who opens that link, which is the point of handing a link to someone.
set -eu

BIN=$1
NAME=${2:-guest}
SEED=${3:-}

# The name arrives from a URL, so it is untrusted: strip it to a short
# alphanumeric slug. That is what stops "?arg=../../.ssh" being a directory
# rather than a player.
NAME=$(printf '%s' "$NAME" | tr -cd 'A-Za-z0-9_-' | cut -c1-24)
[ -n "$NAME" ] || NAME=guest

AETHER_STATE_DIR="$HOME/.aether-descent/$NAME"
export AETHER_STATE_DIR
mkdir -p "$AETHER_STATE_DIR"

# Same treatment as the name: it comes from a URL, so it is digits or it is
# nothing. Anything else and the seed is simply dropped rather than reaching
# the command line.
SEED=$(printf '%s' "$SEED" | tr -cd '0-9' | cut -c1-10)

if [ -n "$SEED" ]; then
    exec "$BIN" --seed "$SEED"
else
    exec "$BIN"
fi
