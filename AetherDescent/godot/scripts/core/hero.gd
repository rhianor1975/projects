class_name Hero
extends RefCounted
## A body: the character you drive. Built from a class seed exactly as
## hero_create() / hero_compute_derived() build one in src/classes.c.

var name := "Wanderer"
var class_id := 0
var archetype := 0
var look := 0
var attrs := PackedInt32Array()

var level := 1
var xp := 0
var xp_next := 20
var hp := 30
var maxhp := 30
var base_atk := 4
var base_def := 1

var weapon_name := ""
var weapon_bonus := 0
var weapon_plus := 0
var weapon_set := -1
var armor_name := ""
var armor_bonus := 0
var armor_plus := 0
var armor_set := -1
var ring_name := ""
var ring_stat := C.Acc.NONE
var ring_bonus := 0
var ring_set := -1
var trinket_name := ""
var trinket_stat := C.Acc.NONE
var trinket_bonus := 0
var trinket_set := -1
var hp_plus := 0
var ammo_plus := 0

var set_complete := -1
var set_bonus_atk := 0
var set_bonus_def := 0
var set_bonus_evasion := 0
var set_bonus_ward := 0
var set_bonus_hazard := 0
var set_bonus_crit := 0

# derived from the thirty
var fov_radius := 6
var hearing_radius := 0
var evasion_pct := 0
var crit_pct := 0
var ward_pct := 0
var hazard_resist_pct := 0
var xp_bonus_pct := 0
var gold_bonus_pct := 0
var shop_discount_pct := 0
var potion_potency_pct := 0
var gear_bonus_pct := 0
var buff_duration_pct := 0
var aggro_reduction := 0
var ambush_resist_pct := 0
var recall_turns := 3
var machine_luck_pct := 0
var fortune_luck_pct := 0
var jinx_pct := 0
var relic_quality_bonus := 0
var aether_max := 8
var aether := 8
var spell_power_pct := 0
var ranged_ammo_max := 6
var ranged_dmg_bonus_pct := 0
var ranged_ammo := 6

var magic_school := 0
var ability_id := 0
var ability_cd := 0
var known_spells: Array = []     # pool indices
var spell_cd: Array = []
var last_spell_slot := -1

var ranged_type := C.Ranged.NONE
var ranged_name := ""
var ranged_bonus := 0
var ranged_ammo_cost := 1
var ranged_cooldown_max := 0
var ranged_cooldown := 0

var atk_buff := 0
var atk_buff_turns := 0
var def_buff := 0
var def_buff_turns := 0
var evasion_buff := 0
var evasion_buff_turns := 0
var reflect_pct := 0
var reflect_turns := 0
var poison_turns := 0
var poison_dmg := 0
var stun_turns := 0
var slow_turns := 0
var held_turns := 0            # caught in a snare
var stance_atk_pct := 0        # the chromatic abyss
var stance_def_pct := 0
var still_turns := 0

var x := 0
var y := 0
var facing := "down"
var alive := true

# ---- a hire (companions.c) ---------------------------------------------------------
# The character and the heroes from the Tavern are the same kind of body; these
# are what a hire carries on top. The character has roster_idx -1 and none of it.
var is_hire := false
var roster_idx := -1
var is_client := false         # walking with you on an escort bounty
var personality := 1           # Party.BOLD / STEADY / CAUTIOUS
var tier := 0                  # which price step was paid
var kills := 0
var gear: Array = []           # [{name, atk, def, hp}] -- found on their own kills
var potions: Array = []        # [{name, heal}] -- picked up and kept
var shot_cd := 0
var guard_turns := 0           # Bulwark
var last_x := -1
var last_y := -1
var scout_x := -1
var scout_y := -1
var scout_turns := 0


static func make(class_id_: int, name_: String) -> Hero:
	var h := Hero.new()
	h.create(class_id_, name_)
	return h


func create(cid: int, n: String) -> void:
	class_id = clampi(cid, 0, ClassesData.CLASSES.size() - 1)
	var seed: Dictionary = ClassesData.CLASSES[class_id]
	name = n
	archetype = seed.archetype
	look = archetype * 2 + (class_id % 2)
	attrs = PackedInt32Array()
	attrs.resize(C.ATTR_COUNT)
	attrs.fill(3)
	for ov in seed.attrs:
		attrs[ov[0]] = ov[1]
	compute_derived()
	magic_school = _pick_school()
	ability_id = _pick_ability()
	var a := attrs
	var base_weapon := clampi(a[C.Attr.MIGHT] / 3 + a[C.Attr.PRECISION] / 4, 1, 6)
	var base_armor := clampi(a[C.Attr.GRIT] / 3 + a[C.Attr.FORTITUDE] / 4, 1, 6)
	weapon_bonus = base_weapon + base_weapon * gear_bonus_pct / 100
	armor_bonus = base_armor + base_armor * gear_bonus_pct / 100
	weapon_name = seed.weapon
	armor_name = seed.armor


func compute_derived() -> void:
	var a := attrs
	base_atk = 4 + a[C.Attr.MIGHT] / 2 + a[C.Attr.BRAWN] / 4
	base_def = 1 + a[C.Attr.GRIT] / 2 + a[C.Attr.FORTITUDE] / 4
	maxhp = 30 + (a[C.Attr.BRAWN] + a[C.Attr.STAMINA]) * 2
	hp = maxhp
	fov_radius = clampi(6 + (a[C.Attr.VISION] - 3) / 2, 5, 11)
	hearing_radius = clampi(a[C.Attr.HEARING] - 3, 0, 6)
	evasion_pct = clampi((a[C.Attr.AGILITY] + a[C.Attr.REFLEXES] + a[C.Attr.BALANCE]) / 3 * 2 - 4, 0, 30)
	crit_pct = clampi((a[C.Attr.PRECISION] + a[C.Attr.FORTUNE]) * 2 - 8, 0, 30)
	ward_pct = clampi(a[C.Attr.WARDING] * 2 - 4, 0, 25)
	hazard_resist_pct = clampi((a[C.Attr.FORTITUDE] - 3) * 6 + (a[C.Attr.RESOLVE] - 3) * 2, 0, 55)
	xp_bonus_pct = clampi((a[C.Attr.INTELLECT] - 3) * 5, 0, 45)
	gold_bonus_pct = clampi((a[C.Attr.CUNNING] + a[C.Attr.FORTUNE] - 6) * 3, 0, 45)
	shop_discount_pct = clampi((a[C.Attr.CHARM] - 3) * 3 + (a[C.Attr.MEMORY] - 3), 0, 25)
	potion_potency_pct = clampi((a[C.Attr.CHEMISTRY] - 3) * 5, 0, 50)
	gear_bonus_pct = clampi((a[C.Attr.CRAFTING] - 3) * 3, 0, 30)
	buff_duration_pct = clampi((a[C.Attr.CONDUIT] - 3) * 4, 0, 40)
	aggro_reduction = clampi(a[C.Attr.PRESENCE] - 3, 0, 6)
	ambush_resist_pct = clampi((a[C.Attr.GUILE] - 3) * 5 + (a[C.Attr.INSTINCT] - 3) * 2, 0, 40)
	recall_turns = clampi(3 - (a[C.Attr.STAMINA] - 3) / 3, 1, 3)
	machine_luck_pct = clampi((a[C.Attr.TECH_WIT] - 3) * 4, 0, 30)
	fortune_luck_pct = clampi((a[C.Attr.FORTUNE] + a[C.Attr.EMPATHY] - 6) * 3, 0, 30)
	jinx_pct = clampi(a[C.Attr.JINX] * 2 - 4, 0, 25)
	relic_quality_bonus = clampi(a[C.Attr.RESONANCE] - 3, 0, 6)
	aether_max = 8 + a[C.Attr.CONDUIT] * 2
	spell_power_pct = clampi((a[C.Attr.RESONANCE] - 3) * 8, 0, 80)
	aether = aether_max
	ranged_ammo_max = 6 + a[C.Attr.PRECISION] * 2
	ranged_dmg_bonus_pct = clampi((a[C.Attr.PRECISION] - 3) * 6, 0, 60)
	ranged_ammo = ranged_ammo_max


func _pick_school() -> int:
	var a := attrs
	var cand := [a[C.Attr.CONDUIT], a[C.Attr.RESONANCE], a[C.Attr.WARDING],
		a[C.Attr.AETHER_SENSE], a[C.Attr.SCRIBING], a[C.Attr.JINX]]
	var best := 0
	for i in range(1, cand.size()):
		if cand[i] > cand[best]:
			best = i
	if cand[best] <= 3:
		best = class_id % 6
	return best


func _pick_ability() -> int:
	var a := attrs
	var cand := [a[C.Attr.MIGHT], a[C.Attr.BRAWN], a[C.Attr.AGILITY], a[C.Attr.REFLEXES],
		a[C.Attr.PRECISION], a[C.Attr.GRIT], a[C.Attr.FORTITUDE], a[C.Attr.STAMINA],
		a[C.Attr.BALANCE], a[C.Attr.INSTINCT]]
	var best := 0
	for i in range(1, cand.size()):
		if cand[i] > cand[best]:
			best = i
	if cand[best] <= 3:
		best = class_id % 10
	return best


## Train one attribute (the Gladiator School), applying the change as the
## difference between two derived sheets -- shift_attributes() in classes.c --
## so levels, shrines and upgrades already earned are kept.
func shift_attribute(give: int, take: int) -> void:
	var before := Hero.new()
	before.attrs = attrs.duplicate()
	before.compute_derived()
	var after := Hero.new()
	after.attrs = attrs.duplicate()
	if give >= 0:
		after.attrs[give] -= 1
	if take >= 0:
		after.attrs[take] += 1
	after.compute_derived()
	attrs = after.attrs.duplicate()
	for f in ["base_atk", "base_def", "maxhp", "fov_radius", "hearing_radius", "evasion_pct",
			"crit_pct", "ward_pct", "hazard_resist_pct", "xp_bonus_pct", "gold_bonus_pct",
			"shop_discount_pct", "potion_potency_pct", "gear_bonus_pct", "buff_duration_pct",
			"aggro_reduction", "ambush_resist_pct", "recall_turns", "machine_luck_pct",
			"fortune_luck_pct", "jinx_pct", "relic_quality_bonus", "aether_max",
			"spell_power_pct", "ranged_ammo_max", "ranged_dmg_bonus_pct"]:
		set(f, get(f) + after.get(f) - before.get(f))
	hp = mini(hp + after.maxhp - before.maxhp, maxhp)


# ---- effective stats ---------------------------------------------------------------
func eff_atk() -> int:
	var atk := base_atk + weapon_bonus + weapon_plus * C.UPGRADE_STEP + atk_buff + set_bonus_atk
	atk += atk * stance_atk_pct / 100
	return maxi(atk, 1)


func eff_def() -> int:
	var d := base_def + armor_bonus + armor_plus * C.UPGRADE_STEP + def_buff + set_bonus_def
	d += d * stance_def_pct / 100
	return maxi(d, 0)


func effective_stat(which: int) -> int:
	var base := 0
	match which:
		C.Acc.CRIT: base = crit_pct + set_bonus_crit
		C.Acc.EVASION: base = evasion_pct + set_bonus_evasion
		C.Acc.WARD: base = ward_pct + set_bonus_ward
		C.Acc.GOLD: base = gold_bonus_pct
		C.Acc.XP: base = xp_bonus_pct
		C.Acc.REGEN: base = 0
	if ring_stat == which:
		base += ring_bonus
	if trinket_stat == which:
		base += trinket_bonus
	return base


func recompute_set_bonus() -> void:
	set_complete = -1
	set_bonus_atk = 0
	set_bonus_def = 0
	set_bonus_evasion = 0
	set_bonus_ward = 0
	set_bonus_hazard = 0
	set_bonus_crit = 0
	for b in 5:
		var n := int(weapon_set == b) + int(armor_set == b) + int(ring_set == b) + int(trinket_set == b)
		if n < 2:
			continue
		set_bonus_atk += C.SET_2PC
		set_bonus_def += C.SET_2PC
		if n < 4:
			continue
		set_complete = b
		match b:
			C.Biome.JUNGLE: set_bonus_evasion += C.SET_4PC
			C.Biome.INDUSTRIAL: set_bonus_def += C.SET_4PC
			C.Biome.RUINS: set_bonus_ward += C.SET_4PC
			C.Biome.WASTES: set_bonus_hazard += C.SET_4PC
			C.Biome.ABYSS: set_bonus_crit += C.SET_4PC


func class_name_str() -> String:
	return ClassesData.CLASSES[class_id].name


func pos() -> Vector2i:
	return Vector2i(x, y)


# ---- persistence ---------------------------------------------------------------------
const _SKIP := ["RefCounted", "script", "Built-in script"]


func to_dict() -> Dictionary:
	var d := {}
	for p in get_property_list():
		if p.usage & PROPERTY_USAGE_SCRIPT_VARIABLE:
			var v = get(p.name)
			if v is PackedInt32Array:
				v = Array(v)
			d[p.name] = v
	return d


static func from_dict(d: Dictionary) -> Hero:
	var h := Hero.new()
	for k in d:
		if k == "attrs":
			h.attrs = PackedInt32Array(d[k])
		elif typeof(h.get(k)) == TYPE_INT:
			h.set(k, int(d[k]))
		else:
			h.set(k, d[k])
	return h
