#!/bin/sh
# tools/card.sh CARDID [OUT]
#
# Compose one finished card: the art from art/, everything else from
# CARDS.tsv, so the face cannot say something the engine does not.
#
#   tools/card.sh RAV010
#   tools/card.sh GOL006 /tmp/counting-house.png
#
# The layout follows what the design makes a player read, in the order a
# player reads it: what is this, what does it cost me, what does it do.
# Cost gets its own badges rather than a line of text because the design
# writes it in a compressed notation -- "3C 2Grv", "6G!" -- that is four
# things at once, and the bang in particular is the difference between
# spending this turn's Levy and spending what you are.
set -e
cd "$(dirname "$0")/.."
ID=$1
[ -n "$ID" ] || { echo "usage: tools/card.sh CARDID [OUT]"; exit 1; }
OUT=${2:-card-$ID.png}
ART="art/$ID.png"
[ -f "$ART" ] || { echo "no art for $ID -- run: python3 gen-art.py --id $ID"; exit 1; }

ROW=$(awk -F'\t' -v id="$ID" '$2==id' CARDS.tsv)
[ -n "$ROW" ] || { echo "$ID is not in CARDS.tsv"; exit 1; }
DECK=$(printf '%s' "$ROW" | cut -f1)
NAME=$(printf '%s' "$ROW" | cut -f3)
TYPE=$(printf '%s' "$ROW" | cut -f4)
CAT=$(printf '%s' "$ROW" | cut -f5)
COST=$(printf '%s' "$ROW" | cut -f6)
READS=$(printf '%s' "$ROW" | cut -f7)
EFFECT=$(printf '%s' "$ROW" | cut -f8)

# A Promise is a form, not a play, so it is laid out as one.  The term it
# carries is read out of its effect line and everything the form then says
# about that term -- what is undertaken, and when it falls due -- comes
# from TERMS.tsv, which the extractor pulls straight out of <promises>.
# Retyping a due condition onto a card face is how a card face and a rules
# engine come to disagree.
TERM=
case "$EFFECT" in
  *"Peace and Aid"*)  TERM=Alliance ;;
  *"any term"*)       TERM=Any ;;
  Term\ Vote*|Propose\ Vote*)               TERM=Vote ;;
  Term\ Peace*|Propose\ Peace*)             TERM=Peace ;;
  Term\ Forbearance*|Propose\ Forbearance*) TERM=Forbearance ;;
  Term\ Restraint*|Propose\ Restraint*)     TERM=Restraint ;;
  Term\ Disclosure*|Propose\ Disclosure*)   TERM=Disclosure ;;
  Term\ Tribute*|Propose\ Tribute*)         TERM=Tribute ;;
  Term\ Manumission*|Propose\ Manumission*) TERM=Manumission ;;
  Term\ Abstention*|Propose\ Abstention*)   TERM=Abstention ;;
esac
UNDERTAKING=; DUE=
if [ -n "$TERM" ] && [ -f TERMS.tsv ]; then
  UNDERTAKING=$(awk -F'\t' -v t="$TERM" '$1==t{print $3}' TERMS.tsv)
  DUE=$(awk -F'\t' -v t="$TERM" '$1==t{print $2}' TERMS.tsv)
fi
# A Promise whose term is not recognised would compose a form with empty
# blanks, which ImageMagick refuses with "label expected" some way down.
# Better to say which card and which term, here.
if [ "$TYPE" = "Promise" ] && [ -z "$DUE" ]; then
  echo "$ID: no term recognised in \"$EFFECT\"" >&2
  echo "  add it to the TERM case in tools/card.sh and to <promises>" >&2
  exit 1
fi

# What the consideration is, and therefore who a broken word offends.
# "Breaking a Promise that carried consideration gives 1 Grievance to
# every living House, not only the promisee.  Taking payment and reneging
# is an offence against the table."
CONS='none'
case "$EFFECT" in
  *"no consideration"*) CONS='none' ;;
  *consideration*)
    CONS=$(printf '%s' "$EFFECT" | sed -n 's/.*consideration \(up to \)\{0,1\}\([0-9][^;,]*\).*/\2/p' \
           | sed 's/ paid now//; s/ *$//') ;;
esac
[ -n "$CONS" ] || CONS='none'
if [ "$CONS" = 'none' ]; then
  BROKEN='1 Grievance to the promisee'
else
  BROKEN='1 Grievance to every living House'
fi
case "$EFFECT" in
  *"2 Grievance to every House"*) BROKEN='2 Grievance to every living House' ;;
esac

# Flavour is not in design.xml -- <card_schema> has id, name, type, cost,
# category, effect and reads, and no field for a voice.  It lives in
# flavour.tsv beside scenes.tsv until the schema gains one.  A card with
# no line there simply has no flavour; unlike a missing scene, that is a
# card you can still play.
FLAVOUR=$(awk -F'\t' -v id="$ID" '$1==id{print $2}' flavour.tsv 2>/dev/null || true)

# The words of the House, from HOUSES.tsv, for the four that have them.
WORDS=$(awk -F'\t' -v d="$DECK" '$1==d{print $3}' HOUSES.tsv 2>/dev/null || true)

# One accent per deck, the same registers the art is painted to, so a
# card's frame and its picture agree about what House it belongs to.
EMBLEM=
# One accent per deck.  Every deck is its own pile on the table, so a deck
# has to be identifiable before you pick it up.
#
# The palette this replaces had ten pairs closer than the legibility
# threshold and two -- Goldwyn against Throne, Aldemar against Court --
# that were the same colour to within a rounding error.  Measured in
# CIELab by tools/mkpalette.py.
#
# The lesson taken from Magic is that colour carries about seven things
# and symbols carry the rest.  A muted ochre-and-verdigris register makes
# that ceiling lower, not higher: nineteen decks in this register separate
# to 14.9 at best, which is not separable, while eight separate to 33.5,
# which is better than five well-chosen hues would manage.
#
# So colour carries the eight piles a player actually chooses between, and
# the back device carries deck identity everywhere else.  The four House
# decks are not among the eight because each one sits in front of its
# owner, and position identifies a deck at least as well as colour does.
#
# War, Political and Intrigue take the colour of the Levy they cost to
# draw from, which is the colour their cost badge already uses: one
# mapping, learned once, holding everywhere.  That puts War near Ravenmark
# and Intrigue near Goldwyn, and both of those are the point -- Ravenmark
# is the Military House and Goldwyn is the Gold one.
case "$DECK" in
  # the four Houses, distinguished by position, arms and frame together
  Ravenmark)    ACCENT='#7d3b2e'; DARK='#241a16'; LABEL='RAVENMARK'
                EMBLEM=assets/house-ravenmark.png ;;
  Vipren)       ACCENT='#2f5c46'; DARK='#131d18'; LABEL='VIPREN'
                EMBLEM=assets/house-vipren.png ;;
  Goldwyn)      ACCENT='#8a6a25'; DARK='#241d10'; LABEL='GOLDWYN'
                EMBLEM=assets/house-goldwyn.png ;;
  Aldemar)      ACCENT='#6a6350'; DARK='#20201b'; LABEL='ALDEMAR'
                EMBLEM=assets/house-aldemar.png ;;
  # the eight piles a player chooses between, separated to 33.5
  War)          ACCENT='#8f3c2c'; DARK='#28100c'; LABEL='WAR' ;;
  Political)    ACCENT='#3f5a74'; DARK='#111920'; LABEL='POLITICAL' ;;
  Intrigue)     ACCENT='#9a7b28'; DARK='#2b220b'; LABEL='INTRIGUE' ;;
  World)        ACCENT='#8b7362'; DARK='#26201b'; LABEL='WORLD' ;;
  Ambition)     ACCENT='#6f4a68'; DARK='#20151e'; LABEL='AMBITION' ;;
  Throne)       ACCENT='#4d2a2e'; DARK='#150b0c'; LABEL='THRONE' ;;
  # decks that never sit out as a pile to be chosen from: these resolve as
  # they are drawn, or belong to one situation.  Their backs tell them
  # apart and their colours only have to stay out of the eight above.
  Court)        ACCENT='#7c7468'; DARK='#23211d'; LABEL='THE COURT' ;;
  The\ Dead)    ACCENT='#4a5560'; DARK='#15181b'; LABEL='THE DEAD' ;;
  *)            ACCENT='#6b6456'; DARK='#1f1e1a'
                LABEL=$(printf '%s' "$DECK" | tr 'a-z' 'A-Z') ;;
esac

IM=$(command -v magick || command -v convert)
convert() { "$IM" "$@"; }

# Spectral, carried in the repo under the OFL.
#
# It replaced Georgia, which was a system face on one operating system:
# not ours to bake into 1,195 card images, and not present at all on the
# Linux box this game is meant to be played against.  The card art is
# the distributed artefact, so the licence mattered here before it
# mattered anywhere else.
#
# Two things decided it over the other OFL serifs.  It sets LINING
# figures -- Georgia's old-style zero dropped below the baseline and made
# "M0" read as "Mo" on the client, and Vollkorn would have repeated that
# exactly.  And a line of card rules text sets 410px against Georgia's
# 411, so nothing on an already-rendered card re-lays-out; Libre
# Baskerville is more legible still and 15% wider, which on a text box
# this tight is not a trade worth making.
FONTDIR=$(cd "$(dirname "$0")/../assets/fonts" && pwd)
SERIF=$FONTDIR/Spectral-Regular.ttf
SERIFB=$FONTDIR/Spectral-Bold.ttf
SERIFI=$FONTDIR/Spectral-Italic.ttf
for f in "$SERIF" "$SERIFB" "$SERIFI"; do
    [ -f "$f" ] || { echo "card.sh: missing font $f" >&2; exit 1; }
done

W=512; H=717
AX=26; AY=26; AW=460; AH=345          # the art window
# A Promise is a form and needs the room a form needs, so its window is
# shorter and its panel is taller.  Everything else on the card -- badges,
# brackets, rivets, the gear behind the text -- is placed off AY, AH, PY
# and PH, so moving these two moves all of it together.
if [ "$TYPE" = "Promise" ]; then AH=180; fi
# An Ambition carries a label, a condition, a standing rule and a flavour
# in a band a plain card sizes for one effect line, so its window gives
# some back.  Less than a Promise needs, because an Ambition is still a
# picture of the thing you want.
if [ "$TYPE" = "Ambition" ]; then AH=255; fi
PY=$((AY + AH + 14))                  # the text panel below it
PH=$((H - PY - 26))
PARCH='#d2bc8e'                        # the text panel
INK='#201a12'

TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT

# ---- the art, cropped to the window rather than squashed into it ------
convert "$ART" -resize "${AW}x${AH}^" -gravity center \
        -extent "${AW}x${AH}" "$TMP/art.png"

# ---- cost badges -----------------------------------------------------
# "3C 2Grv" and "6G!" are four facts in eight characters.  Each token
# becomes one badge; a bang -- the design's mark for a cost paid from
# Standing rather than Levy -- becomes a ring around it and the word
# STANDING under the row, because that distinction decides games.
BADGES=""; BX=0; PERM=0
if [ "$COST" != "0" ] && [ -n "$COST" ]; then
  for TOK in $COST; do
    AMT=$(printf '%s' "$TOK" | sed 's/[^0-9].*//')
    RES=$(printf '%s' "$TOK" | sed 's/^[0-9]*//; s/!$//')
    case "$TOK" in *!) PERM=1 ;; esac
    case "$RES" in
      M)   FILL='#8f3c2c'; SYM='M';   ICON=assets/icon-mil.png ;;
      C)   FILL='#3f5a74'; SYM='C';   ICON=assets/icon-cap.png ;;
      G)   FILL='#9a7b28'; SYM='G';   ICON=assets/icon-gold.png ;;
      Grv) FILL='#5d4a5e'; SYM='GRV'; ICON=assets/icon-grv.png ;;
      *)   FILL='#555';    SYM='?';   ICON= ;;
    esac
    # The disc, then the number, then the thing the number is counted in.
    # A letter is a label and needs reading; crossed swords, a seal, a
    # stack of coins and a broken crown are found rather than read, which
    # is what a hand of seven cards wants.  The disc keeps its colour as
    # well, so the symbol is never the only channel.
    convert -size 68x68 xc:none \
      -fill "$FILL" -stroke '#e8dcc0' -strokewidth 2 \
      -draw "circle 34,34 34,3" \
      -stroke none -fill '#f6edd8' \
      -font "$SERIFB" -pointsize 31 \
      -gravity center -annotate +0-9 "$AMT" \
      "$TMP/b$BX.png"
    if [ -n "$ICON" ] && [ -f "$ICON" ]; then
      convert "$TMP/b$BX.png" \
        \( "$ICON" -resize 26x26 -channel RGB \
           -fill '#f0e2c2' -colorize 100% +channel \) \
        -gravity center -geometry +0+17 -composite "$TMP/b$BX.png"
    else
      convert "$TMP/b$BX.png" -font "$SERIFB" -pointsize 14 \
        -fill '#e2d4b4' -gravity center -annotate +0+17 "$SYM" \
        "$TMP/b$BX.png"
    fi
    BADGES="$BADGES $TMP/b$BX.png"
    BX=$((BX + 1))
  done
fi

# ---- what the card says, in two registers ----------------------------
# The rules text is the engine's own words and has to be exact.  The
# flavour is nobody's rules and has to be clearly not rules, so it is
# italic, smaller, dimmer, and under a hairline -- three signals, because
# one is the kind of thing a player misreads once and resents.
# Type and category are the same word on every Promise card, so the term
# takes the second slot: what kind of promise this is matters, and
# "Promise  ·  Promise" does not.
SUBTITLE="$TYPE  ·  $CAT"
if [ "$TYPE" = "Promise" ] && [ -n "$TERM" ]; then
  SUBTITLE="Promise  ·  $TERM"
fi

TW=$((AW - 40))
# A Promise names a promiser, a promisee, a term, a due condition and a
# consideration, and at Reckoning both Houses have to read it to know what
# is owed.  None of that fitted on a face built for "play this, it does
# that", so a Promise gets a form: the undertaking in the first person,
# two ruled blanks for the Houses, and the three facts that decide the
# Reckoning set out where they can be found.
# An Ambition is not played, it is held: dealt face down at setup, known
# only to its House, and revealed at the Consequence it comes true in.
# Printing its condition in the slot every other card uses for an effect
# invites it to be read as something you do, which is the one thing it is
# not.  So the condition gets a label saying what it is waiting for, and
# the card carries the two rules a player needs at the moment it is
# revealed: that revealing is what happens, and what three of them buy.
build_ambition() {
  L=$(( AW - 40 ))
  convert -size ${L}x22 xc:none \
    -font "$SERIFB" -pointsize 14 -fill '#6a583f' \
    -gravity center -annotate +0+0 "COMPLETE WHEN" "$TMP/amb_lab.png"
  convert -background none -fill "$INK" -font "$SERIF" -pointsize 24 \
          -size ${L}x -gravity center caption:"$EFFECT" "$TMP/amb_cond.png"
  CH=$(convert "$TMP/amb_cond.png" -format '%h' info:)
  convert -background none -fill '#5c4c33' -font "$SERIF" -pointsize 15 \
          -size ${L}x -gravity center \
          caption:"Revealed when completed. Three make a House Throneworthy." \
          "$TMP/amb_note.png"
  NH=$(convert "$TMP/amb_note.png" -format '%h' info:)

  convert -size ${L}x$((22 + 10 + CH + 18 + NH)) xc:none \
    "$TMP/amb_lab.png"  -gravity north -geometry +0+0 -composite \
    "$TMP/amb_cond.png" -gravity north -geometry "+0+$((22 + 10))" -composite \
    "$TMP/amb_note.png" -gravity north -geometry "+0+$((22 + 10 + CH + 18))" -composite \
    "$TMP/effect.png"
  if [ -n "$FLAVOUR" ]; then
    convert -background none -fill '#6b5a3e' -font "$SERIFI" -pointsize 16 \
            -size ${L}x -gravity center caption:"$FLAVOUR" "$TMP/flav.png"
  fi
}

build_form() {
  L=$(( AW - 40 ))
  convert -background none -fill "$INK" -font "$SERIF" -pointsize 22 \
          -size ${L}x -gravity center \
          caption:"${UNDERTAKING:-$EFFECT}" "$TMP/undertaking.png"
  UH=$(convert "$TMP/undertaking.png" -format '%h' info:)

  # the blanks: a Promise is sworn by somebody to somebody, and the card
  # is where that is written down
  convert -size ${L}x68 xc:none \
    -font "$SERIFB" -pointsize 15 -fill '#5c4c33' \
    -draw "text 6,16 'SWORN BY'" -draw "text 6,54 'TO'" \
    -fill '#8a7a5c' -draw "rectangle 104,20 $((L-6)),21" \
    -fill '#e4d6b4' -draw "rectangle 104,22 $((L-6)),23" \
    -fill '#8a7a5c' -draw "rectangle 104,58 $((L-6)),59" \
    -fill '#e4d6b4' -draw "rectangle 104,60 $((L-6)),61" \
    "$TMP/blanks.png"

  # the three facts that decide a Reckoning
  VW=$(( L - 150 ))
  FY=0
  : > "$TMP/facts.cmd"
  convert -size ${L}x120 xc:none "$TMP/facts.png"
  for PAIR in "DUE|$DUE" "CONSIDERATION|$CONS" "IF BROKEN|$BROKEN"; do
    K=${PAIR%%|*}; V=${PAIR#*|}
    convert -background none -fill "$INK" -font "$SERIF" -pointsize 16 \
            -size ${VW}x -gravity west caption:"$V" "$TMP/val.png"
    VH=$(convert "$TMP/val.png" -format '%h' info:)
    convert "$TMP/facts.png" \
      -font "$SERIFB" -pointsize 13 -fill '#6a583f' \
      -draw "text 6,$((FY + 14)) '$K'" \
      "$TMP/val.png" -gravity northwest -geometry "+144+$FY" -composite \
      "$TMP/facts.png"
    FY=$(( FY + VH + 8 ))
  done
  convert "$TMP/facts.png" -crop ${L}x${FY}+0+0 +repage "$TMP/facts.png"
  FH2=$FY

  convert -size ${L}x$((UH + 16 + 68 + 14 + FH2)) xc:none \
    "$TMP/undertaking.png" -gravity north -geometry +0+0 -composite \
    "$TMP/blanks.png"      -gravity north -geometry "+0+$((UH + 16))" -composite \
    -fill '#a08d6a' -draw "rectangle 0,$((UH + 90)) $L,$((UH + 90))" \
    "$TMP/facts.png"       -gravity north -geometry "+0+$((UH + 98))" -composite \
    "$TMP/effect.png"
  if [ -n "$FLAVOUR" ]; then
    convert -background none -fill '#6b5a3e' -font "$SERIFI" -pointsize 16 \
            -size ${L}x -gravity center caption:"$FLAVOUR" "$TMP/flav.png"
  fi
}

build_text() {                       # $1 rules size, $2 flavour size
  convert -background none -fill "$INK" -font "$SERIF" -pointsize "$1" \
          -size ${TW}x -gravity center caption:"$EFFECT" "$TMP/effect.png"
  if [ -n "$FLAVOUR" ]; then
    convert -background none -fill '#6b5a3e' -font "$SERIFI" -pointsize "$2" \
            -size ${TW}x -gravity center caption:"$FLAVOUR" "$TMP/flav.png"
  fi
}
case "$TYPE" in
  Promise)  build_form ;;
  Ambition) build_ambition ;;
  *)        build_text 21 17 ;;
esac
EH=$(convert "$TMP/effect.png" -format '%h' info:)
FH=0
[ -n "$FLAVOUR" ] && FH=$(convert "$TMP/flav.png" -format '%h' info:)

# A long effect and a long flavour together can outgrow the panel.  Rather
# than let either overflow, both step down one size -- the card stays
# readable and the layout stays the layout.
BAND_TOP=$((PY + 98))
BAND_BOT=$((PY + PH - 58))
if [ "$TYPE" = "Promise" ] || [ "$TYPE" = "Ambition" ]; then
  BAND_TOP=$((PY + 92))
fi
GAP=34
# A form already separates its own sections with rules and small caps, so
# the gap before the flavour can be tighter than a plain card needs -- and
# it has to be: the Sworn Alliance's stack came out nine pixels over its
# band and clamped, which put the flavour through the footer.
[ "$TYPE" = "Promise" ] && GAP=18
[ "$TYPE" = "Ambition" ] && GAP=20
STACK=$((EH + FH))
[ "$FH" -gt 0 ] && STACK=$((STACK + GAP))
if [ "$TYPE" != "Promise" ] && [ "$TYPE" != "Ambition" ] \
   && [ "$STACK" -gt $((BAND_BOT - BAND_TOP)) ]; then
  build_text 18 15
  EH=$(convert "$TMP/effect.png" -format '%h' info:)
  [ -n "$FLAVOUR" ] && FH=$(convert "$TMP/flav.png" -format '%h' info:)
  STACK=$((EH + FH))
  [ "$FH" -gt 0 ] && STACK=$((STACK + GAP))
fi

# stack them with a hairline between, then centre the stack in the band
convert -size ${TW}x${STACK} xc:none "$TMP/effect.png" \
  -gravity north -geometry +0+0 -composite "$TMP/text.png"
if [ "$FH" -gt 0 ]; then
  RY=$((EH + GAP / 2))
  convert "$TMP/text.png" \
    -fill '#a08d6a' -draw "rectangle $((TW/2 - 60)),$RY $((TW/2 + 60)),$RY" \
    -fill '#e4d6b4' -draw "rectangle $((TW/2 - 60)),$((RY+1)) $((TW/2 + 60)),$((RY+1))" \
    "$TMP/flav.png" -gravity north -geometry "+0+$((EH + GAP))" -composite \
    "$TMP/text.png"
fi

# ---- the card ---------------------------------------------------------
convert -size ${W}x${H} "xc:$DARK" \
  \( -size $((W - 16))x$((H - 16)) "xc:$ACCENT" \) -gravity center -composite \
  \( -size $((W - 28))x$((H - 28)) "xc:$DARK" \) -gravity center -composite \
  "$TMP/base.png"

# art window: the same brass bezel the panel gets, with a bevel -- dark
# where the light does not reach, pale where it does -- and a bracket at
# each corner.
convert "$TMP/base.png" \
  -fill "$ACCENT" -draw "rectangle $((AX-4)),$((AY-4)) $((AX+AW+3)),$((AY+AH+3))" \
  "$TMP/art.png" -geometry "+${AX}+${AY}" -composite \
  -fill none -strokewidth 1 \
  -stroke '#00000055' -draw "line $((AX-4)),$((AY-4)) $((AX+AW+3)),$((AY-4))" \
  -stroke '#00000055' -draw "line $((AX-4)),$((AY-4)) $((AX-4)),$((AY+AH+3))" \
  -stroke '#ffffff44' -draw "line $((AX-4)),$((AY+AH+3)) $((AX+AW+3)),$((AY+AH+3))" \
  -stroke '#ffffff44' -draw "line $((AX+AW+3)),$((AY-4)) $((AX+AW+3)),$((AY+AH+3))" \
  "$TMP/c1.png"
for BR in "$((AX-1)),$((AY-1))  $((AX+15)),$((AY-1))  $((AX-1)),$((AY+15))" \
          "$((AX+AW+1)),$((AY-1))  $((AX+AW-15)),$((AY-1))  $((AX+AW+1)),$((AY+15))" \
          "$((AX-1)),$((AY+AH+1))  $((AX+15)),$((AY+AH+1))  $((AX-1)),$((AY+AH-15))" \
          "$((AX+AW+1)),$((AY+AH+1))  $((AX+AW-15)),$((AY+AH+1))  $((AX+AW+1)),$((AY+AH-15))"; do
  set -- $BR
  convert "$TMP/c1.png" -stroke '#e0cda0' -strokewidth 3 -fill none \
    -draw "line $1 $2" -draw "line $1 $3" "$TMP/c1.png"
done

# ---- the panel that carries every word on the card -------------------
# Flat colour read as a slab.  What makes it a card rather than a form is
# the furniture, not the tint: aged paper, a gear turning behind the text,
# a brass bezel and four rivets holding it to the frame.  The paper stays
# light and the ink stays dark, because the words have to be legible at a
# glance and texture that costs contrast is texture that costs the game.
# aged paper: a slow mottle for age, a fine grain for fibre, and a
# vignette because paper darkens at its edges first.  Both textures are
# compressed into a narrow band around mid-grey before they are overlaid
# -- at full range the overlay clips and the panel turns to sandpaper.
convert -size ${AW}x${PH} "xc:$PARCH" \
  \( -size ${AW}x${PH} plasma:fractal -colorspace gray -blur 0x8 \
     -auto-level +level 34%,66% \) -compose overlay -composite \
  \( -size ${AW}x${PH} xc:gray50 -attenuate 0.8 +noise Gaussian \
     -colorspace gray -blur 0x0.4 +level 43%,57% \) -compose overlay -composite \
  \( -size ${AW}x${PH} radial-gradient:'#fffaf0'-'#b8a074' \) -compose multiply -composite \
  -modulate 101,110,101 \
  "$TMP/panel.png"

# a gear turning behind the text, at the weight of a watermark
if [ -f assets/gear.png ]; then
  convert "$TMP/panel.png" \
    \( assets/gear.png -resize 250x250 -alpha set \
       -channel A -evaluate multiply 0.06 +channel \) \
    -gravity center -geometry +0+0 -composite "$TMP/panel.png"
fi

# an engraved inner line: one dark stroke, one light beneath it, which is
# what an engraving is
convert "$TMP/panel.png" -fill none -strokewidth 1 \
  -stroke '#8a7a5c' -draw "rectangle 7,7 $((AW-8)),$((PH-8))" \
  -stroke '#e4d6b4' -draw "rectangle 8,8 $((AW-9)),$((PH-9))" \
  "$TMP/panel.png"

# the brass bezel the panel is set into, then the panel itself
convert "$TMP/c1.png" \
  -fill "$ACCENT" -draw "rectangle $((AX-4)),$((PY-4)) $((AX+AW+3)),$((PY+PH+3))" \
  "$TMP/panel.png" -geometry "+${AX}+${PY}" -composite \
  "$TMP/c2.png"

# four rivets, one at each corner, holding it down
convert -size 26x26 xc:none \
  -fill '#8a6a2a' -stroke '#2e2410' -strokewidth 1 -draw "circle 13,13 13,3" \
  -stroke none -fill '#efd9a2' -draw "circle 10,10 10,7" \
  -blur 0x0.5 "$TMP/rivet.png"
for RP in "$((AX+8))+$((PY+8))" "$((AX+AW-34))+$((PY+8))" \
          "$((AX+8))+$((PY+PH-34))" "$((AX+AW-34))+$((PY+PH-34))"; do
  convert "$TMP/c2.png" "$TMP/rivet.png" -geometry "+$RP" -composite "$TMP/c2.png"
done

# title, then a rule, then type and category, then the effect
convert "$TMP/c2.png" \
  -font "$SERIFB" -pointsize 30 -fill "$INK" \
  -gravity north -annotate "+0+$((PY + 14))" "$NAME" \
  -fill "$ACCENT" \
  -draw "rectangle $((AX+30)),$((PY+58)) $((AX+AW-30)),$((PY+59))" \
  -fill '#e4d6b4' \
  -draw "rectangle $((AX+30)),$((PY+60)) $((AX+AW-30)),$((PY+61))" \
  -font "$SERIF" -pointsize 18 -fill '#4a3c26' \
  -gravity north -annotate "+0+$((PY + 68))" "$SUBTITLE" \
  "$TMP/c3.png"

EY=$((BAND_TOP + (BAND_BOT - BAND_TOP - STACK) / 2))
[ "$EY" -lt "$BAND_TOP" ] && EY=$BAND_TOP
convert "$TMP/c3.png" "$TMP/text.png" \
  -gravity north -geometry "+0+$EY" -composite "$TMP/c4.png"

# House name and words along the foot, and the id in the corner: the id
# is there so a card on a table can be found in CARDS.tsv.
# The House emblem sits beside its name.  Frame colour already says which
# House this is, and that is enough until the light is bad, or the reader
# is colour-blind, or the card is face up on a table two feet away; a
# shape carries at all three.  Shared decks have no arms and get none.
EMX=0
if [ -n "$EMBLEM" ] && [ -f "$EMBLEM" ]; then
  LW=$(convert -font "$SERIFB" -pointsize 16 label:"$LABEL" -format '%w' info:)
  # 24px was not enough: a viper's body is a thin stroke and it vanished,
  # and the raven read as a dark wedge.  34px, set against the whole
  # two-line footer rather than against the name alone.
  EMX=$(( W / 2 - LW / 2 - 46 ))
  convert "$TMP/c4.png" \
    \( "$EMBLEM" -resize 34x34 -channel RGB \
       -fill '#44381f' -colorize 100% +channel \) \
    -gravity northwest -geometry "+${EMX}+$((PY + PH - 56))" -composite \
    "$TMP/c4.png"
fi

convert "$TMP/c4.png" \
  -font "$SERIFB" -pointsize 16 -fill '#4e4029' \
  -gravity north -annotate "+0+$((PY + PH - 44))" "$LABEL" \
  -font "$SERIF" -pointsize 15 -fill '#5c4c33' \
  -gravity north -annotate "+0+$((PY + PH - 24))" "${WORDS:+$WORDS}" \
  -font "$SERIF" -pointsize 12 -fill '#5e5039' \
  -gravity northwest -annotate "+$((AX + AW - 116))+$((PY + PH - 23))" "$ID" \
  "$TMP/c5.png"

# The cost badges are laid into the top-left of the art, which is fine
# until the art has something there.  Iron Fist put two badges across a
# gauntlet and both lost their contrast; the badge was designed against
# cards that happened to have sky in that corner.
#
# A scrim rather than a plate: a plate would cover more art than the
# badges do and only move the problem, while a corner that falls into
# shadow reads as lighting.  The gradient is a centred radial cropped to
# its bottom-right quadrant, which puts the dark at the corner.
if [ -n "$BADGES" ]; then
  SW=$(( 70 + BX * 72 ))
  SH=170
  [ "$SW" -gt "$AW" ] && SW=$AW
  convert -size $((SW * 2))x$((SH * 2)) \
    radial-gradient:'#000000'-'#00000000' \
    -gravity southeast -crop ${SW}x${SH}+0+0 +repage \
    -channel A -evaluate multiply 0.62 +channel "$TMP/scrim.png"
  convert "$TMP/c5.png" "$TMP/scrim.png" -gravity northwest \
    -geometry "+${AX}+${AY}" -composite "$TMP/c5.png"
fi

if [ -n "$BADGES" ]; then
  CMD="convert $TMP/c5.png"
  i=0
  for B in $BADGES; do
    CMD="$CMD $B -geometry +$((AX + 10 + i * 72))+$((AY + 10)) -composite"
    i=$((i + 1))
  done
  eval "$CMD $TMP/c6.png"
else
  cp "$TMP/c5.png" "$TMP/c6.png"
fi

# A lens in the far corner of the window for a card that inspects public
# state.  The design gives these a <reads> attribute and calls the
# Reputation category the place the new game lives; before this the face
# said nothing at all about it.
# Built into its own file and composited with an explicit gravity.  The
# -gravity center inside a \( ... \) group stays in effect for the
# composite that follows it, so the first attempt read its offset from
# the middle of the card and quietly put the lens behind the text panel.
if [ -n "$READS" ] && [ -f assets/icon-lens.png ]; then
  convert -size 46x46 xc:none \
    -fill '#2b2318' -stroke '#e0cda0' -strokewidth 2 \
    -draw "circle 23,23 23,3" "$TMP/lens.png"
  convert "$TMP/lens.png" \
    \( assets/icon-lens.png -resize 25x25 -channel RGB \
       -fill '#e8d9ae' -colorize 100% +channel \) \
    -gravity center -geometry +0+0 -composite "$TMP/lens.png"
  convert "$TMP/c6.png" "$TMP/lens.png" -gravity northwest \
    -geometry "+$((AX + AW - 56))+$((AY + 10))" -composite "$TMP/c6.png"
fi

if [ "$PERM" = 1 ]; then
  convert "$TMP/c6.png" -font "$SERIFB" -pointsize 13 -fill '#f2e8d2' \
    -stroke "$DARK" -strokewidth 3 -gravity northwest \
    -annotate "+$((AX + 8))+$((AY + 82))" "STANDING" \
    -stroke none -annotate "+$((AX + 8))+$((AY + 82))" "STANDING" "$OUT"
else
  cp "$TMP/c6.png" "$OUT"
fi

convert "$OUT" \
  \( +clone -alpha opaque -fill black -colorize 100% \
     -fill white -draw "roundrectangle 0,0 $((W-1)),$((H-1)) 24,24" \
     -alpha off \) \
  -alpha off -compose copy_opacity -composite "$OUT"

echo "$OUT  --  $NAME ($ID, $DECK)"
