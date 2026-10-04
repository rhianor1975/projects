#!/usr/bin/env python3
"""Draw the four House emblems into assets/.

    python3 tools/mkemblems.py

Frame colour tells you the House, which is fine until the light is bad or
the reader is colour-blind or the cards are face up on a table two feet
away.  A shape carries at any of those.

Three beasts and a trade, which is itself a statement about Goldwyn:
nobody granted them arms, so they use the scales they actually own.

Rendered through rsvg-convert for the same reason the cost symbols are:
ImageMagick's own SVG path silently produces blank files for stroked
geometry.
"""
import os
import shutil
import subprocess

W = '#ffffff'
EMBLEMS = {
    # Ravenmark -- By Sword and Storm.  A raven's head in profile.  The
    # first attempt made the skull round and the beak short and it read
    # as a comma; a raven is a sloping forehead and a long straight beak,
    # and the beak has to be nearly half the width to say raven rather
    # than bird.
    'ravenmark': '''
      <path d="M 76,92 L 86,50 Q 86,26 60,26 L 6,46 L 52,54
               Q 55,74 58,92 Z" fill="%s"/>
      <circle cx="68" cy="43" r="5" fill="#000" fill-opacity="0.55"/>''' % W,
    # Vipren -- What is Whispered is Owned.  A serpent reared to strike.
    # Coiled, it read as a snail: a spiral is a spiral whatever you put
    # in the middle of it.  An S-curve with a wedge head is a snake at
    # any size.
    'vipren': '''
      <g stroke="%s" stroke-width="14" fill="none" stroke-linecap="round">
        <path d="M 20,90 Q 22,64 44,58 Q 68,52 66,34 Q 64,20 48,22"/>
      </g>
      <path d="M 54,10 L 22,24 L 54,36 Z" fill="%s"/>
      <circle cx="44" cy="22" r="3.5" fill="#000" fill-opacity="0.6"/>''' % (W, W),
    # Goldwyn -- Every Throne Has a Price.  Scales, not coins: the cost
    # symbol is already a stack of coins and two of those on one card
    # would be one idea wearing two hats.
    'goldwyn': '''
      <g stroke="%s" stroke-width="9" stroke-linecap="round">
        <line x1="50" y1="14" x2="50" y2="74"/>
        <line x1="16" y1="28" x2="84" y2="28"/>
        <line x1="28" y1="80" x2="72" y2="80"/>
      </g>
      <path d="M 4,36 L 32,36 L 18,58 Z" fill="%s"/>
      <path d="M 68,36 L 96,36 L 82,58 Z" fill="%s"/>''' % (W, W, W),
    # Aldemar -- The Realm Remembers.  A stag: the oldest charge on the
    # oldest shield, which is the whole of this House's argument.
    'aldemar': '''
      <path d="M 50,94 Q 34,80 34,60 Q 34,46 50,46 Q 66,46 66,60
               Q 66,80 50,94 Z" fill="%s"/>
      <g stroke="%s" stroke-width="8" fill="none" stroke-linecap="round">
        <path d="M 40,50 L 26,32 L 12,26"/>
        <path d="M 26,32 L 16,40"/>
        <path d="M 35,42 L 25,14"/>
        <path d="M 60,50 L 74,32 L 88,26"/>
        <path d="M 74,32 L 84,40"/>
        <path d="M 65,42 L 75,14"/>
      </g>''' % (W, W),

    # The four added 2026-09-20.  Each is a silhouette that survives
    # being 24 pixels wide on a hand card, which is the only size that
    # matters -- the first four taught that the hard way: a coiled
    # serpent read as a snail and a round-skulled raven as a comma.
    # So: one heavy mass, one unmistakable protrusion, nothing thin.
    #
    # Leoward, the Lion -- A Lion's Word is a Lion's Claw.  A maned head facing
    # front.  In profile a lion is a dog; the mane ring is the whole
    # of the reading, so the mane is a disc and the face sits inside it.
    'leoward': '''
      <circle cx="50" cy="52" r="40" fill="%s"/>
      <circle cx="50" cy="54" r="23" fill="#000" fill-opacity="0.45"/>
      <path d="M 38,48 L 44,48 M 56,48 L 62,48" stroke="%s"
            stroke-width="6" stroke-linecap="round"/>
      <path d="M 50,60 L 43,70 L 57,70 Z" fill="%s"/>''' % (W, W, W),
    # Stonegarth, the Ox -- What Is Bound Stays Bound.  A horned head, seen front on,
    # with the yoke across it.  The horns are the silhouette and they
    # go wide rather than up: upward horns read as a stag, which is
    # Aldemar.
    'stonegarth': '''
      <path d="M 30,46 Q 50,34 70,46 Q 74,74 50,88 Q 26,74 30,46 Z" fill="%s"/>
      <g stroke="%s" stroke-width="11" fill="none" stroke-linecap="round">
        <path d="M 30,46 Q 10,40 6,20"/>
        <path d="M 70,46 Q 90,40 94,20"/>
      </g>
      <rect x="22" y="30" width="56" height="9" rx="4" fill="%s"/>''' % (W, W, W),
    # Wulfren, the Wolf -- The Pack Remembers.  A head in profile: unlike a lion, a
    # wolf IS its profile -- the long muzzle and the upright ears say
    # it at any size, and a front view would read as a dog.
    'wulfren': '''
      <path d="M 18,30 L 30,54 L 10,66 L 44,84 Q 72,86 82,62
               Q 90,44 70,34 L 60,30 L 52,12 L 40,32 Z" fill="%s"/>
      <path d="M 18,30 L 26,46 L 34,30 Z" fill="%s"/>
      <circle cx="62" cy="52" r="4" fill="#000" fill-opacity="0.6"/>''' % (W, W),
    # Everhold, the Boar -- It Does Not Stop Coming.  A head lowered to charge, with
    # the tusk curving up.  The tusk is the only part that has to
    # survive shrinking, so it is thick and it clears the jaw line.
    'everhold': '''
      <path d="M 14,70 Q 22,40 48,36 Q 78,32 86,54 Q 90,74 66,82
               Q 36,88 14,70 Z" fill="%s"/>
      <path d="M 26,74 Q 14,66 18,52 Q 24,62 32,64 Z" fill="%s"/>
      <path d="M 12,34 L 26,44 L 20,28 Z" fill="%s"/>
      <circle cx="62" cy="54" r="4" fill="#000" fill-opacity="0.6"/>''' % (W, W, W),
}

os.makedirs('assets', exist_ok=True)
rsvg = shutil.which('rsvg-convert')
for name, body in EMBLEMS.items():
    src, dst = 'assets/house-%s.svg' % name, 'assets/house-%s.png' % name
    with open(src, 'w') as fh:
        fh.write('<svg xmlns="http://www.w3.org/2000/svg" '
                 'viewBox="0 0 100 100" width="100" height="100">%s</svg>'
                 % body)
    cmd = ([rsvg, '-w', '160', '-h', '160', src, '-o', dst] if rsvg else
           ['magick', '-background', 'none', src, '-resize', '160x160', dst])
    subprocess.run(cmd, check=True)
    if os.path.getsize(dst) < 600:
        raise SystemExit('%s rendered blank -- check the SVG renderer' % dst)
    print('  %s' % dst)
