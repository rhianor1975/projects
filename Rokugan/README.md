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
straighten, destroy, move home, honor, card draw. L5R text is a long
tail of one-off wording, so pattern matching levels off quickly; the
rest is written by hand in `fx/cards.tsv`, in this program's notation,
by Oracle card id — one file for every era, since a card legal in
several arcs is one record. Each line's note says where the notation
falls short of the printed card.

The notation covers conditions on a target (attacking, defending,
opposed, bowed, keyword, Force/Chi/Personal Honor limits, lower Chi than
the performer), a performing Monk, Shugenja or Courtier as a cost,
discards as a cost, permanent bonuses, bowing a unit, bringing a unit
into the battle, attacks whose strength is the performer's Chi, Province
Strength until the turn ends, and duels.

| Era | Cards | The engine runs | Of them, Strategies and Spells |
|---|---|---|---|
| Gold | 1,899 | 118 | 37 of 585 |
| Celestial | 2,183 | 177 | 44 of 801 |
| Ivory | 1,334 | 196 | 45 of 387 |

The starter decks are built from these, so a game against the machine
plays itself: every Strategy and Spell in them is one the engine runs.

A card the engine cannot run is still playable. It is marked 手 on the
table; play it from its menu, read its text, and carry it out with the
**table commands** on the right-click menu: bow, straighten, destroy,
send home, ±1 Force, ±1 honor, draw. The machine opponent only uses
what the engine can run.

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

Spells changed with Lotus: to Gold they are cast from the hand and
gone; in Celestial and Ivory a Shugenja equips them and uses them again.

House rules, marked in the code: an empty deck reshuffles its discard
pile at no cost (the editions disagree); on your first turn you may
discard face-up Province cards; Rings are played when you judge their
condition met, because their conditions are one-offs; in a duel each
side focuses the top card of its Fate deck rather than choosing from
the hand; a discard paid as a cost is chosen at random.

## Testing

    make check                 200 machine games per era, invariants armed
    ./rokugan --era ivory --games 500
    ROKUGAN_TRACE=- ./rokugan --era gold --clan0 Crab --clan1 Crane --seed 3

The client takes test arguments after `--`:

    godot --path client -- --shot=out.png --autoplay=40 --skin=modern

## Not yet

- Reactions and Interrupts ("after X happens…"): no trigger system yet,
  so those are by hand.
- Duels are focused from the top of the deck, not chosen from the hand.
- Terrains, tokens, Kharmic, the Imperial Favor and Winds.
- The machine plays to break Provinces and defends what it can; games
  run eight or nine turns, mostly Military victories.
