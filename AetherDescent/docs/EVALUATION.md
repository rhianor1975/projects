# Aether Descent — Project Evaluation

*Last updated: 2026-08-10, after the Hero refactor (§90): one body struct, one
constructor, and the end of `Player` meaning both "the party" and "the
protagonist". Balance is unverified against it -- see §90's last section.*

*Previously updated 2026-08-02, after the render modes, the auto-explore hunter,
the curated spell pool and the difficulty retune landed. All seven audit issues
are fixed and verified; `docs/ISSUES.md` has been retired.*

---

## 1. What this is

A terminal roguelike in C11, drawing through either ncurses or notcurses
(see §7), in text, block or tile graphics. ~8,900 lines across 18 translation
units plus a test suite. 100 floors, 5 biomes, 100 player classes, ~210
spells, 18 ranged weapons, 10 class abilities, 5 gear sets, 3 quest types, 4
difficulty modes.

```
src/render.c    1620   all drawing, screens, animations, the three render modes
src/main.c      1271   game loop, state machine, shops, auto-explore
src/tileart.c    890   the tileset: 36 sprites + colours + the rasteriser
src/mapgen.c     759   floor generation, FOV, pathing primitives
src/spells.c     700   spell pool: 180 curated spells + cast logic
src/tui.c        592   terminal abstraction: ncurses / notcurses backends
src/monsters.c   450   monster roster (curated + procedural)
src/combat.c     375   damage model, monster AI, status, XP
src/save.c       258   highscore, suspended run, per-class records, settings
src/items.c      247   shop stock, consumables, loot tables
src/classdata.c  226   100 class seeds
src/gearsets.c   142   5 biome gear sets + set-bonus math
tests/           526   balance model, invariants, tile art
tools/artdump.c  196   renders the tileset to PNG for review (`make art`)
```

---

## 2. Code health

**Warning-clean.** Under `-Wall -Wextra -Wshadow -Wconversion -Wsign-compare
-Wpointer-arith -Wcast-qual -Wstrict-prototypes`: 3 warnings, all the same
benign `Biome`-enum-to-`int` signedness conversion. No shadowing, no lossy
implicit conversions, no missing prototypes.

**Clean under AddressSanitizer + UndefinedBehaviorSanitizer** (`make asan`).
Driven repeatedly through Swarm-mode play, full-floor auto-explore, every UI
screen, shop confirmation paths, quest flows, and save/resume round trips.
Zero reports.

**Automated tests** (`make test`), linked against the real game modules
rather than copies of the logic:

- `tests/balance.c` — simulates a run using the real XP curve and per-level
  stat gains, then tabulates player-vs-monster damage at seven depths. Fails
  the build if either side's damage collapses or a fight becomes a slog.
- `tests/invariants.c` — hazard-avoidance predicates, monster-array
  compaction (including degenerate cases), damage-model monotonicity and
  clamping, gear-set completeness and depth ordering.
- `tests/tileart.c` — the tileset's table, rasteriser and viewport budget:
  every art id has a row, every sprite is exactly 8x16 of palette characters, every `TileType`,
  `FeatureType` and monster faction maps to real art, and the rasteriser
  fills opaque cells completely, leaves transparent pixels alone, only ever
  darkens when dimming, and survives degenerate sizes. This exists because
  the graphical modes can't be screenshotted by the harness (§7) — these are
  the failures that would otherwise surface as a magenta `?` on floor 60. It
  also pins tile mode's per-frame pixel budget across six cell sizes, which
  is what decides whether that mode is playable.

All three suites currently pass. Neither touches the save file, so `make test` is
safe with a run in progress.

**Architecture.** Module boundaries are real and have held up through heavy
feature work. Shared primitives got factored out when a second caller
appeared rather than being copy-pasted. The one deliberate layering
compromise — gameplay modules calling `render.h` for combat animations — is
documented where it happens.

---

## 3. The two structural fixes

Both audit findings that would have compounded over time are resolved.

### Save integrity

The old format carried a hand-maintained version number that had not been
bumped across many struct changes. Because an older save is usually *larger*
than the current structs, both `fread`s succeeded and a run loaded with every
field read at the wrong offset — producing states like `HP 47/22`, `Level 0`,
and control bytes in item names.

The header now carries `sizeof(Player)` and `sizeof(Map)` and a load refuses
anything that doesn't match the running build exactly, so the check can't rot
even if someone forgets the version constant. On top of that: exact file
length is verified, trailing bytes are rejected, and a loaded run must pass a
sanity gate (coords in bounds, `0 < hp <= maxhp`, valid level/class/biome,
container counts in range) before it becomes live state. `line_of_sight()` —
which indexed the tile array with no bounds check — is now guarded at both
endpoints and every stepped cell.

Verified against five crafted files: an honest save resumes; old-layout,
truncated, and trailing-junk saves are rejected without even offering the
resume prompt; a header-valid file with an impossible player position is
caught by the sanity gate. All rejected files are deleted rather than left to
fail again.

### The damage model

Damage was `atk - def` floored at zero. Player defence outgrew monster attack
by a widening margin, so from roughly the first set of biome armour onward
**every ordinary hit dealt exactly zero** and the only damage left in the game
was a 5% unavoidable crit.

Two coupled causes, both addressed:

1. **Formula.** Now `dmg = atk² / (atk + def)` — diminishing returns instead
   of subtraction. Armour is always worth stacking (def == atk halves a hit,
   def == 3×atk quarters it) but can never nullify one. Extracted as the pure
   `damage_after_defence()` in `combat.c` so tests exercise the shipping
   formula directly.
2. **Growth rates.** The player gains ~1.5 levels per floor (+12 max HP, +1.5
   attack each) while monster attack grew at `floor/3` — roughly a third of a
   point per floor. Monster attack now tracks depth 1:1, which leaves floor 1
   essentially unchanged and only diverges where the player's own growth has
   already run away.

Measured result (`make test`), monster hit as a share of max HP and ordinary
hits to kill the player:

| depth | gear | dmg | %maxhp | hits to kill you | hits to kill it |
|---|---|---|---|---|---|
| 1 | starting kit | 2–7 | 10% | 9 | 2 |
| 5 | early shop | 3–7 | 5% | 17 | 2 |
| 15 | Jungle set | 6–9 | 4% | 23 | 2 |
| 35 | Industrial set | 17–19 | 4% | 21 | 2 |
| 60 | Ruins set | 32–39 | 5% | 17 | 3 |
| 85 | Wastes set | 47–54 | 5% | 17 | 3 |
| 99 | Abyss set | 57–65 | 5% | 16 | 3 |

Confirmed in play: the combat log now reads as an exchange
("you strike for 11" / "hits you for 3") with zero "glances off your armour"
messages, where before it was almost nothing but those.

---

## 4. Also fixed

- **Auto-explore walked through lava and miasma.** Two of its three
  pathfinders used the permissive walkability predicate. All three now prefer
  a hazard-free route and only cross damaging terrain when it's the only way
  through. (Unit-tested via the predicates; end-to-end confirmation is
  unreliable because hazard blobs are small relative to a 140×80 floor.)
- **Dead monsters were never removed** from the map array, so seven per-turn
  scans grew with every monster ever spawned, and hitting `MAX_MONSTERS`
  silently stopped respawns forever. Now compacted once per turn, at the one
  point where no combat routine is holding a `Monster*` across it.
- **A 15-gold shop weapon silently destroyed set gear.** The armory now shows
  the equipped bonus, flags downgrades inline, and confirms before replacing
  a set piece or buying a sidegrade.
- **Stale quest fields** — every per-type field is reset when a bounty is
  issued, not just the ones that type uses.

---

## 5. Where it stands

| Area | State |
|---|---|
| Core loop | Complete and stable |
| Content volume | Strong |
| Memory safety | Clean under ASAN/UBSAN |
| Combat balance | Retuned; levelling still pays, and it's tested (§11) |
| Save integrity | Hardened; corrupt files rejected, not loaded |
| Automated tests | 3 suites, `make test`, linked against real modules |
| Auto-explore | hunts, loots, heals, no step cap (§17) |
| Magic | 180 curated spells, one school per caster (§15–16) |
| Difficulty | scales with the player, not just depth (§11, §14) |
| Character creation | 7 archetypes over 100 classes (§13) |
| Party | up to 5 hired companions, autonomous (§9) |
| Death | Inn checkpoint, except Hardcore (§10) |
| Browser play | `make serve` over the LAN, via ttyd (§8) |
| Rendering | text, block and tile modes, player-selectable (§7, §18) |
| Reproducibility | `--seed N`; a seed names a dungeon (§19) |
| Build | `make` · `make notcurses` · `make serve` · `make test` · `make asan` · `make art` |

**One consequence worth knowing about:** the game is now meaningfully harder,
especially early. Floor 1 previously dealt almost no damage; a starting
character can now die there. Across five naive auto-explore trials with no
healing purchased, four survived floor 1 and one died. That's intended — the
old behaviour was the bug — but it changes the felt difficulty of the opening,
and there is still no natural HP regeneration in the game (healing comes only
from items, shrines, fountains, and level-ups). See ROADMAP §1.1.

---

## 6. Verdict

The two problems that compounded with every addition are gone, and both are
now guarded by tests that would catch a regression the day it lands. The
codebase is in good shape to build on — the remaining roadmap is content and
polish rather than structural repair.

---

## 7. Rendering backends (added after the audit)

The game no longer talks to ncurses directly. `src/tui.h` defines a small,
ncurses-shaped drawing surface, and two backends implement it:

| | `make` → `bin/aether-descent` | `make notcurses` → `bin/aether-descent-nc` |
|---|---|---|
| library | ncurses | notcurses 3.x |
| colour | 8 colours via pairs | 24-bit RGB |
| graphics | characters only | sixel / kitty / iTerm2 pixels, or Unicode subcell blocks |
| runs headless | yes | **no** |

Under the ncurses backend `tui.h` is a pass-through to the real library, so
that path is behaviourally identical to the pre-migration code. Under
notcurses every call is reimplemented in `tui.c`.

**Why both, rather than a straight replacement:** notcurses interrogates the
terminal at startup (DA queries, XTVERSION, a kitty-graphics probe, 256
palette queries) and blocks until it gets answers. It will not start under a
pipe, a detached tmux session, or with `TERM` unset — and there is no
opt-out; the only env vars it honours are `TERM`, `COLORTERM`,
`TERM_PROGRAM`, and `NOTCURSES_LOGLEVEL`. That means the automated playtest
harness (detached tmux + `send-keys` + `capture-pane`) cannot drive it, and
neither can the ASAN stress runs. Keeping ncurses as the default preserves a
path that is fully verifiable in CI-like conditions; the notcurses build is
the one to run in a real terminal.

The shim is shared, so a bug in it breaks both backends equally — which is
what makes verifying via ncurses meaningful.

### Display settings

`d` at the intro screen opens a display screen that reports what the running
build and terminal actually support — backend, `TERM`, pixel protocol, best
available graphics, truecolour, UTF-8 — lets the player choose how the world
is drawn, and **previews the choice right there**: a small sample room drawn
in the highlighted mode, including a bottom row in the out-of-sight tint.
Picking a mode you can't stand shouldn't cost you a descent to find out. The
choice persists in `~/.aether_descent_settings`, and a stored mode that isn't
available on the current run falls back to Text rather than failing — the
same home directory gets used from different terminals.

---

## 8. Playing in a browser

`make serve` streams the game to a web browser over the local network, so
another device in the house can play it. It wraps **ttyd** (`brew install
ttyd`), which hands each browser connection its own pty running the game and
streams it to xterm.js over a websocket. Every connection gets a fresh run.

    make serve      # the ncurses build
    make serve-nc   # the notcurses build -- block/tile graphics

`make serve-nc` is worth knowing about: a browser terminal is a *real* pty, so
it answers the capability queries that make notcurses refuse to start under a
pipe. The graphical render modes work there.

It binds to the LAN, not the internet: anything on the same wifi can reach it,
nothing outside can without a router port-forward, which this does not do.

**Several people can play at once**, and ttyd gives each connection its own
process — verified: two browser tabs, two independent games. But every session
runs as the same OS user, so out of the box they would all read and write the
same four `~/.aether_descent_*` files and silently overwrite each other's
suspended run.

So state is now per player. `AETHER_STATE_DIR` overrides where those files
live (defaulting to `$HOME`, so a normal single-player install is unchanged),
and `tools/play.sh` sets it from a name the player puts in the URL:

    http://host:7681?arg=kevin   -- kevin's own save, highscore and records
    http://host:7681             -- the shared "guest" pool

That name arrives from a URL and becomes a directory, so `play.sh` strips it
to an alphanumeric slug first: `?arg=../../.ssh` becomes the player `ssh`, not
a path. Verified against traversal, slashes, shell metacharacters, empty and
over-long names.

**This surfaced a real bug, not just a packaging job.** The first browser
session drew two frame corners and nothing else. ttyd starts the game before
the browser has reported its geometry, so `initscr()` saw a placeholder size
and the real size arrived afterwards as a resize — and `wait_any_key()` was
*swallowing* `KEY_RESIZE` without redrawing, so every screen that draws once
and then waits stayed frozen at the startup size forever. It now returns the
key and those screens redraw. Also added: a "window too small" notice below
80x24, since a browser window can be dragged to any size.

Neither of those is browser-specific — the same freeze would happen in any
terminal resized while the intro or the character sheet was up.

---

## 9. Hired companions

Town has a Tavern (`V`) offering **twenty heroes**, of whom you can take on
**five** — a party of six with you. Each hire costs ten times the last:
**1,000 / 10,000 / 100,000 / 1,000,000 / 10,000,000**. The fifth is less a
purchase than an ambition.

The design brief, in the player's words, was *"like if the game was
multiplayer and they were a second character doing their thing"* — and that
is the rule everything below is answerable to. They are not pets, not
summons, and not a squad you position. Nobody equips them, nobody orders
them, nobody triggers their abilities.

### They are classes, not NPC types

A hire is one of the same **hundred classes** the player picks from — a
Rivet-Witch, an Iron-Constable, a Cog-Whisperer — with that class's name,
tagline and starting weapon.

The roster is filled **role first, then class**: twenty slots dealt evenly
across the seven archetypes, then a random class from inside each. Drawing
straight from the hundred instead would let the roster inherit the class
table's own lopsidedness — nineteen Skirmisher classes against twelve
Survivor ones — and a tavern that is a third Skirmishers most nights is a
worse list to shop from.

Their stats come from the archetype tilt rather than the class's attribute
spread. `apply_class_to_player` writes thirty attributes and two dozen
derived stats onto a `Player`, and a `Companion` has none of them; wiring
that up would mean making companions into Players. So the class is **who they
are and what they carry**; the role is **what they are good at**.

Names are Victorian-by-way-of-the-engine-deck — Capt. Zephyrine Voss,
Prof. Archimedes Babbage, Lady Penelope Ashford. Honorifics are
role-appropriate (a Duchess is not a Marksman), gender-matched against the
given name, and dropped when they would push the name past the width the
list renders — a title is decoration, and a name cut off mid-word is worse
than a name without one.

### A hire matches what you are, not what level you are

Level is the wrong yardstick. A level-50 character with a set weapon, a ring
and fifteen spells is a different proposition from a level-50 character who
has been hoarding gold, and a hero who is nominally your level but carries
nothing is not a peer — they are baggage.

So the Tavern measures **standing**: melee (base + weapon + set), magic
(spells learnt, scaled by spell power), and ranged (damage, discounted for
the cooldown). Whichever channel you actually lean on is what you are; the
other two count for a quarter each, because summing all three would price a
hybrid as three characters. Buffs are excluded — hiring with a draught
running should not buy you a better hero.

Heroes land at **70–95% of that standing**, spread so two hires of the same
level still differ, with the archetype deciding the shape.

Crucially, standing is **one number, not a copy of your kit**. A fighter who
hires an Arcanist gets a caster of their own calibre — proficient in magic,
not a swordsman in a robe.

### What each role actually does

Tilts are percentages of the yardstick, not flat points, because a flat
+10 HP is a character at level 1 and a rounding error at level 50. **Reach**
is where the archetype stops being a stat block and becomes behaviour — a
Marksman who has to walk into melee is a Marksman in name only.

| Role | HP | Attack | Defence | Reach | Signature move |
|---|---|---|---|---|---|
| Vanguard | +25% | −5% | +30% | 1 | **Bulwark** — halves incoming damage for a few turns |
| Skirmisher | −10% | +15% | — | 1 | **Flurry** — strikes twice in one turn |
| Marksman | −12% | +20% | −8% | 6 | **Called Shot** — ignores armour entirely |
| Arcanist | −22% | +28% | −8% | 5 | **Detonation** — blast around the target |
| Artificer | +5% | +5% | +10% | 3 | **Charge Bomb** — scatters and stuns |
| Survivor | +35% | −8% | +10% | 1 | **Second Wind** — heals a third of its health |
| Envoy | — | +5% | +5% | 1 | **Rally** — patches the party, sharpens your swings |

Ranged attacks carry a two-turn cooldown; without it, firing from six squares
away with no risk would make the Marksman strictly better than everyone.
Nothing shoots through a wall — line of sight is required.

**Anything adjacent is a plain melee swing, no cooldown, for every role.** A
Marksman with something in its face does not stand there working the bolt,
and neither does an Arcanist or an Artificer. When the shot is reloading and
a monster is in view, they close rather than hold — melee is never on
cooldown, so closing always ends in an attack.

Abilities fire **on their own**, when the situation calls for it. That is the
design constraint, not a shortcut: an ability the player has to activate
would make these pets. Second Wind and Rally get their look in *before* the
retreat branch, or a hero hurt enough to need them is always already running.

### Temperament

Bold, Steady and Cautious set how far they range, how far out they notice a
fight, and — the part that reads as character rather than as a number — when
they decide a fight is going badly enough to leave. Cautious disengages at
40% health and stops starting fights at 55%; Steady at 20% and 30%; **Bold
does neither**, which is the whole point of Bold, and why Bold heroes are the
ones who die.

Retreating weights getting clear far above closing on the party. Weighted
evenly, a companion whose party happened to be standing behind the monster
retreated *into* it.

### They explore, and they light the floor

They walk the map the same way auto-explore does — to the edge of what the
party has revealed — over the **same shared `seen` flags**, and they carry
their own field of view. There is no fog of war around a person. Whatever
they can see, you can see, which is most of why hiring is worth something
beyond damage.

The search never looks past the fog, so they cannot shortcut anywhere, and
they are **not looking for the stairs**: descending is the player's decision,
and a companion drifting onto the stairs is the party leaving without you.
They will not step on them at all.

The leash has hysteresis — once they turn for home they keep coming until
they are well inside it. Without that, an explorer who steps out, gets pulled
back one square and steps straight out again just vibrates on the leash line
forever.

**They do not walk around town.** They meet you at the temple mouth. Letting
them loose up there parked one on a shop door, where the `@` hid the building
glyph, and gave them a town's worth of nothing to explore.

### Loot

They **go and get it**, the way the player does — nearest floor item or
unclaimed relic within 24 squares, on ground the party has revealed. They
used to collect only what they happened to tread on, which over 300 turns
with five of them came to 7 items out of 29. They were not looting, they
were tripping over things. Fetching brought the same measurement to 20 of 29.

Where it ends up is the part that is deliberately **not** symmetric, because
you are the one paying: Where it ends up is the part
that is deliberately **not** symmetric, because you are the one paying:

- **Gold, keys, quest items and relics go to you.** A hire pocketing your
  gear-set piece would read as the game taking something from you.
- **Healing draughts they keep and drink**, below 45% health. A draught in a
  dead companion's pocket helps nobody.
- **Buffs and recall charms come to you**, because they have no status model
  to spend them on and destroying them silently would be worse than either.

They also turn up **their own kit** off their own kills — three slots, better
odds deep and against elites and bosses, folded straight into their stats
(there is no equip screen to put it on). The Tavern shows what they carry;
the sidebar shows a `*` per find.

### Death

Monsters attack companions like anyone else — without that they'd be scenery
the monsters walk past, and drawing blows is half of what you're paying for.
A hero who falls goes back on the Tavern's books and can be taken on again at
**a quarter of the tier price**. Their slot frees up immediately.

### Letting one go

**`r` releases a hero**, and a different one takes that chair. It works on
your own only — someone out with you, or someone buried in your service. A
stranger cannot be dismissed, or the roster becomes a free reroll button
until it hands you five heroes you like.

No refund: the Tavern's sign says coin up front and means it. It is
permanent, so it asks first.

### The roster is a seed, not a table

Twenty candidates regenerated from `Player.tavern_seed` every visit, two
bitmasks recording who is out with you and who has been buried, and a
per-line reroll counter so a release really does produce a different person.
Forty-eight bytes instead of twenty structs, identical across visits and
saves.

### Things worth recording

- **Every trait was a bit-slice of one hash**, which correlated them. A
  roster came out eight Artificers deep with four heroes sharing a name.
  Traits now each get their own mix; names dedupe against the entries above
  them. Pinned by tests over 40 seeds.
- **They stood still, and the turn loop was why.** Each branch ended in
  `continue` on *intent* — "I have decided to close on that monster" — rather
  than on *outcome*. A Marksman with a monster inside its reach but behind a
  wall could neither shoot it nor decide to move: measured at **41
  consecutive turns** rooted to one square while the player walked off.
  Every movement helper now returns whether it actually moved, and every
  branch falls through to the next when it comes to nothing. Nothing in the
  turn may end on a no-op. Same measurement afterwards: longest stall 3–4
  turns, and instrumenting *why* they were still showed every one of them
  was mid-fight.
- **Movement was greedy**, and it showed: a companion asked to walk around a
  corner pressed into the wall until the target moved. There are now two
  breadth-first searches in `path.c` — walk-to-a-square and
  walk-to-the-frontier — shared by auto-explore and companions, replacing
  three copies in `main.c` and the greedy stepper. The greedy move survives
  only as the fallback for when no path exists at all.
- **The sidebar ran off the panel** with a full party of five. It now caps at
  the map frame and says `+N more`.
- **The whole lifecycle** — hire, fight, loot, fall, release, return to the
  roster — lives in `companions.c`. It was briefly split, with the reaping in
  the caller's turn resolver, which meant a test couldn't drive it without
  standing up a game loop.

`tests/invariants.c` carries two suites. The economy one pins prices, the
five-hire cap, the gold actually taken, a pauper hiring nobody, roster
stability and spread, class-role agreement, and the full release contract.
The behaviour one drives a real map: reach and line of sight, the shot
cooldown, Cautious disengaging and Bold not, pathing around a wall with one
gap, all seven signature moves, gold and relics reaching the player, a
draught staying with its finder and being drunk, and companions never
touching the stairs. Three of them exist purely to keep the stall from
coming back: a hero that cannot shoot must move, a reloading hero must close,
and a ranged hero with something adjacent must hit it.

### You can see them fight

Companions used to be silent, and the reason was not that their attacks
could not be drawn — it was that five heroes animating one after another,
every single turn, made the game crawl.

So they do not animate one after another. Every blow struck during a
companion turn is recorded, and the whole party's attacks play as a single
**volley** at the end of it: all the bolts travel on the same frames, all
the bursts expand on the same frames. The cost is set by the longest single
shot, not by how many heroes you hired — one is the same price as five.

Each role reads differently at a glance: a Marksman's `*`, an Arcanist's
`*` in aether-violet, an Artificer's tumbling `o`, and a melee strike drawn
on the target rather than as something in flight. The signature moves get
their own: a heavier `=` round for Called Shot, expanding rings for
Detonation and Charge Bomb, and a ring on the hero themselves for Bulwark,
Second Wind and Rally.

The bound that makes it safe is the reach table: nothing travels more than
six squares and no burst is wider than three, so a volley is at most six
frames — about a tenth of a second on the turns where a ranged hero
actually fires, and one frame when the party is only swinging. Shots whose
landing point is off-camera or in unlit ground are dropped **before**
anything is timed, so a party fighting across the floor from you costs
nothing at all.

### They carry their own kit

A hired Arcanist has a **school and real spells** out of the same pool the
guild sells from. A Marksman or Artificer carries a **real weapon** out of
`RANGED_STOCK`. Everyone else fights with their hands, and reach is derived
from what they are holding rather than from a table — a Marksman with no
weapon has no business standing off at six squares.

Spells are dealt one per tier from the top down, so a rotation has something
heavy and something cheap rather than three of the same weight, and the tier
scales with level: a hire taken on at 40 arrives with magic you would have
had to save up for. Charge is sized to open with the heaviest thing they know
and still have something left, or the spellbook is decoration.

**Companions may only learn spells that affect an enemy or themselves.** The
pool contains magic that seals tiles into walls, conjures bridges, scorches
ground into miasma, purges hazards, blinks the caster somewhere random and
opens locked doors. Every one of those is fine in the player's hands and a
menace in an autonomous ally's: a hire that walls off the corridor you were
walking down, or burns the floor you are standing on, is not a companion, it
is a hazard you paid for. That exclusion is the single most important line in
the feature and it has its own assertion.

It also left a hole worth recording. Aether-Sense is blink, reveal and
conjured bridges end to end — *every* spell in it is barred — so an
Arcanist of that school came out with an empty spellbook and a reach of one:
a robed man with a stick. Equipping now walks the schools until one can
actually arm them.

The casting itself is a compact re-implementation of the permitted effects
rather than a call into `cast_spell_slot`, which reads and writes two dozen
`Player` fields a `Companion` does not have and reports its refusals to the
message log — which an autonomous caster deciding fifty times a turn would
flood. The *numbers* come from the same `SpellTemplate` the player would be
casting, so a Conduit bolt hits for what a Conduit bolt hits for.

Shooting is rationed by the weapon's own cooldown. There is **no ammo**: the
player's ammo bar is a resource the player manages, and a companion's would
be one nobody could.

**One bug this introduced and one it exposed.** Introduced: shooters ended up
with two independent ranged cooldowns — the weapon's, and the generic
at-range swing left over from before — which between them allowed a shot
every turn, exactly what the weapon cooldown exists to prevent. Anyone
carrying a kit now reaches through it and nothing else. Exposed: `-1`
sentinels for "no weapon" are wrong by default, because a `memset` slot then
claims to be holding `RANGED_STOCK[0]`. The weapon index is stored **plus
one**, so zero means empty by construction rather than by remembering.

### The class is worth something now

Two Vanguard classes used to be the same hero with different names, their
stats coming from the archetype tilt alone. `apply_class_to_player` is not
reusable — it writes thirty attributes and two dozen derived stats onto a
`Player` — but the class's own attribute *overrides* are data, and the ones
that map onto what a companion has are worth reading. Brawn, Fortitude and
Stamina move health; Might, Precision and Conduit move attack; Grit, Balance
and Warding move defence, each as a deviation from the baseline of 3.

Clamped at ±15%, so a wildly specialised class cannot turn its role inside
out: a Vanguard with a high Might is a hard-hitting Vanguard, not a
Skirmisher. Asserted across every same-role class pair in the table.

---

## 10. The Inn

Town has an Inn (`I`, bottom-right, door facing north). **Walking in is the
whole interaction** — there is no menu and no rest option to pick. It:

- restores you to full HP,
- takes **a fifth of your gold**, every time, rounded down (so a broke
  character can still sleep, and a rich one pays more),
- banks the character exactly as they stand — level, attributes, gold,
  spells, gear, pack, records.

**Die, and you wake up there as you were banked.** The snapshot is not
consumed: it stands until the next rest, so the Inn is a checkpoint rather
than a one-shot revive. This replaces permadeath everywhere except Hardcore.

**Hardcore keeps permadeath.** The bed is real — it heals you and takes the
fee — but nothing is banked and nothing comes back. That is the mode's whole
stated promise, and an Inn that undid it would make Hardcore a lie.

Two things this design gets wrong if you are not careful, both handled:

- **A new character must not be able to die into the previous one.** The
  snapshot is per-character, so creating a character retires whatever was
  banked before it.
- **A corrupt snapshot must not half-load.** Same guards as the suspended
  run: magic, version, the running build's `sizeof(Player)`, exact file
  length, and a sanity gate on the fields that drive array indexing. Anything
  that fails is deleted rather than restored — coming back as a corrupted
  character is worse than not coming back.

It is a separate file from the suspended run (`.aether_descent_inn` vs
`.aether_descent_save`). One means "I closed the game mid-descent", the other
means "I paid for a bed"; they have different lifetimes and must not
overwrite each other. Both respect `AETHER_STATE_DIR`, so served players get
their own (§8).

`tests/invariants.c` round-trips a fully-populated character through the
snapshot and asserts it comes back **byte-identical**, that restoring does not
consume the bank, that clearing it works, and that garbage is refused.

---

## 11. Keeping runs from going easy

Reported from play: on Normal, buy one attack spell, hold `x`, and the run
becomes unstoppable. Three changes, all measured before shipping.

**Monsters scale with the player, not just the floor.** Escalation is
`level x 1% + floors entered x 0.5%`, capped at +100% attack, applied to
every monster made from the moment it's set. Depth-based scaling always loses
this race — the player's power grows faster than any floor curve — so the
input is now the thing that actually caused the problem. Floor entries are a
second, smaller term so a run that wanders back and forth also tightens, and
walking back up to a cleared floor no longer hands you an easy one.

It lands on **attack only**. HP or defence would make fights longer rather
than riskier, which is the tedious kind of hard; rewards are untouched, or a
long run would pay for its own difficulty.

**The trap this mechanic usually falls into**, and the test that guards it:
if monsters scale as fast as the player, every level-up is worthless and the
player runs to stand still. `tests/balance.c` now checks that at a fixed
depth, survivability still *rises* with level — floor 60 goes 2, 3, 4, 6, 8,
10, 12 ordinary hits survived from level 20 to 140. A future tweak that
flattens that fails the build.

For the same reason the `floor²/140` attack term added earlier was removed:
escalation does that job now, and the two together took a floor-99 fight down
to a single hit.

**Standing still costs something.** Every 1000 turns spent on one floor,
whatever is alive on it doubles — measured on an ordinary floor: 58 alive,
then 274, 706, 1570, stopping at the 2000-monster array cap. (The doubling
itself doubles; the pre-existing 25-turn trickle adds on top, which is why
the first step is 58 → 274 rather than 116.) A floor you are picking over
slowly becomes a floor you have to leave, and no two visits to the same depth
play the same.

A cleared floor still repopulates — doubling nothing would have made
"clear it and camp" the safest play in the game, which is backwards — so a
doubling never delivers fewer than eight. `tests/invariants.c` pins all
three: it doubles, a cleared floor refills, and 20 000 turns of camping never
walks off the end of the monster array.

**One Hard floor in eight is overrun.** It generates at Swarm density instead
of Hard's usual 2-3x — measured at 11.2% of floors (boss floors are exempt),
172 monsters becoming 1074 — keeps respawning at three times the usual rate
in waves of five, and is flagged in the frame the whole time you stand in it,
not just in a log line that scrolls away. Its XP is divided a further 4x on
top of Swarm's usual 5x, or an overrun floor would hand over 2-4x a normal
floor's experience and make Hard *easier*.

Where a run now sits (ordinary hits survived, mid-play character):

| | floor 15 | floor 35 | floor 60 | floor 99 |
|---|---|---|---|---|
| before | 20 | 15 | 13 | 11 |
| after | 16 | 8 | 5 | 4 |

**No level cap** — there never was one, and that is part of why this was
needed: `grant_xp` has no ceiling, so a long enough run outgrows anything
keyed to depth alone.

---

## 12. The ammo gauge was decorative

Reported from play as "AM never went down, I thought it was a bug". It was
two bugs.

**The bar showed with no weapon.** Every character gets an ammo pool at
creation (`6 + Precision x 2`) whether or not they own something to fire, and
the sidebar drew the bar unconditionally — so a player who never visited the
Armory saw a permanently-full gauge for a resource they did not have. It is
now hidden unless a ranged weapon is equipped, matching the `Ranged:` line
right below it.

**And it could never fall anyway.** Regen was +1 per turn. Every weapon in
the shop costs 1-3 ammo with a 1-5 turn cooldown, so the pool refilled at
least as fast as the cooldown allowed it to be spent:

| weapon | cost | cooldown | regained while waiting | net |
|---|---|---|---|---|
| Recurve Longbow | 1 | 2 | 2 | **+1** |
| Brass Revolver | 1 | 3 | 3 | **+2** |
| Tin Grenade | 3 | 5 | 5 | **+2** |

Not one of the eighteen could be drained. A resource that cannot be depleted
is not a resource, and it silently made the Precision-scaled pool size
pointless. Regen is now one point per four turns, which makes sustained fire
net-negative for every weapon sold while a walk between fights still refills
it — measured live: 3/12 climbed to full over ~50 turns of walking, then fell
as the hunter opened fire.

`tests/invariants.c` pins the rule rather than the number: for every weapon
in `RANGED_STOCK`, firing as fast as its cooldown permits must lose ammo. A
new weapon with a generous cooldown now fails the build instead of quietly
becoming infinite.

---

## 13. Character creation

Classes are picked in two stages: **role first, then a class inside it.**
Seven archetypes — Vanguard, Skirmisher, Marksman, Arcanist, Artificer,
Survivor, Envoy — split the 100 classes into groups of 12–19. Esc on the
class list goes back to the roles, so changing your mind costs nothing.

A hundred names in one flat list is a scroll, not a choice: nothing on it
told you what a Rivet-Witch actually plays like without reading all hundred
taglines.

**Each archetype starts equipped for its role.** A class's numbers already
come from its attributes, but attributes say nothing about *role* — a
Marksman with no bow is just a worse Skirmisher, and an Arcanist who cannot
cast until they can afford the guild is not an Arcanist yet. So on top of the
attribute-derived kit:

| archetype | +weapon | +armour | and |
|---|---|---|---|
| Vanguard | +3 | +4 | — |
| Skirmisher | +3 | +2 | — |
| Marksman | +1 | +1 | the cheapest bow in the Armory |
| Arcanist | +0 | +1 | a level-1 spell of their own school |
| Artificer | +1 | +2 | a healing draught and a guard tonic |
| Survivor | +1 | +3 | two preserved rations |
| Envoy | +1 | +2 | 60 extra gold |

The fighting archetypes are better armed because everyone else is paying for
something else — measured across all 100 classes, Vanguard and Skirmisher
average 8 points of starting gear against the Arcanist's 3.

The bow and the spell are both *looked up* rather than hard-coded: the bow
from `RANGED_STOCK`, the spell from the caster's own school (the only school
the guild will ever teach them, §15 — anything else would be a spell they can
never build on). `tests/invariants.c` checks the promises the archetype
screen makes: every Marksman class starts with something to shoot, every
Arcanist class starts knowing a level-1 spell of their own school, nobody
starts unarmed, and fighters out-equip casters.

This also caught a live instance of the drift §11 is about: the starting
healing draught was a locally hand-built template still handing out a flat
15 HP, months after shop draughts moved to percentages. Starting items now
come from the real tables via `consumable_by_name`.

**The archetype is a hand-assigned field on `ClassSeed`, not derived.**
Deriving it from lead attributes was tried and got things backwards often
enough to not be worth it — a precision-led net-caster is a Marksman, a
precision-led surgeon is an Artificer, and only the name distinguishes them.
The assignment used the lead attributes with the name breaking ties.

Nothing downstream changed: `screen_class_select` still returns a
`CLASS_TABLE` index, and the archetype is a *view* over the table, the same
shape as the per-school spell list. `tests/invariants.c` asserts the seven
groups partition all 100 classes exactly once — a class in no group is
unpickable and one in two groups is offered twice, and neither is visible
when reviewing a one-field change to a 100-row data table.

---

## 14. Difficulty, and what was changed about it

The game used to be **front-loaded**: a near-even fight on floor 1 and then
nothing dangerous for ninety-eight floors. Measured across three play styles
(hits to kill you : hits to kill it), before:

| | floor 1 | floor 35 | floor 99 |
|---|---|---|---|
| clears every floor | 6 : 5 | 23 : 2 | 17 : 3 |
| fights what it meets | 6 : 5 | 15 : 2 | 11 : 3 |
| runs for the stairs | 6 : 5 | 10 : 3 | 7 : 4 |

Nothing at depth could kill you in a fight. What ended runs was attrition
against a healing supply that stopped scaling: the best draught in the game
restored a flat 70 HP, which is 175% of a fresh character's bar and 6% of a
floor-99 one. Three changes, all measured with `tests/balance.c` and the same
four-seed playtest before and after:

**Healing scales.** `ConsumableTemplate` gained `heal_pct`; drinks now
restore a flat amount *plus* a share of max HP. The best draught went from
6% of the bar at floor 99 to 33%, and never drops below that. The flat part
is what keeps early potions feeling generous.

**Floor 1 got easier.** Starting gold 25 → 60, plus two rations in the pack.
That's option (c) from the old roadmap note — no mechanic touched, smallest
blast radius. 25 gold bought one draught; 60 buys a weapon and some heals.

**Depth got dangerous.** Monster attack was linear in depth and lost the race
against player defence. It now carries an accelerating term
(`floor + floor²/140`) worth nothing at floor 1 and roughly doubling incoming
damage at the bottom. After: an ordinary hit costs 10% of your bar at floor 99
instead of 5%, and a player who skipped the fights on the way down is in
genuine trouble — which is the intended pressure, since kills are what buy
level-ups and level-ups are the heal that scales.

**And there is now a way out of the attrition problem, for a price.** The
**Ouroboros Coil** (armoury, 5000g, `ACC_REGEN`) regenerates 6 HP a turn. It
is deliberately the most expensive item in the game — above the 2600g level-5
spells and five times the best weapon — because it removes the constraint the
whole late game runs on. It does nothing while poisoned, so a trickle of HP
can't silently cancel a damage-over-time effect, and there is no innate
regeneration without it.

**Empirically**, four seeds of naive auto-explore (no shopping, no recall)
went from 0 deaths before to 1 death on floor 4 after. That probe never
enters a shop, so it feels the harder monsters without any of the
compensations — it measures the floor, not the median.

**A drift bug this surfaced:** consumables are declared in five tables and the
same drink appears in several. Adding `heal_pct`, they immediately disagreed —
a looted Superior Draught healed twice what a bought one did. `items.c` now
exposes every row so `tests/invariants.c` can insist one name means one item.

---

## 15. The spell pool

**Every spell is hand-written.** 6 schools x 5 levels x 6 spells = 180, each
with its own name, effect and stat profile, in `src/spells.c`.

It used to be 5 curated per school plus 205 generated by crossing a bank of
adjectives with a bank of nouns. That produced the right *number* of spells
and none of the variety: six entries running would share a noun ("Aether
Bolt, Volt Bolt, Arc Bolt"), and because effect and archetype were picked by
counter rather than by intent, two differently-named spells routinely did the
identical thing. The table costs 180 lines and buys a pool where every entry
was chosen.

Stats still come from the per-level tables multiplied by an archetype, so the
balance curve lives in one place rather than in 180 — but each spell now
*names* its archetype, which is what lets two same-level spells with the same
effect feel different: `Coil Discharge` is heavy and slow to recharge,
`Live Wire` is cheap and quick.

**Prices were too flat.** A spell is a permanent, reusable ability, and
levels 3–5 used to undercut the 1000g top weapon in the armoury. The curve is
now roughly ×2.5 a level — 60 / 180 / 450 / 1100 / 2600 before archetype —
so a level-5 spell is a serious investment rather than an afterthought.

**This renumbered the pool, so `SAVE_VERSION` went to 3.** `known_spells`
stores pool indices and the struct sizes didn't change, so a version-2 save
would have loaded and quietly handed the character a different spellbook.
Rejecting it costs a suspended run; accepting it corrupts one silently.

`tests/invariants.c` holds the table honest: no duplicate name anywhere in
the game, every school the same shape, names that fit the guild's 28-column
field, spells already in level order (the guild browses in table order, so
table order *is* display order), and a price curve where every spell of a
level outprices every spell of the level below.

---

## 16. One caster, one school

A character now only ever sees their own school of magic. The Arcanist's
Guild browses `spell_school_count`/`spell_school_index` instead of the whole
pool, so an off-school spell is never listed, and `player_can_learn` is the
matching rule: learnable means, and only means, "belongs to your school".

Before, levels 1–4 were open to everybody and only level 5 was locked. In
practice that meant every character browsed the same ~200 spells and the
school a class was built around barely mattered — the decision this replaces
came out of play, not review.

Two deliberate choices:

- **Already-known spells are not re-checked anywhere.** A character who
  learned outside their school under the old rule keeps what they paid for,
  and casting never consults the school. Retroactively confiscating spells
  from a live save would be a worse bug than the one being fixed.
- **Nothing downstream changed.** `known_spells` still stores pool indices;
  the school list is a view over the pool, not a second numbering. That's
  what keeps saves, the cast menu and the auto-explore spell picker working
  untouched.

`tests/invariants.c` pins the part that could silently rot: the per-school
lists must partition the pool exactly once, every school must cover all five
levels (or a class hits a ceiling it was built to pass), and the browsable
list must equal the learnable set in both directions — a screen offering
something the buy path refuses is exactly the failure this shape invites.

---

## 17. Auto-explore

`x` is no longer a walking macro. Each step it resolves, in this order:

1. **Heal** if below half HP and carrying something that heals; stop below a
   quarter if not.
2. **Fight** anything living within 10 tiles and in line of sight — melee if
   adjacent, otherwise the strongest offensive spell whose reach covers the
   distance, otherwise the ranged weapon if it's loaded and off cooldown,
   otherwise close in.
3. **Loot** the nearest seen floor item.
4. **Explore** toward the stairs, or the fog if the stairs aren't found yet.

Every leg prefers a hazard-free path and only crosses lava or miasma when
there is no other way — a hunt is not a reason to walk into lava.

**There is no step limit.** It runs until the floor is done, you get hurt, or
you press a key. The old 600-step cap stopped it mid-room for no reason the
player could see, which on a 140x80 floor was most of the time.

A loop with no cap needs some other guarantee it can't spin, so it watches
for *progress* rather than counting steps: position, HP, kills, the tiles
you've revealed, and the health of whatever you're currently hitting are
folded into one number each step. Sixty consecutive steps with that number
unchanged means stuck, not busy, and it says so. Monster health is in there
specifically so a long fight reads as progress while the player stands still;
revealed tiles are in there so walking into fog counts before it finds
anything. At the per-step pause, sixty steps is a few seconds of literally
nothing happening.

Two details worth knowing:

- **The hunt radius is bounded on purpose.** "Hunt what's near you" and
  "clear the floor" are different requests; an unbounded hunter would spend
  its whole step budget crossing a 140x80 map toward something it glimpsed.
  Targets must also be in line of sight, so it never chases a monster it
  only remembers.
- **The attack choice is made without calling the attack functions.**
  `cast_spell_slot` and `fire_ranged` report their refusals to the message
  log, so a hunter that simply tried them would fill the log with "not enough
  aether" every turn. `spell_pick_attack_slot` and `ranged_ready`/
  `ranged_reach` answer "could I?" as pure predicates instead, and are unit
  tested in `tests/invariants.c`.

A monster that is visible but unreachable (across water, say) falls through
to ordinary exploring rather than stalling the run.

---

## 18. The three render modes

Only the map area changes between modes. Menus, shops and the message log are
text in all three, because that is what they are good at.

| | Text | Block graphics | Tileset |
|---|---|---|---|
| needs | nothing | UTF-8 + block glyphs | a pixel-graphics protocol |
| available on | both backends | notcurses | notcurses |
| a cell is | one ASCII character | one Unicode block, true-colour fg **and bg** | an 8x16 sprite scaled to the cell |
| cost per frame | trivial | trivial | ~7 ms to compose, plus shipping the image |

**Where the art lives.** `src/tileart.c` holds one table keyed by `ArtId`,
and each render mode asks it a different question about the same key: block
mode wants the glyph and the two colours, tile mode wants the sprite. That
shared key is what stops the modes drifting apart — adding a tile type is one
enum entry and one table row, and a missing row fails `make test` rather than
appearing as a magenta `?` on floor 60. Sprites are written as literal 8x16
pictures in the source, using a palette of roles (`#` base, `+` highlight,
`-` shade, `*` accent, ` ` transparent) rather than colours, so re-tinting a
tile is a table edit and "does this look like a fountain?" is answerable by
reading the file.

**Block mode** draws terrain as solid coloured blocks and keeps actors as
their own letters over a background tinted by whatever they are standing on —
so a rat in a lava field reads as a rat in a lava field. Out-of-sight cells
darken the *background* rather than the glyph, which is what makes remembered
terrain recede instead of just going grey.

**Tile mode** composes the whole viewport into one RGBA image and hands it to
the terminal in a single blit. Its entire cost is that image, and everything
about how it behaves follows from trying to send it less often and smaller:

- **The plane lives across frames.** notcurses elides an unchanged sprixel,
  but only if the plane survives — so the image is kept, compared against the
  last one, and re-sent only when the picture actually changed. The first
  version destroyed and rebuilt the plane every frame, which forced a
  delete-and-retransmit on every keypress and made the mode unusable.
- **The tiled area is capped by a pixel budget**, adjustable with `-`/`+` on
  the display screen and shown there in tiles and pixels. Tile mode therefore
  draws a *smaller* map than text mode on the same window. That is the
  trade-off the mode is: fewer tiles, but they arrive when you press the key.
  The floor is the 58x18 the rest of the game needs; at very large cell sizes
  (a big font on a retina display reports physical pixels) even that floor
  can exceed the budget, and correctness wins.
- **Oversized images are refused, not attempted.** Terminals state a maximum
  bitmap size; exceeding it is undefined and in practice paints garbage over
  the screen. `tui_blit_rgba` checks and declines.
- **A resize re-lays-out before the next frame.** The viewport used to be
  fixed at startup, so shrinking the window left an image that no longer fit.
- **Repeated blit failures drop the mode**, with a line in the message log,
  rather than leaving a half-transferred picture on screen while the settings
  claim tiles are on.
- **Combat animations draw to a transparent plane above the graphics** rather
  than into the image, so a spell bolt costs one glyph per step instead of
  re-blitting the viewport twenty times. That plane has to be made
  transparent *explicitly*: notcurses renders a plane's base cell wherever a
  cell is empty, a fresh plane's base cell is all zeroes, and `NCALPHA_OPAQUE`
  is zero -- so an untouched overlay paints an opaque block over everything
  beneath it. Getting this wrong is what turned the map into a flat grey
  rectangle the instant a blow landed, taking the tiles with it. The overlay
  is also sized to the map area rather than the screen, so the blast radius of
  any future mistake here is bounded.

**How this was verified.** notcurses will not start under a pipe or a
detached tmux session (see above), so the automated harness cannot screenshot
either graphical mode — and shipping art nobody has looked at is not
verification. Instead:

- `make art` renders the tileset through the *same* rasteriser the game uses
  and writes `build/art-atlas.png` (every sprite, lit and dimmed) and
  `build/art-scene.png` (a laid-out room, so tiles can be judged against each
  other). Both were reviewed and iterated on — the floor tile was rebuilt
  twice, first because rooms read as voids and then because the fix made
  floor and wall too similar.
- `tests/tileart.c` guards the table, the rasteriser, and the viewport
  budget (§2) — the last of these runs under the ncurses build, which has no
  pixel graphics at all, because a regression there is a game that *feels*
  broken rather than one that looks wrong.
- The ncurses build, which shares every line of the resolution and painting
  path, was played through town, a descent, combat with animations, the
  minimap, the character sheet and the display screen, under ASAN/UBSAN,
  clean.

What remains unverified by anything but a human at a real terminal is the
final hand-off: the notcurses `putwc`/`ncvisual_blit` calls themselves. If a
mode looks wrong, the display screen's preview is the fastest place to see
it.

---

## 19. Seeded runs

Every character now carries a **run seed**, and a run is reproducible from
it. `--seed N` starts a new character on a chosen one; without it, one is
drawn and recorded, so a run you stumble into can still be handed over
afterwards. The seed is on the character sheet and on both end screens —
the places where someone is actually about to describe what happened.

The part that makes it worth having is *where* the seed is applied. Seeding
the global stream once at startup is the obvious implementation and it is
close to useless: the floor you arrive at then depends on how many attacks
you rolled on the way down, so "floor 12 of seed 44815" names nothing. Each
floor instead derives its own stream from `(run_seed, floor_num)`, so a given
floor of a given seed is always the same floor however you played to get
there. Combat, drops and respawns carry on from that point — still
deterministic for identical play, and no longer able to change the map.

The Tavern roster derives from the run seed too, so the same seed offers the
same twenty heroes.

Reproducibility is asserted the way it should be: a floor is generated twice
from the same seed — once from a fresh RNG, once after five thousand
throwaway `rand()` calls standing in for a long messy run — and the two are
fingerprinted and compared. Then the negatives, without which the positive is
vacuous: different seeds differ, different depths of one run differ, and the
derivation never returns zero.

**In the browser**, a seed can ride the URL alongside the player name:

    http://host:7681?arg=kevin&arg=44815

Same sanitising as the name — it is digits or it is dropped, never passed
through to a command line as written.

**What a seed does not capture:** your keystrokes. Two people on seed 44815
get the same dungeon, the same rooms and the same roster, and then play it
differently. That is the useful guarantee, and it is the one being claimed.

---

## 20. Three things a player actually sees

### The keys, on `?`

Controls appeared once at the intro and never again. The footer only ever had
room for the common ones, and a player arriving through a browser link may
never have seen the intro at all — which, given that this is now played in a
browser by someone who did not build it, is the whole problem.

`?` opens a key reference, in town and in the temple, and the two lists are
generated from one table with `town`/`deep` flags per row, so they cannot
drift apart. The trimming is the point: no `m`/`s`/`f`/`a` in town, no
"walk into a door" in the dungeon. The footer lost `r recall` and `s recast`
to make room for `? keys`, which is the trade that matters — the footer
cannot hold everything, so its job is to point at the thing that can.

### Hazards on the minimap

`minimap_block_glyph` had no case for lava or miasma, so both drew as plain
floor: the one part of the map that costs health to cross read as somewhere
safe to walk. They now draw as `~` and `%` in their own colours, ranked
below landmarks (a staircase sharing a block still wins) and above plain
floor. Water gets the same treatment in its own colour, since it is the same
glyph and would otherwise be indistinguishable from lava.

The single-line legend wrapped at 80 columns and ate the row below it. It is
two short rows now.

### Per-biome palettes

Every floor was cut from the same grey stone. A hundred floors that differ in
monster roster and hazard mix and in nothing you can see at a glance.

The art table stays **one** set of sprites — authoring five of everything
would be five times the table and five times the drift — and the biome
instead shifts terrain colour through a five-row multiplier table: wet green
for the Jungle, brass and soot for the Industrial works, cold blue-grey stone
for the Ruins, bleached salt for the Wastes, violet dark for the Abyss.

**Terrain only.** Actors, items and features keep their own colours, because
they are the things you have to pick out of the background, and a monster
that goes green in the jungle is a monster you walk into. That rule is the
one most worth testing, and it is.

The biome is set from the map every frame rather than once on arrival. It is
one assignment, and it means no code path — resume, recall, the display
screen's live preview — can leave the table tinted for a floor the player is
no longer standing on.

`make art` now renders one scene per biome (`build/art-scene-<biome>.png`)
through the same rasteriser the game uses. A tint is the one part of the art
nobody can judge from a table of RGB triples, and the alternative to a PNG
was descending to floor 80 to look at it.

`tests/tileart.c` asserts that every biome moves the ground off the default,
that no two biomes land on the same colour, that actors are untouched in all
five, that an out-of-range biome means the surface rather than a stale tint,
and that dimming still darkens remembered ground in every one — without which
the fog would stop reading as fog somewhere dark.

---

## 21. Screens that fit the screen

Three reports from a real session, all the same underlying fault: **screens
drew at absolute rows and columns without ever asking how big the window
was.** At the size the game claims to support — 80x24 — that produced three
different symptoms, and only one of them looked like a bug.

### Gold was invisible in the one shop that needed it

`draw_shop_header` puts `Gold:` and the HP bar on row 2. The Arcanist's Guild
then printed its own aether line at row 2, straight over the top. So the one
shop whose prices run to four figures was the only shop that would not tell
you what you had to spend. Row 3 was empty the whole time.

### Long screens ran off the bottom

The character sheet is about 30 lines. On a 24-row terminal it lost the last
eight — including the run seed and the "press any key" prompt — and nothing
on screen said so. The intro lost its tail the same way: the Inn, Hardcore,
and the display-settings key.

They now scroll. A `Page` counts rows in *document* space and maps them onto
the window, returning -1 for anything out of view; content that does not fit
is reachable rather than lost, and the footer says which way to go. Rows are
claimed whether or not they are visible, so scrolling *moves* the layout
instead of reflowing it.

### Over-long lines corrupted everything below them

The worst of the three, and the one that looked like corruption rather than
truncation. A line one character too wide wraps onto the next row, which
shifts every row below it and writes over the frame. At 80 columns the HUD
sidebar did exactly that — `Recall charms: 0   Keys: 0` is wider than the
twenty columns left beside the map — so the *map itself* came out mangled:

    |#.....................,..............,...................#| Recall charms: 0
    Keys: 0#P##........................##V###....##I###.......#|

Printing is now clipped to the window at the point of drawing: `page_print`
for paged screens, `mvprintw_clip` for the sidebar. Cutting a line is a
visible, local failure; wrapping it is an invisible one that ruins the rest
of the screen. The intro's prose and the key list were also rewrapped to fit
74 columns rather than relying on the clip.

`draw_bar` got the same treatment. It took a fixed width, so the sidebar's
HP/XP/AE bars ran past the right edge and lost their closing bracket and
their `34/56` — the only part of a bar anyone actually reads. It now shrinks
the bar to the room available and keeps the numbers.

### Some screens used a fraction of a big window

The other end of the same fault. The spell lists paged at a hard-coded 18
entries: on a 24-row terminal that overflowed, and on a 44-row one it showed
18 of 30 spells with a third of the screen empty. Page height now comes from
the window, and the detail pane follows the last entry actually drawn rather
than the page height, so a short list does not leave a gap.

Not every screen should fill its window — the general store has six items and
is not hiding anything. The ones worth fixing were the lists that had more to
show than they were showing.

---

## 22. Are the four modes winnable?

**No. None of them.** Not "hard" — unreachable. Across roughly 370 simulated
runs, in every configuration tried including ones the game cannot actually
grant, no run reached floor 100.

### The instrument

`tools/simulate.c` (`make sim`) plays the game headlessly against the **real**
systems: mapgen makes the floors, monsters.c makes and escalates them,
combat.c resolves every blow, items.c stocks the shops, spells.c supplies the
magic. Nothing in it re-implements a formula — a model that re-derived the
damage curve could only confirm its own arithmetic.

The simulated player fights what is next to it, shoots what is in reach,
drinks below 45%, cracks a recall charm when it is out of draughts, visits
town at every waygate to heal at the Inn and buy the best gear it can afford,
and heads for the stairs rather than clearing floors. It does not kite, lure,
skip floors, or reroll a bad start. It is deliberately unspectacular.

### What it found

| Mode | Fair start | 2M gold | 20M gold + party of five |
|---|---|---|---|
| Normal | median 4, best 38 | median 30, best 37 | median 37, **best 42** |
| Hard | median 1, best 22 | median 7, best 25 | median 18, best 24 |
| Swarm | median 1, best 1 | median 2, best 9 | median 12, best 29 |
| Hardcore | median 1, best 1 | median 3, best 10 | median 12, best 19 |

The third column is the important one. It is not a fair run: twenty million
gold, the best weapon and armour in the shops, every spell in the school, the
Ouroboros Coil, a bag of draughts, and five hired heroes. **The best result
in the game's easiest mode was floor 42.**

Money and a party move the *early* game enormously — Swarm's median goes from
floor 1 to floor 12 — and barely move the ceiling. Normal's best went from 38
to 42. That separation is the finding: the early game is a **resource**
problem, and the deep game is a **scaling** problem that resources do not
touch.

### Why the wall is where it is

Monster attack outgrows everything the player can do about it:

| Floor | Monster hp/atk | Player atk/def/hp | Hits to kill it | Hits to die |
|---|---|---|---|---|
| 1 | 15 / 8 | 46 / 42 / 72 | 1 | 72 |
| 20 | 88 / 46 | 84 / 61 / 376 | 2 | 20 |
| 40 | 172 / 133 | 124 / 81 / 696 | 2 | **9** |
| 60 | 235 / 203 | 164 / 101 / 1016 | 2 | 8 |
| 100 | 435 / 410 | 244 / 141 / 1656 | 3 | **6** |

Offence keeps pace — two hits to kill an ordinary monster, all the way down.
Defence does not. Monster attack grows about **50x** from floor 1 to 100;
the player's effective health pool grows about 23x and defence about 3.4x.
Under `dmg = atk^2/(atk+def)`, defence has to scale *with* monster attack to
hold a ratio, and it cannot: the shops cap at +27, and levels give flat
increments against a curve.

Six hits to die is not survivable when four monsters are adjacent, which on
floor 100 they are.

### Two other things worth knowing before touching any numbers

**The floor-doubling mechanic can run away.** Monsters double every 1,000
turns on a floor. In Swarm the density starts high enough that crossing a
floor can take longer than that, so the doubling fires while you are still
fighting the previous wave. Runs were observed hitting the `MAX_MONSTERS`
ceiling of 2,000 on **floor 1**, at which point the floor is not difficult,
it is impassable — the simulation records these as "stuck" rather than
deaths, and Swarm/Hardcore produce them regularly.

**The early game is a separate problem from the wall.** With a fair start,
23 of 40 Hard runs and 40 of 40 Swarm runs die on floor 1. That is not the
scaling curve, that is a level-1 character meeting Swarm density with 56 HP
and three rations.

### What this means for the balance work

The question "are they winnable" has to be answered before difficulty tuning
or DDA, and the answer is that **there is nothing to tune toward yet**. DDA
in particular would be actively harmful here: it adapts difficulty around a
target success rate, and the current success rate is zero at every depth past
40 regardless of what the player does. It would spend its whole time trying
to compensate for a curve problem it cannot reach.

The order that follows from the data:

1. **Fix the defence/attack scaling past floor 30.** Everything else is
   downstream of it. Nothing else moves the ceiling.
2. **Cap or rethink floor-doubling in the high-density modes**, or Swarm and
   Hardcore stay unplayable for reasons unrelated to (1).
3. **Then** the early-game resource gate, which is a different lever
   (starting kit, floor-1 density, shop prices).
4. **Only then** DDA, if it is still wanted.

Re-run `make sim` after each change. It takes about a minute and it is the
only thing in the project that can answer this question.

---

## 23. Gear upgrades, and what they moved

The wall in §22 is a defence wall: the shops top out at +27 while monster
attack keeps climbing to floor 100. The smith is the answer — an **uncapped
track applied to whatever you already carry**, sold in the Armory alongside
the gear.

Each step is +5 to the piece. Prices climb quadratically (`20n² + 100n + 20`):
140g for the first, 3,020g for the tenth, 10,940g for the twenty-first. The
first few are an obvious buy by floor 5; a deep run is still spending
everything it finds.

Two rules give it teeth:

- **Upgrades stay with the piece.** Buying a new weapon starts its count over.
  A +8 Riveted Cleaver against a fresh Star-iron Edge is a real decision.
- **Hired heroes inherit them.** You cannot equip a companion, so this is the
  only way your gear reaches them: your +4 is their +4, on both attack and
  defence. It is also what keeps a party worth paying for at depth — their
  own stats are frozen at the level you hired them, so without this a hero
  bought on floor 5 is a floor-5 hero for the rest of the run.

### What it did to the wall

Best case (20M gold, party of five), before and after:

| Mode | Before upgrades | With upgrades | With upgrades + ranged + sets kept |
|---|---|---|---|
| Normal | best 42 | best 58 | **best 54, median 39** |
| Hard | best 24 | best 39 | best 35, median 20 |
| Swarm | best 29 | best 37 | best 49, median 13 |
| Hardcore | best 19 | best 38 | best 36, median 5 |

The wall moved from about floor 40 to about floor 55. **It is still a wall,
and floor 100 is still unreached in every mode.** The mechanic is the right
shape and it is not yet strong enough — which is a much better problem than
the one before it, because it is now a tuning problem (step size, price
curve) rather than a missing system.

### Two corrections to the simulation, both worth recording

The owner asked what the model was doing about relics, sets and ranged
weapons. Two answers were wrong:

- **The agent was destroying its own gear sets.** It bought the best
  affordable shop weapon whenever that beat its current bonus — including
  over a dungeon relic carrying a set bonus. The game asks the player before
  it lets them make that trade; the agent took it silently. It now refuses,
  like the warning tells you to.
- **The agent never bought a ranged weapon at all.** It knew how to fire one
  and had nothing to fire, so only the classes that start with one ever used
  the ranged branch. Most runs were fighting the entire dungeon in melee.

Fixing both moved the fair-start Normal median from 4 to 12, and is a
reminder of the standing caveat: **a simulation understates the game by
exactly as much as its agent is worse than a person.** Every number here is
a floor, not a forecast.

---

## 24. Can money buy floor 100? No, and the reason matters

The obvious next question after §23: the smith's track is uncapped, so does a
big enough purse simply finish the game? Measured, with a full party and
every shop bought out:

| Starting purse | Deepest floor (Normal) |
|---|---|
| 20,000,000 | 54 |
| 200,000,000 | 80 |
| 500,000,000 | 80 |
| 1,000,000,000 | 80 |
| 2,000,000,000 | **81** |

**A hundred-fold increase in money buys twenty-seven floors; the last
ten-fold buys one.** At two billion gold — with a party of five, every spell,
the best of everything — the four modes reach 81 / 70 / 44 / 63 and none of
them finishes.

### Why it saturates

The damage model is `dmg = atk² / (atk + def)`. Attack is squared in the
numerator; defence only appears added in the denominator. So to hold
hits-to-die constant while monster attack grows by a factor of *k*, defence
has to grow by *k²*.

Monster attack grows about 50x from floor 1 to 100, and escalation nearly
doubles it again. Defence would have to grow four figures' worth. Priced
through the smith's own curve, this is what surviving ten hits actually costs:

| Floor | Monster attack | Defence needed | Upgrades | Gold |
|---|---|---|---|---|
| 20 | 46 | 10 | 2 | 440 |
| 40 | 108 | 202 | 40 | 525,600 |
| 60 | 172 | 614 | 122 | 13,000,000 |
| 80 | 267 | 1,628 | 325 | 235,000,000 |
| 100 | 389 | 3,635 | 727 | **2,590,000,000** |

Armour alone. The weapon side wants the same again. And the cost grows
*cubically*: each upgrade is dearer than the last, and the number of them
needed goes as attack squared.

That is the finding. **This was never an economy problem, and no gold change
fixes it.** The wall at floor 80 is where the curve says it is, and the
economy is only the thing that walks you to it.

### What would actually work

Three levers, in the order they are worth trying:

1. **Slow the monster attack curve.** It is the numerator, and it is squared.
   Roughly halving its growth past floor 40 would bring the floor-100
   defence requirement down from ~3,600 to ~900 — within reach of the smith
   at a few tens of millions, which is a purse a deep run can actually earn.
2. **Cap the escalation multiplier below 100%.** It very nearly doubles
   attack at depth, which under a squared term is a 4x tax on the defence
   needed. This is one constant.
3. **Let player health scale with depth, not just level.** Hits-to-die is
   `HP x (atk+def) / atk²`; HP is the only term that is currently linear in
   nothing much. Player HP grows 6.7x across a run while monster attack
   grows 49x.

Only after one of those lands is difficulty tuning meaningful, and DDA after
that. `make sim` measures each in about a minute.

---

## 25. Floor 100, reached

Two things landed together and between them the game became completable.

### It is not the gear upgrades that feed escalation

The owner's guess was that monster escalation was reading the `+N` off the
weapon and armour, which would have made upgrading self-defeating. It is not:
`monsters_escalation_for` takes **`level` and `floor_entries` only**, and
nothing in `monsters.c` or `mapgen.c` reads a gear field.

But the instinct was sound, because escalation *is* the dominant term — it
just feeds off **level**. `pct = level x 1.0% + floor_entries x 0.5%`, capped
at 100%. By the back half of a run a player is at the cap, and monster attack
is doubled. Since attack is squared in the damage model, doubling it is a
four-fold tax on the defence needed to survive it.

Measured, with 20M gold and a party of five, sweeping only the cap:

| Escalation cap | Deepest floor (Normal) |
|---|---|
| 100% (game default) | 54 |
| 50% | 58 |
| 25% | 80 |
| 0% | **100 — the first win recorded** |

Levelling up therefore makes the dungeon hit harder. That is a deliberate
design (a run should not get easier just by grinding) but at 1% per level it
is the single largest term in the wall.

### The other three bars

The smith's counter now extends past the gear, sold where each belongs:

- **Constitution Draught** (Apothecary) — +10 max HP, permanent
- **Widen your channel** (Arcanist's Guild) — +3 aether
- **Extra shot** (ranged screen) — +2 magazine, and it stays with *you*, so
  buying a new gun keeps it

Health is the large one on purpose. Hits-to-die is `HP x (atk+def) / atk²`,
and HP was the only term in that expression which nothing but levelling fed
— while levelling simultaneously raised monster attack.

**Hired heroes inherit these too**, on the same rule as the weapon and
armour: `companion_max_hp` is what they were taken on with plus whatever the
apothecary has done for the party since. Their own maxhp is frozen at hire
level, so without this a party bought early stops being able to stand in a
fight at exactly the depth the player's own bar stopped being enough.

### The result

| Configuration | Deepest (Normal) | Wins |
|---|---|---|
| No upgrades, 20M gold | 54 | 0% |
| Gear upgrades, 200M | 80 | 0% |
| Gear upgrades, 2,000M | 81 | 0% |
| **+ HP/AE/AM upgrades, 20M** | 54 | 0% |
| **+ HP/AE/AM upgrades, 200M** | **100** | **16%** |

The hard ceiling at 80 is gone. Floor 100 is reachable with the escalation
curve left exactly as it is — what it takes is roughly **200 million gold**.

### So the open question changed shape

It is no longer "is the game winnable" — it is **"can a run earn 200 million
gold?"** A fair-start Normal run currently reaches floor 14 on the median and
28 at best, so the answer today is no. But that is an *economy* question, and
economies are tunable in a way that a squared term in a damage formula is
not.

The three levers now, in order:

1. **Gold income at depth**, or **upgrade prices**. The price curve is
   `20n² + 100n + 20` and cumulative cost goes as n³; halving the constant
   roughly halves the purse a win needs.
2. **The escalation rate.** 1% per level is the largest single contributor to
   the wall and one constant.
3. **The early game**, still separate and still brutal: 30 of 30 Swarm runs
   and 17 of 30 Hard runs die on floor 1.

`make sim` measures all three.

---

## 26. Gold rush floors, and the number they exposed

One floor in eight is now a **gold rush**: same monsters, same danger,
twenty times the coin on the ground. It announces itself on arrival and
carries a standing marker in the sidebar, so it is a floor you recognise and
choose to clear out rather than one you find out about afterwards. Boss
floors are exempt — those already have your attention.

It is the counterpart to the overrun floor: one floor in eight worth going
out of your way *for*, against one worth going out of your way *around*. The
tests pin both halves — that it happens about one time in eight, that it is
worth an order of magnitude more, and that it spawns **the same number of
monsters as any other floor**. A reward that also made the floor safer would
be a different feature.

### What it did, and what it revealed

In the 200M-gold control it lifted the Normal win rate from 16% to **50%**.
On a fair start it moved nothing at all: median floor 14, best 28, unchanged.

That is not a failure of the mechanic, and measuring the income explained why:

| | |
|---|---|
| Gold a fair run finds before it dies (~floor 14) | **775** |
| Gold a *complete* 100-floor descent finds, gold rush included | **4,574** |
| Gold the upgrades to survive that descent cost | **200,000,000** |
| Gap | **~43,000x** |

A twenty-times multiplier on one floor in eight is a rounding error against
forty-three thousand. The economy is not slightly out; it is out by four and a
half orders of magnitude.

### Why the two curves cannot meet

Gold income is roughly **linear** in floor number (`80 + floor x 3` per pile).
The cost of upgrades is **cubic** in how many you have bought, and the number
you need grows with **monster attack squared**. Compounded, demand rises
about as fast as floor⁶ while supply rises as floor.

| Upgrades | Defence gained | Cumulative cost |
|---|---|---|
| 10 | +50 | 13,400 |
| 100 | +500 | 7,274,000 |
| 400 | +2,000 | 436,296,000 |
| 727 | +3,635 (what floor 100 needs) | 2,593,368,940 |

No multiplier on the supply side closes that. What has to change is one of
the exponents:

1. **Make the upgrade step scale**, so late upgrades are worth more than +5.
   Floor 100 needs +3,635 of defence; at +50 a step that is 73 purchases and
   about 2.6M rather than 727 purchases and 2.6 billion.
2. **Flatten the price curve** from quadratic-per-step toward linear.
3. **Scale gold with depth multiplicatively** rather than additively — a
   per-biome multiplier rather than `+3 per floor`.
4. **Reduce what is needed at all**, via the escalation rate (§25): capping
   it at 25% moved the pre-HP-upgrade ceiling from 54 to 80 by itself.

Gold rush stays regardless. It is the right shape — money as a *find* rather
than a grind — and it will matter a great deal once the two curves are within
sight of each other.

---

## 27. The Junkyard

A sixth building in town (`J`, mid-left), and a new kind of dungeon item to
feed it. **Scrap** is not a variant of anything else: it cannot be drunk,
worn, thrown or spent where it lies. It is worth nothing at all until it is
carried back up and weighed.

Walking into the Junkyard sells the lot at once — no menu, the same rule as
the Inn. The sidebar carries a running `Scrap: 37 (~4210 g)` while you are
holding any, so the haul is visible before you decide whether it is worth a
trip home.

### Why scrap and not just more gold

Gold piles scale **additively** with depth: `80 + floor x 3`. That is the
shape that put the economy four and a half orders of magnitude behind the
upgrade tracks (§26) — no multiplier on a linear curve catches a cubic one.

Scrap scales **multiplicatively**. Its value doubles per biome, so an armful
out of the Abyss is worth about sixteen times the same armful out of the
Jungle. It is the only income in the game that is not linear in depth, which
is the whole reason it exists.

### What it did

| | Before | After |
|---|---|---|
| Gold a fair run finds (dies ~floor 14) | 775 | **1,482** |
| Gold a complete 100-floor descent finds | 4,574 | **16,929** |

Roughly a **3.7x** lift on a full descent, against a gap that was ~43,000x.
The gap is now ~12,000x. Scrap is pulling in the right direction and pulling
hard, and it is still not close on its own — as expected, because the
mismatch is between an exponent and a multiplier, and scrap is the first
change that touches the exponent at all.

The remaining distance has to come from the demand side: the upgrade step
(currently a flat +5 per purchase, needing 727 of them for floor 100) and the
price curve (quadratic per step, cubic cumulative). Those are the two numbers
that decide how much survival costs, and they have never been calibrated
against what a run can actually earn.

### Also in

**Gold rush floors** (§26) — one floor in eight, twenty times the coin, same
monsters, announced on arrival and marked in the sidebar. Kept on the owner's
call after measuring that it moves the fair-start numbers very little: it is
the right shape (money as a find, not a grind) and it lifted the 200M-gold
control's win rate from 16% to 50%.

---

## 28. Blood altars and the Pit School

### The altar, every tenth floor

`&` on every floor divisible by ten. Step on it and it takes **half the
health you are currently carrying**, and doubles all gold and scrap for the
**next ten floors** — exactly the span until the next one.

The bargain is priced in the one currency no shop sells back, and it is a
real gamble: the floors right after an altar are where a run is thinnest, and
taking it at 40% health on floor 60 is how a run ends. It never takes the
last point — a menu-less square you walked onto must not be able to kill you
— but it will happily leave you at 20.

The boon is stored as *the floor it lapses on*, not a countdown, so a trip
back to town does not cost you what you paid health for. It never stacks: the
next altar replaces the window rather than extending it.

### The Pit School

`X` in town, top-right of centre. It sells **a point in one of the thirty**,
which is the only way an attribute moves after character creation. Prices are
quadratic in the attribute's current value (`200v² + 300`), so a 3 costs
2,100 and a 9 costs 16,500 — the thirty feed every derived stat in the game,
and a point in them is worth far more than a point of gear.

Each of the thirty carries a one-line note on what it actually buys, because
otherwise the screen is thirty numbers and a price.

**The interesting part is `train_attribute`.** It is the only code in the
game that re-derives stats mid-run, and the obvious implementation — call
`apply_class_to_player` again — would silently do three destructive things:
rebuild the attribute array from the class seed (undoing the training you
just paid for), reset HP to a freshly-derived maximum (throwing away every
Constitution Draught the run has bought), and re-issue the class's starting
weapon over whatever is equipped. The tests exist mostly to pin those three
down, plus that training is not a free full heal.

### Also fixed along the way

**`FEATURE_ALTAR` was first inserted mid-enum**, ahead of `FEATURE_SHRINE`,
which renumbered every later value and broke two unrelated test suites. It is
appended now, with a note saying why: these values reach save files, so
inserting one makes an old save describe a different dungeon.

**Junk pickup was never wired into `main.c`.** The Junkyard, the scrap
generation and the simulator's copy of the pickup all existed and worked, so
the measurements in §27 were real — but the actual game handed the player
nothing. It was lost when an earlier edit aborted partway. Worth recording
because it is the failure mode of scripted edits: the feature *measured*
fine, because the thing doing the measuring had its own implementation.

### Scavenging as you walk

Scrap now arrives two ways. Lying on the floor, as before — and **one piece
every hundred steps below ground**, worth whatever that floor's scrap is
worth. It is not loot you have to spot and detour to; it is what comes off
the walls and out of the wreckage while you are down there, which is why it
accrues with steps rather than with rooms cleared.

Dungeon only. Pacing the plaza would mint money out of nothing.

This turned out to be the largest single income change yet, because the
floors that take the most steps are also the ones where scrap is worth the
most:

| | Before | After |
|---|---|---|
| Gold a fair run finds (dies ~floor 14) | 1,482 | 1,697 |
| Gold a complete 100-floor descent finds | 16,929 | **114,940** |

A **6.8x** lift on a full descent. The gap between what a winning run earns
and what winning costs has gone 43,000x → 12,000x → **1,740x** across the
last three changes. Still not closed, and the remaining distance is still on
the demand side — the +5 upgrade step and the cubic price curve — but the
supply side is now within two orders rather than four and a half.

### Not yet built

**Venusian lizard races** — bet on a race and watch it run. Designed, not
started, stopped here deliberately so the altar and the school can be played
first.

---

## 29. Three bugs from one play session

### Auto-explore walked the run back upstairs

Landing on a staircase takes it, immediately and with no prompt. That is
right for a keypress the player made and wrong for the hundred steps they
delegated — and exploring a floor means crossing the square you arrived on
sooner or later. Every time it did, the run went one floor the wrong way.

The first attempt rejected the *step* if it landed on the up stairs, which
turned "goes the wrong way" into "refuses to move" — and produced the second
report below on floors where the only route crossed it. The fix belongs in
the **search**, not the caller: both pathfinders in `path.c` and the
auto-explore item search now refuse to route *through* the up stairs, so they
go around a single tile. The start square is exempt, because a search only
ever checks the squares it steps into.

Three assertions pin it: a route past the stairs must exist and must not step
on them, the frontier search must not either, and a corridor plugged by the
stairs must be reported as *no route* rather than climbed. Refusing is
recoverable; climbing is not.

### "Nothing left to explore, and no stairs found"

This one was **true**. Some floors had no walkable route to the down stairs
at all.

The room graph is connected by construction, but nearly every later pass
moves walls — lakes, lava, sealed vaults, floor events. A corridor drowned or
bricked over end to end seals off the far half of the floor. Measured across
720 floors: **2 with no route at any cost**, and 4 more reachable only by
wading lava. Rare enough to survive a hundred playtests; common enough that
somebody eventually gets a run they cannot finish through no fault of their
own.

Generation now ends with a reachability check and, if it fails, carves a
direct corridor between the first and last room and re-stamps both
staircases. It has to be the **last** thing that touches tiles: the first
version ran right beside the stairs it protected and was quietly undone by
the vault pass — the measurement did not move at all, which is how it was
caught.

Across 6,800 floors: **zero** unreachable. 15 (0.2%) still require crossing
a hazard, which is a hard floor rather than an impossible one, and
auto-explore's second pass already allows it.

### Messages written over the frame

The Pit School, the Tavern and several shops were passing
`draw_message_log_at` a row derived from however much content they happened
to draw — and on a full screen that row was the bottom border. A message
printed over the frame reads as corruption, not as a screen being too full.

Clipped once, inside `draw_message_log_at`, rather than trusted to every
caller's arithmetic — vertically against the frame line and horizontally
against the right edge. The same fix as §21, applied to the one drawing path
that had been missed.

---

## 30. How big can a floor be?

A floor is `MAP_W x MAP_H` = **140 x 80 = 11,200 tiles**, fixed. Depth changes
what is in it, never its size: floor 99 has the same footprint as floor 1,
and about 47% of it is walkable at every depth.

The question was whether it could be ten times bigger in each direction. It
was built at 1400x800 and measured, twice -- before and after the two fixes
below.

### Memory is not the constraint

`sizeof(Map)` goes 0.27 MB → **8.7 MB**. Nothing on a modern machine. The
real cost there is the save file and the Inn snapshot, which store a whole
Map each -- and the answer to that is already in the game: runs are seeded
(§19), so a floor can be regenerated from `(run_seed, floor)` instead of
stored. Nothing needs to change for that to be possible; it just has not
been done.

### Two functions were doing map-sized work per turn

| | 140x80 before | 1400x800 before | after |
|---|---|---|---|
| `compute_fov` (every turn) | 0.005 ms | 0.409 ms | **0.002 ms** |
| `path_frontier_step` (per companion, per turn) | 0.001 ms | 0.055 ms | **0.001 ms** |
| 3 simulated runs | 0.56 s | 9.5 s | **1.3 s** |

`compute_fov` cleared `visible` across the entire map before lighting a disc
of radius 7 -- O(width x height) to undo O(radius squared). It now remembers
which tiles it actually lit and clears only those, with a full sweep as the
fallback if the list ever overflows. The list is a render cache, deliberately
not part of `Map` and not saved; anything that replaces the map calls
`fov_forget()`.

The two path searches cleared a `visited` array and a parent array per call.
They now use a **generation stamp**: bump a counter, and a square counts as
visited only if its stamp matches. The wrap case does the sweep once, every
four billion calls.

Both are worth having at 140x80 regardless -- `compute_fov` got 2.5x faster
at the current size, and it runs on every keypress. At 1400x800 they turn an
80x cost rise into none at all.

### What is actually left

Not performance. **Content.**

    140x80    walkable 5,355 of  11,200  (47%)
    1400x800  walkable 42,381 of 1,120,000  (3%)

`MAX_ROOMS` is 60. In a hundred times the rock, those same 60 rooms scatter
into a near-empty plain joined by enormous corridors, and a run measured
median floor 1, best 2, 3 gold found, every run stuck: the player spends
hundreds of turns in transit, and the floor-doubling timer fires at 1,000
steps while they are still walking.

That is a statement about *unchanged* mapgen in a bigger rectangle, not about
the idea. A ten-times canvas is only worth having if it is filled with
something other than more of the same rooms -- districts, a lost city, camps,
distinct room types, portals between quarters. That is real work, and it is
mapgen work, not engine work.

The engine-side prerequisites, now that the two hot functions are fixed:

- **`MAX_ROOMS`, `MAX_MONSTERS`, `MAX_FLOOR_ITEMS`, `MAX_FEATURES`** all scale
  with area, not with a constant.
- **Floor-doubling** (1,000 steps) is tuned for a floor you cross in a few
  hundred. On a bigger floor it fires in transit.
- **The minimap** downsamples 140x80 into the terminal; 1400x800 would be
  ~14x20 tiles per character, coarse but workable.
- **Save size**, or regenerating floors from the seed instead of storing them.

A middle step worth considering first: **280x160 with `MAX_ROOMS` at 240**
doubles the linear scale and quadruples the rooms, keeps fill at ~47%, and
costs a few times baseline rather than a hundred times.

---

## 31. Rooms that are not all the same room

Every room was 5-11 tiles on both axes. Sixty draws from one shape, which
makes a floor read as sixty of the same place however the corridors join
them. The floors now come in five shapes, mixed by area so the recipe
survives a bigger map:

| Shape | Size | Per 10,000 tiles |
|---|---|---|
| Plaza | 16-26 x 10-16 | 2 |
| Hub | 10-14 square | 3 |
| Hall | 14-24 x 4-6, either axis | 7 |
| Chamber | 7-11 x 6-9 (the old default) | 22 |
| Cell | 4-6 square | 25 |

**Largest first.** A plaza that cannot find a clear 26x16 has to be tried
before the map fills with cells, or it never lands at all; cells fit almost
anywhere, which is exactly what should be filling the gaps left behind. That
ordering is the whole trick — the same specs placed smallest-first produce
almost no plazas.

**A hub earns its name in the corridors, not the size.** Three or four extra
spokes each, on top of the usual chain and the random cross-links, so more of
the floor genuinely runs through it and you keep passing back through. A
square room with two doors is just a chamber.

### What it changed

    before   walkable 5,355 of 11,200 (47%), one shape
    after    walkable 5,898 of 11,200 (52%), min 5,417, max 6,391

Fill is up five points, but the number is not the point — the *spread* is.
The widest unbroken run of open floor now reaches 137 tiles on some floors,
which is a plaza and a hall lined up, and never drops below 11 on any floor,
which means every floor has something more than a corridor in it. Both are
asserted.

Gameplay measured unchanged: fair-start Normal median floor 11, best 36,
against 11/36 before. Stairs are still reachable on all 540 test floors.

### Why this matters for the size question

§30 concluded that a ten-times canvas is a mapgen problem rather than an
engine one, because 60 identical rooms scattered across a hundred times the
rock gives 3% fill. This is the first half of the answer to that: the room
mix is now defined **per 10,000 tiles** rather than as a flat count, so it
scales with the map instead of thinning out. A bigger floor gets
proportionally more plazas, more hubs, more cells.

What is still missing for a genuinely big floor is structure *above* the room
level -- districts, a lost city quarter, camps, portals between quarters --
and the caps (`MAX_ROOMS`, monsters, items, features) which are still flat
constants.

---

## 32. The floor is a cave that people built in

Room-and-corridor everywhere makes every floor the same kind of place, and
§31's five room shapes do not fix that -- they vary the buildings, not the
ground the buildings stand on. The cave was there first, and not all of it
got built on.

### Wild districts

Laid out **before any room**, so the rooms have to go around them:

| Kind | What it is | Terrain |
|---|---|---|
| Jungle | growth that came in through a crack | ragged floor, thicket clumps, pools, some miasma |
| Sea | standing water in the low ground | water, islands, one or two causeways over it |
| Swamp | neither water nor ground for long | mottled water/floor/miasma, reeds |
| Ruins | a quarter that was old before the tenants arrived | streets, small buildings with a third of the walls collapsed, rubble |

Weighted by biome: the jungle biome is mostly overgrowth and standing water,
the Company Works is old ruins with growth pushing in, the wastes are sour
bog, the abyss is black water.

**`TILE_THICKET` is the one new tile** and it blocks movement *and* sight.
That is the whole reason a jungle feels unlike a room with furniture in it:
you meet things at four paces. `is_walkable_player`, `is_walkable_monster`
and `line_of_sight` all reject it, so pathing, auto-explore, companions and
monster aggro all inherit the behaviour without a special case anywhere.

Corridors already turned `WATER` into `BRIDGE` when they had to cross a lake,
so a road driven through a sea district builds itself a causeway. Roads
through the wilds are wanted -- the floor is inhabited, people cut paths.

### The camp

One floor in six has somewhere you can stop: a palisade with three gates,
tents and crates inside, always a fountain, usually a trader, sometimes a
shrine. **Nothing generates inside it and nothing respawns into it** --
`in_haven()` gates both the generation loop and `spawn_wave`, which is shared
by the respawn trickle and the loitering-doubles.

Two things about it were wrong before they were right:

*The palisade has to be built after every other corridor has been run.*
Built before, each passing corridor punched a hole through it, and a fence
with eight gaps is not a fence.

*The roads have to leave through a gate, not be routed to the middle.* An
ordinary L-shaped corridor from a room to the camp centre cuts the wall
wherever its legs happen to fall -- often running the entire length of one
wall. `haven_gate_road` instead opens exactly one tile and drives straight
out from it until it meets ground that is already open.

Its fittings are placed **below `m->feature_count = 0`**, not up where the
palisade is built. Anything placed above that line is silently wiped; the
altar was lost to precisely this once already (§26).

### How often

    1,600 floors     jungle 293   sea 190   bog 326   ruins 386
                     no wilds at all  43%
                     more than one kind  16%
                     a camp  17%

Forty-three per cent plain is deliberate. The ordinary built floor has to
stay the baseline or the exception stops reading as one -- and a safe room
you can count on is a rest stop, while one you cannot is a relief.

Arriving on a floor now says what is on it, next to the overrun and
gold-rush warnings. Walking into a bog unannounced reads as the map being
broken rather than the floor being unusual.

### What it cost

    fill      5,898 -> 6,086 walkable of 11,200  (52% -> 54%)
    gameplay  fair-start Normal median floor 13, best 38  (was 13 / 38)
    stairs    every test floor still crossable
    saves     SAVE_VERSION 14 -> 15 (Map gained the camp rect and a wild mask)

Two new suites: `wild districts` asserts all four kinds appear, that a
quarter to two-thirds of floors stay plain, and that thicket is never
walkable; `the camp` asserts nothing spawns inside a palisade, every camp has
water, and the water can be walked to from where you arrive.

---

## 33. 280x160

Floor size is now one knob. `MAP_W`/`MAP_H` went to **280x160** -- four times
the ground -- and everything that fills a floor is counted off it:

    MAP_AREA_SCALE     the multiple of the original 140x80 = 11,200 tiles
    SCALE_BY_AREA(n)   n at the old size, 4n at this one

`MAX_ROOMS`, `MAX_MONSTERS`, `MAX_FLOOR_ITEMS`, `MAX_FEATURES`,
`MAX_DISTRICTS`, `ROOM_ATTEMPTS`, `RESPAWN_INTERVAL`, `DOUBLING_INTERVAL` and
the simulator's per-floor turn budget all go through it. Room shapes (§31)
and wild districts (§32) were already per-10,000-tiles and needed nothing.

Four things broke when the number changed, and each of them was a flat
constant pretending to be a design decision.

### Corridors were scaling faster than the floor

The room chain ran through rooms in *placement* order -- effectively random
position order -- so every corridor crossed most of the map. Rooms scale with
area but each of those corridors scales with the map's **span**, so corridor
tiles grow faster than the floor does. Scaled up, walkable went from 54% to
**68%**: the floor stopped being rooms joined by passages and became one open
plain with some walls in it.

Fixed by joining each room to its nearest neighbour already on the network
(Prim, carrying a best-distance array so it is O(n²) and not O(n³) at four
hundred rooms). Cross-links and hub spokes also pick a *near* room now -- on
a big floor "a random other room" is a motorway.

The side effect is the good kind: short corridors mean rooms near each other
end up genuinely near each other, so the floor falls into neighbourhoods on
its own without anything having to plan them.

### Placement was O(attempts x rooms)

Testing a candidate rectangle against every room already placed is fine at
sixty rooms. At four hundred it was most of the cost of a floor, and the
packing ran out of *attempts* before it ran out of *space* -- which is why
raising the room counts stopped helping at 49%.

`g_occ[][]` is an occupancy grid: rooms, districts, the camp, and open water
and lava are all stamped into it **dilated by one tile**, so testing the bare
rectangle enforces the same one-tile gap `rooms_overlap` did. An attempt now
costs the area of the room it is placing and usually bails within a few
tiles. Fill went 49% -> 62% on the same specs, which then let the room mix be
tuned honestly instead of fighting the search.

### Corridors have a width

A corridor is a long room. One in four is now two tiles wide, one in twelve
three -- and a hub's approaches always are, which is most of what makes a hub
read as a junction rather than another square room.

### Monsters and loot were budgeted per room

`for each room: 65% chance of 1-2 monsters` ties the population to whatever
the room mix currently produces. Room counts went from 60 to ~400, so a
Normal floor 1 generated 362 monsters where it used to make 58 -- and then
`DOUBLING_INTERVAL`, still a flat 1,000 turns on a floor taking four times
the walking, ran the population to **7,983**: the array cap, not a
difficulty setting.

Both are budgets off area now (`SCALE_BY_AREA(58)` monsters,
`SCALE_BY_AREA(34)` items), swept over the rooms until spent. `spawn_chance`
still controls *clumping* -- a low chance means fewer rooms holding more
each -- which is what it should have been doing all along.

### Where it landed

    walkable      25,269 of 44,800  (56%)
    rooms         ~400 per floor, all five shapes
    monsters      242 per floor at generation (58 -> 242 is the same density)
    generation    2.3 ms per floor  (was 9.4 ms before Prim and the grid)
    sizeof(Map)   279 KB -> 1,116 KB
    saves         SAVE_VERSION 15 -> 16

    fair start, 20 runs
      Normal   median floor 19, best 61   (was 13 / 38 at 140x80)

Normal got *further* on a bigger map, which is the answer to §30's worry: the
floor is four times the size and it is filled, not stretched.

### Still open

**Hard, Swarm and Hardcore die on floor 1.** They did before this change too
(ROADMAP Phase 0.3) -- but Swarm's 10-20x density multiplier now applies to a
4x budget, so floor 1 generates ~3,500 monsters instead of ~870. This change
made a known problem four times bigger; it did not create it, and it is not
fixed here.

**Four Normal runs in twenty hit the per-floor turn budget**, against three
before. A floor four times the size takes proportionally longer to explore
and the agent's patience did not grow with it.

---

## 34. The Bank of the Deep Well

§25 measured the economy gap at roughly 1,740x, and every fix since has been
*additive* -- junk, the walking scrap trickle, gold rush floors. Additive
fixes cannot close a multiplicative gap, which is why 200M gold reaches floor
80 and 2,000M reaches 81 (§27).

The bank sells the multiplicative one. A share multiplies **every coin you
earn from then on**, and the price is cubic in the share you already hold:

    x2       1,500        a floor or two of takings
    x5       96,000       about a full descent's income
    x10      1,093,500
    x50      176,473,500
    x100     1,455,448,500   the ceiling

Cubic because what is being bought is itself a multiplier on all future
income. Priced linearly, every share after the first is free in practice.

### One funnel for gold

The multiplier had to reach every source, which meant there had to *be* a
single place income passes through. There wasn't. `player_gain_gold()` is
that place now, and consolidating the four scattered copies turned up two
existing bugs:

- The **altar's doubling reached floor gold and scrap but not a single
  kill** -- the one source it most obviously should have applied to.
- **Companion pickups applied nothing at all**: no gold-find, no boon. A
  hero who found the coin was worth less than you finding it.

Order is gold-find, then the altar's boon, then the bank's share, then a
clamp against the two-billion ceiling on an `int` purse. The function returns
what was *actually* credited, and callers log the return value rather than
what they asked for -- so a clamped haul no longer reports a number that
never arrived.

The altar's boon is gated on `p->floor > 0`, which is what stops the scrap
sale double-dipping: junk value is already doubled as it is picked up, and
the junkyard is in town where the boon does not run. Asserted.

### What it does to a run

    fair start, 20 runs, Normal
      bank off    median floor 19   best 61   income per run    41,402
      bank on     median floor 19   best 88   income per run   438,135

Median is unchanged, and that is the honest result: a run that dies on floor
19 does not die of poverty. What moves is the ceiling -- **best run 61 -> 88**
-- because a run that survives long enough to compound now can.

The simulator had its own copy of the junkyard payout, which would have
measured the multiplier as doing nothing to scrap -- most of a run's income.
That is the third time a private copy in `tools/simulate.c` has nearly hidden
a result; it now calls `player_gain_gold` like everything else.

`SAVE_VERSION` 16 -> 17.

---

## 35. 1400x800

§33 shipped 280x160 and said going further wanted a compacted monster array
first. That was the wrong call, and the reason it was wrong is worth writing
down: **I was optimising for a way of playing the user does not want.**

The objection to a big floor was always crossing time. Measured properly:

    turns for an explorer to find the down stairs
      140x80      ~800
      280x160    ~4,000
      700x400   ~33,000        (~33 min of wall clock at the 60 ms tick)

The user's answer was Daggerfall over Morrowind, and the reason is the map.
A floor that takes an hour to walk is not a defect if walking is the game.
Once that is the premise, the question stops being "is it too big" and becomes
"does anything actually break".

### What actually breaks

Very little.

    sizeof(Map)         279 KB -> 27.9 MB   (8.9 MB tiles, 18.4 MB monster slots)
    floor generation    2.3 ms -> 689 ms
    monster turns       0.5 ms -> 11 ms per player turn
    compute_fov         0.00 ms  (radius-bounded, indifferent to map size)
    path searches       ~3-7 ms per call
    walkable            626,518 of 1,120,000  (55%)
    monsters            5,800 per floor at generation
    rooms               ~9,300 per floor

11 ms a turn is imperceptible while walking; 40 keypresses land in 450 ms.
689 ms of generation is a pause when you take the stairs, on a floor you will
then spend an hour inside -- 0.02% of its playtime, and not worth the spatial
index it would take to remove. It is Prim over ~9,300 rooms plus the
nearest-room lookups; room *placement* is not the cost, and raising or
lowering `ROOM_ATTEMPTS` sevenfold changed generation time by 2%.

### Two things did have to change

**Auto-explore's pacing was left alone -- deliberately, after trying not to.**
It sleeps 60 ms and redraws on every step, which at this size means half an
hour of wall clock to cross a floor while the actual computation is under a
millisecond a turn. That looks like an obvious thing to optimise, and it was
tried: adaptive sleep, plus a redraw every sixteenth step on quiet stretches.

It was wrong, and the playtest said so in one sentence -- *"it jumps... it
doesn't go step by step."* Skipping frames makes the walk teleport sixteen
tiles at a time, and the walk is the thing being watched. The 60 ms is not
overhead in front of the content; on a floor this size it **is** the content.

Reverted in full. Worth recording as a category error rather than a bug: the
metric being optimised (turns per second) was not the metric that mattered
(does it look like walking). A big map does not automatically want a faster
way to cross it.

**The test suite would have taken twenty-five minutes.** The map suites swept
~2,100 floors at a fraction of a millisecond each; at 0.7 s each that is not a
thorough suite, it is a suite nobody runs. `sample_seeds()` scales the sample
down as the map goes up, and every threshold is now a fraction of what was
actually sampled rather than a raw count -- the same class of staleness that
had already bitten the doubling and respawn tests twice.

### The general lesson

Three separate constants in this session turned out to be a floor-size
assumption in disguise: the doubling interval, the respawn interval, and the
auto-explore frame delay. None of them looked like map code. The tell is the
same each time -- a literal number that was fine at 140x80 and meaningless at
anything else. `SCALE_BY_AREA` exists for the ones that are about area;
`sample_seeds` for the ones that are about cost.

---

## 36. The auto-explore slowdown

A playtest at 1400x800 reported: auto-explore turns sluggish after about five
minutes, spells and projectiles stay fast, it *speeds up again* now and then,
killing a monster or having one appear makes it fast, and manual walking is
normal throughout.

That is an unusually complete bug report, and every clause of it is a clue.

**My first hypothesis was wrong.** The obvious suspect was the frontier
search flooding a growing revealed region. Measured: 0.00 ms per call even
after 6,000 steps -- because when you are exploring outward you are nearly
always standing next to the fog, and `path_frontier_step` has a fast path for
exactly that. Worth recording that the intuitive answer was not the answer.

**The real cost was `bfs_next_step_to_item`**, run twice per step (hazard-
avoiding, then permissive), and it had two independent problems:

    for each dequeued tile:
        for each of m->item_count items:      <-- 3,402 items on this floor
            is the item on this tile?

That is `O(revealed x items)`. At 23,000 revealed tiles and 3,402 items it is
**78 million comparisons per search, 156 million per step**, and it grows
every time the fog lifts. On top of that the function wiped two map-sized
scratch arrays on entry -- 2.2 million writes -- where `path.c` had long since
solved the same problem with generation stamps.

Measured, walking a real floor with the loot already collected:

    step   400   seen  2,794    5.58 ms per step
    step   800   seen  5,744   16.58 ms
    step  1200   seen  8,329   28.05 ms      and still climbing linearly

Extrapolated to five minutes of walking that is roughly 140 ms a step on top
of the 60 ms pause -- precisely "sluggish".

**Every other clause falls out of the same cause.** Spells and projectiles are
animations and never touch it. Manual movement does not run these searches at
all. And `autoexplore_fight_step` runs *before* them and `continue`s the loop,
so a monster in view skips the expensive path entirely -- which is why a kill
or a new arrival "made it fast again". It was never that fighting was quick;
walking was slow.

The speeding up on its own is the same mechanism from the other side: the
search exits early when it finds an item, so a floor with loot still lying
about is cheap, and one picked clean is worst-case.

### The fix

Three changes, all to work that should never have been per-step:

- **An item lookup grid** stamped once per call, so the inner test is `O(1)`
  instead of a scan over every item on the floor.
- **An early return when nothing is unclaimed** -- late on a floor this is the
  common case, and proving it used to be the most expensive thing in the loop.
- **Generation stamps** instead of clearing the scratch arrays, matching what
  `path.c` already did.

And separately, `autoexplore_progress` was sweeping all 1,120,000 tiles every
step just to count revealed ones. It now reads a counter incremented as the
fog lifts (`fov_reveal_ticks()`), which is deliberately not part of `Map`: it
is a monotonic tick rather than game state, so a save/load resetting it costs
one spurious "something changed" and nothing else.

    after:  0.01 ms per step with the floor picked clean
            0.03 ms per step with loot still on it
            flat -- no growth with revealed area

**28.05 ms and climbing, to 0.01 ms and flat.**

### The lesson

This is the fourth constant this project has found that was really a
floor-size assumption in disguise, and the first that was an *algorithm*
rather than a number. `O(revealed x items)` is invisible at 11,200 tiles and
fatal at 1,120,000. The tell was the same as always -- code written when the
map was small, never re-read when it grew a hundredfold -- but a grep for
suspicious literals would not have found it. Only playing it did.

---

## 37. Floor size becomes a choice

§35 shipped 1400x800 for everyone. The playtest that followed was the
argument against that: one person's Well is another's unplayable, and the
only way to know which you want is to walk one. So the size is now picked on
a new-game screen, alongside the difficulty, and frozen for the run.

    The Shaft    140 x 80     ~6,000 walkable      ~60 monsters
    The Halls    350 x 200   ~39,000              ~360
    The Deeps    700 x 400  ~157,000            ~1,450
    The Well    1400 x 800  ~627,000            ~5,800

Named for the place rather than the file size. "Small/normal/huge" implies
the others are compromises, and none of them is.

### What made it cheap

`MAP_W` and `MAP_H` had been compile-time constants used two ways: as array
bounds, and as loop bounds. Only the first needs to be constant.

So they became runtime values -- `#define MAP_W (g_map_w)` -- exactly the
trick `VIEW_W`/`VIEW_H` had been using since the beginning. **Every loop and
bounds check in the codebase became size-aware without being edited**, and
the work reduced to finding the eighteen array *declarations* and pointing
them at new `MAP_W_MAX`/`MAP_H_MAX` ceilings. Same for the derived caps:
`MAX_MONSTERS` for loops, `MAX_MONSTERS_MAX` for the array.

A `Map` is therefore always 27.3 MB whichever world you pick. The alternative
is dynamic allocation threaded through every system for a saving nobody
asked for.

### The two ordering traps

Both are the same shape -- something read a bound before the bound was set:

- **The viewport clamps to `[58,18] .. [MAP_W,MAP_H]`.** `world_size_apply()`
  has to run before `render_layout_viewport()`, or a Shaft run gets a
  viewport sized for a Well.
- **`load_run` validates coordinates against `MAP_W`/`MAP_H`.** The size has
  to be applied straight after reading the `Player` and *before*
  `player_state_is_sane()`, or loading a Well save while the default is the
  Deeps rejects a perfectly good file. Same fix in `load_inn_snapshot`.

Verified end to end: started a Shaft run, quit, reloaded, and it came back a
Shaft rather than the default.

    all four sizes:  fill 54-56%, every cap scaling, stairs always reachable
    SAVE_VERSION 17 -> 18 (Player carries world_size)

---

## 38. The Bank screen's stale line

Reported: the Bank screen still showed *"Wanderer steps out of the wreck-trail
and into the plaza"* underneath it, and read as the screen not having been
cleared.

The screen was cleared. `draw_bank()` opens with `erase()` like every other
full-screen view. What happened next is that it deliberately redrew the
message log at the bottom -- and the newest line in that log was something
said out in the plaza, before the player ever walked through the door.

So the diagnosis was right and the mechanism was not, which matters because
the two point at different fixes. "The screen is not clearing" points at the
renderer. "The screen is clearing and then having something old drawn onto
it" points at the log.

Not fixed by dropping the log from these screens: it is the only feedback
channel they have. *"The clerk closes the ledger. 1500. You have 60."* has to
land somewhere.

Fixed by `log_reset()` on entry to all ten screens that draw the log under
their content -- the four shops, the guild, the merchant, the ranged counter,
the Tavern, the Pit School, the Bank and the quest board. Only what happens
*inside* a screen belongs underneath it.

The pleasant side effect: walking back out into the plaza, the log now holds
what you just did in the building rather than what you did before going in.

This is the second complaint about this log in the same place. The first
(§2.1e) was messages drawn over the bottom frame, fixed by clipping inside
`draw_message_log_at`. Clipping was the right fix for *that* symptom and did
nothing for this one -- worth noting that a shared drawing helper can be
correct and still be showing the wrong content.

---

## 39. Three districts that are rules, not shapes

The four wilds (§32) differ by terrain. These three differ by *what happens
while you stand in them*, which needed the Map to remember **where** its
districts are -- it had only a bitmask of which kinds existed.

| District | The rule |
|---|---|
| **Mycelium** | The mats carry footfalls: aggro at 20 tiles with no line of sight needed. Stand still two turns and it forgets you. |
| **Hive** | Hurt one and everything within 20 tiles wakes *and* takes a point. Nothing hides you. |
| **Mire** | Mist caps your sight at 3 tiles, whatever your Vision score. |

Built as one mechanism with three consumers rather than three features:
`MapDistrict` on the Map plus `district_at(x,y)`, then one rule apiece in the
aggro check, the damage path and the FOV call.

**The mycelium needed a wait key.** "Hold still for two turns" is not a move a
player can make if there is no key for doing nothing, so `.` (and `5`) now
pass a turn. It is the first time in this game that inaction is a tactic, and
the pair is deliberate: one district where stillness saves you, one where it
does nothing at all.

`DIST_*` moved from a private enum in `mapgen.c` to `common.h`, because the
districts are saved with the floor and read during play. `WILD_KIND_MAX` is
now `DIST_KIND_COUNT` rather than the literal 4 that would have silently
skipped the new arrival messages.

### A camp with no way in

The new suite caught a real bug that predates it: **`haven_gate_road` could
dead-end at the map border**. A camp near the edge points a gate outward, the
road runs three tiles into rock, hits the boundary and stops -- a hole in the
palisade that buys nothing. One camp in 132, all of them on small maps where
a camp sits proportionally nearer an edge.

The road now walks the line first and only cuts it if it *arrives* somewhere,
tries the fourth side when an earlier one was a dead end, and falls back to an
ordinary corridor if all four fail. A shredded palisade beats an unreachable
camp.

Worth noting how it surfaced: the earlier camp suite asserted reachability
and had been passing. Adding three district kinds changed `rand()` consumption
in `pick_district_kind`, which reshuffled every floor, which sampled a bad
one. The bug was always there.

---

## 40. What a floor is worth, per world size

Everything that fills a floor is counted per unit area, so a bigger world
pays more per floor. Measured:

    size        districts   xp/floor   gold/floor   level after 10 floors
    Shaft            0.7      1,817        3,037    ~29   -> +34% escalation
    Halls            3.7     10,858       18,645    ~45   -> +50%
    Deeps            7.6     42,937       16,629    ~59   -> +64%
    Well             9.6    169,787       65,606    ~73   -> +78%

**This mostly self-balances, and the reason is escalation.** Monster attack
scales with *player level*, and level is driven by XP, and XP scales with area
exactly as gold does. A Well run is richer at floor 10 than a Shaft run and
also fighting monsters that hit 44 percentage points harder. The bigger world
is not straightforwardly easier; it is faster in both directions.

**Where it stops self-balancing is the cap.** Escalation is capped at +100%,
reached around level 100. Past that, further levels are pure gain -- and a
Well run reaches the cap far earlier in its hundred floors than a Shaft run
does. So the sizes diverge in the *late* game, not the early one, and the
trade for it is time: a Well floor is a hundred times the walking.

The design rule this implies, for anything added later: **rewards scale with
area automatically, so a "room full of gold" is a room full of gold *per
10,000 tiles*.** That is the correct behaviour -- more floor, more loot, more
time -- but it means a flat reward number is really a reward *density*, and
should be chosen as one.

---

## 41. Waygates priced in walking, not floors

A waygate every five floors is a reasonable walk on the Shaft. On the Deeps
and the Well it is not a walk at all: a floor is 157,000 to 627,000 walkable
tiles, so "climb four floors to reach a gate" means an evening spent
retracing cleared ground.

`waygate_interval()` returns 1 for worlds of 280,000 tiles or more and 5
below it, read by both the generator and the arrival hint. The convenience is
now priced against the same amount of *walking* rather than the same number
of floors.

Keyed off the live map dimensions rather than the `WorldSize` enum, so it
stays correct if the dimensions are ever set another way.

---

## 42. Three districts that change how you fight

Tranche two. Where §39's three altered *what notices you*, these alter what
your character is good at.

| District | The rule |
|---|---|
| **Crystal Caverns** | 30% of casts and shots ricochet off a crystal and hit you for half. Melee is untouched. |
| **The Quiet Quarter** | Nothing is awake; they notice you at 3 tiles. Damage anything and the *entire* quarter is up, permanently. |
| **Storm-Cage** | A rod is struck every 5 turns, telegraphed one turn ahead. Within 2 tiles hurts. Anything touching the rod dies. |

Two new tiles, both **impassable but transparent** -- `TILE_CRYSTAL` and
`TILE_ROD`. That combination did not exist: thicket blocks sight and movement,
walls block both, water blocks movement and is see-through but not
*stand-next-to-able*. A district you can see clean across and cannot walk
across is the whole character of the crystal caverns.

**The crystal rule hooks the one funnel both casting and firing pass through**,
so it covers all 180 spells and all 18 ranged weapons without touching any of
them individually. The aether or the round is spent either way -- it left you.
This is the first place in the game where a melee character is straightforwardly
better off than a caster, which the spell pool has needed since it landed.

**Storm-Cage is the first hazard you can aim.** Anything adjacent to a struck
rod dies outright, so luring something onto one is a kill the floor performs
for you. It is telegraphed a turn ahead on purpose: a hazard you cannot see
coming is a dice roll, not a decision.

### The camp, a third time

A third unreachable camp, a third distinct cause: this time a gate road
stopped at a wall of crystal, because "arrives somewhere" was written as *not
wall and not water* rather than *walkable*. Thicket, crystal and rods are all
impassable.

Fixed -- but the *real* fix was to stop fixing causes. There is now a
reachability invariant checked once at the end of generation, exactly like the
stairs one above it, that carves a corridor to the camp if nothing else did.
A generator with this many interacting passes does not want a proof that each
pass is safe; it wants the invariant asserted after all of them have run.

Three separate bugs, all the same shape, all found by the same suite only
because adding district kinds reshuffled `rand()` and resampled the floors.
That is luck, and the invariant is what replaces it.

---

## 43. Colour, because grey corridors are the actual failure mode

Ten districts were mechanically distinct and visually identical -- all of them
plain floor with scatter, distinguishable only by walking into their rule.
On a floor that takes an hour to cross, that is not a cosmetic problem.

Each district now tints its own **neutral ground** -- floor, wall, decor,
thicket -- and nothing else. Stairs stay stairs-coloured, water stays water,
features keep their own colours: those carry information, and overpainting
them to make a place look nice costs more than it buys.

    jungle   green      mycelium  magenta, lit    crystal  cyan, lit
    sea      blue, lit  hive      amber           quiet    white
    swamp    cyan       mire      blue            storm    yellow, lit
    ruins    white

Eight terminal colours will not separate ten districts, so **brightness is the
second axis** -- and "lit versus dim" happens to be exactly the right
distinction for mycelium-against-mire and crystal-against-swamp.

Applied in `cell_terrain`, the single funnel every terrain cell passes
through, so it lands in all three render modes at once.

`district_color_pair()` is exposed purely so the mapping can be asserted:
ten kinds sharing eight colours is the kind of table that rots silently, and
the suite now fails if any two collide.

---

## 44. Two of a kind on one floor

A floor has always been able to carry more than one district, and nothing
stopped it carrying two of the same kind -- `pick_district_kind()` is called
independently per district. Measured:

    size       districts/floor   floors repeating a kind   most of one kind
    Shaft            0.8                 3%                      2
    Halls            4.4                25%                      8
    Deeps           12.3                25%                     15
    Well            14.0                25%                     15

A quarter of floors on the larger worlds, and as many as **fifteen quiet
quarters on one Well floor**.

That was fine while districts only differed in terrain. It stopped being fine
the moment they carried rules, because both new rules matched on **kind**:

- Breaking the quiet in one quarter woke *every* quiet quarter on the floor,
  across a million tiles.
- Two hives whose edges fell within the 20-tile alarm radius shared news they
  had no business sharing.

Both now scope by `district_index_at()` -- the district you are standing in,
not every district that shares its name. `district_at()` is unchanged and
still answers "what kind of place is this", which is the right question for
the mist and the mycelium, where kind genuinely is all that matters.

Worth recording that this was found by a design observation rather than by a
test or a crash: *"you can have multiple biomes on the same floor, and on
bigger maps they could even repeat."* The suite asserted the rules fired; it
never asserted they fired **only where they should**. Both directions are now
asserted, with two quiet quarters a hundred tiles apart.

The general shape, third time in this project: a rule written when a thing
was unique, still running after the thing became plural.

---

## 45. The arena, and a way out

The last two off the shortlist. They pull in opposite directions on purpose:
one is a fight you cannot leave, the other is a way to leave one.

### The Sunken Colosseum

An amphitheatre cut into the rock -- an elliptical bowl of sand inside a ring
of seating, with one gate. Step onto the sand and it comes down.

    ten waves, each fighting as if the floor were `floor + wave * 2`
    the gates open again after wave five
    wave ten pays a guaranteed set piece, plus gold

**Adopted from the second Colosseum proposal rather than the first**, because
"leave after five, or press on" is a decision you make five times over
instead of a door that locks once. The reward is the point: set pieces are
otherwise random drops with no deterministic path, so set-hunting has had no
goal since the system landed. This is the first place in the game you can
*go and get* one.

Deliberately bare in the middle. Cover would let you take ten waves from a
doorway, which is the opposite of what an arena is for.

The wave loop rides `districts_tick`, so it needs no new turn plumbing, and
it scopes by district *index* -- §44's lesson applied on the way in rather
than after the bug.

### The Toll

A tollkeeper, `$`, on floors big enough to get lost on -- the Deeps and the
Well only, gated on `waygate_interval() == 1`. Hand over **everything in your
purse** and they walk you to the down stairs.

This one exists because the floors got big. "Lost, hurt, and a long way from
the stairs" is a real position on a 627,000-tile floor, and until now the
only answers were a recall charm you might not have or dying. The price is
deliberately total: it should be a decision made in a bad moment, not a
convenience. On the Shaft it would be a joke -- the stairs are never far --
so it does not appear there.

Two guard rails, both asserted: an empty purse buys nothing, and a floor with
no down stairs (the Warden's) cannot be tolled past.

### Verification note

The arena added a fourth `Biome -> int` sign-conversion warning, of the same
known category as the three baseline ones. Cast explicitly rather than
allowed to raise the baseline: a warning count that drifts upward one
justified case at a time stops being a signal.

---

## 46. Work that takes turns

The first of the three systems the idea pool was parked on, and the one with
the most consumers waiting: **commit N turns to something and be unable to
answer while it runs.**

The game had exactly one of these -- channelling a recall charm -- written
inline in the key loop as a bare counter. This is the general form beside it,
in the same shape, because the second one would have been another special
case and the eighth would have been unmaintainable.

    Player: work_turns_left, work_kind, work_x, work_y
    `g`   : start on the tile you are on, or any of the eight around you
    each turn: the world moves, you do not
    Esc   : stop
    hurt  : the job is ruined

**Being hit ends it, and pays nothing.** That is the rule that makes the
whole thing a decision rather than a menu: you cannot start a ten-turn job
until you have made the ground safe, and being driven off three turns from
the end has to cost you the job or the commitment is fake.

Two consumers, chosen because they already existed as scenery:

- **Harvest a storm rod** (5 turns). The tension is built in and I did not
  have to design it: a rod is struck every 5 turns, so working one is a race
  against a clock that is already ticking. The rod is consumed, which also
  removes it as a lightning target -- a storm-cage gets safer as you work it.
- **Strip a wreck** (10 turns) -- rubble, but only inside a ruins district.
  The same rubble elsewhere stays scenery; treating it as a job everywhere
  would carpet every floor in them.

`work_available_at()` is the single table both the key handler and the
resolver read, so they cannot disagree about what is workable. Asserted in
both directions: rods and ruin-rubble offer work, plain floor and rubble
outside the ruins and the town plaza and off-map coordinates do not.

### What is still parked

**Displacement** -- conveyors, floods, teleports, sliding ice, riding a
current. Five candidates, one system, and it stays expensive: every movement
site (player, monsters, companions, auto-explore, knockback) has to accept
"you moved without asking".

**The attribute-reward decision**, which is not mine to make. Roughly a third
of the hundred candidates hand out permanent attribute points; the Gladiator
School charges `200v^2 + 300` for one. My recommendation stands -- districts
pay in School credit or discount, never raw points -- but it is a design call
about what the School is for.

### Verification note

The turn loop was confirmed live only as far as "the key is wired and the
search runs" -- `g` on bare ground reports there is nothing worth the time.
Walking a character into a ruins district to watch a ten-turn job complete
took more attempts than it was worth, so completion and interruption are
covered by the suite rather than by eye. That is weaker evidence than usual
and worth saying plainly.

---

## 47. Writs, and the rule they enforce

The attribute problem from §§40-44 got a decision: **districts pay in School
credit, never in raw points.**

Roughly a third of the hundred candidates in the idea pool wanted to hand out
a permanent attribute point directly, several of them "+1 to all thirty". The
Pit School charges `200v^2 + 300` for one point in one attribute -- 20,300
gold at value 10, and the only sink of its kind in the game. Built as
proposed, the pool would have given away more than a character could buy in a
hundred runs. Not a balance concern; the difference between the School
existing and not.

A **writ of training** is the compromise. It pays the fee outright, but it is
redeemable only *at* the School -- so the School stays the one place an
attribute ever moves, which is the property worth protecting. `w` spends one;
`Enter` still pays in coin.

**One source, and it is the hardest thing in the game.** Finishing all ten
arena waves grants exactly one, alongside the guaranteed set piece. That
keeps writs rare by construction rather than by a drop table, and it gives
the arena a second reason to exist beyond set-hunting.

Asserted: you start with none, finishing the arena is worth exactly one, and
standing in a finished arena mints no more.

### The rule, for whatever gets built next

> A district may pay in gold, gear, scrap, consumables or writs. It may not
> move an attribute directly. If a reward feels like it should be worth a
> point, it is worth a writ.

Written here rather than only in the roadmap, because the roadmap is a list
of things to do and this is a constraint on how to do them.

---

## 48. Displacement, and why it was one function

The last of the three parked systems: **something moves you without asking.**

Five candidates wanted it -- conveyor belts, flood surges, teleport orbs,
sliding ice, and riding a current. I built the current first, because it is
the only one where being moved is a *gift*: the Aqueduct's channels are the
fast route across a district and not your route, and that tension only exists
if walking the banks instead is possible.

`displace_actor()` walks an actor up to N tiles and stops at the first tile it
must not enter. **The guards are the entire value of it being one function**,
because every future consumer inherits them rather than rediscovering them:

- a wall, or anything else impassable
- **a staircase** -- being swept to the next floor by a current you did not
  choose to step into is not a mechanic, it is a bug report
- another actor, monster or player -- two on one tile is the invariant every
  combat routine assumes
- the edge of the map
- a zero direction, which the first version cheerfully "moved" three times

Each of those is asserted once, which is the point of paying for the
abstraction.

The current carries monsters too, from the same district record, so an
aqueduct is chaotic for both sides. Flow direction lives on `MapDistrict`
rather than on `Tile`: one direction per channel-district reads as a river,
a per-tile direction would read as noise, and `Tile` has nothing to spare at
1.1 million of them.

### The two mistakes worth recording

**A zero direction looped.** `displace_actor(m, &x, &y, 0, 0, 3, ...)` moved
the actor onto its own tile three times and reported three steps. Harmless
today -- nothing passes a zero direction -- and a live bug the first time a
conveyor's direction is read from uninitialised district data. Caught by
asserting the degenerate case rather than by it happening.

**I cast away const to reach `monster_at`.** That is the same `-Wcast-qual`
this project fixed once before in `companions_place`, and it went in the same
way: the signature was inconvenient, the cast was one character. Taking a
mutable `Map *` is honest about what the function needs; the cast was the
tidier-looking lie.

### Where that leaves the pool

Twelve district kinds, and **all three parked systems are built**: multi-turn
actions (§46), the attribute rule (§47), displacement (§48). Everything left
in the hundred-candidate pool is now straight content -- conveyors, floods,
quicksand, mining, sarcophagi, orbs -- with nothing underneath it that has to
be invented first.

---

## 49. Two districts that change the fight itself

The first content built with nothing underneath it left to invent. Both were
"worth building, with a change" in the pool; both are cheap; both add a
pressure the game did not have.

### The Blood Marsh

Shallow red water, walkable by everything, and **anything standing in it
mends** -- five HP a turn, monsters only.

Every hazard in the game until now hurt both sides symmetrically. This is the
first terrain that is *good for them and neutral for you*, which turns a
fight into a positioning problem: you can hold the dry ground, or you can go
in after something that is healing faster than you are hurting it.

The pool version had the water heal monsters and offered a poison flask to
taint it. Dropped -- consumable-gated counterplay to a terrain rule is a
second system for one district. Walking around it is counterplay enough.

### The Chromatic Abyss

Pools of liquid light, each colour a **stance**:

    red     +50% attack, -50% defence     hit harder, fold faster
    blue    -50% attack, +50% defence     hold, but do not hurt
    green   +3 HP a turn                  mend, slowly

It is the only candidate in a hundred that rewards standing *somewhere*
rather than avoiding something, and the only one where the right answer
changes mid-fight -- red to break something, blue when it turns on you.

**Re-read every turn rather than applied as a buff.** Step off and it is gone
immediately, which is the entire mechanic; a buff with a duration would let
you charge out of the red pool still carrying it.

The stance lives on the `Player` because `player_eff_atk()` takes only a
`Player`. Threading a `Map` through that signature would have touched every
call site in the game to express something the ground already tells us once
a turn -- the wrong trade, and worth writing down as the reason rather than
leaving the field looking arbitrary.

Asserted in both directions, which for a stance means: it applies while you
stand on it, and it is *gone* when you step off. The second half is the one
that would rot silently.

    14 district kinds, 200 floors sampled, 40% still plain

---

## 50. The Assembly Line, and the golem I did not build

### Belts

Lanes of moving belt with walkable strips between them, **alternating
direction lane by lane**. That alternation is the whole district: you cannot
walk against a belt, so getting anywhere means finding the lane already going
your way. It is navigation, where the Aqueduct (§48) is a shortcut -- the
same `displace_actor()` producing an opposite feeling, which is the best
evidence the abstraction was worth building.

Lane direction is **derived, not stored**: lanes are carved every three rows,
so `(y - district_top) / 3` is the lane number and its parity is its
direction. Two hundred belt tiles cost nothing to remember, and a lane cannot
end up half-reversed because there is no per-tile state to disagree with
itself. Asserted: adjacent lanes genuinely oppose, the third comes back
round, and a belt tile outside an assembly district carries nothing.

### The golem, deferred

The pool's reward for this district -- collect three keys, build a golem,
keep it as a companion for the floor -- is still the most interesting reward
in the hundred, and I have not built it.

`Companion` is a thirty-field struct carrying gear, a spellbook, ranged
slots, cooldowns and a `roster_idx` that the hire and release paths key off.
Building one from scratch means getting every field right; **this project has
already shipped one bug of exactly that shape** -- a zero-initialised
Companion that read as a Conduit caster holding a bow, because `-1` sentinels
do not survive a `memset`.

The safe construction is to generate a well-formed candidate with
`tavern_candidate()` and override the presentation, but that leaves
`roster_idx` pointing at a tavern slot the golem does not occupy, which the
release logic would then treat as a hireable hero.

That is a careful half-hour, not a five-minute addition, and it is the wrong
thing to rush onto the end of a long session. The belts stand on their own;
the golem wants `companion_grant()` written deliberately, with the roster
paths taught what a companion with no roster entry means.

    15 district kinds

---

## 51. One snare, two gardens

The Carnivorous Garden and the Quicksand Basin were triaged as *one trap
system with two skins* rather than two districts, and that is what they are:
a single `TILE_SNARE`, with the district deciding what it costs.

    garden      8 damage, held 2 turns    bites hard, lets go
    quicksand   3 damage, held 4 turns    barely hurts, does not

**The asymmetry is the mechanic.** A snare is walkable by the player and
impassable to everything else, so it is ground you can take and they cannot,
at a price only you pay. Standing in a snare field means you will not be
surrounded -- and cannot run either.

Three things fell out of that for free, which is the sign the rule was placed
in the right layer:

- `is_walkable_monster()` already gates monster pathing, so nothing follows
  you in without a line of code about monsters.
- Auto-explore's hazard-avoiding pass uses the same predicate, so it **routes
  around snares automatically** -- and its permissive fallback still crosses
  one when that is the only way through. Neither behaviour was written.
- The hold reuses `stun_turns_left`, which both the manual and auto-explore
  turn paths already honour.

The pool's quicksand had a deliberate-descent hook -- sink ten turns and take
thirty damage for a chest at the bottom. Dropped for now: it wants a second
tile and a reward table for one district, and the trade above is already the
interesting part.

    17 district kinds, 200 floors, 40% still plain

### On stopping here

That is seventeen kinds, three systems, and about a dozen sessions' worth of
pool worked through. The remaining candidates -- Necropolis sarcophagi, mine
shafts, the scaled clone, destructible boilers, the Inferno Forge's earned
immunity -- are all straightforward now, and none of them is blocked.

The two things genuinely worth doing before more content:

1. **`companion_grant()`** (§50), so the Assembly Line's golem can exist.
2. **A balance pass over the seventeen.** Each district was measured for
   generation and correctness; none has been measured for *fun*, and the
   simulator has not been re-run against a floor set this varied. Seventeen
   kinds at 40% plain is a very different game from the one the winnability
   numbers in §§25-28 describe.

---

## 52. A companion that was built, not hired

`companion_grant_golem()` -- and the reason it was worth doing carefully
rather than quickly.

Every `Companion` until now came from the Tavern roster and carried a
`roster_idx` into it. The reaper uses that index as a **shift amount**:

    p->tavern_hired_mask  &= ~(1u << c->roster_idx);

With `roster_idx = -1` that is undefined behaviour, not merely a wrong
answer. It is precisely the trap §50 predicted, and it was sitting there
waiting for the first companion that was never hired.

Three things make the golem safe:

- It is built by generating a **real roster candidate and overriding it**,
  not by filling thirty fields by hand. A zero-initialised `Companion` has
  already shipped once as a Conduit caster holding a bow, because `-1`
  sentinels do not survive a `memset`.
- `roster_idx = -1`, and every site that indexes the roster masks now checks
  `>= 0` first.
- `temporary = true`, and `companions_dismiss_temporary()` runs at all three
  `enter_floor()` sites. A thing built out of an assembly line does not
  follow you downstairs.

The console costs **three keys** -- the same keys that open locked doors,
deliberately. It turns a currency you were hoarding into a choice, which is
better than inventing a second kind of key with no other use.

Asserted: it occupies a slot, holds no roster seat, never answers to a roster
index, does not cast or shoot, frees no seat and mourns nobody when it dies,
and is gone when the floor changes.

### What this actually unlocked

The golem was the hard case. Now that a companion can exist without a roster
seat, everything of that shape is a thin variant:

- **a hero found and freed in the dungeon**, who joins you
- **escort quests** -- Phase 3 has had "blocked on a friendly-NPC actor that
  does not exist" against it since the beginning, and that actor now exists in
  two flavours
- **random encounters** that hand you an ally rather than a fight

None of those needs new machinery: a name, a statline, and `roster_idx = -1`.

---

## 53. Balance pass over seventeen districts

The winnability numbers in §§25-28 described a game with four district kinds
and no district *rules*. Seventeen kinds later they were stale, so this
re-measures rather than assuming.

The simulator now takes a **world size** as its last argument, because "is it
winnable" has four answers and the harness has to say which one it is
reporting. The Shaft (140x80) is the comparable one -- it is what the old
numbers were measured at.

### What was wrong

    fair start, Shaft, before
      Normal median floor 7   worst crowd 265   4 of 16 runs stuck

Median 7 against a historical 11-13, and every stuck run showed the same
thing under `AETHER_SIM_VERBOSE`: `seen=0` -- the stairs were never *found* --
with **137 to 156 monsters alive on a floor that generates 58**.

So two measurements, and together they are a diagnosis:

    turns to find the stairs   ~800 before districts -> ~1,057 now (worst 1,675)
    doubling interval          1,000 turns

Districts block sightlines and lengthen routes, which pushed the cost of
crossing a floor **past the doubling interval**. Doubling exists to punish
loitering; it had quietly started firing during ordinary exploration. Once it
fires it compounds -- 58 monsters became 1,996 in 4,000 turns of standing
still.

### The change

One constant. `DOUBLING_INTERVAL` 1,000 -> 2,500, which puts it comfortably
above the worst measured crossing (1,675) and still well inside what camping
a floor looks like.

    loitering 4,000 turns:  1,996 alive -> 634 alive

    fair start, Shaft, after
      Normal median floor 13   worst crowd 110   4 of 16 runs stuck

Median back to its historical range, and the crowd figure more than halved.

### What did not change, and is not hidden

**Hard, Swarm and Hardcore still die on floor 1.** Doubling was never their
problem -- they generate at 2-3x and 10-20x density, so floor 1 is lethal
before a single turn passes. This is Phase 0.3 and it is untouched.

**Four runs in sixteen still exceed the harness turn budget.** That is
exploration and combat time, not stranding: the stairs are always reachable
(asserted), and a real player simply keeps walking where the harness gives
up. I have deliberately *not* raised the budget to make the number look
better -- it is measuring something true, which is that floors take longer to
cross than they used to.

### The lesson

This is the fourth time a constant in this project turned out to encode an
assumption that had expired -- and the first one that was a relationship
between *two* numbers rather than one number in isolation. 1,000 was correct
while crossing a floor took 800 turns. Nothing about the doubling code
changed; the thing it was measured against did.

Worth checking whenever floors get harder to cross: **does an interval meant
for standing still still exceed the cost of moving?**

---

## 54. DDA, phase one: measure before acting

Escalation was never DDA. It is a static formula on player level and floors
entered, so it responds to *what you are* rather than *how you are doing* --
which is exactly why it fights an RPG grind. A level gained raises difficulty
permanently, whether you are steamrolling or barely alive.

`src/dda.c` reads performance instead, and **nothing consumes it yet**. Phase
one instruments the signals and has the simulator report what the curve
*would* have been, so phase two tunes against measurements rather than
guesses -- the same discipline the winnability work used.

### The reading

Four signals, all already tracked, folded into one bounded pressure:

    untouched, no potions          +4   cruising
    fought through                 +1   steady
    under 45% health, or 3 potions  -8   hurt
    under 20%, or you retreated    -14   routed

**Asymmetric on purpose.** A rout undoes three cruises. DDA that swings hard
and evenly reads as the game cheating, and players are right to resent that;
quick to forgive and slow to punish is the only version that feels fair.

Producers are calls (`dda_note_heal`, `dda_note_retreat`) rather than field
pokes, so every place a signal is generated is one grep away.

### What the first measurement says

    fair start, Shaft, 16 runs per mode
      Normal     final -4   peak +13   trough -10
      Hard       final  0   peak   0   trough  -1
      Swarm      final  0   peak   0   trough   0
      Hardcore   final  0   peak   0   trough   0

Two findings, and the second is the important one.

**Normal's curve moves the way it should.** It coasts early (+13) as a fresh
character clears shallow floors, then gets pressed (-10) as depth outruns
gear, and ends slightly negative. That trough is precisely where an adaptive
system should be easing off, and it exists without anything being tuned.

**DDA cannot fix Phase 0.3.** Hard, Swarm and Hardcore all read flat zero --
because they die on floor 1 and never complete a floor, so they never
generate a reading at all. An adaptive difficulty system has nothing to adapt
*from* before the first data point. Floor-one lethality is generation-time
density and has to be fixed there.

That is worth knowing before wiring anything: it would have been easy to
build DDA, watch Swarm still die on floor 1, and conclude the DDA was broken.

### For phase two

The curve uses about a third of its range over a 13-to-37 floor run, so the
constants are conservative. Either the per-floor deltas want to be larger or
the bounds want to be tighter -- a decision to make against the numbers above
rather than in advance.

And the cost to accept when pressure starts feeding difficulty: **seeded runs
stop being reproducible in their monsters**. Floor *layout* stays
deterministic (`floor_seed(run_seed, floor)`); how hard the things on it hit
would depend on how you played. Worth it, but worth writing down.

---

## 55. The horde is the mode: ramp the density, do not flatten it

Phase 0.3 has read "Hard, Swarm and Hardcore die on floor 1" since the
beginning, and I had been treating it as *too many monsters*. The playtest
correction was that the density is the appeal -- a zombie invasion -- and the
ask was for **more**, not less.

That reframes the problem entirely. It was never "reduce the horde"; it was
**"survive the horde"**, and a flat multiplier gets that wrong at both ends.

### What flat 10-20x actually did

Floor 1 of Swarm generated ~870 monsters against a level-1 character with a
starting weapon and no spells. That is arithmetic, not a fight, and it ended
every run before the first staircase. Meanwhile the deep end *capped* at 20x,
so floor 90 was no denser than floor 20.

The multiplier now ramps with depth:

    swarm_mult = 1 + floor / 1.8,  capped 40    (Swarm, Hardcore)
    swarm_mult = 2 + floor / 20,   capped  8    (Hard)

    floor      1     5    10    20    40    60
    Swarm    144   267   468   842  1522  1997
    Hard     129   120   131   192   450   882

You open at roughly Normal's density and grow into the invasion, and the deep
end goes **considerably past where the flat number topped out** -- floor 40 now
carries more than floor 100 ever did.

### What it bought

    fair start, Shaft, 16 runs      before -> after
      Hard       median 1, best 9  ->  median 2, best 9
      Swarm      median 1, best 1  ->  median 2, best 10
      Hardcore   median 1, best 1  ->  median 2, best  6

Swarm's best run went from floor 1 to floor 10. It is the first time in this
project's history that Swarm has been anywhere.

### Two honest limits

**The array cap binds at depth.** `MAX_MONSTERS` is `SCALE_BY_AREA(2000)`, so
at Shaft size a floor saturates around 1,997 from roughly floor 55 onward --
past that the ramp is decorative and the *cap* is doing the balancing, which
is not a difficulty setting. Bigger worlds scale the cap with area, so the
Well has room to keep climbing.

**The simulator is a pessimistic proxy for a horde.** At most eight things can
reach you at once, so a horde is survivable in a corridor and lethal in the
open -- and the harness agent does not seek chokepoints, use snares (which
monsters cannot enter), or lure anything onto a storm rod. A player who
fights in doorways should do markedly better than these numbers. That makes
median 2 a floor rather than an estimate, and it is worth remembering before
tuning further against it.

Median 2 is not "fixed". It is the difference between a mode that ends on
floor 1 and one that has a shape.

---

## 56. Only simulate what is near

A playtest question -- *"are you calculating monsters on the other side of the
map? I'm thinking of the Decima engine, which only simulates what's visible or
near-visible"* -- and the answer was yes, all of them, every turn.

`process_monster_turns` walked the whole array on every keypress. On a Well
floor that is 5,800 monsters, most of them several hundred tiles away, having
never seen the player, taking a 25%-chance random step nobody could observe.
Paid for on every single turn.

    monster turns, 1,658 on the floor:   11 ms  ->  0.0 ms

Two radii, both set beyond every rule that reaches past line of sight:

    MONSTER_ACTIVE_RADIUS  40   nothing else gets a turn
    MONSTER_LEASH_RADIUS  120   unless it is already hunting you

Field of view is 7, aggro is 8, the mycelium and hive senses are 20. Nothing
within reach of any rule is ever asleep, so this is invisible in play -- the
entire difference is in what is *not* simulated.

### What it unlocked

`MAX_MONSTERS` was never a design choice. It was the point where an O(n) turn
loop over everything on the floor stopped being affordable. With that gone it
went 2,000 -> 6,000 per area unit, and the interesting result is that **the
cap is no longer the thing that binds**:

    Swarm, Shaft      floor 40    60    70    80    90
    before (cap 2000)            1522  1997  1997  1997  1997
    after  (cap 6000)            1522  2205  2461  2465  2521

The ramp tops out around 2,500 because `SCALE_BY_AREA(58) x 40` is what the
*design* asks for, not because an array ran out. That is the right way round:
"the array cap is doing the balancing" was the honest complaint in §55 and it
no longer applies.

The cost is memory. `sizeof(Map)` went 27.9 MB -> 63.8 MB, whatever world size
is selected, because the arrays are sized at the ceiling. Save files go with
it.

### On generating the floor lazily

The follow-up suggestion -- sketch a skeleton on arrival and only build each
part when the player reaches it -- is the same idea taken further, and it is
right in general. Here, measured, it is not worth it *yet*:

- **The per-turn cost is already gone.** The activity radius took the
  recurring cost to zero; lazy generation would target the one-off.
- **What is left is a 689 ms pause when you take the stairs**, on a floor you
  will then spend an hour walking. That is 0.02% of its playtime.
- **The invariants are global.** "The stairs are reachable" and "the camp is
  reachable" are checked by flooding the finished floor. Under chunked
  generation there is no finished floor to flood until the player has been
  everywhere, so both guarantees would need redesigning -- and both exist
  because they were broken three times each.
- **The memory does not move.** `tiles[][]` is a fixed array sized at the
  ceiling; deferring *content* does not shrink it.

Where it would pay is a world larger than the Well, or one that persists
between visits. Neither exists yet. Recorded rather than built.

---

## 57. Where the time actually goes

With the activity radius in (§56), a proper profile across all four worlds
rather than another guess:

    world      gen      per turn: monsters  fov | frontier  path-to-stairs
    Shaft    33 ms                 0.00    0.00      0.00        0.08
    Halls     9 ms                 0.00    0.00      0.00        1.13
    Deeps    59 ms                 0.01    0.00      0.00        7.44
    Well    702 ms                 0.05    0.00      0.00       13.23

Everything recurring was already free **except one call**: walking toward a
known staircase, 13 ms every turn auto-explore takes, and the companions pay
it too.

### A* -- and getting it wrong first

`path_next_step()` has a target, so it should never have been breadth-first:
BFS floods every tile between you and the goal before arriving. Chebyshev
distance is admissible here precisely *because* movement is uniform-cost and
diagonal -- `max(|dx|,|dy|)` is the exact step count across open ground, so it
never overestimates.

The first version made it **slower**:

    Deeps  7.44 ms -> 25.16 ms      Well  13.23 ms -> 19.77 ms

The bug: `g_gscore` was written unconditionally on every relaxation, with no
stamp, so a score left over from the previous search read as a real one and a
*worse* route could overwrite a better one. That is A* degraded into
breadth-first with a heap bolted on -- strictly worse than the BFS it
replaced, which is exactly what the numbers said.

Stamping the g-scores alongside the closed set, and relaxing only on a genuine
improvement:

    Shaft  0.08 -> 0.04     Halls  1.13 -> 0.79
    Deeps  7.44 -> 1.99     Well  13.23 -> 1.57

**8.4x at the Well**, and the deeper the world the bigger the win, which is
the shape you want.

The frontier search stays breadth-first on purpose: it has no single target to
aim at, and it already costs nothing because it usually answers from the tile
you are standing on.

### The test that matters

Speed is the easy half. An inadmissible heuristic or a bungled relaxation
still *returns a path* -- just a longer one -- and nothing else in the game
would notice. So the new suite asserts the route is still **shortest**: on
open ground the step count must equal Chebyshev distance exactly, walls must
be routed without wandering, and a sealed room must be refused rather than
guessed at.

### What is left

Floor generation at the Well, 669 ms, is now the only cost above a
millisecond -- and it is once per floor, on a floor that takes an hour to
walk. It is Prim over ~9,300 rooms plus the nearest-room lookups. Worth doing
if anything ever makes it recurring; not worth it while it is a single hitch
on the staircase.

---

## 58. Winnable

Three constants, changed one at a time with the simulator between each. Fair
start is 60 gold; rich start is the 200M that §27 said the endgame needed.

    Normal, Shaft, 16 runs        fair: win / median       rich: win / median
      baseline                       0%  / 13                37%  /  97
      A  no quadratic attack         0%  / 14                75%  / 100
      A+B  escalation cap 25%        6%  / 18               100%  / 100
      A+B+C  upgrade step +15        6%  / 23               100%  / 100

**The sixth line is the first fair-start win this project has ever recorded.**

### Why these three, and what each did

The wall was never the economy. Survivability is

    hits survived  =  HP x (atk + def) / atk^2

so gold buys the numerator linearly while the difficulty curve sits squared in
the denominator. 200M was not a price; it was the square root of an attack
curve.

**A -- the `floor^2/140` term in monster attack.** It roughly doubled
floor-100 attack on its own, and because gear scales against the *square*, it
was responsible for something like four times the gear bill. Doubling the rich
win rate from a single deleted term is the measurement that says so.

**B -- escalation cap 100% -> 25%.** The largest single effect, and the one
that turns a fair start from impossible into merely hard. It is also the
change that makes levelling feel like an RPG rather than a treadmill: the
level term was raising every monster's attack permanently, whether you were
winning or dying. This is the slot DDA (§54) is built to fill properly.

**C -- `UPGRADE_STEP` 5 -> 15.** The weakest of the three on win rate (6% ->
6%) and the strongest on the *middle* of the run: median 18 -> 23, ninetieth
percentile 39 -> 52. Same defence for about a ninth of the gold, because the
cumulative price curve is cubic. Kept, because a run that reaches floor 23
instead of 18 is a better run even when neither wins.

### What is deliberately still true

**100% at 200M is not a balance claim.** 200M remains unearnable in a fair
run -- income is ~7,000 a run at the Shaft -- so the rich column measures
"does the combat maths hold at depth", not "is the game too easy". It holds.

**Hard, Swarm and Hardcore are unchanged**: 0% wins, medians 1-2. These three
constants are depth-curve fixes, and those modes die to floor-one density
before depth is a factor. Hard's best run did go 13 -> 25.

**No meta-progression was added.** No vault, no persistent bank shares, no
carried gear. The Bank stays one option among several rather than the game --
which was the explicit design constraint, and it is worth recording that the
winnability problem turned out not to need the thing that would have broken
it.

### The lesson

I spent several sessions treating this as an income problem and proposing
compounding fixes for it. It was a *demand* problem the whole time, and the
demand curve was three constants -- two of which were difficulty curves that
had never been questioned, and one a shop step size. The measurement that
settled it took four simulator runs.

---

## 59. Tiered materials, ore veins, and money that does not level you

A playtest observation that reframed the economy: *"you gain by exploring a
dungeon, that raises your level, and that raises the difficulty, forcing you
to raise even more money -- it's a poisonous cycle."*

Correct in shape. Measured, though, the coupling is looser than it looks:

    floor   kill gold (+XP)   floor gold   scrap   level-free share
        1               276          111     281        58%
       20             1,648          687   1,743        59%
       60             4,432        8,071  16,796        84%
       80             5,603        5,743  43,120        89%

Floor gold, scrap-by-walking, salvage, machines and now ore veins carry no
experience at all -- already most of a floor's money, and **89% of it by floor
80**, because scrap value doubles per biome while monster purses grow
linearly. The cycle exists; it was never the dominant term.

Three things narrowed it further.

### Tiered materials

Scrap splits along the biome seams it was already doubling across:

    scrap      floors 1-40     platinum   41-80     diamond   81+

And the smith stops taking coin alone: **+20 upward wants platinum, +50 upward
wants diamond**, and neither exists above floor 40. Farming a shallow floor
for ever now buys nothing past the twentieth rung -- the only way to afford
going deeper is to have already been deeper.

The material cost is printed on the smith's line rather than discovered by
being refused: a price you cannot see is a price you cannot plan for.

### Ore veins

`TILE_ORE` in the rock, worked with `g` for eight turns through the multi-turn
system (§46). Scattered rather than districted, because the material gate
means *every* depth band has to be able to supply its own tier -- leaving that
to a district that appears on half of floors would strand a run.

Note what this does to the cycle: the deep upgrade rungs are now paid for in
a currency that comes from **walking and mining**, not from killing.

### What it bought

    Normal, Shaft, fair start:   6% wins, median 23  ->  12% wins, median 28

### The recall charm did not work, and I am not pretending it did

Hard and Swarm now start with two recall charms -- the reasoning being that
they die on floor 1 with no way to convert what they have found. The harness
*does* use charms (it recalls below 30% health with no potions), and the
result was **no measurable change**: both still sit at median 1-2.

The likely reason is that escape was never the constraint. You recall with
sixty gold and nothing sellable, buy nothing, and come back to the same
crowd. The charms are kept because they are cheap and right in principle, but
they are unproven, not proven.

That points somewhere specific: the hard modes do not need an *exit*, they
need something to spend when they arrive.

---

## 60. The lizard track

Which is what the track is for.

Money won in town carries **no experience at all**, so it is the one income in
the game that does not make the game harder by being earned. That is the
entire design justification, and it is why this idea -- parked since early in
its life as a piece of colour -- turned out to be load-bearing.

Six runners, fixed odds, a stake you set. And because it is outside the cycle
it has to be bounded, or it becomes the only thing anyone does: **the book
pays over the odds for the first three races of a visit and under them
afterwards, and only going back down resets him.** Town income is gated on
*trips*, not on time.

Winnings run through `player_gain_gold`, so the Bank takes its share of these
like anything else.

**Skipping is the default.** `Enter` backs a runner and shows the result; `w`
backs it and watches them run. A forty-second animation you did not ask for
stops being a treat the third time, and the track is meant to be a quick way
to turn coin over.

## 61. The book was a sink

The track shipped in §60 with an odds ladder of 2..7 and weights of
`120/(odds+1)`. Those two facts together decide the whole economy, because
with `weight ∝ 1/(odds+1)` every runner is the same bet and the ladder's
return collapses to one number:

    return = 1 / SUM(1/(odds_j+1))

For 2..7 that sum is 1.218, so the track returned **0.82 gold per gold
staked**. The window advertised as generous was a money sink; the "sour"
ladder afterwards paid 0.63. It read as generous, the code said generous, and
it took coin off the player every time.

This is the same failure as every other one recorded here: a number that
looked like flavour was actually load-bearing arithmetic, and nothing checked
it. The fix states both ladders as tables with their measured return in the
comment, and `tests/invariants.c` now asserts the sign of the edge:

    GENEROUS  3,4,6,8,10,12  ->  1.165   (the player's edge)
    SOUR      2,3,4,5,6,7    ->  0.821   (the house's)

**And then the honest measurement.** Farming every generous race of every
town visit across a whole 100-floor run, 20,000 trials: 500 gold becomes a
mean of 815, and **39% of players end down**. The three-race cap is the
anti-farming guard and it also caps the income at nothing. A Swarm player,
who dies on floor 1-2, makes zero town visits and is paid exactly nothing.

The track is flavour and a small early bonus. It is not the hard-mode fix,
and no gambling mechanic can be: variance and the anti-farm cap pull in
opposite directions.

## 62. The kitchen

The rule is the design: **meat comes off a blade and nothing else.** Burn a
thing to death and there is nothing on it, put an arrow through it and the
cut is ruined, let a companion tear it apart and you have a mess. That single
restriction is what makes the pub a build decision rather than a passive
drop -- a pure caster walks past it entirely.

Enforced by a latch in `combat.c` set in exactly one place
(`player_attack_monster`, the only melee path) and consumed at the top of
`monster_take_damage`, so it cannot leak into the next source of damage. A
parameter would have been more honest, but all fourteen call sites would pass
`false` and the one that mattered would be no easier to find. The test asserts
the property directly: 59 monsters killed by spell yield 0 cuts, the same
population killed by blade yields 6.

**The purse bug.** The first cut scaled a patron's purse by the grade they
ordered and nothing else. Demand saturates once all four grades are on the
table (about floor 40), so a night at floor 100 paid *less* than a night at
floor 40 -- the differences past that point were pure noise on seven random
patrons. Scaling the purse linearly with `deepest_floor` fixed it, and the
test now pins monotonicity across depth, averaged over 400 rooms because one
sample is far too noisy to compare.

The resulting curve is deliberately front-loaded:

    depth    night pays    one upgrade rung    rungs per night
    1        846           140                 6.04
    20       5461          3540                1.54
    60       33745         22340               1.51
    100      53006         57140               0.93

Several rungs a night when you are poor and it matters; under one at the
bottom, where it should be a supplement and not a reason to come up.

**Bugs found by playing it**, none of which the unit tests could have caught:

- The night was charged at the door, so opening the pass to read the room and
  backing out cost the whole service.
- Walking out then showed an end-of-night summary that had to be dismissed
  again, and told you the room was empty when you were the one who left.
- The "no meat" and "night already spent" cases shared one screen and one
  message, sending players back down for meat they already had.
- Patron names were drawn independently -- seven draws from twelve names, so
  the same customer appeared at the pass twice on the first live night.
  Dealt without replacement now, and asserted over 200 rooms.
- The sidebar larder was a line per grade, which on a short terminal would
  silently push the weapon and armour lines off the bottom, because
  `mvprintw_clip` clips columns and explicitly ignores rows.

## 63. The black market, and why the numbers said not to build it

The proposal: sell player levels for gold. Structurally it is the right
shape -- it converts the thing that hurts you into the thing you need, and
escalation has a `level * 1.0%` term, so it should be negative feedback
replacing positive. It does not survive the arithmetic.

Selling 20 levels at floor 50, at the most generous price defensible (a full
upgrade rung per level):

    monsters:  escalation 25% -> 17%, attack 77.5 -> 72.5   (6.4% weaker)
    you:       HP 248 -> 88                                 (64.5% weaker)

About 10:1 against. Three reasons, all structural:

1. **The escalation cap eats the upside.** It is 25%, reached around level 17.
   Above it, selling a level changes difficulty by exactly zero. Worse, roughly
   half of escalation comes from floor *entries*, which are not yours to sell:
   at level 50 with 50 entries the sellable portion is **zero points**.
2. **Level value is linear, rung prices are quadratic.** Selling five levels at
   level 25 raises 21,140 gold, which buys one rung at floor 50.
3. **HP dominates survivability.** `hits = HP / damage_taken`, so trading flat
   HP for atk/def rungs trades the multiplier for the multiplicand.

The decisive measurement is the last one:

    level 10 -> 11 :  2.25 -> 2.39  levelling up HELPS
    level 40 -> 41 :  6.77 -> 6.98  levelling up HELPS

**Levelling up is currently good for you at every level.** The poisonous
cycle -- level up, floors get harder, need more gold -- is no longer in the
numbers. It was real when escalation was uncapped at 100%; capping it at 25%
(§58) de-fanged the level term as a side effect. A market has nothing to sell
you out of, which is why no price makes it work.

**And it is a printer.** Levels cost `20 + (L-1)*15` XP -- linear -- while any
sane price tracks rung cost, which is quadratic. So sell, drop to floor 1,
re-earn the cheap levels in perfect safety, sell again: x7.1 income at level 2
rising to x11.6 at level 30, risk-free, and *improving* as you go. One partial
defence exists by accident -- a floor-1 farmer only ever sees scrap, so gear
self-limits at +20 -- but gold also buys bank shares, training, companions and
spells, none of which are gated.

The guard that kills it: **pay for each level once, ever.** Track the highest
level ever sold; re-earning it pays nothing. That also makes the market small
-- on the order of 100k gold across a whole run, real money early and noise
against the millions a deep run needs.

**Built anyway, unguarded, by decision.** The analysis above is the case
against, it was put plainly, and the call was to ship it without a single one
of the guards: no once-per-level limit, no floor tied to `deepest_floor`, no
cooldown. Selling down and re-earning cheap levels somewhere safe is a
supported way to play. The reasoning is that the loop is *fun* -- farming
levels to sell is an activity the player wants to do -- and that a measured
x7-x11 is a design choice rather than a defect once it is chosen deliberately.
Guarding it for hardcore alone remains available later, which is why
`levels_sold` and `levels_sold_gold` are tracked from the start.

The one bound kept is level 1, and that is arithmetic rather than balance:
level 0 divides through the stat maths.

### What is pinned instead

Since the economics are deliberately open, the tests pin *correctness*:

- `player_sell_level()` is the exact inverse of the level-up inside
  `grant_xp()`, verified by climbing to every level from 2 to 40 and selling
  all the way back down to the starting stats. The defence-on-even-levels rule
  is the easy half to get wrong, and it is checked separately.
- The batch price quoted on screen equals what a level-by-level sale actually
  pays, across levels 5..70 and counts 1..20. The screen quotes a total before
  the transaction runs one level at a time; if those disagreed the player
  would be quoted a number they do not get paid.
- Selling everything lands on level 1 exactly and refuses to go further.
- The price is quadratic in level while re-earning is linear -- asserted, so
  that nobody later "fixes" the asymmetry without meaning to. That asymmetry
  *is* the loop.

### What the screen shows

Everything the trade does, before it happens: the target level (the number the
player actually steers by), the gold, the hit points, attack and defence given
up, experience forfeited, and the escalation before and after. That last line
is where the analysis surfaces in play -- at level 70 with 70 floors entered,
selling twenty levels moves escalation from +25% to +25%, and the screen says
so in as many words. The information is there; using it badly is allowed.

Quantity is chosen with all four arrows -- horizontal for one level, vertical
snapped to tens, `a` for everything. Snapped rather than stepped because the
player is steering by a round target ("I am 70, I want to be 50"), and
1 -> 11 -> 21 makes them do arithmetic to hit it. Vertical rather than
PgUp/PgDn because a laptop keyboard has neither without a modifier.

## 64. Auto-explore across floors

Auto-explore used to break out of its loop the moment it took a staircase,
handing control back with a comment reading "reached the next floor -- hand
back control". That is one keypress per floor, which is correct when you are
driving and wrong when you are not: the whole point of delegating the walk is
that you can stop watching, and a floor boundary is a doorway rather than a
decision.

It now carries on. The change is small but not a one-liner, because two of the
things checked once at the top of the run are properties of *the floor you are
standing on* rather than of the run, and have to be re-checked on arrival:
there must be a way down, and it must not be the Warden's floor (`MAX_FLOOR`),
which auto-explore has always refused to lead anyone into.

The floor-scoped bookkeeping has to be reset too, and this is the part that
would have been a silent bug. The stuck-detector compares a progress signature
against the previous step's, and that signature folds in position, hit points,
revealed tiles and the health of every living monster -- all of which change
wholesale on arrival. Leaving the old value in place would compare a fresh
floor against the last step of the previous one. `start_floor` likewise has to
advance, or every subsequent step reads as "changed floor" and breaks out.

All three live in one helper called from both places the floor can change (the
move that takes the stairs, and the fight step, which can also end somewhere
else), so they cannot drift apart.

**Everything that stopped it before still stops it**, on every floor: a
keypress, being below a quarter health with no healing item left, and sixty
consecutive steps with nothing observable changing.

Verified live: floor 1 to floor 2 on a single keypress in about eight seconds
with the first floor stripped of anything to loot or fight, then four and a
half minutes of continuous exploring on floor 2 without a stop. That run ended
on "too hurt to press on blindly" -- poison attrition against a character
carrying no potions, which is the pre-existing guard behaving correctly. It is
worth keeping in mind for an idle session: the thing most likely to end a long
unattended descent is running out of healing, not running out of floors.

## 65. The floors were grey

The complaint was that a floor called the Sunken Jungle Roots barely had any
jungle in it, and that most of what you walk through is plain white. Measured
before touching anything, on sixty Shaft floors:

    floors with no wild district at all : 40%
    floors with exactly one             : 31%
    average share of a floor inside one : 5.2%

So ninety-five per cent of an average floor was untinted rock, and the biome
in the floor's *name* never appeared in the floor at all -- nothing coloured
plain terrain by the floor's own biome, only by wild districts.

Two separate causes, and both had to be fixed or the other would not show.

### The wilds were deliberately rare, and the deliberation was wrong

The generator rolled 45% of floors to have no district, 35% exactly one. The
comment defending it read: "the ordinary built floor has to stay the baseline
or the exception stops reading as one." That reasoning is sound and the
outcome was not, because nobody measured what the baseline actually looked
like. At 5.2% coverage the baseline was not "an ordinary built floor" -- it
was grey corridor, and the exception was so rare it read as absence rather
than as contrast.

Every floor is somewhere now. The contrast comes from *how much* of a floor is
wild rather than from whether any of it is: a sparse floor still has a handful
of places, an overgrown one is mostly wilderness with buildings in the gaps.

### The district count scaled but the district size did not

Raising the count alone measured worse on big worlds, and in the direction
nobody would have guessed:

    Shaft  33% wild     Halls  34%     Deeps  27%     Well  13%

The count rose with area while each district stayed a fixed ~20x12 rectangle,
so a Well floor wanted 896 of them, could only place 278 without overlapping
inside any sane try budget, and came out the greyest world of the four. The
biggest map was the emptiest, which is precisely backwards.

Linear size now grows as the fourth root of the area multiple and the count as
the square root. Their product is the covered share, so it comes out constant
on every world, while both the number of places and their individual size
still rise with the map:

    Shaft  34%,  8 districts      Deeps  36%, 40 districts
    Halls  34%, 20 districts      Well   38%, 80 districts

Generation got *faster* at the Well as a side effect -- 970 ms down to 405 ms,
against 669 ms before any of this -- because the old scaling spent most of its
time on placement attempts that could never succeed.

`MAX_DISTRICTS` is now a flat 220 rather than area-scaled, for the same
reason: with size scaling too, the number needed for a given coverage is
roughly world-independent, and scaling it by area only handed the overgrown
roll an unreachable target to burn eighty thousand attempts on.

### The floor's own biome now colours the rock

A second, dimmer wash under everything, keyed to the floor's biome, so a
jungle floor is green between its districts instead of white. Districts paint
over it in the brighter tone, so walking into one still reads as arriving
somewhere. Applied in the close view and the whole-floor map alike -- the map
is where it matters most, being the only place you see the *shape* of a
district rather than the few tiles in front of you. Features and hazards keep
their own colours in both: a staircase must not become scenery.

### The test was pinning the wrong end

`tests/invariants.c` asserted that at least a quarter of floors stay bare.
That assertion encoded the same unmeasured assumption as the generator, and it
failed the moment the generator was fixed. It now asserts the opposite -- no
floor is bare rock with a biome's name over it -- which is the property that
was actually wanted all along.

### The wash was the same mistake in a different colour

The fix above shipped as a *wash*: every neutral tile on the floor took the
biome's colour. It passed every test, and looking at it settled it in one
sentence -- "now everything is green". A grey floor had become a green floor.
One flat colour swapped for another is not variety, and the tests could not
have caught it because they measured whether colour was *present*, which it
emphatically was.

The right description came back as a picture rather than a spec: a jungle
grown over a city is not green, it is stone with green through it, and the
stone showing between the growth is what makes the growth read as growth.

So both layers speckle against a stable per-tile value instead of covering:

    bare stone   73-79% of neutral terrain, every biome
    biome        7-9%
    district     11-24%

The speckle is clumped rather than salt-and-pepper -- a coarse value shared
by a 3x2 block, blended with per-tile jitter for ragged edges. An evenly
random per-tile value looks like television static; growth grows in patches.
The value is stable per tile and per floor, so the ground does not shimmer as
the camera moves.

The whole-floor map deliberately does *not* speckle and does *not* take the
biome wash. One minimap cell is a block of many tiles, so there is nothing to
speckle, and washing every block in the biome's colour reproduces the exact
problem at map scale. Districts alone are coloured there, against neutral
rock, because their *shape* is the thing worth seeing on a map.

`terrain_wash_at()` is exported so `tests/invariants.c` can assert the
proportion on all five biomes -- most of a floor stays stone, and something
grows on every one of them. Both halves matter: the first assertion is the one
that would have caught the wash, the second the one that would have caught the
original grey.

## 66. A pass over a played-in build

Nine things, from a session of actually playing it. Four were mine, and the
two most expensive were both the same shape: a multiplier applied where it
had no business being.

### The track printed money

Reported as "I just pressed enter for 3-5 minutes, 2,805 races and an easy
million", with the correct diagnosis attached: the bank share.

`player_gain_gold()` applies the bank multiplier, the gold-find bonus and the
altar's doubling. That is right for income. The track's payout is *stake plus
winnings* -- so multiplying it multiplied the stake, money the player already
had. A book paying 0.82 per gold staked, built to bleed anyone who stood
there all day, paid 1.64 at two shares and printed without bound.

Gambling returns are credited flat now, through `player_credit_gold()`, which
clamps and nothing else. Verified across bank shares x1 to x8: identical
results, 100,000 gold down to 73,600 over 3,000 races. Any future mechanic
that hands back a stake belongs on the same function.

### The black market was a cheat code

Also reported from play, and the arithmetic backed it up completely: 88 kills
on the first floor reaches level 38, which sold for 10,020 gold -- sixteen
times what those same kills paid in coin. Grinding to level 70 on floor one
and cashing out paid **753,060 gold**, which buys most of the shop.

The price was keyed to *level*. Levels are grindable anywhere and cost the
same experience wherever they are earned; depth is the thing that cannot be
faked. Keyed to `deepest_floor` instead, the same floor-one farm pays 4,830 --
a 156-fold cut -- while somebody who genuinely reached floor 80 still gets
1.3M for it, and pays 552 max HP to collect.

Alongside it, per the same report, monster rewards now carry a quadratic term
in depth. Flat-ish linear growth had a floor-100 kill paying under ten times a
floor-1 kill, which made the shallows a perfectly good place to farm anything
counted per kill. Winnability on Normal went 12% -> 18% as a side effect;
the hard modes are unchanged at 0%.

### The heroes danced

Reported as heroes "moving back and forward, or up and down", and as the game
crawling with a full party. Measured by counting moves that returned an actor
to the square it occupied two turns earlier -- and the first version of that
metric was wrong, because it counted *standing still* as a dance step, which
inflated exactly the behaviour being added.

With the metric fixed, two independent causes:

- Scouting fell through to a random drift whenever the way on was blocked.
  With five hires competing for one corridor that fired constantly.
- Two frontiers the same distance apart swap places as a hire moves between
  them, so it walked one step toward each in turn, forever.

Waiting when blocked, and refusing to step straight back onto the square just
left, together take a five-hire party from **25% of moves being dance steps to
0%** (three hires: 15% -> 5%), and cut total hero moves by 38%.

### The crawl was the pause, not the work

Auto-explore was capped at sixteen steps a second while walking by hand was as
fast as a key could be pressed. Profiling put the work behind a step at 1.8ms
on a Shaft floor and 8.3ms on a Well floor with five hires and eighty thousand
monsters -- against a deliberate `napms(60)`. The sleep was 85% of the wall
clock.

Now 24ms, chosen by the person watching it rather than by measurement, which
is the right way to pick the speed of something that exists to be watched. End
to end that is 22 steps a second against about 12 -- the remaining ~20ms is
the draw, which is now the dominant cost and the place to look next.

### Gold stuck in the walls

`$` was drawn for three different things: dropped gold, the toll feature, and
`TILE_ORE`. Ore is terrain cut into rock, so it keeps being drawn from memory
once seen -- reading exactly as coins embedded in a wall that can never be
reached. Only 1 item in 2,709 was genuinely mis-placed; the bug was the glyph.
Ore is `\` now.

### Smaller things

- A hire released now hands back **half** the fee. At 1,000 / 10,000 /
  100,000 gold a slot, changing your mind cost more than most mistakes in the
  game. A hero who died in your service still refunds nothing -- there is
  nobody to pay off.
- Crossing into a wild district says so. A third of a floor is wild now, and
  districts change the rules you are playing by; walking into one silently
  meant learning that by dying in it. Tracked by district *index*, so two bogs
  on one floor are two arrivals.
- A gold rush turns the border gold and stamps the frame, the same treatment
  an overrun floor already had. Overrun wins when both are true: being killed
  matters more than being paid.
- The party list moved above the gear. Five hires are five more actors with
  their own hit points, and they were listed last -- so on a short window they
  were the first thing pushed off the panel. What you own is static between
  shops; who is still standing changes every turn.
- Race stakes take `+`/`-` for ten times the stake. Reaching a 5,000 bet fifty
  at a time is ninety-nine keypresses. Not Up/Down -- those pick the runner.

### Two tests had to be inverted

`releasing refunds nothing` and, earlier in the session, `plenty of floors are
still ordinary built floors`. Both encoded a design decision as an invariant,
and both failed the moment the decision changed. That is the tests working:
the failure is the notification that a deliberate choice is being reversed.
Neither was a bug in the test.

## 67. Cutting the heroes loose

Six more from play. Two were measurement problems as much as code problems.

### The blue tile you could stand on for minutes

An aqueduct current pushes whatever stands in it, every turn. Auto-explore
paths across a channel, gets carried off its route, re-paths, steps back in,
and is carried off again. The stuck detector never fires, because its progress
signature folds in *position* -- and the position keeps changing. From the
outside that is progress; from the chair it is minutes of being shoved.

Delegated walking -- the player on auto-explore, and hires -- now routes around
currents via `is_walkable_delegated()`, which is `is_walkable_monster()` plus
"not a current". Monsters keep their own predicate and are still swept, since
that is the district's mechanic. The hazard-allowing second pass still crosses
a channel when there is genuinely no other way, and being carried is then the
fast way through rather than a trap.

### "The more they view, the slower it gets"

Reported as the game crawling in proportion to how much is on screen. Profiling
the logic said no: frontier search does grow with revealed area, 0.001ms to
0.038ms, but that is nothing, and companions actually get *cheaper* as a floor
opens up. Total logic on a Shaft floor is ~1.2ms.

The cause was `erase()` at the top of every game frame. It blanks the whole
virtual screen, so the `refresh()` that follows finds every cell different from
blank and repaints the entire terminal -- a full screen of escape sequences per
step. ncurses already diffs against the previous frame; `erase()` is the one
thing that defeats it.

The map viewport is safe to leave alone -- the draw writes every cell of it
unconditionally. The sidebar and log are not (both draw a variable number of
lines and stop), so those two regions are blanked explicitly. A resize still
does a full erase, once, because the viewport has moved and the old layout is
debris.

    200x55 window: 28.1 -> 31.6 steps/sec, now matching an 80x24 window

The window-size penalty is gone. Note the measurement environment understates
the win: tmux writes to a buffer, while a real terminal paints pixels.

### The heroes were an escort, and that was the bug

The standing request was to stop them babysitting. The leash -- "past this
distance from the player, turn round and walk home" -- turned five hired
adventurers into five people shuffling in the player's corridor, which is
where most of the dancing came from and is a strange thing to buy for a
hundred thousand gold.

It is gone. `heading_back` with it. Personality is now pure aggression: how
far a hire ranges looking for trouble (`chase`), how far it notices (`sight`),
and how much damage it takes before it stops looking. Nobody has any idea
where the player is.

Removing the leash made the dancing *worse* at first -- 25% to 39% at five
hires -- and the reason is instructive. The anti-backtrack rule added earlier
only guarded the frontier branch. With the leash gone, most movement became
chase-driven through `step_toward()`, which was unguarded, so target-flipping
between two equidistant monsters had free rein. Guarding `step_toward` too --
as a preference, tried on a second pass, not a prohibition, because a corridor
may genuinely require going back -- closes it:

    dance steps    1 hire 0%    3 hires 0%    5 hires 0%
    and they still explore: 1,020 distinct tiles covered by five hires,
    ranging up to 138 tiles from the player (the old Bold leash capped it at 40)

### Smaller things

- **The Elixir of Vigor.** Reported as losing its bonus. Measured: it does
  not -- ten elixirs at level 20, sold all the way down to level 1, and all
  50 HP were still there. Levelling and level-selling both preserve it,
  because the level contribution is added and removed symmetrically. The one
  thing that reverts it is dying back to an Inn snapshot taken before it was
  drunk, which is the snapshot working as designed. Price 50 -> 5,000, and
  the amount +5 -> +50 with it: permanent max HP at fifty gold was the
  cheapest stat in the game by two orders of magnitude, and raising the price
  alone would have left dead stock on the shelf.
- **The inventory stays open.** It closed on the first use, so drinking five
  potions meant opening the pack five times. It now returns a *count* rather
  than a flag, and the caller resolves one turn per item -- staying on the
  screen must not turn five draughts into one turn of free healing.
- **The larder scales with level.** A flat 99 per grade meant a level-2
  character hauled as much as a level-60 one. Twelve plus two a level now,
  ceiling 99.

### The bill for removing erase()

Reported immediately: walking from town into the temple left the town's
buildings on screen everywhere the new floor was still dark -- "only what is
inside the fog of war is correct".

Two places assumed a blank screen underneath, and both were invisible while
every frame began with `erase()`:

- `paint_cell()` returned early on an unseen cell. Unseen is not "nothing to
  draw", it is "draw nothing *here*" -- it has to actively blank.
- The paint loop skipped world coordinates outside the map. Town is smaller
  than the viewport, so those squares are real screen cells with the previous
  frame still in them.

The rule the optimisation depends on, now written where it is enforced: the
map viewport owns every cell it covers, seen or not, in bounds or not.

This is the cost of the trade. Dropping a blanket clear buys back a full
screen of escape sequences per step, and in exchange every region of the
screen has to take responsibility for its own background. Two regions did not,
and the failure showed up as one screen's contents leaking into the next. Worth
it -- but it is the kind of change whose bugs appear somewhere other than where
the edit was.
## 68. The harness was the bug, three times over

A per-tick position trace was requested for exactly the right reason: summary
metrics kept lying, and "are they moving" should be a question about data.
`tools/trace.c` writes one CSV row per actor per tick -- tick, who, x, y, hp,
alive -- and `tools/trace_report.py` reduces it to idle share, longest stall,
two-cycles, distinct tiles and net drift.

It paid for itself immediately, and its first three findings were all bugs in
itself.

**Run one** showed two hires sitting on one square for 775 consecutive ticks
and a third running a clean ten-tile loop -- while the two-cycle detector used
in the previous section reported 0%. Both facts were true of the trace; only
the second was true of the game.

The cause was the simulated turn not matching the real one. `refresh_vision()`
lights the player's field of view *and* calls `companions_reveal()`. The
harness did only the first, so hires walked to a frontier that could never be
revealed and thrashed against it forever. The freezes were the tool's.

**Then** the trace tool was moved into `tools/` and built with the project's
real warning flags, which immediately caught a call to
`process_monster_turns(&p, &m, &st)` -- the signature is
`(Map*, Player*, bool*)`. The scratch build had used `-w`, so three arguments
of the wrong type had been passed silently for the whole investigation. One
hire's 145 two-cycles were that corruption; with it fixed, the figure is 1.

Only after both fixes does the data mean anything:

    actor    idle%   longest stall   2-cycles   distinct tiles
    player      3%              24         15              726
    hires    4-14%           10-31       1-19          538-656

Healthy. Which raises the obvious question about the change made while the
tool was still lying -- and the answer, measured properly this time, is that
it earns its keep anyway:

    without sticky scout targets:  30-62% idle, stalls of 118-489 ticks
    with them:                      4-14% idle, stalls of 10-31 ticks

So the standing complaint that hires get stuck was real; it was arrived at
through a broken measurement and confirmed by a fixed one. The one line added
in response to the phantom deadlock -- releasing the backtrack memory on a
wait -- measured identically with and without, and was removed.

The rule this earns, written at the top of `tools/trace.c`: **a harness that
simulates a turn must simulate the whole turn.** Anything the real loop does
each tick that the harness omits does not show up as a missing feature, it
shows up as a plausible bug somewhere else entirely.

### The elixir, tested rather than argued

Reported as not working: bought a few, sold levels, ended at base HP. Driven
live through the whole flow rather than reasoned about:

    level 20, three elixirs drunk   223 HP  (208 base + 15)
    sell one level                  215 HP  (200 base + 15)
    sell every level to 1            71 HP  ( 56 base + 15)

The bonus survives the entire sale. What does not survive is buying without
drinking -- and the shop line said `+5 max HP (permanent)` next to a price,
which reads as a purchase that raises max HP. It is a bottle; it does nothing
until it comes out of the pack. The label now says so. Amount back to +5 at
5,000 gold, as asked.

## 69. The market stopped asking where you had been

`level_sale_price` was keyed to `deepest_floor`. The intent was to stop
level-1 farming; the effect was to make it strictly better. Touch floor 81
once, retreat to floor 3, and 93 harmless monsters paid 18,870 a level --
same lap, same minutes, thirty-three times the rate of a player who stayed
deep and earned it.

It pays a flat gold per point of experience now, exactly what the level cost
to earn (`MARKET_GOLD_PER_XP`, items.c). Depth still decides the rate, but
through the experience curve rather than a multiplier: a lap of floor 93
yields 27,041 xp against floor 3's 655, so deep farming already earns levels
forty-seven times faster for the same walking. The curve was doing the job
the multiplier was hired for.

The tests were inverted to match -- they used to pin "a deeper player is
worth more", they now pin "how deep you once went prices nothing" across
depths 1..100, and "a level sells for what it cost" across levels 2..80.

## 70. Two keypresses were a rest day

`races_this_visit` and `kitchen_nights` reset on entering the temple *and* on
climbing up, both free. Step through the door and back out: the track's
generous races and the pub's one service night returned, forever. Those caps
are the only guard those two systems have.

They now reset on one event only -- going **down** -- which costs a floor's
worth of walking to find a staircase. main.c has a single reset site now
instead of three.

## 71. The harness modelled one player and called it the game

Every winnability figure this project has quoted came from an agent that
takes the stairs the moment it sees them. A comment in `tools/simulate.c`
recorded that grinding had been *deliberately deleted* from the policy
because it "measures grinding, not whether the game can be finished" -- which
answered *can this be finished quickly* and quietly stopped answering *can
this be finished*.

There are four styles now, differing in exactly one decision (`ready_to_descend`)
so any difference in results is caused by that and nothing else:

| style | rule |
|---|---|
| rush | stairs on sight -- the original policy, preserved as the floor |
| steady | explore the floor out, then descend |
| farm | stay until ~4x floor, or until the floor stops paying |
| overkill | stay until ~7x floor, and grind it dry first |

Building it surfaced three bugs in the harness, none in the game:

**The floor loop ended when the player's feet touched the staircase.** That
silently made every style a rusher: an explorer whose route crossed the
stairs got pulled down a floor it had not finished with. The gate decides
now; the tile is only where the decision is acted on.

**The first farm target was below the free curve.** A rusher already reaches
about 2.5x the floor number unaided (floor 20 at level 51, floor 38 at 102).
The target asked for `4 + 2xfloor`. The farmer met it on arrival and behaved
identically to a rusher -- the harness would have reported "farming does
nothing" with a straight face. Measure the baseline before setting a target
above it.

**The turn budget was deciding everything.** Once the targets were raised
they became unreachable at depth, so every floor ran to its budget and the
reported numbers were produced by a constant. The rule is diminishing
returns now -- stay while the floor is still paying, leave when experience
per turn drops below a share of this floor's best rate -- and each style
reports *why* its floors ended. Budget now ends 0% of them.

## 72. Time buys the win, and the price is legible

Normal, after the shopping fix in §73:

| style | win | turns/run | health between floors |
|---|---|---|---|
| rush | 13% | 9,825 | -- |
| farm | 50% | 83,377 | 87% |
| overkill | 25% | 138,314 | 92% |

Farming buys a 4x better win rate for 8.5x the time and arrives comfortable.
That is the FF7 argument working: time is the constraint, and spending it
converts into power. The wrinkle worth designing against is that overkill
costs 66% more turns than farm for half the win rate.

"Health between floors" exists because win rate cannot tell a squeaker from
an obliteration. A run that clears every floor at 90% never had a fight it
could lose; one that arrives at 15% won the same number of times and played a
different game.

**These numbers rest on 4-20 run samples and should not be trusted yet.** A
rush control drifted 13% -> 0% between two small samples, which is what tiny
samples do, but a conclusion drawn over a drifting control is not a
conclusion. A properly sized run across all four styles and all four
difficulties is the next real measurement.

## 73. Every per-style constant is a place to author your own findings

Overkill died on floor 21 with 0% wins, and the write-up nearly said "the
game punishes over-farming". It did not. The agent carried **two recall
charms**, a flat number tuned for a rusher who is on a floor for a few
hundred turns -- while sitting on one floor for three thousand turns meeting
every respawn that arrived. With charms it wins 25% and reaches floor 100.

The first fix replaced one arbitrary constant with four (2/6/10 per style),
which is the same mistake wearing a hat. Charms stack without limit --
`give_consumable` increments by name, `INV_CAP` bounds distinct stacks, not
quantity -- and there is a shop at every waygate, so nothing in the game caps
them. A share of the purse goes on charms each visit now, and how many a
style carries falls out of how rich its play made it.

Rule for this file: if a style differs because of a number chosen per style,
that difference is authored, not measured.

## 74. The pack does not fill

`INV_CAP` was 20, but the real ceiling was **26** -- the inventory screen
selected with `ch - 'a'`, so anything past the 26th entry was unreachable
whatever the cap said. Raising one without the other moves the wall instead
of removing it.

Cap is 120, the screen pages (20 a page, left/right to turn, letters address
the page), `SAVE_VERSION` 37 -> 38. Quantities never had a limit, so the cap
only ever meant "you may not pick up a *kind* of thing you do not already
carry", which is about the least interesting refusal a game can make.

## 75. --silentrun: the real game, with nobody at the keyboard

```
./bin/aether-descent --silentrun [--log PATH] [--keys N] [--seed N]
```

`tools/simulate.c` answers "is this balanced" but drives combat and mapgen
directly -- it never opens a shop, an inventory or a save prompt, so every
menu in the game is invisible to it. This runs the actual main.c loop: a
curses screen written to /dev/null (frames still render, because a crash in
drawing code is exactly what this should catch), input from a policy, and a
log of keystrokes, game messages and periodic state snapshots.

Three things it taught while being built:

**Input has two doors.** `getch` covers every screen; character creation
reads the name through `getnstr`. Missing the second stalled the first run at
the name prompt -- 3,000 keystrokes that never entered the game. Both are
hooked in common.h.

**The policy needs its own RNG.** Sharing `rand()` would mean keypresses
consumed draws from the stream the dungeon is generated from, so replaying a
seed would give a different dungeon depending on how many keys were pressed.
Reproducing a crashing run is the entire point.

**A key budget is decorative if the policy can quit.** `q` sat in the pool at
weight 2; over enough keys a quit is near-certain, and every run ended at
about 26,000 keys regardless of `--keys` -- including a user run with a
400,000,000 budget that stopped at 14,233. The budget was measuring the
expected time for a random walk to press `q`. It is out of the pool; the
wind-down still presses it once, at the end. `autoplay_end` is on `atexit`
so a log gets its footer however the process dies.

### Known limitation: it does not play the game

60,000 keys, deepest floor **1**, every snapshot reading `floor 0, lvl 1,
gold 60, turn 0`. It wanders town forever. Entering the dungeon needs the
player to walk onto a specific tile and a random walk essentially never does,
so everything below floor 1 is untested by it. It is a menu-and-town fuzzer
today, which has value, but the state snapshots exist precisely so the log
says this out loud instead of having to be inferred from which shop refusals
appear in it.

The fix is a policy that knows which screen it is on and paths to the stairs
deliberately, reusing `path_next_step` from the simulator.

## 76. Score for the session: five instrument bugs, zero game bugs

The staircase ending floors, the farm target below the free curve, the budget
deciding everything, the flat charm cap, and a fuzzer that quit on itself.
Every one of them looked like a finding about the game before it was traced.
Add the three from §68 and the pattern is not a coincidence.

The working rule: when a harness reports something surprising about the game,
suspect the harness first, and make it report *why* it concluded what it did
(end reasons, state snapshots, controls that must not move) so the next
surprise arrives with its own diagnosis attached.

## 77. The Gladiator School deleted the run it was paid to improve

The first confirmed **game** bug in this file, against the eight instrument
bugs of §68 and §76.

`train_attribute` bought a point and then called `compute_derived` in place.
`compute_derived` *assigns* the sheet from the attribute table -- and a
character who has been played is not the sum of their attributes any more.
Nineteen levels are +19 attack, +9 defence and +152 max HP living in
`base_atk`, `base_def` and `maxhp` and nowhere else. Shrines add to `base_atk`
directly. The bow is an assignment to `ranged_type` that `compute_derived`
clears on its way past.

Measured on a level-20 character carrying a shortbow, buying one point of
Might:

```
before: lvl 20 atk 28 def 16 maxhp 238 ranged shortbow
after : lvl 20 atk 10 def  6 maxhp  86 ranged NONE
```

Twenty levels and the weapon, for 2,100 gold. The old code did have a restore
list -- max HP upgrades, current HP, gear bonuses, bought aether -- which is
why the failure is not total. The list had four entries and needed about
eight, and there was no way to know that from reading it.

It is applied as a **difference** now. Two sheets are derived from the
attribute table alone, one before the point and one after, and every stat
moves by the gap between them. For a stat nothing else writes, moving by the
gap is identical to assigning it; for one that levels or shrines or the smith
also feed, only the attribute's own share moves. Nothing has to be listed and
kept in step, which is the property the restore list did not have.

The existing test passed throughout, because it tested a level-1 character
with no bow -- the one character for whom the two are the same thing. It tests
a level-20 one with a bow and a shrine bonus now, and pins the general rule:
a point moves the stat by that attribute's share and by nothing else.

**The rule this suggests:** anything that reaches a stat through the attribute
table has to be tested on a character who has been somewhere. A level-1
character is the fixed point of every one of these functions.

## 78. A fifth playstyle, and what it is allowed to differ by

PRESTIGE, modelling the loop the project owner actually plays: farm, recall
home nearly dead on whatever floor, sell **every** level at the black market,
spend the gold on shares, attribute points and hired heroes, and start again
at floor 1.

What makes it a loop rather than a slow suicide is which side of the reset
each thing falls on. Levels are the only thing given up. Shares, attribute
points, heroes, gear and smith upgrades all survive, because they live on the
Player and the Player does not restart. The cycle converts something
impermanent into something that is not.

It reads FARM's descent gate, FARM's patience, FARM's turn budget and FARM's
consumable reserve -- the same `case` labels, not values that happen to be
equal -- so the only thing separating the two rows is the cash-out. §73's rule
is enforced structurally rather than by intention: there is no per-style
number to drift.

**The control that proves it.** Recall does not exist in Hardcore -- main.c
refuses the key -- so the cycle can never fire there and PRESTIGE degenerates
into FARM exactly. At 100 runs the two Hardcore rows come out identical digit
for digit, down to `worst crowd 438 (fl 2)` and `deaths on floor 1: 63`. A
run of the matrix where they diverge is a run where prestige code has leaked
into the shared policy, and it costs nothing to keep asking.

Two things push back on the loop without being told to, and both are the
game's own rules:

- the Inn takes a fifth of the purse, so cashing out rich costs more;
- escalation reads the player's **level** as well as their floor entries, so
  selling down to 1 makes the next descent's monsters *weaker* -- while the
  floor-entries term keeps climbing regardless.

## 79. Three more instrument bugs, found by building the fifth style

**Hardcore could recall.** `consume_recall_charm` does not know about
difficulty; the refusal lives in main.c's input handler. The harness called
the function directly, so every Hardcore number this project has ever quoted
was measured against an escape hatch the mode does not have. Gated now, and
it is what makes the §78 control possible.

**Winning runs reported nought gold.** The win path returned early without
setting `r.gold_earned`, so income was averaged with a zero for every run that
killed the boss -- the measurement most likely to be quoted as "the economy is
short", made worse in exact proportion to how often the mode was won.

**The prestige trigger fired for nobody.** The shared recall branch is "hurt
*and* out of draughts", which a farmer carrying forty healing draughts
essentially never satisfies: the first version of PRESTIGE ran **zero** laps
in three runs, which is FARM wearing a different name. It goes home at the
same threshold everybody else goes home at -- thirty percent, the game's own
charm -- without drinking its way back into the fight first. The number is not
new; the dropped condition is the whole change.

None of the three announced itself. The reason the first was caught is that
the Hardcore control was designed before the numbers were read.

## 80. Averages of small counts are a way of printing zero

"prestige laps per run: 0" and "the cycle never fired" are the same sentence
in integer arithmetic and completely different findings. The first sample that
worked reported 0 laps while having run one. Anything that can legitimately
average below one prints to two places now, and recalls are on every row
rather than prestige's -- until they were printed there was no way to tell a
style that never needed to go home from one whose trigger could not fire.

## 81. The fuzzer was not refusing to go down. It could not find the door.

§75 recorded 60,000 keys and a deepest floor of 1, and read it as a policy
that preferred the town. It was not a preference. Entering the temple means
walking onto one specific tile in a 140x80 plaza, and a random walk does not
land on it -- so mapgen, monsters, combat, descent, death and every screen
below the plaza had never been reached by this mode at all.

The policy is two policies now, split along the line the evidence drew.
*Navigation* is deliberate: it knows whether it is in the town or the temple,
and paths with the same `path_next_step` the simulator and the hired
companions use. *Everything else* is the fuzzer, unchanged. The split is
structural, not a matter of discipline -- main.c arms a scene immediately
before its own `getch()`, and the first key served consumes it, so every
nested screen falls back to random keys without being told to, including
screens nobody has written yet.

In town it tours every service in turn and heads for the temple door last,
which fuzzes each shop *and* guarantees the descent. In the temple it walks to
the staircase when the staircase is known, hands the floor to auto-explore
when it is not, and answers the floor-choice prompt with "continue from the
deepest" -- the fuzzer answered that prompt with Esc, which the prompt reads
as floor 1, so no run could ever accumulate depth however long it ran.

**Getting from floor 1 to floor 3 took five more instrument bugs, each hidden
behind the one in front of it.** In the order they had to be peeled:

1. **Diagonals walked into walls.** The searches step eight ways; only four
   have arrow keys. Sending the horizontal half of a diagonal looks
   equivalent and is not -- a diagonal through a corner has both halves in the
   wall, main.c refuses an unwalkable move *without spending a turn*, and the
   policy pressed the same refused key forever. Measured as a run pinned at
   (144,72), advancing forty turns across five and a half thousand keys. The
   game has `y`/`u`/`b`/`n`; it uses them.

2. **A quarter of the key pool did nothing.** `o` was labelled "auto-explore"
   at the heaviest weight in the table and auto-explore is `x`. `z` was
   "cast"; the spell menu is `m`. `>` and `<` were "descend" and "ascend" and
   there are no such keys -- stairs are taken by walking onto them. Fifty-three
   points of weight, the largest single share, went on keys the game discards,
   and nothing in the log said so, because a key the game ignores looks
   exactly like a key it handled quietly.

3. **Auto-explore stopped on its first step, always.** It polls
   `getch()` in `nodelay` mode once per step to let a watching player call the
   walk off. The self-play hook only knows how to answer "give me a key", so
   it answered every poll with a keypress. The one thing in the game that can
   cross a floor and take the stairs by itself had never run under this mode.
   A non-blocking peek is a different question, and has its own entry point
   now -- which answers ERR, and one time in four hundred a key, because
   "player interrupts a delegated walk" is a branch worth walking into.

4. **The run spent three quarters of its wall clock asleep.** Two animation
   pauses did not honour the headless flag: the red border flash, 70ms on
   every hit taken, and the lizard race, 45ms a frame. Measured at 26% CPU --
   four minutes of wall for one minute of work. Both went through the flag by
   being drawn *earlier in render.c than the flag was declared*, which is a
   good argument for the flag being declared at the top. Every pause in the
   file goes through one guarded helper now.

5. **The fuzzer played in the project owner's save slot.** It quits through
   the game's own save prompt and dies two hundred times a run, and dying
   writes a highscore, a class record and an Inn snapshot and deletes the
   suspended run. Left going overnight against `$HOME` it will quietly eat a
   game somebody was in the middle of. `--silentrun` sets `AETHER_STATE_DIR`
   to its own directory now unless one is set deliberately.

And a sixth, once it was working: auto-explore was capped at three tries per
floor, a number picked to stop a policy whose only idea was `x` from pressing
it forever. Auto-explore does the work of hundreds of hand-walked steps, so
three-then-never meant a soak spent **189,243 keys** walking the frontier by
hand against about 339 invocations of the thing that actually crosses floors.
It is gated on the turn counter now -- an auto-explore that gave up leaves it
where it was, one that walked half the floor does not -- so only a genuine
refusal falls through.

After all six, three soak runs:

| seed | the game it drew | descents | deaths | deepest |
|---|---|---|---|---|
| 4242 | Normal, The Halls | 62 | 61 | 3 |
| 7 | Normal, The Deeps | 7 | 10 | 3 |
| 31337 | Swarm, The Well | 0 | 771 | 1 |

Against a previous best, over every seed ever run, of **floor 1 and no
descents at all**.

**What it is still not.** It plays badly -- it does not shop with intent, and
survival, not navigation, is now what bounds it. Note the third row: 771
deaths without a single descent, on a mode where this project's *balance*
harness has a median depth of 1 across all five styles. That is the mode
answering, not the policy failing, and telling those apart is exactly why the
log names the character's difficulty and world now. The navigation lines say
*why* each step was taken -- "stairs", "explore (stairs not found)",
"auto-explore", "nowhere" -- because "never descended" means opposite things
depending on which of them dominates. It was `explore (stairs not found)`,
7,840 times, which is what pointed at auto-explore.

## 82. Two more real bugs, both surfaced by the fuzzer playing badly

**Every death in the game reported floor 0.** `STATE_GAMEOVER` read
`player.floor` for the "Whatever happened on floor %d" line *after*
`load_inn_snapshot` had replaced the whole Player -- and a snapshot's floor is
always 0, because snapshots are taken in a bed in town. Nobody notices this
playing normally, because you know which floor you died on. A run that died
216 times and printed the same wrong sentence 216 times is harder to miss.

**The Gladiator School bug in §77** is also, in effect, a fuzzer find: the
prestige style is the first thing in this project that ever bought an
attribute point outside a hand-played session.

## 83. The matrix, mostly at a hundred runs a cell

Five styles, four difficulties, `./bin/simulate 100 <mode> 60 0 -1 1 0 <style> 6`.
Cells with n=30 are 30-run samples -- Normal steady, overkill and prestige,
and Hard prestige. Those four cost between one and two hours of wall clock
apiece at 100 runs and had not finished when this was written. Sample size is
quoted per cell and never averaged over, because averaging over it is the
failure §72 recorded.

**Normal.** Death counts are out of that cell's own sample, so read the two
death columns against the `n` beside them and not against each other.

| style | n | win | median | turns/run | died in floors 1-9 | died below 10 |
|---|---|---|---|---|---|---|
| rush | 100 | 14% | 9 | 27,054 | 52 | 32 |
| steady | 30 | 30% | 2 | 112,715 | 16 | 2 |
| farm | 100 | 35% | 4 | 67,265 | 52 | 12 |
| overkill | 30 | 26% | 4 | 151,781 | 16 | 4 |
| prestige | 30 | 46% | 4 | 102,678 | 15 | **0** |

Steady is the one row re-measured after the stuck-and-won fix below, because
it was the row whose arithmetic exposed the bug. It went 33% -> **30%**: one
run of thirty, exactly the predicted size of the error, and the counts now
add up (9 won, 18 died, 3 stuck, 30 runs). The other rows are pre-fix and
carry the bound stated below.

**Hard, Swarm, Hardcore: 0% for every style.** Nobody kills the Warden. But
the modes are not equally dead, and on Hard the styles separate sharply
despite all reading 0%:

| Hard, by style | median | 90th | best | turns/run |
|---|---|---|---|---|
| rush | 1 | 5 | 24 | 2,096 |
| steady | 1 | 6 | 17 | 4,441 |
| farm | 1 | 4 | 24 | 2,807 |
| overkill | 1 | 6 | 19 | 5,920 |
| **prestige (30)** | 1 | **17** | **53** | 39,712 |

A 90th percentile of 17 against everyone else's 4 to 6, and a best of 53
against 24. Win rate alone said all five styles were identical on Hard, and
they are not remotely identical -- prestige gets three times as deep at the
top of its distribution as anything else in the game. This is the strongest
single argument in the matrix that the loop does something, and it is
invisible in the column everyone reads first.

It is also honestly terminated: **10% of those runs ended because the laps
stopped paying and 0% ended on the clock**, which is the property §71
demanded and the previous version of this harness did not have.

| Swarm and Hardcore | medians | best floor, any style |
|---|---|---|
| Swarm | 1 | 6 |
| Hardcore | 1 | 4 |

### What the numbers say

**The §72 numbers were as unreliable as §72 said.** Normal rush 13% -> 14%,
which is the control holding still at last. Normal farm 50% -> **35%**. A
fifteen-point error, from a twenty-run sample.

**The early game is the whole game, and farming does not help with it.**
Rush and farm draw the same seeds and lose *exactly the same 52 runs* in the
first ten floors. Staying longer on floor 2 does not make floor 2 safer -- it
makes it more dangerous, and only pays afterwards.

**What farming buys is everything after floor 10.** Past it, rush loses 32
runs in 100 and farm 12, on the same seeds. Steady lost 2 in 30 and prestige
**none at all** in 30 -- small samples, but a style that loses nothing below
floor 10 across thirty runs is saying something a hundred runs would only say
louder. Farming is not insurance. It is leverage, and it is leverage on the
part of the game you have to survive without it first.

**The prestige loop wins most and gives up most.** 46% against farm's 35% and
rush's 14%, at 3.8x a rusher's turns -- and it does it while going home less
than once a run on Normal. A style that cashes out 41 levels and buys 31
attribute points per run, on average, from 0.77 laps.

**The loop is a response to being beaten, not a schedule.** It fires where the
game hurts, because its trigger is "nearly dead" and nothing else. On Normal
the agent is comfortable enough to go home 0.77 times a run; on Hard, 1.60,
selling 88 levels for half a million gold a run. That is the correct shape for
the mechanic -- and it is why Hard, the mode where it is needed, is where it
shows the largest effect.

**Over-farming is a real cost, and §72 called it correctly on twenty runs.**
Overkill spends 151,781 turns a run -- 2.3x farm, 5.6x rush, the most
expensive style in the game -- and wins 26% against farm's 35%. It buys the
same early-game safety as farm (16 deaths in floors 1-9 against farm's
equivalent share) and then keeps paying for depth it does not convert. This
is the one place where more time does *not* buy more power, and it is worth
understanding before anything is tuned on top of it: the design claim is that
time is the constraint, and overkill is the counterexample the design has to
answer for.

**Win rate is the wrong instrument below Normal.** Every style reads 0% on
Hard, and reporting only that would have said "nothing helps". The 90th
percentile says prestige reaches floor 17 where rushing reaches 5. Any future
tuning of Hard, Swarm or Hardcore should be judged on the depth distribution,
because the win column will read 0% for all of it right up until it does not.

**Hard, Swarm and Hardcore are not a farming problem, and now that is
measured.** Every style, every mode, 0% and a median of 1. Hardcore loses 63
of 100 runs on floor 1 and 34 more on floor 2 -- there is no run there for a
loop to be part of. Whatever is wrong is in floor-one density, exactly where
§58 left it, and five styles agreeing is much stronger evidence than one.

### Two caveats on these numbers, both from this file's own rules

**The stuck-and-won overlap.** Reaching floor 100 was scored on arrival, so a
run *abandoned* there counted as a win. Fixed after these rows were measured.
It can only flatter a style, and only by the bracketed stuck count: at most 1
run in 100 on every row above except Normal steady (3 in 30) and Normal
prestige (2 in 30). Re-measuring steady confirmed the bound exactly -- one run
moved, 33% -> 30% -- so the largest correction still outstanding anywhere in
the table is one percentage point on Normal rush, and two runs on Normal
prestige.

It was found by arithmetic that would not add, in a row nobody was looking at
closely. Printing the counts a rate is derived from, next to the rate, is why
that was possible at all.

**PRESTIGE's tail is patience-dependent and does not saturate.** Swept on
Hard, 8 runs each: patience 2 and 6 both top out at floor 17; patience 14
reaches 27; the share of runs ending on the rule is 12% at all three. More
laps buy more depth, without limit, because attribute prices grow as 200v^2
while the purse feeding them grows with the level curve and compounds through
the bank. That is the FF7 argument working exactly as intended -- and it means
no single patience setting can be quoted as *the* prestige result. It is
`argv[9]`, and it belongs in a write-up as a curve.

## 84. Score for the session: eleven instrument bugs, two game bugs

In the balance harness:

1. Hardcore could recall, in a harness whose game forbids it.
2. Winning runs reported nought gold found.
3. The prestige trigger could never fire -- zero laps in three runs.
4. Integer averages printed "never happened" for "happened once".
5. A run could be scored as both stuck and won.

In the self-play harness:

6. Diagonal steps decomposed into two halves that were both wall.
7. A quarter of the key pool was bound to keys the game discards.
8. Auto-explore stopped on its first tick, every time, because a
   non-blocking poll was answered with a keypress.
9. Auto-explore was then capped at three tries by a number I picked.
10. 74% of the wall clock went on animation pauses nobody was watching.
11. It played in the project owner's save slot.

That last pair are not really bugs in a policy; they are what happens when a
program written for a person at a terminal is handed to something that is not
one. Both were invisible until the fuzzer ran long enough to make them
obvious -- 74% is not a number you notice in a five-second session.

The game, at last: the Gladiator School deleting every level, shrine bonus and
ranged weapon the character had earned; and every death in the game reporting
that it happened on floor 0.

**The pattern held, and then broke in a useful way.** Suspecting the harness
first was right eleven times out of thirteen, which is a stronger record than
§76's. But both game bugs were found the same way, and it was not by
suspicion -- it was by *doing something in the harness that nobody had done by
hand*. Nothing in this project had ever bought an attribute point outside a
played session, so nothing had ever exercised `train_attribute` on a character
with levels in them. Nothing had ever died 216 times in four minutes, so
nobody had ever read the same wrong sentence often enough to see it.

So the rule gains a second half. Suspect the harness first -- and when the
harness is clean, look at what it just did that a person never would.

## 85. Every number in this file is about one map size

The project owner asked whether map size had been taken into account. It had
not. Every cell of §83 was run with `world 0` -- **The Shaft**, 140x80, the
smallest of four, and the one whose whole area is a hundredth of the largest.

`tools/simulate.c` has always printed `World: The Shaft` at the top of its
output and §83 quotes the command line that says `0`, so the record was never
wrong. It simply never said out loud that it was one slice of a four-way axis,
and the conclusions were phrased as though it were the game.

| world | size | tiles | waygates |
|---|---|---|---|
| The Shaft | 140x80 | 11,200 | every 5 floors |
| The Halls | 350x200 | 70,000 | every 5 floors |
| The Deeps | 700x400 | 280,000 | **every floor** |
| The Well | 1400x800 | 1,120,000 | **every floor** |

### Two harness bugs that only fire on the big worlds

**The waygate interval was hardcoded.** mapgen places a gate wherever
`floor_num % waygate_interval() == 0`, and that interval is 1 rather than 5
once a floor passes 280,000 tiles -- deliberately, because "climb four floors
to a gate" on the Deeps is an evening of retracing cleared ground. The harness
said `floor % 5 == 0`. On the two large worlds the agent would have been given
a fifth of the town stops the game actually offers: a fifth of the shopping,
the banking and the upgrades. It reads the game's own `waygate_interval()`
now.

**`MIN_FLOOR_STAY` and `XP_SAMPLE_TURNS` were flat.** They are the windows the
farming rule judges "is this floor still paying" by, at 400 and 250 turns,
tuned on the Shaft. A floor of the Well is 627,000 walkable tiles. Four
hundred turns there is not enough to judge a floor by; it is the first
corridor. A farmer would have sampled two rooms, concluded the floor was
tapped and taken the stairs -- FARM behaving exactly like RUSH, which is the
failure §71 records for the first farm target, and which would have read as
*a finding that farming does nothing on large maps*. Both scale by area now,
like TURN_BUDGET_BASE always has.

Neither touches the Shaft: `SCALE_BY_AREA(250)` is 250 and
`waygate_interval()` is 5 at 11,200 tiles. Verified rather than assumed --
Hardcore rush at 100 runs is identical before and after, down to
`worst crowd 438 (fl 2)`. The §83 table stands.

### And then the actual finding: map size dominates difficulty

20 runs, rush, fair start. This is a probe, not a matrix: one style of five,
and the Well not covered at all. Everything below should be read as "large
enough to act on, not large enough to quote".

| | Shaft | Halls | Deeps |
|---|---|---|---|
| **Normal**, win | 0% (14% at n=100) | **70%** | **95%** |
| **Normal**, turns/run | 14,140 | 429,484 | 2,231,348 |
| **Hardcore**, median | 2 | 1 | 1 |
| **Hardcore**, 90th | 2 | 2 | **22** |
| **Hardcore**, best | 3 | 3 | **27** |
| **Hardcore**, health between floors | 74% | 91% | **100%** |

**Read the Halls column and the Deeps column differently.** At 70,000 tiles
the waygate interval is still 5, so nothing about the waygate fix above
applies to the Halls: Normal going 14% -> 70% there is floor size and nothing
else. The Deeps is past the 280,000-tile threshold where the game switches to
a gate on every floor, so its column mixes floor size with five times as many
town stops -- both real, both the game, but not separable from one run each.
Anyone quoting the Deeps figures should say which of the two they are
claiming, or run the Deeps with the interval forced to 5 to split them.

**And the help is not uniform across difficulties, which sharpens §83 rather
than replacing it.** Hard on the Halls barely moves (best 24 -> 25, still 0%).
Swarm barely moves (best 5 -> 11, still 0%) and its crowding gets worse for
the obvious reason -- worst crowd 391 -> 4,463, because a bigger map holds
proportionally more monsters and Swarm's pathology is density, not depth. A
bigger floor helps a run that *survives the early game*; it does nothing for
the early game itself. Which is the same wall §83 measured when rush and farm
lost exactly the same 52 runs in floors 1-9.

**Why it happens is not subtle once it is written down.** Monster *density* is
held constant across worlds by design -- every population cap is
`SCALE_BY_AREA`d. What is not constant is the walk. A bigger floor means more
encounters between the entrance and the staircase, at the same depth, so the
player arrives at floor 20 having fought four hundred things instead of forty.
Experience scales with the area of a floor; monster strength scales with its
*number*. The two run away from each other, and the bigger the world the
further ahead the player gets.

Hardcore on the Deeps is the clearest case: **100% health between floors** and
a 90th percentile of floor 22, in the mode that on the Shaft dies on floor 1
sixty-three times in a hundred. It is not a different difficulty setting. It
is a different game.

### What this costs the rest of the file

**The roadmap's largest open item was a statement about the Shaft.** "Hard,
Swarm and Hardcore are 0% at medians of 1, and the cause is floor-one
density" is true, and it is true of 140x80. On the Deeps, Hardcore rush
reaches floor 27 at full health without any farming at all. Whatever is
choking those modes is a Shaft-shaped problem -- a floor small enough that
there is nothing to fight on the way down and no level to be had before the
monsters catch up.

**Time is the constraint here too, and the price is legible.** The Deeps buys
95% at 2,231,348 turns a run against the Shaft's 14% at 27,054 -- 82x the
time. That is the same trade §72 measured between rush and farm, appearing on
a completely different axis, and it is the design principle working: the four
worlds are four answers to *how long is a run*, and difficulty falls out of
that rather than being independent of it.

**Nothing should be tuned per-difficulty until it is measured per-world.** A
change that fixes Hardcore on the Shaft has four chances to be wrong, and this
file currently has evidence about one of them.

### Coverage, so the next session knows what is actually known

Measured (rush only, n=20 unless noted):

| | Shaft | Halls | Deeps | Well |
|---|---|---|---|---|
| Normal | yes (n=100, all 5 styles) | yes | yes | no |
| Hard | yes (n=100, all 5 styles) | yes | running | no |
| Swarm | yes (n=100, all 5 styles) | yes | no | no |
| Hardcore | yes (n=100, all 5 styles) | yes | yes | running |

So: the Shaft column is a real matrix, the Halls column is one style, and the
Deeps and the Well are a handful of probes. The cheapest thing that would
change the picture is the Halls at all five styles -- it is the world where
floor size is the only variable, and it already showed the largest clean
effect in the file.

The Well is expensive enough to need planning rather than patience: Normal
rush on the Deeps already costs 2.2 million turns a run, and the Well is four
times the area again.

## 86. Two problems wearing one name: the floor-one wall and the resupply ceiling

§85 found that the hard modes come alive on the Deeps, and the obvious reading
was floor size. It is not. `waygate_interval()` drops from 5 to 1 at exactly
the Deeps threshold, and forcing that interval independently of map size
(`argv[10]`, `town_stop_interval()`) separates the two.

**The Halls, floor size held completely still, rush, n=20:**

| | town every 5 | town every 1 |
|---|---|---|
| Hard, median / 90th / best | 1 / 3 / 25 | 1 / **36** / **49** |
| Hard, health between floors | 89% | **100%** |
| Hardcore, median / 90th / best | 1 / 2 / 3 | 1 / 5 / 5 |

Hard's 90th percentile goes from floor 3 to floor 36 without a single extra
tile, which reproduces and exceeds the whole Deeps jump. Floor area was never
the cause for Hard.

**The Shaft, the world the problem actually lives on, rush, n=60:**

| | town every 5 | town every 1 |
|---|---|---|
| Hard, median / 90th / best | 1 / 4 / 15 | 1 / **14** / 23 |
| Swarm, median / 90th / best | 1 / 2 / 3 | 1 / 2 / 4 |
| Hardcore, median / 90th / best | 2 / 2 / 4 | 2 / 2 / 5 |

And here the story splits, which is why it is worth writing down rather than
acting on the Halls alone.

**Every median is unmoved.** 1, 1, 2 before and 1, 1, 2 after, in all three
modes. The harness gives a town stop on floor 1 regardless of the interval, so
the median run dies before a second town stop could ever have helped it. The
floor-one wall §58 identified is real, it is not town starvation, and nothing
measured this session touches it -- not floor area, not waygate frequency, not
farming, not the prestige loop.

**The tails move, and only for Hard.** 90th percentile 4 -> 14 on the Shaft
and 3 -> 36 on the Halls. Swarm and Hardcore barely register (2 -> 2 in both).

So there are two problems here and the project has been calling them one:

1. **A floor-one wall.** Kills the median run in all three hard modes, on
   every map size, under every style. This is what §58 named and it is still
   entirely open.
2. **A resupply ceiling behind it.** Runs that get past the wall are then
   limited by how often they can shop -- for Hard, decisively. Fixing this
   changes nothing about the win rate (still 0%) and a great deal about how
   far a surviving run gets.

**Why Hard and not Swarm or Hardcore.** Those two carry a 10x experience
penalty (`combat.c`: `xp /= 10` for SWARM and HARDCORE). Resupply converts
gold into survival, but gold is earned per kill and levels are not -- a mode
that cannot level cannot use a shop it cannot afford. That also explains why
Hardcore *does* come alive on the Deeps (90th 22) where the Halls at interval
1 only reached 5: the Deeps supplies both the shop trips and the encounters,
and Hardcore needs both. Hard needs only the shop.

### What this means for the roadmap

The standing diagnosis -- "Hard, Swarm and Hardcore die to floor-one density"
-- survives, and is now better bounded than it was: it is specifically about
the *median* run and specifically not about resupply, because giving those
modes five times the town stops moves no median at all.

The resupply ceiling is a separate, cheaper, and independently useful finding.
`waygate_interval()` is one line. It will not make a mode winnable, and it
should not be sold as if it might; what it buys is that a Hard run which
survives its first two floors reaches floor 14 at the 90th percentile instead
of floor 4.

## 87. The dungeon master: three versions, two of them wrong

The project owner asked for a cheat -- "a guardian angel: more criticals,
evade more, the monsters fail more" -- to hold a new player's hand long enough
that they stop dying on floor 2 and actually meet the systems. It took three
attempts, and the first two failed in opposite directions.

**Version one: help when hurt.** Points of crit and evasion below 45% health,
scaled to full at death's door. A/B at 60 runs a difficulty, off against on:

| | off | on |
|---|---|---|
| Normal, win / median | 11% / 5 | 11% / 4 |
| Hardcore, median / best | 2 / 4 | 2 / 4 |

Nothing. Hardcore came out digit for digit identical. The cause was structural
rather than a tuning miss: **45% is the line the player drinks at**, so the
angel's entire operating range was the range a competent player spends no time
in -- and the deaths it existed to prevent are bursts that cross from
comfortable to dead without pausing in between.

**Version two: help early.** A large unconditional "escort" -- 45 points at
floor 1, tapering to nothing by floor 50 -- on the reasoning that the opening
is what needs to be a cakewalk. It worked enormously, and was still wrong:

| Normal, 60 runs | off | escort |
|---|---|---|
| deaths in floors 1-9 | 32 | **2** |
| deaths on floor 1 | 14 | 1 |
| median floor | 5 | **100** |
| **health between floors** | 81% | **87%** |

That last row is the failure. Health between floors went *up*. Deaths fell 16x
because **nobody was ever in danger** -- a flat bonus large enough to stop the
deaths also stops the fights being fights, and removes exactly the near misses
it is supposed to let you survive. It also snowballed: the escort is gone by
floor 50, yet the median run reached 100, because power accumulated while
protected carries through the half that is not. Making the first half a
cakewalk does not make the first half easy, it wins the game.

**Version three: what a dungeon master actually does.** Not a bonus. A screen
to roll behind.

- **A nudge** -- the escort, cut from 45 to 15. Tilts the dice; does not end
  the fight.
- **A screen** -- `guardian_catch()`. The blow that would have killed you
  leaves you on one hit point instead. Once per floor, opening half only,
  never in town, and it says so out loud, because a save the player does not
  notice buys nothing. Wired into all six death paths, so no route to death
  quietly bypasses it.
- **A net** -- version one, kept. Small, only when nearly dead, at every
  depth. Past the middle it is all there is.
- **An avenging half** -- `avenger_escalation()`. Up to 25 points of extra
  monster escalation when the run has been *coasting*. A master who only ever
  saves you is running a cutscene.

| 60 runs, rush | dm off | dm on |
|---|---|---|
| Normal: win / median / turns | 11% / 5 / 24,108 | 16% / **36** / 37,995 |
| Hard: median / 90th / best | 1 / 4 / 15 | 2 / **9** / **29** |
| Hard: recalls per run | 0.57 | **2.70** |
| Hardcore: median / best | 2 / 4 | 2 / 5 |

Against the design goal -- *live long enough to meet the systems* -- Normal's
median run goes from floor 5 to floor 36 and spends 58% more time in the game.
Floor 5 matters specifically: it is the first waygate back to town, so the
median unassisted run **died on the floor it would first have come home rich
from**. Hard's recalls per run going 0.57 -> 2.70 is the same thing seen from
another angle: the player now survives long enough to use a system they
previously never touched.

And the avenging half is doing its job. The escort alone produced median 100
and a 51% win rate; with the master pushing back it is median 36 and 16%. The
run goes much deeper and sees much more, and still has to earn the ending.

### The asymmetry, which is the design

The avenging half reads `dda_pressure`. The guardian half deliberately does
not, and the reason is not an inconsistency:

- **Dying is an instant.** It happens inside a floor, usually the first, and
  pressure only moves in `dda_floor_end()`, which runs when a floor is *left*.
  A guardian riding on pressure is blind to the deaths it exists to prevent
  (§86). It reads hit points, which are true every turn.
- **Coasting is a history.** One easy room proves nothing; four floors at full
  health is a fact about the run -- exactly what pressure measures. And a
  coasting player is by definition finishing floors, so the update always
  arrives.

This is the first thing to consume `dda_pressure`, which has carried
"NOTHING CONSUMES THIS YET, deliberately" in its header since it was written.

### Version four: the crossfade

The two faces now pivot on the same floor in opposite directions -- the escort
loudest at floor 1 and gone by 50, the avenger at a quarter weight at floor 1
and full from 50 down -- so the descent reads as *carried, let go, leaned on*.
Neither reaches zero: the avenger keeps a floor early so a second playthrough
cannot coast the opening untouched, and the guardian's net stays live at every
depth so the bottom of the dungeon is survivable.

| 60 runs, rush | dm off | dm on (crossfade) |
|---|---|---|
| Normal: win / median / turns | 11% / 5 / 24,108 | **18% / 37** / 39,657 |
| Normal: deaths in floors 1-9 | 32 | **15** |
| Normal: deaths in floors 31-50 | 15 | 23 |
| Hard: median / 90th / best | 1 / 4 / 15 | 2 / **9** / **29** |
| Hard: deaths in floors 1-9 | 56 | **53** |
| Hardcore: deaths in floors 1-9 | 60 | **60** |

On Normal this is the intended shape at last. Early deaths are *halved* rather
than eliminated -- the opening is survivable without ceasing to be dangerous --
and the deaths that remain moved deeper, into floors 31-50 where the escort is
gone and the avenger is coming up to full weight. The median run goes from
floor 5 to floor 37 and spends 64% longer in the game.

### The save has no follow-through, and the hard modes prove it

Hard's early deaths went 56 -> 53. Hardcore's went 60 -> 60. **Not one run in
sixty was saved**, with fifteen points of escort and a free save on every
floor.

The reason is visible once stated: `guardian_catch()` leaves the player on one
hit point, in the same room, with the same monsters adjacent, on their turn.
The next blow lands and the character dies anyway. A save that only buys a
single hit point buys a single blow, and in the fights that actually kill
people the next blow is already coming.

A dungeon master does not say "you are on one hit point" and move on. They
say the thing that gives the player a turn to use: the creature recoils, the
ceiling comes down between you, you come to twenty feet away. The screen needs
to buy *space*, not a hit point -- a moment of invulnerability, a shove that
puts distance between the player and what is hitting them, or a free turn --
and until it does, it will keep working on Normal (where the next blow is
survivable) and keep doing nothing at all on the modes it was built for.

This is the same class of mistake as versions one and two, for the third time:
the mechanic was specified as a number when what the design needed was a
*situation*.

### What is still wrong

**The instrument cannot see drama.** The design target is not "few deaths" --
it is *low health between floors and few deaths at the same time*: constantly
nearly dying and constantly getting away with it. Health between floors barely
moved (81% -> 84% on Normal, the wrong way) and it is a poor proxy anyway,
because it samples on arrival, after a town heal. What the harness needs and
does not have is a count of **how often the screen fired** and **the lowest
hit points reached per floor**. Until those exist, "is it exciting" is not a
measurement and nothing here should be tuned toward it.

**Hardcore is unmoved, and the angel is the wrong tool.** Median 2 -> 2, best
4 -> 5, with 15 points of escort and a free save per floor. It is not the
dice: a Hardcore character is still level 2 on floor 3 whatever the rolls do,
because of the 10x experience penalty (`combat.c`), while monsters scale with
depth regardless. Against the stated principle that every mode must be
finishable, this points at the experience penalty and not at the master.

## 88. The swarm had no zombies in it

Asked why Swarm and Hardcore earn a tenth of the experience, the project
owner's answer was the design intent: those monsters are meant to be *weak* --
a zombie horde that kills by numbers rather than by strength -- so a tenth of a
monster pays a tenth of the reward.

That half had never been built.

`make_monster_for_floor(int floor_num, int x, int y)` does not take a
difficulty and never has, so it cannot scale a monster by mode. Grepping
`difficulty` across monsters.c finds three comments and one line about
respawns, and nothing that touches hit points, attack or defence. The actual
state of both modes was:

- **monsters identical to Normal** -- same HP, same attack, same defence;
- **numbers 10-20x**, ramping with depth (mapgen.c);
- **experience at a tenth**, which §87 showed collapses to a flat 1 per kill
  on the floors where every run of those modes dies.

Not a zombie swarm. The Normal bestiary, twenty times over, paying trash
rates: both halves of the punishment and neither half of the compensation.

**It explains every failed intervention.** The guardian angel at 260% strength
did not move Hardcore's median. Five times the town stops did not move it. No
playstyle moved it. The whole experience-divisor range did not move it. All of
them were treating a level-2 character fighting floor-3 monsters at floor-3
strength, which is not a problem any of them address.

### Built

`monsters_set_difficulty()`, alongside `monsters_set_escalation()` -- the same
idiom for the same reason, so none of the ten call sites had to learn about
difficulty. Swarm monsters take **a tenth of the hit points, a third of the
attack, half the defence**, applied *after* escalation so they are a fraction
of whatever the curve currently says a monster is.

Hit points and experience now divide by **one constant**,
`SWARM_MONSTER_FRACTION`: monsters.c cuts the stats, combat.c's `grant_xp`
divides the reward. They cannot drift back into "a tenth of the reward for a
whole monster".

Attack is the one figure not derived from it, deliberately. Only eight things
can reach you at once however many are on the floor, so cutting attack in the
same proportion as hit points would make a horde *safer* than a single Normal
monster -- eight attackers at a tenth each is four fifths of one ordinary
blow. At a third, a surrounded player takes about two and a half times what
one Normal monster deals.

### Measured, 60 runs, rush, guardian off

| | before | after |
|---|---|---|
| Normal: win / median | 11% / 5 | 11% / 5 |
| Hard: win / median / best | 0% / 1 / 15 | 0% / 1 / 15 |
| **Swarm**: median / 90th / best | 1 / 2 / 3 | **6 / 13 / 17** |
| **Hardcore**: median / 90th / best | 2 / 2 / 4 | **5 / 11 / 16** |
| Hardcore: turns per run | 646 | 4,891 |

Normal and Hard are byte-identical, which is the control: the change reaches
only the modes it names. And Hardcore's median finally moved -- from 2 to 5,
after six versions of the guardian angel, five town-stop intervals, five
playstyles and the entire experience-divisor range had each moved it by
nothing at all.

### And it overshot

**The ladder is inverted.** Swarm's median is 6 and Hardcore's is 5, against
**Hard's 1**. The two modes that are meant to sit hardest now go five times
deeper than the mode below them. Some of that is Hard being mis-tuned in its
own right -- it has sat at median 1 since §83, with 2-3x density and
full-strength monsters and no compensation of any kind, and it is now the
hardest mode in the game by a distance. But the fraction is clearly too
generous as well.

**A quarter of Swarm runs now end *pinned*.** 16 of 60: alive, staircase
reachable, not getting there, held in a crowd of 1,170. That is the horde
mechanic working exactly as described -- and it is now the dominant failure
mode of the mode, ahead of dying. Worth deciding whether being ground to a
halt by numbers is the intended Swarm death or a policy failure in the agent,
because the harness cannot tell those apart and neither can this entry.

Both numbers say the same thing: `SWARM_MONSTER_FRACTION` at 10 and
`SWARM_ATTACK_PCT` at 35 are a first guess that landed too soft, and they want
the same sweep treatment the prestige patience and the waygate interval got
before anything is concluded from them.

## 89. The ladder, and three intents that shipped half-built

The project owner set the acceptance test: *Normal easiest, then Hard, then
Swarm, finally Hardcore. If we get this the game is balanced.* Before this
session's work the order was inverted -- Hard was the hardest mode in the game
and the two horde modes were the easiest after Normal.

It was not fixed by tuning. It was fixed by three mechanics whose punishing
half had shipped and whose compensating half never had. Each was found the
same way: the owner said what a mode was supposed to be, and the code
disagreed.

**Swarm monsters were not weak** (§88). `make_monster_for_floor` takes no
difficulty and never has, so Swarm and Hardcore fought the Normal bestiary at
full strength, twenty times over, while being paid trash rates for it.

**The horde did not exist on floor 1.** `swarm_mult = 1 + floor_num * 11 / 20`
is **1** at floor 1, so Swarm and Hardcore opened at *Normal density* -- and
once the monsters were correctly weakened, their opening floor came to a third
of Normal's threat. Meanwhile Hard opened at 2x with full-strength monsters:
measured total incoming attack on floor 1 was Normal 609, **Hard 1294**, Swarm
186, Hardcore 186.

**The infestation did not infest.** `maybe_respawn_monsters` returned early for
Swarm and Hardcore. Normal and Hard got the slow trickle; the two horde modes
were the only ones in the game where clearing a room cleared it permanently. A
horde that can be ground down to nothing is a stockpile.

### What changed

- Swarm monsters take a tenth of the hit points, a third of the attack and
  half the defence. Hit points and experience divide by one constant so the
  payment and the thing paid for cannot drift apart again.
- Floor-1 density: Hard's base 2 -> 1, the swarm base 1 -> 3, and a Hardcore
  multiplier on top -- **the first thing that has ever distinguished Hardcore
  from Swarm**, which until now generated byte-identical floors and differed
  only in respawns and recall.
- Infested floors refill toward the density they were *generated* at. A level,
  not a rate: clearing never sticks, the monster array cannot run away, and
  killing faster than it refills still makes progress you just cannot bank.

Five knobs (`AETHER_HARD_BASE`, `AETHER_SWARM_BASE`, `AETHER_SWARM_ATK`,
`AETHER_SWARM_FRAC`, `AETHER_HC_DENSITY_PCT`, plus `AETHER_INFEST_RATE`) are
environment-backed so the grid could be walked without a rebuild per point --
the same idiom `AETHER_STATE_DIR` already uses.

### Measured: 100 runs a cell, three styles, guardian off

| 90th percentile | Normal | Hard | Swarm | Hardcore |
|---|---|---|---|---|
| rush | 100 | 13 | 4 | 3 |
| farm | 100 | 10 | 4 | 2 |
| prestige | 100 | 33 | 3 | 2 |

| best floor | Normal | Hard | Swarm | Hardcore |
|---|---|---|---|---|
| rush | 100 | 24 | 8 | 5 |
| farm | 100 | 24 | 12 | 5 |
| prestige | 100 | 53 | 15 | 5 |

Monotone on every style, on both measures, with no ties. Normal rush also
returned to 14%, matching §83 exactly, which confirms none of this disturbed
the mode it was not supposed to touch.

### Three things not to lose

**The ladder lives in the tail.** Medians are 9/2/2/1 -- compressed, and Hard
and Swarm tie there. For modes at 0% wins the depth distribution is the only
instrument that discriminates (§86), and it discriminates cleanly, but "Hard
and Swarm feel different" currently rests on the top decile of runs.

**Swarm's pinned count is now 14 in 100.** Alive, staircase reachable, not
getting there. That is the infestation working -- the floor filling in behind
you is exactly what stops you getting through -- and it is now the mode's most
common non-death outcome. Whether being ground to a halt should score as a
death is a design decision, not a measurement one.

**None of this is shipped.** Every value above is an environment override; a
player launching the game gets the old inverted ladder. The sweep produced
these numbers, and a sweep is not a decision.

---

## 90. One body struct

`Player` meant two things at once. It was the party -- the purse, the pack, the
quest, how deep the run had got -- and it was also the protagonist's body: hit
points, thirty attributes, a weapon, a spellbook. `Companion` was the other
kind of body: a hire, carved down to what somebody thought a follower needed.

That split was reasonable while a hire was a follower. It stopped being
reasonable the moment a hire could *be* the main character, and the ROADMAP's
take-control entry lists six bugs it produced, every one of them the same bug
wearing different clothes: the AI drove the body you were steering, the screen's
"you" stayed pinned to the character, auto-explore read `p->x`, the panel had no
aether bar to draw for a hire.

The root was never really the two structs. It was **two constructors**. One
built a character out of a class seed and wrote thirty attributes and two dozen
derived stats; the other built a hire out of an archetype tilt and wrote none of
them. Every difference downstream fell out of that, and none of it was a
decision anybody made.

### What it now is

One `Hero`: position, vitals, gear, the thirty, every derived stat, spells,
ranged kit, ability, status, buffs, and the AI's scratch space. One `Player`:
`Hero party[6]`, `controlled`, and the state the party genuinely shares.

`hero_create()` is the only way a body is made -- the character at new-game, a
hire at the Tavern, a golem off the assembly line.

### How it was done, which is the transferable part

The instinct with a refactor this wide is to read every call site. That is the
wrong instinct and it is why this sat undone: 3,300 field references across
21,000 lines reads as a week.

It was two passes.

**Pass one changed the struct and let the compiler enumerate the damage.**
1,948 errors, every one of the form *no member named X in Player*, each with a
file, a line and a column pointing at the member token. Because `p->hp` meant
the character, `p->party[0].hp` is a faithful translation -- so the fix was
mechanical and provably behaviour-preserving, and a 40-line script applied it
to all 1,873 distinct sites. The three suites passed immediately afterwards,
which is the evidence that pass one changed nothing.

**Pass two changed what needed changing**, and was small enough to read: the
constructors, the party accessors, the per-turn tick, and the signatures of the
systems that should never have known whose body they were holding.

The lesson is the split, not the script. **A refactor that cannot change
behaviour and a refactor that should are two different changes, and doing them
in one pass is what makes a wide change frightening.** The judgement was
concentrated in about thirty places; the compiler found the other 1,843.

### Two bugs, neither of them looked for

**A driven hire never ticked.** In `companions_take_turn`, the line that skips
the body the player is driving sat *above* the block that decrements cooldowns,
regenerates aether and expires buffs. So the one body you were actually
fighting with was the one body for which time did not pass: take over a caster
and their spells stayed on cooldown for the rest of the run.

It is a two-line move, and the reason it is worth writing down is why it was
wrong. The line was written to mean *the AI does not decide for this body*. It
quietly also meant *this body does not exist this turn*. Ticking is something
that happens **to** a body; deciding is something done **with** one. Only the
second has an owner, and the code had no way to say so because a hire's
existence was expressed as an exception in someone else's loop.

**The snare gap deleted itself.** §-none: the previous session wrote down, as
an open item, that a snared hire takes the damage but not the hold, "because
Companion has no stun field -- that lives on Monster and on Player", and
estimated four lines. There is one body struct now, so a hire has the stun
field the character always had. The fix was to remove the paragraph explaining
why it could not be fixed.

That is the shape of a good structural change: the backlog gets shorter without
anybody working on it.

### What fell out, which was the point

A hire is built by the character's constructor, so they arrive with a real
attribute sheet, a real derived sheet, their class's own innate ability, and
their class's own magic school rather than one drawn from a name hash. Measured
across a roster: **six of six candidates carry thirty attributes where they
previously carried none**, and their field of view and crit now vary by class
because they are derived from the class rather than assigned by role.

An Arcanist hire still walks the schools if their own has nothing a companion
may cast -- Aether-Sense is blink, reveal and bridges end to end -- but that is
a fallback now instead of the whole selection. A Coil-Adept casts Conduit
because that is what a Coil-Adept is.

### One distinction worth keeping

`alive` is a fact about a body. `in_use` is a fact about the *party* -- which
seat, if any. Putting both in the constructor made every Tavern candidate you
merely looked at claim a party seat. They are set in different places for a
reason.

### Verification

`make` and `make notcurses` clean from scratch. Three suites, 0 failures. The
aggressive warning scan at **3 warnings, the same three `Biome` sign-conversions
as the baseline** -- unchanged by a refactor that touched every file. Four ASAN
`--silentrun` sessions, roughly 120,000 keys, zero reports. `grep -rn TESTFORCE`
empty. `SAVE_VERSION` 40.

### What this does not tell you

**Nothing above is a balance measurement.** Suites and sanitisers say the game
is the same program; they do not say it is the same game. One behaviour a run
can feel did change -- a driven body's cooldowns now advance -- and no number in
§83 or §89 was taken against that. Re-measuring the ladder is the first item on
the ROADMAP for exactly this reason, and until it is done the honest statement
is that the balance is *unverified*, not that it is unchanged.

### Still open

Movement and the melee swing are still two functions: `handle_player_move` for
slot 0 and `companion_player_move` for a driven hire. They are the last
duplicated pair and they now differ for no structural reason. Merging them is
the natural next step, and it is the same premise leaking one last time.

---

## 91. Three bugs from playing it, and one map per body

§90 was verified by three suites, a warning scan and four ASAN sessions, and
all of them passed. Then the project owner played it for ten minutes and found
three bugs. None of them was a crash, a leak or a warning, which is why none of
those instruments had anything to say.

Reported, in their words:

- *"when i go back to after i changes i don't see @"*
- *"the auto explore after work for a while stops"*
- *"another @ appears next to me"*

All three are the same premise, one layer below the one §90 removed: **a single
set of eyes and a single "you", belonging to nobody in particular, that every
piece of code assumed was the character's.**

### The three, and where each one lived

**The vanishing `@`.** `build_cells` had been taught to draw the driven body.
`resolve_one_cell` -- the single-cell version the animation code uses to put
back whatever a bolt or a flash was drawn over -- had not. It restored the
player glyph at slot 0. So every animated frame rubbed out the `@` you were
steering and painted one on the character standing somewhere else. One line,
two symptoms: the missing you, *and* the twin.

**The indistinguishable twin.** The undriven character was drawn by its own
branch, separately from the hires, in `CP_PLAYER` -- the player's own colour --
while every hire drew in its own. So there were two identical white `@`s and no
way to tell which one the keys moved. And because the character's AI walks them
toward the party, the twin reliably turned up *next to you*. Every undriven
body now draws the same way, in its own colour; `CP_PLAYER` means "the body
holding the role" and nothing else.

**Auto-explore stopping "after a while".** `autoexplore_progress()` built its
no-progress signature from slot 0's position, hit points and kills, while every
step of the walk moved `party_x/party_y`. Driving a hire therefore meant the
stuck-detector was watching somebody else. The character's AI moves them for a
while, so the signature moves for a while; when the AI settles, the signature
stops, and auto-explore declares itself stuck while the hire is walking
perfectly well. **"After a while" was the tell** -- a detector that was simply
broken would fire immediately.

There was a fourth, unreported: `effective_fov_radius()` took the character's
Vision score and asked whether the *character* was standing in the mire, then
lit the floor around the driven body. Its comment justified this -- "a hire has
no Vision attribute of their own, the thirty are the MC's" -- and that stopped
being true the moment `hero_create()` started building hires.

### One map per body

The owner's instruction was to stop patching the shared flags and give each
hero its own map. That is `src/vision.h`.

Six `HeroVision`s, one per party slot, each with its own `vis` and `seen` over
the full 1400x800. The Tile flags the renderer and the searches read are now a
*composite* rebuilt each turn from whichever bodies are standing -- so nothing
downstream changed, and there is now a real answer to "what does this
particular hero know" instead of a shared global that the last caller happened
to write.

13.4 MB, held once for the program rather than per floor, and deliberately not
on the `Hero` struct: a Hero is copied by value in a dozen places -- the Tavern
rolls candidates, the School derives two throwaway sheets, hiring assigns a
whole struct -- and none of them want 2.2 MB coming along.

**Time was the real constraint, not memory.** Clearing six million-tile layers
per turn to undo a few hundred lit tiles is exactly the mistake §57 removed, so
each slot keeps its own lit-list and clears only what it lit; the composite does
the same. This is why the union costs what was lit rather than what exists.

**On the six threads.** They were offered and they are not needed, and it is
worth saying why rather than just declining: the work is a few hundred
line-of-sight walks per turn, the six maps are written once and read once in
the same turn, and the join would cost more than the work. What made this area
slow at 1400x800 was a map-sized sweep per turn, and the fix for that is the
lit-list, which is in. The slots *are* independent, which is the property that
would make threading safe if it ever became the bottleneck.

### What is saved, and what is not

The save file is the `Map` struct and always has been, so it carries the
party's shared memory of a floor rather than six private copies. A resumed run
hands every standing body what the party knew and they diverge again from
there. That costs a distinction nobody can perceive and saves six million tiles
of save file.

### The harness was made to match

`tools/simulate.c` and `tools/trace.c` lit the floor with `compute_fov` plus
`companions_reveal`. The game composites six maps. A harness that lights the
floor differently from the game measures its own lighting -- §68 and §79 are
two sessions' worth of that lesson -- so both now call `hero_vision_update()`,
the same function `refresh_vision()` does.

### The lesson, which is not a new one

§68 and §76 say *when a harness reports something surprising about the game,
suspect the harness first*. This is the other half of it, and it deserves
stating: **when the suites are green and the game is wrong, suspect the
instruments' coverage.** Three suites, a sanitiser and a warning scan cannot
see a glyph drawn in the wrong colour or a signature computed from the wrong
body, because every one of those is a correct program doing the wrong thing.

The regression test added here (`test_per_hero_vision`) states each bug as the
property that was false, which is the form that would have caught them: *the
body you are driving can always see its own square*; *walking the driven body
counts as progress, whoever slot 0 is*.

### Verification

Clean build on both backends, three suites at 0 failures with the new test, the
warning scan still at exactly 3 `Biome` sign-conversions, two further ASAN
`--silentrun` sessions with zero reports, `grep -rn TESTFORCE` empty.

**Still not a balance measurement**, for the same reason as §90 -- and the
per-hero split makes re-measuring more urgent rather than less, because the
undriven bodies' sight radius is now applied per body rather than bolted on
afterwards. It is deliberately the same `COMPANION_FOV` it always was, so the
composite should be identical for an undriven party; that is a claim about the
code, and it has not been measured.

---

## 92. Six actors

§90 removed one premise -- that a hire is a lesser thing than a character --
and §91 removed it one layer down, in the eyes. Then the project owner played
it and named the premise that was still there, in one sentence:

> *"assume 6 actors... the problem is that you're thinking in one hero and not 6."*

That is the correct diagnosis and it is not the same as §90's. §90 gave every
body the same *fields*. It left the *verbs* asymmetric: there was still a
movement function for the character and a different one for a hire, still an
AI written for the character and a different one for the hires. One hero with
five attachments, wearing a shared struct.

### What was actually wrong

Three implementations of "an actor moves one square":

| | what it could do |
|---|---|
| `handle_temple_move` | hazards, snares, levers, the camp, loot, features, shops, portals, stairs |
| `companion_player_move` | walk, and swing |
| `companion_move_to` | walk, and bump |

`party_step` chose between the first two based on who was driving. **So taking
over a hire silently demoted you to a poorer set of physics** -- a different
portal implementation, no lever, no camp, no merchant, and terrain damage that
went through a separate function (`hire_landing_damage`) with a separate
landing path (`party_landing_effects`) behind it.

Every bug reported from play was those implementations disagreeing. They could
not be patched into agreement, which is what the owner meant by *patches won't
work for this*.

### What it is now

One movement function, `actor_step()`, used by the human's keys, by the AI and
by auto-explore. One attack, `hero_attack_monster()`. One per-turn tick,
`hero_tick()`, run once for each of the six. One AI pass over all six slots,
skipping whichever one the human is attached to.

Deleted outright: `companion_player_move`, `handle_temple_move`, `party_step`,
`mc_take_turn`, `hire_landing_damage`, `party_landing_effects`,
`player_attack_monster`, and the character's six separate tick calls. Roughly
230 lines of duplicate path, gone -- not moved.

**The one asymmetry left is named honestly.** `player_led` in `actor_step` is
not "is this the main character". It is *does the party follow this actor* --
which is true of whoever the human is attached to and false of the other five.
The stairs, the waygate and the merchant answer to it, because those are
decisions rather than events, and an actor under AI has no way to answer the
question they ask. Everything that happens *to* a body -- lava, snares, loot
into the shared pack -- happens to all six alike.

### The dog

Reported as *"the ai follows me like a dog"*. The last branch of
`mc_take_turn()` was:

```
/* Otherwise keep up with whoever is being driven. */
```

The character pathed to your feet every turn, stood next to you, and drew in
the player's own colour. Hires never had that -- their leash was removed
deliberately, and the comment where it used to be says why: *they are
adventurers with their own opinions, not an escort.* The character was still
on one because the character was still special.

`mc_take_turn` is deleted. Slot 0 goes through the same AI as everybody else.
Nothing in the game follows anybody now.

### Two things caught on the way

**Ticking is not deciding.** The tick was the first half of the AI loop, so
hanging it there meant it did not run in town -- a buff taken downstairs never
expired while you shopped. It is its own pass now (`party_tick`), over all six,
everywhere. Everybody ticks; only five decide, and only underground.

**Order is load-bearing.** Moving the tick after the decision instead of before
it changes a weapon with cooldown N from firing every N turns to every N+1 --
a quiet nerf to every ranged weapon in the game, arrived at by moving two
lines. Caught by the invariants suite, which pins the cadence. The tick runs
first, as it always did.

### On the map painting, and what "I don't care about performance" does not buy

§91's per-actor maps are painted onto the Tile flags for the renderer to read.
The first cut repainted the whole map every turn. That is correct and it is
1.1 million writes at the Well to express a picture that changed by a few
dozen tiles -- the exact map-sized per-turn sweep §57 removed after measuring
it as the largest cost in the game.

It now repaints only when the answer changes: when the human attaches to a
different actor, or a new floor is entered. Between those, the composite keeps
the Map in step one tile at a time as the fog lifts.

The instruction was *"i don't care about memory and Performance"*, and it buys
a great deal -- 13.4 MB of per-actor maps, six field-of-view passes a turn
instead of one. It does not buy an O(map) loop per turn, because that one stops
the instruments working rather than merely costing time.

### What was measured, and what was only reasoned

Clean build on both backends, three suites at 0 failures, the aggressive
warning scan still at exactly 3 `Biome` sign-conversions, ASAN `--silentrun`
across four seeds with zero reports (one to completion at floor 7; the other
three are on the larger worlds and were cut short by the harness timeout, not
by a fault).

**None of that is a play test, and the four bugs that prompted this rebuild
were all found by playing.** They are fixed structurally -- there is no second
mover left to disagree, no `controlled = 0` anywhere, no follow branch -- and
that is a stronger claim than a patch, but it is not the same as having seen
them not happen. The profile was checked for a regression and none was found:
time goes to rendering and monster turns, and the fuzzer now reaches floor 7-8
where it used to reach floor 2, which costs more per key for a good reason.

### Still open

`src/render.c` reads the active actor correctly everywhere, but its screens
still take a `Player *` and reach for the active actor themselves rather than
being handed one. The owner's rule -- *the UI is agnostic, it should be able to
handle an active hero* -- wants those signatures to take the actor. That is
hygiene rather than behaviour, and it is the obvious next tidy.

`p->gold` at new-game is **600,000,000**, a testing value set at the owner's
request so the Tavern, the smith and the bank can be exercised from turn one.
The shipping value is 60. Put it back before measuring anything: every
winnability number this project has quoted is a fair-start number.

---

## 93. Everyone can do what their class allows

Reported from play, at the Guild, by a hero the owner had taken over:

```
Aether: 32/32   Power: +0%   School: ? (the only one taught)
The guild has nothing to teach a ? caster.
```

`companion_equip()` blanked `magic_school` to `-1` and restored it only for
Arcanists. So every other hire came out of the Tavern as a "?" caster and the
Guild refused to teach them.

That was harmless for as long as a hire was a follower who could never walk
into a shop. The moment a hire could be the body you are playing, it locked a
playable character out of an entire system -- **because of a sentinel written
for a design that no longer existed.**

The owner's rule: *everyone should be able to do everything that the class
allows, like the MC.* So the question became: where else?

### The sentinel, and what it was really for

Every class has a school. `compute_magic_school()` assigns one even to classes
with no magic attribute at all, spreading them by `class_id` precisely so that
nobody is excluded. `-1` was never "this class has no magic" -- it was "this
hire is carrying no spellbook", and *that* has its own field, `spell_count`,
which every guard in the module already tests.

Schools are kept now. `spell_count` stays 0 for a non-caster, so the AI
behaves exactly as before -- and the Guild will teach them, because there is
nothing left saying it should not. 160 candidates across 8 rosters, none of
them a "?" caster.

### The one that was worse

`give_ranged()` set `ranged_slot` and stopped.

The AI reads its weapon through that index into `RANGED_STOCK`, so a hired
Marksman shot perfectly well. But `ranged_type` stayed `RANGED_NONE`, and that
is what `fire_ranged`, `ranged_ready`, the character sheet and the Armory all
test. **So a Marksman you took over was carrying a bow the game would not let
them fire** -- "You have no ranged weapon equipped", from a hero whose entire
archetype is the bow.

Two representations of "what I am shooting with", one for the AI and one for
the player, and a body could only use the one it was born to. `give_ranged`
now calls `equip_ranged` from the same template; the slot stays as provenance.

### The rest of the family

Everything below read slot 0 where it meant *the actor being played*. Each was
found by asking the owner's question of one system at a time.

- **the rescue** put the *character* back on their feet when a driven hire
  went down -- charm spent, run continuing, and the body you were playing
  still on the floor;
- **Rally** healed the five hires and buffed slot 0, so the actor you were
  fighting with got neither;
- **arriving on a floor** dropped the party around slot 0, so descending while
  driving a hire scattered everyone around a character standing elsewhere;
- **a new hire and a built golem** appeared next to slot 0 rather than next to
  whoever paid for them;
- **the smith's party bonus** read slot 0's upgrade counters, so upgrades
  bought while driving a hire benefited nobody -- it is the best in the party
  now, since the smith went to that weapon whoever was carrying it;
- **the Tavern's yardstick** scaled new hires against slot 0 rather than
  against the peer actually hiring them;
- **a spell's line of fire** and **the fuzzer's drink rule** both watched
  slot 0.

`grep -c 'party\[0\]'` across every gameplay file is now **0**. It survives
only in `save.c`'s sanity check, where slot 0 means "the slot that always
exists", and in `classes.c`, where `apply_class_to_player` means slot 0 on
purpose.

### The pattern worth naming

None of these was a hard bug. Every one was a **sentinel or a shortcut that
was true under the old design and silently false under the new one** -- and
none of them could be found by the suites, the sanitiser or the warning scan,
because each is a correct program doing the wrong thing.

They were found by taking one sentence -- *everyone should be able to do
everything that the class allows* -- and asking it of each system in turn.
That is a better instrument than any of the automated ones for this class of
defect, and it is the third time in three sessions that playing the game has
out-performed the harness (§91, §92, and now this).

### Verified

Both backends clean, three suites at 0 failures with two new tests
(`test_everybody_has_a_school`, and the golem's "does not cast" restated as an
empty spellbook rather than a missing school), the warning scan still at
exactly 3 `Biome` sign-conversions, ASAN with zero reports, `TESTFORCE` empty.

A roster audit is the direct evidence: over 20 candidates, **0** with an
invalid school, **0** with no innate ability, **0** with no attribute sheet,
and **0** marksmen or artificers without a usable ranged weapon.

---

## 94. The UI takes the actor

Stated by the project owner as a rule: *the UI is agnostic, it should be able
to handle an active hero.*

After §92 every screen in `render.c` already drew the right body -- they all
called `hero_driven_c(p)` and got the correct answer. So this changed no
behaviour at all. It is worth writing down anyway, because the *shape* was
still wrong in the way that keeps producing bugs in this project.

**A convention can be forgotten; a parameter cannot.** Fifteen screens each
independently reaching for the active actor was fifteen chances for the
sixteenth to reach for slot 0 instead -- and several of them already had, which
is what §93 spent its afternoon undoing. Nothing in the signature said which
body a screen was about, so nothing stopped it choosing wrongly.

Every screen now takes the body it is drawing:

```c
void draw_shop_armory(const Player *p, const Hero *actor);
void screen_character_sheet(const Player *p, const Hero *actor);
void draw_game_screen(const Map *m, const Player *p, const Hero *actor, ...);
```

`p` is what the party shares -- the purse, the pack, the quest, the depth.
`actor` is the body -- its bars, its sheet, its spells, its gear. The same
line drawn everywhere else in the rebuild.

`grep -c 'hero_driven\|party\[0\]\|hero_mc' src/render.c` is now **0**. The
renderer has no way to ask who is active; it is told.

### Two screens stopped needing the party

`screen_minimap` and `draw_spell_menu` dropped their `Player *` entirely --
once the body is passed in, a minimap is a map and a position, and a spellbook
belongs to whoever is holding it. That is the useful signal from an exercise
that changed no behaviour: **when the split is drawn in the right place, some
things turn out to be on only one side of it.**

### And one real defect, found by the compiler

`render.h` declared `draw_gladiator_school` twice, with two different comments,
and `ATTR_NAMES` twice with the same one. Both had been there long enough to be
invisible. Changing the signature made the duplicate a conflicting declaration
and the build pointed straight at it.

### Verified

Both backends clean, three suites at 0 failures, the warning scan at exactly 3
`Biome` sign-conversions, ASAN with zero reports, `TESTFORCE` empty. No
behaviour was expected to change and none did.

---

## 95. Four asks and two bugs, all of them the same shape

Asked for: upgrade armour at the smith the way you upgrade a weapon; more
platinum; hires able to use a trinket. Reported while that was in hand:
auto-explore stopping when two `@` are near each other, and the right-hand
border going missing.

### "I want to update my armour, like I do to my weapon"

The armour upgrade was there the whole time, on `v`, handled correctly. The
screen was **41 rows long** and the smith's two lines landed at rows 35 and 36.
On the ~30-row terminal this project is actually played on -- large fonts, see
ROADMAP 2.1e, which has "walk through the shops at the minimum size" as an open
item -- both were below the fold.

A feature you cannot see is a feature you do not have. Weapons and armour are
the same shape and the same length, so they now sit side by side, and
accessories pair up the same way: 41 rows down to 28, with the smith at rows
23-24 and visible on any terminal the game will start on.

### "Raise the drops of platinum"

The real defect was worse than a rate. **The smith demands platinum from +20
(`UPGRADE_PLATINUM_FROM`) and platinum did not drop until floor 41.** So the
rung that needed it arrived long before the floor that produced it, and the
only way past +20 was to reach floor 41 on upgrades the smith would not sell.

`material_tier_for_floor`'s own comment said "Ruins and Wastes", and the Ruins
begin at floor 36 -- the number never matched its description. The cuts now
follow the biome bands (36 and 86, against `get_biome_for_floor`'s 35/85), and
the suite asserts them *against the biome function* rather than against numbers
written down twice.

The yields went up too, where going out of your way should pay: a mined vein
2-4 -> 4-8, a stripped wreck 3 -> 6.

**And the invariant that was missing is now pinned**: a material the smith
demands must drop somewhere. That is the test that would have caught this.

### "The heroes should be able to use a trinket"

They could wear one -- `equip_accessory` targets whoever is being played -- and
it did nothing, because there were still **two damage models**.
`companion_strike` rolled `base_atk + atk_buff + party_atk_bonus` and stopped:
no weapon bonus, no set bonus, no stance, no crit. Crit is where the
accessories land, through `effective_stat`'s ring and trinket terms. So an
entire equipment slot worked for one actor and was decoration for the other
five.

One `hero_damage_roll()` now, used by the player's swing and the AI's alike.

### The two bugs

**Auto-explore stopping when two `@` are near each other** was mine, from the
six-actor rebuild. I enforced one-actor-per-tile by *refusing* the step. The
path searches only know about terrain, so any delegated walk eventually picks a
square an ally is standing on -- refused, no turn spent, nothing changed, and
the stuck-detector fires. Allies swap places instead: it keeps the invariant,
always makes progress, and is what two people edging past each other in a
corridor do. The rule now lives in one named function, `party_displace_into()`,
used by both movers, with a test.

**The missing right border**, which took a screenshot to find and was an
off-by-one, not a clipping problem at all.

The sidebar blanks its region every frame -- a fix-the-class change so that no
panel line has to remember to pad. It computed the region as:

```c
int sb_w = sb_cols - sb_x;          /* columns sb_x .. sb_cols-1 */
mvhline(r, sb_x, ' ', sb_w);
```

`sb_cols - 1` is the frame's right-hand border. So the panel erased it, every
frame, on exactly the rows the panel covered -- **which is why the border
survived on the two rows above the panel and vanished from the first line of
it downwards.** That detail is in the screenshot and it is what identified the
culprit: a clipping bug would have eaten the border where the *text* was long,
not in a clean vertical run starting at the panel's first row.

The other three region-clears in the file all already stopped a column short.
This one, the one that runs every frame, did not.

I had guessed twice before that (the gold field, then the clipping sweep) and
been wrong both times, on the reasonable-sounding theory that something was
*overwriting* the border. Nothing was. It was being deliberately blanked by
code whose job is blanking. **Reading the symptom precisely -- which rows,
not just "the border is gone" -- was worth more than either guess.**

The two changes made while guessing are both still worth keeping on their own
terms, and are recorded honestly as what they are rather than as the fix:

- the shop header printed `"Gold: %d"` unpadded at a fixed column with the HP
  bar starting at column 16, so an eight-digit purse ran the number into the
  bar (`Gold: 57408340HP [====`, visible in the owner's paste) and a ten-digit
  one -- the bank's ceiling is two billion -- wrote over its bracket;
- every content print in `render.c` now goes through `mvprintw_clip`, which
  reserves the final column. 178 calls converted; the 9 left are the pre-frame
  "window too small" fallback, the frame's own title, and `draw_bar`'s
  internals, all of which place deliberately. Nothing that draws *content* can
  reach the border column now, on any screen, with any values.

That second one is a real hardening and it would have caught a whole class of
this, but it would never have fixed the reported bug, because the reported bug
was not content.

### The pattern, again

Three of these five were a thing that was true under an older design and
silently false under the current one: platinum's floor, the trinket's damage
path, the armory's row budget. The other two were mine, from this session.
None was findable by the suites.

### Verified

Both backends clean, three suites at 0 failures with two new tests
(`test_allies_displace`, and the material invariant), the warning scan at
exactly 3 `Biome` sign-conversions, `TESTFORCE` empty.

---

## 96. A bug hunt, and the one that ends runs

Asked for a hunt across the systems. The lens that has worked all session:
*what was true under the old design and is silently false now.* Every finding
below came from asking that of one system at a time, and each was **measured on
a probe before it was touched** rather than argued from the code.

### The one that matters: a hire could kill the Warden and void the run

The AI had **five** ways to hurt a monster -- a strike, a spell, a shot, a
blast, an ability -- and not one of them went through `monster_take_damage()`.
Each did `mo->hp -= dmg` and its own small pile of bookkeeping. So a monster
killed by a hire produced:

- no gold for the party,
- no bounty progress (a CLEAR contract could be stalled forever by your own
  party clearing the floor for you),
- no larder, no set-piece drop,
- **and no boss kill.**

`STATE_WIN` is reached only through the flag `monster_take_damage` sets, and
nothing else ever looks. So a hire landing the final blow on the Warden left it
dead on the floor with the run still running and nothing left to fight.

Probe, before: `3 of 3 confirmed`. After: `0 of 3`.

The fix is the same shape as everything else this week. `monster_take_damage`
takes the **killer** now: the tally and the experience go to the body that
landed it, the gold, the bounty, the larder and the boss flag go to the party
whoever swung. One `companion_hurt()` routes all five AI paths through it. A
kill landed while the world is moving leaves `p->boss_felled` for the turn loop
to act on, because an actor under AI has no `GameState` to write to.

**A second levelling curve went with it.** `companion_grant_xp` -- proportional
gains for hires against flat ones for the character -- had no callers left once
the kill paths merged. Deleted. Every actor levels on one curve now, which is
what "a hero bought at the Tavern is the same thing as the one the run started
as" means when it reaches the code. It is a real change in what a high-level
hire is worth and it wants measuring.

### The character was invulnerable while you played somebody else

`companion_at()` scanned slots 1..5. Monsters find an adjacent body to hit
through it, and `monster_attack_player` only ever targets whoever holds the
controller -- so with the human driving a hire, **nothing in the temple could
touch the character.** It now means "any body that is not the driven one", over
all six slots.

Its mirror image: `companions_place()` also ran 1..5, so slot 0 was only
repositioned by being the one who took the stairs. Descend while driving a hire
and the character kept the *previous floor's* coordinates -- measured landing
them inside solid terrain, unreachable and unable to walk out.

### And the defensive twin of the damage split

Making the character hittable exposed the other half. An undriven body defended
with `c->base_def` alone -- no armour bonus, no set pieces, no stance -- while
the driven one used `hero_eff_def()`. Symmetric now, the same way
`hero_damage_roll()` made the attack symmetric.

### What was checked and found clean

Worth recording, because a hunt that only lists hits is not a hunt: status
effects, the black market, healing draughts, the shared purse, and the
save/load round trip (including `controlled` and every hire) all correctly
follow the body being played. The earlier sweep to `hero_driven` covered them.

### A build defect the tests found

Adding two test functions broke the **link**, not the tests:

```
32-bit RIP-relative reference out of range (displacement=2178169242, max +/-2GB)
```

`sizeof(Map)` is 65 MB and the suite declared a `static Map` per test function.
Thirty of them is 1.96 GB of BSS; thirty-two is over the linker's range. The
tests added this session share one scratch world instead. The other thirty are
left alone -- they work, and converting them is a change with no behaviour in
it -- but the headroom is now about two more test functions, and that is worth
knowing before the next one is written.

### Verified

Both backends clean, three suites at 0 failures with two new tests
(`test_ai_kills_count`, `test_no_body_is_special`), the warning scan at exactly
3 `Biome` sign-conversions, ASAN with zero reports, `TESTFORCE` empty.

**Not measured: balance.** Three of these changes move numbers -- kills landed
by hires now pay the party, hires level on the character's curve, and undriven
bodies both hit and defend with their full kit. Every one makes the party
stronger. Nothing in §83 or §89 was taken against any of it.

---

## 97. The list from the review

§96 ended with a list of things I was not happy with. This is that list, done,
excepting the two the project owner deferred: the 600,000,000 testing purse and
the balance re-measurement.

### The defence pipeline, which was still half a pipeline

§96 gave an undriven body its real defence *value*. It did not give it the
rest. `monster_attack_player` had the unavoidable-crit rule, evasion, warding
and the status roll; `companion_take_damage` had a flat subtraction. So a hire
never dodged, never warded, was never poisoned or stunned or slowed, and could
not be crit -- and from the moment the character could be undriven, none of it
applied to them either.

`monster_hit_hero()` now, for any of the six. Measured after: an undriven hire
with 100% evasion is untouched in 187 of 200 attacks (the remainder is the
unavoidable crit, which is the point of it), the same for warding, and a hire
can be poisoned.

The guardian angel's levers stay pointed at whoever the human is playing, and
that is deliberate rather than an oversight: the angel exists to keep *the
person holding the controller* in the run, and softening a blow aimed at an
AI-driven body spends it on somebody who cannot notice.

### One animation system, which was the thing blocking one spell system

The AI could not call `cast_spell_slot()` or `fire_ranged()` -- the real spell
and the real weapon -- because those animate immediately and five bodies
animating in sequence every turn made the game crawl. That is a true and
long-standing constraint, and the response to it had been to reimplement both
inside companions.c.

The batching was the whole obstacle, and it lived in the wrong place: inside
companions.c as its own private AnimShot array. It is the renderer's now.
`anim_batch_begin()` / `anim_batch_end()` make `anim_bolt`, `anim_flash_cell`
and `anim_burst` collect instead of play, so the AI can call exactly what the
player calls at exactly the same cost as before. companions.c's parallel
animation buffer is deleted.

**The swap itself is not done, and that is deliberate.** Routing the AI onto
`cast_spell_slot` changes what a hire's spell does -- the companion version
scales on `base_atk/3` and ignores `spell_power_pct`, which hires now have. It
is a balance change wearing a refactor's clothes, and it belongs with the
measurement rather than ahead of it. The blocker is gone; the change is a small
one when somebody wants to make it.

### `boss_felled` moved out of saved state

It was a field on Player: turn-scoped news, living in run state that gets
written to disk, where it could only ever be false at the moment of writing.
Now a module flag in companions.c behind `companions_boss_felled()`, which
clears on read. `SAVE_VERSION` stays at 41 -- the layout moved twice and 40
names no shape this build can read.

Worth recording how the version got wrong in the first place: the field was
added and the version was not moved. The `sizeof()` guard in the header would
have refused the mismatched saves without ever saying why. **The guard catches
the corruption; the rule catches the confusion.** Both are needed.

### The test suite's two-gigabyte ceiling

`sizeof(Map)` is 65 MB and the suite declared a `static Map m;` inside nineteen
separate test functions -- distinct objects, never alive at the same time, 1.2
GB of BSS. It put the binary close enough to the linker's 2 GB range limit that
adding two test functions broke the *link*:

```
32-bit RIP-relative reference out of range (displacement=2178169242)
```

which looks nothing like its cause. One shared scratch world now, reset at each
of the old declaration sites so every test still starts from a zeroed map.
**904 MB, down from 2.03 GB**, and room for roughly sixteen more test functions
rather than two.

### And the screens, which nobody could look at

ROADMAP 2.1e -- "walk through the shops at the minimum size" -- has been open
since it was written, and the reason it stayed open is that there was no way to
see a screen without a human at a terminal. Every layout claim in this project
has therefore been arithmetic, and arithmetic is what said the armory was fine
while the smith's two lines sat six rows below the bottom of the screen.

`tools/screenshot.c` and `make shot` draw any screen at any size into a real
curses buffer and read the cells back. With `all` it walks every non-blocking
screen and reports the ones that overflow or lose the frame's right border.

The first run found what arithmetic had not: the armory's prices did not line
up, because names longer than the `%-18s` field pushed them right. Widened to
the longest name in each table. It also confirmed, by looking, that the border
fix in §95 works -- every row on every screen keeps its `|`.

**Twelve screens, at 24x80 and at 30x100: all fit, all keep their border.**
Three of them -- armory, ranged, arcanist -- sit on the last usable row at
24x80, so they have no headroom at all. That is worth knowing before a line is
added to any of them, and it is the kind of thing this tool exists to say.

Only the `draw_*` screens are covered. The `screen_*` ones run their own key
loop and block -- which the tool discovered by hanging, and which is now
written down in it.

### Verified

Both backends clean, three suites at 0 failures, the warning scan at exactly 3
`Biome` sign-conversions, ASAN to completion with zero reports at floor 7,
`TESTFORCE` empty, and the screen sweep clean at both sizes.

**Still not measured: balance.** Nothing in this section moves numbers except
the defence unification, which makes every undriven body harder to kill -- one
more entry on a list that is now long enough that the ladder measurement should
come before anything else.

---

## 98. DDA can see the floor you die on

The ROADMAP's "shape of the descent" proposal wants each of the five biome
bands to do something *for* the player, tuned against `dda_pressure`. It also
names, in its own words, the blocker to fix before designing anything on top:

> `dda_floor_end()` is the only place pressure moves, and it is called when a
> floor is **left**. A run that dies on floor 1 never leaves it, so DDA reads
> nothing and an invisible bonus riding on it can never fire -- for precisely
> the runs that need it most.

Which per §86 is the *median* run in all three hard modes. A help-the-
struggling-player system built on that would have helped exactly the players
who were already going to be fine.

### The fix, and the shape of it

`dda_floor_so_far()` reads how the floor underfoot is going, from the signals
that already existed -- current health against the snapshot taken on arrival,
draughts spent, whether the player has turned round. `dda_pressure()` is now
the settled history *plus* that. `dda_settled()` is the history alone.

Recomputed on demand rather than accumulated, so it cannot double-count with
the verdict `dda_floor_end()` files for the same floor.

**It only ever reads negative**, and that is the load-bearing decision. A floor
going badly says so at once; a floor going well says nothing until it is
finished and the verdict can see how it actually ended. That is the module's
own principle -- quick to forgive, slow to punish -- applied to *time* as well
as to size, and it is what stops a strong opening on a floor you are about to
die on from reading as "this run is coasting".

Measured on a probe. A character mauled on floor 1, having left nothing:

| state | old | now |
|---|---|---|
| arrives untouched | 0 | 0 |
| down to 40% | 0 | -8 |
| down to 12%, about to die | 0 | -14 |
| healthy mid-floor | 0 | 0 (silence, not credit) |

### Two things it turned up

**`dda_label()` described a different number from the one it labelled.** It
read the raw field while the gauge next to it read the total, so it said "even"
at -14. A label that disagrees with its own gauge is worse than no label.

**The simulator would have reported roughly double the movement.** It samples
immediately after `dda_floor_end()` files a verdict -- and at that moment the
live reading is still looking at the same floor's low health, so the total
counts it twice. It reports `dda_settled()` now, which is what that line has
always *meant*. Left unfixed, the first thing anybody measured on top of this
would have been the instrument.

That is the §68 lesson arriving on schedule: the harness is the first suspect,
and the moment a measurement changes, the thing that reports it has to be
re-read rather than trusted.

### What this does not do

Nothing consumes pressure yet. That was true before and it is still true --
phase one is measurement, deliberately, so that the band gifts can be tuned
against numbers rather than guesses. What has changed is that the numbers now
exist for the runs the proposal is actually about.

### Verified

Both backends clean, three suites at 0 failures with a new test, the warning
scan at exactly 3 `Biome` sign-conversions, `TESTFORCE` empty, the simulator
builds warning-free.

---

## 99. The five bands give something back

The ROADMAP's proposal, in the project owner's words: a hundred floors in five
bands, each with an environment that helps the player in its own way, and the
first band's job is to *present the world and make the player want to continue*
-- not to filter them.

Three quarters of it already existed. The bands were there; `dda.h` was there
and measuring; what no band had was a **mechanical gift** -- something the
environment does *for* you rather than *to* you. That is what this is.

§98 removed the blocker. This is the thing it was blocking.

### The three decisions, made and written down

The ROADMAP said these needed deciding once. The project owner decided them:

**Invisible.** Nothing is announced, nothing appears on any screen, no number
moves that a player can read. `grep log_msg src/bands.c` is empty and that is
the specification, not an accident. `dda.h` argued the case and it is right:
DDA that swings hard reads as the game cheating.

**The bands stay 15/20/25/25/15.** Not five twenties. The short first band gets
you out of the introduction quickly and the long middle is where a run lives;
more to the point it is what every measurement so far was taken against, and
moving the cuts would move relic sets, monster rosters, material tiers and the
boss floors with them.

**Asymmetric, and one knob.** A gift appears when pressure is negative and
never becomes a penalty when it is positive -- a player coasting gets the game
as written. `BAND_MAX_HELP` is the only constant, and each band expresses it in
its own units, because EVALUATION §73's rule is that a bonus chosen per band,
per difficulty and per stat is five numbers somebody picked and a finding they
authored. One number can be swept and reported as a curve.

### What each band gives

| band | the place | the gift |
|---|---|---|
| Roots | growth that feeds you | 1-2 hit points a turn |
| Works | machinery still under power | aether and ammunition return faster |
| Ruins | wards that still hold | up to 12% of blows turned aside |
| Wastes | salt that preserves | up to 40% chance a draught is not spent |
| Abyss | dark that hides as it blinds | noticed 1-4 tiles later |

Every one is a property of the *place*, readable as flavour rather than as
charity. The Abyss's is the one worth pointing at: it is the band that
suppresses sight, and the gift is that same dark working for you instead of
against you.

### The defect the first cut had, which is the interesting part

Written and probed, the whole system was **inert for the exact case it exists
for.**

A run dying on floor 1 has no settled pressure -- it has not finished a floor.
All it carries is §98's live reading, which bottoms out at `DDA_ROUTED` (-14)
against a scale that runs to -40. Floored division made that `help = 1`, and
two of the five gifts divide help by two, so they rounded to nothing. The band
whose stated job is to catch a floor-1 death caught nothing at all.

Measured before, at 12% health on floor 5: `help 1, regen 0`. After rounding
up: `help 2, regen 1`.

It is the same lesson as §98 one layer along. A system can be correct in every
line and still be worth nothing at the value that matters, and the only way to
find that out is to ask it for a number at that value. The probe took a minute;
reading the code had already convinced me it was fine.

### An API note worth keeping

`bands.h` takes no `Map`. mapgen sets `m->biome` from
`get_biome_for_floor(floor_num)` and nothing else ever writes it, so the band
is a function of `p->floor` -- which is what lets these be asked from the
places that spend a draught and never had a `Map` to hand. Worth checking that
invariant still holds before anybody gives `Map` a biome of its own.

### What this is not

**Not measured.** `BAND_MAX_HELP` is 4 because it is a small number, and the
per-band units are each the smallest step that is a step at all. Neither has
been swept. Set it to 0 to turn the whole system off, which is the control any
measurement of it needs, and sweep it before trusting the value -- exactly as
`GUARDIAN_MAX_GRACE` demands and for the same reason.

This is one more entry on a list of unmeasured changes that is now long enough
that the ladder measurement should come before anything else on the ROADMAP.

### Verified

Both backends clean, three suites at 0 failures with a new test that pins all
three decisions as properties, the warning scan at exactly 3 `Biome`
sign-conversions, ASAN to completion with zero reports, `TESTFORCE` empty, and
the screen sweep still clean at 24x80.

---

## 100. Phase 3 content: a rule, a proc, three bounties

Three ROADMAP Phase 3 items, in the order they were built.

### A rule per biome

Phase 3's complaint was that biomes "differ only in monster roster and hazard
mix mechanically", and it named three of the five rules it wanted. They are in
`bands.h`, next to §99's gifts and deliberately so: each rule is its band's
gift pointed the other way, which is what makes a band one thing rather than a
bag of effects.

| band | the rule |
|---|---|
| Roots | growth hides -- two tiles off *everyone's* notice, yours and theirs |
| Works | machinery gates -- already shipped, see below |
| Ruins | 12% of a monster's blows turned aside by its own old ward |
| Wastes | the salt bites: +3 on every hazard |
| Abyss | sight cut by 3, floored at 4, and **halved by Aether-Sense** |

The Abyss's is the one Phase 3 asked for by name -- "suppress FOV so
Aether-Sense matters" -- and the attribute is what answers it, which is the
whole point. The floor is not decoration: a rule that makes the map unreadable
at 1400x800 is a bug, not a difficulty (ROADMAP 3.1, rule 2).

**The Works' rule already existed.** `maybe_place_lever_vault()` has run levers
at 35% on Industrial floors against 15% elsewhere since it was written, which
is exactly the "gates routes behind more lever/machine puzzles" being asked
for. A second mechanism would have been two dials on one thing, so there is
none. That is the **fourth** time this session the answer to "build this" was
"most of it is already there", and it is worth making a habit of looking first.

### A proc per completed set

Deferred twice, correctly, while the damage model was in doubt -- flat bonuses
can be reasoned about on a sheet, a proc cannot. The model is measured and
regression-guarded (§14), so they are now safe to hang off it.

| set | on a hit |
|---|---|
| Root-Drowned | venom: attack penalty, refreshed while you keep hitting |
| Company | machinery: a blow that staggers |
| Ward-Touched | a pulse that ignores armour -- and, defending, turns a blow aside outright |
| Salt-Bitten | corrosion: permanently eats a point of that monster's guard |
| Abyssal | the killing dark: a heavier strike out of nothing |

`SET_PROC_PCT` is the one knob, for the same reason `BAND_MAX_HELP` is. Zero
disables every proc, which is the control.

They ride on `hero_damage_with_procs()` rather than on any one attack, so a
proc fires for a spell, a shot and a swing alike, and for all six actors --
because the roll underneath is the shared one. That is the §96 unification
paying for itself: the feature was written once and arrived everywhere.

**Two pieces pay a number; four pieces do a thing.** That is the point. A flat
bonus is never a reason to hunt the last piece of a set rather than wear the
best four things you happen to have found.

### Three more bounties, and the blocker that cleared itself

`QUEST_TIMED` (be there before the clock), `QUEST_NORECALL` (get there without
cracking a charm), `QUEST_ESCORT` (get somebody else there alive).

The first two were "no new machinery" exactly as predicted: a deadline stored
as an absolute turn, and a flag set in the one place a charm is spent.

**Escort had carried "blocked on a friendly-NPC actor that does not exist"
since Phase 3 was written, and nobody ever unblocked it.** It stopped being
blocked as a side effect of the Hero refactor: once there is one kind of body
and a constructor that can make one without a roster seat, a client is a party
slot and a bounty field. There is no client struct and no client code path --
they walk, fight and die under the same rules as anybody else.

**The quest system moved into `src/quests.c`.** Not tidiness: the test suite
links every module except `main.o`, so quest logic living in main.c could not
be tested at all. Given how much of this session was spent on defects that only
play could find, leaving a brand-new system untestable was not defensible.

Two bugs the tests then caught immediately, both about the client:

- **an escort offered only 5 times in 400.** Clients were never released, so
  after five bounties the party was full and every later escort fell back to a
  kill bounty. Now 64 in 400, in line with the rest.
- **a dead client kept its party slot for ever.** The failure path forgot them
  --- alive and following you with no bounty attached, or dead and occupying a
  seat nobody could fill. Every exit now goes through one `release_client()`.

### And a performance finding, which is not Phase 3's fault

`--silentrun` at seed 7 (Swarm, the Shaft) has got substantially slower per key
across this session's later work: **115s for 20,000 keys** when measured at
§92, against **203s for 8,000** now. Same seed, same difficulty draw, same
world, comparable depth.

It is **not** the band gifts or the set procs. Measured directly by setting
both knobs to zero and running the same seed: 241s and floor 8, against 203s
and floor 7 with them on. The run with the features *off* was slower, because
it survived one floor further -- which says the wall-clock tracks depth and
crowd size rather than these features.

The leading suspect is §97's defence unification. Every body now rolls the
unavoidable crit, evasion, warding and a status chance on every blow it takes,
where an undriven body previously took a flat subtraction. In Swarm, with
thousands of monsters and six bodies, that is a great many more rolls per turn.
It is correct, and it may simply be what correct costs -- but it has not been
measured and it should be, because it is the difference between the sanitiser
build finishing a run and not.

**Recorded rather than fixed.** Guessing at a hot path is how §91's per-turn
map repaint got written, and the instrument to settle it -- a controlled
before/after on one change at a time -- is the same instrument the ladder
measurement needs. It belongs in that pass.

### Verified

Both backends clean, three suites at 0 failures with three new tests, the
warning scan at exactly 3 `Biome` sign-conversions, `TESTFORCE` empty, the
screen sweep clean at 24x80, ASAN with zero reports across the runs that
finished. `SAVE_VERSION` 42.

---

## 101. Guarding the data, and one ability worth spending

Two small ROADMAP items, both explicitly invited.

### 2.6 -- the data tables now have a suite

The harness had never looked at data. Every existing test drives *behaviour*:
the damage model, hazard predicates, compaction, the ladder. Nothing asserted
that the tables those systems read were well formed.

That gap has a specific shape, and this session made it concrete. **A table
entry with a wrong field is a correct program doing the wrong thing** -- it
compiles, it runs, it never crashes, and it stays invisible until a player
walks into the one screen that reads it. A `magic_school` of -1 was exactly
that: legal, deliberate under an older design, and silently "the guild has
nothing to teach a ? caster" once a hire could be the body you were playing.

Three suites, the three the roadmap named:

**The spell pool.** Every spell belongs to a real school, has a level in 1-5,
an effect the caster can resolve, a name, and a charge cost, cooldown and
price. The guild reads the pool through `spell_school_count` and
`spell_school_index`, so those must partition it exactly -- a school that
indexes a spell belonging to another school is a menu offering something
nobody can buy. And every school must have a level-1 spell, or a character
assigned to it starts with an empty book and no way to fill it.

`EFFECT_COUNT` was added to make the effect check possible at all. Without a
count nothing could say whether a table entry held a real effect or a typo,
which is the precise class of defect this suite is for.

**The class table.** Names, taglines, archetypes, starting kit, and -- the one
that is not cosmetic -- that every attribute-override list is terminated with
its `ATTR_COUNT` sentinel, since the list is walked to it. Plus: every
archetype has at least one class in it, because the Tavern picks a role and
*then* a class inside it, and `archetype_class_index` returning -1 for an
empty role builds a hire out of class 0 wearing somebody else's archetype.

**The generators.** 40 rosters of Tavern names checked against both the buffer
they are stored in and the column they are shown in -- `TAVERN_NAME_SHOWN`
exists so a hero's honorific is never the part that gets cut off. Every monster
`make_monster_for_floor` produces across all 100 floors, and every bounty
target name.

**Nothing was found.** 180 spells, 100 classes, 7 archetypes, 40 rosters, 100
floors of monsters: all clean. That is the good outcome and it is worth saying
plainly rather than dressing up -- the tables were already right, and they are
now guarded against the next edit.

### 2.1b -- auto-explore spends the class ability

The hunter has used melee, spells and ranged weapons since it was written, and
not the class ability. The roadmap left that as a judgement rather than an
omission: an ability on a long cooldown spent on a rat is worse than not
spending it.

**Elites and bosses only**, which is the answer the roadmap proposed and the
only one that survives its own objection. Placed *above* the spell and the
shot, because the order of that function is "spend the thing that is hardest
to replace first, while the target is still worth it" -- an ability returns on
a cooldown and a boss does not return at all.

Measured in a real run rather than argued: 6,000 keys at seed 11 produced
**12 ability uses against 6,472 combat events** -- 0.19%, which is what "only
on the rare thing" looks like from outside. No refusals came from this path,
since it checks the cooldown before calling.

That is evidence rather than proof. `autoexplore_fight_step` is static in
main.c and the suite links every module *except* main.o, so it cannot be tested
directly -- the same structural gap that made the quest system worth moving to
`src/quests.c` in §100. Auto-explore is far more entangled with main.c's
helpers than the quest board was, so it is written down here rather than
half-moved at the end of a long session.

### Verified

Both backends clean, three suites at 0 failures with three new data suites, the
warning scan at exactly 3 `Biome` sign-conversions, `TESTFORCE` empty. Test BSS
904 MB against the linker's 2 GB, so there is still room for roughly sixteen
more test functions.

---

## 102. The hunter comes out of main.c

ROADMAP 2.6 left one thing open after §101: `autoexplore_fight_step` and the
rest of the delegated walk are static in `main.c`, and the suite links every
module *except* `main.o`. So none of it could be tested.

That is not a hypothetical. **Both auto-explore defects this session found were
decisions, and both were found by playing**: a stuck-detector reading slot 0
while the walk moved somebody else (§92), and a step refused for standing next
to your own party (§95). Neither would have survived a test, and neither could
have had one.

### The seam

Auto-explore is two things bolted together: *decisions* -- where to step, what
to spend on a target, whether anything is happening at all -- and a *loop* that
carries them out, draws a frame and reads the keyboard.

`src/autoexplore.c` is the decisions. The loop stays in main.c, because it
needs the turn resolution, the renderer and the input queue, and dragging those
into a module to satisfy a header would move the tangle rather than undo it.

The fight decision is now a value rather than a side effect:

```c
AxFight autoexplore_decide_fight(const Player *p, const Map *m,
                                 const Monster *target, int dist);
```

`main.c` switches on it. The order inside it is the design -- melee if
adjacent, then the class ability on an elite or a boss, then a spell, then a
shot, then close -- and the order is now a thing a test can read back.

The three search wrappers came too. They are what "where should I step" is made
of, and a module that could not answer its own question would have been a
worse arrangement than none.

### What that immediately bought

Nine assertions that were impossible an hour earlier, including the two
defects restated as properties: *moving the driven body counts as progress and
moving somebody else does not*, and *the class ability is not spent on a rat*.
Plus the two-pass hazard rule, the hunt radius bound, and the backstop that a
delegated walk never steps onto the way back up.

### And a test that was nearly a duplicate

The connectivity check written to baseline a mapgen change turned out to
already exist: `test_stairs_always_reachable`, 540 floors. **Fifth time this
session that the answer to "build this" was "most of it is already there."**

It was extended rather than duplicated. It ran at whatever world size happened
to be set -- in practice one -- and a floor's structure scales by area, so the
sizes are precisely where a structural change to mapgen would break
connectivity without the small map noticing. It now sweeps the Shaft, the Halls
and the Deeps: **572 floors, every one crossable.**

That is the baseline any change to mapgen's structure should be measured
against, and it is in place before the change rather than after it.

### Verified

Both backends clean, three suites at 0 failures, the warning scan at exactly 3
`Biome` sign-conversions, `TESTFORCE` empty. `main.c` is 2,520 lines, down from
about 2,700 at the start of the session, with `autoexplore.c` at 232 and
`quests.c` at 234 -- both testable, where neither was.

---

## 103. Two districts, and the ground nobody could reach

Three things this session, and the middle one was not asked for by the roadmap.
`2.5` was closed by decision -- notcurses stays the opt-in build -- and then
§3.1's tranche: **the watch** (79 Stealth District, the last unbuilt entry on
that section's own shortlist) and **the barrow** (91 Gauntlet of the Fallen,
which the roadmap calls the best idea in the hundred). The project owner then
asked, as the districts went in, that *all* of them be re-evaluated for places
a player or the AI could get stuck. That request found more than the two builds
did.

### The instrument first

`tests/movement.c`, a fourth suite, asking one question no other suite asks:
**is there anywhere on a generated floor a body can end up and not get out
of?** Two shapes, and they are not the same shape:

- **ground you cannot walk to** -- districts are carved before any room is, and
  each is joined to the floor by two roads run to a single `district_anchor()`
  tile, so a district whose interior is cut into more than one walkable piece
  ships with the pieces the anchor is not in stranded;
- **ground that moves you** -- a belt or a current takes whatever stands on it
  every turn, which is the mechanic for a person and a trap with no floor for a
  *delegated* walk, which paths, gets carried off route, re-paths, and is
  carried again, forever, never tripping the no-progress detector because the
  position keeps changing.

It was written before either fix and it failed immediately on both, which is
the only reason the numbers below can be trusted.

### What it found, measured over 456 floors

| kind | stranded | instances losing >10% |
|---|---|---|
| the overgrowth | **15.9%** | **161 / 308** |
| the mist | 9.5% | 56 / 218 |
| the bog | 8.0% | 103 / 395 |
| the flooded quarter | 4.7% | 33 / 211 |
| all seventeen | 3.2% | -- |

More than half of every jungle ever generated was missing a piece of itself:
the monsters, the loot and the district's own rule, gone quietly, on floors
nobody would ever have reported as broken. `test_stairs_always_reachable`
could not see it -- it asks only whether the *way down* is reachable, and it
always was.

And `is_walkable_delegated()` excluded `TILE_CURRENT` and not `TILE_BELT`. The
comment above that function argues the case at length; it was written for the
current and never re-applied when the assembly line shipped the same idea with
a different tile. **The reasoning was right and had simply not been carried
forward** -- which is the failure mode a type-level assertion catches and a
map-level one does not, so the property is now stated over the tile types.

### The fix, and the threshold that was backwards

`district_relink()`: after a district is carved, flood from its anchor, and cut
a trail back to the anchor from every pocket that is cut off. Deliberately one
place rather than seventeen -- the eighteenth carve function would forget, and
none of them can see whether the blobs they scattered happened to close a ring
anyway. This is one of the few cases where a global guard is the honest answer
and not the lazy one: the property is global.

The pocket threshold started at 8 tiles, reasoning that a small gap behind a
thicket is the thicket doing its job and cutting a trail to it would erode the
exact thing the jungle exists for. **Measurement said that was backwards
twice.** It did not work -- the residue is *made of* small pockets, and at 8
the overgrowth still lost a tenth of itself on 62 instances in 308 -- and the
erosion argument runs the wrong way, because a two-tile pocket is by definition
adjacent to reachable ground and needs a one-tile trail, where an eight-tile
one needs a real cut. **Small pockets are the cheapest to open and the most
numerous.**

At 2: the overgrowth strands 3.9% and loses a tenth of itself on 4 instances in
308; all seventeen kinds together strand 1.3%. What is left is single tiles
nobody could stand in usefully, left on purpose -- a district with no
unreachable nook in it reads as a room.

It costs nothing. Deeps floor generation 45.7 ms -> 46.2 ms, and under
AddressSanitizer 187s -> 185s over 1,200 fuzzer keys, which is noise. Worth
stating explicitly given ROADMAP item -1: **this pass is not a contributor to
the per-key cost.**

### The watch

The one district in the game where the answer is not to fight. Its wardens are
awake -- which is what makes it the quiet quarter's opposite number rather than
its reskin -- but they are watching for trouble rather than hunting, so they
notice you at two tiles. Hurt one, anywhere in the district, by any hand, and
every warden knows for good.

The prize is a **permanent +5% to what the body that took it finds**, capped at
60%. Not a purse: §3.1's rule 3 is explicit that flat coin is flavour against
an economy where a full descent earns ~438,000 and a floor-100 build costs
~200M, and names a permanent % gold find as one of the two shapes a reward can
take if it is meant to matter. It grows linearly with how many watches you
walk rather than compounding like a bank share, which is what makes it safe to
pay out once per district. It lands on the *body*, not the party -- the purse
is shared and knowing where to look is not, which is the line take-control
already draws.

### The barrow

The arena's structure and a different question. Ten waves of whatever the floor
could produce, there; here, one wave per ghost, read off a roster of the runs
this save directory has actually lost.

`~/.aether_descent_fallen`, a ring of sixteen: name, class, level, the floor it
got to, the purse it was carrying, the statline. Deliberately a *summary* and
not a `Hero` written raw as everything else in save.c is -- a Hero is large,
it changes shape whenever the party does, and tying the roster to
`SAVE_VERSION` would have emptied every barrow in the game twice in this
session alone. A version mismatch discards the file instead of migrating it,
which is the opposite of the rule for a run save and the reason it is its own
file and its own format: losing the roster costs a few ghosts, losing a run
save costs a run.

The reward is the gold that ghost was carrying and nothing else. No set piece,
no writ, and above all no attribute point -- §3.1 states that rule four
separate times, because more than a third of its hundred candidates wanted to
hand out permanent points against a School that charges `200v^2 + 300` for one.
Ghosts are worth **no experience** either: killing what you used to be is not
training, and an XP source that scales with your own past best is a loop.

It is the only district whose *existence* depends on state outside the map. A
barrow with nobody in it is a room with piers in it, so until a save directory
has lost a run the kind does not come up at all.

### What the verification could and could not reach

The fuzzer generated both freely and could use neither. It crossed a watch 459
times in 40,000 keys and **broke 231 of them** -- a random-key player attacks
everything, so it reached the strongbox 18 times and every one of those was
after the noise. Barrows generated on 2 floors of the second run and it never
walked into one.

That is the districts working, not the harness failing, and it is also the
limit of what a fuzzer can say about a mechanic whose content is *restraint*.
The success paths are pinned in `tests/invariants.c` instead: a box taken
quietly pays and one taken loudly does not, one blow breaks the watch and wakes
the warden who did not see it, forty takings do not exceed the cap, and the
barrow seals, raises somebody off the roster by name with exactly that run's
purse and no experience, and opens again when it is cleared.

### Three faults in the suite, found by leaning on it

None of these were in the game.

**The suite was reading the player's home directory.** Two tests set
`AETHER_STATE_DIR` to a scratch path and handed it back with `unsetenv()` --
which does not restore the previous value, it removes it. From the first such
test onward, `state_dir()` fell back to `$HOME`. Nothing was corrupted, because
no later test happened to write; what it broke was quieter, and the barrow
exposed it. "Every district kind still generates" read the *developer's* fallen
roster, so it failed on a machine whose owner had never lost a run and would
have passed, for the wrong reason, on one whose owner had. There is one scratch
directory now and both tests restore it.

**A test was asserting on a coincidence.** `test_per_hero_vision` checked that
the character and a hire had seen a *different number* of tiles. That is not
the property. `comp_light()` unions every body's sight into the driven body's
memory on purpose -- its own comment says "anything any actor can see is told
to the active one" -- so the character's map is always a superset of a hire's,
and the counts only differed because the hire's wandering happened not to cover
the few tiles the character could see from where it stood. Adding two district
kinds reshuffled the weight table, floor 1 of seed 99 came out differently, the
hire's trail covered them, and a test that had never measured separation
started failing for a game that was entirely correct. It now asserts both
directions and means them: the driven body knows things the hire does not
(separation), and is told everything the hire can see (the union rule).

**And it never dropped the per-body maps.** `hero_vision_forget()` is what
`vision.h` says to call whenever the floor is replaced, and the test replaced
the floor without it, so the six maps carried whatever the previous test in the
process had left in them.

**The lesson is §68's, from the other end.** That rule says: when the suites
are green and the game is wrong, suspect the instruments' coverage. This is the
inverse and it costs just as much -- when a suite goes red under a change that
is correct, the fault can be in what the test was pinning. Two of the three
above would have been "fixed" by adjusting the game.

### Verified

Four suites at 0 failures from a clean state directory. The aggressive warning
scan at **exactly 3**, the same three `Biome` sign-conversions. No
AddressSanitizer or UBSan report at 600, 1,200 or 2,000 keys. Twelve screens
clean at 24x80 and 30x100. 80,000 fuzzer keys across two runs, no hang.
`SAVE_VERSION` is 43.

Nineteen district kinds. The stranded-ground figure for the two new ones is
0.71% (the watch) and 0.00% (the barrow).

---

## 104. Three bugs from play, and two instruments that could not see them

Reported from a session, in the project owner's words: auto-explore sometimes
says there is no path to the stairs, "which is impossible since there is
always a path"; the UI is wrong after visiting a shop; and the AI casts spells
that do no damage and "can get stuck doing that for minutes, not moving".

All three were real. Two of them were fixed as *classes* rather than as
instances, at the project owner's insistence, and that was the right call both
times.

### 1. "No known path to the stairs"

The floor was right and the message was right; the conclusion drawn from it
was wrong.

Both of auto-explore's searches walk only ground the player has revealed
(`path.h`'s `require_seen`), which is deliberate -- a delegated walk must not
route through a shortcut the player has no way of knowing about. So *known*
was doing real work in that sentence. But `autoexplore_route()` treated seeing
the staircase as a switch: once `m->tiles[stairs].seen` was true it took the
"go there" leg and, in an `else`, gave up the frontier leg permanently. Glimpse
the stairs across a room you have not walked to -- through a doorway, down a
lit corridor, over a bridge whose approach is still dark -- and the search
failed and the walk stopped.

"No known path" meant *the route has not been uncovered yet*, which is
precisely the situation exploring exists to fix. Seeing the stairs is
information, not an instruction, and it should never take a leg of the walk
away. The `else` is gone; the frontier search now runs whenever the other two
legs come up empty. The message that remains fires only when all three fail --
no loot, no route, no frontier -- which is a floor genuinely walked out, and it
says so.

### 2. The stripe down the side of the panel

Reported as the UI being wrong after a shop. In the screenshot it is one
character on each panel row, immediately left of the text: "eLevel", "cATK/DEF",
"gGold", "sRecall". The panel appeared to have grown a ragged extra column.

The play screen does not `erase()`. It repainted the map, the panel and the log
and trusted that between them they covered everything -- and the arithmetic was
wrong three separate times:

- the rows below a shortened panel, which showed as "tGold" and "mFloor" (§92);
- the two columns between the map's right edge and the panel's text, which is
  this report;
- the panel's half of the banner row, which nobody had reported yet.

Each was found by a player. Each was fixed on its own. **Three bugs of one
shape means the shape is wrong**, which is the project owner's call: *"this
happens because you clean parts of the screen... just clean all the screen."*
A screen that clears part of itself needs every part accounted for by
somebody; a screen that clears all of itself needs nothing accounted for at
all.

The long comment arguing against `erase()` -- that it defeats ncurses'
frame-to-frame diff and repaints the whole terminal, "most of the cost of a
step on a big window" -- did not survive being measured:

| window | region clears | `erase()` |
|---|---|---|
| 100x30 | 0.135 ms/frame | 0.139 ms/frame |
| 200x60 | 1.480 ms/frame | 1.518 ms/frame |
| 300x80 | 2.168 ms/frame | 2.175 ms/frame |

Between 0.3% and 2%, inside the run-to-run spread of the same variant.
`erase()` blanks the *virtual* screen; the diff against the physical screen
still happens, so what goes over the wire is still only what changed. The cost
the old note described belongs to `clear()`/`clearok()`, which forces a real
repaint. *"How much can I lose anyway"* -- a fortieth of a millisecond.

**And an instrument that answers is worse than no instrument.** `make shot`
had walked every screen for three sessions without seeing any of this, because
it calls `erase()` before each one -- it could not have caught a bug about not
erasing. It draws the play screen now, over a terminal filled with junk, and
asserts the result is identical to drawing it on a clean one. That property
needs no knowledge of where the regions are and fails against all three
versions of the defect above.

Two smaller things fell out. `make shot` had `$(GAME_OBJ)` as an *order-only*
prerequisite, so it linked whatever objects happened to be in `build/` and
reported on a binary that did not contain the change being tested -- which is
how the first A/B of this fix came back clean. And the first version of the new
check dirtied the screen with `draw_shop_general()` and passed against the very
bug it was written for, because at 100 columns that screen does not reach the
gutter. Filling the terminal is what asks the question.

### 3. The hire that stood there cursing

The one that took longest, because the obvious fix was correct and changed
nothing.

`companion_cast()` chose a spell with a usefulness test whose last branch was
an `else` commented `EFFECT_WARD_SHIELD`. It was not one. It swallowed STUN,
SLOW and CURSE as well -- all three of which `effect_fit_for_companion()`
allows a hire to know -- so their usefulness was answered by
`c->def_buff_turns == 0`, a field none of them writes. True on every turn,
forever. And the pick is weighted `level * 100 + magnitude`, so a level-5 curse
outranked a level-1 bolt and was the *preferred* move. `companion_cast()` runs
above `companion_fire()` and `companion_strike()` and `continue`s on success,
so the hire neither shot, nor swung, nor moved.

The fix is a switch with no catch-all -- every effect states its own condition,
control effects ask about the state they actually set, and anything unlisted is
not useful rather than silently useful -- plus priority bands so that staying
up beats ending the fight beats making it easier.

**The bands did nothing, and the reason is worth the paragraph.** They keyed on
`effect_is_offensive()`, and *that function counts stun, slow and curse as
offensive*. Which is a fair reading of the word and useless as a priority: a
rule saying "prefer the offensive spell" preferred the curse right back, and
the level-5 curse still won at weight 1555 against the bolt's 1107. Nothing
short of a print inside the loop would have found it -- both spells were being
considered and the arithmetic was doing exactly what it said. There are two
predicates now: `effect_is_offensive()` answers *needs a target*, and
`effect_deals_damage()` answers *ends the fight*, and the weighting uses the
second.

The decision moved into `companion_pick_spell()`, pure and declared in
`companions.h`, for the reason §102 split auto-explore's decisions out: a
choice made inside a turn loop cannot be asserted on. Two attempts to test this
from outside -- a hire with a curse-only book, then a synthetic 60-turn fight
-- both passed against the bug, because the casting was never what was broken.
**It was the choosing.** Against the pure function the test fails on four
assertions with the original code and passes on none of them by accident.

### Still open, and not mine

`Auto-explore: getting nowhere here -- stopping` was reported alongside these
and is **unchanged**: 696 firings in 15,000 fuzzer keys before this session's
autoexplore work, 707 after, which is the same number through different RNG.
It is the no-progress detector at `AUTOEXPLORE_STUCK_LIMIT` 60, and it is a
separate defect from the stairs message it was reported with. Ruled out so far:
the driven body's spell pick, which uses `spells.c`'s `effect_is_offensive()`
and is damage-only, so the §3 defect above does not reach it; and every branch
of `autoexplore_fight_step()`, each of which returns false rather than
reporting an action it did not take. It wants its own instrument and it did not
get one this session.

### Verified

Four suites at 0 failures from a clean state directory. Warning scan at exactly
3, the same three `Biome` sign-conversions. Twelve screens fit at 24x80 and
30x100 and the play screen covers itself at both. 15,000 fuzzer keys with no
"no known path to the stairs" at all.

---

## 105. The aqueduct, and where the time actually goes

Two items off the top of the roadmap, in order. One is fixed. The other is
measured, and the thing it was asking for turns out not to be possible.

### "Getting nowhere here" was the aqueduct

Diagnosed in one look at a log, after a session of guessing at it: **707 of
707** stuck reports had the same line immediately before them.

```
1378   | The current takes you 1 tiles.
1378   | The current takes you 1 tiles.        (x25)
1378   | Auto-explore: getting nowhere here -- stopping.
```

The project owner recognised it at once -- *"the aqueduct... some biomes push
it to left or right, and the AI normally gets stuck there, I sometimes have to
exit the auto explore to exit those areas."*

A body standing in a channel is moved `CURRENT_PUSH` tiles at the end of every
turn. `autoexplore_route()` is goal-directed and cheerfully picks a step along
or back into the channel; the current then undoes it, and the body ends the
turn on the square it started on. The progress signature is unchanged, sixty
times over, and the walk stops.

**The detector was right and the report was right.** `getting nowhere here` was
a true statement about a body that was, in fact, getting nowhere. The defect
was upstream of it, and it is the other half of a rule that was already half
written: `is_walkable_delegated()` stops a delegated walk *routing onto*
conveyed ground, and nothing got it *off* ground it was already standing on.
That is the same shape as the belt bug in §103 -- a rule stated once and not
carried through to the second case that needed it -- for the second time in
two sessions.

`autoexplore_escape_conveyance()` is the missing half, and it runs first, ahead
of loot, the stairs and the frontier: while the floor is moving you, no other
decision survives being made. 707 stuck reports in 15,000 fuzzer keys before,
**none after**, and current pushes fell from 78,078 to 8,017 -- the walk now
leaves channels instead of being shoved back into them.

Guarded in `tests/movement.c`: over 10,998 conveyed tiles, 10,366 can be
stepped off and every escape lands on ground that does not move you. The 632
that cannot are mid-channel between two banks, where being carried is the
aqueduct's whole point.

### Item -1: measured, and the bisect is impossible

The roadmap asks for the quadrupled per-key cost to be bisected "one change at
a time". It cannot be, and the reason took a correction to get right.

**First answer, and it was wrong: "this project is not under version control."**
It is. There is a repository with one commit, `d1a20b4`, from 2026-08-08 --
"working state before the tileset spike", made for exactly this reason and
saying so in its own message. The session-start check that reported no repo was
looking at the parent directory, and I did not verify it before writing it
down here. Corrected rather than quietly deleted, because the shape of the
mistake is the one this file keeps recording: an instrument answered, the
answer was believed, and nobody checked what it had been pointed at.

**The real reason is narrower and still fatal to the bisect.** The one commit
predates `--silentrun` (§81) and the Hero refactor (§90) alike, so it cannot
run the harness the claim is measured with, and there are **no revisions at
all inside the §92-to-now window** where the regression is supposed to live.
There is nothing to bisect *between*. Every performance claim in this file that
compares two sessions remains unfalsifiable after the fact -- not because
history is absent, but because it is one point and the claim needs two.

From this session there is a second commit, so the next regression is
bisectable. That is the whole of what could be done about it.

What could be done was done -- profile the present code and try to make it
cheaper. Sampled with inlining off, one auto-explore step is 7,481 samples, and
**3,827 of them are `autoexplore_route()`**, the overwhelming majority inside
`bfs_next_step_to_item()`. The loot search is the single most expensive thing
the walk does, by a wide margin. Nobody knew that.

Two ways to make it cheaper were implemented and measured, and both are gone:

| attempt | result |
|---|---|
| cache the *failure* -- stop looking until the floor changes | 741 turns vs 739 in 60s: nothing |
| cache the *destination* -- walk back to it with a directed search | 662 turns vs 749: **12% worse** |

The first does nothing because floors nearly always have loot on them, so the
flood stops early and the hopeless case is rare. The second loses because A* to
one specific square over seen-only ground costs more than a flood that meets
the nearest item almost immediately. **The flood is cheap precisely because it
usually stops at once**, which is the opposite of what it looks like.

Both are written into the comment above the call, so the next person does not
spend an afternoon rediscovering them. No change shipped: the code is back
where it started (753 turns against a 749 baseline) and the profile is the
deliverable.

**What item -1 needed was a second commit**, and it has one now. The next time
something gets slower there will be two points to draw a line between. Until
then "the per-key cost quadrupled" is a memory, not a measurement, and should
be read as one.

### Verified

Four suites at 0 failures, warning scan at exactly 3.

---

## 106. The structural leftovers, and one that was already done

Three items, and only one of them turned out to need building. The other two
were a stale note and a stale premise -- which is its own result, and the
reason the roadmap now says so in both places.

### The two movement functions were already one

The roadmap has carried "movement and the melee swing are still two functions,
`handle_player_move` for slot 0 and `companion_player_move` for a driven hire"
since §91, flagged as where the next bug of that shape would be. Neither
function exists. `actor_step()` replaced them during the six-actor rebuild, and
`companions.c` says so at the site where the second one used to stand.

`handle_player_move` is not in the source at all; the only two mentions of
`companion_player_move` are comments recording that it is gone.

### But the premise had leaked somewhere else

Looking for the duplicate turned up a live one, in the place §91's lesson
predicts: **the world acted on the driven body and not on the other five.**

`districts_tick()` moved the human's actor and every monster on the floor along
belts and currents, and left the other five party members standing in a running
channel as though the water were scenery. Written against `p->x` before there
were six bodies, and never revisited -- the same premise the Hero refactor
existed to delete, surviving in the one system that moves people without asking
them.

And underneath it, a second: `displace_actor()` refused to carry anything onto
*the driven body* and knew nothing about the other five, so a belt could shove
a monster straight onto a hire. Two actors on one tile is the single invariant
the combat system assumes everywhere, and the code that most needed to respect
it could not see five sixths of the party.

Both fixed: every in-use body is carried, and the collision check is over
bodies rather than over "the player". The log line stays with the driven body
only -- five hires being carried is the water working, five log lines a turn
about it is the log becoming unreadable.

Guarded in `tests/invariants.c`, and the guard fails against the old rule on
exactly the hire case.

### The monster array is not waste

The roadmap asked for a compacted monster array, on the note that `sizeof(Map)`
was 1.1 MB with 736 KB of it empty monster slots. Measured now: **62.4 MB, of
which 52.6 MB is the monster array** -- which reads like four fifths of the Map
being waste and is not.

| world | Normal | Hard | Swarm | Hardcore | cap |
|---|---|---|---|---|---|
| Shaft | 114 | 3,249 | 3,289 | 3,224 | 6,000 |
| Halls | 515 | 16,490 | 16,943 | 17,580 | 37,500 |
| Deeps | 1,635 | 6,485 | 57,228 | 58,932 | 150,000 |
| Well  | 6,154 | 24,512 | **231,106** | -- | 600,000 |

The hard modes sit at 38-55% of the cap at every world size. The note was
written when `MAX_MONSTERS` was a flat 2,000 and Normal was the reference; §55
then gave Swarm a multiplier that ramps with depth and §56 raised the ceiling
precisely so that the ramp rather than the array would set the density.
Cutting the cap re-imposes the limit §55 removed.

Compacting helps **Normal only**, at 1% of the cap, and costs the save format:
the run save is `Player` and `Map` written as raw structs, deliberately and
from the beginning, and a pointer in `Map` turns that into a field-by-field
serialiser for the benefit of the mode that already fits. Declined, and
recorded as settled rather than left on the page as a blocker.

### On stale notes

Two of the three items in this section were wrong about the code, and both had
been on the page for sessions. They are the same failure as §104's "instrument
that answers is worse than no instrument", one level up: **a roadmap entry that
is out of date is worse than a missing one**, because it is read as a
description of the present and spends somebody's afternoon before it is
disbelieved. The habit that follows is the one the owner asked for directly --
delete what is settled, and check the claim before acting on the item.

### Verified

Four suites at 0 failures, warning scan at exactly 3, the play screen covering
itself at 24x80 and 30x100, and 8,000 fuzzer keys with no stuck report.

---

## 107. Five districts, and the pool is nearly empty

§3.1's remaining buildable candidates, in one tranche. **Twenty-four district
kinds now**, and after this the pool has exactly one district candidate left in
it that is not built.

### Three that needed no new anything

**The eye** (99 Eye of the Storm) is the rod field with the polarity reversed.
There, one tile is about to be dangerous and you stand clear of it; here, one
quarter is sheltered and everything else is not, so you are riding a shelter
rather than dodging a hazard.

Which quarter is **derived from the floor's turn counter** --
`(turns_on_floor / EYE_ROTATE_EVERY) % 4` -- rather than stored. That costs a
district nothing to remember, saves and reloads correctly without a field, and
cannot drift out of step with itself. It also makes the rotation a *rhythm the
player can learn* instead of a roll they cannot, which is the difference
between a mechanic and weather. It catches monsters as readily as it catches
you, so standing in the quiet quarter is a decision rather than a rest.

**The mirror** (51 Mirror Dimension) stands up a copy of you at half health and
half damage. Half rather than parity, on this file's own earlier reasoning: a
mirror match at parity is a coin flip that ignores every decision the player
made about their build, which is the opposite of what a mirror is for. Built as
a `Monster` from the driven `Hero`, exactly as the barrow's ghosts are, so it
needs no new actor machinery. Worth no experience and carrying no purse --
levelling off your own reflection is a loop.

**The stone wood** is the ambush rule and nothing else, which is the only part
of Petrified Forest worth building; the rest was a thicket reskin and the
thicket already exists. Something you cannot see hits you twice.

It asks the *tile* whether you can see the thing hitting you rather than
remembering anything per monster, and that is not a shortcut. `Monster` is 92
bytes and a Well floor can hold 231,000 of them (§106), so a flag would cost
megabytes to express what the map already knows -- and the map's answer is the
better one anyway, because it is the same "can you see it" the renderer used to
decide whether to draw it.

### Two that needed plumbing

**The workings** (Mine Shaft) is galleries, ore and holes in the floor that
drop you a level for `PIT_FALL_HP_PCT` of your maximum health. A percentage
rather than a flat number so it stays a real question at depth instead of
decaying into free travel.

The candidate wanted a Listen action to reveal the holes, deferred until
multi-turn actions existed. They exist, and the listening is still not built,
because a hidden hole that hurts you for finding it by accident is a *trap* and
the interesting version is the one you can see and step into anyway.

`TILE_PIT` is walkable for a person and refused by everything else -- the
snare's rule, priced in floors instead of turns. Nothing under orders routes
into one, nothing follows you down, and `displace_actor()` stops short of one
for the same reason it already stops short of a staircase: being swept to the
next floor by a current you did not choose is not a mechanic, it is a bug
report.

**The boneyard** is Clockwork Boneyard's salvage half, which is the half §3.1
said to keep. Ten turns inside a dead golem, fully vulnerable throughout, for
the largest material haul in the game -- the longest commitment the game asks
for, paying like it.

**No new tile.** Rubble in a boneyard is a machine worth stripping exactly as
rubble in an old quarter already is, so `work_available_at()` stays keyed on
the *pair* -- tile type and district -- and one tile means three different
things depending on where it is lying. Pinned in the suite, because the next
district that wants rubble to mean something will make it four.

### What the fuzzer could and could not say

Both new districts generate and are walked into (the workings 23 times in
20,000 keys on one seed). **No fall and no core was ever completed**, which is
the watch's result again and for the watch's reason: a random-key player cannot
use a mechanic whose content is a decision. A hole you must choose to enter and
ten turns you must choose to stand still for are exactly what a fuzzer does not
do.

So the mechanics are pinned in the suites instead -- the work pairing directly,
and the holes by asserting that shaft districts actually contain them (435
across 456 floors, because a mine with no holes in it is a mine).

Reachability, all twenty-four kinds: **1.11% stranded overall**, and the five
new ones between 0.45% and 0.82% with no bad instances.

### Verified

Four suites at 0 failures, warning scan at exactly 3, twelve screens fitting at
24x80 and 30x100, the play screen covering itself, and no stuck report across
52,000 fuzzer keys. `SAVE_VERSION` is 44 -- `TILE_PIT` and `WORK_CORE` are
appended enum values that move no struct, so the header's size guard would let
an old save through and it would then read a floor whose tile numbering it does
not share. That is the case the version number exists for and the size check
cannot see.

---

## 108. The proving ground, and the end of the district pool

§3.1's last buildable candidate. **Twenty-five district kinds**, and every
candidate in the hundred that was ever triaged as buildable is now built.

### Three districts, one mechanic

Candidates 76 (Duelling Grounds), 78 (Arcane Duel) and 80 (Gladiator Gauntlet)
were three entries and one idea: a fight with part of your kit taken away.
§3.1 said to build it once with three skins rather than three times, and that
is what this is -- **blades only**, **the arts only**, **no weapons**.

It is the only thing in the hundred that attacks *build identity*. Every other
district changes the ground; this one changes what you are allowed to be while
standing on it, and "a hundred classes and eighteen ranged weapons all funnel
into one loadout you never change" is exactly the complaint it answers.

The rule is rolled once when the district is carved and lives in the
`MapDistrict.state` the watch already uses, so it is a property of the *place*
rather than of the turn -- which is what makes it announceable. It is announced,
every time, on arrival. **A rule you discover by casting a spell into a
monster's face is the mechanic failing rather than arriving.**

Six kills under the rule pays a writ of training. The arena's currency, and the
only kind §3.1 permits: that section says four separate times that a district
pays in School credit or in nothing, because more than a third of the hundred
candidates wanted to hand out permanent attribute points against a School that
charges `200v^2 + 300` for one.

### Enforced where it is used, and by everyone

At the point of use in all four places -- `cast_spell_slot()`, `fire_ranged()`,
`hero_attack_monster()`, and the weapon's contribution inside the damage roll
-- rather than at some gate that every future caller would have to remember.

And by **the hires as well**. `companion_cast()`, `companion_fire()` and
`companion_strike()` all ask. A rule the player keeps and their party does not
is not a rule, it is a handicap, and it would read as the game cheating -- which
`dda.h` already says players are right to resent.

**The one that mattered most was auto-explore.** `autoexplore_decide_fight()`
asks before choosing, so a delegated walk never picks an action the ground will
refuse. A refusal spends no turn, so a walk that picked one would pick it again
for as long as the player watched -- which is the shape of *every* stuck-walk
defect this project has had, three of them in this session alone. Toe to toe on
ground that forbids blades, with nothing to cast, the walk declines the fight
and leaves, which is the only honest answer.

### What the suite insisted on

The unarmed skin originally suppressed your weapon and left your bow alone, and
the test written against it failed. It was right to: a restriction that takes
your blade and permits your bow does not restrict anything, it tells you which
button to press. All three skins refuse the bow now -- all three are about
closing with somebody, and a bow is the way out of that.

That is the second time this session a test has corrected a design rather than
catching a defect, and both times the design was mine.

### Verified in play, at last

The fuzzer could not use the watch, the barrow, the holes or the cores -- every
one of those has a *decision* at its centre, and a random-key player does not
make decisions. This one it can use, because the restriction acts on it whether
it decides anything or not. On one seed, over 14,000 keys:

| | |
|---|---|
| entered | 143 |
| rule announced | 143 |
| actions refused | 1,278 |
| writs paid | **26** |
| stuck reports | **0** |

The refusals are the fuzzer pressing the cast and fire keys directly and being
told no, which is a person's experience of the district. The zero is
auto-explore never once picking a forbidden action across all 143 visits.

Reachability: 48,383 tiles over 103 instances, **0.00% stranded**.

### Verified

Four suites at 0 failures, warning scan at exactly 3, twelve screens fitting at
24x80 and 30x100, the play screen covering itself. `SAVE_VERSION` is 45: no
struct moves, but a version-44 floor's `districts[i].state` means "the watch is
unbroken" where this build reads it as "blades only" -- the same class of
change as 44, invisible to the size guard and fatal to an old file's meaning.

---

## 109. Records, and a row that could go missing quietly

ROADMAP 2.2, which has been open since it was written and was right about why
it would be cheap: the data mostly existed already. Kills, turns, gold and
depth are on `Player`; the lifetime best is in the highscore file; the per-class
bests are in their own.

**What it could not have anticipated is the half that makes it worth looking
at.** The fallen roster was built three sections ago for the Gauntlet of the
Fallen, to give the barrows somebody to put in them. It turns out to be the
better content on this screen too: a list of records is a scoreboard, and a
list of your own dead -- name, class, the floor they got to, the purse they
were carrying -- is a history. The barrow made the meta-progression *fightable*
and this makes it *readable*, off one file.

`R` in the temple. This run, the lifetime best and the title that goes with it,
the deepest six classes, and the last five of the fallen.

Both lists are capped rather than paged, deliberately. There are a hundred
classes and sixteen slots on the roster; a player who has taken eleven classes
down a hole wants the deepest six, not a paging control on a page they will
look at for four seconds.

### The audit gained a check while this went in

Adding a key meant adding it to the footer, and the footer is a single string
literal drawn through `mvprintw_clip()` -- so it does not overflow, it
*silently loses its tail*. The existing hint row was already 82 characters
against 78 usable at 24x80, which means the game has been quietly clipping
"q quit" off the smallest supported window for as long as that row has existed.

That is the one row that has to survive the smallest window: a player who
cannot see it cannot find the screens that hold everything else. So the
screenshot tool now reads the drawn cells back and fails if `? keys` is not
among them, and the row was shortened until it fits. `q quit` came off it --
Esc works everywhere, and `?` lists it.

**A literal that clips is the same failure as a region that is not cleared**
(§104): it is not wrong, it is *quietly incomplete*, and only reading the
screen back can tell you. That is now two things the screenshot tool checks
that it was not built to check, and both were found by adding something
ordinary next to them.

### Verified

Thirteen screens fitting at 24x80 and 30x100 -- records among them -- the play
screen covering itself, the key hints present at both sizes, four suites at 0
failures and the warning scan at exactly 3.

---

## 110. Six new shapes, not six new numbers

Phase 3's last content item, and it was understated. The entry said a school
"draws on only 3-4 distinct effects". Measured before touching anything:

| school | spells | distinct effects |
|---|---|---|
| Conduit | 30 | 3 |
| **Resonance** | 30 | **2** |
| Warding | 30 | 3 |
| Aether-Sense | 30 | 3 |
| Scribing | 30 | 3 |
| Jinx | 30 | 4 |

Thirty Resonance spells were BUFF_ATK or HEAL_SELF and nothing else. The
curation pass gave every school an identity and left its mechanics alone, so
what separated two same-level spells was an archetype and a number.

### One new shape each, chosen for what the school could not do

Not one new effect *per se* -- one thing the school had no way of expressing:

| effect | school | the shape it adds |
|---|---|---|
| `CHAIN` | Conduit | hits several things without being a radius: a nova hits what is near *you*, this hits what is near *what you hit* |
| `PARTY_HEAL` | Resonance | **the first spell in the game that acts on the party**, which is what a school named for carrying something between bodies should always have had |
| `HASTE` | Resonance | acts on *time* -- everything you were waiting for is ready |
| `REFLECT` | Warding | the third answer to being hit, after "take less" and "do not be there" |
| `SENSE_LIFE` | Aether-Sense | information: it could reveal *ground* and move you across it and had no way to answer "what is on this floor" |
| `MASS_SLOW` | Scribing | control over an area rather than a target -- same verb, different question |

Jinx was left alone. It was already at four, and adding one for symmetry would
have been a count rather than a reason.

Only `REFLECT` needed state on the body (`reflect_pct`, `reflect_turns`); the
other five act and are done. It ticks down in `hero_tick()` with the rest of
the timers, which is the one place a body's clock advances and runs for all six
-- the exact thing §92's driven-hire bug was about.

`reflect_back()` is deliberately *not* inside `hero_take_damage()`. That
function is also called by lava, by snares and by the eye of the storm, and a
ward that punished the ceiling for falling on you would be a ward that had
stopped meaning anything. It hangs off the two paths where a monster's blow
lands, because reflect is about being struck by something that can be struck
back.

### What the hires are not given, and why it is written down

`effect_fit_for_companion()` excludes all six. CHAIN and PARTY_HEAL would both
be fine in a hire's hands and are the obvious next thing to give them. The
other four are decisions about *when* -- and `companion_pick_spell()` has no
way to know that this is the turn worth spending Haste on. **A hire casting
Haste at an empty corridor is §104's cursing bug wearing a new spell**, and
that one cost this session an afternoon. Left out on purpose, and the comment
names them so the next person knows it was a choice.

### The fuzzer could not answer this one either

15,000 keys turned up **one** chain and nothing else, which is not a failure --
a character belongs to one school and has to *buy* the spells, so a single seed
sees a sixth of the pool at best. This is the fourth mechanic this session the
fuzzer cannot reach, after the strongbox, the fall and the core.

So all six are cast directly in the suite and the *outcome* asserted: the chain
damages a second monster, the chorus mends a hire and not only the caster,
haste clears another spell's cooldown but pointedly not its own, the ward comes
up and wears off on the body's clock, the sense lifts fog from a monster
outside any reveal radius, and the clause catches more than one thing.

The pool test also gained a bar rather than a count -- *every school does at
least four different things* -- and a check that each new effect is on
somebody's list, because an effect nobody casts is an enum value with a comment
on it.

**One failure in that suite was mine, in the test.** The reflect wear-off loop
bounded itself on the very counter `hero_tick()` decrements, so it exited early
and failed a game that was correct. Third time this session a test has been
wrong about working code, and worth the tally: they are as easy to write badly
as the thing they check.

### Verified

Four suites at 0 failures, warning scan at exactly 3, thirteen screens fitting
at 24x80 and 30x100, key hints present at both, no stuck report.
`SAVE_VERSION` is 46 -- the six effects are appended and invisible to the size
guard, but `Hero` gained two fields and that moves `sizeof(Player)`.

*A note on the tooling, from five confused minutes:* `tools/screenshot.sh` runs
whatever `bin/screenshot` already is and does not rebuild. Running it directly
after a source change reports on a binary without that change -- the key-hint
check "failed" against a stale tool and passed the moment it was rebuilt. The
script says so at the top now. `make shot` has had the objects as a real
prerequisite since §104 and is the one to use.

---

## 111. Quarters, and three mistakes made measuring them

ROADMAP 2.1f's last open piece, and the last item in Phase 3 that is not
balance. Room shapes gave a big floor variety at the scale of a room and wild
districts gave it variety at the scale of a region; the scale *between* them
was untouched, so a Well floor was a very large amount of the same
connectivity. Everywhere reachable from everywhere by about as many routes,
which means no route is a decision and nothing is behind anything.

`carve_quarters()`: two dividing walls down the floor and one across, six
quarters, three chokepoints per line. The point is not the walls -- it is that
crossing the floor now goes *through* somewhere, and that "the stairs are in
the north-east" starts to mean something you can act on. Deeps and Well only;
on the Shaft a floor is crossed in minutes and quartering it would be a wall in
a corridor.

### It removes connectivity on purpose, so it proves itself

This is the most dangerous thing in mapgen: it takes connectivity away, late,
after everything else is placed. So each line is laid one at a time, recording
every tile it changes, and then the floor is flooded from the arrival point. If
the line stranded ground, the pass **opens more gates** -- a tile on the line
with reached ground on one side and cut-off ground on the other is by
construction exactly where a gate belongs -- and floods again. If it still
cannot be made to work, the line is rolled back tile for tile.

`tests/movement.c` checks that from the outside, because a self-verifying pass
that verifies itself wrongly is the shape of a bug nobody finds. Result: **35
of 36 dividing lines survive**, and 0.38% of a quartered Deeps floor is
unreachable against the 1.04% the districts already account for.

### Three mistakes, all in the measuring

**The threshold was the wrong shape, not the wrong number.** A line was rolled
back if it stranded more than 400 tiles. That threw away *every* line on the
Well and three in five on the Deeps -- and the instrumented reason was that a
Well line stranded 473 tiles out of 789,264 reachable, which is 0.06%. One
absolute figure cannot mean the same thing on floors whose walkable area
differs fourfold. It is a fraction now, and the fraction was chosen from the
measured residue (0.06%-0.21%) rather than guessed.

**An optimisation that was worth nothing broke the pass.** Clearing the flood's
scratch array row by row instead of all at once is the obvious saving. It
silently took 35 surviving lines to 0. The cause was not chased down and will
not be: the A/B says the whole pass costs 8.7 ms of an 85 ms floor, so the
saving was worth *nothing at all*. An optimisation that buys nothing and breaks
the thing it optimises is not worth understanding, it is worth deleting -- and
the comment now says so, so nobody has the same clever idea twice.

**And I compared against a stale baseline and alarmed myself.** Deeps
generation had been 46 ms/floor when it was measured at §103. After quartering
it read 84, and I spent a round of work on a slowdown that was not there: the
eight district kinds added earlier in this session account for nearly all of
it. The honest A/B, built both ways *now*, is **85.1 ms without quartering and
93.8 with**, or about 10%.

That is the third time in two sessions that a comparison against a remembered
number has been wrong, and it is the same lesson as §105's: the number in the
file describes the build it was taken from, and nothing else.

### A real bug, found by adding a key

The screenshot tool's key-hint check -- added in §109, when the footer had to
be shortened -- began failing at 24x80 and I twice blamed a stale binary. It
was not stale. `footer_y` is derived from where the message log ends, and at 24
rows the log pushes it onto the frame, where `mvprintw_clip()` drops it
entirely. **The smallest supported window has been showing no key hints at
all**, for as long as that row has existed.

Clamped onto the last usable row. It still clips its tail on a narrow window,
which is intended; going missing is not. This is the third defect of that exact
family this session -- a region nobody cleared, a literal that clipped, a row
that fell off the bottom -- and all three were invisible until something read
the screen back.

### Verified

Four suites at 0 failures, warning scan at exactly 3, thirteen screens fitting
at 24x80 and 30x100 with the key hints present at both, the play screen
covering itself, and 10,000 fuzzer keys with no stuck report.

**Phase 3 has nothing left in it that is not balance.** The district pool is
empty, the schools have their variety, the records screen is built, and the
floors have structure. What remains is New Game+, which is gated on a hundred
floors actually being finished, which is the balance work.

---

## 112. A bug hunt, and one that got away

A sweep across every system before moving on, asked for by the project owner.
Cheap instruments first, then the defect classes this session kept producing.

### What the instruments said

**Clean:** `-Wswitch-enum` over the whole source turns up five warnings and all
five are deliberate `default:` catch-alls or `_COUNT` sentinels. That matters
because "an appended enum value nobody handled" is the class that produced
three of this session's `SAVE_VERSION` bumps, and there is none of it left.
Every one of the twenty-five district kinds has a name, an arrival note and a
colour -- checked by walking the enum rather than by reading the tables.
AddressSanitizer and UBSan are clean over three fuzzer seeds *and* over the
movement suite, which is the thing doing million-tile floods.

### Three real defects, all the same shape

**The prism floor only existed for the body holding the controller.** The
stance is re-read every turn -- step off and it is gone, which is the whole
mechanic -- and it was read and cleared for `hero_driven(p)` alone. Wrong in
both directions: a hire on coloured ground got nothing, and, the sharp half, a
body that was driven while standing on the prism **kept its +50%/-50% for the
rest of the run**, because the only code that clears the stance runs for
whoever holds the controller now. Tab away from red ground and the character
you left was permanently swung, anywhere on the floor.

`hero_eff_atk()` has applied `stance_*` for all six actors since §92. Only the
*setting* of it was still written as though there were one body. Found by
grepping this file for `hero_driven(p)` inside the per-turn world -- the same
search that found the belts and currents in §106, run again because it worked.

**The guardian angel left you unable to move.** `actor_step()` sets
`alive = false` the moment your hit points hit zero and *then* asks the saves
whether anyone wants to undo it. `companions_attempt_rescue()` sets the flag
back. `guardian_catch()` restored the hit points and never did -- so a run the
guardian saved continued with a body at 1 HP that `hero_is_up()` says is not
standing. From that moment `actor_step()` refuses every move, `hero_tick()`
stops advancing cooldowns, and the district rules skip you.

**The key hints were still going missing at 24x80.** §111 clamped the row onto
the last usable line; it was then 82 characters against 78 usable, so it lost
`? keys` -- the one entry whose whole job is to say where the other keys are.
Single spaces between the groups now: on the smallest supported window, being
*there* beats being well spaced.

### And a guard that found something the moment it existed

Extending the movement suite to check districts on *quartered* floors -- the
big worlds, which the main sweep never covers -- failed on its first run
against a jungle that had lost 811 of its 814 tiles. It was severed with
quartering switched **off**: a rare, pre-existing condition, measured at **one
severed district in 1,364** across all four world sizes. The assertion was
rewritten as an aggregate, because a per-district check on a six-floor sample
fails now and then for reasons the reader cannot act on, which is worse than no
check. The rate is recorded here instead.

Quartering also now stops at the edge of any district rather than only the
sealing ones -- a dividing line through an authored place makes it a way
through, which is a better chokepoint than a gap in a wall.

### The one that got away

`Auto-explore: getting nowhere here` is **back**, at 35-71 stalls per 14,000
keys on one seed and none on others, and I did not catch it. What is now known,
so the next attempt starts from evidence:

- The driven body is at **full health**, not stunned, `in_use`, `alive`, with
  no target in hunt range.
- `autoexplore_route()` returns a step every iteration, and the destination is
  ordinary floor: walkable, no monster, no companion.
- `actor_step()` called directly on that step **succeeds**.
- Yet after `actor_turn()` -- which is `actor_step()` plus
  `resolve_after_player_turn()` -- the body is on the **same square**, byte for
  byte identical, sixty times over. The world's half of the turn does run: a
  respawn message appears inside it.

So the body steps and the world puts it back. Ruled out by experiment, not by
reading: the aqueduct and the belts (the tile is plain floor), the heal branch
(full health), stun, a corpse blocking the square (`monster_at` filters on
`alive`), and a hire swapping into the square via `companion_move_to` -- that
fix was written, measured, and made no difference to the trace, so it was
reverted rather than kept on a hunch.

What is left to examine is the rest of `resolve_after_player_turn`: the other
paths that write a body's position, which are `party_displace_into()` from
`actor_step` itself, `step_away_from()`, and the rescue. **Cross-build stall
counts are not evidence** -- 35 became 71 across a change that turned out to be
irrelevant, because a different build consumes the RNG differently and
generates different floors. Only the position trace is.

### Verified

Four suites at 0 failures, warning scan at exactly 3, ASAN and UBSan clean,
thirteen screens fitting at 24x80 and 30x100 with the key hints present at
both, and the play screen covering itself.

---

## 113. The one that got away, caught

§112 characterised the returning `getting nowhere here` and did not find it.
This is the rest of that, and the answer is one line.

### Following the body rather than guessing

Four measurements, each halving the problem, and none of them a hypothesis:

1. **Where is the body across the stall?** Traced at the top of each iteration:
   identical, sixty times. Not oscillating -- *static*.
2. **Does the step land?** Logged inside `actor_turn()`, on both sides of the
   step: `(310,56) -> (311,55)`, every time. The step lands.
3. **What puts it back?** The same log, after
   `resolve_after_player_turn()`: `(311,55) -> (310,56)`. Every time.
4. **Which part of the world?** Bracketed each call in that function:
   `companions_take_turn()`.

Then one more, because the obvious suspect was wrong: `party_displace_into()`
logs **no swaps at all** during the stall. So it was not a hire squeezing past
the player -- a fix I had written for exactly that, measured, found no effect
from, and reverted. Logging which *slot* moved the driven body gave the answer:

```
SLOT 0 moved driven -> (310,56) ctl=1 isdriven=1
```

Slot 0 is the driven body, `controlled` says 1, and the loop ran the AI over
slot 0 anyway.

### `p->controlled` is an index; `hero_driven()` is a body

`companions_take_turn()` has skipped the driven actor since §92, by comparing
the loop index against `p->controlled`. Those agree right up until they do not.

`controlled` can point at a body that has **stopped standing** -- a hire you
were driving when it went down, in the window before the torch is passed --
and `hero_driven()` documents a fallback to slot 0 for exactly that case.
Every other consumer in the game takes the fallback. This one compared the raw
index. So with `controlled` at 1 and slot 1 a corpse, the loop skipped the
corpse and ran the AI over slot 0, which is the body the human was driving.

The walk steps you a square; the AI walks the same body back; the position is
byte-identical turn after turn until the no-progress detector gives up. **35 to
71 stalls per 14,000 fuzzer keys before, none after**, across four seeds.

```c
-        if (i == p->controlled) continue;
+        if (c == hero_driven(p)) continue;
```

### Why this one is worth a section

The comment above that line already describes this bug. It was written in §92
about the AI taking a turn for the steered body, and it says what that plays
like: *"my keys move me in the wrong direction"*. The defect came back through
the one path that was not asking the question the comment asks -- it asked
about an index instead of about a body.

That is §91's rule with a new instance: **treat every `p->x`, `p->y` and
`p->controlled` outside combat as a suspect.** `controlled` belongs on that
list and was not on it. It is not a body; it is a *hint* about which body, and
`hero_driven()` is the only thing entitled to interpret it.

And the method is the point. §112 ruled things out by argument and got nowhere
for an hour; this took four logs, each one splitting the space in half, and the
last one printed the answer. Two of the hypotheses I had been most confident in
-- conveyed ground, and a hire swapping places -- were both wrong, and one of
them I had already written and reverted a fix for. **The trace was worth more
than the reasoning every single time.**

### Guarded

`tests/invariants.c` builds the state that produced it -- control pointing at a
fallen hire, slot 0 up and driven by the fallback -- runs six turns of world
and asserts the driven body has not moved. It fails against `i ==
p->controlled` and passes against `c == hero_driven(p)`.

The test needed correcting once itself: it first left slot 0 without `in_use`,
so `hero_is_up()` was false, the loop skipped it either way, and the test
passed against the bug. Fourth time this session a test has been wrong about
working code, and the second time the mistake was a body that was never
standing.

### Verified

Four suites at 0 failures, warning scan at exactly 3, ASAN and UBSan clean,
thirteen screens fitting at 24x80 and 30x100 with the key hints present, the
play screen covering itself, and **no stall across five fuzzer seeds**.

---

## 114. Ghosting

Phase D, played by the project owner. Two of the three items came back clean
and the third came back with a word for its problem.

### 1.1 and 1.2 are closed

**The retuned difficulty is right.** The Ouroboros Coil at 5000g is reachable,
`floor^2/140` is not too steep, and 6 HP/turn is the right rate. Three
questions that had been open since §14 and could only ever be answered by
playing; all three answered yes.

**The bosses land.** Floor 15 and floor 35 fought at the new multiplicative
curve, neither trivial nor overlong. The hand-modelling in the roadmap turned
out to be right and neither `make_biome_boss()` nor `make_boss()` needs
touching.

Worth saying plainly because it is the unusual result: the numbers were guessed
from a model, sat unplayed for several sessions, and were correct.

### 2.4 is two faults, not one

*"The tileset is slow and suffers of delay... sometimes it can go in a
direction for seconds even if I stopped typing."* And then, unprompted:
**"it's like ghosting."**

That is a better name than anything in this file, and it names the *second*
fault, which turned out not to be the renderer at all.

**The play loop is draw-then-read-then-act, one frame per key.** That is
correct and cheap right up until a frame costs more than the terminal's
key-repeat interval. Past that point the queue fills faster than it drains, and
every buffered key is still spent -- one slow frame at a time -- after the key
is released. The character walks on because the *game* is still walking; it is
minutes behind the player's hands.

**Fixed by not drawing what nobody will see.** `input_waiting()` peeks without
consuming, and both play loops skip the frame they were about to draw whenever
a key is already waiting. Every turn still happens, in order -- this drops
*drawing*, not input -- and the last key of a burst always draws, because by
then nothing is behind it. The game keeps up with the typing, so there is no
backlog to run on with.

The distinction matters and is worth keeping: **the fix is not to discard the
player's input.** Dropping keys would turn a laggy game into an unresponsive
one, which is worse and much harder to diagnose. Every keystroke still spends
its turn; only the pictures between them are skipped.

### What the ncurses build could not have told me

The first version declared the notcurses backend's one-slot pushback *after*
the `getch()` that reads it. `make` was clean -- the ncurses backend does not
compile that code -- and `make notcurses` failed on four undeclared
identifiers. **The second backend is not decoration; it is a second compiler
pass over code the first one never sees**, and this is the first time in this
file it has caught something.

The peek's contract is checked in `tools/screenshot.c` rather than in
`tests/invariants.c`, because it needs a real curses screen and that suite is
headless -- the first version sat there and failed for want of an `initscr()`,
which is the suite being right about what it is. It needed one more correction
after that: the pty the tool runs under has keys of its own in flight, so the
check flushes first and now names which of its five steps failed instead of
reporting a bare no.

### Still open, and it is the half nothing here can measure

Tile mode's raw speed. **notcurses will not start under a pipe**, so no
instrument in this repository can time a tile frame -- which is the same
property that closed 2.5 in favour of ncurses staying the default, read from
the other end.

What is needed is what the roadmap asked for when it wrote the entry: the
numbers the display screen reports, **cell size especially**, since a retina
display reports physical pixels and doubles everything. And whether `-`/`+`
there, which moves the per-frame pixel budget, changes how it feels now that
the ghosting is gone. That is the one input a human at a real terminal has and
this project does not.

### Verified

Four suites at 0 failures, warning scan at exactly 3, **both backends
building**, thirteen screens fitting at 24x80 and 30x100, the play screen
covering itself, the key hints present, the input peek free, and no stall
across the fuzzer.

---

## 115. The screenshot that showed it

§114 fixed the ghosting and left tile mode's speed open for want of an
instrument. Three screenshots from play answered a different question instead,
and the second and third are what made it diagnosable: **text mode is correct,
block graphics is correct, and only tiles is wrong.**

That triangulation is the whole finding. One screenshot of a broken screen says
"the renderer is broken". Three, two of them right, say "it is the thing the
third one does differently" -- and the only thing tile mode does differently to
the layout is `render_clamp_tile_viewport()`.

### A pixel budget that shrank more than the picture

Tile mode caps how many pixels a frame may draw, and enforces it by shrinking
`VIEW_W`/`VIEW_H` until `w*cell_w*h*cell_h` fits. On a retina display, where a
cell reports *physical* pixels, that is a hard clamp -- and the roadmap has
warned about the doubling since the entry was written.

Shrinking the viewport is the right lever. The bug is what followed it:
`sb_x`, `log_y` and the footer were all derived from `VIEW_W`/`VIEW_H`. In text
and block modes that is exactly right, because there the viewport *is* the
window minus its reserves and the map ends where the panel should start. In
tile mode the viewport is much smaller than the window, so the whole interface
followed the map into the corner and the frame enclosed a void.

They anchor to the window now. The arithmetic is deliberately chosen to be a
**no-op** in the two modes that were already right -- `cols - 42` against
`1 + (cols - 45) + 2`, and the log's own formula with the window's height in
place of the clamped viewport's -- checked at 80x24, 100x30, 140x40 and 200x60,
identical to the row at every one.

The first version of the log anchor was *not* a no-op: it read
`win_rows - 2 - MSG_LOG_VISIBLE` and sat one row lower than the game has always
drawn it. Caught by comparing the arithmetic against the old formula rather
than by looking at a screen, and it mattered because the owner's screenshot had
just established that text mode is right. **A fix to a broken mode must not
move a working one**, and "one row tighter" is not a no-op even when it looks
tidier.

### What it does not fix

The blank *map* area. A clamped viewport draws fewer tiles than the region
could hold, and no amount of anchoring changes that -- it is the budget doing
exactly its job. The furniture is where it belongs now; the picture is still
small, and the knob is `-`/`+` on the display screen.

### Guarded

`tools/screenshot.c` simulates the case it cannot open: a large window with
`VIEW_W`/`VIEW_H` forced down to what a 360,000-pixel budget leaves on a retina
cell, then checks that the panel still reaches the right-hand side and the log
the bottom. It reports "blank below row 25 of 40" against the old anchoring and
passes against the new.

Simulation rather than measurement, and honestly so: notcurses will not start
under a pipe, so nothing here can open tile mode. But the clamp's *only* effect
on layout is to make the viewport smaller than the window, and that is exactly
what the simulation does -- which makes this one of the few places where a
simulated input is not a weaker test but the same test.

### Verified

Four suites at 0 failures, warning scan at exactly 3, both backends building,
thirteen screens fitting at 24x80 and 30x100, the play screen covering itself,
the key hints present, the input peek free, and a small viewport in a big
window now reaching the edges.

---

## 116. Five hires, one colour

Reported as a nitpick and it is not one: *"in block mode and tileset heroes have
the same colours, on text have all different colours."*

The party are all drawn as `'@'`. Colour is the only thing telling them apart --
`COMPANION_COLORS` is six pairs, chosen deliberately with none of them the
player's cyan, and the comment above it says exactly that. So in the two modes
where they came out identical, the party had **no** distinguishing mark at all.

### One resolve pass, two ideas about colour

The renderer resolves a cell once and paints it three ways, which is the design
that has absorbed a backend split and two graphical modes without the screens
changing. The cell carries both a `text_pair` -- the colour pair the *game*
chose -- and an `occupant` ArtId.

Text mode draws from `text_pair`. Blocks take their foreground from
`art_block_colors(cv->occupant, ...)`, and tiles rasterise the sprite for
`cv->occupant`. Every hero is `ART_PLAYER`, so both graphical modes asked the
art what colour a body is, and the art has one answer.

Neither is wrong on its own. A sprite *should* carry its colour -- that is what
makes a rot-hound read as a rot-hound at a glance. The mistake is that a body
is the one occupant whose colour is not a property of what it is.

### `tint_by_pair`, and only for bodies

`CellView` gained a flag, set by a new `cell_put_body()` and by nothing else.
Block mode substitutes the pair's RGB for the sprite's foreground;
`art_sprite_rgba_tinted()` does the same for tiles, overriding the figure and
leaving the accent alone so whatever a sprite uses `'*'` for still reads.

**Monsters keep the art's colours on purpose.** Their sprites are drawn to be
told apart by *shape*, which is the thing tiles can do that text cannot, and
tinting them by faction would throw that away to solve a problem they do not
have.

`tui_pair_rgb()` is the new backend call, and adding it meant lifting the
`ANSI8` table out of the ncurses branch to sit above the split -- one table, so
a `'@'` cannot be one green in text mode and a different green in blocks.

### Tested where it can be

Neither graphical mode opens under a pipe, so the check is on the rasteriser
rather than on a screen: tinting `ART_PLAYER` must change the pixels, the
colour it was given must actually appear among them, and passing no tint must
be byte-identical to the untinted call -- that last one because every other
caller in the game goes through the same function and must not have moved.

It fails on the first two against a `(void)fg_override;` and passes on all
three with it restored.

### Verified

Four suites at 0 failures, warning scan at exactly 3, both backends building,
`make art` still rendering, thirteen screens fitting, and the play screen,
key hints, input peek and small-viewport checks all holding.

---

## 117. Two call sites, one converted

§116 said the party's colours were fixed. They were not, and the report came
back with three screenshots: *"still same colour, only on text mode is ok."*

**There were two places a body is put into a cell and I converted one.**
`build_cells()` fills the viewport once per frame -- that is the one that draws
the party on the map -- and a second, single-cell version exists for the
animation code to restore a square after a bolt has been drawn over it. The
fix went into the second. Every check passed, the rasteriser test proved the
tint reached the pixels, and nothing the player could see had changed.

That is the sharpest instance this session of a thing it keeps finding: **a
verified fix that verifies the wrong half.** The tileart test asked "does a
tint reach the pixels" and the answer was honestly yes; nobody asked "does the
map ask for one". Both call sites now go through `cell_put_body()`, which is
the only function that sets the flag, so a third site cannot be added without
choosing.

Both party loops in the renderer also carried `i == p->controlled`, the stale
index from §113. Harmless here -- the player is drawn over it afterwards --
and fixed anyway, because "harmless today" is how the first one survived.

### The tile budget was charging for a map it was not delivering

Reported alongside: *"the viewport is so small on tileset that breaks the
layout."* Measured, and it is worse than a bad default.

`TILE_PIXELS_DEFAULT` was 360,000, calibrated on an 8x16 cell where it buys a
75x37 viewport. A retina terminal reports *physical* pixels, so the same
apparent cell is 20x44 and the same budget buys the floor: 58x18, whatever the
window. And the floor at that cell size **costs 918,720 pixels** -- so the
budget was already being exceeded by a factor of nearly three, and its only
remaining effect was to pin the map at its minimum. It was not protecting
anybody. It was taking the map away and charging for it anyway.

| cell | old default | new default | new max |
|---|---|---|---|
| 8x16 | 75x37 | 125x62 | window-limited |
| 14x30 | 58x18 *(floor)* | 72x33 | 248x115 |
| 20x44 | 58x18 *(floor)* | 58x19 | 173x78 |

The ceiling went to 12 million because a full-window map at 20x44 is 8.4
million pixels, and a ceiling below that cannot express "show me the whole
window" however hard the player leans on `+`. The default is still short of a
full map on a retina cell, deliberately: what a terminal can ingest per frame
is the one thing nothing here can measure, so the starting point stays
conservative and the lever is the answer.

### And an answer to "how big can the viewport be"

Already as big as the map, and it costs almost nothing. `compute_viewport()`
caps at `MAP_W`/`MAP_H` and `compute_camera()` pins to 0,0 when the viewport
covers the floor, so the whole thing simply stops scrolling.

Measured in text mode:

| world | window | viewport | ms/frame |
|---|---|---|---|
| Shaft | 100x30 | 58x21 (1,218 cells) | 0.311 |
| Shaft | 185x89 | **140x80 -- the entire floor** | 1.940 |
| Deeps | 185x89 | 140x80 (window-limited) | 2.253 |

A whole Shaft floor on screen at once is 2 ms a frame and the screen audit
passes at that size. The window is the binding constraint everywhere else: the
Deeps would want a 745x409 terminal.

### Verified

Four suites at 0 failures, warning scan at exactly 3, both backends building,
thirteen screens fitting at 24x80 and 30x100 and at 185x89.

---

## 118. Daylight, and a key to see it by

The last of D, and both halves came from the same place: the surface was being
drawn by whatever the code did when nobody had decided anything.

### The city had no palette because -1 means two things

Every dungeon floor takes a biome tint -- five rows of channel multipliers, and
they are the reason a hundred floors read as more than one place. Town passed
`art_set_biome(-1)`, and `-1` means *no tint*: the raw art colours, flat warm
grey, buildings barely separable from the plaza they stand on. Reported as
*"the city on mode 2 is a bit meh"*, which it was.

The bug is that `-1` was carrying two meanings: **"a caller that has not said"**
and **"the surface"**. The first should be untinted; the second should look
like somewhere. `ART_TINT_CITY` is a sixth row past the five biomes -- past
them on purpose, because the city is not a band of the descent, it is the place
you come back to -- and it is the only tint in the table that pulls *two*
channels up. Everything below ground pulls at least one down; daylight is what
that contrast is for.

`make art` renders `art-scene-city.png` alongside the five biome scenes now.
That matters more than the palette: the city went untinted for as long as it
did **precisely because nothing rendered it for anybody to look at**, and every
other tint in the file has had a picture since the day it was written.

### And a key, because the intro is the worst place to choose

`screen_display_settings()` was reachable from the title screen and nowhere
else. That is the one moment a player has nothing to compare against -- you
cannot judge a render mode from a title card, and changing your mind meant
abandoning the floor you actually wanted to look at.

`D` now opens it from the plaza and from a floor. `render_set_mode()` already
re-lays the viewport, so there is nothing to do afterwards; the key is the
whole change. It is on the `?` screen and deliberately not in the footer --
that row is 74 characters against 78 usable and has already lost `q quit` once
this session, and its job is to say where the rest of the keys are rather than
to be all of them.

### Verified

Four suites at 0 failures, warning scan at exactly 3, both backends building,
`make art` rendering seven scenes, thirteen screens fitting at 24x80 and
30x100, and the play screen, key hints, input peek and small-viewport checks
all holding.

**D is done.** 1.1 and 1.2 closed by play, 2.4 answered in four parts -- the
ghosting, the layout, the party colours and the city -- with tile mode's raw
frame cost the one thing left, and the one thing nothing in this repository can
measure.

## 119. Three more from play, and a test that agreed with me

D was not done. Three more reports came back from the same session, and each
one took a different shape.

**The plaza went dark.** Coming back up to town left a black blob across the
map, in all three render modes. The fix looks obvious -- the town is always
fully lit, so the per-body vision composite has nothing to say there, and
`hero_vision_update` now returns early on floor 0. The test I wrote for it
reported *"0 of 1044 town squares unlit"* and passed.

It also passed with the fix removed. It was not testing anything.

The reason is the composite only repaints when the driven body has changed
since the last paint, so a test that walks one body from a floor into town
never triggers the paint at all -- the town's own flags survive by luck, and
the test measures the luck. Once the paint is forced, the bug is there and it
has two stencils: a body remembering the last floor paints that floor's fog
onto the plaza, and a body remembering nothing paints the plaza black. The
test now drives both. Without the guard: 960 of 1044 squares unlit. With it: 0.

Worth being precise about what is and is not established. The second stencil
is caught, reproduced and fixed. Whether the screenshot from play was that
stencil or the first one, I have not proved -- the first does not reproduce in
the harness, because seeding a body's memory from the town map gives it nothing
foreign to paint. The guard covers both regardless, since it stops the paint
from running on floor 0 at all.

**The sidebar was crossed out.** *"On the right the text is cut."* It was not
being cut, it was being ruled through: `Weapon: Piston Fist------------`. The
divider under the map was drawn `cols - 2` wide unconditionally, which is right
when the map fills the window and wrong the moment it does not. Tile mode's
pixel budget shrinks the viewport, the sidebar and log stay anchored to the
window -- correctly, that was the earlier fix -- and the map's bottom edge ends
up in the middle of the sidebar with a full-width rule drawn along it.

The interesting part is that the sweep has a check called *"small viewport in a
big window"* and it was green throughout. It pinned the viewport at 18 rows and
ran in a 24-row window, where the map's natural height is 15 -- so its "small"
viewport was **taller** than the window allowed, the short-map path never ran,
and the check passed by never reaching the code it was named after. It is
derived from the window size now. Struck through 10 columns with the bug
restored, clean with it back.

**All the biomes looked the same.** *"Like blue water."* This one was true and
had been true from the start. Measured across the six rendered scenes, wastes
and city landed **3.8 apart out of 255**, and the widest pair in the entire
game was 28.9. Six biomes, one place.

Two causes, and the second is the one I would have missed. The rows were
near-neutral -- nothing further from 100 than a quarter -- so the strongest hue
any biome could ask for was a wash. And the shift was purely multiplicative,
which is the wrong operator for art this dark: floor's foreground is
`RGB(58,55,52)`, and 130% of a channel at 55 is sixteen levels. The tint was
loudest where the art was already bright enough not to need it and silent in
the dark, which is where the floor is. Real hue separation in the rows plus an
additive term for the dark end: closest pair 3.8 -> 14.8, widest 28.9 -> 68.2.

The through-line in all three is the same and it is not "write more tests". All
three had a test or a check nearby. The fog test passed on the unfixed code,
the layout check passed by not reaching the path, and the tint had a test that
asserted tinting *happened* rather than that it **separated**. A test that
asserts the mechanism ran is a test that will agree with you. The three that
found something asserted the property a human would complain about: the plaza
is lit, the sidebar is not struck through, two biomes are far enough apart to
be two places. Each was A/B'd against the broken code before being believed,
and the first one had to be rewritten when it wasn't.

## 120. A dead stat, and the collision underneath it

Reviewing the blocked list before starting balance turned up two things that
were not on it.

**`hearing_radius` was read by nothing.** Derived from `ATTR_HEARING` in
`hero_create()`, copied by `TRAINED()`, printed on the character sheet under
*"How far you hear what you cannot see"* -- and consumed by no system. I
checked all twenty-two derived stats the same way, counting reads outside
`classes.c` and the character sheet: every other one scores at least 1, and
this one scores 0. It is the only zero.

The reason it matters is that the stat is **for sale**. The Gladiator School
sells every attribute at `200v^2 + 300`, so gold spent on HEARING bought
nothing, and five of the hundred classes carry it at 6-8 as a primary -- they
were paying a build cost at creation too.

Wired up as the smallest honest version: a monster inside somebody's hearing
radius that nobody can see is *heard*, and the renderer marks the square with
a `?` rather than drawing the monster. Position, not identity -- "something is
behind that wall" is what the stat promises and also what stays interesting.

Writing the test taught me something about the feature I had not noticed while
designing it. Two attempts failed before one worked. The first looked for a
monster the generator had dropped within six tiles and found none, reporting
zero for both the deaf and the sharp variant -- which reads exactly like a dead
stat, and would have been believed. The second stood at the stairs and looked
for a dark square within six tiles, and there were none, because the stairs sit
in open ground.

That second failure is the interesting one, because it is a fact about the
design rather than the test. `fov_radius` clamps to 5..11 and `hearing_radius`
clamps to 0..6, so **hearing is never a longer sense than sight**. It reaches
through walls and nowhere else. That is a defensible thing for it to be -- a
dungeon is mostly walls, and "what is round the corner" is a real question --
but it is narrower than the help text implies, and it is the kind of thing
worth knowing before the balance pass rather than after.

**And underneath it, a colour collision.** `CP_BIOME_JUNGLE` through
`CP_BIOME_ABYSS` were numbered 67-71, written when the district block ended at
66. The district block then grew to 74. Ten names, five curses pairs. The
districts won by being initialised second, so every biome wash in text and
block mode drew in a district's colour: jungle blue, industrial magenta, ruins
cyan, wastes white, abyss yellow.

Nothing complained, because `init_pair()` on a live pair is a legal
redefinition and not an error. Both blocks looked correct in isolation and the
bug lived entirely in the arithmetic between them. Moved to 80-84 with a gap
for the district list to grow again, and `tests/invariants.c` now reads
`src/common.h` and fails on any two `CP_` names sharing a number -- reading the
header rather than keeping a list here, because a list here would be a second
copy of the thing under test and would have drifted exactly as the header did.

Two bugs, one shape: the biome wash and the hearing stat were both **built,
wired, displayed and inert**. Neither was a crash, neither showed up in a
fuzzer run, and neither would ever be reported from play, because a player
cannot tell a stat that does nothing from a stat that does very little, and
cannot tell a wash drawn in the wrong colour from a wash drawn in the right
one. The only way to find either was to ask what reads this, and count.

## 121. Two districts, one of them better than its own spec

The chapel and the vent field, the two district items that survived the cut.

**The vent field is a skin, and stayed one.** The Storm-Cage's machinery --
telegraphed, timed, area damage, harvestable -- was already built, and the
roadmap's own duplicate table said to build the system once and skin it twice.
So `districts_tick` now finds either kind, aims at either tile, and words the
message either way; there is no second implementation. The test asserts that
rather than asserting the vents hurt: if the two ever become separate code,
one of them stops being maintained, and that is the failure the duplicate
table exists to prevent.

**The chapel is a better district than the one that was specified.** As
written in the pool it was "sound-based aggro, so it lands free once the
Mycelium is built" -- another skin, and a thin one, since the Mycelium was
already the sound-aggro district. What changed is that hearing became a real
player sense an hour earlier (§120). So the chapel takes it: `hearing_radius`
is zero on chapel ground, and monsters standing there notice you at three
tiles and only down a clear line, because aggro-at-range *is* hearing and
there is none.

That makes it symmetrical, which is the part worth keeping. A district that
only removes something from the player is a penalty, not a place. This one
removes the same sense from both sides, so standing in the silence is a
position you can choose -- you lose your warning, they lose theirs -- and
standing one square outside it is a different position with different
arithmetic. The Mire does the same trick with sight; these are now a pair.

**The weight table silently ate them.** `DIST_WEIGHT[BIOME_COUNT][DIST_KIND_COUNT]`
is initialised row by row, and a row shorter than `DIST_KIND_COUNT` is legal C
that zero-fills the rest. Two kinds added to the enum, no columns added to the
table, and both districts generated perfectly and were never once placed. No
warning: this is exactly the kind of thing `-Wall -Wextra` has no opinion
about.

The suite caught it on the first run -- *"every kind of wild turns up
somewhere"* named both by name. That test was written three sessions ago for a
different reason, and it is the only thing between this change and shipping two
districts that exist entirely in the source code. Worth noticing what kind of
test it is: it does not check that the districts are *good*, it checks that
they are *reachable by the game at all*, which is the assertion a whole
category of silent-drift bugs fails.

**And the test that skipped.** The chapel's first test printed *"(no blind
square within five tiles on this floor -- skipped)"* and reported success. That
is the same failure as §119's fog test and §120's first two hearing tests, for
the third and fourth time in two sessions: a test that does not reach its
subject reports the same green as a test that does. It now searches the floor
for a position where sight is actually blocked, the way the hearing test
learned to. The pattern is worth stating plainly, because it keeps recurring
and it is not about test coverage: **the dangerous test is not the missing one,
it is the one that runs, passes, and never touched the thing.**

## 122. The Inn asked nobody, and the town shrank

Two from the same afternoon, and the second was found while looking for
somewhere to put a new building.

**"A person could save and don't even realise."** The Inn had no prompt at
all, on purpose -- walking in *was* the interaction, the way it is at the
Junkyard. The Junkyard gets away with it because walking in only ever hands
you money. The Inn takes a fifth of the purse and moves the checkpoint, and
the plaza is somewhere you walk *through* on the way to the temple. So the
cost of the mistake scales with how well the run has gone, which is precisely
backwards for an accident. It confirms now, and says the fee and what the
room actually buys -- which differs on Hardcore, where the bed is real and the
resurrection is not.

**And the town was being rebuilt to the size of the window.**
`generate_town_map()` laid the plaza out to `VIEW_W x VIEW_H`, with a comment
explaining that town fits in one screen with no scrolling. That was true when
every render mode showed 58 columns. Tile mode shrinks the viewport to fit a
pixel budget -- which is a change *this session* made larger -- so climbing the
stairs in tile mode rebuilt the plaza narrower, and every building past the new
edge got written into ground that had never been floored.

Measured: at 52 columns the lizard track is walled off; at 46, the Armory, the
Inn and the track; at 40, the same three; at 30, five of the thirteen doors.
No message, no error, nothing to walk into -- the building is *there*, in a
pocket of wall, unlit and unreachable.

Nothing was going to catch this. The screenshot sweep renders the town but does
not walk it. The movement suite walks generated floors but had never been
pointed at the town, because the town is hand-laid and hand-laid things are
assumed correct. And the bug only appears in a render mode that cannot run
under a pipe, at a viewport size that only exists on a real terminal, after a
round trip to the dungeon and back. It is a three-way interaction between a
hand-written map, a mode-dependent constant, and the stairs.

The fix is to stop the town being a function of the viewport at all: it is
`TOWN_MAP_W x TOWN_MAP_H` now, the same place whoever is looking at it, and the
camera clamps to the plaza rather than the dungeon grid so a narrow window
scrolls instead of showing a screen of nothing.

**A footnote with teeth.** Those constants were called `TOWN_W` and `TOWN_H`
for about a minute. `#define TOWN_H` collides with `town.h`'s own include guard
`#ifndef TOWN_H` -- so defining it in `common.h` silently skipped that entire
header, and `TEMPLE_DOOR_X`, `PLAYER_TOWN_START_X` and `generate_town_map`
went undeclared at every call site simultaneously. Ten errors from one macro
name, none of them mentioning the macro. Worth remembering the next time a
build breaks in more places than the change touched.

The test now sweeps six viewport widths and floods from the player's arrival
square to all thirteen doors. Against the restored bug it names eleven
failures across four widths.

## 123. Three doors in the plaza

The Oracle, the Altar and the Bazaar -- the last three items on the roadmap
that were not balance.

**The Oracle** sells information, which nothing else in this economy did. Three
readings, one mechanism at three scopes: mark the way down, map the next floor,
map the next three. It is the only shop whose value *rises* with the world
size, which is what made it the strongest of the four candidates on a game
whose largest floor is 1400x800.

The test that matters is not "does the reveal work" but that the cheap reading
and the dear one are actually different. They look identical from the buyer's
side on the turn they pay -- both say "you know something you did not" -- and
if the cheap one quietly revealed the floor, nobody would ever notice, and the
dear ones would be worthless. So: the way down marks **one** square, and the
assertion is `seen_total < 200`. With the distinction removed it reports 7,302.

**The Altar** is the reallocation sink the roadmap circled in four separate
places. A point off one attribute, a point onto another, and still half the
School's fee on the School's own `200v^2 + 300` curve -- a trade that cost only
a point would be a free respec, which this game does not do.

The interesting part is underneath. `train_attribute()` works by deriving two
sheets, raising the attribute on one, and carrying every derived stat across by
the difference. There is no un-train, so the Altar could not be two calls to
it -- and even if there were, two sequential recomputes are not the same as one
recompute of the finished sheet anywhere a derived stat is clamped, which
several are. So the body was lifted into `shift_attributes(h, give, take)` and
both doors now use it: one before-sheet, one after-sheet, one set of deltas,
whatever the move was.

**The Bazaar** re-prices between trips, 55% to 150%, and shows the percentage
so a good day is legible without having memorised the list. The day advances on
descent rather than on entering town, deliberately: a shop you can re-roll by
stepping through a door twice is a slot machine, not a market.

**One duplication removed on the way.** `discounted_price()` was static in
main.c, and the same arithmetic was written out inline at seven places in
render.c -- the tills used the function and the screens used the copies.
Nothing had drifted, but a shop whose displayed price is computed separately
from its charged price is one edit from lying to the player, and the Bazaar
makes that materially worse by moving the base price under both. It is one
function in items.c now, used by both sides, and `bazaar_price_pct()` is
likewise shared between the screen and the till rather than being computed
twice.

**And a note on where the seam was.** Two of these tests could not be written
at first, because the code they needed was `static` in main.c -- the Oracle's
spend, and `apply_reveal_perks` around it. The fix was not to widen the test
harness but to move the logic: `oracle_spend_reading()` lives in common.c now,
because "did the player get what they paid for" is a question about the game
and not about the menu that sold it. The opposite call was made for the Altar's
derived-stat check, where the honest assertion was against the documented
formula (`maxhp = 30 + (BRAWN + STAMINA) * 2`, so one point into Brawn must
move it by exactly two) rather than exposing `hero_compute_derived` to satisfy
a test. Widening an API to make something testable is sometimes right and
sometimes the test changing the code; the difference is whether the thing being
exposed is game logic or an implementation detail.

All three screens fit 24x80 -- §3.2's third rule -- and are in the sweep, which
now walks sixteen.

## 124. The bug hunt before balance

Five findings, and the two worth the hunt were both in code written the same
day.

**Self-play could not reach the three newest screens.** `autoplay.c` keeps a
`TOWN_TOUR` -- the list of doors the policy walks into, because walking onto
the tile is what opens a shop, and its own comment says every service in the
plaza is "a screen the balance harness structurally cannot reach, which is the
entire reason this mode exists". The Oracle, the Altar and the Bazaar were not
on it. So the three least-exercised screens in the game were also the only
three the fuzzer could not open: exactly backwards, and silent, because a tour
that visits fourteen doors looks no different from one that visits seventeen.

The test derives the list from the town map rather than restating it, so it
keeps holding as doors are added. Against the restored gap it names all three
by tile type.

**Hearing was handing over the map.** The worse one, and it had shipped four
commits earlier. `cell_apply_heard()` marked the square by setting
`has_occupant = false` and `seen = true`, reasoning that hearing reports a
position and not an identity, and that an unseen cell draws nothing. Both
halves were wrong, in different render modes.

`seen = true` makes every renderer draw the *terrain* of a square the player
has never been shown. So the sense sold as "something is behind that wall" was
also reporting what the floor back there is made of -- in block mode through
the cell's background colour, in tile mode entirely.

And `has_occupant = false` meant tile mode drew no mark at all: it composes
terrain plus occupant and never looks at `glyph`. The one mode where the leak
was total was also the one with nothing on screen to explain it. A player would
have seen unexplored corridors quietly filling in and had no way to connect it
to the stat they had bought.

It is an occupant with its own sprite now, `seen` is left alone, and the three
draw paths each handle "heard on ground you have not seen" by drawing the mark
on blank. `tests/tileart.c` asserts the sprite is not biome-tinted (it is not
part of the floor), and -- the assertion that would have caught the tile-mode
half -- that it is not almost entirely transparent.

**Three smaller ones.** Working a geothermal vent said *"cutting the ore out of
the rod"* and *"The ore comes free"*, because the vent reused the rod's work
kind; it has its own words now. `Map.wild_kinds` is a 32-bit mask indexed by
`DistrictKind`, which is at 27 -- the thirty-third kind would shift by 32,
which is undefined behaviour rather than a wrong answer, so it is a
`_Static_assert` now and fails the build instead. And `heard_rebuild` called
`district_at` -- a linear scan -- once per monster per body per turn.

That last one is worth recording for how it was measured rather than for what
it was. The first timing said 0.146 ms per update on a Swarm floor with 1,471
monsters, which looked small, and it got written up as "cost nothing worth
reporting, this is tidiness rather than a fix". The A/B said 0.151 ms against
0.056 -- the scan was about sixty per cent of the whole vision update. The
absolute number was small and the proportion was not, and only one of those two
was measured before the conclusion was written. **Small and unnecessary are not
the same measurement.**

Both the plain fuzzer and the ASAN/UBSan build ran throughout and reported
nothing, across five megabytes of play log. Neither would have found any of the
five: none is a crash, and four of them are the game doing something wrong
quietly and correctly-in-C.
