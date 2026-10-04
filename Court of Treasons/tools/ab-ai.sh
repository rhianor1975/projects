#!/bin/sh
# tools/ab-ai.sh [N] [FIRST...]
#
# Two AIs at the same table, and which one wins.
#
# A soak cannot answer this.  A soak is self-play: every change reaches
# both seats and any strength difference cancels, which is why a change
# that makes the machine measurably worse can still leave the win spread
# where it was.  COURT_AI_RESERVE is a bitmask, one bit per seat, so one
# binary can sit the AI with the change against the AI without it.
#
# The variant plays BOTH seats in turn over the same seeds -- mask 1,
# then mask 2 -- because the seats are not equal (seat 0 moves first)
# and the Houses are dealt, so a single assignment measures the chair
# as much as the player.
set -e
N=${1:-300}; shift || true
RANGES=${*:-"1 1001 5001"}
cd "$(dirname "$0")/.."
tot=0; won=0
for first in $RANGES; do
  rw=0; rt=0
  for mask in 1 2; do
    seat=$((mask - 1))
    i=$first; last=$((first + N - 1))
    while [ $i -le $last ]; do
      line=$(COURT_SEED=$i COURT_SEATS=1 COURT_AI_RESERVE=$mask ./court)
      w=$(printf '%s\n' "$line" | sed -n 's/.*winner=\([A-Za-z_]*\).*/\1/p' | head -1)
      h=$(printf '%s\n' "$line" | sed -n "s/.*seat${seat}=\([A-Za-z_]*\).*/\1/p" | head -1)
      rt=$((rt + 1))
      [ -n "$w" ] && [ "$w" = "$h" ] && rw=$((rw + 1))
      i=$((i + 1))
    done
  done
  tot=$((tot + rt)); won=$((won + rw))
  awk -v w="$rw" -v t="$rt" -v f="$first" 'BEGIN{
    printf "  seeds %-6s reserve won %4d of %4d  %5.1f%%\n", f, w, t, 100*w/t }'
done
awk -v w="$won" -v t="$tot" 'BEGIN{
  p = w/t; se = sqrt(p*(1-p)/t)*100
  printf "\n  overall      reserve won %4d of %4d  %5.1f%%  (+-%.1f, 1 s.e.)\n", w, t, 100*p, se
  d = 100*p - 50
  if (d > 2*se)       printf "  the reserve is stronger: %+.1f points, over two standard errors\n", d
  else if (-d > 2*se) printf "  the reserve is WEAKER: %+.1f points, over two standard errors\n", d
  else                printf "  no difference the test can see: %+.1f points against +-%.1f\n", d, se }'
