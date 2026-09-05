# prophecy-c

A console **Prophecy** in C with ncurses: walk a ring of twenty spaces,
build a hero, and take four of the five Artifacts from the Astral Planes.

    make
    ./prophecy

Needs a terminal of at least **86x29**; the board is 85 columns wide, so
100x30 is comfortable at a large font.

## The board

The rulebook says the world is divided into **20 spaces**, and the
perimeter of a `6x6` grid is exactly `6*4-4 = 20`. So the ring is drawn as
the border of a 6x6 grid, and the 4x4 interior is the sea that holds the
five Astral Planes.

    +-------------+-------------+-------------+-------------+-------------+-------------+
    |Village      |Mountains    |Magic Tower  |Forest       |Plains       |Forest       |
    |@1          ~|             |            %|@2           |            ~|@3           |
    +-------------+-------------+-------------+-------------+-------------+-------------+
    |Plains       |                     +-----------+                     |Monastery    |
    |             |                     |Magic Tower|                     |            +|
    +-------------+                     |  .  G   A |                     +-------------+

`~` port, `%` Magic Gate, `+` safe. The City has both a port and a gate,
as it does on the real board. Each Plane shows its garrison: `L G A` for
the Lesser Guardian, the Greater Guardian and the Artifact still inside.

Each Plane lies outward of a landmark and is entered from either
neighbour. That placement is checked against the rulebook's own example -
the Plane by the Monastery is attacked from the adjacent Forest or the
City - and it comes out right.

## What makes it not-Talisman

* **Movement is chosen, not rolled.** Stay, walk one, ride two for a Gold,
  take ship between ports, step through a Magic Gate for two - or spend
  your whole move working: the City pays 2 Gold for 1 Magic, the Thieves'
  Guild 3 Gold for a Health, the Fortress 2 Experience for a Health.
* **Strength *is* Health and Willpower *is* Magic.** A wound makes you
  weaker in the very next fight; casting lowers the stat you cast with.
  The status line shows it: `S3/4` is a wounded hero.
* **Two kinds of battle.** A Battle of Wills takes precedence but the one
  who starts it pays 2 Magic first - and against a character, one more for
  every Artifact she carries.
* **Four of the five Artifacts wins.** Beat a Plane's Lesser Guardian, then
  its Greater Guardian, and the Plane closes behind you.

## The Final Battle

When all five Artifacts are out of the Planes but nobody holds four, the
rules turn the heroes on each other: the loser of a battle gives up an
Artifact, and a hero holding none is out of the game.

This is not decoration - it is what makes the game *end*. Without it 45%
of games ran to the turn cap and averaged 1858 rounds. With it:

| | without | with |
|---|---|---|
| games producing a winner | 22/40 | **50/50** |
| hit the turn cap | 18 | **0** |
| average rounds | 1858 | **132** |

## Choosing a hero

You pick the number of heroes and which one you play. The choice matters
twice over: Strength-versus-Willpower decides how you will fight, and the
two guilds on your card are the ones whose Abilities you can buy for
Experience alone - anywhere else costs the same again in Gold.

    [1] Warrior     Str 5  Will 2  Gold 2    trains cheaply at Fortress and Forest Camp
    [5] Sorceress   Str 2  Will 6  Gold 1    trains cheaply at Magic Tower and Monastery

## Prompts show the comparison, not the arithmetic

Every battle prompt states both totals against what they face, so the
choice needs no working out:

    Ranger   [s] Strength 3 v 6    [w] Wills 6 v 3  (costs 2 Magic)

The Wills figure already has the Magic cost taken off it - that payment
lowers the very Willpower you are about to fight with, which is the whole
tension of the rule.

## On pauses

The computer's moves are paced so you can follow them (`PROPHECY_DELAY`,
500ms by default). Your own turn is not paced at all - the log is on
screen and you are the one driving, so routine actions simply happen.

The game only stops and waits for you at moments that are actually worth
reading: the dice of a battle, its outcome, and an Artifact changing
hands. Moving, working, buying, learning and using an Opportunity all pass
without asking anything.

## Testing

Same habits as its sister project `talisman-c`, built in from the start
rather than retrofitted:

| var | effect |
|-----|--------|
| `PROPHECY_AUTO=1`  | headless, no rendering; prints `WIN|Thief|4|132` |
| `PROPHECY_SEED=n`  | fixed seed, so any game can be replayed |
| `PROPHECY_TRACE=f` | full plain-text log to `f` (`-` for stderr) |
| `PROPHECY_CHECK=1` | assert game invariants every round |
| `PROPHECY_DELAY=n` | ms between computer actions |
| `PROPHECY_SHOT=f`  | append a text dump of every frame |

`--watch` plays every hero for you and renders it: `space` pauses, `+/-`
change speed, `q` stops. `--log FILE` writes the same full text log as
`PROPHECY_TRACE`, and it records the ending, so a finished game says who
took the throne and after how many rounds.

Over 50 seeded games: every one produced a winner, no stalls, **no
invariant violations**, and every subsystem fires - Plane assaults 100%,
Artifacts taken 82%, Final Battles 30%, character duels 74%, bands of
creatures 92%, Battles of Wills 77%. Clean under ASan and UBSan, and no
warnings at `-Wall -Wextra -Wshadow -Wstrict-prototypes -Wpointer-arith`.

## The cards are ours, not the game's

Be clear about this: the deck here is **invented**. The published list is
211 cards for the main game - 63 Adventure, 22 Chance, 35 Common Items, 26
Rare Items, 5 Artifacts, 5 Lesser and 5 Greater Guardians, and 10
Abilities at each of the five guilds. This build has 24 Adventure cards, 9
Items, 10 Abilities and 10 Guardians, all written by us to the shapes the
rules describe.

The card-list PDF is a **scanned image** - `pdftotext` gets 5 bytes out of
it. Running it through `tesseract` at 300dpi recovers 195 of the 211 names
(`CARDS-ocr.txt` here, produced with `--psm 6` and `--psm 4` merged, since
the decorative border defeats either alone). Even a perfect read would
give names and counts only; the Strength and Willpower values live on the
card faces and are in none of the files.

### A better source: the Tabletop Simulator mod

The Steam Workshop mod **"Prophecy + Expansions"** (`2518181087`) is a far
better source, and it is already on this machine. Its JSON carries no card
names or text - the cards are images - but TTS caches the card atlases
locally at full size (3936x4096, a 9x6 grid, so 437x682 per card face).
Cropped out and enlarged, those faces OCR into the real thing:

    Golem Guardian   xp3
    Skin of stone: if you use a Weapon, it becomes damaged after the battle.
    Roll a die to find the treasure being guarded: 1-4 = Common Item, 4-6 = Rare Item.

    Wiseman
    The Wiseman shares his wisdom with the worthy. You may pay 5 Experience
    to gain 1 Willpower from the bank.

That is names, numbers *and* rules text - everything the PDFs could not
give. `tts-cards.py` here does the extraction. Two things it had to learn:
the image cache predates a Steam hostname change, so atlases match on the
ugc id rather than the URL; and a card's `CardID` is `atlas*100 + slot`, so
cards are not at sequential positions and one deck draws from several
atlases. With that right it finds all 93 Adventure cards and 7 Lesser
Guardians.

What the mod actually holds - **25 decks, 388 cards**:

| deck | cards | |
|------|-------|--|
| Adventure Deck | 93 | 63 base + expansions |
| Chance Cards | 22 | matches the published 22 |
| unnamed | 51 | Common Items (35 base + expansion) |
| unnamed | 39 | Rare Items (26 base + expansion) |
| unnamed x7 | 10 each | the five guilds' Ability decks, and two more |
| Map Fragments | 12 | |
| Greater / Lesser Guardians | 8 / 7 | 5 base each, plus expansion |
| Water Realm, Dragon Realm | 80 | whole expansions |

### Result

`tts-extract.py` recovers **322 cards, 286 of them with usable rules text**:

| deck | cards | usable |
|------|-------|--------|
| Adventure Deck | 93 | 58 |
| Chance Cards | 22 | 22 |
| unnamed pool (Items + Abilities) | 160 | 160 |
| Greater / Lesser Guardians | 8 / 7 | 8 / 7 |
| Map Fragments | 12 | 11 |
| Water / Dragon Realm | 20 | 20 |

Sample, straight out of the pipeline:

    Peaceful Times   Nothing happens. You get two consecutive turns this round.
    Plains           Place a new Adventure card in every Plains space.
    Fortress         Turn up a new Training card in the Fortress.
    Magical          Recharge up to 4 Magic. All other players recharge up to 2 Magic.
    (Guardian)       Riddles: before the battle you may roll a die. Pay 1 Magic for every roll.
    (Guardian)       Agony: before the battle lose 1 Health and 2 Magic.

Two hard-won notes are in the script. **`magick atlas.png -crop 9x6@`
slices all 54 faces in one pass** - re-reading a 40MB atlas per card is
what made the first attempt never finish. And **tesseract must be able to
read the output directory**: on this machine `/tmp/...` is readable by
ImageMagick but not by tesseract, which fails with `fopenReadStream ...
failed to open locally` and silently yields empty text. Every "blank card"
in the earlier attempts was that, not bad OCR.

**Still to do before the deck can be rebuilt from this.** The card names are often lost in the decorative title band, so most rows
carry the rules text but not a clean name. The Strength/Willpower numbers
sit in ornamented corners and come out unreliable. And 35 of the 93
Adventure cards are still too noisy to use. So this is a strong base for
writing a faithful deck by hand, not a drop-in replacement for `data.c`.

## Spells

The rulebook is explicit that Spells are not a separate subsystem:

> Some Abilities are called Spells (mostly from the Magic Tower, but even
> thieves and warriors have access to some Spells). Spells are Abilities which
> are activated by paying one or two Magic -- the cost is stated on the card
> below its name. The effect of a Spell on the game is treated the same as
> other Abilities.

So a Spell here is just an `AbilityCard` with a non-zero `magic` cost and a
`SpellKind`. Ordinary Abilities keep `SP_PASSIVE` and a standing `d_str` /
`d_will` bonus; Spells are activated instead.

The names and effects are the real cards, recovered from `CARDS-tts.tsv`:

| Spell | Magic | Effect |
|---|---|---|
| Berserker Rage | 1 | +2 Strength in a battle |
| Battle Fury | 2 | +3 Strength in a battle |
| Thievery | 1 | +2 Strength in a battle |
| Mind Lance | 1 | +2 Willpower in a Battle of Wills |
| Counter-Spell | 2 | +3 Willpower in a Battle of Wills |
| Forest Trails | 1 | move to any Forest space |
| Mountain Paths | 1 | move to any Mountains space |
| Miraculous Healing | 2 | heal 2, instead of moving |
| Meditation | 0 | recharge 2 Magic, instead of moving |
| Sacrifice | 0 | a Health for 3 Magic, instead of moving |

**Magic and Willpower are the same pool.** This is the part that makes the
costs interesting rather than decorative: spending Magic lowers the very
Willpower a Wills spell is about to boost. Counter-Spell costs 2 and gives 3,
so it is worth `+1` net, not `+3` -- `battle_spell()` scores it that way, which
is why cheap Mind Lance wins ties against it. A Strength spell has no such
tax, and the AI casts those freely.

Each spell is offered once per battle, per the rule that Spells "cannot be
used in the same moment for the same goal" -- so the bonus is computed once
where `mine` is decided, not inside `fight_roll()`, which a band battle calls
three times.

Movement Spells *are* the move (`MV_SPELL`), not an extra: they set `p->idx`
and fall through to `resolve_space()` like any other arrival.

A human holding several Spells gets a submenu under `[c]`; the AI just takes
the highest-scoring one. That split matters -- the AI's score is a heuristic,
and it should not silently decide which spell a player is allowed to cast.

### What the soak says

50 seeded games, all reaching a throne, no invariant violations, average 103
rounds (up from 93 -- battles are more survivable now):

```
159  Berserker Rage      88  movement spells
128  Thievery            51  Mind Lance
 90  Battle Fury         44  Counter-Spell
  2  Sacrifice            2  Miraculous Healing
```

**Meditation never fires, and that is a finding, not a bug.** Instrumenting
`pick_utility_spell()` showed holders *do* reach 3-4 Magic, so the guard is
not the problem: recharging 2 Magic is simply worth less to the AI than a turn
of progress, because outside the Magic Tower characters barely spend Magic in
the first place. If the Magic economy ever tightens, this card becomes live on
its own. It is offered to human players regardless.

## Damaged Items, repair and resale

Three rules that only make sense together:

> Some Items (mainly throwing weapons and shields) can be damaged during the
> game. Damaged cards should be turned face-down and may not be used. In a
> Civilization space (a blue space: any guild, the City or the Village), it is
> possible to repair an Item. For each repaired Item you must pay 1 Gold.

> It is also possible to sell Items -- the price is half, rounded up... You must
> repair damaged Items before selling them. Sold Items do not stay on the same
> space -- they are discarded.

A damaged Item is tracked per *copy*, not per card type, so `Player` carries
`dmg[MAX_ITEMS]` alongside `items[MAX_ITEMS]`. `carried()` skips damaged
Items, which is the whole effect: a broken shield gives nothing until mended.

**Throwing.** The rulebook's own worked example has a character throw his Axe
on the last roll of a band battle for an extra point, win, and turn the card
face-down. So `thrown` on an `Item` is that bonus, and the weapon *still
counts for the battle it is thrown in* -- it is only dead from the next one.
The throwables are the real ones (`CARDS-tts.tsv` confirms the Dagger "can
also be thrown in a Battle of Strength"): Dagger, Spear, Javelin, Boomerang.

The AI only throws when the margin is in doubt (`mine >= theirs + 3` declines).
Without that guard it threw in every battle it could -- 855 throws across 50
games versus 471 with it -- which is not wrong, exactly, since a Gold mends the
weapon, but it churns gear for nothing when the fight is already won.

### The parallel-array trap, and the invariant that catches it

`items[]` and `dmg[]` must move together, so every gain and loss goes through
`item_give()` / `item_take()` -- nothing else touches either array. `item_take()`
shifts the tail down and then clears the vacated top slot, and `PROPHECY_CHECK`
asserts that no slot at or above `nitems` still carries damage.

That check is not decorative. Removing the one line that clears the slot
produces **429 violations across the same 50 games**, and the symptom would
have been a sound Item silently reading as broken (or a repair charged for
damage nobody was carrying). It is exactly the failure mode parallel arrays
invite, so it is worth an assertion rather than care.

Because a damaged Item is invisible otherwise, the status row now carries the
kit: `I3` for three Items, `I3!1` when one of them is face-down.

### What the soak says

50 seeded games, all reaching a throne, no invariant violations, average 114
rounds (up from 103 -- broken gear makes characters weaker for a while):
471 throws, 360 repairs, 1 sale. The AI sells only when flat broke with more
than one Item, which is rare by design; a human is asked once per City, not
once per Item.

## Per-guild Ability decks

Each guild now has its own shuffled deck, drawn without replacement, built
from the `guild` field the Abilities already carried. Before this every guild
drew from one pile, which produced lines like "Arcane Lore is taught at the
Thieves' Guild" -- a Magic Tower spell on sale from cutpurses. Across 50 games
now, all twenty Abilities appear only at their own guild.

The Thieves' deck was the thin one, so it gained the two real thief Abilities
the rulebook names in its FAQ, both of which finally have something to bite on
now that repair and resale exist:

- **Haggling** -- "only works when buying or selling in the City or Village":
  1 Gold off a purchase, 1 Gold more on a sale.
- **Counterfeiting** -- "possible to use this only once when paying for
  something", and "repairing more Items... counts as one payment".

Counterfeiting forced a split between *quoting* a price and *paying* it.
`quote_price()` is pure; `pay()` is what actually spends the once-a-round use.
The first draft had a single side-effecting function called twice in
`buy_item()` -- once to test affordability, once to charge -- which would have
burned the forgery on the question and then charged her full price for the
answer.

### Peaceful Times was doing nothing

Chasing "once a *round*, not once a turn" turned up a dead flag: `CH_PEACE`
set `extra_turn = 1` and nothing ever read it, so the card had no effect since
the day it was written. It now genuinely replays the same character before
passing the turn on -- 244 second turns across 50 games, where there had been
none. Counterfeiting resets when the turn passes to the next character rather
than at the top of each turn, which is precisely the distinction the rulebook
draws when it says the extra turn does not buy you a second use.

## The Adventure deck, from the real cards

The deck is now 47 distinct cards / 76 copies (it was 24 / 40), and the names
and effects are the real ones, read out of `CARDS-tts.tsv`. The OCR mangles the
Strength and Willpower in the card corners -- the extractor's own header warns
about that -- so the numbers are fitted to this game's curve, but every special
below is quoted from a real card:

| Card | Its rule |
|---|---|
| Vampire | "You must win a Battle of Wills, followed by a Battle of Strength" |
| Giant | "Stun: if he defeats you... he takes 2 Magic in addition" |
| Stone Guardian | "Skin of stone: if you use a Weapon, it becomes damaged" |
| Headless Knight | beat him with Strength, gain Willpower -- and the reverse |
| Doppelganger | "Its Strength is twice your current Health" |
| Noble Lion | "Nobility: you do not need to fight" |
| Highwayman | "Thievery: instead of a Health, you must give him an Item" |
| Poisonous Snake | "Poison: lose 2 Health" -- win or lose |
| Sold One, Centaur | spare it and "you gain no Experience. Instead... 1 Common Item" |
| Chapel | "+2 for your first die roll, then discard this card" |

Two of these only became possible because damaged Items already existed: Skin
of stone turns your best weapon face-down, and Thievery takes an Item through
the same `item_take()` everything else uses.

### The Noble Lion ran away with the game

Nobility looked like the simplest card here and was the only one that broke
anything. Declining the fight returned "draw", and a draw leaves the Creature
on its space -- so a character could decline the same Lion forever. It fired
**4271 times across 50 games**, average game length went 111 -> 193 rounds, and
one game ran to the 4000-turn cap.

The fix is that passing is *resolving*: the Lion lets you by and moves on, so
`battle_creature()` returns 2 for "settled without a fight" and the caller
clears the space for either 1 or 2. Nobility now fires 27 times, and the
average is back to 100 rounds. Worth noting the deck itself was never the
problem -- the harder Creatures cost about 0.4 Strength on average, nothing
like the swing the blocker produced.

## Guardians that levy a toll

The Astral Plane Guardians were large Creatures and nothing more. The real
cards all charge you something before a blow is struck -- Agony, Lightning,
Shadow Battle, Riddles, the Anti-magic Aura -- and that toll is what makes a
Guardian different in kind from a big Ogre. The names here are still ours; the
tolls are theirs.

`fight_guardian()` is the only way a Guardian reaches the battle code, so the
toll cannot be skipped by a future caller. The Anti-magic Aura is the awkward
one: it has to reach `battle_spell()`, which sits deep inside the fight, so it
travels as a file-scope flag raised and lowered around the battle. Across 50
games the Guardians took 871 tolls and no character was killed by a toll alone.

## The two variants

`--apocalypse` and `--teams`, both from the rulebook's variant section.

**Apocalypse.** When the fifth Artifact is claimed the whole Chance deck,
discards included, is reshuffled and "the deck now counts down the time towards
the Apocalypse". If it runs out undecided the deck is *not* reshuffled again --
instead every character loses 1 Health and 1 Magic at the head of every round
until someone is crowned or nobody is left. Characters holding no Artifact when
the Final Battle opens are swept away.

In 40 games the Final Battle opened 5 times and 3 characters were swept away,
but the Apocalypse itself never began -- the Final Battle settles things well
inside 22 Chance cards. Rather than ship the branch untested I forced it by
cutting the deck to 2 cards: it fires, the world drains everyone each round,
and games still end with a throne.

**Team play.** Four players as two pairs; turn order is 0,1,2,3 and teams are
`i % 2`, which seats partners opposite each other exactly as the rules ask.
Comrades do not attack each other and may hand over Items and Gold when they
share a space.

The interesting part is that they *must* fight in the end. The rules say a pair
may "transfer the Artifacts at the end" but then correct themselves: "you can
only give away Items and Gold". So Artifacts can never be handed over, and the
only way to gather four into one pair of hands is to take them -- including
from your own comrade. Without allowing that, allies deadlocked: 3 of 40 games
hit the turn cap at an average of 391 rounds. Letting comrades fight once the
Final Battle opens puts it back to 40/40 at 101 rounds.

### I walked into my own trap again

The trade block above was written, the build was clean, and the feature did
nothing: `gifts: gold=0 items=0` across 40 games. The `.replace()` that was
meant to insert it had matched nothing, because I anchored on
`if (!p->alive) return;` when the line actually reads
`if (!p->alive || turn_over) return;`. This README already warns about exactly
this. Every patch in this batch now carries an `assert old in s` before the
write, so a missed anchor is a crash rather than a clean build with a missing
feature.

## Bug hunt

Swept alongside [talisman-c] before moving on, with sanitizers, extra
compiler warnings and volume.

**Sanitizers: clean.** Built with `-fsanitize=address,undefined` and run over
seeded games in all three modes -- standard, `--apocalypse` and `--teams` --
there were no findings at all. (The same sweep on Talisman found a real
out-of-bounds write, so the tool was working.)

**Warnings: one real tidy.** `-Wshadow -Wcast-qual -Wformat=2` and friends
turned up a single cast dropping const, `space_worth((Player *)p, j)`.
`space_worth()` never modifies the player, so it now takes a `const Player *`
and the cast is gone.

`glog()` now carries `__attribute__((format(printf, 1, 2)))`. It found nothing
wrong across the existing call sites, but a mismatched format in a variadic
logger stays silent until it prints nonsense, and it is a compile error now.

**Volume: 210 games** -- 70 each of standard, apocalypse and team play -- all
reaching an ending, no invariant violations, no non-zero exits.

## The AI plays the odds

The rule: *never fight a war you cannot win, never refuse one you are
likely to.* A Battle here is `stat + d6` against `stat + d6`, so that is
arithmetic rather than judgement -- `win_chance()` sums the 36 ways two
dice fall. Equal statistics is 41%, not 50, because six of those are ties
and a tie ends your turn.

```c
#define WORTH_FIGHTING 50
#define DESPERATE      33
```

**A band is where the exact answer beats a good approximation.** Penalising
a band by adding to its Strength is the right instinct, but a band "fights
as a band: three rolls must all be won", so the real chance is `p^lives`.
Against a Pack of Wolves at even odds that is 41% cubed -- about **one in
fifteen**, not "somewhat harder". The evaluation computes it directly.

Poison is the other case a penalty cannot express: venom is paid whether or
not you win, so at low Health a *winnable* fight is still a fatal one. Its
chance is zeroed rather than reduced.

| | rounds | deaths | creature fights won |
|---|---|---|---|
| thresholds | 144 | 1 | 63% |
| probability | 164 | **0** | **65%** |

Games run longer, which is the price of an AI that declines fights it would
probably lose. That was the trade asked for.

## Not yet built

Item-specific Adventure effects that need machinery this engine does not
have: the Pied Piper's Rat-counting, the Riddler and Chronicler's
look-at-cards, the Master Wizard's teleport, and the keep-this-card effects
beyond the Chapel's blessing.
The rules PDFs are two-column - `cols.py` here splits them, without which
`pdftotext` interleaves the columns into nonsense.

## Known gaps, and what blocks them

- **170 of the 195 named cards are not in the game.** `CARDS-ocr.txt` has the
  names from the printed card list; `CARDS-tts.tsv` has legible rules text
  for many of them. Neither has the numbers: Strength and Willpower are
  printed on the cards as icons, and the OCR renders them as `€3`, `©`, `»`.
  Adding those cards means inventing their stats, so they stay out.
- **Nightmare** -- "before the battle, choose whether your nightmare is an
  animal, a demon, a humanoid, or an undead" -- needs creature types, which
  nothing else in the game has yet. It is a plain Willpower fight here.
- **The Ghouls' grave-search** ("you may choose to search the graves they
  were digging up; if you do, pay 1 Health") and **the Mummy's curse** on the
  Rare Item are both missing; the creatures fight correctly otherwise.

Validated and correct: the treasure rewards match their cards (the Bear's
pelt sells for 3 Gold, the Ghost guards 3), and the Centaur and Sold One
offer to serve when spared, as printed.

