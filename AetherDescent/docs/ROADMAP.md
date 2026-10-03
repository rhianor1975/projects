# Aether Descent — Improvement Roadmap

*Last updated: 2026-08-15 (EVALUATION §103). `2.5` is closed by decision --
ncurses stays the default and notcurses stays the opt-in `-nc` build, because
every instrument on this project drives ncurses and notcurses will not start
under a pipe. Then §3.1's next tranche: **the watch** (79 Stealth District,
the last unbuilt entry on that section's own shortlist) and **the barrow**
(91 Gauntlet of the Fallen). Nineteen district kinds now.*

*The larger find was not either of them.* Asked to re-check every district for
places a player or the AI could get stuck, a new fourth suite
(`tests/movement.c`) found that **more than half of every jungle ever
generated was missing a piece of itself** -- 15.9% of the overgrowth's ground
unreachable, on 161 instances in 308, with the bog, the mist and the flooded
quarter behind it. Districts are carved before the rooms and joined to the
floor at one anchor tile, so any district that closed a ring with its own
terrain stranded everything the anchor was not in. `district_relink()` takes
all seventeen to 1.3% and costs 1% of floor generation. Separately,
`is_walkable_delegated()` excluded the aqueduct's current and not the assembly
line's belt -- the same reasoning, never carried forward to the second tile
that uses it, which is why the rule is now asserted over the tile *types*.

*Three faults in the test suite fell out of leaning on it, none of them in the
game, and two of them would have been "fixed" by adjusting the game. §103 has
them.*

*Then three bugs from play (§104).* **"No known path to the stairs"** was
auto-explore treating *seeing* the staircase as a reason to stop exploring:
both searches walk revealed ground only, so the message meant "the route has
not been uncovered yet", which is what exploring is for. **The stripe down the
side of the panel after a shop** was the play screen clearing regions and
getting the arithmetic wrong for the third time; on the project owner's call it
now clears the whole screen, and the measurement that had argued against
`erase()` for three sessions turned out to be worth 0.3-2% of a frame.
**And a hire would stand in front of a monster cursing it** rather than
killing it -- a catch-all `else` that made control spells permanently "useful"
and a weight that made a level-5 curse outrank a level-1 bolt.

*And then `Auto-explore: getting nowhere here`, which was the aqueduct*
(§105). **707 of 707** stuck reports had "The current takes you 1 tiles."
immediately before them: a body standing in a channel is pushed back exactly as
far as the route just stepped it, so it ends every turn where it began. The
detector was right; what was missing was the other half of a rule already half
written -- `is_walkable_delegated()` stops a walk *routing onto* conveyed
ground and nothing got it *off* ground it was already on. Fixed, and **none in
15,000 keys** after.

*Last updated: 2026-08-10, after the six-actor rebuild (EVALUATION §92): one
movement function, one attack, one tick and one AI for all six, and nothing
that follows anybody. Then §93 -- every actor can do what its class allows,
with `party[0]` gone from every gameplay file -- §94, the screens taking the
actor they draw, §95, the armory made to fit a real terminal and the material
tiers made to match the biomes they are named for, §96, a bug hunt whose worst find was that a
hire could kill the Warden without ending the run, and §97, the review list --
one defence pipeline, one animation system, and `make shot`, which can finally
look at a screen. Preceded the same day by the Hero refactor (§90) and the
per-actor maps (§91).*

*Balance is unverified against all three, and `p->gold` at new-game is a
600,000,000 testing value -- the shipping number is 60. Put it back before
measuring anything.*

*Earlier that day: the Hero refactor (§90) and the
three play-reported bugs that followed it, fixed with one map per body (§91).
Before that, the same day: the prestige
playstyle, the fuzzer's navigation policy, the matrix, the Gladiator School
fix, the guardian angel, the swarm rebuild, and the difficulty ladder finally
standing the right way up (§89).*

**Read the balance numbers on this page with that in mind.** The refactor
changed one thing a run can feel -- a driven body's cooldowns, aether and buff
timers now advance, where before the body you were steering was the one body
time did not pass for -- and nothing in §83 or §89 was measured against that.
Re-measuring the ladder now comes before the world-size axis below.

**The ladder is fixed and shipped.** Normal easiest, then Hard, then Swarm,
then Hardcore: monotone across rush, farm and prestige at 100 runs a cell, on
both 90th percentile and best floor. It was inverted before -- Hard was the
hardest mode in the game -- and it was not fixed by tuning. It was fixed by
three mechanics whose punishing half had shipped and whose compensating half
never had:

- swarm monsters were full-strength Normal monsters, twenty times over, paid
  at trash rates (§88);
- the horde did not exist on floor 1 -- Swarm opened at *Normal* density while
  Hard opened at double (§89);
- the infestation did not infest: Swarm and Hardcore were the only modes where
  clearing a room cleared it permanently (§89).

Every one was found by the project owner saying what a mode was supposed to
be and the code disagreeing. None would have come out of a constant sweep.

The three items that headed this file are done. What they turned up:

- **PRESTIGE is in** (EVALUATION §78), sharing FARM's every constant so the
  only difference between the rows is the cash-out, with the Hardcore
  identity as a standing control.
- **`--silentrun` navigates** (§81). It tours the town's services, takes the
  temple door, and hands each floor to auto-explore. Six instrument bugs
  stood between "paths to the stairs" and "descends 62 times in a run", each
  hidden behind the one in front of it.
- **The matrix is measured** (§83), most cells at 100 runs, and the §72
  numbers it replaces were as unreliable as §72 suspected: Normal rush
  13% -> 14%, but Normal farm 50% -> 35%. Normal prestige is the best style
  in the game at 46%, and loses nothing at all below floor 10.
- **Two confirmed game bugs** (§77, §82): the Gladiator School was deleting
  every level, shrine bonus and ranged weapon the character had earned, and
  every death in the game reported that it happened on floor 0.

The §83 matrix predates the swarm rebuild and the ladder fix, so read it as a
record of how those numbers were arrived at rather than as the current game.
§89's table -- rush, farm and prestige at 100 runs a cell -- is what the game
now does.

**Start here next session.** In value order:

-1. **Commit often enough to bisect.** (EVALUATION §105.) The item below asks
   for a bisect and one is not possible, though not for the reason first
   written here: the project *is* under version control, and has been since
   `d1a20b4` on 2026-08-08. What it did not have was a second point. That one
   commit predates `--silentrun` and the Hero refactor alike, so it cannot run
   the harness the claim is measured with, and there are no revisions at all
   inside the window where the regression is supposed to live.

   There is a second commit now. The rule that follows from it: **commit at
   every green state** -- suites passing, warning scan at 3 -- so the next
   thing that gets slower can be located instead of guessed at.

-0.5. **The per-key cost: profiled, not fixed** (EVALUATION §105). What is now
   known: one auto-explore step is 7,481 samples and **3,827 of them are
   `autoexplore_route()`**, almost all inside `bfs_next_step_to_item()`. The
   loot search is the most expensive thing the walk does. Two optimisations
   were measured and both rejected -- caching the failure does nothing (741
   turns against 739), caching the destination is 12% *worse* (662 against
   749) -- and the reasoning is in the comment above the call so nobody repeats
   them. Ruled out as causes: the band gifts, the set procs, and the district
   relink pass. The original entry follows.

-1b. **The per-key cost has roughly quadrupled** and nobody knows why
   (EVALUATION §100). Same seed, same draw, same world: 115s for 20,000 keys
   at §92 against 203s for 8,000 now. Proved *not* to be the band gifts or the
   set procs by an A/B at zero. Leading suspect is §97's defence unification --
   every body now rolls crit, evasion, ward and status on every blow taken.
   It matters beyond comfort: it is the difference between the ASAN build
   finishing a run and timing out. Bisect it one change at a time; it wants
   the same controlled before/after the ladder does, so do them together.
   *That pairing no longer holds:* the ladder is balance work and balance is
   being done last, deliberately, because it is the closing act of the project
   and everything above it invalidates the numbers. This is a performance
   regression, timed in keys rather than measured in win rates, and it stands
   on its own.

0. **Re-measure the ladder after the Hero refactor** (EVALUATION §90). It is
   item zero rather than item one because every number in items 1-4 below was
   taken before it, and one of them -- the ladder in §89 -- is measured on
   exactly the axis the refactor moved. Cheap: the simulator already runs.

1. **Measure the world-size axis before tuning anything per-difficulty**
   (EVALUATION §85). This displaced everything below it. The whole of §83 --
   and every winnability number this project has ever quoted -- was run on
   **The Shaft**, the smallest of four worlds and a hundredth of the area of
   the largest. Map size turns out to dominate difficulty:

   | rush, n=20 | Shaft | Halls | Deeps |
   |---|---|---|---|
   | Normal, win | 14% (n=100) | 70% | 95% |
   | Hardcore, 90th percentile | 2 | 2 | 22 |
   | Hardcore, health between floors | 74% | 91% | 100% |

   Monster density is held constant across worlds; the *walk* is not. A bigger
   floor means more encounters between the entrance and the stairs at the same
   depth, so experience scales with a floor's area while monster strength
   scales with its number, and the two run away from each other.

   Swarm is the exception and worth understanding: on the Halls it improves
   much less (best 5 -> 11, still 0%) because its pathology is crowding, and
   a bigger map has proportionally more monsters. Worst crowd went 391 -> 4,463.

2. **Every mode below Normal is still 0% wins**, and that is now the open
   question rather than the ordering. The ladder discriminates cleanly on
   depth -- 90th percentiles of 100 / 13 / 4 / 3 on rush -- but nobody kills
   the Warden outside Normal. Per the owner's principle that every mode must
   be finishable, Hardcore is the one that proves it. Note the ladder lives in
   the *tail*: medians are 9/2/2/1 and Hard ties Swarm there.

   Two known levers, neither swept: the early experience rate on Swarm and
   Hardcore is a flat 1 per kill and cannot be changed by the divisor (§87 --
   it is mapgen's per-kill cut and grant_xp's clamp that bind), and Swarm now
   ends 14 runs in 100 *pinned* rather than dead, which is the infestation
   working and may or may not be the death it should count as.

   **Judge any fix on depth, not on wins.** The win column reads 0% for all
   three modes and will keep reading 0% for every intermediate improvement,
   right up until it does not. Depth is the instrument that discriminates --
   Hard prestige reaches a 90th percentile of 33 against Swarm's 3.

3. **The prestige tail is patience-dependent and does not saturate.** Swept on
   Hard: patience 2 and 6 both top out at floor 17, patience 14 reaches 27,
   with the same 12% of runs ending on the rule. Depth scales with how many
   laps you allow, which is the FF7 argument working -- and it means any
   single patience setting authors the tail. Report it as a curve, or find a
   terminator that is a property of the game rather than of the harness.

4. **Over-farming is the one place time does not buy power.** Normal overkill
   spends 2.3x farm's turns for 26% against 35% -- the most expensive style in
   the game, and not the best one. §72 suspected this on twenty runs and it
   survives at thirty. The design claim is that time is the intended
   constraint; overkill is the counterexample, and either the diminishing
   return is intended (in which case say so) or something in the deep floors
   stops converting levels into survival.

5. **The fuzzer dies of being a bad player, not of being lost.** Navigation is
   solved; survival is not. It does not shop with intent, and its difficulty
   and world size are drawn on fuzzed screens -- a Swarm draw and a Normal
   draw are different games, and the log names which one now.

Design principles stated by the project owner, to hold to:
*every system should be useful in the early, middle and late game*; *limited
inventories are not fun*; *if a player wants to spend time to earn a power
fantasy, that is allowed* -- time is the intended constraint, not a cap;
**every mode must be finishable, Hardcore included** -- a difficulty that
cannot be completed is not a difficulty, it is a wall with a name on it.

---

## The shape of the descent *(proposed 2026-08-09 by the project owner)*

A hundred floors in five bands of twenty. Each band gets an environment that
helps the player in its own way. The first band's job is to *present the
world and make the player want to continue* -- not to filter them. And the
game should be willing to help invisibly.

**Built** (EVALUATION §99). The three decisions this section asked for were
made by the project owner and are written down in `src/bands.h`: the gifts are
**invisible**, the bands stay **15/20/25/25/15**, and the whole system hangs
off **one knob** (`BAND_MAX_HELP`) so it can be swept rather than argued about.
Set it to 0 for the control.

| band | the gift |
|---|---|
| Roots | 1-2 hit points a turn |
| Works | aether and ammunition return faster |
| Ruins | up to 12% of blows turned aside |
| Wastes | up to 40% chance a draught is not spent |
| Abyss | noticed 1-4 tiles later |

Unmeasured, deliberately: §99 says what to sweep and what the control is.

*The original note follows.* **Three-quarters of this already exists and is unused.**

- **The five bands are already there.** `get_biome_for_floor()` cuts at
  15 / 35 / 60 / 85: Sunken Jungle Roots, Company Works, Flooded Ruins, Salt
  Wastes, Abyss. Each already has its own relic set. What no band has is a
  *mechanical gift* -- something the environment does **for** you, as opposed
  to terrain that does things **to** you. That is the missing piece, and it is
  the piece that would make "every system useful in the early, middle and late
  game" true band by band rather than as an aspiration.
  (Bands are 15/20/25/25/15 rather than five twenties. Worth deciding whether
  the uneven split is intentional before building on it.)
- **The invisible bonus already has an instrument.** `dda.h` measures
  pressure per floor -- cruised / steady / hurt / routed, bounded -40..+40,
  "quick to forgive, slow to punish" -- and says in its own header that
  **nothing consumes it yet, deliberately**, so that phase two could be tuned
  against numbers rather than guesses. This proposal is phase two. The numbers
  it was waiting for now exist (§83, §85, §86).
- **What is measured says the first band is where the game is decided.** On
  Normal, 52 of 100 runs die in floors 1-9 -- and 46 of those 52 die on floors
  1 to 4. Every hard mode has a median death of floor 1 or 2. The first twenty
  floors are not currently an introduction; they are the filter.

### The blocker to know about before designing on top of DDA -- *fixed*

**Was:** `dda_floor_end()` was the only place pressure moved, and it is called
when a floor is **left**. A run that dies on floor 1 never leaves it, so DDA
read nothing and an invisible bonus riding on it could never fire -- for
precisely the runs that need it most, which per §86 is the median run in all
three hard modes.

**Now:** `dda_floor_so_far()` reads the floor underfoot from the signals that
already existed, and `dda_pressure()` is the settled history plus that
(EVALUATION §98). A character mauled on floor 1 reads -8, then -14, where it
used to read 0 all the way down.

It only ever reads *negative*: a floor going badly says so at once, a floor
going well waits to be finished. That asymmetry is deliberate -- it is the
module's quick-to-forgive rule applied to time -- and anything built on top
should keep it, or a strong opening on a fatal floor will read as coasting.

Still true, and still deliberate: **nothing consumes pressure yet.** The
numbers now exist for the runs this proposal is about, which is what was
missing.

### The tension worth deciding deliberately

An invisible bonus is a hidden safeguard, and this project has been explicit
that *the black market has no safeguards, deliberately*, and that spending
time to earn a power fantasy is allowed. Those are stances about systems the
player *chooses to use*. A first-band bonus is different in kind: it acts on
someone who has not chosen anything yet and cannot see it. `dda.h` already
argues the case -- "DDA that swings hard reads as the game cheating, and
players are right to resent it" -- so the shape it recommends is small,
forgiving, and asymmetric. Deciding *whether the player is ever told* is the
real design question, and it should be decided once and written down here.

### Hardcore has to be finishable

Stated as a principle above, and currently false: 0% at a median of 1, every
style, 100 runs (§83). §86 narrows why -- it is not resupply, since five times
the town stops moves no median at all -- and it carries a 10x experience
penalty on top of the floor-one wall, so it is the mode where the wall and the
levelling curve compound. If the five-band structure lands, Hardcore is the
mode that proves it worked.

*Previously updated 2026-08-02, after the render modes, the auto-explore
hunter, the curated spell pool and the difficulty retune landed.*
*Pick this up cold: each item says what, why, where, and how to verify.*

Phases 1 and 2 of the previous roadmap are **done** — see `EVALUATION.md`
§3–4 for what was fixed and how it was verified. What follows is what's
left, reordered now that the structural work is out of the way.

---

## Phase 0 — Winnable *(EVALUATION §58)*

**Normal is winnable from a fair start: 6% of runs reach floor 100**, median
depth 23, and 100% with the endgame gold §27 called for. Three constants did
it -- the quadratic attack term, the escalation cap, and the upgrade step --
with no meta-progression, no vault and no persistent bank shares.

Still open in this phase: Hard, Swarm and Hardcore remain at 0%, medians 1-2.
They die to floor-one density, which these three constants do not touch.

*Read this section as being about **The Shaft**, like everything written
before 2026-08-09: see §85. On the Deeps, Hardcore rush reaches floor 27 at
full health with no farming at all.*

## Phase 0 (historical) — Winnable, but not yet earnable

Floor 100 **is** now reachable (EVALUATION §25): with the HP/aether/ammo
upgrade tracks added alongside the gear ones, a party of five and 200M gold
finishes Normal 16% of the time, with the escalation curve untouched.

What is not yet true is that a run can *earn* that. Fair-start medians are
floor 14 (Normal), 1 (Hard), 1 (Swarm), 1 (Hardcore).

- **0.1 The economy, off by ~1,740x** (43,000x → 12,000x → 1,740x as scrap,
  then the scavenging trickle, landed). A complete 100-floor descent finds
  ~115,000 gold (floor scrap, the walking trickle and gold-rush floors all
  included); the upgrades needed to survive it cost ~200,000,000
  (EVALUATION §26, §27, §28). Supply has now been
  worked on twice -- gold rush floors and the Junkyard, the latter the only
  income in the game that scales multiplicatively with depth.

  **The Bank (EVALUATION §34) is the first genuinely multiplicative fix.** A
  share multiplies every coin earned; the price is cubic in shares held.
  Measured over 20 fair-start Normal runs it took income from 41,402 to
  438,135 and the best run from floor 61 to 88. The median did not move --
  a run that dies on floor 19 does not die of poverty -- so this raises the
  ceiling, not the floor.

  Still untouched: the **demand** side, never calibrated against what a run
  earns -- the upgrade step (+5 flat, needing 727 purchases for floor 100)
  and the price curve (quadratic per step, cubic cumulative). With the bank
  in, that recalibration can finally be done against a supply curve that has
  the same shape as the demand curve.
- **0.2 The escalation rate.** `level x 1.0% + entries x 0.5%`, capped 100%.
  Capping it at 25% instead moved the pre-HP-upgrade ceiling from 54 to 80 on
  its own. Largest single term in the wall; one constant.
- **0.3 The early game.** 30 of 30 Swarm and 17 of 30 Hard runs die on floor
  1 from a fair start. Separate problem, separate levers (starting kit,
  floor-1 density).
- **0.4 Floor-doubling in the dense modes** still hits the 2,000-monster cap
  on floor 1 of Swarm, which makes floors impassable rather than hard.

DDA still sits behind all of it: it adapts around a success rate, and the
fair-start success rate is zero in every mode.

---

## Phase 1 — Follow-through on the balance change

The damage model is fixed and regression-tested, but the change has knock-on
effects that only real play will surface. This phase is about confirming the
new curve feels right, not about further formula surgery.

### 1.1 Play the retuned difficulty -- *closed: all three are fine*

**Played by the project owner, 2026-08-15, and reported good.** The Ouroboros
Coil is reachable, `floor^2/140` is not too steep, and 6 HP/turn is the right
rate. No numbers to change; the three questions below are answered.

*The original note follows.*

**Effort:** a few real runs · **Risk:** none to code

The healing question is answered (EVALUATION §14): potions scale with max HP,
floor 1 is gentler, depth is genuinely dangerous, and the Ouroboros Coil at
5000g is the deliberate way out of attrition. All three were measured, none
were felt. What play needs to settle:

- **Is 5000g reachable?** The Coil is meant to be a late-run capstone, not an
  unattainable one. If nobody ever affords it, it isn't in the game; if it's
  bought by floor 30, it's too cheap. One number in `ACCESSORY_STOCK`.
- **Is `floor²/140` too steep?** At floor 99 an ordinary hit now costs 10% of
  the bar, and a player who skipped fights on the way down takes 3 hits to
  die. That's the intended pressure, but it's the harshest thing in the
  change and the easiest to over-correct. One term in `instantiate()`.
- **Is 6 HP/turn the right rate?** It should soften attrition, not delete it.
  If it trivialises the deep floors, lower the rate before raising the price.

**Verify:** `make test` still passes (the balance table will catch an
over-correction), plus how a run actually feels.

### 1.2 Re-check the boss fights at the new curve -- *closed: they land*

**Fought by the project owner, 2026-08-15.** The floor-15 and floor-35 bosses
play correctly at the new multiplicative curve -- neither trivial nor
overlong. The hand-modelling in the original note turned out to be right, and
`make_biome_boss()` / `make_boss()` need no adjustment.

*The original note follows.*

**Effort:** ~1 hr

Biome bosses and the Warden were tuned against the *old* subtractive formula.
Hand-modelling suggests they land well (floor-15 boss ≈ 9 hits to kill while
able to take ~17 from you; Warden ≈ 7 and 28), but that hasn't been played.

Reach floors 15 and 35 and fight them for real. If they now feel trivial or
overlong, they're pure data in `make_biome_boss()` / `make_boss()` and safe to
adjust — no formula changes needed.

---

## Phase 2 — Player-facing quality

*(2.1 in-run help screen: done — EVALUATION §20.)*

### 2.1b Decide whether auto-explore should use class abilities too -- *done*

**Decided: yes, on elites and bosses only** (EVALUATION §101), placed above the
spell and the shot because an ability returns on a cooldown and a boss does
not. Measured at 12 uses in 6,472 combat events. *Original note follows.*

It now hunts with melee, spells and ranged weapons (EVALUATION §14), but not
the class ability — that wasn't asked for, and an ability on a long cooldown
spent on a rat is worse than not spending it. If it should, `use_ability`
already has the same shape as the other two and slots into
`autoexplore_fight_step` just above the ranged branch. The judgement call is
when it's worth burning: probably elites and bosses only.

### 2.1b-2 Real accounts, if the URL trick stops being enough -- *a note, not a task*
Players identify themselves with `?arg=name` (EVALUATION §8), which is fine
for a household and no protection at all: anyone can type anyone's name. If
that ever matters, ttyd has basic auth (`-c user:pass`), or the
dependency-free server below can own identity properly.

### 2.1d Companions — *closed*
All three third-pass items are done (EVALUATION §9): real spellbooks and
weapons drawn from the player's own tables, class attributes applied on top
of the archetype tilt, and attacks you can see via `anim_volley`.

What a companion still is not, and this is now a design position rather than
a backlog item: they have no inventory you can open, no gear you can hand
them, and no orders. That was the brief — *"like if the game was multiplayer
and they were a second character doing their thing"* — and every remaining
difference from a Player follows from it.

### 2.1e Audit the rest of the screens at 80x24 -- *done, and now automatic*

`make shot` (tools/screenshot.c) draws any screen at any size into a real
curses buffer and reads the cells back; `tools/screenshot.sh 24 80 all` walks
every non-blocking screen and reports overflow and a lost border. **Twelve
screens, clean at 24x80 and 30x100.** Armory, ranged and arcanist sit on the
last usable row at 24x80 -- no headroom, so check before adding a line.

Only the `draw_*` screens are covered; the `screen_*` ones run their own key
loop and block. The original text follows.


Three layout faults turned up from one session's screenshots (EVALUATION
§21) and the fix was structural -- clipping at the point of drawing, and a
scrolling Page for long screens. What has *not* been walked through at the
minimum size: the shops, the quest board, the inventory, the class and
archetype pickers, and the two end screens. The tools now exist; it is a
matter of opening each one in an 80x24 terminal and looking.

### 2.1f Bigger floors
Measured at 1400x800 (EVALUATION §30). The engine handles it now that
`compute_fov` and the path searches no longer do map-sized work per turn --
those two fixes are in and are worth having at the current size anyway. What
does not handle it is mapgen: 60 rooms in a hundred times the rock gives 3%
fill and unplayable transit times.

So this is a content task, not a performance one. Two pieces are done:

**Room shapes** (EVALUATION §31) -- five kinds, plaza through cell, mixed per
10,000 tiles so the recipe scales with the map rather than thinning out.

**Wild districts and camps** (EVALUATION §32) -- jungle, sea, swamp and ruins
laid out before the rooms are, plus a walled camp on one floor in six where
nothing spawns. Also counted per 10,000 tiles.

Still flat constants: `MAX_ROOMS`, `MAX_MONSTERS`, `MAX_FLOOR_ITEMS`,
`MAX_FEATURES`.

~~Still missing: structure above the *district* level -- a floor divided into
quarters that connect through chokepoints rather than a uniform corridor
mesh.~~ **Built** (EVALUATION §111). `carve_quarters()` runs last, on the
Deeps and the Well only, and divides the floor with two walls down and one
across, joined at three chokepoints each. 35 of 36 dividing lines survive
their own verification; it costs about 10% of a big floor's generation.

It is the only pass in mapgen that *removes* connectivity on purpose, so it
walls a line, floods the floor, opens more gates where it cut something off,
and rolls the whole line back if the floor is worse for it. `tests/movement.c`
checks that independently, because a self-verifying pass that verifies itself
wrongly is the shape of a bug nobody finds.

*Still open here, and it is balance:* the **floor-doubling interval retuned for
a floor that takes longer to cross**. It is a constant measured against win
rates, so it waits with the rest of the balance work.

**Floor size is a per-run choice** (EVALUATION §37): the Shaft 140x80, the
Halls 350x200, the Deeps 700x400, the Well 1400x800, picked at new-game and
frozen for the run. `MAP_W`/`MAP_H` are runtime; the `_MAX` forms size the
arrays.

**1400x800 works** (EVALUATION §33, §35). Floor size is one knob,
`SCALE_BY_AREA`, and every cap goes through it. 1.1 million tiles, ~620,000
walkable, ~9,300 rooms and 5,800 monsters a floor. Crossing one takes on the
order of an hour, which is the intent rather than a defect.

What the change surfaced, and did not fix:
- Swarm/Hard/Hardcore floor 1 is now ~3,500 monsters rather than ~870. Phase
  0.3 was already about this; it is four times more urgent.
- 4 of 20 Normal runs hit the simulator's per-floor turn budget.
- ~~`sizeof(Map)` is 1.1 MB, and 736 KB of that is empty monster slots. Going
  further than 280x160 wants a compacted monster array first.~~ **Settled: not
  doing this, and the premise is stale.** Measured 2026-08-15: `sizeof(Map)` is
  now 62.4 MB, of which 52.6 MB *is* the monster array -- 600,000 slots at the
  Well -- so the note reads as though four fifths of the Map were waste.

  It is not waste. Peak monsters actually generated, swept across all four
  worlds and all four difficulties:

  | world | Normal | Hard | Swarm | Hardcore | cap |
  |---|---|---|---|---|---|
  | Shaft | 114 | 3,249 | 3,289 | 3,224 | 6,000 |
  | Halls | 515 | 16,490 | 16,943 | 17,580 | 37,500 |
  | Deeps | 1,635 | 6,485 | 57,228 | 58,932 | 150,000 |
  | Well  | 6,154 | 24,512 | **231,106** | -- | 600,000 |

  The hard modes sit at 38-55% of the cap at every size. The array is sized for
  a real workload, and it became one when §55 gave Swarm a depth-ramped
  multiplier and §56 raised the ceiling so that *the ramp, not the array*, sets
  the density. Cutting the cap re-imposes exactly the limit §55 removed.

  Compacting it -- allocating to fit -- would help **Normal only**, which uses
  1% of the cap, and it costs the save format: the run save is `Player` and
  `Map` written as raw structs, deliberately and since the beginning, and a
  pointer in `Map` turns that into a field-by-field serialiser for the sake of
  a mode that already fits. Not worth it. The memory is the price of the
  density ramp, and that is a fair price.

### 2.2 Stats / run-history screen -- *built* (EVALUATION §109)

`R` in the temple. This run, the lifetime best and its title, the deepest six
classes, and the fallen roster.

It was right that the data mostly existed. What it could not have anticipated
is that the most interesting half arrived later and for another reason: the
fallen roster was built for the barrow (§103), and a list of records is a
scoreboard where a list of the dead is a history.

Capped rather than paged in both lists -- a hundred classes will not fit, and a
player who has played eleven wants the deepest six rather than a paging
control. Audited at 24x80 like every other screen, and the audit gained a check
of its own while this went in: the play screen's key-hint row is composed from
a literal and clipped at draw time, so it can go missing silently, and now
cannot.

*(2.3 hazards on the minimap: done — EVALUATION §20.)*

### 2.4 Look at the two graphical modes and say what's wrong with them

**Looked at, 2026-08-15, and the answer is two separate faults** (EVALUATION
§114). Reported from play: tile mode is slow, *and* "sometimes it can go in a
direction for seconds even if I stopped typing" -- named, exactly, as
**ghosting**.

**The ghosting is fixed.** It was not the renderer: the play loop is
draw-then-read-then-act, one frame per key, which is correct until a frame
costs more than the terminal's key-repeat interval. Then the queue fills faster
than it drains and every buffered key is still spent, one slow frame at a time,
after the key is released. The loop now skips the frame it was about to draw
whenever a key is already waiting (`input_waiting()`, both backends). Every
turn still happens in order; only the drawing is dropped, and only while the
player is ahead of the game, so the last key of a burst always draws.

**And a layout bug behind it, from the screenshots** (EVALUATION §115). Tile
mode drew a small picture in one corner of a large terminal with several
hundred square characters of framed nothing under and beside it. The panel, the
log and the footer were all anchored to the *map* rather than to the *window* --
identical in text and block modes, where the viewport is the window minus its
reserves, and badly wrong in tile mode, where the per-frame pixel budget
shrinks the viewport and nothing else. They anchor to the window now, by
arithmetic that is a no-op at every size in the two modes that were already
right.

What that does **not** fix is the blank map area itself: a clamped viewport
still draws fewer tiles than the region can hold. That is the budget doing its
job, and the knob for it is `-`/`+` on the display screen.

**The city has its own palette** (EVALUATION §118). It was the one place in
the game drawing with no tint at all -- flat warm grey, buildings barely
separable from the plaza -- because `art_set_biome()` was handed -1 for floor 0
and -1 means "nobody said". Reported as *"the city on mode 2 is a bit meh"*.
`ART_TINT_CITY` is sunlit stone, the only tint that pulls two channels *up*,
and `make art` renders `art-scene-city.png` beside the five biomes so it can be
judged the same way they are.

**And the render mode can be changed from inside a run.** `D`, in the plaza or
on a floor. It was reachable only from the intro, which is the one moment a
player has nothing to compare against -- you cannot judge a render mode from a
title card, and switching used to mean abandoning the floor you wanted to look
at.

**The raw slowness is still open, and it is the half nothing here can
measure.** notcurses will not start under a pipe, so no instrument in this
repository can time a tile frame. What is needed is still what this entry
originally asked for: **the numbers the display screen reports** -- cell size
especially, since a retina display reports physical pixels and doubles
everything -- and whether `-`/`+` on that screen (the per-frame pixel budget)
changes how it feels. That is the one input a human at a real terminal has and
this project does not.

*The original note follows.*

**Effort:** one session · **Prereq:** none — they're implemented (EVALUATION §14)

Block graphics and Tileset both draw now. What hasn't happened is a human
looking at them in Ghostty during an actual descent, and that's the one
check nothing here can substitute for. Fastest route: `d` at the intro,
press `2` then `3`, and compare the previews before playing either.

Things most likely to want changing, in the order they'll annoy you:
- **Tile speed, still.** The first version was unusable; the fixes (keep the
  sprixel plane alive so notcurses can elide unchanged frames, cap the tiled
  area by a pixel budget) are in, and the budget is on `-`/`+` at the display
  screen with the resulting tile count shown. If it is still slow, note the
  numbers that screen reports — cell size especially, since a retina display
  reports physical pixels and doubles everything — because that is the input
  no test here can obtain.
- **Tile legibility at speed.** 36 sprites is enough coverage, not enough
  characterisation — the abyssal and boss sprites are deliberately similar
  (they share a colour in text mode too) and may need pulling apart.
- **Per-biome palettes: done** (EVALUATION §20). Terrain is tinted by biome;
  `make art` renders a scene per biome to review it. What is left here is a
  judgement only you can make: whether the five tints are *far enough* apart
  on a real screen, and whether the Abyss is too dark to play in. Both are
  one row each in `BIOME_TINT`.

Use `make art` while iterating: it renders the atlas and a sample room to
PNG through the same rasteriser, which is far faster than restarting a run.

### 2.5 Decide whether notcurses becomes the default -- *closed: it does not*

**Decided by the project owner, 2026-08-15: leave it exactly as it is.**
`make` builds ncurses (`bin/aether-descent`), `make notcurses` builds the
graphical backend (`bin/aether-descent-nc`) beside it, and neither moves.

The decision costs nothing and buys the thing that matters: **ncurses stays
the backend everything automated can drive.** The three test suites, the
simulator, `make shot`, the ASAN runs and the fuzzer all go through it, and
notcurses will not even start under a pipe or a detached tmux session
(Makefile:8-10). Making the graphical build the default would mean the
default build is the one no instrument on this project can run -- and every
number in EVALUATION comes from an instrument.

The two backends already share one render layer, so this is not a fork being
kept alive: all three modes go through one `resolve_cell` and one
`paint_cell`, and a fourth would too. `make notcurses && ./bin/aether-descent-nc`
is the whole cost of playing in tiles, and the `-nc` binary and `build-nc/`
object dir mean the two can never share stale objects.

Nothing to re-open unless notcurses grows the ability to run headless. If it
ever does, the switch is still one line.

### 2.5b A fourth suite: can anything get stuck? -- *built* (EVALUATION §103)

`tests/movement.c` asks the question no other suite asks -- whether there is
anywhere on a generated floor a body can end up and not get out of -- and it
found two real faults on its first run. It is the guard for anything that adds
terrain: a new district, a new conveying tile, a change to how districts are
placed.

What it pins: district ground is reachable (per kind, with the bars set where
the current numbers are rather than at a tenth, so they would fail against the
code as it stood before `district_relink()`); a delegated walk refuses every
tile that moves you, asserted over the tile *types* so the next one added
fails the suite rather than a play session; every ride on a belt or a current
ends; and a hold is finite and nothing under orders routes into a snare.

### 2.6 Extend the test suites as bugs are found -- *the three named ones are done*

All three landed (EVALUATION §101): spell-pool integrity, class-table
completeness, and name overflows in the generators. Nothing was found -- the
tables were already right -- and they are guarded now.

The gap they close is worth keeping in mind for the next addition: every other
test in this file drives *behaviour*, and nothing had ever asserted that the
*data* those systems read was well formed. A wrong field in a table is a
correct program doing the wrong thing, which is the defect class that cost this
project the most time.

~~Still open here, and cheap: `autoexplore_fight_step` and the rest of
auto-explore are static in main.c.~~ **Done** (EVALUATION §102). The decisions
are `src/autoexplore.c` and the loop stayed behind; nine assertions that were
impossible before, including both of the defects play found this session,
restated as properties.

*Original note follows.* The pattern is established and cheap: link against
real modules, assert on invariants.

---

## Phase 3 — Content and depth

*Seeded runs are done — see EVALUATION §19.*

Ordered by gameplay value per unit of work:

- ~~**Biome-specific mechanics.**~~ **Built** (EVALUATION §100): a rule per
  band in `bands.h` -- growth that hides, warded monsters, salt that bites, and
  the Abyss suppressing sight with Aether-Sense as the answer. The Works' rule
  turned out to have shipped years ago. *Original note follows.*
  Biomes now differ in colour too (§20), but
  still only in monster roster and hazard mix mechanically. Give each a rule: Abyss suppresses FOV (making
  Aether-Sense matter), Industrial gates routes behind more lever/machine
  puzzles, Ruins does something with the old wards. Cheapest way to make 100
  floors feel less uniform.
- **Play the new spell economy.** The pool is now 180 hand-written spells at
  ~2.5x price per level (EVALUATION §14), which is a guess at what the gold
  curve can bear, not a measurement. The thing to watch: whether a level-5
  spell at ~2600g is ever actually affordable before the run ends, and
  whether level 1-2 are now so cheap they're auto-buys. Both are single rows
  in `LEVEL_PRICE`.
- ~~**Effect variety inside a school.**~~ **Built** (EVALUATION §110). It was
  worse than this entry said: measured before the pass, Conduit, Warding,
  Aether-Sense and Scribing drew on three effects each and **Resonance drew on
  two**, so thirty spells differed by archetype and a number.

  Six new effects, one new *shape* each rather than one new number -- CHAIN
  (multi-target without being a radius), PARTY_HEAL (the first spell that acts
  on the party), HASTE (acts on time), REFLECT (punishes being hit rather than
  preventing it), SENSE_LIFE (buys information), MASS_SLOW (control over an
  area). Every school is at four now, and the suite holds that as a bar rather
  than a count.
- ~~**Set bonus procs.**~~ **Built** (EVALUATION §100). One per set, riding on
  the shared damage roll so a proc fires for a spell, a shot and a swing alike
  and for all six actors. `SET_PROC_PCT` is the one knob; zero is the control.
- ~~**More quest types.**~~ **Built** (EVALUATION §100): TIMED, NORECALL and
  ESCORT. The escort's client is a party slot and a bounty field -- no client
  struct, no client code path. The system moved to `src/quests.c` so it can
  actually be tested, and two client-leak bugs were caught the moment it
  could be.
- **New Game+.** Larger lift; only worth it once a full 100-floor run has
  actually been completed and tuned.

### 3.1 Micro-biomes — district candidates *(idea pool, no specs yet)*

The five biomes still differ mainly by monster roster and hazard mix. Wild
districts (EVALUATION §32) proved the cheaper lever: a *region* with its own
rule, placed before the rooms, that you stumble into and can walk back out
of. Four exist -- jungle, sea, swamp, ruins. This is the pool of candidates
for the next ones.

**Three rules that decide whether a candidate survives.** Every entry below
was triaged against them, and they are the first thing to re-apply to
anything added later:

1. **District, not floor.** At 1400x800 a floor-wide rule is unavoidable (the
   player never opts in) and a "hidden at the centre" reward is unfindable.
   Regions of 24-40 x 14-24 are the proven shape.
2. **Auto-explore has to survive it.** It reads the true map, cannot be
   deceived, and crosses the floor a step at a time. Any mechanic whose point
   is fooling the player is a non-event to it -- or forces the map view to
   lie, which destroys the only navigation tool at this size.
3. **Flat gold rewards are flavour, not economy.** A full descent earns
   ~438,000 (EVALUATION §34) against ~200M+ needed for floor 100. A 25,000g
   prize is 6% of a run's income. Anything meant to *matter* has to be
   multiplicative (a bank share, a permanent % gold find) or categorical (a
   guaranteed set piece).

#### Built — tranche one *(EVALUATION §39)*

**Mycelium, Hive-Mind and Cursed Mire are in.** They shipped together because
they are one mechanism -- districts the Map remembers the position of, with a
rule read during play -- and three consumers of it. The mycelium also brought
the wait key (`.`), without which its stillness rule is unplayable.

**Tranche two is in too** (EVALUATION §42): Crystal Caverns, the Quiet
Quarter and Storm-Cage -- where tranche one changed what notices you, these
change what your build is good at. Plus per-district colour (§43), without
which ten mechanically distinct places all looked like grey corridor.

**The shortlist is done** (EVALUATION §45): the Colosseum in 43's form (ten
waves, gates open after five, guaranteed set piece at ten) and the Toll --
Surrender Hall reworked into a feature, on big worlds only, where being
stranded is a real position.

Eleven district kinds now, and **multi-turn actions are built** (EVALUATION
§46) -- `g`, interruptible, paid only on completion, with rod-harvesting and
wreck-stripping as its first two consumers. That unblocks the largest group
in the pool: mining, searching, struggling out of quicksand, cracking a golem
core, breaking a sarcophagus, the deliberate sink in the quicksand basin.

**Displacement is built too** (EVALUATION §48) -- `displace_actor()`, with the
Aqueduct as its first consumer. Conveyors, floods, teleport orbs and sliding
ice now need no new machinery, only carve functions and a call.

Nothing in the pool is blocked on a system any more. **Blood Marsh, Chromatic
Abyss and the Assembly Line are built** (EVALUATION §49, §50) -- fifteen
district kinds now.

**The golem is built** (EVALUATION §52): three keys at the assembly console
and the line stands something up beside you until you leave the floor.

**It unlocked more than itself.** A companion can now exist without a roster
seat, so *found allies*, *rescued heroes* and **escort quests** -- which have
carried "blocked on a friendly-NPC actor that does not exist" since Phase 3
was written -- are now a name, a statline and `roster_idx = -1`. That blocker
is gone.

**The trap system is built too** (EVALUATION §51) -- one snare, two skins,
seventeen district kinds. Both follow-ups are done: `companion_grant_golem()` (EVALUATION §52) and the
**balance pass** (§53), which found the doubling interval had started firing
during ordinary exploration and moved it 1,000 -> 2,500. Normal's median went
7 -> 13 and the worst crowd 265 -> 110.

**Phase 0.3 has moved** (EVALUATION §55). The framing was wrong: the density
is the *appeal*, not the bug. Swarm's multiplier now ramps with depth (1x at
floor 1 to 40x at floor 60+) instead of sitting flat at 10-20x, so you grow
into the invasion and the deep end goes well past where the flat number
capped. Swarm's best run: floor 1 -> floor 10.

**The turn loop now only simulates what is near** (EVALUATION §56) -- 11 ms
to 0.0 ms -- which let `MAX_MONSTERS` go 2,000 -> 6,000 so the density ramp,
not the array, sets the ceiling. Lazy floor generation was considered and
deliberately not built; the reasoning is in §56.

**Pathfinding is A\* now** (EVALUATION §57) -- 13.2 ms to 1.6 ms per call at
the Well, the last recurring cost above a millisecond. Floor generation at the
Well (669 ms, once per floor) is all that remains, and is deliberately left.

**Tiered materials, ore veins and the lizard track are in** (EVALUATION §59,
§60). Normal is at 12% wins, median 28. The track is the first income that
carries no experience, which is the structural answer to the level/difficulty
cycle.

Still open: median depth is 2 on all three hard modes. The measured lesson is
that they do not need an exit (starting recall charms changed nothing) -- they
need something to *spend* on arrival. Note that the
simulator does not use chokepoints, snares or storm rods, so it is a
pessimistic proxy for horde survival -- tune against it carefully.

**DDA phase one is in** (EVALUATION §54): `src/dda.c` reads how a run is
actually going and the simulator reports the curve, with nothing acting on it
yet. Its first measurement raised Phase 0.3's priority -- the three hard modes
read flat zero because they die before completing a single floor, so **DDA
cannot help them**. Fix floor-one density first, then wire pressure into
escalation's level term.

What is left:
- ~~The attribute-reward rule~~ -- **decided and built** (EVALUATION §47).
  Districts pay in **writs of training**, redeemable only at the Pit School,
  never in raw attribute points. One source: finishing all ten arena waves.
  Any future candidate that wants to grant a point grants a writ instead.

#### The shortlist is now empty *(EVALUATION §103)*

**79 Stealth District is built, as the watch** -- the last entry on this
section's shortlist. Wardens who are awake but watching rather than hunting, a
two-tile notice radius, and one blow anywhere in the district breaking it for
good. The prize is a permanent +5% gold find on the body that took the box,
capped at 60%: rule 3 names a permanent % gold find as one of the two shapes a
reward may take, and unlike a bank share it grows linearly, which is what makes
it safe to pay per district.

**91 Gauntlet of the Fallen is built, as the barrow.** One wave per ghost, read
off `~/.aether_descent_fallen` -- a ring of sixteen summaries of runs this save
directory has actually lost. The reward is the gold that ghost was carrying and
nothing else; no experience either, because an XP source that scales with your
own past best is a loop. It is the only district whose existence depends on
state outside the map: no dead runs, no barrow.

**What is left in §3.1**, in the order the section's own triage puts it:

- ~~**99 Eye of the Storm**~~ **built** as *the eye* -- one quarter of the
  district is sheltered at a time and it rotates on the floor's own turn
  counter, so the shelter is a rhythm you can learn rather than a roll. It
  catches monsters as readily as it catches you.
- ~~**51 Mirror Dimension**~~ **built** as *the mirror* -- a copy of you at
  50% HP and 50% damage, worth no experience and carrying no purse.
- ~~**Petrified Forest**~~ **built** as *the stone wood*, the ambush rule
  alone: something you cannot see hits you twice. It reads the tile's own
  visible flag rather than remembering anything per monster, because a Well
  floor can hold 231,000 monsters and a byte on each is megabytes.
- ~~**Mine Shaft**~~ **built** as *the workings* -- galleries, ore, and holes
  in the floor that drop you a level for a percentage of your maximum health.
  The holes are visible on purpose: the candidate wanted a Listen action to
  reveal them, and a hidden hole that hurts you for finding it by accident is
  a trap, where the interesting version is the one you can see and step into
  anyway. Nothing under orders may route into one and no current may carry you
  down one -- it is the snare's rule, priced in floors instead of turns.
- ~~**Clockwork Boneyard**~~ **built** as *the boneyard*, salvage only, which
  is the half this file said to keep. Ten turns inside a dead golem, fully
  vulnerable, for the deepest material haul in the game. No new tile: rubble
  in a boneyard is a machine worth stripping exactly as rubble in an old
  quarter already is, so work stays keyed on the tile-and-district pair.
- ~~**80 Gladiator Gauntlet, merged with 76 and 78**~~ **built** as *the
  proving ground*: one restriction mechanic with three skins -- blades only,
  the arts only, no weapons -- rolled per district and announced on arrival,
  because a rule you discover by casting a spell into a monster's face is the
  mechanic failing rather than arriving. Every skin refuses the bow; all three
  are about closing with somebody. Six kills under the rule pays a writ of
  training, which is the arena's currency and the only kind §3.1 allows.

  Enforced at the point of use in all four places -- melee, cast, shoot, and
  the weapon's contribution to the damage roll -- and, critically, inside
  `autoexplore_decide_fight()`, so a delegated walk never picks an action the
  ground will refuse. A refusal spends no turn, so a walk that picked one would
  pick it again forever, which is the shape of every stuck-walk defect this
  project has had.

**§3.1's district pool is now empty.** Every candidate in the hundred that was
triaged as buildable has been built. What is left in Phase 3 is not terrain.
- **46 Boiler Room** (destructible terrain), **31 Inferno Forge** (immunity
  earned from terrain and spent crossing lava) and **5 Bioluminescent Abyss**
  (void non-walkable, blind patrols) are the three that are genuinely new
  mechanics rather than new scenery, and are correspondingly more expensive.

**And the section still needs splitting.** Town buildings (61 Barter Bazaar,
63 Auction House, 81 Oracle's Sanctuary, 83 Altar of Sacrifice) and global
systems (62 debt, 64 durability, 87, 89 pacts, 90 brands) are not micro-biomes
and get triaged against terrain while they live under this heading. The fourth
batch asked for this and it has not been done.

*The original shortlist follows, kept because each entry records why it was
picked.*

#### Shortlist — build these first

- **Mushroom Mycelium.** Bioluminescent fungal mats, giant caps for cover.
  Stalks block line of sight but *transmit* vibration: walking aggros at long
  range, standing still two turns makes the mycelium forget you. A "Spore
  Bloom" at the heart doubles gold on the floor and tells everything exactly
  where you are.
  *Why first:* it adds a verb the game does not have -- **standing still**.
  Every turn today is move-or-fight. It also splits sound from sight as two
  senses, which the thicket tile already sets up. Cheap: one aggro radius and
  a stillness counter, in code that already exists ([combat.c:241](../src/combat.c:241)).

- **Sunken Colosseum.** Enter the sand and the gates shut; survive five
  escalating waves to reopen them. A Champion's Chest holds a **guaranteed
  gear-set piece**.
  *Why second:* the only candidate that fixes something already on this list
  -- set pieces are random drops with no deterministic path, so set-hunting
  has no goal. "You cannot leave" is also a genuinely different fight from
  every other fight in the game. Gate-locking needs no new machinery;
  `TILE_SEALED_DOOR` already does it.

- **Storm-Cage.** Open plateau under permanent lightning, metal rods driven
  into the ground. Every few turns a bolt strikes a rod; standing within two
  tiles hurts, and a monster beside a struck rod dies outright. Conductive
  ore can be mined, three exposed turns per deposit.
  *Why third:* a hazard you can **aim**. Luring something next to a rod would
  be the first environmental kill in the game, and a timed area effect is
  cheap.

#### Worth building, with a change

- **Bioluminescent Abyss.** Sinkholes into lit caverns, root-and-bone bridges
  one or two tiles wide, blind cave-beasts that hunt by sound.
  *Change:* void must be **non-walkable, not instant death**. Instant-death
  tiles plus auto-explore across 620,000 tiles is a run ended by a pathing
  edge case. The blind patrols are the good part and share the Mycelium's
  sound machinery.

- **Memory Vault.** Ethereal shelves; each book read gives lore plus a random
  buff or debuff for the floor. No enemies -- the danger is curiosity. One
  hidden Tome grants a permanent +1 attribute.
  *Change:* cap concurrent effects (~5) or escalate risk per read, otherwise
  the optimal play is to eat forty debuffs while hunting the Tome.

- **Clockwork Boneyard.** Collapsed steampunk golems half-buried, steam
  geysers venting.
  *Change:* keep the **salvage** (ten turns cracking a core, fully vulnerable
  throughout) and drop the pressure-plate labyrinth -- the lever-and-sealed-
  door puzzle already exists and this would be a reskin of it.

#### What the second batch revealed

Twenty more candidates arrived and most of them are not gated on being good
ideas -- they are gated on **three systems that do not exist**. Naming them
changes the order of everything below, because building one unlocks five
candidates at once rather than one.

- **Multi-turn actions.** Harvest 5 turns, salvage 10, search 5, struggle 3,
  mine 3, push 3, brake 1, break a sarcophagus 5, listen 2. Nothing in the
  game does this today except `recall_channel_left` -- a single hard-coded
  counter, which is the precedent but not the system. A general
  "commit N turns, interruptible, vulnerable throughout" is the single
  highest-leverage thing on this page: it is the shared verb behind at least
  eight candidates, and *being unable to act while something approaches* is a
  tension the game currently cannot express at all.
- **Involuntary displacement.** Conveyor belts, flood surges, teleport orbs,
  shifting islands, sliding ice. Every one needs the movement code to accept
  "you moved without asking", which today it cannot: there is no knockback,
  no push, no slide anywhere. One system, five candidates -- but it touches
  player, monsters, companions, auto-explore and pathing, so it stays
  expensive no matter how many ideas ride on it.
- **Sound as a second sense.** Already shortlisted via the Mycelium. The
  Silent Chapel, the Hive-Mind and the Abyss's blind beasts are the same
  machinery wearing different clothes, and should be built as one thing.

**A warning about the rewards.** Six of these twenty hand out a *permanent
attribute point* (VISION, RESOLVE, WARDING, FORTITUDE, CONDUIT/MIGHT, and one
that grants +1 to all thirty). Attributes are fixed at creation and move only
at the Gladiator School, for coin, on a `200v^2 + 300` curve -- that is the
only sink of its kind in the game. Handing out six free points would empty
the School of its purpose and flatten the curve it is priced on. If a
district is going to touch attributes at all, it should pay in **School
credit or a discount**, not in raw points, and at most one or two should do
it. This is the attribute equivalent of rule 3.

#### Shortlist additions

- **Cursed Mire.** Mist caps line of sight at ~3 tiles; local monsters carry
  a lingering blight on hit.
  *Why:* `fov_radius` is a plain field on `Player` -- this is nearly free, and
  it is the same idea Phase 3 already wanted for the Abyss ("suppress FOV so
  Aether-Sense matters"), but scoped to a region you can leave. Fog that
  makes you walk into things is a different fear from a thicket that hides
  them.

- **Crystal Caverns.** Aether-charged crystals reflect spells: cast near one
  and it ricochets to a random target, yourself included. Melee is safe,
  ranged is a gamble.
  *Why:* the first place in the game where **your build is the wrong build**.
  Everything else scales all classes equally; this punishes exactly one axis
  and rewards the melee character who has been the poor relation since the
  spell pool landed. Spells already resolve against a target -- a redirect is
  cheap.

- **Hive-Mind Nest.** Damaging one monster tells every monster within ~20
  tiles, and chips them all. Stealth is impossible by construction.
  *Why:* the deliberate opposite of the Mycelium, sharing its machinery. One
  district where holding still saves you, one where nothing does.

#### Worth building, with a change

- **Mine Shaft.** Hidden vertical shafts drop you to the *next floor down*
  with damage; carts and ore veins to work.
  *Change:* keep the shafts, drop the "Listen action to reveal them" until
  multi-turn actions exist. A shortcut that costs HP and skips the rest of
  the floor is genuinely new -- and floor transitions already work, so the
  expensive-sounding part is the cheap part.

- **Petrified Forest.** Permanent sight-blocking stone trees, blind corners,
  ambushing enemies.
  *Change:* it is a thicket reskin plus the ambush rule; build the **ambush**
  (a free attack on whatever rounds a corner into you) and skip the rest.
  Drop the +1 VISION -- see the warning above.

- **Carnivorous Garden / Quicksand Basin.** Both are hidden trap tiles that
  hold you in place; both want the same tile-with-a-trigger and the existing
  stun. Build them as **one trap system with two skins**, not two districts.
  Quicksand's *deliberate* sink -- ten turns down and thirty damage for a
  chest -- is the better hook of the two and wants multi-turn actions first.

- **Soul Mirror.** *Built* as `DIST_MIRROR`, at 51's reduced numbers.

- **Geothermal Vents** is the **Storm-Cage's system with a different skin** --
  telegraphed, timed, area damage, harvestable. Build the system once, skin it
  twice. *(Scrap Heap and Blast Furnace were here too; both are deleted, see
  the decisions below.)*

#### Third batch — the pool has saturated

Sixty candidates now, and they resolve to roughly **twelve distinct
mechanics**. That is not a criticism of the ideas; it is the signal that the
pool is full enough to build from. What follows is consolidation, so the same
mechanic stops being re-argued under five names.

**Duplicates, mapped to what they duplicate.** Each of these is the same
system with different scenery, and should be *skins*, not builds:

| Candidates | One mechanic |
|---|---|
| 1 Mycelium · 30 Silent Chapel · 36 Spore Forest · 54 Echoing Halls · 59 Sonic Mines | sound-based aggro |
| 3 Glass-Frost · 32 Glacial Crevasse | sliding ice |
| 10 Storm-Cage · 14 Geothermal · 33 Tempest Spire · 49 Piston Press | telegraphed timed area damage |
| 18 Assembly Line · 47 Conveyor Crossroads · 42 Aqueduct | conveyed movement |
| 8 Memory Vault · 44 Archive of Whispers | read-for-random-effect |
| 25 Soul Mirror · 51 Mirror Dimension | a clone that fights you |
| 19 Blast Furnace · 31 Inferno Forge | lava with a safe route |
| 5 Abyss · 52 Void Tear | void you can fall into |
| 24 Dreamscape · 60 Pandemonium | geometry that rearranges |
| 29 Plague Pits · 48 Foundry · 56 Radiation Zone | floor-wide HP drain |
| 7 Colosseum · 43 Colosseum Redux | committed wave fight |
| 28 Lich's Tower · 6 Cathedral | stacked elevation |

**Two of the new ones are straight upgrades to entries already shortlisted,
and replace them:**

- **43 supersedes 7 (Colosseum).** "Leave after wave 5, or push to wave 10
  for the real prize, and bribe the crowd to skip one" is a better design than
  a flat five waves: it is a *decision* every wave rather than a wall. Adopt
  43's structure, keep 7's guaranteed set piece as the wave-10 reward.
- **51 supersedes 25 (Soul Mirror).** A clone at **50% HP and 50% damage** is
  exactly the change I asked for -- a mirror match at parity is a coin-flip
  that ignores every build decision the player made. Adopt 51's numbers.

#### What is genuinely new in the third batch

Six mechanics here do not exist anywhere in the pool. These are the reason
this batch was worth reading:

- **Temporary immunity earned from terrain** (31, cooling vents: stand two
  turns, cross lava for five). A state you *earn from the map* and spend --
  neither a buff nor an item. It turns a hazard field into a route-planning
  problem, and it is the cleanest use of multi-turn actions in the pool.
- **Destructible terrain** (46, boilers that explode and open new paths). The
  map is immutable today except for a handful of terrain spells. Blowing a
  hole in a wall to make a shortcut is a real verb and the mapgen already
  proves the tiles can change under play.
- **Terrain that helps the enemy** (39, marsh water regenerating monsters).
  Every hazard in the game currently hurts both sides symmetrically. Ground
  that is *good for them and neutral for you* is a new pressure, and it is
  nearly free.
- **Stance tiles** (55, coloured tiles granting +damage/-defence and so on).
  A positioning game rather than a hazard: fight *here* and you hit harder but
  fold faster. Cheap, and it is the only candidate that rewards standing
  somewhere specific rather than avoiding it.
- **Equipment as a vulnerability** (34 acid destroying a weapon; 35 magnetism
  catching metal gear). The first mechanic where *what you are wearing*
  matters beyond its stat line. Handle with care -- destroying a set piece a
  player spent a run assembling is not tension, it is a rage-quit. Corrode a
  `+N` upgrade level rather than the item, or gate it on unequipping.
- **Currents as transport** (42, aqueduct flow you ride deliberately).
  Displacement used as a *benefit*. If displacement gets built, this is the
  candidate that makes it feel like a gift instead of a tax.

Two more are new but should be refused on their own terms:

- **53 Dream Forge** -- unlimited random gear for 10,000g a roll. The game
  needs gold *sinks*, so the instinct is right, but an unbounded slot machine
  for equipment breaks the gear economy and the set system in one move. A
  bounded version (n rolls per visit, or crafting *into* a set) is worth
  keeping; this version is not.
- **58 Time Rift** -- the floor rewinds five turns, so monsters can be farmed
  repeatedly. That is an XP and gold exploit written down as a feature, on
  top of needing per-floor state snapshots. The *visual* is excellent and
  costs nothing; the mechanic should not be built.

#### Two numbers that now need a decision

**Attributes: 24 of the 60 candidates hand out a permanent point.** Batches
two and three together would grant something like twenty-four free attribute
points across a run. The Gladiator School charges `200v^2 + 300` per point --
at value 10 that is **20,300 gold for one**, and it is the only sink of its
kind in the game. Twenty-four free points does not devalue the School, it
deletes it.

This needs a rule, not case-by-case judgement. The suggestion from batch two
stands and hardens: **districts pay in School credit or discount, never in raw
points**, and no more than one or two in the entire set grant a point
outright. A "+1 to all physical attributes" reward (53) should not exist at
any price.

**Gold: the rewards inflated an order of magnitude between batches.** Batch
one ran 2,000-25,000. This batch runs 20,000-100,000. Measured against a full
descent earning **438,135** with the Bank (EVALUATION §34), a single 100,000g
pickup is roughly a quarter of an entire run's income from one object. Rule 3
said flat gold is flavour; at these numbers it stops being flavour and becomes
a balance change made by accident. Either bring them back to the 5,000-25,000
band, or accept them and re-measure the economy afterwards -- but not both
silently.

#### Where the third batch lands

- **Shortlist gains nothing.** Mycelium, Colosseum (in 43's form),
  Storm-Cage, Cursed Mire, Crystal Caverns and Hive-Mind still lead, and
  three of the twelve mechanics above are already among them.
- **Worth building, with a change** gains **39 Blood Marsh** (enemy-healing
  terrain, nearly free), **55 Chromatic Abyss** (stance tiles, cheap and
  novel), and **46 Boiler Room** (destructible terrain -- more expensive, but
  the only candidate that changes the map under the player's own hand).
- **Parked** gains **31 Inferno Forge** and **42 Aqueduct**, both blocked on
  systems already parked (multi-turn actions, displacement), and both the best
  argument for building those systems.
  *Superseded:* both systems were built, and 42 Aqueduct is now `DIST_AQUEDUCT`
  and shipped. See the blocker review.
- **Settled against** gains four more, all under rules already written down at
  the end of this file: two floor-wide HP drains, another undo button, and a
  district made of districts. See *Settled — decided against*; they are not
  listed by name because the rule is the useful part and the names are not.

#### Fourth batch — mostly not districts

A hundred candidates now, and this batch is a different animal from the first
three. Those were terrain: a region with a rule, placed before the rooms.
About two thirds of this batch are **game systems** -- an economy layer, a
party layer, a contract layer, an endgame -- wearing district clothing.

That distinction matters more than any individual triage, because a system
dressed as a district gets built as a district and then leaks into the whole
game. So the first job is to sort them by *what they actually are*.

**Already in the game.** Four of these describe things that exist:

- **95 Infinite Stairs** (descend several floors at once). The Temple already
  does this -- `prompt_floor_choice(p->deepest_floor)` lets you drop straight
  to any depth you have reached ([main.c:847](../src/main.c:847)).
- **67 Mercenary Camp** and **70 Slave Market** are both the Tavern: hire up
  to five companions, priced on a curve, released at will, fighting on their
  own. 67's *floor-only* hire and gold-cut are a genuine variation and belong
  as a Tavern option rather than a district.
- **65 Guild Vault** (deposit gold, earn interest) is the Bank with a
  different verb, and a worse one -- the Bank multiplies income you have not
  earned yet, which compounds harder than interest on a balance.
- **69 Hollowed Church** (pay for a random blessing, small chance of a curse)
  is `FEATURE_SHRINE`, which already exists and already rolls luck.

**These are new town buildings, not districts** (61, 63, 81, 83) **and these
are global systems** (62, 64, 87, 89, 90). Both groups have **moved out of this
section** -- see §3.2 and §3.3 below. They were triaged here against rules
written for terrain, which is the mistake this batch was about in the first
place, and leaving them under a micro-biome heading kept making it.

#### The genuinely new district candidates

Five, and only five, are districts:

- **79 Stealth District.** You *cannot fight* -- attack once and the whole
  district aggros. Reach the exit unseen.
  *Why it matters:* the only candidate in a hundred that removes combat
  entirely. Everything else in this game is a fight with different scenery.
  It also runs on the sound machinery already shortlisted.
- **80 Gladiator Gauntlet.** Your weapon is locked away; every ten turns you
  are handed a random one off the floor.
  *Why:* the only candidate that attacks *build identity*. A hundred classes
  and eighteen ranged weapons all funnel into one loadout you never change;
  this makes you play someone else for a while.
- **76 Duelling Grounds / 78 Arcane Duel.** Restricted-loadout fights -- melee
  only, or spells only. Same idea as 80 from the other direction, and the two
  should share one "restriction" mechanic rather than being built twice.
- **86 Surrender Hall.** Hand over everything you carry and walk to the next
  floor, or fight. A real decision with a real price, and it needs no new
  machinery at all.
- **99 Eye of the Storm.** A rotating safe quadrant you move in sync with.
  It is the timed-area-damage system (Storm-Cage) with the polarity flipped:
  instead of dodging a hazard, you *ride* a moving safe zone.

#### The best idea in the hundred

**91 Gauntlet of the Fallen -- fight the ghosts of your own failed runs.**

The game already keeps highscores and per-class records across runs
([save.c:28](../src/save.c:28)), and companions already prove that an actor
with stats and gear can fight on its own. A ghost carrying the build and the
gold of a character who actually died on floor 47 is a fight no procedural
generator can produce, and it makes the meta-progression *visible* instead of
a number on a title screen.

It is also the only candidate that turns failure into content. Everything
else on this page rewards success.

The reward as written (+1 to all attributes) should be the gold the ghost was
carrying and nothing else -- see below.

#### Attributes: this now needs deciding before anything is built

Across all four batches, **more than a third of the hundred candidates hand
out permanent attribute points**, and this batch escalates from +1 to a named
attribute to *+1 to all thirty* (84, 91, 93, 96, 100) and, in one case,
**+5 to all thirty per hundred tiles walked** (94) -- which on a floor that is
1,400 tiles across is several hundred points.

The Gladiator School charges `200v^2 + 300` for one point in one attribute.
The pool as written would hand out, free, more than a character could buy in
a hundred runs. This is no longer a balance concern; it is the difference
between the School existing and not.

**The rule, stated once so it stops needing restating:** districts and
buildings may sell attribute *reallocation* (83) or School *credit and
discount*, never raw points. If a single reward in the entire set grants a
point outright it should be the hardest thing in the game. "+1 to all
attributes" should not appear at any price, anywhere.

#### Where the fourth batch lands

- **Shortlist** gains **79 Stealth District** and **86 Surrender Hall** --
  both cheap, both adding something no other candidate does.
- **Worth building, with a change** gains **80 Gladiator Gauntlet** (merge
  with 76 and 78 into one loadout-restriction mechanic) and **99 Eye of the
  Storm** (reskin of the Storm-Cage system, polarity flipped).
- ~~**A new heading is needed above this one:** *town buildings* and *global
  systems* are not micro-biomes and should not live under §3.1.~~ **Done** --
  they are §3.2 and §3.3, each with triage rules of its own.
- **100 Ascension is not a district either -- it is the missing endgame.**
  The game currently ends with `STATE_WIN` when the Warden dies and nothing
  after it. "Take the portal and win, or stay and become the Warden against an
  endless horde" is a real answer to *what is floor 100 for*, and it belongs
  in Phase 3's New Game+ entry rather than here.

*Specs come later, deliberately. This is a pool to add to and pick from.*

---

### The pool is closed *(decided 2026-08-16)*

The idea pool ran to a hundred candidates over four batches. It is now closed:
every entry is built, or deleted, or one of the three things listed at the
bottom of this page. Nothing above this line is a live candidate, and the
batch analyses are kept only as reasoning -- the duplicate table in particular,
because it is what stopped the same mechanic being re-argued under five names.

**The blockers, checked against the code rather than the prose.** Every
"blocked" and "parked" note in the pool was written when the systems did not
exist, and none was revisited when they arrived.

| Named blocker | Status |
|---|---|
| Multi-turn actions | **Built.** `WORK_*` -- rod, salvage, mine, golem core. |
| Involuntary displacement | **Built.** `displace_actor()`, used by the Aqueduct and the Assembly Line. |
| A friendly NPC that is not a hire | **Built.** The golem, `roster_idx = -1`. |
| Sound as a second sense | **Built 2026-08-16** (EVALUATION §120), which unblocked the Silent Chapel. |
| Movement cost | **Deleted.** |
| Destructible terrain | **Deleted.** |
| Stacked elevation | **Deleted.** |

Four of seven were already built, and most of the parked pool with them: 42
Aqueduct is `DIST_AQUEDUCT`, 18 Assembly Line is `DIST_ASSEMBLY`, 51 Soul
Mirror is `DIST_MIRROR` at the reduced numbers, and Garden and Quicksand went
in as the one trap system with two skins. Twenty-five district kinds exist.

**What is left to build, in order.** This is the whole list.

1. ~~**Silent Chapel**~~ -- **built** as `DIST_CHAPEL` (EVALUATION §121). It
   turned out to be a better idea than when it was written: with hearing now a
   real sense, the chapel *takes* it, and takes it from the monsters standing
   there too. Its pipe-organ note sequence stays parked -- a minigame in a game
   with no minigames, and that has not changed.
2. ~~**Geothermal Vents**~~ -- **built** as `DIST_GEOTHERMAL`, sharing the
   Storm-Cage's tick rather than copying it.
3. ~~**81 Oracle's Sanctuary**~~ -- **built** (EVALUATION §123). Three readings
   at one mechanism: mark the way down, map the next floor, map the next three.
4. ~~**83 Altar of Sacrifice**~~ -- **built**. A point for a point, plus half
   the School's fee on the School's own curve, and nothing goes below 1.
5. ~~**61 Barter Bazaar**~~ -- **built**. The Apothecary's bottles at 55-150%,
   re-rolled per descent rather than per visit.

**Everything on this page is now built. What is left is balance**, and then the
project closes.

**87 Choose-your-difficulty is folded into the balance pass** rather than built
as a feature. It is half done already and the roadmap was wrong about which
half: `dda.c` measures pressure and `bands.c` consumes it, but only the
*negative* side -- a struggling run gets help, and a run that is coasting gets
nothing. Making positive pressure do something is one knob with zero as its
control, which is rule 1 of §3.3 and is a thing you tune against the ladder,
not a thing you build before measuring it.

### 3.2 Town buildings — a door in the plaza

Split out of §3.1, where they were being judged against rules written for
terrain. A district is a region with a rule that you stumble into; a building
is a door, a screen and a price curve that you choose to walk through. Almost
nothing that decides one decides the other.

**Four rules, and they are not §3.1's three.**

1. **Judge it against the ten buildings already there**, not against a
   hillside. The question is whether the plaza needs another door, and what
   the player is giving up by spending the walk on this one.
2. **A duplicate is an option, not a door.** 67 Mercenary Camp's floor-only
   hire belongs as a line on the Tavern screen; 65 Guild Vault is the Bank
   with a worse verb, since a share multiplies income not yet earned and
   compounds harder than interest on a balance; 69 Hollowed Church is
   `FEATURE_SHRINE`. None of the three is a building.
3. **The screen has to fit 24x80.** `tools/screenshot.sh` walks every one of
   them, the armory and the arcanist already sit on the last usable row, and a
   building that cannot be read on the owner's terminal is not shipped.
4. **The attribute rule binds here too**: reallocation or School credit, never
   raw points. It is stated four times in §3.1 for districts and it is the
   *building* rules where it actually bites, because a building can be visited
   every run.

**The candidates, in the order I would build them:**

- **81 Oracle's Sanctuary** -- pay gold for a true answer about the run.
  **Information as a purchasable good does not exist anywhere in this game**,
  and on a 1400x800 floor "which way is the exit" is worth real money. It is
  also the only entry here that gets *better* as the world gets bigger, which
  makes it the natural partner to the world-size work. Strongest of the four.
- **83 Altar of Sacrifice (redux)** -- trade a point in one attribute for a
  point in another. The right answer to the attribute problem §3.1 keeps
  circling: a reallocation *sink* rather than a fountain. Belongs next to the
  Gladiator School and priced against the same `200v^2 + 300` curve.
- **61 Barter Bazaar** -- fluctuating prices you time your purchases against.
  The only candidate in the hundred that makes *when* you shop matter.
- **63 Auction House** -- bidding against NPCs with your gold locked while the
  auction runs. The locked gold is the good part and the whole idea; without
  it this is a shop with extra keystrokes.

**One thing to decide first, and it is not a building.** Three of these four
are priced in gold, and the economy's demand side has never been calibrated
against what a run actually earns (Phase 0.1). Pricing a new sink before that
is done means pricing it twice.

### 3.3 Global systems — things that change every floor

Also split out of §3.1. None of these can be scoped to a region, prototyped in
a corner, or judged by whether auto-explore survives them. They change the
whole game, which makes them the most expensive items on this page and the
ones most tangled with the balance work that is deliberately being left until
last.

**Three rules.**

1. **It needs one knob, and zero has to be the control.** `BAND_MAX_HELP` and
   `SET_PROC_PCT` are the pattern: a system that cannot be turned off cannot be
   measured against the ladder, and a system that cannot be measured against
   the ladder cannot ship in a game whose difficulty is its open question.
2. **It is balance work, so it lands with the balance work.** Every one of
   these moves win rates on every mode. Building them before the ladder is
   re-measured means measuring twice and believing neither.
3. **It must not be a second name for something already here.** 87
   choose-your-difficulty *is* the DDA work under a new name -- `dda.c` reads
   pressure per floor already, and §98 taught it to read the floor underfoot.
   Do not build it twice.

**The candidates:**

- **87 Choose-your-difficulty** -- already half built. `dda.h` measures
  pressure, nothing consumes it yet, and that was deliberate so it could be
  tuned against numbers. It is the closest of these to being real.
- **89 Pacts** and **90 Brands** -- a permanent buff bought with a permanent
  debuff. One mechanic, two names; build it once. It is the black market's
  logic applied to the character rather than to the purse, which fits a game
  that has already decided the black market has no safeguards on purpose.
- **62 Debt and collectors** -- an ongoing drain with something hunting you
  for it. The only candidate that makes gold a *pressure* rather than a score,
  and the one that would give the Bank a counterweight.
- **64 Item durability** -- handle with the care §3.1 already demanded of
  equipment damage: corrode a `+N` upgrade level, never destroy a set piece a
  player spent a run assembling. Weakest of the four, and the easiest to make
  miserable.

---

## Settled — decided against, and not to be re-proposed

Everything here is a *decision*, not a blocker. Nothing on this list is waiting
for a system, a measurement or a free afternoon, and none of it should come
back as a new candidate under a new name.

It exists because the alternative was worse. Four separate "Rejected, with
reasons" catalogues used to sit inside §3.1, interleaved with live candidates,
listing perhaps twenty dead ideas at paragraph length. They made the pool
look larger than it was, they were re-read every session, and twice the same
idea was re-proposed and re-rejected under a different number anyway. The
catalogues are gone. What is kept is the **rule** each rejection established,
because a rule stops the next twenty; a catalogue only records the last twenty.

**No z-axis.** `Map` is `Tile[MAP_H][MAP_W]` and adding elevation touches FOV,
both path searches, monster AI, companions, rendering, the minimap and the save
format. *Four* candidates asked for it (Cathedral of Gears, Lich's Tower, Tower
of Ascension, Magnetic Ascender). The answer is no. If it is ever wanted it is
its own project, not a district with stairs in it.

> Worth recording that this rule was already written when the pool was still
> carrying the Cathedral and the Lich's Tower as *blocked on stacked
> elevation*. The page contradicted itself for months: one section had decided
> the question and another was still waiting for the answer. That is the whole
> argument for deleting settled things rather than filing them.

**No movement cost.** Every step is one turn, for the player, monsters and
companions alike, and it stays that way. Two candidates asked (Tar Pits, Scrap
Heap) and the system would have to be understood by every mover in the game --
player, monsters, companions, auto-explore, displacement. What it buys is
"walking is slower" on floors that already take an hour to cross. The cost is
the highest on the page and the payoff the least certain; that combination is
not going to improve by being thought about again.

**No destructible terrain.** Blowing a hole in a wall to make a shortcut is a
real verb and this was the closest call of the three. It dies on arithmetic
rather than on principle: one consumer (46 Boiler Room) for a change that makes
the map mutable under play, which every path search, the minimap, auto-explore
and the save format all currently assume it is not. One district is not worth
that assumption.

**No disorientation as a goal.** Astral Observatory, Dreamscape and Pandemonium
all wanted geometry that rearranges behind you, and all three had their system
once displacement was built -- so this is a decision and not a blocker. On a
1400x800 floor, auto-explore and the `M` map are the player's only orientation,
and a district that deliberately breaks both produces a bug report rather than
a mood. Rule 2 already said this; these three are the last candidates it kills.

**No permanent debuffs, no debt, no durability.** The three §3.3 candidates
that were not 87. Pacts/Brands trade a permanent buff for a permanent debuff,
62 Debt drains gold with collectors hunting you, and 64 Item durability corrodes
what you are wearing. Each is defensible on its own and all three share one
problem: they move win rates on every mode, and they would land on top of a
ladder that is about to be measured for the last time. A system built after the
final balance pass is a system nobody measures. Deleted rather than deferred,
because "after balance" means never on this project.

**No auction house.** 63, cut against the three town buildings that survived.
The locked gold while bidding runs is genuinely the good part, and without it
this is a shop with extra keystrokes -- but it needs a whole screen at 24x80 to
deliver one idea, and the Bazaar delivers a comparable one for a line.

**No undo.** Rewinding turns needs per-turn snapshots of a Map measured in tens
of megabytes, and the game's tension is that decisions stick. Two candidates
asked (Time Rift, Memory Stone). The *visuals* were good and cost nothing;
the mechanic is not to be built.

**No floor-wide HP drain.** A floor takes tens of thousands of steps to cross,
so a per-turn tax is arithmetic rather than difficulty. Three candidates asked
(Plague Pits, Foundry, Radiation Zone). Bounded to a district the same idea is
fine, and that is where it goes.

**Nothing that makes the map lie.** The `M` view and auto-explore are the only
orientation a player has on a million-tile floor. A mechanic whose point is
deceiving the player is either a non-event to auto-explore or a broken
navigation tool. *The settings are worth stealing* -- an abandoned market, rows
of stalls -- with a different hook.

**No fourth wall, and no free-text input.** Asking the player about the damage
formula or their lifetime gold is a register the game never uses, and the
terminal UI has no text entry.

**No duplicate wearing a new name.** If `FEATURE_ALTAR`, `FEATURE_SHRINE`, the
Tavern or the Bank already does it, the answer is an option on that thing, not
a new one beside it. This came up five times across the four batches and it is
the single most common way a candidate arrives.

**No system rewarding the player for spending people.** Buying followers to
sacrifice them for profit makes the optimal play a thing the game should not be
optimising for. The upkeep and the grim tone are keepable; press-ganged crew,
indentured debtors or salvaged automatons carry both without it.

**No dependency-free browser server.** Declined by the project owner on the
strongest possible grounds: the ttyd path has been in daily use since it
shipped and works well, and a second web backend would replace something that
works with something that might. The cost of leaving it is
`brew install ttyd` on whatever machine hosts the game.

- **Rewriting the render layer.** It has now absorbed a backend split *and*
  two graphical render modes without the screens changing, which is the
  strongest evidence yet that it doesn't need rewriting. All three modes go
  through one `resolve_cell` pass and one `paint_cell`; a fourth would too.
- **Further damage-formula changes without the harness.** The current curve
  is measured and guarded. If it needs adjusting, change the numbers and let
  `make test` tell you what happened — don't re-derive the model by feel.
- **Putting a step cap back on auto-explore.** It was removed deliberately
  (EVALUATION §17) and replaced with a no-progress detector, which is
  strictly better: a cap interrupts work that is going fine, a stuck-check
  only fires when nothing is happening. If it ever runs away, the bug is in
  what counts as progress, not in the absence of a ceiling.
- **Removing the manual playtest loop.** The automated suites catch formula
  and data regressions; they cannot judge whether a fight is *fun*. Both are
  needed.

---

## Suggested first session back

All three of the items that used to be here have been played and answered
(§114, §119): 1.1 and 1.2 are closed, and 2.4 is answered in seven parts --
the ghosting, the layout anchoring, the party colours, the city palette, the
town fog, the divider struck through the sidebar, and the biome tints that had
never separated by more than four levels of 255. Its raw tile speed is the one
thing still open, and it waits on numbers only a human at a real terminal can
read: frames per second in tile mode, and the terminal's cell size in pixels.

**What is left on this page is balance**, and balance is deliberately last --
it is the closing act of the project, and everything above it moves the numbers,
so measuring before the content is finished means measuring twice. Start with
the purse: `p->gold` at new-game is a 600,000,000 testing value and the
shipping number is 60.

---

## Take control *(started 2026-08-10; the Hero refactor landed the same day)*

GTA-style body switching. Tab hands the run to another party member.

**The governing rule, in the project owner's words: "as soon as we change to
the new hero, he de facto becomes the MC."**

Read that as: *main character is a role, not a character.* Whoever holds it is
the MC, and everything attached to the role attaches to them -- the purse, the
shared charms, the Inn's bed, the character sheet, the game-over condition.
The one you left becomes an ordinary hire.

It is worth stating this once and loudly because it dissolves, rather than
answers, every question that looks open here. What walks into town when the
original character is dead? The MC does -- that is now the hero. What does the
Inn checkpoint? The MC. Who owns the gold? The MC. What transfers on a swap?
Nothing: the role moved, and the role is where those things were attached all
along. Each of those was asked and each was already answered by the sentence
above.

The rest of the shape, also the owner's: the character you left fights on
under AI, switching is instant and free, and a body dying passes control to
the next one rather than ending the run -- the party are lives, and the run
ends only when nobody is standing.

### DONE: one entity, not two

**"A MC is just a controlled hero. NOTHING MORE."** -- the project owner, after
four bugs in a row, and it was the correct design. It is now what the code
says. `Hero` carries everything a body carries -- the thirty attributes, the
derived sheet, weapons, armour, spells, ability, HP, XP, aether, position,
status -- and `Player` is `Hero party[6]` plus the state the party shares.
There is no `Companion` struct and no reduced copy of anything.

**What it cost and what it caught**, recorded because the estimate below was
the reason it kept being deferred: 1,873 body-field accesses moved, in one
compiler-driven pass that preserved behaviour exactly (`p->hp` meant the
character, so `p->party[0].hp` is a faithful translation), followed by a
smaller pass that changed what needed changing. Three suites stayed green
throughout, which is the only reason a change this wide was safe to make.

Two real bugs fell out, neither of them looked for:

- **A driven hire never ticked.** The AI loop's "skip the body the player is
  driving" sat *above* the block that decrements cooldowns, regenerates aether
  and expires buffs -- so the one body you were actually fighting with was the
  one body for which time did not pass. Take over a caster and their spells
  stayed on cooldown for the rest of the run. The line was written to mean "the
  AI does not decide for this body" and quietly also meant "this body does not
  exist this turn".
- **The snare gap closed itself.** The previous session wrote down that a
  snared hire takes the damage but not the *hold*, "because Companion has no
  stun field", and left it as a four-line struct change for someone who had
  played it. There is one struct now, so a hire has the stun field the
  character always had. It needed deleting, not fixing.

**And the thing the refactor was actually for**: a hire is now rolled by the
same constructor as the character, so they arrive with a real attribute sheet,
a real derived sheet, their class's own innate ability, and their class's own
magic school instead of one drawn from a name hash. Verified across a roster:
six of six candidates carry thirty attributes where they previously carried
none. `train_attribute`, `hero_eff_atk`, `hero_eff_def`, `effective_stat`,
`recompute_set_bonus`, `cast_spell_slot`, `use_ability` and `fire_ranged` all
take a `Hero *` and cannot tell whether a person or the AI is driving it.

`SAVE_VERSION` is 40. Old saves do not migrate and are not worth migrating --
the file has always been the struct, and the struct is a different shape.

**Then it was played, and three bugs came straight out** (EVALUATION §91) --
the `@` vanishing after a switch, a second identical `@` turning up next to
you, and auto-explore stopping "after a while". All three were the same premise
one layer down: a single set of eyes and a single "you" belonging to nobody,
which the animation restore path, the glyph colour and auto-explore's
stuck-detector each assumed was the character's.

The fix is `src/vision.h`: **one map per body.** Six `HeroVision`s, each with
its own field of view and its own memory of the floor, composited into the Tile
flags the renderer and the searches read. Nothing downstream changed, and there
is now a real answer to "what does this hero know". 13.4 MB, held once, with a
per-slot lit-list so the per-turn cost is what was lit rather than what exists.
Threads were offered for this and are not needed -- the reasoning is in §91,
and the slots are independent if that ever changes.

~~**Still open**: movement and the melee swing are still two functions,
`handle_player_move` for slot 0 and `companion_player_move` for a driven
hire.~~ **Done, and this note was stale for several sessions** (EVALUATION
§106). Neither function exists: `actor_step()` replaced them during the
six-actor rebuild, and `companions.c` says so at the site where the second one
used to stand.

**It was right about where the next bug would be, though.** Looking for the
duplicate found a live one in the same shape: `districts_tick()` carried the
driven body and every monster along belts and currents and left the other five
party members standing in a running channel, and `displace_actor()` refused to
land anything on *the driven body* while knowing nothing about the other five
-- so a belt could shove a monster onto a hire. Both fixed. The balance has not been re-measured; see the note at the end of this
section, which still stands.

**And the lesson worth carrying**, because it cost a play session to learn:
green suites, a clean sanitiser and an unchanged warning count said the
refactor was sound, and they were right and insufficient. None of them can see
a glyph drawn in the wrong colour or a progress signature computed from the
wrong body. *When the suites are green and the game is wrong, suspect the
instruments' coverage* -- the other half of the rule in §68.

Everything built for take-control before this assumed the opposite -- that
`Player` was the real thing and `Companion` a reduced copy -- and it produced a
bug per system, every one of them the same bug. Kept as the record of what the
premise cost, because the seventh and eighth are below and they were found by
removing it rather than by looking:

| symptom | cause |
|---|---|
| keys moved the wrong body | the AI also drove the body you steered |
| nothing on screen was "you" | the highlight was pinned to `Player` |
| auto-explore walked the character | it read `p->x/p->y` |
| the panel lost XP and AE | `Companion` has no aether bar worth drawing |
| the roster listed you, not the character | the list predates the MC being a member |
| `tGold`, `mFloor` debris | the panel changed height with whose body it drew |

Six symptoms, one premise. Patching them one at a time is what produced this
list, and it will keep producing it: every system that says `p->` is another
place the premise leaks.

**The change, as built.** One `Hero` carrying what a character carries --
attributes, derived stats, gear, spells, ability, HP/XP/aether, position,
status. `Player` is the *party*: `Hero party[6]`, `controlled`, plus the
genuinely shared state (gold, the pack, keys, scrap, the larder, quest and
floor progress). No `Companion` struct, no reduced copy -- systems take a
`Hero *` and cannot tell whether a person or the AI is driving it.

**The shape, in the project owner's words: a factory that makes heroes, and a
flag marking which one is in use.** That names the root better than the prose
above does -- the problem is not really two structs, it is **two
constructors**. `apply_class_to_player()` builds the character from a class
seed; `tavern_candidate()` / `companion_hire()` builds a hire from an
archetype tilt. Two creation paths producing two shapes, and every difference
downstream falls out of that.

So: one `hero_create(...)` that everything goes through -- the character at
new-game, a hire at the Tavern, a granted golem, whatever comes later -- and
per-hero flags for `in_use` (this slot holds somebody), `alive`, and the
party-level `controlled` index saying which one has the controller attached.
In C that is a factory function plus a fixed array; the C# instinct maps
straight over, and it is what makes "the MC is just a controlled hero" true in
the code rather than only in the design.

Both of the things that were meant to fall out for free did. A hire is rolled
with a real class *and* the sheet that goes with it, and the character is no
longer the only body with attributes. One refinement the design did not
anticipate: `alive` is a fact about a body and belongs in the constructor,
`in_use` is a fact about the *party* -- which seat, if any -- so it is set by
whoever seats them. A Tavern candidate you are only looking at is alive and
holds no seat, which is exactly right and was wrong when the constructor set
both.

**Why it was not built that way.** The Companion struct's own comment says it:
carrying thirty attributes and two dozen derived stats per hire is "what
making a Companion into a Player would amount to", and that was a reasonable
call when hires were followers. It stopped being reasonable the moment a hire
could *be* the main character.

**Scope, as it turned out.** The estimate was right about the breadth and
wrong about the difficulty. Combat, render, save, shops, mapgen, companions and
the simulator all took `Player *` and all touched these fields -- but once the
struct changed, the compiler enumerated every single site, and the fix at each
was mechanical. The judgement was concentrated in perhaps thirty places, not
two thousand. **The lesson worth keeping: a refactor this wide is a
compiler-driven pass, not a reading exercise, and the way to make it safe is to
split it into one pass that provably cannot change behaviour and a second that
is small enough to review.**

**Take-control is no longer a prototype.** `PARTY_SWITCH_ENABLED` is 1 and has
been since before this refactor; the note that it was gated off at 0 was stale
by the time it was read.

### The architecture note, which is the important part

Stated by the project owner after three bugs in a row: **the main character is
not special. The character and the hires are all derivations of the same
thing -- a playable body -- and the role is what moves between them.**

That is the diagnosis for a whole class of bug, not a preference. Everything
built here first treated `Player` as *the player* and `Companion` as *not the
player*, so every system needed an `if (controlled)` branch and every place
that did not have one broke:

- the companion AI took a turn for the body the player was steering, so every
  keypress was answered by the hire walking off on its own;
- the screen's "you" highlight stayed pinned to the character, so nothing
  under the camera was you;
- auto-explore read `p->x/p->y` and walked the character while you were
  somebody else.

Three symptoms, one cause, and all three found by playing rather than by the
suite -- which passed throughout, because the role *semantics* were right and
what was wrong was who else acted on a body and what the screen called you.

`party_step()` in main.c is where the fix starts: callers ask for a step and
never learn who took it. `party_x/party_y` are the same idea for position.
The full version is an actor interface both structs satisfy, so that a system
added next year cannot forget the branch -- because there is no branch. Until
that exists, treat every `p->x`, `p->y` and `p->hp` outside combat as a
suspect.

### What is shared, and what is not

Settled by the project owner, and it is the decision that keeps this feature
small: **gold and recall charms are the party's. Everything else is the
body's.** Weapons, armour, spells, items, level, experience -- a hire you take
over fights with their own kit, not the character's.

Both shared things are *already* shared, by construction and with no work
required: only Player has a purse, and count_recall_charms() reads one
inventory. Every shop, the smith, the bank and the black market take a
`Player *` and therefore keep working untouched whoever is being driven.

This is worth stating plainly because the obvious-sounding alternative -- one
pack that any body can equip from -- would mean equipment stops being fields
on Player and becomes "an item in the pack, currently worn by body N", which
is an ownership field on every item and a rewrite of combat, the shops, the
smith, the character sheet and recompute_set_bonus. That is not the design.
The party is six kits that travel together and share a purse.

**Done.** `Player.controlled` (0 = MC, slot+1 = a hire), the `party_x/party_y/
party_body/party_switch_next/party_anyone_alive` accessors in common.c, the
camera, the field of view, and the top half of the side panel -- all of which
now read the driven body instead of the main character. The panel splits into
*whoever you are* (name, class, vitals, the numbers you fight with) above, and
*what the party carries* (gold, keys, scrap, larder, floor) below -- which is
exactly the shared/not-shared line above, drawn on screen.

**Gated off** behind `PARTY_SWITCH_ENABLED` in common.h, at 0. With
`controlled` at 0 every accessor returns the MC and the game is exactly as it
was -- verified by the suites and the warning baseline.

**The four things that were left are all done**, and were done before the
Hero refactor rather than by it -- the list below had gone stale in the file
while the code moved on. Kept because each entry records a decision, and the
snare gap in item 1 is the one the refactor closed on its way past.

1. ~~Movement and attacking still operate on the MC.~~ **Done.**
   `companion_player_move` walks and swings with the driven body's own arm,
   and `party_landing_effects` gives them the stairs and the waygate, because
   the role is what the staircase answers to.

   The landing effects are split along the same line as everything else:

   - **body-level** -- lava, miasma and snares hurt whoever walked into them,
     routed through `companion_take_damage` so a driven hire can actually die
     to the ground and hand the role on mid-step (`hire_landing_damage`);
   - **party-level** -- loot goes in the shared pack, locked doors open to the
     party's keys, portals move whoever stepped in, and the stairs and the
     waygate move everybody (`party_landing_effects`).

   One gap, written down rather than bodged: a snared hire takes the damage
   but not the *hold*, because Companion has no stun field -- that lives on
   Monster and on Player. Four lines to add, and SAVE_VERSION has already
   moved this session, but it is a struct change for one hazard and it can
   wait for someone who has played it.

   Shrines and other features already trigger through the shared path, since
   `trigger_feature` takes the Player and the benefit is the party's.
2. **The MC fights on under the companion AI, keyed on their class.** The
   swap is symmetric: the body you take over *is* the protagonist, and the
   character you left becomes another hire. Which is the answer to what looked
   like the hard part -- the MC does not need an AI written for them. Every
   class carries an archetype (`CLASS_TABLE[class_id].archetype`, the thing
   the player actually picks first), and companion behaviour is already keyed
   on archetype -- `companion_ability_name(int archetype)` and the stat tilt
   both take one. So an undriven MC is run as a Vanguard, a Marksman, an
   Arcanist, whatever their class says they are.

   The mechanical shape: a lightweight Companion view of the MC while they are
   not being driven, populated from their real hp/atk/def/level with the
   archetype supplying behaviour. Not a copy of the Player -- carrying thirty
   attributes and two dozen derived stats is exactly what the Companion struct
   exists to avoid -- and not a rewrite of the AI either.
3. **Death has to hand control on.** `STATE_GAMEOVER` currently means "the MC
   hit zero"; it has to become `!party_anyone_alive(p)`.

   The obvious-looking knock-on is not one. "What walks into town when the
   original character is dead?" answers itself from the rule above: whoever
   you are driving *is* the main character, so the hero walks in as the main
   character. Nothing transfers, because nothing was ever personally the dead
   one's -- the purse and the shared charms belong to the party by the
   sharing rule, and the hero has always had their own weapon and spells.

   Read `Player` as **the party's shared state plus the original character's
   body**, not as "the protagonist". The shared half outlives any particular
   body; `controlled` says which body is currently the protagonist. Once that
   reading is taken, death handling is three lines and no data moves.

   `save_inn_snapshot()` follows the same rule: it checkpoints the MC, which
   is whoever holds the role when the bed is paid for. The snapshot therefore
   has to record `controlled` along with everything else -- not as a special
   case, but because that field is part of what the MC currently is.
4. ~~**`SAVE_VERSION` has to move** -- `controlled` is new state on Player.~~
   **Done** (39), and moved again by the refactor (40).

**It will break the balance**, and that is understood and accepted: §89's
ladder is measured against a party where only the MC is driven. Re-measure
after, not during. The simulator knows nothing about control at all, so every
number in EVALUATION remains valid for the game as currently shipped.

*Still true after the Hero refactor, and now the top of the list.* The
refactor was verified against the three suites, the aggressive warning scan
(baseline 3, unchanged) and four ASAN sessions -- none of which measures
balance. The one thing that did change under the simulator, and the reason to
re-measure before trusting any number in §83 or §89 again, is that a driven
body's cooldowns now advance. **Re-measuring the ladder is the next task on
this page, ahead of the world-size axis.**
