#!/bin/sh
# tools/mkassets.sh
#
# Compose every card the art exists for into client/assets/cards/, and
# copy the backs and the card table beside them.
#
# The client reads the same CARDS.tsv the engine compiled from, so a card
# the engine knows and the client does not shows up as a named empty
# frame rather than as a blank rectangle with no explanation.
#
# Faces are written at 512 wide, which is what card.sh makes, and scaled
# down by the client.  A 1024 face would look no better on a 140-pixel
# hand card and would quadruple an asset folder that is already the
# largest thing here.
set -e
cd "$(dirname "$0")/.."
OUT=client/assets
mkdir -p "$OUT/cards" "$OUT/backs"

cp CARDS.tsv "$OUT/cards.tsv"
for b in art/backs/*.webp; do
  case "$b" in *-unshielded.webp) continue ;; esac
  cp "$b" "$OUT/backs/"
done 2>/dev/null || true

n=0; had=0
while IFS="$(printf '\t')" read -r deck id name rest; do
  [ "$id" = "id" ] && continue
  [ -f "art/$id.webp" ] || continue
  had=$((had + 1))
  # Only recompose when the art is newer than the face, so a rerun after
  # twenty new cards costs twenty seconds and not four minutes.
  if [ ! -f "$OUT/cards/$id.webp" ] || [ "art/$id.webp" -nt "$OUT/cards/$id.webp" ]; then
    # </dev/null matters: the loop is reading CARDS.tsv on stdin, and a
    # child that reads stdin eats the rest of the card list.  Without it
    # this composed one card and then failed on a half-line.
    # Composed as PNG, stored as WebP: a face is a tenth the size and
    # the client reads either.  See tools/webp.py.
    ./tools/card.sh "$id" "$OUT/cards/$id.png" >/dev/null 2>&1 </dev/null
    python3 tools/webp.py "$OUT/cards/$id.png" 2>/dev/null </dev/null
    n=$((n + 1))
  fi
done < CARDS.tsv

# Small faces for the table.
#
# A card in hand is about 150 pixels wide and the composed face is 512:
# four times the pixels, every one of which has to be decoded at play
# time because these are generated after the editor last imported and
# so cannot be .ctex.  The big face is still there and is still what the
# reading copy uses -- that is one card at a time, on hover.
if [ "$n" -gt 0 ] || [ ! -d "$OUT/thumbs" ]; then
  mkdir -p "$OUT/thumbs"
  IM=$(command -v magick || command -v convert)
  [ -n "$IM" ] && "$IM" mogrify -path "$OUT/thumbs" -resize 256x -quality 85 \
    "$OUT/cards"/*.webp 2>/dev/null || true
  echo "  $(ls "$OUT/thumbs" 2>/dev/null | wc -l | tr -d ' ') small faces"
fi

echo "  $had cards have art, $n composed this run"
du -sh "$OUT" | awk '{print "  assets:", $1}'
