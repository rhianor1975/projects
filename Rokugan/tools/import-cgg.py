#!/usr/bin/env python3
"""Import the L5R CCG card pool from CardGameGeek into data/, locally.

    tools/import-cgg.py fetch            every L5R record -> data/cgg/cards.json
    tools/import-cgg.py scans ERA...     card scans for those eras -> data/cgg/scans/
    tools/import-cgg.py build ERA...     -> data/ERA/cards.tsv and data/ERA/art/

ERA is gold, celestial or ivory (see ERAS below).

Nothing this writes is committed.  The card text and the scans are AEG's,
and the repository is public, so data/ is gitignored: a clone runs this
once to fill it, the same way Court of Treasons runs `make setup`.

Why the scans are read and not only the records: the database has the
right Chi, Gold cost, Honor Requirement and Personal Honor, but Force is
zeroed on most Personalities and Followers, and a printed "—" Honor
Requirement (none) is stored as 0 (a real requirement).  Six random
Personalities were checked against their scans before any of this was
written; the stats that disagreed were Force, every time.
"""
import json
import os
import sys
import time
import urllib.parse
import urllib.request

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DATA = os.path.join(ROOT, "data")
CGG = os.path.join(DATA, "cgg")
GAME = "635206a1-a610-4de8-a158-a8508787e48a"
SEARCH = "https://cardgamegeek.com/api/search/collections/cards/documents/search"
UA = {"User-Agent": "rokugan-import/1 (+https://github.com/rhianor1975/projects)"}

# Which printed sets make up each era's pool.  Base sets only for now:
# they are what a starter deck of the period was built from, and each is
# a single frame design, which is what the scan reader needs.  Jade
# Edition's records have no rules text at all, so its cards borrow the
# text of their nearest later printing -- see build.
ERAS = {
    "gold":      ["Gold Edition", "Jade Edition"],
    "celestial": ["Celestial Edition"],
    "ivory":     ["Ivory Edition"],
}


def get_json(url, tries=4):
    for i in range(tries):
        try:
            req = urllib.request.Request(url, headers=UA)
            return json.load(urllib.request.urlopen(req, timeout=60))
        except Exception:
            if i == tries - 1:
                raise
            time.sleep(2 ** (i + 1))


def fetch():
    """Every L5R record, all sets, without the image metadata blobs except
    the scan URL.  Typesense pages at 250."""
    os.makedirs(CGG, exist_ok=True)
    out, page = [], 1
    while True:
        q = urllib.parse.urlencode({
            "q": "*", "filter_by": "game:=" + GAME, "per_page": 250, "page": page,
            "exclude_fields": "artists,translations,variants,reprints",
        })
        d = get_json(SEARCH + "?" + q)
        for h in d["hits"]:
            doc = h["document"]
            img = doc.pop("image_front", None) or {}
            doc.pop("image_back", None)
            doc["scan"] = (img.get("urls") or {}).get("cdn", "")
            out.append(doc)
        if page == 1:
            print("found", d["found"], flush=True)
        if len(d["hits"]) < 250:
            break
        page += 1
        time.sleep(0.4)
    with open(os.path.join(CGG, "cards.json"), "w") as f:
        json.dump(out, f, ensure_ascii=False)
    print(len(out), "records ->", os.path.join(CGG, "cards.json"))


def load():
    path = os.path.join(CGG, "cards.json")
    if not os.path.exists(path):
        sys.exit("no %s -- run: tools/import-cgg.py fetch" % path)
    with open(path) as f:
        return json.load(f)


def era_records(cards, era):
    sets = ERAS[era]
    return [c for c in cards if c["facets"]["set"]["name"] in sets]


def scans(eras):
    cards = load()
    sdir = os.path.join(CGG, "scans")
    os.makedirs(sdir, exist_ok=True)
    want = [c for e in eras for c in era_records(cards, e)]
    got = miss = fail = 0
    for c in want:
        path = os.path.join(sdir, c["id"] + ".jpg")
        if os.path.exists(path):
            got += 1
            continue
        if not c.get("scan"):
            miss += 1
            continue
        try:
            req = urllib.request.Request(c["scan"], headers=UA)
            data = urllib.request.urlopen(req, timeout=60).read()
            with open(path + ".tmp", "wb") as f:
                f.write(data)
            os.replace(path + ".tmp", path)
            got += 1
        except Exception as e:
            fail += 1
            print("  failed:", c["name"], c["facets"]["set"]["name"], e, flush=True)
        time.sleep(0.15)
    print("%d scans, %d records with none, %d failed" % (got, miss, fail))


def main(argv):
    if len(argv) < 2 or argv[1] not in ("fetch", "scans", "build"):
        sys.exit(__doc__)
    eras = argv[2:] or list(ERAS)
    for e in eras:
        if e not in ERAS:
            sys.exit("unknown era %r; one of %s" % (e, ", ".join(ERAS)))
    if argv[1] == "fetch":
        fetch()
    elif argv[1] == "scans":
        scans(eras)
    else:
        import build_cards  # tools/build_cards.py, beside this file
        build_cards.build(load(), eras, ERAS, DATA, CGG)


if __name__ == "__main__":
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    main(sys.argv)
