#!/bin/sh
# Does any text overrun the box it is drawn in?
#
# The client measures itself: --audit asks the font, at draw time, how
# wide each string actually is and compares that with the box it is
# about to be drawn into.  It plays a whole game with --auto so the
# check sees the hands and lords it happens to be dealt.
#
# Not something to do by looking at screenshots: what breaks depends on
# the House, the names dealt, the type size and the window, so the
# combination that breaks is rarely the one photographed.  Three type
# sizes against three window shapes.
GODOT=${GODOT:-/Applications/Godot.app/Contents/MacOS/Godot}
cd "$(dirname "$0")/.." || exit 1
[ -x "$GODOT" ] || { echo "  no Godot at $GODOT; skipped"; exit 0; }
bad=0
st=5
for sc in 0.85 1.0 1.35; do
  for r in 1920x1200 1920x1080 1440x900; do
    # Hard timeout per run, and kill the whole process group.
    #
    # A run that hangs used to outlive this script: the shell's timeout
    # killed the wrapper and left Godot spinning at 100% on the user's
    # machine, which is how two of them were found sitting there.  It
    # hung because --here retried a two-second connect sixty times.
    # --stall and --chaos matter as much as --audit here.
    #
    # With --auto alone the client answers every prompt the instant it
    # arrives, so the action list is never painted and _fit("action",
    # ...) is never reached: this test ran nine times and had never
    # once looked at the widest text in the panel.  Shrinking that box
    # to 60px produced no complaint at all, which is how it was found.
    # --stall makes the client sit at a real prompt; --chaos varies
    # which cards and lords it is sitting there holding.
    out=$( { "$GODOT" --path client --resolution "$r" -- \
             --here --audit --auto --stall="$st" --chaos="$st" \
             --scale="$sc" --shot-at=7 \
             --shot=/tmp/fit-shot.png 2>&1 & GP=$!
             ( sleep 90; kill -9 $GP 2>/dev/null ) & W=$!
             wait $GP 2>/dev/null; kill $W 2>/dev/null; } \
           | grep '^OVERFLOW' | sort -u)
    if [ -n "$out" ]; then
      echo "  $sc $r"
      echo "$out" | sed 's/^/    /'
      bad=1
    fi
    st=$((st + 3))
  done
done
[ "$bad" = 0 ] && echo "  no text overruns its box at any size: all clear"
exit $bad
