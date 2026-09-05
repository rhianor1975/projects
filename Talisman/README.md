# talisman-c

A console Talisman, in C with ncurses. Three concentric regions, an
adventure deck, and computer opponents that plan on the board instead of
searching it.

    make
    ./talisman            # you play @1, three computer opponents
    ./talisman --watch    # sit back: four computer characters, you just watch

Needs a terminal of at least **86x29**. The board itself is 85 columns
wide, so a 100x30 window is comfortable at a large font size.

## The board

The whole map is one `7x7` grid of cells. Its concentric perimeters come
out to exactly the region sizes Talisman uses:

| square | perimeter | region             |
|--------|-----------|--------------------|
| 7x7    | 24        | Outer Region       |
| 5x5    | 16        | Middle Region      |
| 3x3    |  8        | Inner Region       |
| 1x1    |  1        | Crown of Command   |

24 + 16 + 8 + 1 = 49, so every cell of the grid is a real space and there
is no dead area to draw around. `board.c` is the whole of the geometry:
`ring_cell()` walks a perimeter clockwise from its top-left corner, and
`board_init()` inverts that into `gridmap[7][7]`.

You move around your current ring only. There are three ways inward, and
they sit in the same column of the grid:

* **SENTINEL** (outer 3) - beat it to cross into the Middle Region, either
  by fighting it (Strength 6) or outwitting it (Craft 6).
* **PORTAL** (middle 10) - opens only if you carry a Talisman.
* **DREAD GATE** (inner 1) - one step from here onto the Crown.

In the Inner Region you move exactly one space per turn, so the four-space
walk from the Threshold to the Dread Gate is the dangerous part of the
game. The two directions are the same distance but not the same cost.

## Playing

A turn is **roll first, then choose** - you cannot pick a road sensibly
without knowing how far down it you are going:

1. `space` throws the die. It appears as pips in the right-hand panel.
2. You are then asked which way to go, with **both destinations named**:

       You rolled 3:   [a/<-] Barrow   [d/->] Village   [q] quit

In the Inner Region there is no roll - you move exactly one space - so you
are only asked for a direction.

* Lose a fight and you may spend a **Fate** point to reroll your die
* Defeated enemies bank **trophies**; 7 points trade for +1 Strength or Craft
* Reach the Crown and cast the Command Spell each turn: every rival loses a life

Every roll in the game is shown, in the dice panel and in the log - the
movement die, the two combat dice with their totals, and the rolls behind
the Tavern, Temple, Graveyard, Mine, waste and den.

## Watching

`./talisman --watch` (or `[w]` on the opening screen) hands all four
characters to the computer and plays the game out in front of you at a
readable pace. While it runs:

| key | |
|-----|--|
| `space` | pause / resume |
| `+` | faster (100ms less between actions, down to none) |
| `-` | slower (up to 3s) |
| `q` | stop watching |

The speed shows in the banner as you change it, and the controls work
while paused too. `TALISMAN_DELAY` sets the starting pace.

This is a different thing from `TALISMAN_AUTO=1`, which is the headless
soak-test harness below: that one draws nothing at all and exists to run
hundreds of games as fast as the machine will go.

## How you win

Reach the **Crown of Command** at the centre and work the ending: while you
are alone up there, every turn you cast the Command Spell and every rival
loses a life.

You cannot simply murder everyone instead. Until somebody first reaches the
Crown, a killed character is replaced - that player draws a **new character
at random** and carries on. The moment anyone sets foot on the Crown that
stops for the rest of the game and the dead stay dead. So killing the table
early achieves nothing; it only clears your path once the endgame is on.

Nor is the Crown a safe chair. If a rival follows you up there, neither of
you may work the ending: while two or more characters stand on the Crown,
their turns consist only of fighting each other, and the fight is not
optional.

Measured over 80 games, **every single one** ended with the winner on the
Crown - the last-character-standing path exists in the code but no longer
happens in practice, which is exactly what the respawn rule is for.

## Fighting other characters

Land on a space someone else occupies and you may attack them. Resolved
the way the rulebook does it:

1. Character battles are fought with **Strength** (not Craft).
2. Both roll a die and add Strength.
3. The **attacker** decides about spending Fate first, then the defender.
4. Higher score wins; equal is a stand-off.
5. The winner claims **one** thing: cost the loser a life, take one of
   their **Objects**, take a Gold, or take their **Talisman**. Kill them and
   you take everything you can carry; whatever you cannot take stays lying
   on the space for somebody else.

Either way **the turn ends there** - as it does after any defeat or
stand-off, including against monsters. In the Inner Region characters can
only meet on the Plain of Peril.

The AI hunts Talismans: it will go after a dropped one lying on its ring,
and now also after a living character carrying one, provided it thinks it
can win.

## Objects are real things

Objects and Followers are not baked-in stat bonuses - a character holds a
list of actual cards, and Strength and Craft are recomputed from what is in
their hands right now:

    Strength 4    Craft 2    Lives 4/4  Fate 3/3  Gold 1
      (base 4 / 2 -- the rest comes from what you carry)

So taking a Runesword off somebody moves its +2 Strength with it. Beat a
character and pick `[o]`, and you get to choose from their kit:

      TAKE ONE OBJECT
      Dwarf the Dwarf is carrying:
        [1] Ring of Fate     +1 Fate
        [2] Amulet           +1 Craft
        [3] Unicorn          +1 Craft, +1 Fate
        [4] Crystal Ball     +1 Craft

Die and your whole kit scatters onto the space as `*` glyphs; anyone
landing there picks up what they can carry.

### Carrying limit

The rulebook's limit applies: **four Objects**. Go over it and you choose
what to leave, and the rest is placed on your space for anyone to find.

**Followers do not count** against it - and that matters, because in the
real card list every pack beast (Mule, Horse and Cart, Riding Horse,
Warhorse) is a *Follower*, not an Object. A Mule that cost you an Object
slot would eat the space it exists to free. So:

| | |
|---|---|
| Mule | Follower, carry 2 more Objects |
| Horse and Cart | Follower, carry 4 more Objects |

Both turn up in the Adventure deck, and the **City has a Stables** where
you can buy them at the rulebook's prices - Mule 3g, Horse and Cart 5g.
Your sheet shows the tally: `Carried -- 3 of 6 Object slots used`.

## Spells

The 24-card Spell deck is in, with the real names and copy counts:
Acquisition, Alchemy, Counterspell x2, Destroy Magic, Destruction x2,
Divination, Healing x2, Hex, Immobility x2, Invisibility, Mesmerism,
Nullify, Preservation, Psionic Blast x2, Random x2, Teleport x2,
Temporal Warp.

**How many you may hold** is the rulebook's table, read off your
*effective* Craft - so a Magic Object can buy you a Spell slot, and losing
one can cost you a Spell:

| Total Craft | 1 | 2 | 3 | 4 | 5 | 6+ |
|-------------|---|---|---|---|---|----|
| Max Spells  | 0 | 0 | 1 | 2 | 2 | 3  |

Go over and the surplus is discarded at once. Most characters begin with
none; you learn them from the **Book of Spells**, and from the **Enchanter**
and the **Mage**, who teach whoever finds them.

**When you may cast** follows the two moments the rules describe:

* at the start of your turn, before you roll
* in a battle, *before the attack roll is made*

and you may cast at most as many Spells in a turn as you **held at the
start of it**. Your status line carries `z2` for Spells in hand.

Four Spells are battle-only, and they are the interesting ones: **Psionic
Blast** adds your Craft to your Strength, **Invisibility** ends the fight
before it starts, **Preservation** wards off the next life you would lose,
and **Nullify** makes the creature roll again.

One simplification: **Teleport** takes you to the crossing on your current
ring - the Sentinel, the Portal, or the Dread Gate - rather than to a
space of your choosing, which would need a target-picking screen for a
board you can already read at a glance.

## Shops: the Village, the City, the Market

You do not buy raw Strength. You buy **Objects**, from the real 28-card
**Purchase deck** - Sword, Axe, Shield, Helmet, Armour, Water Bottle,
Raft, Mule - and everything bought is an ordinary Object afterwards, so it
can be stolen off you, dropped when you run out of room, or looted from
your body.

The stock is finite. Buy the last Sword and the shelf reads *(sold out)*
for the rest of the game.

| | sells | services |
|---|---|---|
| **Village** | Purchase deck | the Mystic (1g, learn a Spell), the Doctor (1g, heal) |
| **City** | Purchase deck, Horse and Cart 5g | the Doctor, the Alchemist (an Object becomes 3 Gold) |
| **Market** (a card) | Purchase deck | - |

The Mystic, the Doctor and the Alchemist are all named in the rulebook.
Prices are ours; the card list carries none.

An Object you have no room for is shown greyed out and cannot be bought -
paying for something you would have to put straight back down is not a
decision worth offering.

## Characters and their powers

Every character has a special power, not just five numbers. Two are quoted
straight from the rulebook (it cites the Troll's regeneration and the
Warrior's two Weapons); the rest are ours, written in the spirit of the
originals.

| character | power |
|-----------|-------|
| Warrior   | wields two Weapons at once |
| Wizard    | holds one Spell beyond his Craft |
| Elf       | may retake his movement die once a turn |
| Dwarf     | the Mines never harm him, and pay him double |
| Priest    | +1 to his attack score against Spirits |
| Thief     | robs a character he meets without a fight |
| Troll     | regenerates a Life on a **natural** 6 to move |
| Sorceress | may pay Fate to force a foe to reroll, instead of retaking her own die |

The Troll reads the natural die, not a modified one - the rulebook makes a
point of that distinction.

## Weapons and Armour

Kit no longer simply stacks, because the rulebook says it does not:

> "A character may only use one Weapon during an attack." ... "Some
> Objects that prevent the loss of life when a character is defeated have
> the keyword Armour... only one Armour during an attack."

* **Weapon** (Sword, Axe, Runesword, ...): only your **best** one counts.
  Magic items that are not weapons - Magic Belt, Potion of Strength - still
  stack normally.
* **Armour** (Armour, Shield, Helmet, ...): not a Strength bonus at all.
  When you are defeated it rolls to spare the life instead, on a 5 or 6.

The Warrior's power is precisely the exception to the first rule.

Only clearly named weapons and armour carry the keywords - 17 and 8 cards
respectively across every set. An expansion card whose name gives no clue
is treated as a plain magic item.

## Rafts: the second way across the river

The Sentinel is not the only crossing, and this is the rule that says so:

> "Any character in a **Woods or Forest** space at the start of his turn who
> has an **Axe** may declare that he is building a Raft. Instead of moving,
> the character takes a Raft Card... may cross the river to any space of his
> choice **directly opposite** the one he is in. This is his move for that
> turn; he does not roll the die."

So the Raft - which this build previously wrote off as "no water to cross
on this board" - is a whole alternative route into the Middle Region:

* Stand in a Woods, Forest or Thicket carrying an **Axe** and spend your
  entire move building one. Or find one in the deck.
* Next turn it carries you straight across, **no die rolled**, to the space
  directly opposite. On this board "directly opposite" is the same grid
  cell pulled one square inward, which lands the Sentinel space on
  *Crossing* - exactly where the Sentinel bridge goes.
* Used or refused, the Raft is gone at the end of that turn.

It matters most to the characters who cannot bully the Sentinel. Over 60
games a raft carries someone across in a third of them.

## Trophies

Killing something banks its printed value as a trophy, and **7 points buys
+1**. The two piles are separate and cannot be traded against each other,
which is the actual rule:

* beat something with **Strength** -> Strength trophies -> +1 Strength
* beat something with **Craft** -> Craft trophies -> +1 Craft

Your status line shows both as `t3/5`, and `t` at the roll prompt opens a
character sheet with what you carry and an itemised trophy pile:

    Trophy pile -- 6 slain, 23 points earned in total
       1 x Goblin         (Str 2)
       1 x Serpent        (Str 4)
       2 x Bear           (Str 4)
       1 x Shade          (Craft 4)
       1 x Ogre           (Str 5)

## The map remembers

Cards are not all discarded after you meet them. What is left behind shows
as a glyph on the space, and the legend sits in the right-hand panel:

| glyph | meaning |
|-------|---------|
| `!` | a monster that beat you, or fought you to a standstill, and now holds that space |
| `?` | a Stranger - they stay where they were drawn |
| `M` | the Cave - it breeds a new Enemy on a roll of 3+ |
| `~` | a hazard: the Marsh wounds, the Maze costs you a turn |
| `&` | the Market - it sells from the Purchase deck |
| `%` | a Magic Portal - it throws you somewhere else on your ring |
| `+` | a boon: Shrine, Pool of Life, Magic Stream, Fountain of Wisdom, Fairy |
| `*` | an Object lying on the ground |
| `$` | loose gold on the ground |
| `T` | a Talisman lying where someone dropped it |

The rules behind those glyphs:

* An enemy you **fail to kill stays on that space**. Kill it later and it is
  gone for good; otherwise the next player to land there meets it too.
* **Place** cards (`& % + M`) are never discarded - once drawn they are part
  of the map for the rest of the game.
* When a character **dies, their gold and their Talisman drop where they
  fell**. Someone else can walk over and pick them up, and the AI will go
  out of its way to do exactly that.

A space holds at most four such things at once.

Anything already on a space is dealt with **before** the space draws new
cards, so a busy space gets busier.

## Layout

    src/talisman.h   types, board constants, the three crossing indices
    src/board.c      ring <-> grid geometry, and what is left on each space
    src/items.c      carried Objects, and the effective stats they produce
    src/data.c       spaces, the adventure deck, the characters
    src/ui.c         ncurses rendering and prompts
    src/ai.c         opponent decisions
    src/game.c       turn loop, combat, encounters

## Debug / balance switches

Environment variables, all optional:

| var | effect |
|-----|--------|
| `TALISMAN_DELAY=0`   | remove the pause between AI actions |
| `TALISMAN_AUTO=1`    | every character is AI, no prompts; prints `RESULT|character|turns` on exit |
| `TALISMAN_SEED=n`    | fixed RNG seed, for reproducing a game |
| `TALISMAN_TRACE=f`   | same as `--log f` (`-` writes to stderr) |
| `TALISMAN_SHOT=f`    | append a plain-text dump of every rendered frame to `f` |

Together those let you soak-test balance without a human at the keyboard:

    TALISMAN_AUTO=1 TALISMAN_SEED=7 TALISMAN_TRACE=- ./talisman >/dev/null

Over 120 seeded games, with no crashes and none hitting the turn cap:

| | |
|---|---|
| someone crossed the Sentinel | 100% |
| someone reached the Inner Region | 99% |
| the Crown was actually seized | 99% |
| a monster was left on a space and re-fought later | 100% |
| a Gate of Passage threw someone across the ring | 95% |
| a dead character's Talisman was picked up off the ground | 37% |
| one character attacked another | 94% |
| a Talisman was taken off a living character | 68% |
| an Object was taken off another character | 99% |
| a dropped Object was picked up off the ground | 63% |
| a killed player came back as a new character | 68% |
| two characters contested the Crown | 15% |
| a Spell was cast | 100% |
| a character had too much Craft-magic and had to discard | 11% |
| a character was too dull to hold a Spell at all | 45% |
| somebody hit the carrying limit | 79% |
| an Object was left on the ground for lack of room | 51% |
| a Mule was acquired | 80% |

Games run about 149 turns across four characters, and over 100 seeded
games **all 17 Spell effects fired at least once** - the cheapest way to
know the deck is not carrying dead cards.

Crown-seizing was 84% while the AI chose its direction *before* rolling.
Moving the roll ahead of the choice let it score the exact space each
direction would land on, and that alone took it to 99%. Adding character
combat brought it back to 94%, which is the point - rivals now kill each
other instead of running four separate solo races. About 5 character
battles happen per game.

Letting the Sentinel be outwitted with Craft was what fixed the worst
imbalance. Crossing rates before and after, over 100+ games each:

| character | Strength gate only | either stat |
|-----------|--------------------|-------------|
| Wizard    | 41% | 62% |
| Priest    | 34% | 46% |
| Sorceress | 28% | 45% |

Craft characters could previously never convert their winnings into the
one stat the only door accepted - and because the trophy piles are
separate, no amount of killing spirits would ever have helped.

## Expansions

At startup you tick which expansions to shuffle in, exactly as the boxes
intend. `--sets=reaper,bloodmoon` or `--sets=all` skips the question;
`TALISMAN_SETS` does the same for headless runs.

| set | | Adventure deck |
|-----|---|---------------|
| base game alone | | 104 cards, 24 Spells |
| + The Reaper | | 194 cards, 51 Spells |
| + The Blood Moon | | 215 cards, 34 Spells |
| everything | | **621 cards, 171 Spells** |

Eleven sets carry Adventure or Spell cards: the Reaper, Frostmarch, Sacred
Pool, Blood Moon, Firelands, Cataclysm, Dungeon, Highland, Woodland and
Harbinger. The rest of the boxes (Dragon, City, Nether Realm, Deep Realms)
are new *boards* rather than new cards, so there is nothing here to take
from them.

**Be clear about what is authentic.** The card list gives real names,
types and copy counts - and those are exact. It carries no numbers, so for
the 517 expansion cards the Strength and Craft values are derived from the
card's type, nudged by a hash of its name so they are varied but stable
run to run. Their effects are mapped onto the ones this game already has,
by keyword: a card called "Bane Sword" gets +Strength, "Barrow Wight" is a
Craft fight, "Bag of Carrying" adds Object slots. Expansion Places,
Strangers and Spells reuse base effects under their own names.

So the expansions give you a much bigger, more varied deck with the right
cards in it - not the printed rules text of 517 individual cards.

## The deck

The base Adventure deck is the **published base-game deck**: 80 distinct
cards, 104 in total, with the real names, types and copy counts, and
hand-written Strength/Craft values.

| type | | |
|------|---|---|
| Enemy - Animal  | 7  | Ape, Bear, Lion, Serpent, Wild Boar x2, Wolf |
| Enemy - Dragon  | 3  | Dragon x3 |
| Enemy - Monster | 9  | Bandit x2, Giant, Goblin x2, Hobgoblin x2, Ogre x2 |
| Enemy - Spirit  | 10 | Demon, Ghost x2, Lemure x2, Shadow x2, Spectre, Wraith x2 |
| Event           | 13 | Angel, Blizzard, Devil, Mephistopheles, Pestilence, Raiders, Siren, ... |
| Follower        | 13 | Alchemist, Gnome, Guide x2, Maiden, Mercenary, Mule, Pixie, Princess x2, Unicorn, ... |
| Magic Object    | 13 | Amulet, Cross, Holy Grail, Holy Lance, Magic Belt, Runesword, Talisman x2, ... |
| Object          | 18 | Armour, Axe, Bag of Gold x8, Helmet, Raft, Shield, Sword x2, Two Bags of Gold x3, Water Bottle |
| Place           | 9  | Cave, Fountain of Wisdom, Magic Portal, Magic Stream, Market, Marsh, Maze, Pool of Life, Shrine |
| Stranger        | 8  | Enchanter, Fairy, Healer, Hermit, Mage, Phantom, Sorcerer, Witch |

`data.c` is **generated** from the published list rather than hand-typed,
by a script that refuses to emit anything if a base-game card has no stats
defined - so a card can never be silently dropped.

**Strangers** are new, and like Places they are never discarded: the
rulebook has "any Stranger or Place there must be visited", so once drawn
they are part of the map. They show as `?`.

Two Places would break the game if they stayed, because a Place that
persists is an endless tap: the **Fountain of Wisdom** (+1 Craft) and the
**Fairy** (+1 Fate) are used up and discarded, while repeatable ones that
cap themselves - Pool of Life, Magic Stream, Shrine - remain.

The **Water Bottle** finally earns its Object slot: carry one and the
desert costs you nothing. The **Raft** is honestly labelled "no water to
cross on this board" - it is a real card, taking up a real slot, doing
nothing. That is a decision the carrying limit now makes interesting.

### Real card and character data

Two files here are **not invented**, unlike `data.c`:

* **`CARDS-2e.tsv`** - 813 official 2nd Edition cards, read out of Djeryv
  Tar's print-and-play set (`2e-Tal-Is-Poor.zip`, the Polish artwork
  edition). Covers the base game, Expansion, Dungeon, City, Purchase,
  Spell and Timescape decks. 638 carry a clean name, 103 a printed
  Strength, 34 a Craft. Built by `extract-2e-cards.py` (OCR) and
  `parse-2e-cards.py` (kept separate, so re-parsing never means re-reading
  a thousand images).
* **`CHARACTERS.tsv`** - 1,554 characters with printed Strength, Craft,
  Fate, Gold, Lives, alignment and **starting space**, from the database at
  `talisman.edwebb.net`. 1,149 of them are 2nd Edition. This is the answer
  to the biggest gap in this build: the eight characters in `data.c` have
  invented stats, invented powers, and no starting spaces at all.

Those print sheets are far better OCR material than any digital source we
tried - 5x4 grids of crisp black line art on white, designed to be printed
and read. A worked comparison: the same job against Tabletop Simulator's
card atlases was heading for five hours and partial results; this was
under an hour and near-complete.

Spot-checking the real numbers against the ones invented for `data.c` is
reassuring - Dragon 7 and Giant 6 both match exactly; Ogre is 4, not the 5
guessed here.

### Where the rules came from

Three sources, all outside this repo:

* the **official Talisman Revised 4th Edition rulebook** (2008), which
  Tabletop Simulator had cached at `~/Library/Tabletop Simulator/Mods/PDF/`
  as part of a workshop mod. It is fuller than the compilation below and
  is the canonical wording; it is where the space-topping-up draw rule
  came from.

* a 52-page rules compilation (base game plus expansions) - `pdftotext
  -layout` gets clean text out of it. It settled the battle sequence, the
  trophy rules, the carrying limit, the respawn rule and the Crown ending.
* a card-list spreadsheet, one sheet per expansion, giving each card's
  title, type, deck and copy count. It is what revealed the pack beasts
  are Followers, and it is the source the deck is generated from.

The list carries no Strength/Craft values - those live on the card faces -
so every combat number in `data.c` is ours.

## The game log

`--log FILE` writes a complete plain-text record of the game - every roll,
move, encounter, purchase, theft and death, with both dice and their
totals shown for each fight. A four-character game runs to about a
thousand lines.

    ./talisman --log game.log

It opens with everything needed to reconstruct the game, including the
seed, so any game can be replayed exactly:

    === TALISMAN ===
    seed 12   (replay with TALISMAN_SEED=12)
    sets: Base Game
    Adventure deck: 104 cards. Spell deck: 24.
      @1 Sorceress  Str 2  Craft 4  Lives 4  Fate 5  Gold 1  (you)
      @2 Elf        Str 3  Craft 3  Lives 4  Fate 5  Gold 1  (computer)
    ...
    Elf (Craft 3 + roll 3 = 6)  vs  Lemure (2 + 2 = 4)
    Elf defeats the Lemure!
    ...
    === Dwarf the Dwarf wears the Crown of Command, after 161 turns ===

The log is also the test harness: every balance figure in this README was
measured by running games headless with `TALISMAN_AUTO=1` and grepping
these lines.

## The fourteen real characters

The roster is now the official 2nd Edition box: Warrior, Wizard, Elf, Dwarf,
Priest, Thief, Troll, Monk, Sorceress, Prophetess, Ghoul, Druid, Minstrel,
Assassin. Where the numbers came from matters, because they came from three
different places:

- **Strength, Craft, Lives, Gold** -- `CHARACTERS.tsv`, filtered to the rows
  whose Expansion column reads exactly "Talisman 2nd Edition".
- **Alignment, starting space, and every special ability** -- the printed
  cards themselves. `extract-2e-characters.py` slices the ten `CHAR_*.BMP`
  sheets in `2e-Tal-Is-Poor.zip` (2x3 cards each) and OCRs them;
  `parse-2e-characters.py` turns that into `CHARACTERS-2e.tsv`. Split in two
  for the same reason as the card scripts: re-parsing must never mean
  re-OCRing ten 3MB sheets.
- **Fate** -- ours, and only ours. No 2e card prints a Fate value and the
  database column is empty for every 2e row, so these are set by feel.

Three of the fourteen titles defeated tesseract entirely -- the stylised
headings -- but each is named unmistakably by its own rules text, so
`BY_POWER` in the parser repairs Assassin, Minstrel and Sorceress from a
distinctive phrase plus their starting space. That pairing matters: matching
on the phrase alone put the Sorceress in the Crags, because "Instead of
attacking" appears on more than one card.

Characters no longer start wherever the table seats them. Each card names a
space and `outer_by_name()` finds it, returning -1 rather than quietly
dropping someone at the Village if a name is ever mistyped.

### What the cards actually say

My invented abilities were mostly wrong, and one was accidentally right. The
Warrior really does "use two weapons at the same time" -- but he also rolls
two combat dice and keeps the higher, which I never had. Against that, the
Elf's movement retake, the Troll's regeneration and the Sorceress's jinx are
on no card at all; they are gone, along with the code that served them.

`Ability` was a single enum, which cannot express a real card: the Dwarf's
prints six abilities. It is now a bitmask, and the eleven implemented powers
are all quotations:

| Character | The card |
|---|---|
| Warrior | two Weapons at once; roll 2 dice, keep the higher |
| Monk | "add your Craft to your Strength in Combat"; no Sword |
| Priest | "automatically destroy any Spirit, without resort to Combat" |
| Wizard, Prophetess | "always have at least one Spell" |
| Wizard, Sorceress, Ghoul | attacker "may choose to make the Combat Psychic" |
| Sorceress | Enchant instead of attacking: "they can do nothing" |
| Sorceress | "You may Evade Spirits" |
| Ghoul | a Life taken "you add it to your own" |
| Assassin | "your victim may not roll a die" |
| Minstrel | "Animals and Dragons will not attack you" |
| Prophetess | "draw one Card more than necessary and then discard one" |
| Elf, Dwarf, Troll, Druid | "you are always safe there" (Forest / Crags) |

The Assassin needed one extra thought: his victim may not roll, and spending
Fate to reroll is still rolling, so the defender's Fate reroll is blocked too.
The Minstrel needed an `animal` flag on `Card`, now set on the 19 Enemies the
2e cards subtitle as Animals.

### Fourteen will not fit

The character-select screen was hard-coded to eight keys and two rows each.
Fourteen at two rows is 29 lines before the header, on a board whose stated
minimum is 29 rows. Each character now gets one line, and the keys run 1-9
then a-e.

### What the soak says

40 seeded games, all 40 crowned, no invariant violations, average 151 turns.
Every one of the eleven abilities fires, and -- the part worth checking --
**all fourteen characters win at least once**, so the real printed stats are
playable as they stand. The Troll and the Ghoul lead on 6 wins each; the
Assassin, Druid and Prophetess trail on 1.

### A hang that was the test harness

The first human-seat run after this work stopped dead at turn 1 and looked
exactly like a freeze in the new character code. It was not: `human2.py`
cycles a fixed string of keystrokes, and that string had no `l`, `o`, `t` or
`g` -- the claim-reward keys -- so the run blocked on a prompt it could not
answer. The harness now covers every key the game can ask for. Worth
recording, because a harness that cannot answer a prompt is indistinguishable
from a game that has hung.

## Regions and boards

The expansions add boards, and the rulebook is unusually helpful about how to
model them. Its Dungeon section says the Dungeon "counts as **a Region** for
the purposes of casting spells" and again for Events -- the same word this
code already used for Outer, Middle and Inner. So an expansion board is not a
thing beside the board; it is more Regions.

Two tables, in `data.c`:

- A **Region** is a run of spaces with one shape: its `Space` array, its
  length, its topology, its first `space_id`, and which board draws it.
- A **Board** is one or more Regions sharing a grid. The main board is one
  board holding four Regions -- Outer, Middle, Inner and the Crown all live
  on the same 7x7.

That distinction only became obvious once the display question was settled
(below); before that I had them conflated, with the Dungeon as a peer of the
Outer Region rather than a board of its own.

`space_id()`, `ring_len()` and `space_at()` were three hand-written switches
and are now table lookups. `NSPACES` stops being `24+16+8+1` and becomes a
capacity of 128 that the resident arrays are sized by, with `board_init()`
checking the table fits inside it -- removing that check and shrinking the
capacity produces `board: 49 spaces declared but NSPACES is 40` rather than a
quiet overwrite of `res_card`.

**Topology is why a PATH cannot borrow the RING's step.** The rulebook again:
in the Dungeon "you may only move towards the center", and "you must stop
moving if you reach the Entrance space itself". Movement today is
`((idx + dir*i) % len + len) % len`, which would carry a character out of the
deepest chamber and back to the door with no message at all. So topology sits
on the Region and the two step functions stay separate.

## One board per screen

The board is 85 columns of a 7x7 grid; a second one will not fit beside it in
100. The rule is therefore: **draw only the board the current player is
standing on**. Turn order makes that unambiguous -- there is exactly one
current player -- and the drawing loop did not have to change, since it
already walked cells and dropped `@N` on whoever matched. It is handed a
different grid, and cells the board has no space at stay blank.

The title line names what you are looking at and why:
`T A L I S M A N  -  The Kingdom  -  Assassin's turn`. Without it the view
would change under you with no explanation.

**The cost of the idea, and its fix.** If a rival can be somewhere you cannot
see, the map stops being how you track people. The status rows are already
about 47 of their 48 columns, so the panel takes it: when anyone is on another
board, the legend's rows become an `ELSEWHERE` list naming each absent
character, their board and their space. While every character shares a board
-- the whole game until the Dungeon opens -- there is nothing to report and
the legend keeps its rows.

### Proving a refactor changed nothing

Both steps above are meant to be *inert*: same games, same rolls, same
endings. Seeded runs are deterministic, so that is checkable rather than
hopeful. 45 games (30 base, 15 all-expansions) were captured before the work
and hashed:

```
baseline    75945475956cae3d8d5c46832a4b175bb716d559
after step 1  (region/board tables)      identical
after step 2  (per-board grid, title)    identical
```

The `ELSEWHERE` panel and the board-switching view cannot be exercised by a
game with one board, so they were tested against a temporary two-space second
board wired into the table, then the scaffolding was removed and the hash
checked again. The view followed the current player onto it, the absent
character vanished from the Kingdom map and appeared in `ELSEWHERE`, and the
unlaid cells drew blank instead of garbage.

## The Dungeon

The first expansion board, built from the rulebook section the 43-page
`RULES-2e.txt` turned out to contain, and from the board scan in
`2e-Tal-Is-Poor.zip`. Two things in it were not what I had assumed:

**Movement is an ordinary die roll.** I had guessed one space per turn.
Rule 6.2: characters "follow the spiral path to the Treasure Chamber, rolling
one die and moving the indicated number of spaces in the same way as movement
on the Outer and Middle Regions". It is the *direction* that is constrained --
"you may only move towards the center" -- with three exceptions in 6.4: a card
says so, you were just beaten in Combat (next turn only), or you are fleeing.
7.3 supplies the clamp: "You must stop moving if you reach the Entrance space
itself."

**The way in is a card, not a place.** I had the Dungeon hanging off the Chasm
as a fixed board feature. Rule 3.1 shuffles **four Doorway cards into the
Adventure deck**, and 4.1 treats a drawn one as a Place card left where it
fell, with at most two on the board at a time. So the entrance is wherever the
deck puts it, and a Doorway is just another resident card -- which the engine
already knows how to keep on a space. The real rule was simpler than my design.

Entry takes three turns, as written: land on a Doorway; next turn step onto
the Entrance and stop; the turn after, descend. It is optional at every stage,
and 5.3 takes your Horse, Warhorse and Horse and Cart off you on the way down.

The board is 25 spaces -- 16 in the outer lap, 8 in the inner, then the
Treasure Chamber -- which are the perimeters of a 5x5 and a 3x3 with a middle,
so `ring_cell()` already knew how to walk it. A spiral is nested squares you
do not stop between. Room rules are off the board scan: the Guard ("Strength 5
... bribe him with 2G"), the Library's die table, the Cell that takes a
Follower, the Torturer's 1 Gold or a point of Strength or Craft, the Kitchen,
and the Chamber of Darkness that throws you 3 back to 3 forward.

Leaving is the table printed in the Treasure Chamber -- Castle, Temple,
Warlock's Cave, Portal of Power, Plain of Peril, Crown of Command -- with
"Add 1 to the die roll for each character on the Crown of Command space,
counting scores over 6 as 6". This board has no Castle and its cave is plain
Cave, so those two map to the City and the Cave; the other four are exact.

### The bug that stopped four games finishing

With the Dungeon in, 26 of 30 games ran to the 4000-turn cap. It looked like a
balance problem and was not: `crown_reached` -- the flag that stops the dead
coming back, and therefore the only thing that lets a game end -- is set inside
`enter_crown()`. Rolling a 6 in the Treasure Chamber put a character on the
Crown by assigning `p->region = REG_CROWN` directly, so the flag stayed false,
respawns never stopped, and the Crown-holder killed the same characters
forever. One log had 518 deaths and 956 castings of the Command Spell.

Landing on the Crown is a transition with an owner, and going around that
owner is what broke it. Routing the Treasure Chamber's exit through
`enter_crown()` puts it back to 30/30.

### What the soak says

30 base games and 20 with all expansions, all reaching an ending, no invariant
violations. Average **132 turns against a 143-turn baseline** -- the Dungeon
shortens games, which is what a shortcut to the Crown ought to do. Every room
fires: 72 descents, 21 exits by the Treasure Chamber, 52 climbs back out, 21
fights with the Guard, 19 characters driven back after losing a fight, 7 turns
lost in the dark. A 222-turn human game found no invariant violations.

## Distance to the Crown

Your 2010 version worked because "the map is fixed and known" -- you could see
how far you were from winning by looking at where you stood. A second board
ends that: a character in the Dungeon has no way to judge whether she is
closer than before, and `space_worth()` was still counting ring positions.

So the board is walked once at startup as a graph and every space is given its
distance, in turns, from the Crown of Command. A turn moves you one to six
spaces, so each space reaches its six neighbours for a cost of one; the
Sentinel, the Portal and the Dread Gate are edges too. `TALISMAN_DIST=1`
prints the table, because a distance map that is quietly wrong would steer the
AI wrongly without ever looking wrong.

Two weightings in it are judgements rather than geometry:

**The Treasure Chamber's exit costs 6, not 1.** Rolling a 6 there really does
put you on the Crown, but it is one roll in six and you do not get to try
again. Weighted 1, the Dungeon reads as a motorway.

**There are two tables, not one.** The Portal of Power needs a Talisman, and a
single table would tell a character without one that the surface route is
seven turns when in truth it is shut. Computed both ways, the board looks like
this:

```
without a Talisman   Outer: every space -1   Dungeon: Entrance 10
with a Talisman      Outer: 6-8              Dungeon: Entrance 10
```

`-1` is "no road to the Crown from here at all", and that is the honest
answer: without a Talisman the Dungeon is not a shortcut, it is the only way
to win. With one, the surface is shorter and she should walk.

That is the whole of the AI's decision now. It used to be a guess -- a
Strength threshold and "no Talisman" -- and guessing made it dither: it went
down 72 times across 30 games and turned back 52 of them.

| | guessing | with the table |
|---|---|---|
| average game | 132 turns | **116** |
| descents | 72 | 90 |
| saw it through to the Treasure Chamber | 21 | **66** |
| turned back | 52 | 35 |

Asking the question from where she stands rather than from the door is what
stopped the dithering: half way down, turning back is waste, and
`ai_stays_below()` can see that where the old test could not.

Across 30 games the Crown is now reached 26 times through the Dread Gate and
16 times up out of the Dungeon -- so the second board is a real alternative
route rather than a curiosity. 30 base games and 20 with all expansions all
reach an ending with no invariant violations.

One thing the table does not know is difficulty: it treats beating the
Sentinel as one turn's travel, when it is a Strength 6 fight. Distances are
geometry, and the AI still needs its own judgement about whether it can
survive the road it can see.

## The City

The third board, and the one that changes the main board rather than adding
beside it: "The Talisman City board is a replacement for the City space in one
corner of the main Talisman board... the City space on the original board can
be considered as being one and the same as the City Gates." Rule 1 of the
expansion says the old City space's own contents are now redundant, so the
Market that used to be there is gone.

It is a plain ring, because the rules say so -- the City "remains part of the
Outer Region" and moves the same way, roll a die and go that many spaces. 24
spaces, which is exactly the 7x7 perimeter the main board's Outer Region uses.
Streets and Locations alternate, which is how the board itself keeps rule (b):
"it is impossible to get from one Location to another without first going"
through the street between them.

Entry is landing on the City space; leaving is reaching the Gates, or paying
2 Gold at the Wharf for passage to the Outer Region. The **Donjon is not on
the ring at all** -- you only reach it by being arrested -- so it is a Region
of its own at the middle of the board, which the region table made free.

Locations are off the board scan: the High Temple's two-dice prayer, the
Apothecary's potion, the Bank (Neutral and Evil characters "may attempt to rob
the bank"), the 6 Fates Inn with its Big Money card game, the Armoury, the
Stables, the Doctor's Surgery at "up to 2 Lives for 1 Gold each", the Magic
Emporium's 2 Gold Spell, the Royal Castle's Warrant to set on a rival, and the
Enchantress's blessing. One Location is deliberately missing: the Anarchists'
Guild, whose whole effect is changing Alignment, and Alignment has no
mechanics here yet.

**The Law, in the small.** Warrants, the Watch and the Donjon are in --
robbing the Bank or being denounced in the Town Square earns a Warrant, the
Watch (Strength 7) bar the Gate to anyone carrying one, and the Donjon's own
table settles it: bribe at 2 Gold, escape on a roll of 1, or be judged.
Not in: the posts (Sheriff, Master Thief, High Mage, King's Champion), loans,
stowaways, and the City's own Adventure deck -- `CITY_A/B/C.JPG` are in the
zip but not yet OCR'd, so street spaces draw from the main deck.

### Two holes worth naming

**The City was not on the map.** Adding a board is not the same as joining it
up: until the Gates and the Wharf were added to the distance graph, every City
space returned "no road to the Crown", which is a lie the AI would have acted
on. The Gates and the main board's City space are the same place, so they are
an edge in both directions.

**A warranted character would have loitered forever.** The first cut had the
AI prefer to stay in the City while carrying a Warrant, on the reasoning that
it could not leave anyway. But the Watch are the *only* thing that clears a
Warrant -- either she beats them and tries again, or she loses and the Donjon
settles it -- so staying kept it for good. A warranted character now makes for
the Gate.

### Testing what almost never happens

Across 30 games the City drew 27 visits, 13 prayers at the High Temple, 12 at
the Inn, 6 attempts on the Bank -- and **two** Warrants, neither of whose
holders then landed exactly on the Gates. So the entire Law was correct and
untested. Forcing it -- every character starting warranted at the Gate --
fired all of it: 64 confrontations with the Watch, 54 arrests, 10 escapes by
beating them, 18 escapes from the Donjon, and every branch of being judged.
Then the scaffolding came out and the hash-free regression was re-run.

**30 base games and 20 with all expansions, all ending, no invariant
violations, average 114 turns.** The Crown is reached 21 times by the Dread
Gate and 16 out of the Dungeon.

### The same trap, a third time

The scaffolding above did nothing the first time I ran it: I anchored the
patch on a line from *Prophecy* (`if (team_play)`), which this codebase does
not contain, and a `.replace()` with no assert changed nothing while reporting
success. Every patch here is supposed to carry `assert old in s` for exactly
this reason, and I wrote it without one anyway. The lesson is not "remember to
assert" -- it is that a silent no-op looks identical to a feature that does
not work, and the only defence is making the miss loud.

## The Timescape

The last of the three boards, and the one that is least like the others. It is
not a ring or a spiral but a **directed graph**: sixteen realities joined by
coloured warp lines, and the board's own legend gives the rule --

> Follow the coloured warp line corresponding with the dice roll shown below:
> 1,2 -- 3,4 -- 5,6. Movement is One Way Only (except from the Time Loop).

So every reality has exactly three exits, the die picks which one you take,
and rule 4 is blunt about the rest: "Characters have no control over their
movement... No Character may use Followers, Objects, Spells or Abilities to
affect where they move." There is no direction to ask for, which made
`TOPO_WARP` a third topology beside RING and PATH rather than a variation of
either.

Three other rules make it a strange place, and the design note on the board
explains why: each space "represents a complete separate reality". So
**characters never meet in the Timescape** -- two pieces on one space are not
really in the same place -- and `pvp_phase()` returns immediately there.

Every space effect is real. The board says "See Timescape Data Sheet" for six
of them, and the rulebook reproduces that sheet in full: the Vortex's exit
table, the Warp Demon at "Strength 12 and Craft 12" with your choice of which
to fight and a point of it if you win, the Rad Zone's mutations, Death World's
Aliens at 9, the Sentinels policing the place, the Nexus where you "draw five
Adventure cards, choose one you wish to encounter and discard the others", and
the Space Fortress with its Robo-Doc and Rogue Trader.

You get in through the Enchantress, exactly as printed on the City board and
in rule 3: roll two dice, and if the total is at most your Strength plus
Craft, a Warp Gate opens. Horses are left behind. You get out through the
Vortex, five of whose six exits put you back on the main board -- which is
also the edge that joins the Timescape to the distance map.

### The one part that is ours

The warp lines themselves. On the scan they cross and overlap so heavily that
tracing 48 directed edges off it reliably is not possible, and a half-traced
graph presented as the real board would be worse than an honest substitute. So
they are generated: from each reality the 1-2 line steps 1 on, the 3-4 line 5
on, the 5-6 line 11 on. All three are coprime with 16, which makes the board
strongly connected -- and that is the property that matters, because a
character who cannot reach the Vortex is a character stranded for the rest of
the game. Forcing traffic through it confirmed the intent: all sixteen
realities were reached, from Limbo at 8 visits to the Fourth Dimension at 31.

### Testing the parts the dice rarely reach

Seven characters entered the Timescape across 30 games and made 41 warp moves
between them, which left the Rad Zone and the Sentinels never once visited.
Starting every character in a different reality fired all of it: 342 warp
moves, 28 turns folded back by the Time Loop, 26 fights with the Warp Demon,
21 exits through the Vortex, 13 mutations, and every branch of the Sentinels'
judgement. The Aliens were fought seven times and never beaten, which is not a
bug -- Strength 9 is a great deal for a character who has just arrived.

**30 base games and 20 with all expansions, all ending, no invariant
violations, average 108 turns.** All four boards see use: 79 descents into the
Dungeon, 25 visits to the City, 7 journeys into the Timescape.

### A false alarm worth writing down

The first regression after this reported one game in thirty and no board
activity at all, which read as a serious hang. It was neither. The logs were
written to `run1.log ... run30.log` and aggregated with
`cat R*.log > r1.log` -- and this filesystem is case-insensitive, so `R1.log`
and `r1.log` are the same file and the aggregate truncated its own input. The
games had been fine throughout. Worth remembering that a measurement can fail
in ways that look exactly like the thing being measured failing.

## The City and Timescape card decks

Both boards were drawing from the Kingdom's Adventure deck, which the rules do
not allow: the City has its own stack, and the Timescape has forty cards of
its own. `CITY_A-C.JPG` and `TIME_A-B.JPG` in the print-and-play zip are five
5x4 sheets of them -- black line art on white, which OCRs far better than the
packed atlases the TTS route offered.

`extract-city-time-cards.py` slices and reads the sheets;
`parse-city-time-cards.py` turns the raw text into `CARDS-city-time.tsv`.
Split in two, like the other extractors here, because re-parsing must never
mean re-OCRing a hundred card faces.

**100 faces read, 76 named**, 27 carrying a printed Strength or Craft. The
text came out for all hundred; it is the *titles* that OCR loses, because they
sit in a display face over artwork. Three passes on the parser got it from 0
to 76, each fixing a real pattern rather than a guess:

- the copies digit often reads as a stray `I` or `l`, which a regex anchored
  at the front of the card trips over -- so the name is found token by token;
- `The WATCH` is a real card name, so a leading article had to be allowed;
- Enemies carry a subtype above the name (`Animal- GIANT FLY`,
  `SPIRIT- AIR ELEMENTAL`) which is a label, not part of it.

That yields **31 distinct City cards and 23 Timescape cards**, and their names
are plainly the real ones: Conscription, Cutpurse, Drunken Revelry, Heretic
Priest, Horse Thief, Corrupt Sheriff, The Watch; Dimensional Rift, Battle
Droid, Chameleon Suit, Combat Enviro-Suit, Gyro-Compass, Anti-Grav Platform.

### Two stacks, not two expansions

The obvious way to add them -- a `citydeck_proto[]` array of their own -- was
wrong, and the compiler did not say so. Items, Followers and trophies are all
keyed on an index into `deck_proto`, so a card living in a separate array can
be fought but never picked up, kept or counted. They are instead entries in
`deck_proto` like everything else, tagged `SET_CITYD` and `SET_TIMED`, with
`deck_build()` diverting those two sets into their own shuffled stacks rather
than into the Adventure deck.

That tagging is what keeps them honest under `--sets=all`, which turns on
every other expansion: the main deck stays at 701 cards, and across 30 games
no City or Timescape card was ever drawn on a Kingdom space.

**30 base games and 20 with all expansions, all ending, no invariant
violations.** 83 City cards drawn on City streets, and the Timescape realities
now deal from their own forty.

## Bug hunt

A deliberate sweep before starting on Mega-Talisman, on the theory that a
substrate about to carry twenty more boards should be checked first. Three
tools, in order of what they actually found.

### Sanitizers found a real buffer overflow

Built with `-fsanitize=address,undefined` and run over the seeded soaks,
UBSan flagged `board.c` immediately:

```
board.c:270: runtime error: index 7 out of bounds for type 'CellRef[7][7]'
```

The Timescape's layout used `r = 1 + (idx / 4) * 2`, which is 7 for realities
12 to 15 -- one past the end of a 7x7 grid. Those four wrote into the *next
board's* cells and were never drawn at all, so Death World, The Void, the
Space Fortress and the Sentinel Outpost existed in the warp lines but not on
screen. It never crashed, because `gridmap` is `[MAX_BOARDS][7][7]` and the
overflow landed inside a board nobody was using yet -- which is exactly the
kind of bug that waits for a fifth board to become a crash.

Rows and columns now run 0,2,4,6, with a bounds check besides. After the fix:
zero sanitizer findings across base games, all-expansions games, and a forced
run with characters scattered across the Dungeon, the City, the Donjon and the
Timescape. Prophecy is clean too, in all three of its modes.

### Reading found an asymmetry

`doorways_out()` counts Doorways across every space on the board -- that is
what enforces the two-Doorway cap. `first_doorway()`, which finds the one you
climb out to, searched only the Outer Region. A Doorway is an ordinary Place
card left wherever it was drawn, so two of them landing in the Middle or Inner
Region satisfied the cap while making rule 8.2's way out of the Dungeon
silently unavailable.

It now searches every Region of the main board and returns where it found one.
Verified by forcing a Doorway into the Middle Region with characters at the
Entrance: 23 climbs out where the old code would have offered none.

### Warnings found the small stuff

`-Wshadow -Wcast-qual -Wformat=2 -Wmissing-prototypes` and friends turned up
four things, all mine and all removable: two `(Player *)` casts dropping const
that were never needed (`eff_str` has taken a `const Player *` all along), an
`int i` shadowing the enclosing loop's own `i` inside the Cell, and the same
const cast in Prophecy's `space_worth`, which is now `const` as it should
always have been.

Both `glog`s now carry `__attribute__((format(printf, 1, 2)))`. That found no
existing bug -- several hundred call sites, all correct -- but a mismatched
format in a variadic logger is silent until it prints nonsense or reads off
the stack, and it is now a compile error instead.

### And hardening where it was safe only by accident

The Magic Portal has guarded its recursion with `depth >= 2` since it was
written. The Time Loop and the Chamber of Darkness, both added recently, call
`resolve_space()` again without one. Neither can currently cycle -- no warp
line leads back into the Time Loop, and the Dark Room's shift is never zero --
but both of those are properties of a data table rather than of the code, and
the table is generated. They use the guard now.

### Volume

**200 Talisman games and 210 Prophecy games** (70 in each of standard,
apocalypse and team play), all completing, no invariant violations, no
non-zero exits.

## The AI plays the odds

The rule the AI is built to: *never fight a war you cannot win, never
refuse one you are likely to.* Not a coward, not a fool.

That is arithmetic, not judgement. Every fight is `stat + d6` against
`stat + d6`, and the difference of two dice is triangular -- there are
`6 - |k|` ways to roll a difference of k -- so the chance of winning any
given fight is a sum over 36 outcomes:

```c
static int win_chance(int mine, int theirs)
{
    int start = 1 - (mine - theirs), k, ways = 0;
    if (start < -5) start = -5;
    for (k = start; k <= 5; k++) ways += 6 - (k < 0 ? -k : k);
    return ways * 100 / 36;          /* a tie is a stand-off, not a win */
}
```

Equal statistics is 41%, not 50: six of the thirty-six outcomes are ties,
and a tie ends your turn. The whole personality then hangs off two numbers:

```c
#define WORTH_FIGHTING 50   /* take anything better than even */
#define DESPERATE      33   /* never start anything worse     */
```

Enemies on a space, Spirits, and the Sentinel are all scored through it.
Spare lives buy worse odds at the Sentinel -- losing there costs a life and
you may try again -- but never hopeless ones.

### Ganging up, and not being a lamb

`lead_score()` ranks how far along each character is: region counted ten
apiece, the Talisman that got them there worth eight, then their best
statistic. Whoever tops it is worth **ten percentage points of worse odds**
to attack, because letting someone walk to the Crown unopposed loses the
game exactly as surely as losing a fight does.

The same applies at the end. Once anybody reaches the Crown the rest are
dying to the Command Spell whether they move or not, so `endgame()` drops
the threshold hard. Sitting still is not safety; it is a slower loss.

### Fate needs the die, not the deficit

Spending a Fate point rerolls **your** die, so you need a result greater
than what it showed plus the deficit. Rolling a 1 while two behind is a coin
flip; rolling a 4 while two behind cannot be saved at all. The deficit alone
cannot tell those apart, so `ai_use_fate()` takes the roll as well:

```
chance = (rolled + deficit >= 6) ? 0 : (6 - rolled - deficit) * 100 / 6
```

Spend above even, spend anything at all on your last life, and never spend
where no reroll could bridge it. Fate is not scarce here -- the Gnome, the
Guide, the Pixie, the Ring and the Tavern all give it back -- so hoarding it
is a slow way of dying. Measured over 80 games, hoarding cost about 6% more
deaths and bought nothing.

### What it is worth

The number that matters is not how long a game runs but whether the fights
the AI *chooses to start* are good ones:

| | fights started | attacker won | monster fights won |
|---|---|---|---|
| thresholds | 199 | 62% | 64% |
| probability | 192 | **66%** | **66%** |

Just as willing, better at picking.

### A bug that was hiding as an AI problem

Reading game logs turned up what looked like spells being cast at nothing:
"...but there is no magic here to undo." It was not the AI. `res_remove()`
shortens the resident list, so the `i == res_n[sid]` test for "found
nothing" became true the moment the spell succeeded on the last card:

```
Prophetess casts Destroy Magic!
  The Healer fades away.
  ...but there is no magic here to undo.
```

It worked, and then reported that it had not. 22 false reports across 40
games, now none. Worth remembering that a log can accuse the wrong
subsystem -- the AI had chosen correctly every time.

## Known gaps

Nothing here is a crash or a hang - 50 games across base and
all-expansions complete cleanly, and the carrying limit, spell limit and
trophy pools are all enforced. These are rules and features that are
simply not implemented:

**Missing rules**

* **Alignment** is now carried and shown -- every character has the Good,
  Evil or Neutral printed on her 2e card -- but no card or space keys off it
  yet, so it is currently colour rather than rule. **Day/night and Toads**
  are not implemented; the Blood Moon's Lunar Events are ordinary Events.
* **Evading** is only available through Invisibility and the Thief's
  robbery. That is closer to the rules than it sounds - evading always
  needs a card or ability to grant it - but characters have no general
  way to duck a fight.
* **Trophy remainders carry over** instead of being lost when turned in.
  More generous than the rules, deliberately.

**On the expansion boards**

* Both side decks are extracted and in play, but **24 of the 100 card faces
  lost their titles to OCR** and are not in the tables; and the *effects* of
  those that are come from this game, not the cards -- only the names, types
  and printed combat values are authentic.
* The City's **posts** - Sheriff, Master Thief, High Mage, King's Champion -
  along with bank loans and stowaways, are not built. Warrants, the Watch
  and the Donjon are.
* The **Timescape's warp lines are ours**, not the board's, for the reason
  given above; every space effect on it is real.
* The **Anarchists' Guild** is left off the City board because its whole
  effect is changing Alignment, which has no mechanics here yet.

**Generated rather than authentic**

* Expansion card **stats and effects are derived**, not printed values -
  see "Expansions" above. Strangers and expansion Places reuse base
  effects under their own names.
* **Teleport** goes to the crossing on your ring rather than a space of
  your choosing.


**Balance knobs**

* Win rates over 100 games run from Troll 31% down to Sorceress 8%. The
  Craft characters are still the weak end even with the Sentinel opened to
  Craft; their powers help less than raw Strength does.

* Monsters left on the board never expire, but they no longer pile up:
  the rulebook's draw rule caps a space, and that is now implemented -
  "the player only draws one new card to bring the total to two cards".
  You top a space **up to** its number, you do not add to it. Roughly nine
  unkilled monsters are on the board at any time in a finished game.
* Trophies are uncapped, so a very long game inflates stats past 20.
* The AI buys at most one thing per shop visit.

## Known deviations from the 2e rules

Kept honest rather than quietly rounded off:

- **Board-space text.** The Graveyard, Ruins and Crypt have Good/Evil effects
  printed on the board itself, which the rulebook does not reproduce and I do
  not have. Nor the curse that sends a cursed Character to "the Chapel (or
  Ruins, if Evil)".
- **The Bow** (13:2 as amended) has no subject: no Character in the 2e data
  carries one. Nor does any Character forbid Followers, which the Henchmen
  rule refers to.
- **Thirteen expansion Characters** have no power text, because their cards
  are not in the scans. They say so on the selection screen.
- **The Chameleon Suit** is the one Object with no value the AI can read; its
  OCR is too mangled to take a number off.


## Terminal size

The board is a fixed 86x29 -- it is a board, and it does not stretch. But
everything else now takes what the window gives it, rather than being laid
out for the minimum and leaving the rest black:

| | 86x29 | 110x34 | 160x40 |
|---|---|---|---|
| log lines | 3 | 8 | 15 |
| characters per page | 12 | 14 | 28, one page |

The log has no natural height, so it grows to the bottom of the window and
the prompt sits on the last row. The character list is two rows each -- the
powers are whole sentences off the real cards -- and goes to two columns past
150 columns, which fits all 28 base-plus-expansion characters on one page.

Resizing mid-game is handled: the layout is recomputed, and dragging the
window below 86x29 says what is needed and what there is until it grows
again.


## Chaos Bloodbath, and rule 13:2

**Chaos Bloodbath** (`[x]` on the startup screen, `--chaos`): "Use only one of
the Talisman cards, instead of all four. In addition, any player whose
Character is killed immediately loses the game." There are more than four
Talismans once the expansions are in, so the rule is read as its intent --
exactly one, total -- and death is elimination with no new hero. It does
what it promises: games run about 129 turns against 147 for the ordinary
game, and every one ends with three characters dead.

**Rule 13:2**, as amended: "A Character must choose to encounter either one
Character of their choice who is in that space, or in the space itself." Two
things were wrong before: you were never asked *which* Character, and landing
on an occupied space resolved the fight *and* the space, when the rule says
one or the other. Both fixed.

The amendment also allows firing "their Bow (if they possess one) at a
Character who is in range". No Character in the 2e data carries a Bow -- the
Samurai and the Centaur are not among the 52 -- so that clause has no
subject here, in the same way the Followers clause in the Henchmen rule has
none.


## Henchmen

Optional (`[h]` on the startup screen, `--henchmen`, or `TALISMAN_HENCH=1`),
and dealt rather than chosen: "We suggest that players not choose Henchmen
Characters as this would give them an unfair advantage." Each Character gets
one extra Character card, drawn at random from what is left.

A Henchman is a card, a count of Lives, and nothing else. He "cannot possess
any Gold, Objects or Followers", cannot raise a stat, and never has more than
4 Lives. Killed, he is out of the game and is not replaced.

He is useful twice over:

- **He fights for you.** "At the start of any combat, you can declare that
  your Henchman is going to fight on your behalf" -- monster or duel alike.
  He fights on the **base values printed on his card**: no Objects, no
  Followers, none of the Strength and Craft you have piled up, and no armour
  save when he loses. The wound is his, not yours.
- **He does things for you.** "The player can use any or all of the
  Henchman's Special abilities... These must be abilities the Henchman could
  pass on or use for you. For example, you could not gain a Henchman's
  resistance to a Siren's song but he could steal for you." The line drawn
  here is between what he *does* and what he *is*: thieving, mining,
  scouting and the destruction of Spirits pass to you; resistances,
  evasions, and his own way of swinging a sword stay with him for the fights
  he takes himself. Restrictions never pass -- a Henchman cannot make you
  forbidden to carry a sword.

Not implemented: "Players whose main Characters are not allowed Followers
cannot have Henchmen" -- no Character in the 2e data forbids Followers, so
the clause has no subject. Henchmen also count as Followers "wherever the
rules specify such", which is wired for combat but not for every card that
targets a Follower.


## Alignment

Every Character card prints an Alignment -- Good, Neutral or Evil -- and rule
7 gives it teeth:

- **Objects can demand one.** "The Assassin discovers the Holy Lance (a Magic
  Object) which can only be used by Good or Neutral Characters. He cannot use
  it because he is of Evil Alignment. He must leave it face up in the Space
  where he Encountered it." The Holy Lance is Good-or-Neutral; the Runesword
  is Evil-only. One gate in `item_give()` covers every route an Object can
  take to a Character -- found, looted, bought or handed over.
- **7:1 / 7:2** Alignment can change during a game, and "no Character,
  including the Druid, may change Alignment more than once in any Turn."
- **7:3** On changing, "any Magic Objects not permitted by their new
  Alignment must immediately be placed face up in the Space they occupy."
- **The Chapel is not for the Evil.** The Druid's own card spells the
  interaction out: "if you are carrying the Runesword and you wish to pray at
  the Chapel, you must drop the Runesword and leave it there."
- **The Druid** "may change Alignment at will" -- offered at the top of her
  Turn, and only when something on the board actually turns on it, so a human
  Druid is not asked the same question a hundred times.

Not implemented, because the rulebook does not carry the board-space text
(it is printed on the board): any Good/Evil effects on the Graveyard, the
Ruins, or the Crypt, and the curse mechanic that sends a cursed Character to
"the Chapel (or Ruins, if Evil)".


## The six ways to win

The 2e rules add an optional deck of six **Alternative Ending** cards:

> "Shuffle the six cards and pick one randomly; without looking at the card
> chosen place it facedown on the Crown of Command. The first player to
> reach the space reveals the card."

Turn them on with `[e]` on the startup screen, `--endings`, or
`TALISMAN_ENDINGS=1`. The card stays face down until somebody crosses the
Valley of Fire, so nobody -- you or the AI -- knows which game they are
walking into.

| Card | What is waiting | How you win |
|---|---|---|
| **Crown of Command** | the Command Spell: roll a die per victim, 4-6 they admit defeat or lose a Life, 1-3 nothing | sit on it and grind everyone down |
| **Demon Lord** | a Spirit, Craft 12, 4 Lives, Psychic Combat, behind a barrier that keeps everyone else out of the Valley of Fire | take all four of his Lives; flee to the Plain of Peril and he heals |
| **Pandora's Box** | a chest that hands you 1d6 Spells a turn to hurl at anyone on the board | be the last one standing |
| **Belt of Hercules** | a flat Strength of 12, Teleportation and 5 Lives -- but you must go to them | duel every other character to death |
| **Horrible Black Void** | nothing at all: the first one across is annihilated with everything they carry -- unless the Timescape is in play, which spits them out at the Warp Gate | you don't -- the card is discarded and a new one laid down |
| **The Dragon King** | one die, six futures: an outright win, three fights, a knife in the dark, and being eaten | roll well |

The other rule changes from *Talisman the Adventure* that are in: **Mules
now carry eight Objects** (rule 5:3 makes a Mule unlimited -- the worked
example has a Wizard carrying ten -- and the Adventure caps it). Still to
do: **Henchmen**, the **Chaos Bloodbath Option**, the Tavern gambling
amendment, and rule 13:2's choice of encounter.

Because crossing the Valley of Fire can now kill you outright or drop you in
front of something you cannot beat, the Dread Gate is a **decision** rather
than a formality -- `[c] cross` or `[w] wait`. Without the optional rule the
old certainty holds and you simply walk through.

### Six endings, six strategies

Each card is a different game, so the AI plays each one differently, and only
commits at the moment of the reveal:

- **Crown of Command** -- someone on the throne is killing you from a
  distance. Rush it and contest.
- **Demon Lord** -- Strength is worth nothing; Craft is everything. The AI
  will not cross without the Craft to win, goes to the Temple to find it, and
  *retreats* rather than feed him. The barrier means there is nothing to
  contest, so the game gets decided out on the board instead -- picking
  fights becomes the productive move, not the wasteful one.
- **Pandora's Box** -- the bolts reach everywhere, so there is no waiting it
  out. Rush hardest. The thrower aims at whoever is winning, and finishes
  anyone on their last Life first, since after the Crown is reached the dead
  stay dead.
- **Belt of Hercules** -- the wearer must come to *you*, so racing the Crown
  gains nothing once the Belt is gone. Hunt the wearer instead -- killing
  them sends the Belt back -- but only if you can actually take them.
- **Horrible Black Void** -- one card in six annihilates whoever crosses
  first. A character in second place lets the leader test the water; a
  character in the lead goes anyway, because waiting loses just as surely.
- **Dragon King** -- a lottery worth entering, but not on your last Life.

