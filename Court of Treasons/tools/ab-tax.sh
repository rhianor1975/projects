#!/bin/sh
# tools/ab-tax.sh [N]
#
# <open_questions> Q5: "Whether the Lord Paramount's tax should take
# Standing, as written, or Levy.  Standing is the stronger catch-up and
# the more painful; it may be too strong."
#
# Three ranges of seeds, never one, because that is what the engine
# contract says a change is judged by.
set -e
N=${1:-400}
cd "$(dirname "$0")/.."
for FIRST in 1 5000 90000; do
  for MODE in standing levy; do
    i=$FIRST; last=$((FIRST + N - 1)); tot=0; lp1=0; spread=""
    OUT=$(mktemp)
    while [ $i -le $last ]; do
      COURT_TAX=$MODE COURT_SEED=$i COURT_HOUSES=4 ./court >> "$OUT"
      i=$((i + 1))
    done
    awk -v m="$MODE" -v f="$FIRST" -v n="$N" '
    { for (j=1;j<=NF;j++){split($j,kv,"="); v[kv[1]]=kv[2]}
      lp1 += v["lp1won"]; win[v["winner"]]++; rounds += v["rounds"]
      grv += v["grievance"]; last += (v["by"]=="The_Last_House")
      brk += v["broken"]; made += v["promises"] }
    END { lo=1e9; hi=0
      for (k in win) if (k != "none") { if (win[k]<lo) lo=win[k]; if (win[k]>hi) hi=win[k] }
      printf "  seeds %-6s  %-8s  lp1-wins %5.1f%%   spread %5.2fx   " \
             "rounds %4.1f   grievance %4.2f   all-dead %5.1f%%   broken %4.1f%%\n",
             f, m, 100*lp1/n, (lo>0?hi/lo:99), rounds/n, grv/n, 100*last/n,
             (made? 100*brk/made : 0) }
    ' "$OUT"
    rm -f "$OUT"
  done
  echo
done
