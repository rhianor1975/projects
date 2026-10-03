class_name Dda
## How the run is going, and what the place does about it -- a port of
## src/dda.c and src/bands.c.
##
## Pressure reads performance: a floor cleared comfortably pushes it up, one
## that mauled you or drove you out pushes it down, harder and faster. Quick
## to forgive, slow to punish.
##
## The bands are what the environment does *for* a run that is drowning --
## invisible, asymmetric, one knob: the Roots feed you, the Works give back
## aether and shot, the Ruins' wards turn blows aside, the Wastes' salt keeps
## a draught unspent, the Abyss's dark hides you. They only ever add.
##
## The biome rules are the opposite: what each band always does, to everyone.
##
## The guardian angel is a cheat, not a difficulty: off unless you switch it
## on. It moves dice -- crit and evasion for a run in trouble, a softer
## monster critical, one caught killing blow a floor above floor 50 -- and it
## leans on a run that is coasting.

const MAX := 40
const MIN := -40
const CRUISED := 4
const STEADY := 1
const HURT := -8
const ROUTED := -14
const BAND_MAX_HELP := 4

const GROWTH_COVER := 2       # the Roots: tiles off everyone's notice
const RUINS_WARD_PCT := 12    # the Ruins: your blows a monster's ward turns aside
const WASTES_BITE := 3        # the Wastes: extra damage the ground does
const ABYSS_BLIND := 3        # the Abyss: tiles of sight the dark takes
const ABYSS_SIGHT_FLOOR := 4

const ESCORT_GRACE := 15
const FADES_BY := 50
const NET_GRACE := 20
const MAX_GRACE := 90
const BY_DIFFICULTY := [100, 180, 220, 260]
const RECOIL_TURNS := 3
const AVENGER_MAX := 25
const AVENGER_EARLY := 25


# ---- pressure --------------------------------------------------------------------
static func floor_begin() -> void:
	Game.dda_start_hp = Game.hero.hp
	Game.dda_heals = 0
	Game.dda_retreated = false
	Game.guardian_caught = false


static func _comfort() -> int:
	var h := Game.hero
	return h.hp * 100 / h.maxhp if h.maxhp > 0 else 100


## How the floor underfoot is going. Only ever negative: a floor going badly
## says so at once, a floor going well waits to be finished.
static func floor_so_far() -> int:
	if Game.depth <= 0:
		return 0
	var c := _comfort()
	if Game.dda_retreated or c < 20:
		return ROUTED
	if c < 45 or Game.dda_heals >= 3:
		return HURT
	return 0


static func pressure() -> int:
	return clampi(Game.dda_pressure + floor_so_far(), MIN, MAX)


## Read how the floor being left went. Called before the next is entered.
static func floor_end() -> void:
	if Game.depth <= 0 or Game.map == null:
		return
	var c := _comfort()
	var delta := STEADY
	if Game.dda_retreated or c < 20:
		delta = ROUTED
	elif c < 45 or Game.dda_heals >= 3:
		delta = HURT
	elif c >= 85 and Game.dda_heals == 0:
		delta = CRUISED
	Game.dda_pressure = clampi(Game.dda_pressure + delta, MIN, MAX)


static func label() -> String:
	var v := pressure()
	if v >= 25: return "coasting"
	if v >= 10: return "comfortable"
	if v > -10: return "even"
	if v > -25: return "pressed"
	return "drowning"


# ---- the bands' gifts ------------------------------------------------------------
## How much help the floor underfoot will give: zero unless pressure is
## negative, rounded up so any trouble at all buys the smallest real step.
static func band_help() -> int:
	if Game.depth <= 0:
		return 0
	var p := pressure()
	if p >= 0:
		return 0
	return mini((-p * BAND_MAX_HELP + (-MIN) - 1) / (-MIN), BAND_MAX_HELP)


static func _on(biome: int) -> bool:
	return Game.depth > 0 and C.biome_for_floor(Game.depth) == biome


static func regen_bonus() -> int:
	return (band_help() + 1) / 2 if _on(C.Biome.JUNGLE) else 0


static func recharge_bonus() -> int:
	return (band_help() + 1) / 2 if _on(C.Biome.INDUSTRIAL) else 0


static func ward_bonus() -> int:
	return band_help() * 3 if _on(C.Biome.RUINS) else 0


static func supply_saved() -> bool:
	if not _on(C.Biome.WASTES):
		return false
	var help := band_help()
	return help > 0 and Game.rng.randi() % 100 < help * 10


static func aggro_reduction() -> int:
	return band_help() if _on(C.Biome.ABYSS) else 0


# ---- what each band always does ------------------------------------------------------
static func growth_cover() -> int:
	return GROWTH_COVER if _on(C.Biome.JUNGLE) else 0


static func monster_ward_pct() -> int:
	return RUINS_WARD_PCT if _on(C.Biome.RUINS) else 0


static func hazard_extra() -> int:
	return WASTES_BITE if _on(C.Biome.WASTES) else 0


## The Abyss is dark; Aether-Sense is what answers it.
static func sight(h: Hero, r: int) -> int:
	if not _on(C.Biome.ABYSS):
		return r
	var blind := ABYSS_BLIND
	if h.attrs[C.Attr.AETHER_SENSE] >= 7:
		blind /= 2
	return maxi(r - blind, mini(r, ABYSS_SIGHT_FLOOR))


# ---- the guardian angel (an assist you switch on) ------------------------------------
static func guardian_on() -> bool:
	return bool(Game.settings.get("guardian", false))


static func grace() -> int:
	if not guardian_on() or Game.depth <= 0:
		return 0
	var g := 0
	if Game.depth < FADES_BY:
		g += ESCORT_GRACE * (FADES_BY - Game.depth) / FADES_BY
	var c := _comfort()
	if c < 45:
		g += NET_GRACE * (45 - c) / 45
	g = g * BY_DIFFICULTY[clampi(Game.difficulty, 0, 3)] / 100
	return clampi(g, 0, MAX_GRACE)


## The monster's unavoidable critical, softened -- never to nothing.
static func softened_crit(base: int) -> int:
	var g := grace()
	if base <= 0 or g <= 0:
		return base
	var relief := mini((base * g + 2 * MAX_GRACE - 1) / (2 * MAX_GRACE), base / 2)
	return maxi(base - relief, 1)


## A killing blow, converted into one hit point: once a floor, above floor 50.
static func guardian_catch() -> bool:
	var h := Game.hero
	if not guardian_on() or Game.depth <= 0 or Game.depth >= FADES_BY or Game.guardian_caught:
		return false
	Game.guardian_caught = true
	h.hp = 1
	h.alive = true
	Game.warn("You should be dead. You are not. Something in the dark was not finished with you.")
	# a turn is not a rescue, it is a chance: what stands over you recoils
	var stunned := 0
	for mo in Game.map.monsters:
		if mo.alive and absi(mo.x - h.x) <= 1 and absi(mo.y - h.y) <= 1:
			mo.stun_turns = maxi(mo.stun_turns, RECOIL_TURNS)
			stunned += 1
	if stunned > 0:
		Game.good("Whatever was standing over you recoils -- you have a moment. Use it.")
	return true


## The other half: a run that is coasting gets leaned on, a little.
static func avenger_escalation() -> int:
	if not guardian_on():
		return 0
	var p := pressure()
	if p <= 10 or Game.depth <= 0:
		return 0
	var extra := AVENGER_MAX * (p - 10) / (MAX - 10)
	var weight := 100 if Game.depth >= FADES_BY else AVENGER_EARLY + (100 - AVENGER_EARLY) * Game.depth / FADES_BY
	return clampi(extra * weight / 100, 0, AVENGER_MAX)
