#!/bin/sh
# tools/sheet.sh [DECK] [OUT]
#
# Tile finished cards into one sheet, because a deck has to be judged as a
# deck.  Twelve cards that are each fine and do not sit together is the
# failure mode a per-card look will not catch -- and since the frame, the
# cost badges and the flavour are part of what has to sit together, the
# sheet composes whole cards rather than tiling bare art.
#
#   tools/sheet.sh              everything drawn so far
#   tools/sheet.sh Vipren       one deck
set -e
cd "$(dirname "$0")/.."
DECK=$1
OUT=${2:-sheet.png}

[ -d art ] || { echo "nothing in art/ yet"; exit 1; }

# This ImageMagick has no font configuration of its own, so the font is
# named by path.  Spectral because a card label wants a serif and it is
# the one that ships on every Mac this will run on.
FONTDIR=$(cd "$(dirname "$0")/../assets/fonts" && pwd)
FONT=$FONTDIR/Spectral-Regular.ttf
[ -f "$FONT" ] || FONT=/System/Library/Fonts/Helvetica.ttc

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
n=0
# The label is the card's own name from CARDS.tsv, so a sheet cannot drift
# out of step with the table the way a hand-kept caption list would.
while IFS="$(printf '\t')" read -r deck id name rest; do
  [ "$id" = "id" ] && continue
  [ -n "$DECK" ] && [ "$deck" != "$DECK" ] && continue
  [ -f "art/$id.webp" ] || continue
  ./tools/card.sh "$id" "$TMP/$(printf '%03d' $n)_$id.png" >/dev/null
  printf '%s\n' "$name" > "$TMP/$(printf '%03d' $n)_$id.txt"
  n=$((n + 1))
done < CARDS.tsv

[ "$n" -gt 0 ] || { echo "no images for ${DECK:-any deck} yet"; exit 1; }

set -- 
for f in "$TMP"/*.png; do
  set -- "$@" -label "$(cat "${f%.png}.txt")" "$f"
done

montage "$@" \
  -tile 4x -geometry 300x420+8+8 -background '#15120f' \
  -font "$FONT" -fill '#d8c9a8' -pointsize 15 "$OUT"
echo "$OUT  --  $n cards${DECK:+ from $DECK}"
