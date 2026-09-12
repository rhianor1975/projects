# djarhun-c

Djarhun — *The Quest for the Book of Avrakar* — as a C/ncurses console
game, built from the Renegade Version print-and-play set.

> Djarhun is a world in peril. The evil wizard Gharad has stolen the Book of
> Avrakar... You will travel Djarhun and strengthen yourself enough to face
> the horrors of the Abyss.

Build with `make`, play with `./djarhun`. It wants a terminal of at least
100×30.

## The sources were unusually kind

`Djarhun_Rules.pdf` carries **real text**, not scanned images — the first
time in these three projects that `pdftotext` has just worked. All 21 pages
are in `RULES-djarhun.txt`, and every rule quoted in the source comes from
it verbatim.

The reference sheet gave the three tables the whole game turns on:

| Strength | 1-2 | 3-5 | 6-9 | 10-13 | 14-19 | 20+ |
|---|---|---|---|---|---|---|
| **Items carried** | 3 | 4 | 5 | 6 | 7 | 8 |

| Speed | 1-3 | 4-9 | 10-15 | 16-21 | 22+ |
|---|---|---|---|---|---|
| **Movement die** | d4 | d6 | d8 | d10 | d12 |

| Sorcery | 3-5 | 6-9 | 10-16 | 17+ |
|---|---|---|---|---|
| **Spells held** | 1 | 2 | 3 | 4 |

Djarhun is not a d6 game — "you will need a d4, d6, d8, d10, d12 & d20" —
and those tables are why the statistics matter: Strength is what you can
carry, Speed is how far you move, Sorcery is how much magic you hold.

Levels run 0 to 10 on a printed XP ladder — 7, 21, 42, 70, 105, 147, 196,
252, 315, 385 — which is exactly `7·n(n+1)/2`, so `xp_for_level()` computes
it rather than tabulating it. Each level grants 3 points, "no more than 2
points into a single statistic".

Battle is simple and brutal: both sides roll a d8 and add a statistic, the
Foe's own highest deciding whether it is Strength or Sorcery. Ties end your
turn. A protective Item buys a d20 defence roll, surviving on 18 or better.

## The board

Four concentric rings with the Abyss at the middle, transcribed from
`_board_main.jpg` (5411×4091) and `_board_urthe.jpg`:

```
   Frostburn   38 spaces   the frozen outer edge
   Tar'ri      40          the ocean; you need a ship
   Durach      32          the heartland: Elidor and the Gypsy Camp
   Aldun       14          the desert
   The Abyss    9          a one-way spiral to Gharad's Tower
   Urthe       32          Durach a thousand years on, reached by time travel
```

The crossings are all named in the rules: Aldun↔Durach between the Wolfbane
Hills and the Nesta Badlands, Frostburn↔Durach by the Mage at Springvale or
the Sorcerer at Glacial Hills, the Abyss by the Oasis of Ezrabar, and Urthe
home again by bringing Propha some Ancient Bones.

Rings are walked either way; the Abyss is a `TOPO_SPIRAL`, one space a turn
inwards, "until they reach Gharad's tower" — and back out again the same
way, which matters more than it sounds (see below).

## Three views, and the player picks

The board is far bigger than Talisman's: 38-space rings do not fit a
terminal at a readable width. Rather than choose for you, all three ways of
drawing it are built and `[v]` cycles between them mid-game. The rules never
see the difference.

1. **The whole ring**, eight characters a cell. You see the shape of the
   land; "Demonblood Mountains" becomes `Demonblo`.
2. **A viewport** — seven spaces at eleven characters, centred on whoever is
   playing, with a position bar showing where in the ring you are, and an
   ELSEWHERE panel for everyone you cannot see. A d12 moves you twelve, so
   seven spaces is most of a turn either way. *This is the default.*
3. **Ribbons** — every land at once, one character a space. Nothing hidden,
   nothing legible; the line underneath names where you actually stand.
4. **Chart and inspection panel** — *the default*, and Rhianor's idea. The
   other three all fight the same losing battle: the shape of a land and the
   name of a space cannot share a cell. Eleven characters of "Demonblood"
   leaves no room for the ring; a ring of four-character cells leaves no
   room for the name. So stop putting them in the same place. The chart
   carries the shape in two characters a space, and a panel down the right
   spells out whatever you are standing on — its name, its land, what it
   makes you draw, and every card lying on it with the numbers you will have
   to beat.

```
  Durach                                                     INSPECTION
   [>>] [..] [..] [..] [..] [..] [..] [..] [Mk]
   [..]                                    [Mk]             Space 27 of 32
   [..]                                    [..]             Witchcall
   [..]                                    [..]             Durach
   [..]                                    [@1]
   [@3]                                    [..]             Draw 1 card
   [..]                                    [..]
   [..] [>>] [..] [..] [..] [..] [..] [..] [..]             HERE:
                                                             Ogre
                                                              Str 6 / Sor 0
```

`..` empty, `!!` a Foe, `$$` treasure, `**` both, `Mk` a town, `>>` a way
into another land, `Lk` the Lake of Tears, `G!` Gharad, `@n` a hero.

A perimeter of n cells satisfies `cols + rows = (n + 4) / 2`, so the shape
is a single choice — and height is what binds, since there are only about
nine rows between the title and the roster. Pick the rows first and let the
width follow: Frostburn's 38 spaces come out as 12 by 9, inside 100 columns.

## What the soak found

Four bugs, and each one was invisible until the games were counted.

**Nobody could leave the Abyss.** The spiral only ran inwards, so a hero who
reached Gharad's Tower stayed there for the rest of the game. The Book was
taken 245 times across 20 games and carried home **none** of them. The rules
say plainly that a hero "may choose to head back to the Lake of Tears",
moving in the opposite direction and ignoring the spaces on the way.

**The Book ceased to exist when its carrier died.** The rules are explicit —
"it drops onto the space they died to wait for someone to claim it" — and I
had simply cleared the flag. Any game in which a carrier died was
unwinnable from that moment, silently. It is now dropped where they fell and
picked up by whoever lands there: 15 recoveries across 50 games.

**Dying stripped the statistic you had been building.** Losing a level costs
3 points, and I deducted them Strength-first. A hero who died a few times
ended at Level 9 with Speed 21 and Strength 16 — useless against Gharad. The
points now come off whatever she does *not* fight with.

**The AI spread its points and so never grew.** Three points a level across
three statistics tops out near 13 in everything by Level 10. Gharad has
Strength 20 *and* Sorcery 20. Heroes were reaching Level 9 and still never
entering the Abyss, because they never could have won. The rules permit 2
points into one statistic per level, which reaches the low twenties — so the
AI specialises, and the game is telling you that you must too.

Together those took the game from **0 wins in 20** to **48 in 50**.

## A minimum level per land

The AI first used a single "strong enough for Gharad" number, which was both
a magic constant and the wrong shape — the lands are not equally dangerous,
and what a hero needs to know is whether *this* one will kill her. Each land
now carries a `min_level` and the AI will not cross into one it is not ready
for:

```
   Durach 0    Frostburn 2    Tar'ri 3    Aldun 4    Urthe 6    Abyss 8
```

That closed the last stalls outright — 40/40 where a stat threshold gave
58/60 — and it doubles as the difficulty gradient the board always implied.
The Abyss gate is worth its own note: at 9 the games completed but ran long
(max 10449 turns), at 7 they went back to stalling, at **8** they finish in
845 turns on average with a worst case of 2086.

It gates nothing for a human. You may walk into the Abyss at Level 1 and
find out.

## The 54 Heroes

`hero_a.jpg`..`hero_l.jpg` are twelve sheets of six cards each — **72 Heroes**,
not the dozen I had assumed. `extract-heroes.py` slices and OCRs them,
`parse-heroes.py` produces `HEROES.tsv`, and the table in `data.c` is
generated from that. Names, Race, Home, Morality and the three statistics
are the printed values; each hero also carries the first of their numbered
Skills as flavour text.

71 of 72 cards parsed, 58 with every field intact, and **54 have a Home that
resolves onto the board**. The two that do not — Djarhun Cemetery and Dolyan
Grassland — are real spaces I did not transcribe.

Matching the Homes needed care. The board names here are abbreviated to fit a
13-character cell, so "Church of Gedwin" is stored as `Chrch Gedwin` and
"Wolfbane Hills" as `WolfbaneHlls`. Prefix matching found 32; comparing by
similarity found 54.

**The cards carry three Moralities, not two.** Dacre the Defender is *Kind*,
alongside Fair and Vile — 27 Fair, 15 Kind, 29 Vile across the set. Kind
sides with Elidor as Fair does; only the Vile take the Book to the Gypsies.
The races are wider than expected too: Human, Elf, Dwarf, Undead, Planar,
Halfling, Dragon, Construct, Troll, Goblin, Centaur.

### Two memory bugs the Heroes exposed

Installing them broke things that had been fine with eight:

* `int taken[32]`, the "already chosen" array in setup, was sized for the old
  table. With 54 heroes it read off the end of the stack — and had been doing
  so in **every soak before this**, which is why the 37/40 measured just
  before is not a number to trust. It is sized from `MAX_HERO_TEMPLATES` now.
* Space ids run to 164 once Urthe is counted (base 133, 32 spaces) and
  `MAX_SPACES` was 160, so the last five spaces wrote past the end of the
  card piles. The capacity is 256 now, and `board_check()` refuses to start
  if the land table ever outgrows it.

Both were invisible in play and both were caught by
`-fsanitize=address,undefined` in a few seconds.

## Choosing a smaller die

The rules say a hero "may also choose any dice lower than what they can
roll", which reads like a flourish and is not. A border has exactly one
crossing space on it, and reaching a particular space needs an exact
landing. A hero who can only throw her largest die orbits the ring for
hundreds of turns without ever stopping where she means to — one soaked game
reached Level 10, crossed 26 times, and never once landed on Wolfbane Hills,
the single space that opens into Aldun.

The AI now picks the smallest die that can still reach its goal. That one
rule took the game from **35/40 at 1972 turns to 39/40 at 768**.

## The real decks

All 79 sheets are read: **1,563 card faces, 1,340 of them named**, in
`CARDS-djarhun.tsv`. `extract-decks.py` slices and OCRs, `parse-decks.py`
turns that into the table, split as always so re-parsing never means
re-OCRing.

Three things came out of the cards that the placeholders could not have:

**The Foes carry their printed statistics.** 324 of them, and they are far
harder than the numbers I had invented: Bone Dragon Strength 23 and Sorcery
23, Ancient Wyrm 18, Gorge Demon 17, Naga 15. My placeholders topped out at
9. That is what makes Gharad at 20/20 the right final boss and ten levels
the right ladder -- the game was always pitched there, and the placeholder
deck had been quietly making it easy.

**The roman numeral is a rule.** Every card carries one at the top right,
and the rulebook explains it: when several share a space "the lowest number
is dealt with first, and so on, until they have all been dealt with". It is
parsed into `order`.

**Names are the flavour.** Tower of Lartem, Mystical Doorway, Tomb of Grug,
Shrine of Rejuvenation, Crypt of Brumat -- and henchmen with titles, Garban
the Giant Slayer, Wareg the Priest, Lura the Wizard.

### Two mistakes installing them

`DECK_MAX` was 64. With 577 generated cards, of which the shared ones go
into every land, each land's deck filled with shared Events before a single
Foe was reached -- **0 wins in 25 games, and not one Foe encountered**. The
soak said so immediately; nothing else would have.

And the real Foes exposed a gap the placeholders had hidden: this AI had no
space evaluation at all. It walked toward its goal and fought whatever it
landed on, which against Strength 23 is not a plan.

## The AI plays the odds

A Battle here is `stat + d8` against `stat + d8`, so the chance of winning
one is a sum over the 64 ways two dice fall -- there are `8 - |k|` ways to
roll a difference of k, for k in -7..7.

`space_risk()` scores what is lying on a space by those odds: a fight better
than 55% is worth walking into, one under 35% costs a Health -- and at one
Health it costs the game, so it is weighted double. `ai_direction()` then
weighs progress toward the goal against what is waiting at each end of the
move. Progress is worth something, but not a Health.

| | games finished | turns | fights won |
|---|---|---|---|
| real decks, no evaluation | 10/25 | 8441 | 9% |
| with the odds | **20/25** | **3211** | **24%** |

Losses fell from 128,960 to 23,208. 24% still sounds low, and it is honest:
you cannot decline a fight on your own space in Djarhun, only avoid landing
there, and a die roll gives limited control over that. The remaining five
unfinished games are the same story -- the per-land level gates were tuned
against a placeholder deck that was far too soft, and want re-tuning against
these numbers.

## Testing

`DJARHUN_AUTO=1` plays headless and prints a result line; `DJARHUN_SEED`
replays a game exactly; `DJARHUN_TRACE` / `--log` writes the full text log.

**60 seeded games with the real Heroes: 58 finish, average 949 turns.**
Sanitizers find nothing, and the build is clean under `-Wall -Wextra`.

The two that do not finish are the long tail of the same exact-landing
problem: with one crossing space per border and nothing but dice to reach
it, a hero can be unlucky for a very long time. The real answer is more ways
to travel — ships, the Master Wizard's teleport, the Spells that move you —
all of which are on the list below.

## Not yet built

The parts that are still this game's own rather than Djarhun's, and are
marked as such in the source:

* **Hero Skills.** All 54 Heroes carry their printed Skill text and none of
  it is implemented — Kran drinking blood to fortify Health, Dacre's Star of
  Elidor, Grimstrike bound to the Grimblade, Notira taking Planars as
  Henchmen. They are flavour on the card sheet for now.
* **Card effects.** All 79 sheets are extracted and 577 cards are in play
  with their real names, types and Foe statistics — but only the Foes do
  anything mechanical. The rules text on Places, Traps and Items is carried
  as flavour and not implemented, and the roman-numeral resolution order is
  parsed but not yet used.
* **Henchmen, Spells, Equipment slots.** The rules give eight equipment
  types, Henchmen who fight for you, and Spells with a Sorcery contest;
  none are implemented. `MAX_SPELLS` and `MAX_HENCH` exist and are unused.
* **Ranged battle** using Speed, with its seven-rule sub-system.
* **Ships**, and therefore the Tar'ri Ocean as anything but a ring you can
  already walk.
* **Bartering** — trading Items by Gem value rather than paying.
* **Urthe** is on the board and reachable in the data, but the time-travel
  ways in are not implemented.
* Several space rules are legible on the board scan and not yet coded — the
  Centaur Springs' d4, the Gypsy Camp's d8, Demonblood Mountains, the
  markets' price lists.
