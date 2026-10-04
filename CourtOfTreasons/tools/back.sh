#!/bin/sh
# tools/back.sh HOUSE [OUT]
#
# Compose a card back from art/backs/HOUSE.png.
#
#   tools/back.sh ravenmark
#   tools/back.sh realm /tmp/shared-back.png
#
# The art arrives square and the card is portrait, so the shield is set on
# a House-coloured ground rather than cropped to fit: cropping a square
# shield into a portrait card takes the gilt corners off it, and those
# corners are most of what makes it read as heraldry rather than as a
# picture of a bird.
set -e
cd "$(dirname "$0")/.."
H=$1
[ -n "$H" ] || { echo "usage: tools/back.sh HOUSE [OUT]"; exit 1; }
SRC="art/backs/$H.png"
[ -f "$SRC" ] || { echo "no back art for $H"; exit 1; }
OUT=${2:-back-$H.png}

IM=$(command -v magick || command -v convert)
convert() { "$IM" "$@"; }

# The same accents tools/card.sh uses, so a deck's back and its faces are
# the same colour.  Kept here rather than sourced because a back has to
# compose from art alone, without CARDS.tsv.
case "$H" in
  ravenmark) ACCENT='#7d3b2e'; DARK='#241a16' ;;
  vipren)    ACCENT='#2f5c46'; DARK='#131d18' ;;
  goldwyn)   ACCENT='#8a6a25'; DARK='#241d10' ;;
  aldemar)   ACCENT='#6a6350'; DARK='#20201b' ;;
  war)       ACCENT='#8f3c2c'; DARK='#28100c' ;;
  political) ACCENT='#3f5a74'; DARK='#111920' ;;
  intrigue)  ACCENT='#9a7b28'; DARK='#2b220b' ;;
  world)     ACCENT='#8b7362'; DARK='#26201b' ;;
  ambition)  ACCENT='#6f4a68'; DARK='#20151e' ;;
  throne)    ACCENT='#4d2a2e'; DARK='#150b0c' ;;
  promise)   ACCENT='#2b4a2c'; DARK='#0c150c' ;;
  lever)     ACCENT='#4a7040'; DARK='#152012' ;;
  council)   ACCENT='#56646b'; DARK='#181d1e' ;;
  court)     ACCENT='#7c7468'; DARK='#23211d' ;;
  *)         ACCENT='#6b6456'; DARK='#1f1e1a' ;;
esac

W=512; H2=717
AX=26; AY=98; AW=460; AH=460          # the shield, square, centred

TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT

convert "$SRC" -resize "${AW}x${AH}^" -gravity center \
        -extent "${AW}x${AH}" "$TMP/art.png"

# The same frame the faces wear, so a deck looks like one deck whichever
# way up it is lying.
convert -size ${W}x${H2} "xc:$DARK" \
  \( -size $((W - 16))x$((H2 - 16)) "xc:$ACCENT" \) -gravity center -composite \
  \( -size $((W - 28))x$((H2 - 28)) "xc:$DARK" \) -gravity center -composite \
  "$TMP/base.png"

# A brushed-brass ground above and below the shield, so the bands are not
# two slabs of flat colour.
convert -size ${AW}x$((H2 - 52)) "xc:$DARK" \
  \( -size ${AW}x$((H2 - 52)) xc:gray50 -attenuate 0.9 +noise Gaussian \
     -colorspace gray -motion-blur 0x30+90 -auto-level +level 42%,58% \) \
  -compose overlay -composite "$TMP/ground.png"

convert "$TMP/base.png" "$TMP/ground.png" -geometry "+${AX}+26" -composite \
  -fill "$ACCENT" -draw "rectangle $((AX-4)),$((AY-4)) $((AX+AW+3)),$((AY+AH+3))" \
  "$TMP/art.png" -geometry "+${AX}+${AY}" -composite \
  "$TMP/c1.png"

# corner brackets, as on the face
for BR in "$((AX-1)),$((AY-1))  $((AX+15)),$((AY-1))  $((AX-1)),$((AY+15))" \
          "$((AX+AW+1)),$((AY-1))  $((AX+AW-15)),$((AY-1))  $((AX+AW+1)),$((AY+15))" \
          "$((AX-1)),$((AY+AH+1))  $((AX+15)),$((AY+AH+1))  $((AX-1)),$((AY+AH-15))" \
          "$((AX+AW+1)),$((AY+AH+1))  $((AX+AW-15)),$((AY+AH+1))  $((AX+AW+1)),$((AY+AH-15))"; do
  set -- $BR
  convert "$TMP/c1.png" -stroke '#e0cda0' -strokewidth 3 -fill none \
    -draw "line $1 $2" -draw "line $1 $3" "$TMP/c1.png"
done

# the title, above and below, so the back is legible in a stack seen edge on
FONTDIR=$(cd "$(dirname "$0")/../assets/fonts" && pwd)
SERIFB=$FONTDIR/Spectral-Bold.ttf
[ -f "$SERIFB" ] || SERIFB=/System/Library/Fonts/Helvetica.ttc
LABEL=$(printf '%s' "$H" | tr 'a-z' 'A-Z')
[ "$H" = "realm" ] && LABEL="COURT OF TREASONS"
[ "$H" = "court" ] && LABEL="THE COURT"
# Top upright, bottom turned over.  A back is looked at from whichever
# side of the table you are sitting on, and two of the same word the same
# way up reads as a printing mistake.
convert "$TMP/c1.png" \
  -font "$SERIFB" -pointsize 25 -fill '#e6d5ab' \
  -gravity north -annotate "+0+52" "$LABEL" "$TMP/c2.png"
convert -background none -fill '#e6d5ab' -font "$SERIFB" -pointsize 25 \
  label:"$LABEL" -rotate 180 "$TMP/lab.png"
convert "$TMP/c2.png" "$TMP/lab.png" -gravity south -geometry +0+46 \
  -composite "$TMP/c2.png"

convert "$TMP/c2.png" \
  \( +clone -alpha opaque -fill black -colorize 100% \
     -fill white -draw "roundrectangle 0,0 $((W-1)),$((H2-1)) 24,24" \
     -alpha off \) \
  -alpha off -compose copy_opacity -composite "$OUT"

echo "$OUT  --  $LABEL"
