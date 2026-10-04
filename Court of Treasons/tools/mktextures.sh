#!/bin/sh
# tools/mktextures.sh
#
# The surfaces the table is made of.
#
# Generated rather than drawn, for the same reason the card panel's paper
# is: a texture built from noise and a gradient can be regenerated at any
# size, tweaked by changing a number, and does not need storing in the
# repository.  They go to client/assets/ui and are gitignored.
#
# The lesson from the card panel applies here too and is the whole of why
# these look like anything: -level EXPANDS a range and clips, +level
# COMPRESSES one, and an overlay wants the compressed form.  At full
# range every one of these came out as sandpaper.
set -e
cd "$(dirname "$0")/.."
OUT=client/assets/ui
mkdir -p "$OUT"
IM=$(command -v magick || command -v convert)

# The table: old oiled leather.  Mottled slowly for age, grained finely
# for hide, and darker at the edges because a table is lit from above its
# middle.
#
# Cut to the size it covers rather than tiled.  Tiling a 1024 square put
# a hard seam down the screen at x=1024, and a seamless version of a
# plasma mottle is more work than simply making one the size of the
# table.  It costs three megabytes and nobody is downloading this over a
# modem.
"$IM" -size 1920x1080 "xc:#1a140e" \
  \( -size 1920x1080 plasma:fractal -colorspace gray -blur 0x10 \
     -auto-level +level 28%,72% \) -compose overlay -composite \
  \( -size 1920x1080 plasma:fractal -colorspace gray -blur 0x2 \
     -auto-level +level 41%,59% \) -compose overlay -composite \
  \( -size 1920x1080 xc:gray50 -attenuate 1.4 +noise Gaussian \
     -colorspace gray -blur 0x0.4 +level 42%,58% \) -compose overlay -composite \
  -modulate 104,118,100 \
  "$OUT/table.png"

# The panel: brushed brass, gone dark.  The brushing is noise dragged
# vertically, which is what a brushed surface is.
"$IM" -size 480x1080 "xc:#241d14" \
  \( -size 480x1080 xc:gray50 -attenuate 1.0 +noise Gaussian \
     -colorspace gray -motion-blur 0x40+90 -auto-level +level 33%,67% \) \
  -compose overlay -composite \
  "$OUT/panel.png"

# The status bars: the same leather, darker, so a House's bar reads as a
# band laid on the table rather than a hole cut in it.
"$IM" "$OUT/table.png" -resize 1456x260! -modulate 74,100,100 "$OUT/bar.png"

# A vignette to lay over the whole table, so the middle is where the eye
# goes.  Alpha only; the client tints nothing with it.
"$IM" -size 1920x1080 radial-gradient:'#00000000'-'#000000' \
  -channel A -evaluate multiply 0.55 +channel "$OUT/vignette.png"

# The card shadow: one soft ellipse, scaled by the client to whatever
# card needs it, which is cheaper than a real-time shadow and at this
# size indistinguishable from one.
"$IM" -size 256x256 xc:none \
  -fill '#000000' -draw "roundrectangle 24,24 232,232 12,12" \
  -blur 0x16 -channel A -evaluate multiply 0.62 +channel "$OUT/shadow.png"

for f in table panel bar vignette shadow; do
  printf "  %-10s %s\n" "$f" "$("$IM" identify -format '%wx%h %b' "$OUT/$f.png")"
done

# The faces.  Spectral, carried in the repo under the OFL, copied in
# under generic names so the client does not name a typeface in its
# source and a substitution is one copy away.
#
# It replaced Georgia, which was a system face on one operating system:
# not ours to redistribute, absent on the Linux box this is meant to be
# played against, and the source of the old-style zero that made "M0"
# read as "Mo".
FONTSRC=$(cd "$(dirname "$0")/../assets/fonts" && pwd)
cp "$FONTSRC/Spectral-Regular.ttf" "$OUT/serif.ttf"
cp "$FONTSRC/Spectral-Bold.ttf"    "$OUT/serif-bold.ttf"
cp "$FONTSRC/Spectral-Italic.ttf"  "$OUT/serif-italic.ttf"
cp "$FONTSRC/OFL.txt"              "$OUT/OFL.txt"
echo "  faces      Spectral (OFL)"
