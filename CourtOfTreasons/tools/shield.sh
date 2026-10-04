#!/bin/sh
# tools/shield.sh NAME
#
# Force a back's art into a heraldic shield.
#
#   tools/shield.sh aldemar
#
# NOT USED, and kept only as a record of a wrong turn.  Next to four
# painted shields with carved bosses and relief, a masked crop with a
# flat gold outline reads as exactly what it is: a cut-out.  The shape
# came out right and the card came out worse.
#
# The actual fix was the negative prompt.  Three attempts went into
# rewording the positive one -- 'shield-shaped', 'pointed escutcheon',
# dropping the laurel -- when the tool for excluding a shape is the field
# whose entire job is excluding things, and 'roundel, circle, oval,
# wreath' in the negative got a shield on the next run.
#
# For when the model will not draw the shape.  Aldemar's stag came back a
# roundel three times: once from "a single heraldic shield bearing one
# stag", once from "shield-shaped" said outright, and once more with the
# laurel dropped on the theory that a wreath was pulling it round.  Three
# attempts is enough to conclude it is not going to, and the shape of a
# shield is not something that needs a model anyway -- it is six points
# and two curves, and masking is exact where asking is not.
#
# Writes art/backs/NAME.png, keeping the original as NAME-unshielded.png.
set -e
cd "$(dirname "$0")/.."
N=$1
[ -n "$N" ] || { echo "usage: tools/shield.sh NAME"; exit 1; }
SRC="art/backs/$N.png"
[ -f "$SRC" ] || { echo "no back art for $N"; exit 1; }

IM=$(command -v magick || command -v convert)
S=512
TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT

# A heater shield: square shoulders, straight flanks, and a base that
# curves to a point.  Drawn twice, the second a little larger, so the
# difference between them is the gilt rim.
cat > "$TMP/shield.svg" <<EOF
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 100 118" width="400" height="472">
  <path d="M 4,4 L 96,4 L 96,54 Q 96,90 50,114 Q 4,90 4,54 Z" fill="#fff"/>
</svg>
EOF
cat > "$TMP/rim.svg" <<EOF
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 100 118" width="432" height="510">
  <path d="M 4,4 L 96,4 L 96,54 Q 96,90 50,114 Q 4,90 4,54 Z" fill="#fff"/>
</svg>
EOF
RSVG=$(command -v rsvg-convert)
if [ -n "$RSVG" ]; then
  "$RSVG" -w 400 -h 472 "$TMP/shield.svg" -o "$TMP/mask.png"
  "$RSVG" -w 432 -h 510 "$TMP/rim.svg"    -o "$TMP/rim.png"
else
  "$IM" -background none "$TMP/shield.svg" -resize 400x472 "$TMP/mask.png"
  "$IM" -background none "$TMP/rim.svg"    -resize 432x510 "$TMP/rim.png"
fi

# The ground keeps the art's own colour: the same picture, blurred past
# recognition and taken down, so the shield sits on its own world rather
# than on a swatch.
"$IM" "$SRC" -resize ${S}x${S}^ -gravity center -extent ${S}x${S} \
  -blur 0x22 -modulate 58,70,100 "$TMP/ground.png"

# The charge, scaled past the shield before being cropped to it.  The
# source picture has its own frame -- Aldemar's stag came inside a gilt
# roundel -- and at 1:1 that frame's arc cuts straight across the shield.
# Zooming in puts the borrowed frame outside the mask entirely, which is
# the only thing that needed to happen to it.
ZOOM=${ZOOM:-150}
"$IM" "$SRC" -resize $((400 * ZOOM / 100))x$((472 * ZOOM / 100))^ \
  -gravity center -extent 400x472 "$TMP/art.png"
"$IM" "$TMP/art.png" "$TMP/mask.png" -alpha off \
  -compose copy_opacity -composite "$TMP/charge.png"

# the rim, which is the larger shield in gilt with the smaller cut out
"$IM" "$TMP/rim.png" -channel RGB -fill '#b08a3a' -colorize 100% +channel \
  "$TMP/rimgold.png"

"$IM" "$TMP/ground.png" \
  "$TMP/rimgold.png" -gravity center -geometry +0+0 -composite \
  "$TMP/charge.png" -gravity center -geometry +0+0 -composite \
  "art/backs/$N.png.new"

mv "$SRC" "art/backs/$N-unshielded.png"
mv "art/backs/$N.png.new" "$SRC"
echo "art/backs/$N.png  --  shielded"
