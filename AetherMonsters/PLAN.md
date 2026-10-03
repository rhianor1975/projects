# Aether Monsters — implementation plan

*Working title.* A fork of the Godot edition of Aether Descent
(`../AetherDescent/godot`). It turns the hundred-floor descent into a
creature-catching RPG: Final Fantasy-style battles, Pokémon-style collecting,
and the same Well underneath.

This document is the plan, not the game. Every section ends in things that
can be built and tested one at a time; the phases at the end say in which
order.

---

## 1. The pitch in one paragraph

You are a **Binder**: someone who goes down the Deep Well with a team of
creatures instead of a sword. You catch what lives on each floor, the creatures
level and evolve, and other Binders on the way down challenge you. Every tenth
floor has a **Warden** who guards the way, with a team built around one type.
Floor 100 holds the thing at the bottom of the Well. Battles are side-view and
menu-driven in the Final Fantasy style, with up to three creatures a side.
Catching, types, evolution and the creature log are in the Pokémon style.

---

## 2. Decisions to make first

Each decision has a recommended answer, so work can start without waiting.
Any of them can be changed later at small cost if we decide early.

| # | Question | Recommendation | Why |
|---|---|---|---|
| D1 | Turn-by-turn, or Final Fantasy's timer bars (ATB)? | **Turn-by-turn with a visible turn timeline**, like Final Fantasy X (CTB). Faster creatures act more often. You can see who goes next. Nothing happens while you think. | Easy for a child to read. No time pressure. Agility and Reflexes matter on every turn. |
| D2 | Does the Binder fight? | **No, but the Binder acts.** On the Binder's turn: items, Catch, Swap, and one *Binder skill* from their class. | Keeps the Pokémon feel (creatures fight) and the Final Fantasy feel (a party with commands). The 100 classes stay useful. |
| D3 | How many creatures fight? | **Up to 3 on the field a side, 3 in reserve (6 carried).** Front and back row, as in Final Fantasy. | Three-a-side is what makes it feel like Final Fantasy, and rows add tactics at almost no cost. |
| D4 | What happens when you lose? | **Two modes, chosen at the start.** *Adventure* (default): your team faints, you wake at the Inn, you lose half the gold you carried, and the floor resets. *Hardcore*: a fainted creature is gone for good and a wipe ends the run, as in Aether Descent. | A child needs a gentle default, and the roguelike edge stays available for whoever wants it. |
| D5 | Random encounters, or visible monsters? | **Both.** Creatures you can see roam the floors and battle you if they touch you. Tall grass, water and other district tiles also have random encounters, with an item and the *Presence* attribute to keep them away. | The map code already handles visible monsters. Grass tiles already exist. |
| D6 | Are creatures designed or generated? | **Hybrid.** About 30 designed families (around 90 creatures with evolutions) plus generated *wildforms* that fill the hundred floors. | See section 7. |
| D7 | Should the moves cost something? | **Aether (MP) per creature, as in Final Fantasy**, refilled at the Inn, by resting, and by items. No per-move PP. | One number to track instead of four. The *Conduit* attribute already means "aether pool". |
| D8 | Names | Everything original: creatures, items and terms. We can borrow mechanics, not names. No "Poké-", no "Potion of Final Fantasy". | The game needs to be ours, and it avoids trademark trouble. |

---

## 3. What happens to each part of Aether Descent

The fork keeps more than it throws away. Here is every system and its fate.

### Kept as is
- The engine setup, the 640×360 pixel scaling, the menus and windows (`Menu`, `Gfx`), the sound effects, and the 16-bit/Classic art switch.
- Save and restore (new fields only). Records, high scores and the fallen roster.
- Map generation: all floors, biomes, the 27 districts and both world sizes.
- Field of view, map memory, hearing, auto-explore and the minimap.
- Gamepad support, export presets and the test setup (smoke run, fuzzer, screenshot drivers).

### Kept, with a new job

| Aether Descent | Aether Monsters |
|---|---|
| **Hero** (`hero.gd`), 100 classes, 30 attributes | The **Binder**. Their class gives a Binder skill and team-wide bonuses. Their attributes become the *Binder's talents* (catch rate, shop prices, how far you see). |
| **Monster** (`monster.gd`) on the map | A **wild creature or trainer** on the map: a token pointing at a creature or team. Touching it starts a battle. |
| **Hired heroes** (`party.gd`): their walking, pathing and AI | The **lead creature follows you** on the map, as in Pokémon HeartGold. Map **trainers** use the same walking and pathing AI to patrol, spot you and walk up. |
| **Tavern** | **The Binders' Lodge**: trade creatures with NPC Binders (they want X, they give Y), rematch Binders you've beaten, and take escort and catch bounties. |
| **Quest board**: kill, fetch, clear, timed, no-recall, escort | Adds **catch** ("bring back a Volt type"), **defeat a Binder**, and **show me**. The escort client keeps its role. |
| **Inn** | The **Inn** heals the whole team and refills aether. It stays cheap rather than free, because gold should matter. |
| **General store / Apothecary** | Binding crystals, healing draughts, status cures, repel incense. |
| **Armory** | **Charms**: one held item per creature (like held items), upgraded with the existing smith and materials system. |
| **Arcanist's Guild** | The **move tutor**: the old spell scrolls become move scrolls that teach a creature a move its type allows. |
| **Gladiator School** | The **Battle Hall**: a ladder of Binder fights and the place to re-fight Wardens. |
| **Black Market** (sells levels) | The **Rebirth Clinic**: take a creature back to level 1 and it keeps +1 in its best attributes. The "sell levels and earn them back" loop becomes a real way to train. |
| **Kitchen** (meat from clean kills) | Cook **meals** from what battles drop. Meals raise one attribute group a little. This is the training system (the role of EVs), but you can see and understand it. |
| **Lizard Track** | Race **your own creatures**. Agility, Stamina and Balance decide the race. |
| **Altar** | **Fusion**: two creatures become one that inherits parts and attributes. This fits the generated creatures perfectly; see section 7. |
| **Bank, Junkyard, Bazaar, Oracle** | Bank and Bazaar unchanged. The Junkyard sells materials. The Oracle tells you where a rare creature is on the next floor. |
| **Districts** | Each district is a **battle terrain** with its own rule, and an **encounter table**. See section 6. |
| **Biomes** | Decide which **types** live there and how levels scale. |
| **Floor events, gold rush, overrun** | Become a **swarm floor** (more of one species), a **rare bloom** (a rare creature roams), and so on. |
| **Warden on floor 100** | The **legendary** at the bottom of the Well. Ten **floor Wardens** are added (floors 10, 20 … 90) as gym leaders. |
| **DDA pressure and band gifts** | Same idea: invisible help for a run that is struggling, such as slightly better catch odds and gentler wild levels. |
| **Guardian angel** | The same opt-in assist, now covering battles. |
| **Barrow of the Fallen** (ghosts of your dead heroes) | Ghost **teams from your earlier runs**. Beat your old self. |
| **Mirror district** | A Binder with a copy of your team. |
| **Proving ground** (rules like "no ranged") | Battle **rules**: no items, single creature, level cap. |
| **Arena** | A **gauntlet** of Binder fights in a row with no healing. |

### Removed
- Bump-to-attack combat on the map, ranged weapons and ammunition, and the hero's spells cast on the map. All fighting moves into the battle screen.
- Hiring heroes, the hire roster, "pass the torch" and switching bodies with `p`.
- Weapons and armour for the Binder. Charms replace them.

---

## 4. The 30 attributes: what makes creatures interesting

This was the best idea in the request. Pokémon has six stats. Aether Descent
has thirty attributes, already wired into derived values in `hero.gd`. We
give **every creature all thirty**. Most creature games have nothing like it,
and it fixes three of Pokémon's problems:

1. **Every creature is good at something.** A creature too weak to battle
   may have the best Vision or Tech-Wit on your team, and that matters on
   the map.
2. **No "HM slaves".** In Pokémon you carry useless creatures to cut trees.
   Here the team's best value for each attribute applies on the map
   automatically. A good scout is a real reason to carry a weak creature.
3. **Individual creatures differ.** Two creatures of the same species roll
   different attributes, so catching a second one is worth it.

### Groups, so a child can read it

The thirty attributes come in six groups of five. Battle screens show six
**group grades** (S/A/B/C/D), and the full thirty are on the creature's
profile page.

| Group | Attributes | In battle | On the map |
|---|---|---|---|
| **Body** | Might, Brawn, Grit, Fortitude, Stamina | Physical attack, HP, physical defence, status resistance | Walk on hazards (Fortitude), recall faster (Stamina) |
| **Speed** | Agility, Reflexes, Balance, Precision, Instinct | Turn speed, evasion, accuracy, critical hits, first strike | Footing on quicksand, belts and currents (Balance); not ambushed (Instinct) |
| **Mind** | Intellect, Cunning, Memory, Resolve, Tech-Wit | XP gain, move slots (Memory: 4 to 6), resisting confusion and fear | Consoles and machines (Tech-Wit), more gold (Cunning) |
| **Heart** | Charm, Guile, Presence, Empathy, Fortune | Intimidate, luck, steal, escape | Shop prices (Charm), keep weak wild creatures away (Presence), catch odds (Empathy), rare finds (Fortune) |
| **Craft** | Crafting, Chemistry, Scribing, Vision, Hearing | Item potency in battle (Chemistry) | Sight radius (Vision), hear Binders through walls (Hearing), find materials (Crafting), read rune walls (Scribing) |
| **Aether** | Aether-Sense, Conduit, Resonance, Warding, Jinx | Aether pool, special attack, special defence, chance to cause statuses | See in the Abyss (Aether-Sense) |

### Where the numbers come from

- **Species profile.** Each species has a base value for each attribute, with points spread like Pokémon's base stats.
- **Individual roll.** −2 to +2 per attribute when the creature is caught or hatched (the role of IVs). Shown openly as "talents".
- **Nature.** One attribute up and one down. `shift_attribute()` in `hero.gd` already does exactly this.
- **Training.** Meals from the Kitchen, Rebirth at the clinic, and charms.
- **Level.** Growth per level follows the species profile.

All the formulas for derived stats live in one function (as `compute_derived()` in
`hero.gd` does today), so there is exactly one place to balance.

---

## 5. Battles

### Screen
- Side view. Enemies on the left, your team on the right, as in Final Fantasy 4–6.
- **One sprite per creature, drawn facing right.** It's mirrored for your side. This halves the art: no separate front and back sprites, as Pokémon needs.
- The turn timeline runs along the top: the next eight turns, as portraits.
- Bottom windows: your creatures' HP and aether bars, and the command menu.
- A short transition from the map (screen shatter or swirl), and back again.

### Commands
- **Creature turn:** Fight (pick a move, then a target), Guard, Swap (with reserve), Flee (wild battles only).
- **Binder turn:** Item, Catch (wild only), Command (the Binder skill: *Rally* +speed for a round, *Read* reveals an enemy's types and attributes, *Taunt* forces targeting, and so on, set by class archetype), Swap two creatures.

### Rules
- **Damage:** move power × attacker's attack ÷ target's defence, × type effectiveness (0, ½, 1, 2 and 4 for dual types), × same-type bonus (1.25), × row (back row takes and deals ¾ physical), × critical, × terrain. Every factor comes from one function with tests behind it.
- **Targeting:** each move hits one target, a row, all enemies, an ally, or the whole team, as in Final Fantasy.
- **Statuses:** the existing poison, stun and slow, plus burn, sleep, confusion, fear (from Presence), petrify (the Petrified district) and bleed. Each status has a matching resisting attribute.
- **Experience:** shared by every creature that took part. Reserves get half.
- **Enemy AI:** weighs the moves it has (type advantage, finishing a low target, healing). Wardens and the Rival are smarter: they swap out of bad matchups.
- **Speed options:** 1×/2×/4× animation, skip animations, and **Auto** (repeat the last command; the battle version of auto-explore) for grinding.

### Catching
- Odds rise as the target's HP falls, with sleep and stun, with a better crystal tier, and with Empathy and Charm (the Binder's and the lead creature's). They fall with species rarity. DDA helps a little when things are going badly.
- The crystal shakes three times. Every shake is a real roll, so the tension is honest.
- A full team sends new catches to the **Vault** (the Bank's back room), reachable from town.
- You can't catch a Binder's creatures.

---

## 6. Types, biomes and districts

### Twelve types
Beast, Flame, Tide, Root, Volt, Stone, Gear, Venom, Gale, Spirit, Aether, Void.

- Each type is strong against two or three types and weak to two or three. Creatures can have two types.
- Every type gets an icon with a distinct **shape**, not just a colour, so it can be read by colour-blind players.
- A test checks the type chart is balanced: each type has about the same number of strengths and weaknesses, and nothing goes unanswered.

### Biomes decide who lives where

| Biome (floors) | Common types |
|---|---|
| Jungle Roots (1–20) | Root, Venom, Beast |
| Company Works (21–40) | Gear, Volt, Flame |
| Flooded Ruins (41–60) | Tide, Stone, Spirit |
| Salt Wastes (61–80) | Stone, Gale, Flame |
| Abyssal Approach (81–100) | Void, Aether, Spirit |

### Districts become battle terrain and special habitats
The 27 districts become 27 battlefields, each with its own rule and its own
creatures. A few examples:

- **Storm-Cage:** Volt moves +50%, and lightning strikes a random combatant every few turns. That's the storm telegraph we already have, moved into battle.
- **Sea, Aqueduct:** Tide +50%, Flame −50%.
- **Geothermal:** Flame +50%, and creatures without Fortitude get burned.
- **Quiet Quarter:** sound moves fail.
- **Eye of the Storm:** the safe quarter becomes the safe row.
- **Mirror:** a Binder with a copy of your team.
- **Prism / Chromatic Abyss:** home of **prismatic** creatures, the rare palette (the role of shinies).
- **Crystal:** catch odds +25%.
- **Proving Ground:** a battle rule (no items, single creature, level cap).

**Evolution by place:** some species only evolve when they level up inside a
certain district ("Sparkit evolves in the Storm-Cage"). That gives the
districts a lasting purpose.

---

## 7. Creatures: designed and generated

### About 30 designed families (around 90 creatures)
The starters, the Wardens' signature creatures, rare species and legendaries.
Each one has a name, a description, a hand-set attribute profile, a move list
and a clear look. This is where your son designs creatures: he describes or
sketches one, and we build it in the art generator with hand-picked parts.

### Generated "wildforms"
The art generator (`tools/gen_art.py`) builds creatures from parts:

- **Body plans:** blob, quadruped, biped, serpent, bird, insect, fish, golem and floating.
- **Parts:** heads, eyes, ears, horns, crests, tails, wings, spikes and patterns.
- **Type:** sets the palette and some parts (Flame adds flame tufts, Gear adds rivets).
- **Evolution:** the same seed grows bigger, with more horns, longer tails and a richer palette, so a family always looks related.
- **Names:** made from syllables chosen by type and body.

Every wildform comes from a seed. The same seed always gives the same
creature, so the creature log, trading and saves all work with numbers.

### Problems and how to handle them
- **Generated creatures can look samey.** Hand-curate the parts library, make review sheets of 50 at a time and remove bad combinations. Keep designed creatures for every moment that matters (starters, Wardens, legendaries).
- **Sprite sizes:** 64×64 in battle, at most 96×96 for bosses; 16×16 or 32×32 on the map. All of them come from the generator, with no hand-pixelled frames.
- **Animation:** two-frame idle (bob or breathe), a lunge to attack, a flash when hit, a fade when fainting. Cheap, and it reads clearly at this size.

---

## 8. Binders in the dungeon (replacing hired heroes)

- **Wandering Binders:** they stand or patrol. When you enter their **line of sight** (the field-of-view code we already have), a "!" pops up, they walk to you (hire pathing) and the battle starts. *Hearing* lets you know where they are before they see you.
- **The Rival:** appears about every ten floors with a team that grows and evolves. They start with the starter strong against yours.
- **Floor Wardens (10, 20 … 90):** gym leaders, each built around one type, in a set-piece room. Beating one gives a **Seal**, and each Seal unlocks something: higher creature levels obey you, a map talent, a shop tier.
- **The bottom of the Well (100):** the legendary, plus whatever we decide the story is.
- **Ghost teams** from your earlier runs in the Barrow (from `fallen.json`).
- **Bounty Binders** from the quest board.
- Beaten Binders pay gold, and some trade or rematch at the Lodge.

---

## 9. Progression and stakes

- **Within a run:** your team, gold, items, Seals and deepest floor.
- **Across runs (Hardcore mode):** the **creature log** stays: seen and caught, with habitat (biome, district and floor range) and a silhouette for creatures seen but not yet caught. Catching a species once unlocks it as an optional starter for later runs. This is the reason to start again.
- **Adventure mode** is a single long save with no run resets, for a child's first playthrough.
- **Hall of Fame:** the team that reached floor 100, saved with a screenshot card.

---

## 10. Improvements beyond the two inspirations

- **Link battle:** two players with two controllers, each with a saved team, fighting on one screen. Probably the most fun thing for a parent and child.
- **Field talents instead of HMs:** see section 4.
- **The lead creature follows you on the map,** reacts to things (finds items, growls at hidden Binders) and shows its mood (friendship).
- **Fusion at the Altar:** generated creatures make it easy to blend two into one.
- **Rebirth:** the Black Market loop turned into a real training method.
- **Meals:** stat training you can see and choose.
- **Creature races** at the Lizard Track.
- **Accessibility:** text speed, battle speed, type icons with shapes, and an easy mode with bigger catch odds and fewer wild battles.

---

## 11. Risks, and what we do about them

| Risk | What we do |
|---|---|
| **Scope:** creature games are mostly content | A **vertical slice** first (Phase 4): floors 1–10, about 8 species, one Warden, full battle and catch loop, placeholder art. Then only content and polish. |
| **Balance with 30 attributes and 12 types** | One formula function. A headless **battle simulator** fights thousands of random battles (same style as the smoke test) and reports win rates per type, species and level gap. A failed balance rule fails the test run. |
| **Too many numbers for a child** | Group grades on the main screens, numbers only on the profile page; "super effective" shown as an icon and a word. |
| **Grinding gets boring** | Auto-battle, battle speed, Presence and incense to avoid weak wild creatures, and XP shared with reserves. |
| **Losing feels bad** | Adventure mode by default (wake at the Inn); Hardcore is a choice. |
| **Generated creatures look bad** | Review sheets before committing to them; designed creatures for every important moment. |
| **The fork drifts from Aether Descent** | Accepted. A fix to shared code (map generation, menus) is ported by hand when it matters. The fork is a separate game. |
| **Saves clash with Aether Descent** | Avoided: the project name is different, so Godot gives it its own save folder. |
| **Trademarks** | Original names, art and terms (D8). |
| **Battle UI on a gamepad and the web build** | The menu system already handles both; the battle screen uses the same `Menu`. Fuzzer runs cover battles too. |

---

## 12. Code layout (new and changed files)

```
scripts/creatures/
  species_data.gd      designed species: profile, types, moves, evolution, art parts
  wildforms.gd         seed -> generated species (body, parts, types, profile, name)
  creature.gd          one creature: species, level, xp, 30 attributes, derived stats,
                       moves, aether, status, charm, friendship, nature
  types.gd             12 types, chart, icons
  moves_data.gd        moves (ported from spells and abilities, plus new ones)
scripts/battle/
  battle.gd            rules only: turn timeline, damage, statuses, terrain, AI, catch.
                       No drawing. Pushes events, as rules.gd does for MapView.
  battle_ai.gd         enemy decisions, Warden and Rival behaviour
  battle_view.gd       side-view drawing and animation of battle events
  battle_ui.gd         command menus built on Menu
scripts/core/
  binder.gd            the player (was hero.gd): class, Binder skill, talents, team, vault
  trainers.gd          map trainers: sight, approach, teams, Rival, Wardens
  encounters.gd        wild tables by biome, district and floor; grass and water encounters
  (game, rules, mapgen, districts, quests, kitchen, dda: adapted as in section 3)
tests/
  battle_sim.gd        thousands of headless battles, balance report, fails on rules
  smoke.gd             gains: catch, evolve, trainer battle, save with team
tools/gen_art.py       creature parts library, battle and map sprites, review sheets
```

Battle rules stay separate from drawing, as the current game keeps
`rules.gd` apart from `map_view.gd`, so the simulator can run thousands of
battles headless.

---

## 13. Phases

Each phase ends in something playable or testable, committed and pushed,
with screenshots.

| Phase | Delivers | Size |
|---|---|---|
| **0. Fork** *(done)* | `AetherMonsters/` on its own branch, renamed, own saves, own export presets. Identical to Aether Descent so far. | — |
| **1. Creature model** | `creature.gd`, `types.gd`, `moves_data.gd`, 8 test species. Attribute groups and grades. Tests for derived stats and the type chart. | M |
| **2. Battle rules** | `battle.gd` and `battle_ai.gd`: timeline, damage, statuses, rows, catching, XP and level-up. Headless battle simulator with first balance report. | L |
| **3. Battle screen** | `battle_view.gd`, `battle_ui.gd`: side view, menus, animations, transition, sounds, speed options, Auto. Placeholder sprites. | L |
| **4. Vertical slice** | Encounters on the map (visible creatures and grass), starter choice, lead creature follows you, team menu and creature profile, Inn heals the team, Vault. Map combat removed. Floors 1–10 with 8 species and the first Warden. **Playable start to end.** | L |
| **5. Creature art** | Parts library and generator, battle and map sprites, evolution growth, review sheets. The first 10 designed families with your son. | L |
| **6. Binders** | Line-of-sight trainers, the Rival, ten Wardens, Seals, ghost teams, Lodge trading and rematches. | M |
| **7. Town conversion** | Every building from section 3: Mart, charms, move tutor, Battle Hall, Rebirth Clinic, meals, races, Fusion. | M |
| **8. Content** | 30 designed families, wildforms on every floor, encounter tables for every biome and district, district battle terrain, evolution by place, prismatic creatures. | L |
| **9. Progression** | Adventure/Hardcore, the creature log, starter unlocks, Hall of Fame, records. | M |
| **10. Polish** | Balance passes with the simulator, link battle, accessibility options, builds for Mac/Windows/web. | M |

**Phases 1–4 are the critical path.** After Phase 4 the game can be played
from start to end, so every later choice can be made by playing it rather
than by guessing.
