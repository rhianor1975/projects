#!/usr/bin/env python3
"""Turn the Oracle of the Void records into the engine's card tables.

    tools/build-cards.py [ERA...]     -> data/ERA/cards.tsv, and a coverage report

One row per card legal in the era.  Stats come straight from the Oracle,
which was checked against scans and had them right; this only cleans
them up (a printed "-" Honor Requirement stays "-", it is not 0).

The rules text is read into abilities the engine can run.  Each ability
is either understood -- in which case the engine applies it, for the
person and for the machine alike -- or kept as text for the person to
carry out by hand with the table commands.  The `auto` column says which
a card is: "all" when every line of its text was understood, "some", or
"none".  The report at the end says how many of each, per era, and the
most common lines that were not understood, which is the list to work
down when widening the parser.

Ability encoding, one per ';':   TIMING|COSTS|TARGET|EFFECTS
    TIMING   battle limited open enter produce static
    COSTS    comma list: bow (this card)  gold:N  honor:N (lose)  destroy (this card)
    TARGET   none  self  opers (your Personality)  epers (enemy Personality)
             efol (enemy Follower)  ecard (enemy Personality or Follower)
             eunit  ounit  eholding
    EFFECTS  comma list: force:N  chi:N  destroy  bow  straighten  home
             gain:N (honor)  lose:N (honor)  olose:N (each opponent)
             draw:N  produce:N  ranged:N  melee:N  fear:N
             attforce:N  defforce:N   (static, while attacking/defending)
"""
import html
import importlib.util
import json
import os
import re
import sys
from collections import Counter

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
_spec = importlib.util.spec_from_file_location("ootv", os.path.join(HERE, "import-ootv.py"))
ootv = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(ootv)

COLUMNS = ["id", "name", "type", "deck", "clan", "keywords", "cost", "force", "chi",
           "hreq", "ph", "focus", "gold", "pstr", "shonor", "auto", "fx", "text"]

TYPES = {"Personality", "Holding", "Event", "Region", "Stronghold", "Follower",
         "Item", "Strategy", "Spell", "Ring", "Ancestor", "Sensei", "Celestial", "Wind"}


def first(card, key, default=""):
    v = card.get(key)
    if isinstance(v, list):
        v = v[0] if v else default
    return default if v is None else v


def num(card, key):
    """The Oracle stores numbers as strings, with "+2" on Items and "-"
    for none.  Returns the string the engine reads: an integer, or "-"."""
    v = str(first(card, key, "")).strip()
    if v in ("", "-", "—", "*", "X"):
        return "-" if v in ("-", "—") else "0"
    m = re.match(r"^\+?(-?\d+)", v)
    return m.group(1) if m else "0"


def plain(s):
    s = re.sub(r"<br\s*/?>", "\n", s or "")
    s = re.sub(r"<[^>]+>", "", s)
    s = html.unescape(s).replace(" ", " ")
    return s


def lines_of(card):
    """Rules text split into ability lines, reminder text in brackets
    dropped -- it explains a keyword the engine handles by the keyword."""
    out = []
    for ln in plain(first(card, "text")).split("\n"):
        ln = re.sub(r"\([^)]*\)", "", ln).strip()
        ln = re.sub(r"\s+", " ", ln)
        if ln:
            out.append(ln)
    return out


# -------------------------------------------------------------- the parser
#
# Each rule is a regex over one normalised line, and a function from its
# match to an encoded ability.  Lines are normalised first: the bow icon
# becomes BOW, gold icons :gN: become "N Gold", and a leading run of
# keyword words before the timing ("Fire Battle", "Political Limited") is
# dropped -- the engine reads keywords from the card, not the line.

TIMINGS = r"(Battle/Open|Battle/Engage|Battle|Limited|Open|Engage)"
LEAD = r"^(?:(?:[A-Z][a-z]+|Repeatable|Tireless|Unstoppable|Absent) )*"


def norm(line, name=""):
    line = line.replace(":bow:", "BOW")
    if name:
        for n in sorted({name, name.split()[-1]}, key=len, reverse=True):
            if len(n) > 2:
                line = re.sub(r"\b%s\b" % re.escape(n), "this card", line)
    line = re.sub(r"\bthis (?:Personality|Shugenja|Follower|Item|Holding|Courtier|Monk|Samurai)\b",
                  "this card", line)
    line = re.sub(r":g(\d+):", r"\1 Gold", line)
    line = line.replace("a card", "1 card").replace("an additional card", "1 card")
    return line


def cost_of(s):
    if not s:
        return []
    out = []
    for part in [p.strip() for p in s.split(",")]:
        if part == "BOW":
            out.append("bow")
        elif re.fullmatch(r"(\d+) Gold", part):
            out.append("gold:" + re.fullmatch(r"(\d+) Gold", part).group(1))
        else:
            return None
    return out


TARGETS = [
    (r"(?:a |one of )?(?:your )?target (?:unbowed )?Personality you control|your target (?:unbowed )?Personality|target (?:one of )?your (?:unbowed )?Personality|your (?:unbowed )?target Personality", "opers"),
    (r"a target enemy Personality|target enemy Personality|an enemy target Personality", "epers"),
    (r"a target enemy Follower|target enemy Follower", "efol"),
    (r"a target enemy card|target enemy card", "ecard"),
    (r"a target enemy unit|target enemy unit", "eunit"),
    (r"a target enemy Holding|target enemy Holding", "eholding"),
    (r"(?:your )?target unit you control|your target unit", "ounit"),
    (r"a target Personality|target Personality", "apers"),
    (r"a target Follower|target Follower", "afol"),
    (r"this card", "self"),
]


def target_of(s):
    for pat, code in TARGETS:
        if re.fullmatch(pat, s.strip()):
            return code
    return None


def effect_clause(s):
    """One effect sentence with its target phrase -> (target, [effects])."""
    s = s.strip().rstrip(".")
    s = re.sub(r"^(?:a|make a) ", "", s)
    m = re.fullmatch(r"Ranged (\d+) Attack|Ranged Attack (\d+)", s)
    if m:
        return "none", ["ranged:" + (m.group(1) or m.group(2))]
    m = re.fullmatch(r"Melee (\d+) Attack|Melee Attack (\d+)", s)
    if m:
        return "none", ["melee:" + (m.group(1) or m.group(2))]
    m = re.fullmatch(r"Fear (\d+)", s)
    if m:
        return "none", ["fear:" + m.group(1)]
    m = re.fullmatch(r"Produce (\d+) Gold", s)
    if m:
        return "none", ["produce:" + m.group(1)]
    m = re.fullmatch(r"Gain (\d+) Honor", s)
    if m:
        return "none", ["gain:" + m.group(1)]
    m = re.fullmatch(r"Lose (\d+) Honor", s)
    if m:
        return "none", ["lose:" + m.group(1)]
    m = re.fullmatch(r"Draw (\d+) cards?", s)
    if m:
        return "none", ["draw:" + m.group(1)]
    m = re.fullmatch(r"(?:Each|Every) (?:other player|opponent) loses (\d+) Honor", s)
    if m:
        return "none", ["olose:" + m.group(1)]
    m = re.fullmatch(r"Give (.+?) ([+-]\d+)F(?:/([+-]\d+)C)?", s)
    if m and target_of(m.group(1)):
        fx = ["force:" + str(int(m.group(2)))]
        if m.group(3):
            fx.append("chi:" + str(int(m.group(3))))
        return target_of(m.group(1)), fx
    m = re.fullmatch(r"Give (.+?) ([+-]\d+)C", s)
    if m and target_of(m.group(1)):
        return target_of(m.group(1)), ["chi:" + str(int(m.group(2)))]
    for verb, code in (("Destroy", "destroy"), ("Bow", "bow"), ("Straighten", "straighten")):
        m = re.fullmatch(verb + r" (.+)", s)
        if m and target_of(m.group(1)):
            return target_of(m.group(1)), [code]
    m = re.fullmatch(r"Move (.+?) home|Move home (.+)", s)
    if m and target_of(m.group(1) or m.group(2)):
        return target_of(m.group(1) or m.group(2)), ["home"]
    m = re.fullmatch(r"Send (.+?) home", s)
    if m and target_of(m.group(1)):
        return target_of(m.group(1)), ["home"]
    return None


def effects(s):
    """A run of effect sentences sharing one target.  '. ' and ' and '
    separate them; a second sentence with no target of its own inherits
    the first one's."""
    tgt, out = "none", []
    for part in re.split(r"\.\s+|, and |\band then\b", s.strip().rstrip(".")):
        r = effect_clause(part)
        if not r:
            return None
        t, fx = r
        if t != "none":
            if tgt != "none" and t != tgt:
                return None
            tgt = t
        out += fx
    return tgt, out


def parse_line(line, ctype, name=""):
    s = norm(line, name)
    # :bow:: Produce N Gold.  The commonest line in the game.
    m = re.fullmatch(r"BOW: Produce (\d+) Gold\.?", s)
    if m:
        return "produce|bow|none|produce:" + m.group(1)
    # Timed actions: "[Keywords] Battle[, costs]: effects"
    m = re.fullmatch(LEAD + TIMINGS + r"(?:, ([^:]+))?: (.+)", s)
    if m:
        timing = {"Engage": "battle", "Battle/Engage": "battle",
                  "Battle/Open": "open"}.get(m.group(1), m.group(1).lower())
        costs = cost_of(m.group(2))
        body = m.group(3)
        # "Bow this card to ..." / "Destroy this card: ..." as a cost inside the body
        mm = re.match(r"(Bow|Destroy) this card(?: to make| to |: |\. )(.+)", body)
        if mm and costs is not None:
            costs = costs + ["bow" if mm.group(1) == "Bow" else "destroy"]
            body = mm.group(2)
            body = body[0].upper() + body[1:]
        if costs is None:
            return None
        e = effects(body)
        if not e:
            return None
        return "%s|%s|%s|%s" % (timing, ",".join(costs), e[0], ",".join(e[1]))
    # After this card enters play: effects
    m = re.fullmatch(r"After this (?:card|Personality|Follower|Holding|Item) enters play(?: from your hand)?, (gain|lose) (\d+) Honor\.?", s)
    if m:
        return "enter||none|%s:%s" % (m.group(1), m.group(2))
    m = re.fullmatch(r"After this (?:card|Personality|Follower|Holding|Item) enters play(?: from your hand)?: (.+)", s)
    if m:
        e = effects(m.group(1))
        if e and e[0] == "none":
            return "enter||none|" + ",".join(e[1])
        return None
    # Events: "After you reveal cards in your Provinces at the start of
    # your turn, if this Event is face-up in one of them, <effects>"
    m = re.fullmatch(r"After you reveal cards in your Provinces at the start of your turn, if this Event is face-up in one of them, (.+)", s)
    if m:
        body = m.group(1)
        body = body[0].upper() + body[1:]
        e = effects(body)
        return ("reveal||%s|%s" % (e[0], ",".join(e[1]))) if e else None
    m = re.fullmatch(r"(?:Will|Can) only attach to an? (.+?)\.?", s)
    if m:
        return "static||none|attachonly:" + m.group(1).replace(",", " ")
    m = re.fullmatch(r"This Province has ([+-]\d+) strength\.?", s)
    if m:
        return "static||none|pstr:%d" % int(m.group(1))
    m = re.fullmatch(r"This card has ([+-]\d+)PH\.?", s)
    if m:
        return "static||self|ph:%d" % int(m.group(1))
    m = re.fullmatch(r"This card has ([A-Z][a-z]+)\.?", s)
    if m:
        return "static||self|kw:" + m.group(1)
    m = re.fullmatch(r"(?:This (?:card|Holding) enters|Enters) play for (\d+) less Gold if you are an? (.+?) player\.?", s)
    if m:
        return "static||none|discount:%s:%s" % (m.group(1), m.group(2).replace(",", " "))
    m = re.fullmatch(r"(?:This card does not|Does not) count towards? an Enlightenment Victory\.?", s)
    if m:
        return "static||none|noenlighten"
    # Statics on the card itself
    m = re.fullmatch(r"This (?:card|Personality|Follower|unit) has ([+-]\d+)F while (attacking|defending)\.?", s)
    if m:
        return "static||self|%s:%d" % ("attforce" if m.group(2) == "attacking" else "defforce", int(m.group(1)))
    return None


# Lines that change nothing the engine models and are safe to drop:
# deck-construction notes, uniqueness reminders.
IGNORE = [
    r".*for deck construction.*",
    r"This card is considered .*",
]


def abilities(card):
    """-> (encoded abilities, understood count, total count, misses)"""
    enc, ok, total, miss = [], 0, 0, []
    for ln in lines_of(card):
        if any(re.fullmatch(p, ln) for p in IGNORE):
            continue
        total += 1
        a = parse_line(ln, first(card, "type"), plain(first(card, "title")).strip())
        if a:
            enc.append(a)
            ok += 1
        else:
            miss.append(ln)
    return enc, ok, total, miss


def stronghold_stat(card, key):
    """Ivory's double-sided Strongholds keep their numbers on a printing,
    not the card, and in the Personality fields: Force is province
    strength, cost is gold production, Personal Honor is starting honor.
    Read there when the card itself has none."""
    if first(card, key):
        return num(card, key)
    alt = {"strength": "force", "production": "cost", "startinghonor": "personalhonor"}[key]
    for p in card.get("printing", []):
        v = p.get(alt)
        if v and str(v[0]).strip() not in ("", "-"):
            return num({alt: v}, alt)
    return "0"


def production(card, enc):
    for a in enc:
        if a.startswith("produce|"):
            return a.rsplit(":", 1)[1]
    return num(card, "production") if first(card, "production") else "0"


def row(card):
    ctype = first(card, "type")
    enc, ok, total, miss = abilities(card)
    auto = "all" if ok == total else ("some" if ok else "none")
    kws = card.get("keywords") or []
    kws = [plain(k).strip() for k in kws if plain(k).strip()]
    clan = first(card, "clan", "Unaligned")
    text = plain(first(card, "text")).replace("\t", " ").replace("\n", " | ")
    return {
        "id": str(card["cardid"]),
        "name": plain(first(card, "title")).strip(),
        "type": ctype,
        "deck": first(card, "deck", ""),
        "clan": plain(clan).strip(),
        "keywords": ",".join(kws),
        "cost": num(card, "cost"),
        "force": num(card, "force"),
        "chi": num(card, "chi"),
        "hreq": num(card, "honor"),
        "ph": num(card, "ph"),
        "focus": num(card, "focus"),
        "gold": (stronghold_stat(card, "production") if ctype == "Stronghold"
                 else production(card, enc) if ctype == "Holding" else "0"),
        "pstr": stronghold_stat(card, "strength") if ctype == "Stronghold" else num(card, "strength"),
        "shonor": stronghold_stat(card, "startinghonor") if ctype == "Stronghold" else "0",
        "auto": auto,
        "fx": ";".join(enc),
        "text": text,
    }, miss


def build(eras):
    cards = ootv.load()
    for era in eras:
        out_dir = os.path.join(ROOT, "data", era)
        os.makedirs(out_dir, exist_ok=True)
        rows, missed = [], Counter()
        for c in cards:
            if not ootv.in_era(c, era) or first(c, "type") not in TYPES:
                continue
            r, miss = row(c)
            rows.append(r)
            for ln in miss:
                missed[re.sub(r"\d+", "N", ln)[:110]] += 1
        with open(os.path.join(out_dir, "cards.tsv"), "w") as f:
            f.write("\t".join(COLUMNS) + "\n")
            for r in rows:
                f.write("\t".join(r[k] for k in COLUMNS) + "\n")
        n = Counter(r["auto"] for r in rows)
        print("%-10s %5d cards: %5d fully automated, %5d partly, %5d by hand"
              % (era, len(rows), n["all"], n["some"], n["none"]))
        if "-v" in sys.argv:
            for k, v in missed.most_common(40):
                print("   %4d  %s" % (v, k))


if __name__ == "__main__":
    eras = [a for a in sys.argv[1:] if not a.startswith("-")] or list(ootv.ERAS)
    build(eras)
