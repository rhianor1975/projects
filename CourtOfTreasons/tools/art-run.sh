#!/bin/sh
# Queue every deck's art, one deck at a time, and keep going if one fails.
#
# Deck by deck rather than one --all run so the log says which deck is
# in flight and a failure costs one deck's queue and not the night.
# Detach it: this is days of rendering.
#
#   nohup ./tools/art-run.sh > logs/art.log 2>&1 &
cd "$(dirname "$0")/.." || exit 1
# The four new Houses come FIRST.  Everything before them has art or
# is queued for it, and a House with no faces is the one a player
# actually notices -- a deck of named empty frames.
DECKS="Leoward Stonegarth Wulfren Everhold
       Ravenmark Vipren Goldwyn Aldemar War Combat Political Intrigue
       Council World Promise Lever Ambition Throne Court Ghost
       Resurrection Instigator Cataclysm"
for d in $DECKS; do
    echo "=== $d  $(date '+%H:%M:%S')"
    python3 gen-art.py --deck "$d" </dev/null || echo "!!! $d failed, continuing"
done
echo "=== sweep for anything missed  $(date '+%H:%M:%S')"
python3 gen-art.py --all </dev/null
echo "=== done  $(date '+%H:%M:%S')"
