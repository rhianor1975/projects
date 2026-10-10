#!/usr/bin/env python3
"""Import the L5R CCG card pool from the Oracle of the Void, locally.

    tools/import-ootv.py fetch          every L5R card -> data/ootv/cards.json
    tools/import-ootv.py arcs           list the legality arcs and their sizes
    tools/import-ootv.py images ERA...  card images for those eras -> data/ootv/images/

The Oracle (oracleofthevoid.com) is the community's card database, kept
by Don Eisele since AEG sold the game in 2015.  It was chosen over
CardGameGeek after checking both against scans: CardGameGeek zeroes Force
on most Personalities and stores a printed "-" Honor Requirement as 0;
the Oracle had every one of them right, and it carries legality per arc,
which is what an era is.

Nothing this writes is committed.  The text and images are AEG's and the
repository is public, so data/ is gitignored and a clone runs this once.
"""
import json
import os
import sys
import time
import urllib.request

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OOTV = os.path.join(ROOT, "data", "ootv")
API = "https://api.oracleofthevoid.com/oracle-fetch?table=l5r&cardid="
IMAGES = "https://images.oracleofthevoid.com/l5r/"
UA = {"User-Agent": "rokugan-import/1 (+https://github.com/rhianor1975/projects)"}
BATCH = 200

# An era is a set of the Oracle's legality arcs.  The player picks one
# when a game starts; a card is in the pool if it was legal in any of
# them.  Arc names are the Oracle's, with &nbsp; read as a space.
ERAS = {
    "gold":      ["Four Winds (Gold)"],
    "celestial": ["Destroyer War (Celestial)"],
    "ivory":     ["A Brother's Destiny (Ivory Edition)"],
}


def get(url, tries=4):
    for i in range(tries):
        try:
            req = urllib.request.Request(url, headers=UA)
            return urllib.request.urlopen(req, timeout=90).read()
        except Exception:
            if i == tries - 1:
                raise
            time.sleep(2 ** (i + 1))


def fetch():
    """Card ids are dense but not contiguous, and the end is not
    published, so this walks batches until five in a row come back
    empty."""
    os.makedirs(OOTV, exist_ok=True)
    out, start, empty = [], 1, 0
    while empty < 5:
        ids = ",".join(str(i) for i in range(start, start + BATCH))
        got = json.loads(get(API + ids))
        out += got
        empty = 0 if got else empty + 1
        start += BATCH
        print("\r  %d cards, ids to %d" % (len(out), start - 1), end="", flush=True)
        time.sleep(0.3)
    print()
    out.sort(key=lambda c: c["cardid"])
    with open(os.path.join(OOTV, "cards.json"), "w") as f:
        json.dump(out, f, ensure_ascii=False)
    print(len(out), "cards ->", os.path.join(OOTV, "cards.json"))


def load():
    path = os.path.join(OOTV, "cards.json")
    if not os.path.exists(path):
        sys.exit("no %s -- run: tools/import-ootv.py fetch" % path)
    with open(path) as f:
        return json.load(f)


def arcs_of(card):
    return [a.replace("&nbsp;", " ").strip() for a in card.get("legality", [])]


def in_era(card, era):
    want = ERAS[era]
    return any(a in want for a in arcs_of(card))


def arcs():
    count = {}
    for c in load():
        for a in arcs_of(c):
            count[a] = count.get(a, 0) + 1
    for a, n in sorted(count.items(), key=lambda kv: -kv[1]):
        print("%6d  %s" % (n, a))


def printing_for(card, era):
    """The printing whose image to use: the latest printing in a set the
    era's arcs cover is not knowable from the record, so take the
    primary printing -- the Oracle's own choice of face."""
    want = card.get("printingprimary", "1")
    for p in card.get("printing", []):
        if p.get("printingid") == want:
            return p
    return (card.get("printing") or [None])[0]


def image_url(card, p, size="details"):
    if p.get("imagehash") and p.get("image"):            # modern layout
        return IMAGES + p["imagehash"] + "/" + p["image"][0][size]
    if p.get("printimagehash"):                          # legacy layout
        return "%s%s/printing_%d_%s_%s.jpg" % (
            IMAGES, p["printimagehash"][0], card["cardid"], p["printingid"], size)
    return None


def images(eras):
    cards = load()
    idir = os.path.join(OOTV, "images")
    os.makedirs(idir, exist_ok=True)
    want = [c for c in cards if any(in_era(c, e) for e in eras)]
    got = none = fail = 0
    for c in want:
        path = os.path.join(idir, "%d.jpg" % c["cardid"])
        if os.path.exists(path):
            got += 1
            continue
        p = printing_for(c, eras[0])
        url = p and image_url(c, p)
        if not url:
            none += 1
            continue
        try:
            data = get(url)
            with open(path + ".tmp", "wb") as f:
                f.write(data)
            os.replace(path + ".tmp", path)
            got += 1
        except Exception as e:
            fail += 1
            print("  failed:", c["title"][0], e, flush=True)
        time.sleep(0.15)
        if (got + fail) % 50 == 0:
            print("\r  %d of %d" % (got + none + fail, len(want)), end="", flush=True)
    print("\n%d images, %d cards with none, %d failed" % (got, none, fail))


def main(argv):
    cmd = argv[1] if len(argv) > 1 else ""
    eras = argv[2:] or list(ERAS)
    for e in eras:
        if e not in ERAS:
            sys.exit("unknown era %r; one of %s" % (e, ", ".join(ERAS)))
    if cmd == "fetch":
        fetch()
    elif cmd == "arcs":
        arcs()
    elif cmd == "images":
        images(eras)
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main(sys.argv)
