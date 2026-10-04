#!/usr/bin/env python3
"""Draw the four cost symbols into assets/.

    python3 tools/mkicons.py

The design denominates costs in Military, Capital, Gold and Grievance and
writes them as M, C, G and Grv.  A letter is a label; a symbol is a thing,
and a thing is what the eye finds in a hand of seven cards.

Each is drawn white on transparent so the badge can tint it, and each is
drawn as a silhouette rather than a picture, because it has to survive
being 18 pixels wide inside a coloured disc.
"""
import os

W = '#ffffff'
ICONS = {
    # Military: crossed sabres.  The guards at the hilt are what stop an
    # X from reading as a cancel mark.
    'mil': '''
      <g stroke="%s" stroke-width="13" stroke-linecap="round" fill="none">
        <line x1="22" y1="22" x2="80" y2="80"/>
        <line x1="78" y1="22" x2="20" y2="80"/>
      </g>
      <g stroke="%s" stroke-width="9" stroke-linecap="round" fill="none">
        <line x1="8"  y1="62" x2="32" y2="86"/>
        <line x1="92" y1="62" x2="68" y2="86"/>
      </g>''' % (W, W),
    # Capital is political, not monetary -- votes, councils, oaths,
    # legitimacy -- so it is a seal on a ribbon and not a coin.
    'cap': '''
      <path d="M 36,58 L 24,94 L 44,84 L 52,64 Z" fill="%s"/>
      <path d="M 64,58 L 76,94 L 56,84 L 48,64 Z" fill="%s"/>
      <circle cx="50" cy="40" r="28" fill="%s"/>
      <circle cx="50" cy="40" r="13" fill="none" stroke="#000"
              stroke-opacity="0.35" stroke-width="7"/>''' % (W, W, W),
    # Gold: a stack of coins.  A purse read as a loaf of bread, and a
    # single coin inside a coloured disc is a circle inside a circle,
    # which at this size is a smudge.  A stack has a silhouette.
    'gold': '''
      <g fill="%s" stroke="#000" stroke-opacity="0.4" stroke-width="5">
        <ellipse cx="50" cy="74" rx="33" ry="13"/>
        <ellipse cx="50" cy="53" rx="33" ry="13"/>
        <ellipse cx="50" cy="32" rx="33" ry="13"/>
      </g>''' % W,
    # Grievance: a crown, broken in half.  Two arcs of a snapped chain
    # link merged into a blob at this size and read as an S.  Grievance
    # is held against the Lord Paramount and spent on unseating them, so
    # the crown is both what it is about and a silhouette that survives
    # being eighteen pixels wide.
    'grv': '''
      <g fill="%s">
        <path d="M 10,78 L 10,38 L 28,54 L 45,22 L 43,78 Z"/>
        <g transform="rotate(14 72 54)">
          <path d="M 57,78 L 55,22 L 72,54 L 90,38 L 90,78 Z"/>
        </g>
      </g>''' % W,
    # A lens, for cards that read the table.  The design calls the
    # Reputation category "where the new game lives" and gives those
    # cards a <reads> attribute; nothing on the face said so.
    'lens': '''
      <g fill="none" stroke="%s" stroke-width="12" stroke-linecap="round">
        <circle cx="44" cy="42" r="26"/>
        <line x1="63" y1="61" x2="86" y2="84"/>
      </g>''' % W,
}


os.makedirs('assets', exist_ok=True)
for name, body in ICONS.items():
    with open('assets/icon-%s.svg' % name, 'w') as fh:
        fh.write('<svg xmlns="http://www.w3.org/2000/svg" '
                 'viewBox="0 0 100 100" width="100" height="100">%s</svg>'
                 % body)
    print('  assets/icon-%s.svg' % name)

# Render to PNG here rather than leaving it to the caller.  ImageMagick
# silently produced a blank 321-byte greyscale file for the two icons
# drawn with <g stroke=...><line/></g> while rendering the two drawn with
# filled paths correctly -- so the SVGs were fine and the renderer was
# not.  rsvg-convert, which is ImageMagick's own declared SVG delegate,
# draws all four.
import shutil
import subprocess

rsvg = shutil.which('rsvg-convert')
for name in ICONS:
    src = 'assets/icon-%s.svg' % name
    dst = 'assets/icon-%s.png' % name
    if rsvg:
        subprocess.run([rsvg, '-w', '160', '-h', '160', src, '-o', dst],
                       check=True)
    else:
        subprocess.run(['magick', '-background', 'none', src,
                        '-resize', '160x160', dst], check=True)
    if os.path.getsize(dst) < 600:
        raise SystemExit('%s rendered blank -- check the SVG renderer' % dst)
    print('  %s' % dst)
