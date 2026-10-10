# Rokugan

The Legend of the Five Rings collectible card game, 1995–2015, against a
machine opponent. A C engine that knows the rules, and a Godot client
with two skins: **Classic**, the 1997 Windows window with rebuilt
parchment cards, and **Modern**, dark lacquer with portraits, ring
tokens and the log down the side.

The player picks the **era** when a game starts. An era is one of the
Oracle of the Void's legality arcs, and it decides the card pool:

| Era | Arc | Cards |
|---|---|---|
| Gold | Four Winds (Gold Edition) | 1,899 |
| Celestial | Destroyer War (Celestial Edition) | 2,183 |
| Ivory | A Brother's Destiny (Ivory Edition) | 1,334 |

Rules that changed between arcs are fields of `Era` in `src/data.c`,
never a test of the era's name somewhere in the rules.

## Getting the cards

The card text and images are AEG's and this repository is public, so
neither is in it. `make setup` fetches them from the
[Oracle of the Void](https://oracleofthevoid.com) into `data/`, which is
gitignored:

    make setup      # cards (a few minutes), then images (longer)
    make            # the engine
    godot --path client

`tools/import-ootv.py images` resumes where it stopped. The images are
only for the client; the engine needs `data/ERA/cards.tsv` alone.

Why the Oracle and not CardGameGeek: both were checked against card
scans. CardGameGeek zeroes Force on most Personalities and stores a
printed "–" Honor Requirement as 0; the Oracle had every one right, and
records legality per arc, which is what an era is.

## What the engine runs, and what you run by hand

`tools/build-cards.py` reads each card's text into abilities the engine
can apply — gold production, Ranged/Melee Attacks and Fear, ±Force, bow,
straighten, destroy, move home, honor, card draw — and reports how many
cards it understood completely:

| Era | Fully automated | Partly | By hand |
|---|---|---|---|
| Gold | 75 | 249 | 1,575 |
| Celestial | 122 | 210 | 1,851 |
| Ivory | 143 | 202 | 989 |

L5R text is a long tail of one-off wording, so pattern matching levels
off there. The rest is written by hand, in this program's own notation,
in `fx/ERA.tsv` — card names and encodings only, so it is committed.
Every line there replaces what the reader made of that card.

A card the engine cannot run is still playable. It is marked 手 on the
table; play it from its menu, read its text, and carry it out with the
**table commands** on the right-click menu: bow, straighten, destroy,
send home, ±1 Force, ±1 honor, draw. The machine opponent only uses
what the engine can run, which is why its decks prefer those cards.

## Decks

`tools/make-decks.py` builds a 40-Dynasty, 40-Fate deck and a Stronghold
for each clan in each era, preferring automated cards and Personalities
the clan's starting honor can recruit. The lists are names only, in
`decks/ERA/CLAN.txt`. Rerun it as `fx/` grows.

## The rules as built

Straighten, Events (face-down Province cards turn up; Events resolve,
Regions attach), Action (Open and Limited actions alternate until both
pass), Attack (assign, defend, battle actions with the defender first,
resolution), Dynasty (recruit with gold from bowing Holdings, chosen to
waste least), End (draw one, discard to eight).

Resolution: the lower-Force army is destroyed, both on a tie; the
Province falls if the attacker beats the defenders by more than its
Strength. The attacker gains 2 honor per card destroyed by resolution.
Surviving attackers go home bowed.

Victory: 40 Family Honor at the start of your turn, all four enemy
Provinces destroyed, or five Rings in play. Defeat at −20.

House rules, marked in the code: an empty deck reshuffles its discard
pile at no cost (the editions disagree); on your first turn you may
discard face-up Province cards; Rings are played when you judge their
condition met, because their conditions are one-offs.

## Testing

    make check                 200 machine games per era, invariants armed
    ./rokugan --era ivory --games 500
    ROKUGAN_TRACE=- ./rokugan --era gold --clan0 Crab --clan1 Crane --seed 3

The client takes test arguments after `--`:

    godot --path client -- --shot=out.png --autoplay=40 --skin=modern

## Not yet

- Reactions and Interrupts ("after X happens…"): no trigger system yet,
  so those are by hand.
- Duels: by hand.
- The machine defends only when it can win or must save a Province, so
  games run short — about eight turns, mostly Military victories.
- `fx/` is empty: the hand encodings are the next step.
