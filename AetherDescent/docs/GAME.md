# Aether Descent

A terminal roguelike in C11. One hundred floors down, a steampunk temple sunk
into the ground rather than raised out of it, and a city on no Imperial chart
sitting on top of it.

This document describes **what the game is and what it does**. The reasoning
behind the numbers lives in [EVALUATION.md](EVALUATION.md); what is still
unfinished lives in [ROADMAP.md](ROADMAP.md).

---

## Running it

```bash
make          # ncurses build   -> bin/aether-descent
make notcurses   # notcurses build (colour, tile art)
make test     # three suites: invariants, balance, tile art
make sim      # headless winnability harness
```

```bash
./bin/aether-descent
./bin/aether-descent --seed 44815      # a specific world
```

`AETHER_STATE_DIR` overrides where saves, highscores and settings are kept
(defaults to `$HOME`). A run in progress is saved on quit and resumed by name.

---

## The premise

Your ether-ship came down through the canopy. You walked out of the wreck
alone and three days later the jungle let you go, into a city that is not on
any chart: brass domes gone green, ether-lanterns still guttering, vine roots
thick as a man's waist through every window. Somebody still lives there, and
they still take good coin.

At the heart of it, a temple sinks into the ground. The locals call the bottom
of it the Deep Well, and they say it in the tone people use for a debt.

One hundred floors. Nobody has come back up to say whether that is really
where it ends.

---

## The shape of a run

1. **Pick a difficulty**, then an archetype, then a class within it, then a
   name.
2. **Shop in the city.** You start with 60 gold — enough for a weapon and a
   couple of heals.
3. **Go down.** Each floor is a self-contained level: find the `>` stairs,
   fight or avoid what is between you and them, take what is worth taking.
4. **Come back up** through a waygate — every five floors on the small worlds,
   every floor on the big ones — or by cracking a recall charm, or by walking.
   Spend, repair, hire, upgrade.
5. **Keep going** until something kills you or you reach floor 100.

Death is not permanent except on Hardcore: you wake at the Inn, keeping your
character and your deepest floor, and can descend again directly to any depth
you have already reached.

---

## Difficulty

Chosen once, at character creation, and it never changes.

| Mode | What changes |
|---|---|
| **Normal** | The baseline. |
| **Hard** | 2–3× the monsters. One floor in eight is **overrun** — it generates at Swarm density and keeps producing while you are on it. |
| **Swarm** | 10–20× the monsters. Same stats per monster, same fair line-of-sight aggro. It is the numbers that change, not the danger per monster. XP and gold per kill are cut to compensate. |
| **Hardcore** | Swarm's density, and no safety net: monsters never respawn once cleared, but recall charms are disabled outright. |

Difficulty is separate from **escalation**, below, which applies in every mode.

---

## Character

### Archetypes and classes

Seven archetypes, one hundred classes between them:

| Archetype | Leans on | Classes |
|---|---|---|
| Vanguard | might, brawn, grit — stands in front | 14 |
| Skirmisher | agility, reflexes, balance — fast and slippery | 19 |
| Marksman | precision, vision — kills at a distance | 12 |
| Arcanist | the six aether attributes — casters | 13 |
| Artificer | tech-wit, crafting, chemistry — builders | 16 |
| Survivor | fortitude, stamina, instinct, fortune — endures | 12 |
| Envoy | charm, guile, presence — leads and talks | 14 |

A class sets your starting attributes, your magic school, your active ability
and your opening gear. Classes unlock by depth: reaching deeper floors makes
more of them available on future characters.

### Attributes

Thirty of them, in six groups of five — physical, defensive, perceptual,
mental, social, and technical/aether:

```
MIGHT BRAWN AGILITY REFLEXES PRECISION      GRIT FORTITUDE STAMINA BALANCE VISION
HEARING INTELLECT CUNNING MEMORY RESOLVE    INSTINCT CHARM GUILE PRESENCE EMPATHY
TECH-WIT CRAFTING CHEMISTRY SCRIBING AETHER-SENSE   CONDUIT RESONANCE WARDING FORTUNE JINX
```

Every attribute feeds at least one derived stat: crit chance, evasion, ward
(the chance a spell glances off), gold find, XP find, HP regeneration, spell
power, aether pool, ammunition capacity, hazard resistance, shop discount,
shrine luck, machine luck. Nothing is decorative.

Attributes are fixed at creation and afterwards move only at the **Gladiator
School**, one point at a time, for coin — or for a **writ of training**, which
pays the fee outright. Writs come from one place: finishing all ten waves of a
colosseum. Nothing in the dungeon grants an attribute point directly, by
design — the School is the only door.

### Active ability

One per class, keyed to its strongest attribute, free to use and gated by a
cooldown — pressed with `a`:

Crushing Blow · Adrenaline · Evasive Roll · Riposte · Called Shot ·
Unbreakable · Iron Resolve · Second Wind · Steady Footing · Predator's Sense

---

## Combat

Melee has no attack key: you walk into something.

```
damage = attacker_attack² / (attacker_attack + defender_defence)
hits you survive = HP × (attack + defence) / attack²
```

Attack is squared; your gold buys HP and defence linearly. That means
survivability rises with the *square* of what you invest, and it is the single
most important fact about this game's balance — the reason deep floors are
hard, and the reason they were once impossible.

**Escalation** is a global multiplier on monster *attack only*:

```
pct = level × 1.0%  +  floor_entries × 0.5%      capped at +25%
```

It lands on attack rather than HP or defence, because those make fights
*longer* rather than riskier, and rewards are excluded so a long run never
pays for its own difficulty. The cap is deliberately low: at +100% a level
gained made every monster permanently harder, which punished exactly the
grinding an RPG should reward.

**Status effects:** poison (damage per turn), stun (cannot act), slow (50%
chance to skip a turn). They apply to monsters and to you.

**Monsters** aggro only when they can actually see you — line of sight is
real, and thicket and walls both block it. 54 monster templates across four
depth tiers, plus elites every ten floors, biome bosses on floors 15, 35, 60
and 85, and the Warden at the bottom.

---

## Magic

Six schools, 180 spells, thirty per school across five power levels:

| School | Does |
|---|---|
| **Conduit** | offence — bolts, blasts, scorched ground |
| **Resonance** | enchantment — attack, defence and crit buffs |
| **Warding** | protection — shields, barriers, purging hazards |
| **Aether-Sense** | utility — blink, reveal, conjured bridges |
| **Scribing** | control — stun, slow, unbinding locks |
| **Jinx** | curses on the things you hit |

Your class assigns your school; you can only learn from that one. Spells are
bought at the **Arcanist's Guild** and cast with `m`, or `s` to repeat the
last one without opening the menu. Casting spends **aether**, which regrows by
one point per turn — resting does not top it up, so a caster's real limit is
how fast they walk between fights. Each spell also has its own cooldown.

Some spells alter terrain — scorching ground, raising barriers, bridging
water, purging miasma — and the map remembers it.

---

## Ranged weapons

Eighteen in stock: six types, three tiers each. They are not a damage-number
reskin — each type behaves differently.

**Bow · Gun · Laser · Blowgun · Thrown · Grenade**

Fired with `f`, spending ammunition (`AM` on the sidebar), which regrows by
one point every four turns — like aether, it is paced by walking rather than
by resting. Weapons also have a firing cooldown of their own. Lasers pierce,
grenades splash, blowguns poison, and so on. Bought at the **Armory**.

---

## Items and gear

- **Weapon** and **armour** slots, plus a **ring** and a **trinket**.
- **Consumables** — healing draughts, ether tonics, guard tonics, rations,
  recall charms. Six kinds in the general store, more found below.
- **Gear sets** — five sets, one per biome (Root-Drowned, Foreman's,
  Custodian's, Salt-Cured, Warden's), four pieces each: weapon, armour, ring,
  trinket. Wearing more pieces of a set grants escalating bonuses that apply
  to the real combat maths, not just the display. Set pieces are found in
  vaults, dropped by elites and bosses, and never sold in shops.
- **Junk** — scrap, worth nothing but coin. It cannot be equipped or used. You
  accumulate one piece per hundred steps walked, and its value doubles with
  each biome, so a deep haul is worth carrying home. Sold at the Junkyard.
- **Keys** open `+` locked doors. **Levers** open `D` sealed doors, one per
  floor at most, and the vault behind them is worth the walk.

### Upgrades

Bought at the shops and applied to whatever you have equipped, so they are
never lost by changing gear:

| Track | Step | Applies to |
|---|---|---|
| Weapon +N | +5 attack | equipped weapon |
| Armour +N | +5 defence | equipped armour |
| HP +N | +10 max HP | you |
| AE +N | +3 max aether | you |
| AM +N ("extra shot") | +2 ammunition | you |

Hired heroes inherit your weapon and armour `+N` — they cannot choose gear, so
they carry yours.

---

## The city

Ten buildings around one plaza, each entered by walking into its door.

| Glyph | Building | What it does |
|---|---|---|
| `G` | **General Store** | consumables, recall charms |
| `M` | **Armory** | weapons, armour, ranged weapons, and the `+N` upgrade tracks |
| `P` | **Apothecary** | the heavier draughts and tonics |
| `A` | **Arcanist's Guild** | learn spells from your school |
| `X` | **Gladiator School** | train one attribute, for coin — or spend a **writ of training** (`w`). The only way an attribute moves after creation |
| `B` | **Bank of the Deep Well** | buys a **gold multiplier** — see below |
| `R` | **The Venusian Track** | lizard races. Six runners, your stake, and the only money in the game that carries no experience |
| `V` | **The Brass Lantern** (tavern) | hire heroes to come down with you |
| `J` | **Junkyard** | weighs your scrap, pays out, no menu |
| `I` | **The Inn** | heals you to full and saves a snapshot. Costs a fifth of everything you are carrying, every time — so resting is never free, and gets more expensive the richer you get |
| `T` | **Temple of the Deep Well** | the way down |
| `K` | **The Ashfall Kitchen** | you hunt the meat, the house cooks it |
| `&` | **The Black Market** | buys levels off you, no questions asked |
| `Q` | **Quest board** | take a bounty |

### The Bank

Sells one thing: a **share** that multiplies every coin you earn from then on
— kills, floor gold, bounties, machines, and the scales at the junkyard alike.
The price is cubic in the shares you already hold:

```
x2   1,500          x10   1,093,500
x5   96,000         x50   176,473,500        x100 (ceiling)  1,455,448,500
```

Cubic because what is being bought is itself a multiplier on all future
income. It does not lend and it does not buy back.

### The Venusian Track

Six runners. The book is **generous early and mean late**: the first three
races of a visit return about 1.16 per gold staked, and every race after that
returns 0.82. Going back down and coming up again is what resets him. Every
runner on a ladder is the same bet — there is no trap pick and no runner you
are supposed to know to avoid, only longer odds for longer prices.

By default a bet resolves instantly. `w` backs it *and watches them run*.

It is a small, bounded income and it is meant to be: over an entire run,
farming every generous race turns 500 gold into about 800, and better than a
third of players end down. It is a flutter, not a wage.

### The Ashfall Kitchen

You bring the meat up; the house cooks it. **Meat only comes off monsters you
killed with a blade** — a body burst by fire has nothing on it, an arrow ruins
the cut, and a companion leaves a mess. That one rule makes the pub a build
decision: a pure caster walks past it entirely, and a fighter who ignores it
is leaving a second income on the floor of every corridor they cleared.

Four grades — **stringy, fair, prime, mythic** — set by depth, nudged up by
notably heavy kills, and a boss is always the best cut in the house.

A **service night** is seven covers, one at a time. Each patron came in for a
particular grade and pays best for it. Serving better than they asked pays a
little more and pleases them a lot; serving worse pays almost nothing and the
house hears about it. Reputation runs 0–100 and multiplies every payout from
**x0.50** at an unknown room to **x3.00** at the best table in the city — so
the restaurant is a second progression track that runs entirely on decisions
and never on your character level.

One night per visit, and the larder is finite, so it cannot be farmed. The
payout is deliberately front-loaded: several upgrade rungs a night when you
are poor, under one a night at the bottom of the temple.

### The Black Market

Sells nothing. **Buys levels.** Hand one over and you lose 8 max HP, a point
of attack, a point of defence if it was an even level, and any progress toward
the next one — and you walk out with coin.

Choose how many to sell in one go: Left/Right for one at a time, Up/Down in
tens, `a` for everything. You steer by the level you want to end on, and every
consequence is on screen before you agree to it — the gold, the stats, and what
the monsters will carry afterwards.

The price is set by **how deep you have actually been**, not by what level you
have reached — a level is a level wherever you earn it, and grinding easy
floors to sell the results paid better than descending. A farmer who never
leaves the first floor sells levels for pocket change.

There are **no limits** on the trade itself. No cap on how many you sell, no
cooldown. Selling down and re-earning the cheap levels
somewhere safe is a supported way to play, not a hole. The only refusal is at
level 1, because there is nothing underneath it.

Two things worth knowing before you lean on it, both shown on the screen:

- Escalation is **capped at 25%**, and roughly half of it comes from floors
  *entered*, which no sale can touch. Past about level 17 selling levels
  usually changes the difficulty by nothing at all.
- Levels are cheap to re-earn and expensive to sell — the price is quadratic
  in level, the experience is linear. That asymmetry is deliberate.

### Hired heroes

Up to five, for a party of six. They are generated to match your actual
standing — a fighter hiring a caster gets a *proficient* caster, with a real
spellbook and real weapons, not a fighter in a robe.

**They are not an escort.** Once you are on a floor they go their own way:
they explore it, fight what they find, and pick up what they trip over,
without any reference to where you are. They will happily end up on the far
side of the map. Everything they reveal, you see — that is what you are paying
for, along with five more sets of hit points somewhere in the building.

Personality is pure aggression, and it decides how far they range and how much
punishment they take before they stop looking for more:

| | ranges | notices | breaks off at |
|---|---|---|---|
| **Bold** | far | far | never — this is why Bold heroes die |
| **Steady** | middling | middling | 20% health |
| **Cautious** | close | close | 40% health |

Releasing one hands back **half** the fee. A hero who died in your service
hands back nothing.

### Quests

Three kinds of bounty from the board: **kill** a named monster on a named
floor, **clear** a quota of monsters on a floor, or **fetch** an item planted
on a specific floor. Gold and XP on completion.

---

## The dungeon

**You choose how big a floor is** when you start a run, right after the
difficulty. It cannot be changed afterwards — every floor of the run is built
to it.

| | Tiles | Walkable | Monsters/floor | Feel |
|---|---|---|---|---|
| **The Shaft** | 140 × 80 | ~6,000 | ~60 | cleared in minutes; the stairs are never far |
| **The Halls** | 350 × 200 | ~39,000 | ~360 | room to get lost without losing the afternoon |
| **The Deeps** | 700 × 400 | ~157,000 | ~1,450 | each floor is a journey |
| **The Well** | 1400 × 800 | ~627,000 | ~5,800 | crossing one floor is the evening's work |

Roughly 55% of every size is walkable, and every cap — rooms, monsters, items,
features, districts, respawn and doubling intervals — is counted off the area,
so the recipe holds at all four. The camera follows you, and `M` shows the
whole floor scaled down. Five biomes by depth:

**the Sunken Jungle Roots → the Company Works → the Flooded Ruins → the Salt
Wastes → the Abyssal Approach**

### Rooms

Five shapes, mixed by area rather than by flat count, so the recipe scales
with the map:

| Shape | Size |
|---|---|
| Plaza | 16–26 × 10–16 |
| Hub | 10–14 square, with extra corridor spokes |
| Hall | 14–24 × 4–6, either axis |
| Chamber | 7–11 × 6–9 |
| Cell | 4–6 square |

Rooms join their *nearest* neighbour rather than a random one, so corridors
are short and the floor falls into neighbourhoods on its own. A corridor is a
long room and has a width: one in four is two tiles across, one in twelve is
three, and a hub's approaches always are.

### Wild districts

The floor is a cave that people built inside, and the cave was there first.
Roughly 57% of floors have at least one district that was never built on,
placed *before* the rooms so the rooms go around it:

| Kind | Terrain |
|---|---|
| **Jungle** | ragged ground, thicket clumps, pools, some miasma |
| **Sea** | standing water, islands, causeways laid over it |
| **Swamp** | mottled water, floor and gas; reeds |
| **Ruins** | streets and small buildings, a third of the walls collapsed |
| **Mycelium** | bioluminescent mats and caps — *the mats carry your footsteps* |
| **Hive** | comb-work cells — *hurt one and they all know* |
| **Mire** | standing water under low mist — *you see three tiles* |
| **Crystal** | see-through crystal veins — *spells and shot ricochet back* |
| **Quiet Quarter** | streets where nothing is awake — *until you start something* |
| **Storm-Cage** | open plateau of iron rods — *lightning strikes one every five turns* |
| **Colosseum** | a bowl of stained sand — *step on and the gates come down* |
| **Aqueduct** | cut channels of running water — *step in and it carries you* |
| **Blood Marsh** | warm red shallows — *what wades in it stops staying hurt* |
| **Chromatic Abyss** | pools of liquid light — *the ground you fight on is a choice* |
| **Assembly Line** | belt-work still running — *lanes alternate; you cannot walk against one* |
| **Carnivorous Garden** | flowers with teeth among pollen — *8 damage, held 2 turns* |
| **Quicksand Basin** | pale sand with a ripple in it — *3 damage, held 4 turns* |

A **snare** (`&`) is ground you can walk into and nothing else can. That is the
trade: in a snare field you will not be surrounded, and you cannot run
either.

Three of those seven are **rules, not terrain**. In the mycelium anything
within 20 tiles hears you walk, with no line of sight needed — but stand
still for two turns (`.`) and the mats forget you. In the hive, hurting one
monster wakes and wounds everything within 20 tiles, and nothing hides you.
In the mire, mist caps your sight at three tiles whatever your Vision.

Three more change what your character is good at. In the **crystal**, three
casts or shots in ten come back at you for half — melee is untouched, and it
is the one place a sword beats a spellbook. In the **quiet quarter** nothing
notices you past three tiles, but damage anything and the entire quarter is
awake for good. In the **storm-cage** a rod is struck every five turns, one
turn after it starts to sing: stand two tiles clear, or put something else
beside it, because whatever touches a struck rod dies.

The **colosseum** is the exception to all of them: walk onto the sand and the
gates shut for ten waves, each fighting as if the floor were deeper than it
is. They open again after five, so pressing on is a choice you make five
times — and wave ten pays a **guaranteed gear-set piece**, the only place in
the game you can go and get one on purpose.

Two more change the fight without touching you at all. The **blood marsh**
heals any monster standing in it, five a turn — the first terrain that is
good for them and neutral for you. The **chromatic abyss** is the opposite: a
red pool gives +50% attack and −50% defence, a blue one the reverse, a green
one mends you slowly. It applies only while you stand there, so the right
pool changes mid-fight.

Each district tints its own ground, so you can see where you are.

Which kind appears is weighted by biome. **Thicket** (`"`) blocks movement
*and* sight — that is what makes a jungle fight start at four paces.

Arriving on a floor tells you what is on it.

### The camp

One floor in six has somewhere you can stop: a walled compound with three
gates, tents inside, always a fountain, usually a trader, sometimes a shrine.
**Nothing spawns inside it and nothing respawns into it.** Rare on purpose — a
safe room you can count on is a rest stop; one you cannot is a relief.

### Features and terrain

- **Shrine** — a blessing, or not, depending on your luck.
- **Fountain** — drink; it may not be water.
- **Merchant** — a trader who came down and stayed.
- **Machine** — ancient equipment that may pay out, reveal the map, or wake
  something up.
- **Relic** — a set piece.
- **Waygate** — free passage to town. One every fifth floor on the Shaft and
  the Halls; **one on every floor** on the Deeps and the Well, where climbing
  back four floors would be an evening of retreading cleared ground.
- **Console** (`C`) — in an assembly line. Turn three keys and the works
  build you a golem: slow, sturdy, and yours until you leave the floor.
- **Toll** (`$`) — on the Deeps and the Well only. Hand over everything in
  your purse and the tollkeeper walks you to the down stairs. Total price, on
  purpose: it is for the bad moments, not for convenience.
- **Altar** — every tenth floor. Sacrifice half your current HP to double all
  gold found for ten floors.
- **Portals**, paired, on some floors.
- **Lava** and **miasma** are walkable and hurt; **water** is not walkable, and
  bridges cross it.

### Floor events

A floor may announce itself on arrival: Ambush · Trader's Camp · Old Ones
Shrine · Flooded Passage · Toxic Vent Rupture · Buried Cache · Raiders' Den ·
Ancient Machinery Awakens · Eerie Calm.

Two special floor kinds, one in eight each:

- **Overrun** (Hard only) — Swarm density, and it keeps producing.
- **Gold rush** — ordinary monsters, twenty times the coin lying on the floor.

### Pressure

Standing still costs something. Every doubling interval spent on one floor,
the population doubles — so a floor you are picking over slowly becomes a
floor you have to leave. On Normal and Hard a slower trickle also refills
cleared ground; Swarm and Hardcore start saturated instead.

---

## Controls

```
Moving      h j k l      left, down, up, right
            y u b n      diagonals
            arrows       the same, if you would rather
            into a monster  attack it — there is no separate attack key
            into a door     enter the shop, inn or tavern

Fighting    m  cast a spell        s  cast the last one again
            f  fire your ranged weapon
            a  class ability       r  recall charm, back to town

Looking     i  inventory           c  character sheet (and your run seed)
            M  the whole floor at once
            .  wait a turn — in the mycelium, waiting hides you
            g  set to work: cut ore from a storm rod, strip a wreck in the
               ruins. Takes turns, and being hit ruins it
            x  auto-explore: walks, fights and loots for you
            ?  keys                q  quit (a run in progress is saved)
```

Auto-explore walks to the edge of what you have revealed, picks up what it
finds, fights what blocks it, avoids lava and miasma when there is a way
round, and never takes the up-stairs.

**It does not stop at a floor boundary.** When it finds the way down it takes
it and carries straight on exploring the next floor, and the next — a floor
boundary is a doorway, not a decision, and the point of delegating the walk is
that you can stop watching. Left alone it will descend as far as it can.

It stops for reasons that are actually yours to answer:

- **any keypress**
- **badly hurt with nothing to drink** — it will drink a healing item on its
  own below half health, but at a quarter with none left it hands back
- **no observable progress** for sixty steps running
- **the Warden's floor** — it will walk you to floor 99 and no further

---

## Colour

A floor is **stone with its biome growing through it** — the Jungle green, the
Works amber, the Ruins white, the Wastes red, the Abyss violet — showing in
patches, not as a coat of paint. About three quarters of the ground and walls
stay bare rock, and the rock between the growth is what makes the growth read
as growth.

**Wild districts** are the same idea turned up: inside one, roughly two thirds
of the stone is covered in that district's own colour, so walking into a jungle
quarter reads as arriving somewhere without turning into a green rectangle. A
jungle inside the Abyss is green against violet.

Roughly a third of every floor is wild, on every world size, and no floor is
bare rock with a biome's name over it. Stairs, doors, water, lava and features
keep their own colours everywhere — those carry information, and scenery must
not overpaint them.

The whole-floor map (`M`) colours districts only, against neutral rock. One
map cell covers a whole block of tiles, so there is nothing to speckle there,
and what is worth seeing at that scale is the *shape* of the places.

## Presentation

Two rendering backends behind one interface:

- **ncurses** — glyphs and sixteen colours. Works anywhere.
- **notcurses** — true colour, and an 8×16 pixel sprite for every tile, with
  per-biome palette tinting. Selected on the display screen; falls back
  automatically when the terminal cannot do it.

Combat is animated in both: hit flashes, coloured bolts, beams and bursts for
ranged fire and spells, and volleys from your hired heroes.

The minimum terminal is 80 × 24; every screen pages and clips to fit whatever
you actually have, and grows to use the whole window when there is more.

---

## Persistence

- **Mid-run save/resume**, keyed by character name. Quitting saves; dying does
  not lose the character (except on Hardcore).
- **Highscores** and **class unlocks** by deepest floor, kept across runs.
- **Settings** — render mode and display preferences.
- Save files carry a header with a magic number, a version, and the sizes of
  the structures they contain. A mismatch is rejected cleanly rather than
  loaded into a crash.

Every floor is derived from `(run_seed, floor_number)`, so *floor 12 of seed
44815* always means the same floor. Your seed is on the character sheet.

---

## Source layout

```
src/
  main.c        the turn loop, town flow, every screen's key handling
  common.[ch]   the shared types: Map, Player, Monster, Tile, and the caps
  mapgen.c      floor generation: rooms, districts, camps, features, FOV
  monsters.c    monster templates, spawning, respawn, escalation
  combat.c      the damage model, XP and the single gold funnel
  spells.c      the 180-spell pool and every effect
  ranged.c      the six ranged weapon behaviours
  abilities.c   class active abilities
  companions.c  hired heroes: generation, equipment, AI, animation
  items.c       stock tables, prices, upgrade and bank curves
  classes.c     100 class seeds, attributes, derived stats
  gearsets.c    the five biome sets and their bonuses
  features.c    shrines, fountains, machines, altars, relics
  render.c      all drawing, for both backends
  tui.[ch]      the ncurses-surface shim over notcurses
  tileart.c     8×16 sprites and biome tinting
  path.c        the shared breadth-first searches
  save.c        versioned save/load
  town.c        the city layout

tests/          invariants, balance, tile art
tools/simulate.c  headless winnability harness — plays real runs, no rendering
```
