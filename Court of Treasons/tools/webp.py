#!/usr/bin/env python3
"""tools/webp.py -- the one place art becomes WebP.

    python3 tools/webp.py [--quality N | --lossless] [--keep] FILE.png ...
    python3 tools/webp.py --tree DIR [--quality N | --lossless]

Art is stored as WebP because PNG is the wrong format for paintings: a
512-pixel card face is 428 KB as PNG and 43 KB as WebP at a quality
nobody can tell from the original, and the repository was carrying a
gigabyte and a half of PNG.  Paintings go lossy; the UI surfaces are
mostly generated noise, which lossy coding smooths into mud, so those go
lossless (still eight times smaller than the PNG).

Pillow does the work when it is installed, and ImageMagick when it is
not -- every other art tool here already needs ImageMagick, so that is
the dependency this adds nothing to.

Also imported: save_webp(data, dest) writes bytes that arrived as PNG
(from ImageLab, say) straight to dest as WebP.
"""
import os
import shutil
import subprocess
import sys
import tempfile

PAINTING = 90      # card faces, backs: small text survives at this
MASTER = 92        # the raw renders everything else is composed from

try:
    from PIL import Image
except ImportError:  # pragma: no cover - depends on the machine
    Image = None


def _magick():
    for name in ('magick', 'convert'):
        p = shutil.which(name)
        if p:
            return p
    return None


def convert(src, dest, quality=PAINTING, lossless=False):
    """src (any format) -> dest (.webp).  Writes to a temporary and moves
    on success, so a failed conversion never leaves half a file where a
    picture was."""
    tmp = dest + '.tmp.webp'
    if Image is not None:
        im = Image.open(src)
        im.load()
        if im.mode == 'P':
            im = im.convert('RGBA')
        kw = {'lossless': True} if lossless else {'quality': quality}
        im.save(tmp, 'WEBP', method=6, **kw)
    else:
        im = _magick()
        if im is None:
            raise RuntimeError('need Pillow or ImageMagick to write WebP')
        args = [im, src]
        args += (['-define', 'webp:lossless=true'] if lossless
                 else ['-quality', str(quality)])
        args += ['-define', 'webp:method=6', tmp]
        subprocess.run(args, check=True)
    os.replace(tmp, dest)


def save_webp(data, dest, quality=MASTER):
    """Bytes of any image format -> dest as WebP."""
    fd, src = tempfile.mkstemp(suffix='.png')
    try:
        with os.fdopen(fd, 'wb') as fh:
            fh.write(data)
        convert(src, dest, quality)
    finally:
        os.unlink(src)


def main(argv):
    quality, lossless, keep, files = PAINTING, False, False, []
    it = iter(argv)
    for a in it:
        if a == '--quality':
            quality = int(next(it))
        elif a == '--lossless':
            lossless = True
        elif a == '--keep':
            keep = True
        elif a == '--tree':
            root = next(it)
            for d, _, names in os.walk(root):
                files += [os.path.join(d, n) for n in sorted(names)
                          if n.endswith('.png')]
        else:
            files.append(a)
    before = after = 0
    for f in files:
        dest = os.path.splitext(f)[0] + '.webp'
        before += os.path.getsize(f)
        convert(f, dest, quality, lossless)
        after += os.path.getsize(dest)
        if not keep:
            os.unlink(f)
    print('  %d files, %.1f MB -> %.1f MB'
          % (len(files), before / 1e6, after / 1e6), file=sys.stderr)
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
