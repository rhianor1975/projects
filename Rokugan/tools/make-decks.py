#!/usr/bin/env python3
"""Build a starter deck for every clan in every era.

    tools/make-decks.py [ERA...]    -> decks/ERA/CLAN.txt

Each deck is 40 Dynasty and 40 Fate cards and a Stronghold, at most three
copies of a card and one of a Unique.  It prefers cards the engine runs
by itself (the `auto` column of data/ERA/cards.tsv), because the machine
opponent can only use those; as fx/ERA.tsv grows, rerunning this builds
better decks from the same pool.

The lists hold card names only, so they are committed: a clone can see
what each clan plays before it has imported anything.
"""
import csv
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CLANS = ["Crab", "Crane", "Dragon", "Lion", "Phoenix", "Scorpion", "Unicorn",
         "Mantis", "Spider"]
NEUTRAL = ("Unaligned", "")


def encoded():
    """Oracle id -> hand-written encoding from fx/cards.tsv: the engine
    runs those, whatever the text reader made of them."""
    ids = {}
    path = os.path.join(ROOT, "fx", "cards.tsv")
    if os.path.exists(path):
        for line in open(path):
            if line[:1].isdigit():
                f = line.rstrip("\n").split("\t")
                ids[f[0]] = f[1]
    return ids


def performable(r, people):
    """Can this deck's Personalities perform the card?  A card that names
    a performing Monk or Shugenja is a dead card in a deck of bushi -- the
    first Crab decks held nine of them and never played a Strategy."""
    enc = FX.get(r["id"]) or r["fx"]
    need = re.findall(r"(?:bowperf|destroyperf|perf)=([A-Za-z/]+)", enc)
    if r["type"] == "Spell":
        need.append("Shugenja")
    for kws in need:
        if "any" in kws.split("/"):
            continue
        if not any(k in p["keywords"] or k == p["clan"] for p in people for k in kws.split("/")):
            return False
    return True


def load(era):
    path = os.path.join(ROOT, "data", era, "cards.tsv")
    with open(path) as f:
        rows = list(csv.DictReader(f, delimiter="\t", quoting=csv.QUOTE_NONE))
    for r in rows:
        for k in ("cost", "force", "chi", "ph", "focus", "gold", "pstr", "shonor"):
            try:
                r[k] = int(r[k])
            except ValueError:
                r[k] = 0
        r["unique"] = "Unique" in r["keywords"]
        r["auto"] = r["auto"] == "all" or r["id"] in FX
        if r["id"] in FX:
            r["fx"] = r["fx"] or "hand-written"
    return rows


def take(pool, want, score, limit=3):
    """Pick cards by score, copies up to the limit, until `want` cards."""
    out = []
    for r in sorted(pool, key=score, reverse=True):
        n = 1 if r["unique"] else limit
        for _ in range(n):
            if len(out) >= want:
                return out
            out.append(r["name"])
    return out


def hr_ok(r, start):
    """Recruitable within a few turns of the start: a Personality whose
    Honor Requirement the clan cannot reach is a dead card, and the first
    batch of decks lost Crane games to exactly that."""
    return r["hreq"] == "-" or int(r["hreq"] or 0) <= start + 3


def deck(rows, clan):
    shs = [r for r in rows if r["type"] == "Stronghold" and r["clan"] == clan]
    if not shs:
        return None
    sh = max(shs, key=lambda r: (r["gold"] * 2 + r["pstr"] + r["shonor"] * 0.3 + r["auto"] * 3))
    start = sh["shonor"]
    mine = lambda r: r["clan"] == clan
    neutral = lambda r: r["clan"] in NEUTRAL

    pers = [r for r in rows if r["type"] == "Personality" and (mine(r) or neutral(r))
            and 0 < r["cost"] <= 13 and hr_ok(r, start)]
    p_score = lambda r: ((r["force"] * 1.2 + r["chi"] * 0.3 + r["ph"] * 0.4 + 1)
                         / max(r["cost"] - (2 if mine(r) else 0), 1)
                         + (0.6 if r["auto"] else 0) + (0.3 if mine(r) else 0))
    dyn = take(pers, 18, p_score)

    hold = [r for r in rows if r["type"] == "Holding" and r["gold"] > 0
            and (mine(r) or neutral(r)) and r["cost"] <= 8]
    h_score = lambda r: r["gold"] / max(r["cost"], 1) + (0.4 if r["auto"] else 0) + r["gold"] * 0.05
    dyn += take(hold, 20, h_score)

    extra = [r for r in rows if r["type"] in ("Event", "Region") and r["auto"]
             and (mine(r) or neutral(r))]
    dyn += take(extra, 2, lambda r: r["auto"], limit=1)
    if len(dyn) < 40:
        dyn += take([r for r in pers if r["name"] not in dyn], 40 - len(dyn), p_score)

    fol = [r for r in rows if r["type"] == "Follower" and (mine(r) or neutral(r))
           and 0 < r["cost"] <= 6 and r["force"] > 0]
    fate = take(fol, 10, lambda r: r["force"] / max(r["cost"], 1) + (0.5 if r["auto"] else 0))
    items = [r for r in rows if r["type"] == "Item" and (mine(r) or neutral(r))
             and 0 < r["cost"] <= 5 and r["force"] > 0]
    fate += take(items, 6, lambda r: (r["force"] + r["chi"] * 0.3) / max(r["cost"], 1)
                 + (0.5 if r["auto"] else 0))
    people = [r for r in pers if r["name"] in dyn]
    acts = [r for r in rows if r["type"] in ("Strategy", "Spell")
            and (mine(r) or neutral(r)) and r["cost"] <= 4 and performable(r, people)]
    fate += take([r for r in acts if r["auto"] and r["fx"]], 40 - len(fate),
                 lambda r: r["focus"] + (1 if r["type"] == "Strategy" else 0))
    if len(fate) < 40:
        # Not enough the engine can run: the rest are played by hand, and
        # chosen for their Focus value, which a duel uses either way.
        fate += take([r for r in acts if r["name"] not in fate], 40 - len(fate),
                     lambda r: r["focus"])
    return sh["name"], dyn[:40], fate[:40]


def write(era, clan, built):
    sh, dyn, fate = built
    out = os.path.join(ROOT, "decks", era)
    os.makedirs(out, exist_ok=True)

    def counted(names):
        seen = {}
        for n in names:
            seen[n] = seen.get(n, 0) + 1
        return ["%d %s" % (c, n) for n, c in seen.items()]

    with open(os.path.join(out, clan + ".txt"), "w") as f:
        f.write("# %s, %s era -- built by tools/make-decks.py\n" % (clan, era))
        f.write("# Stronghold\n%s\n" % sh)
        f.write("# Dynasty (%d)\n%s\n" % (len(dyn), "\n".join(counted(dyn))))
        f.write("# Fate (%d)\n%s\n" % (len(fate), "\n".join(counted(fate))))


FX = {}


def main():
    global FX
    FX = encoded()
    eras = sys.argv[1:] or ["gold", "celestial", "ivory"]
    for era in eras:
        rows = load(era)
        made = []
        for clan in CLANS:
            b = deck(rows, clan)
            if b:
                write(era, clan, b)
                made.append("%s (%d/%d)" % (clan, len(b[1]), len(b[2])))
        print(era + ":", ", ".join(made))


if __name__ == "__main__":
    main()
