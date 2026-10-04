#!/bin/sh
# tools/soak.sh N FIRST [HOUSES]
#
# Play a batch and print the five measures <engine_contract> names, each
# with the threshold the design sets for it.  A measure without its
# threshold printed beside it is a number nobody acts on, which is how
# Grievance sat pinned at 10 next door for the whole of a project.
set -e
N=${1:-500}; FIRST=${2:-1}; H=2
cd "$(dirname "$0")/.."
OUT=$(mktemp)
i=$FIRST; last=$((FIRST + N - 1))
while [ $i -le $last ]; do
  COURT_SEED=$i COURT_SEATS=1 ./court >> "$OUT"
  i=$((i + 1))
done

awk -v n="$N" -v h="$H" -v tax="${COURT_TAX:-standing}" '
{
  # v persists across lines in awk, so it is cleared: the per-seat lines
  # COURT_SEATS adds would otherwise be counted as games with whatever
  # winner the previous line had.
  delete v
  for (j = 1; j <= NF; j++) { split($j, kv, "="); v[kv[1]] = kv[2] }
  # A per-seat line.  Houses are drawn from four into two chairs, so they
  # do not appear equally often -- 164 tables to 136 over the same 300
  # seeds -- and a win RATE has to divide by the tables a House actually
  # sat at.  Dividing raw win counts made Vipren read 6.0% when it was
  # winning 14.7% of its own games, and made the win spread a number
  # about the shuffle as much as about the Houses.
  if ("seat0" in v) { appear[v["seat0"]]++; next }
  if ("seat1" in v) { appear[v["seat1"]]++; next }
  plays += v["plays"]; inert += v["inert"]
  made  += v["promises"]; kept += v["kept"]; broke += v["broken"]
  grv   += v["grievance"]; rounds += v["rounds"]
  win[v["winner"]]++; by[v["by"]]++
  lp1won += v["lpwon"]; lpheld += (v["lpheld"] != "none"); unspent += v["unspent"]
}
END {
  printf "\n  %d games, %d Houses, %.1f rounds average\n", n, h, rounds/n
  # Which tax was in force decides most of these numbers, so it is printed
  # beside them.  Standing is what design.xml says and it produces a
  # runaway; Levy is what <open_questions> Q5 asks about and what the
  # measurements recommend.  Quoting a figure without saying which was
  # running is how the good number and the default configuration come to
  # be different things nobody notices.
  printf "  a duel; the tax takes Levy, the Council elects the Paramount\n\n"

  ip = plays ? 100*inert/plays : 0
  printf "  inert plays      %5.1f%%   (next door it was 53%%; the most\n", ip
  printf "                            honest number the engine produces)\n"

  pb = made ? 100*broke/made : 0
  printf "  promises broken  %5.1f%%   (should sit near 10%%: near zero\n", pb
  printf "                            means priced from where the liar\n"
  printf "                            stands, near half means nothing is\n"
  printf "                            holding them)\n"
  printf "  promises made    %5.2f    per game\n", made/n

  printf "  grievance at end %5.1f     (must not sit pinned at 10)\n", grv/n

  printf "\n  wins   (of the games that House was dealt into, not of all)\n"
  for (k in win)
    if (k in appear)
      printf "    %-12s %4d of %3d  %5.1f%%\n", k, win[k], appear[k],
             100*win[k]/appear[k]
    else
      printf "    %-12s %4d          %5.1f%%\n", k, win[k], 100*win[k]/n
  printf "\n  by\n"
  for (k in by) printf "    %-22s %4d  %5.1f%%\n", k, by[k], 100*by[k]/n

  printf "  levy unspent     %5.1f%%   (Houses routinely ending turns with\n", 100*unspent/n
  printf "                            Levy in hand means the costs are too\n"
  printf "                            low and there is no curve)\n"
  printf "\n  holding the Paramountcy longer won %5.1f%% of games\n", 100*lp1won/lpheld
  printf "  (chance is 50%%; a large gap means the tax compounds, which is\n"
  printf "   what electing the title rather than accruing it is meant to stop)\n"

  lo = 1e9; hi = 0
  for (k in win) if (k != "none" && (k in appear)) {
    rate = win[k] / appear[k]
    if (rate < lo) lo = rate
    if (rate > hi) hi = rate
  }
  if (lo > 0 && lo < 1e9)
    printf "\n  win spread       %5.2fx    (no House should win more than\n                            about a third more often than the worst)\n", hi/lo
}
' "$OUT"
rm -f "$OUT"
