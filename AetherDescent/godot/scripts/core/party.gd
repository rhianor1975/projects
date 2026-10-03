class_name Party
## Heroes hired at the Tavern -- a port of src/companions.c.
##
## They are not a pet system. The player never equips them, never orders them,
## never picks their spells. They walk the floor on their own, fight what they
## run into and level off their own kills -- closer to other players sharing
## the dungeon than to a summon. What the player controls is who to pay for,
## and each one costs ten times the last.
##
## Game.party holds every body: slot 0 is the character the run started as,
## the rest are hires. Game.hero is whichever one the player is driving, and
## the AI here runs every body that is not -- the character included, when
## you have taken over somebody else.

const MAX_COMPANIONS := 5
const TAVERN_ROSTER := 20
const NAME_SHOWN := 24
const COMPANION_FOV := 5
const SHOT_COOLDOWN := 2
const ABILITY_CD := 10
const LOOT_RANGE := 24
const SCOUT_PATIENCE := 40
const GEAR_SLOTS := 3
const POTION_SLOTS := 3
const SPELL_SLOTS := 3

enum { BOLD, STEADY, CAUTIOUS }
const PERSONALITY_NAMES := ["Bold", "Steady", "Cautious"]

const M32 := 0xFFFFFFFF

# Victorian drawing-room by way of the engine deck: airship captains,
# calculating-machine professors, safe-crackers and society ladies.
const GIVEN := [
	["Alistair", "m"], ["Zephyrine", "f"], ["Thaddeus", "m"], ["Seraphina", "f"], ["Barnaby", "m"],
	["Orion", "m"], ["Isadora", "f"], ["Magnus", "m"], ["Cordelia", "f"], ["Peregrine", "a"],
	["Archimedes", "m"], ["Emmeline", "f"], ["Nikola", "a"], ["Genevieve", "f"], ["Horatio", "m"],
	["Theodosia", "f"], ["Kelvin", "m"], ["Rosalind", "f"], ["Silas", "m"], ["Mordecai", "m"],
	["Lorelei", "f"], ["Augustus", "m"], ["Katrina", "f"], ["Victor", "m"], ["Penelope", "f"],
	["Reginald", "m"], ["Violetta", "f"], ["Geoffrey", "m"], ["Esmeralda", "f"], ["Percival", "m"],
	["Eleanor", "f"], ["Winston", "m"], ["Clarissa", "f"], ["Ambrose", "m"], ["Octavia", "f"],
	["Ignatius", "m"], ["Wilhelmina", "f"], ["Bartholomew", "m"], ["Arabella", "f"], ["Cornelius", "m"],
]
const EPITHET := [
	"Finnegan", "Voss", "Roche", "Skye", "Quill", "Blackwood", "Vance", "Sterling", "Wainwright",
	"Hawks", "Babbage", "Gearhart", "Kessel", "Vex", "Thorne", "Wrench", "Flux", "Croft", "Crank",
	"Nettle", "Devereux", "Grimshaw", "Raven", "Locke", "Vancour", "Pierce", "Vesper", "Ashford",
	"Bumblethorpe", "Heliotrope", "Wainscott", "Vane", "Ravenswood", "Quick", "Smythe", "Bramble",
	"Cogsworth", "Ironside", "Pellingham", "Marchbanks",
]
# Honorifics by role; "" is "no title this time", so a plain name stays common.
const TITLES := [
	[["Capt.", "a"], ["Cmdr.", "a"], ["Sir", "m"], ["", "a"]],
	[["", "a"], ["", "a"], ["Sgt.", "a"], ["", "a"]],
	[["Capt.", "a"], ["", "a"], ["", "a"], ["", "a"]],
	[["Prof.", "a"], ["Dr.", "a"], ["Madame", "f"], ["", "a"]],
	[["Prof.", "a"], ["Dr.", "a"], ["Baron", "m"], ["", "a"]],
	[["", "a"], ["", "a"], ["", "a"], ["", "a"]],
	[["Lord", "m"], ["Lady", "f"], ["Duchess", "f"], ["", "a"]],
]
# What each kind of hero is better and worse at, in percent: hp, atk, def.
const ARCH_TILT := [[25, -5, 30], [-10, 15, 0], [-12, 20, -8], [-22, 28, -8], [5, 5, 10], [35, -8, 10], [0, 5, 5]]
const GEAR_PREFIX := ["Etched", "Blackened", "Ether-touched", "Salvaged", "Fluted", "Hollow",
	"Verdigris", "Bone-inlaid", "Storm-cut", "Old Imperial"]
const GEAR_NOUN := [
	["Pauldron", "Greatshield", "Warplate"], ["Stiletto", "Half-cloak", "Spurs"],
	["Longarm", "Scope", "Bandolier"], ["Focus", "Sigil-ring", "Grimoire"],
	["Toolrig", "Charge-pack", "Bracer"], ["Talisman", "Field-kit", "Hide-wrap"],
	["Signet", "Gorget", "Standard"],
]
const ABILITY_NAMES := ["Bulwark", "Flurry", "Called Shot", "Detonation", "Charge Bomb", "Second Wind", "Rally"]
const ABILITY_DESCS := [
	"Digs in: takes half damage for a few turns.",
	"Strikes twice in the time it takes to strike once.",
	"A shot that ignores armour entirely.",
	"Detonates the air around its target.",
	"Throws something that scatters and stuns.",
	"Gets back up: heals a third of its health.",
	"Steadies the party and sharpens your next swings.",
]
const KILL_VERBS := ["cuts down", "opens up", "drops", "burns down", "blows apart", "wears down", "talks past and finishes"]
# The colour each roster seat wears, so you can tell your heroes apart.
const COLORS := [Color8(120, 230, 120), Color8(240, 190, 90), Color8(220, 220, 240),
	Color8(240, 120, 110), Color8(255, 220, 96), Color8(200, 140, 255)]

const HP_ATTRS := [C.Attr.BRAWN, C.Attr.FORTITUDE, C.Attr.STAMINA]
const ATK_ATTRS := [C.Attr.MIGHT, C.Attr.PRECISION, C.Attr.CONDUIT]
const DEF_ATTRS := [C.Attr.GRIT, C.Attr.BALANCE, C.Attr.WARDING]


static func rnd(n: int) -> int:
	return Game.rng.randi() % maxi(n, 1)


# ---- who is who -------------------------------------------------------------------
static func is_up(h: Hero) -> bool:
	return h != null and h.alive and h.hp > 0


static func hires() -> Array:
	return Game.party.filter(func(h): return h.is_hire)


static func count() -> int:
	return hires().size()


## Every body standing that the player is not driving.
static func others() -> Array:
	return Game.party.filter(func(h): return h != Game.hero and is_up(h))


## A body other than the driven one at (x,y).
static func other_at(x: int, y: int) -> Hero:
	for h in Game.party:
		if h != Game.hero and is_up(h) and h.x == x and h.y == y:
			return h
	return null


## Any body at all at (x,y), the driven one included.
static func body_at(x: int, y: int) -> Hero:
	for h in Game.party:
		if is_up(h) and h.x == x and h.y == y:
			return h
	return null


## A hire's given name, for anywhere a whole "Madame Wilhelmina Flux" won't fit.
static func short_name(h: Hero) -> String:
	var parts := h.name.split(" ")
	return parts[parts.size() - 2] if parts.size() >= 2 else h.name


static func color_of(h: Hero) -> Color:
	if not h.is_hire:
		return Color8(128, 224, 248)
	if h.is_client:
		return Color8(255, 236, 190)
	return COLORS[maxi(h.roster_idx, 0) % COLORS.size()]


# ---- the smith works for the whole party -------------------------------------------
# A hire cannot be handed a weapon, so whatever the smith has done to the best
# weapon in the party, theirs has too. It is what carries a hire bought on
# floor 5 down to floor 50.
static func _best(field: String) -> int:
	var best := 0
	for h in Game.party:
		best = maxi(best, int(h.get(field)))
	return best


static func atk_bonus() -> int:
	return _best("weapon_plus") * C.UPGRADE_STEP


static func max_hp(h: Hero) -> int:
	return h.maxhp + (_best("hp_plus") * C.UPGRADE_HP_STEP if h.is_hire else 0)


static func hp_pct(h: Hero) -> int:
	var mx := max_hp(h)
	return h.hp * 100 / mx if mx > 0 else 0


# ---- prices --------------------------------------------------------------------
## What the n-th hire costs: 1k, 10k, 100k, 1M, 10M.
static func hire_cost(already: int) -> int:
	var cost := 1000
	for i in mini(already, MAX_COMPANIONS):
		cost *= 10
	return cost


## A hero who died in your service comes back for a quarter.
static func rehire_cost(t: int) -> int:
	return maxi(1, hire_cost(t) / 4)


# ---- the roster ------------------------------------------------------------------
# Regenerated from the seed rather than stored: same seed, same twenty people,
# every visit and across a save.
static func _mix(a: int, b: int) -> int:
	a &= M32
	b &= M32
	var h := ((a * 2654435761) & M32) ^ ((b + 0x9E3779B9 + (a << 6) + (a >> 2)) & M32)
	h ^= h >> 15
	h = (h * 2246822519) & M32
	h ^= h >> 13
	return h


static func _trait(idx: int, field: int) -> int:
	var seed: int = Game.tavern_seed if Game.tavern_seed != 0 else 1
	var slot: int = idx * 977 + int(Game.tavern_reroll[idx]) * 31
	return _mix(_mix(seed, slot), (field * 2654435761 + 0x51ED270B) & M32)


static func _role(idx: int) -> int:
	return _trait(idx, 5) % 7


static var _arch_classes: Array = []


static func _classes_of(arch: int) -> Array:
	if _arch_classes.is_empty():
		for a in 7:
			_arch_classes.append([])
		for i in ClassesData.CLASSES.size():
			_arch_classes[ClassesData.CLASSES[i].archetype].append(i)
	return _arch_classes[arch]


## Names for the whole roster at once: uniqueness is a property of the list,
## so on a clash the later entry yields and re-rolls.
static var _names_key := ""
static var _names_cache: Array = []


static func _names() -> Array:
	var key := "%d:%s" % [Game.tavern_seed, str(Game.tavern_reroll)]
	if key == _names_key:
		return _names_cache
	var names: Array = []
	for i in TAVERN_ROSTER:
		var role := _role(i)
		var nm := ""
		for salt in 32:
			var gn: Array = GIVEN[_trait(i, 1 + salt * 4) % GIVEN.size()]
			var sur: String = EPITHET[_trait(i, 2 + salt * 4) % EPITHET.size()]
			var ti: Array = TITLES[role][_trait(i, 3 + salt * 4) % 4]
			var title: String = ti[0]
			if title != "" and ti[1] != "a" and gn[1] != "a" and ti[1] != gn[1]:
				title = ""
			if title != "" and title.length() + gn[0].length() + sur.length() + 2 > NAME_SHOWN:
				title = ""
			nm = ("%s %s %s" % [title, gn[0], sur]) if title != "" else ("%s %s" % [gn[0], sur])
			if not nm in names:
				break
		names.append(nm)
	_names_key = key
	_names_cache = names
	return names


## How much this class shifts a hire's health, attack and defence, in percent,
## on top of the archetype tilt -- so two Vanguards are not the same Vanguard.
static func class_variation(class_id: int) -> Array:
	var seed: Dictionary = ClassesData.CLASSES[class_id]
	var out := [0, 0, 0]
	var groups := [HP_ATTRS, ATK_ATTRS, DEF_ATTRS]
	for g in 3:
		var edge := 0
		for ov in seed.attrs:
			if ov[0] in groups[g]:
				edge += int(ov[1]) - 3
		out[g] = clampi(edge * 2, -15, 15)
	return out


## What the driver is worth in a fight -- the thing a hire has to match. One
## number, not a copy of their kit: the archetype decides what shape it takes.
static func _yardstick() -> Array:
	var ref := Game.hero
	var melee := ref.base_atk + ref.weapon_bonus + ref.weapon_plus * C.UPGRADE_STEP + ref.set_bonus_atk
	var magic := ref.known_spells.size() * (100 + ref.spell_power_pct) / 200
	var ranged := ref.ranged_bonus * (100 + ref.ranged_dmg_bonus_pct) / 200 if ref.ranged_type != C.Ranged.NONE else 0
	var best := maxi(melee, maxi(magic, ranged))
	var power := best + (melee + magic + ranged - best) / 4
	var def := maxi(0, ref.base_def + ref.armor_bonus + ref.armor_plus * C.UPGRADE_STEP + ref.set_bonus_def)
	return [maxi(ref.maxhp, 20), maxi(power, 1), def]


static func candidate(idx: int) -> Hero:
	var names := _names()
	var role := _role(idx)
	var list := _classes_of(role)
	var class_id: int = list[_trait(idx, 6) % list.size()]
	var c := Hero.make(class_id, names[idx])
	c.is_hire = true
	c.roster_idx = idx
	c.personality = _trait(idx, 4) % 3
	var lv := maxi(Game.hero.level, 1)
	c.level = lv
	var h := _trait(idx, 3)
	var y := _yardstick()
	c.maxhp = y[0] * (70 + (h >> 20) % 26) / 100
	c.base_atk = y[1] * (70 + (h >> 8) % 26) / 100
	c.base_def = y[2] * (70 + (h >> 24) % 31) / 100
	var t: Array = ARCH_TILT[c.archetype]
	c.maxhp = c.maxhp * (100 + t[0]) / 100
	c.base_atk = c.base_atk * (100 + t[1]) / 100
	c.base_def = c.base_def * (100 + t[2]) / 100
	var cv := class_variation(class_id)
	c.maxhp = maxi(12, c.maxhp * (100 + cv[0]) / 100)
	c.base_atk = maxi(1, c.base_atk * (100 + cv[1]) / 100)
	c.base_def = maxi(0, c.base_def * (100 + cv[2]) / 100)
	c.hp = c.maxhp
	_equip(c, _trait(idx, 9))
	c.xp = 0
	c.xp_next = 20 + (lv - 1) * 15
	return c


## The assembly line's golem: built rather than hired -- no roster seat, no
## fee, and gone when you leave the floor. Sturdy and slow: a wall you can put
## in front of yourself.
static func grant_golem(floor_num: int) -> bool:
	if Game.party.size() >= MAX_COMPANIONS + 1:
		Game.msg("The frame stands up, looks at your crowd, and sits back down.")
		return false
	var c := candidate(0)
	c.name = "Assembly Golem"
	c.roster_idx = -1
	c.temporary = true
	c.known_spells = []
	c.spell_cd = []
	c.ranged_type = C.Ranged.NONE
	c.aether = 0
	c.aether_max = 0
	c.level = maxi(floor_num, 1)
	c.maxhp = 40 + floor_num * 12
	c.hp = c.maxhp
	c.base_atk = 8 + floor_num * 2
	c.base_def = 10 + floor_num * 3
	c.weapon_bonus = 0
	c.armor_bonus = 0
	c.personality = BOLD
	c.archetype = 0
	c.look = 4 * 2       # the artificer's frame
	c.x = Game.hero.x
	c.y = Game.hero.y
	for d in C.DIRS8 + [Vector2i(2, 0), Vector2i(-2, 0), Vector2i(0, 2), Vector2i(0, -2)]:
		var p: Vector2i = Game.hero.pos() + d
		if Game.map.walkable_monster(p.x, p.y) and not Game.map.monster_at(p.x, p.y) and not body_at(p.x, p.y):
			c.x = p.x
			c.y = p.y
			break
	Game.party.append(c)
	Game.good("The frame shudders, finds its feet, and falls in beside you.")
	return true


## The live party member for roster seat `idx`, if they are out with you.
static func by_roster(idx: int) -> Hero:
	for h in Game.party:
		if h.is_hire and h.roster_idx == idx:
			return h
	return null


static func is_hired(idx: int) -> bool:
	return Game.tavern_hired & (1 << idx) != 0


static func has_fallen(idx: int) -> bool:
	return Game.tavern_fallen & (1 << idx) != 0


static func can_release(idx: int) -> bool:
	return is_hired(idx) or has_fallen(idx)


## Takes candidate `idx` on. Returns a line for the Tavern to show.
static func hire(idx: int) -> String:
	if is_hired(idx):
		return "They already drink on your coin."
	var filled := count()
	if filled >= MAX_COMPANIONS:
		return "Six is a crowd already. Bury someone or travel lighter."
	var fallen := has_fallen(idx)
	var price := rehire_cost(filled) if fallen else hire_cost(filled)
	if Game.gold < price:
		return "That one wants %s gold. You have %s." % [Gfx.comma(price), Gfx.comma(Game.gold)]
	var c := candidate(idx)
	c.tier = filled
	c.x = Game.hero.x
	c.y = Game.hero.y
	Game.party.append(c)
	Game.gold -= price
	Game.tavern_hired |= 1 << idx
	Game.tavern_fallen &= ~(1 << idx)
	Game.good("%s takes your coin (%s) and shoulders a pack." % [c.name, Gfx.comma(price)])
	return "%s takes your coin and shoulders a pack.%s" % [c.name, " They do not mention the last time." if fallen else ""]


## Lets candidate `idx` go: half the fee back if they are still walking, and a
## new face takes the chair.
static func release(idx: int) -> String:
	if not can_release(idx):
		return "They don't work for you. There's nothing to end."
	var who := candidate(idx).name
	var was_out := is_hired(idx)
	var refund := 0
	if was_out:
		refund = hire_cost(maxi(0, count() - 1)) / 2
	var live := by_roster(idx)
	if live:
		if live == Game.hero:
			Game.controlled_set(Game.party[0])
		Game.party.erase(live)
	Game.tavern_hired &= ~(1 << idx)
	Game.tavern_fallen &= ~(1 << idx)
	Game.tavern_reroll[idx] = int(Game.tavern_reroll[idx]) + 1
	var line := ""
	if was_out:
		Game.gold += refund
		line = "%s hands back the pack and doesn't argue. (+%s gold)" % [who, Gfx.comma(refund)]
	else:
		line = "You strike %s off the books for good." % who
	Game.msg(line)
	return line + " %s is drinking in their place." % candidate(idx).name


## Anyone who fell gives up their place and goes back on the Tavern's books,
## where they can be taken on again for a quarter.
static func reap_fallen() -> void:
	for h in Game.party.duplicate():
		if not h.is_hire or is_up(h):
			continue
		if h.roster_idx >= 0:
			Game.tavern_hired &= ~(1 << h.roster_idx)
			Game.tavern_fallen |= 1 << h.roster_idx
		Game.party.erase(h)


# ---- their kit ---------------------------------------------------------------------
# A hired caster carries real spells, a hired shooter a real weapon -- but only
# spells that affect an enemy or themselves. A hire that walls off your
# corridor or blinks somewhere random is a hazard you paid for.
static func _fit(effect: int) -> bool:
	return effect in [C.Effect.DAMAGE, C.Effect.DAMAGE_NOVA, C.Effect.LIFE_DRAIN, C.Effect.DOT_BURN,
		C.Effect.STUN, C.Effect.SLOW, C.Effect.CURSE, C.Effect.BUFF_ATK, C.Effect.HEAL_SELF, C.Effect.WARD_SHIELD]


static func _deals_damage(effect: int) -> bool:
	return effect in [C.Effect.DAMAGE, C.Effect.DAMAGE_NOVA, C.Effect.LIFE_DRAIN, C.Effect.DOT_BURN]


static func _give_spells(c: Hero, h: int) -> void:
	c.known_spells = []
	c.spell_cd = []
	var top := clampi(1 + c.level / 9, 1, 5)
	var want := top
	while want >= 1 and c.known_spells.size() < SPELL_SLOTS:
		var found := -1
		var seen := 0
		for i in SpellBook.school_count(c.magic_school):
			var idx := SpellBook.school_index(c.magic_school, i)
			var s := SpellBook.get_spell(idx)
			if s.level != want or not _fit(s.effect):
				continue
			seen += 1
			if (h >> (want * 3)) % seen == 0:
				found = idx
		if found >= 0 and not found in c.known_spells:
			c.known_spells.append(found)
			c.spell_cd.append(0)
		want -= 1
	var most := 0
	for idx in c.known_spells:
		most = maxi(most, int(SpellBook.get_spell(idx).cost))
	c.aether_max = most * 3 + 6
	c.aether = c.aether_max


static func _give_ranged(c: Hero, h: int, thrown_only: bool) -> void:
	var t := 2 if c.level >= 30 else (1 if c.level >= 12 else 0)
	var best := -1
	var seen := 0
	for i in ItemsData.RANGED.size():
		var r: Dictionary = ItemsData.RANGED[i]
		var thrown: bool = r.type == C.Ranged.THROWN or r.type == C.Ranged.GRENADE
		if thrown != thrown_only or i % 3 != t:
			continue
		seen += 1
		if (h >> 9) % seen == 0:
			best = i
	Rules.equip_ranged(c, ItemsData.RANGED[maxi(best, 0)])


static func _equip(c: Hero, h: int) -> void:
	var own := c.magic_school
	c.known_spells = []
	c.spell_cd = []
	c.ranged_type = C.Ranged.NONE
	match c.archetype:
		3:   # Arcanist: their own school first, then the rest
			for n in 6:
				c.magic_school = own if n == 0 else (h + n) % 6
				_give_spells(c, (h + n * 7919) & M32)
				if not c.known_spells.is_empty():
					break
			if c.known_spells.is_empty():
				c.magic_school = own
				_give_ranged(c, h, true)
		2:
			_give_ranged(c, h, false)
		4:
			_give_ranged(c, h, true)


static func reach(c: Hero) -> int:
	if not c.known_spells.is_empty():
		return 5
	if c.ranged_type != C.Ranged.NONE:
		return 3 if c.ranged_type == C.Ranged.THROWN or c.ranged_type == C.Ranged.GRENADE else 6
	return 1


static func _has_kit(c: Hero) -> bool:
	return not c.known_spells.is_empty() or c.ranged_type != C.Ranged.NONE


static func weapon_line(c: Hero) -> String:
	if c.ranged_type != C.Ranged.NONE:
		return "Sidearm: " + c.ranged_name
	if not c.known_spells.is_empty():
		var names: Array = c.known_spells.map(func(i): return SpellBook.get_spell(i).name)
		return "%s magic, %d aether: %s" % [C.SCHOOL_NAMES[c.magic_school], c.aether_max, ", ".join(names)]
	return "Fights with a %s." % ClassesData.CLASSES[c.class_id].weapon


# ---- in the dungeon -------------------------------------------------------------
## Drops everybody the stairs did not already move in a ring around the arrival.
static func place() -> void:
	var m := Game.map
	var a := Game.hero.pos()
	for c in Game.party:
		if c == Game.hero or not is_up(c):
			continue
		c.last_x = -1
		c.last_y = -1
		c.scout_x = -1
		c.scout_y = -1
		c.scout_turns = 0
		c.x = a.x
		c.y = a.y
		var done := false
		for r in range(1, 7):
			for dy in range(-r, r + 1):
				for dx in range(-r, r + 1):
					if done:
						break
					var x := a.x + dx
					var y := a.y + dy
					if not m.walkable_monster(x, y) or m.monster_at(x, y) or body_at(x, y):
						continue
					if Game.depth > 0 and m.t(x, y) in [C.Tile.STAIRS_DOWN, C.Tile.STAIRS_UP]:
						continue
					c.x = x
					c.y = y
					done = true
			if done:
				break


## Whatever they can see, you can see.
static func reveal() -> void:
	if Game.depth == 0:
		return
	for c in others():
		Game.map.compute_fov(c.x, c.y, COMPANION_FOV, false)


static func _say(c: Hero, text: String, col := Color8(200, 205, 225)) -> void:
	# Five hires narrating every swing would bury your own log; you hear about
	# what happens where you can see it.
	if Game.map.is_visible(c.x, c.y):
		Game.msg(text, col)


static func _shot_color(c: Hero) -> Color:
	match c.archetype:
		2: return Color8(255, 210, 120)
		3: return C.SCHOOL_COLORS[c.magic_school]
		4: return Color8(255, 160, 64)
	return Color.WHITE


static func _hurt(c: Hero, mo: Monster, dmg: int) -> bool:
	if not mo.alive:
		return false
	Rules.monster_take_damage(mo, dmg, c)
	if not mo.alive:
		c.kills += 1
		_say(c, "%s %s the %s." % [c.name, KILL_VERBS[c.archetype], mo.name], color_of(c))
		_maybe_find_gear(c, mo)
	return not mo.alive


static func _blast(c: Hero, at: Vector2i, radius: int, dmg: int, stun: int) -> void:
	for mo in Game.map.monsters.duplicate():
		if not mo.alive:
			continue
		if (mo.x - at.x) * (mo.x - at.x) + (mo.y - at.y) * (mo.y - at.y) > radius * radius:
			continue
		if stun > 0:
			mo.stun_turns += stun
		_hurt(c, mo, dmg)


# They earn their own kit off their own kills; it is theirs the moment they
# have it, and it never competes with the player for relics.
static func _maybe_find_gear(c: Hero, mo: Monster) -> void:
	if c.gear.size() >= GEAR_SLOTS:
		return
	var chance := 2 + Game.depth / 12
	if mo.is_elite:
		chance *= 3
	if mo.is_boss:
		chance *= 8
	if rnd(100) >= mini(chance, 60):
		return
	var g := {"atk": c.base_atk / 8 + 1, "def": c.base_def / 8 + 1, "hp": c.maxhp / 8 + 2}
	if mo.is_boss:
		for k in ["atk", "def", "hp"]:
			g[k] *= 2
	g.name = "%s %s" % [GEAR_PREFIX[rnd(GEAR_PREFIX.size())], GEAR_NOUN[c.archetype][rnd(3)]]
	c.gear.append(g)
	c.base_atk += g.atk
	c.base_def += g.def
	c.maxhp += g.hp
	c.hp += g.hp
	Game.good("%s digs a %s out of the mess and keeps it." % [c.name, g.name])


# ---- what they pick up --------------------------------------------------------------
# Gold and relics go to the player; healing draughts they keep and drink.
static func _loot_here(c: Hero) -> void:
	var m := Game.map
	for it in m.items_at(c.x, c.y):
		m.items.erase(it)
		match it.kind:
			"gold":
				var g := Game.gain_gold(int(it.amount))
				_say(c, "%s finds %d gold and hands it over." % [c.name, g], Color8(255, 220, 96))
				Game.emit_fx({"type": "pickup", "at": c.pos(), "text": "+%dG" % g, "color": Color8(255, 220, 96)})
			"key":
				Game.keys += 1
				Game.good("%s tosses you a brass key." % c.name)
			"quest":
				Game.quest.found = true
				Game.good("%s turns up %s and passes it to you." % [c.name, it.name])
			_:
				var t := ItemsData.consumable_by_name(it.name)
				if int(t.get("heal", 0)) > 0 and c.potions.size() < POTION_SLOTS:
					c.potions.append({"name": it.name, "heal": int(t.heal)})
					_say(c, "%s pockets a %s." % [c.name, it.name])
				elif Game.give(it.name):
					_say(c, "%s brings you a %s." % [c.name, it.name])
	var f := m.feature_at(c.x, c.y)
	if not f.is_empty() and f.type == C.Feature.RELIC and not f.get("used", false):
		f.used = true
		Game.good("%s prises something out of the wall and brings it to you." % c.name)
		Rules.apply_set_gear(int(f.relic_set), int(f.relic_kind))


static func _loot_target(c: Hero) -> Vector2i:
	var m := Game.map
	var best := LOOT_RANGE * LOOT_RANGE + 1
	var out := Vector2i(-1, -1)
	for it in m.items:
		var d: int = (it.x - c.x) * (it.x - c.x) + (it.y - c.y) * (it.y - c.y)
		if d == 0 or d >= best or not m.is_seen(it.x, it.y):
			continue
		best = d
		out = Vector2i(it.x, it.y)
	for f in m.features:
		if f.type != C.Feature.RELIC or f.get("used", false) or not m.is_seen(f.x, f.y):
			continue
		var d: int = (f.x - c.x) * (f.x - c.x) + (f.y - c.y) * (f.y - c.y)
		if d == 0 or d >= best:
			continue
		best = d
		out = Vector2i(f.x, f.y)
	return out


static func _maybe_drink(c: Hero) -> bool:
	if c.potions.is_empty() or hp_pct(c) > 45:
		return false
	var pot: Dictionary = c.potions.pop_back()
	c.hp = mini(max_hp(c), c.hp + int(pot.heal))
	_say(c, "%s drinks a %s (+%d)." % [c.name, pot.name, pot.heal], Gfx.GREEN)
	Game.emit_fx({"type": "heal", "who": c, "amount": int(pot.heal)})
	return true


# ---- moving -----------------------------------------------------------------------
static func _free(c: Hero, x: int, y: int) -> bool:
	var m := Game.map
	if x == c.x and y == c.y:
		return false
	if not m.walkable_monster(x, y):
		return false
	# The stairs are the player's decision.
	if m.t(x, y) in [C.Tile.STAIRS_DOWN, C.Tile.STAIRS_UP, C.Tile.PORTAL, C.Tile.LEVER, C.Tile.CURRENT, C.Tile.BELT]:
		return false
	if Game.hero.x == x and Game.hero.y == y:
		return false
	if m.monster_at(x, y):
		return false
	return true


## One actor per tile: an ally in the way is somebody to squeeze past.
static func displace_into(h: Hero, x: int, y: int) -> bool:
	for o in Game.party:
		if o == h or not is_up(o) or o.x != x or o.y != y:
			continue
		var from := h.pos()
		o.x = h.x
		o.y = h.y
		h.x = x
		h.y = y
		o.last_x = -1
		o.last_y = -1
		Game.emit_fx({"type": "move", "who": o, "from": Vector2i(x, y)})
		Game.emit_fx({"type": "move", "who": h, "from": from})
		return true
	return false


static func _move_to(c: Hero, x: int, y: int) -> void:
	Rules._face(c, x - c.x, y - c.y)
	if not displace_into(c, x, y):
		var from := c.pos()
		c.last_x = c.x
		c.last_y = c.y
		c.x = x
		c.y = y
		Game.emit_fx({"type": "move", "who": c, "from": from})
	_loot_here(c)


static var _prev := PackedInt32Array()


## The first step of a shortest walk over seen ground from `c` to `goal`.
static func _path_step(c: Hero, goal: Vector2i) -> Vector2i:
	var m := Game.map
	if _prev.size() != m.w * m.h:
		_prev.resize(m.w * m.h)
	_prev.fill(-2)
	var start := c.y * m.w + c.x
	var target := goal.y * m.w + goal.x
	_prev[start] = -1
	var q := PackedInt32Array([start])
	var head := 0
	while head < q.size() and head < 1500:
		var cur := q[head]
		head += 1
		if cur == target:
			while _prev[cur] != start and _prev[cur] >= 0:
				cur = _prev[cur]
			return Vector2i(cur % m.w - c.x, cur / m.w - c.y)
		var cx := cur % m.w
		var cy := cur / m.w
		for d in C.DIRS8:
			var nx: int = cx + d.x
			var ny: int = cy + d.y
			if nx < 0 or ny < 0 or nx >= m.w or ny >= m.h:
				continue
			var ni := ny * m.w + nx
			if _prev[ni] != -2:
				continue
			if ni != target and (m.seen[ni] == 0 or not m.walkable_monster(nx, ny)
					or m.tiles[ni] in [C.Tile.STAIRS_DOWN, C.Tile.STAIRS_UP, C.Tile.PORTAL]):
				continue
			_prev[ni] = cur
			q.append(ni)
	return Vector2i.ZERO


## The nearest seen, walkable square next to unseen ground.
static func _frontier(c: Hero) -> Vector2i:
	var m := Game.map
	if _prev.size() != m.w * m.h:
		_prev.resize(m.w * m.h)
	_prev.fill(-2)
	var start := c.y * m.w + c.x
	_prev[start] = -1
	var q := PackedInt32Array([start])
	var head := 0
	while head < q.size() and head < 3000:
		var cur := q[head]
		head += 1
		var cx := cur % m.w
		var cy := cur / m.w
		if cur != start:
			for d in C.DIRS8:
				if m.inb(cx + d.x, cy + d.y) and m.seen[(cy + d.y) * m.w + cx + d.x] == 0:
					return Vector2i(cx, cy)
		for d in C.DIRS8:
			var nx: int = cx + d.x
			var ny: int = cy + d.y
			if nx < 0 or ny < 0 or nx >= m.w or ny >= m.h:
				continue
			var ni := ny * m.w + nx
			if _prev[ni] != -2 or m.seen[ni] == 0 or not m.walkable_monster(nx, ny):
				continue
			if m.tiles[ni] in [C.Tile.STAIRS_DOWN, C.Tile.STAIRS_UP, C.Tile.PORTAL]:
				continue
			_prev[ni] = cur
			q.append(ni)
	return Vector2i(-1, -1)


## One step toward (tx,ty) by a real path, falling back to the greedy step;
## the square just left is tried last, which is what stops the dance.
static func _step_toward(c: Hero, tx: int, ty: int) -> bool:
	var cand: Array = []
	var p := _path_step(c, Vector2i(tx, ty))
	if p != Vector2i.ZERO:
		cand.append(p)
	var gx := signi(tx - c.x)
	var gy := signi(ty - c.y)
	for f in [Vector2i(gx, gy), Vector2i(gx, 0), Vector2i(0, gy)]:
		if f != Vector2i.ZERO:
			cand.append(f)
	for pass_n in 2:
		for d in cand:
			var nx: int = c.x + d.x
			var ny: int = c.y + d.y
			if not _free(c, nx, ny):
				continue
			if pass_n == 0 and nx == c.last_x and ny == c.last_y:
				continue
			_move_to(c, nx, ny)
			return true
	return false


## Away from the threat and, as a tiebreak, toward whoever is being played.
static func _step_away(c: Hero, tx: int, ty: int) -> bool:
	var here := (c.x - tx) * (c.x - tx) + (c.y - ty) * (c.y - ty)
	var best := -1
	var to := c.pos()
	var hx := Game.hero.x
	var hy := Game.hero.y
	for d in C.DIRS8:
		var nx: int = c.x + d.x
		var ny: int = c.y + d.y
		if not _free(c, nx, ny):
			continue
		var away := (nx - tx) * (nx - tx) + (ny - ty) * (ny - ty)
		if away <= here:
			continue
		var score := away * 128 - ((nx - hx) * (nx - hx) + (ny - hy) * (ny - hy))
		if score > best:
			best = score
			to = Vector2i(nx, ny)
	if to == c.pos():
		return false
	_move_to(c, to.x, to.y)
	return true


static func _nearest(c: Hero, radius: int) -> Monster:
	var best: Monster = null
	var best_d := radius * radius + 1
	for mo in Game.map.monsters:
		if not mo.alive:
			continue
		var d: int = (mo.x - c.x) * (mo.x - c.x) + (mo.y - c.y) * (mo.y - c.y)
		if d < best_d:
			best_d = d
			best = mo
	return best


## The nearest monster they can actually hit from here, in sight.
static func _in_reach(c: Hero) -> Monster:
	var m := Game.map
	var r := reach(c)
	var best: Monster = null
	var best_d := r * r + 1
	for mo in m.monsters:
		if not mo.alive:
			continue
		var d: int = (mo.x - c.x) * (mo.x - c.x) + (mo.y - c.y) * (mo.y - c.y)
		if d > r * r or d >= best_d:
			continue
		if d > 2 and not m.line_of_sight(c.x, c.y, mo.x, mo.y):
			continue
		best_d = d
		best = mo
	return best


# ---- fighting ---------------------------------------------------------------------
## Which spell slot to cast right now, or -1. Every effect states its own
## condition; anything unlisted is not useful rather than silently useful.
static func pick_spell(c: Hero, target: Monster, can_reach: bool) -> int:
	var best := -1
	var best_w := -1
	for i in c.known_spells.size():
		if int(c.spell_cd[i]) > 0:
			continue
		var s := SpellBook.get_spell(c.known_spells[i])
		if s.is_empty() or int(s.cost) > c.aether:
			continue
		var useful := false
		match int(s.effect):
			C.Effect.DAMAGE, C.Effect.DAMAGE_NOVA, C.Effect.LIFE_DRAIN, C.Effect.DOT_BURN:
				useful = can_reach
			C.Effect.HEAL_SELF:
				useful = hp_pct(c) < 60
			C.Effect.BUFF_ATK:
				useful = c.atk_buff_turns == 0 and target != null
			C.Effect.WARD_SHIELD:
				useful = c.def_buff_turns == 0 and target != null
			C.Effect.STUN:
				useful = can_reach and target.stun_turns == 0
			C.Effect.SLOW:
				useful = can_reach and target.slow_turns == 0
			C.Effect.CURSE:
				useful = can_reach and target.jinx_turns == 0
		if not useful:
			continue
		var w: int = int(s.level) * 100 + int(s.magnitude)
		if s.effect == C.Effect.HEAL_SELF:
			w += 2000
		elif _deals_damage(s.effect):
			w += 1000
		if w > best_w:
			best_w = w
			best = i
	return best


static func _cast(c: Hero, target: Monster) -> bool:
	if c.known_spells.is_empty():
		return false
	var m := Game.map
	var r := reach(c)
	if not Districts.allows(c.x, c.y, C.Act.ARCANE):
		return false
	var can := target != null and (target.x - c.x) * (target.x - c.x) + (target.y - c.y) * (target.y - c.y) <= r * r \
		and m.line_of_sight(c.x, c.y, target.x, target.y)
	var slot := pick_spell(c, target, can)
	if slot < 0:
		return false
	var s := SpellBook.get_spell(c.known_spells[slot])
	c.aether -= int(s.cost)
	c.spell_cd[slot] = int(s.cooldown)
	var power: int = int(s.magnitude) + (c.base_atk + atk_bonus()) / 3
	var col: Color = C.SCHOOL_COLORS[c.magic_school]
	Game.emit_fx({"type": "cast", "who": c, "color": col})
	if target != null and _deals_damage(s.effect) or s.effect in [C.Effect.STUN, C.Effect.SLOW, C.Effect.CURSE]:
		Game.emit_fx({"type": "bolt", "from": c.pos(), "to": target.pos(), "color": col})
	_say(c, "%s casts %s." % [c.name, s.name], col.lightened(0.3))
	match int(s.effect):
		C.Effect.DAMAGE:
			_hurt(c, target, power)
		C.Effect.DAMAGE_NOVA:
			Game.emit_fx({"type": "burst", "at": target.pos(), "radius": 2, "color": col})
			_blast(c, target.pos(), 2, power, 0)
		C.Effect.LIFE_DRAIN:
			_hurt(c, target, power)
			c.hp = mini(max_hp(c), c.hp + power / 2)
		C.Effect.DOT_BURN:
			target.jinx_pen += int(s.magnitude) / 3 + 1
			target.jinx_turns = int(s.duration)
			_hurt(c, target, power)
		C.Effect.STUN:
			target.stun_turns += int(s.duration)
		C.Effect.SLOW:
			target.slow_turns += int(s.duration)
		C.Effect.CURSE:
			target.jinx_pen += int(s.magnitude)
			target.jinx_turns = int(s.duration)
		C.Effect.HEAL_SELF:
			c.hp = mini(max_hp(c), c.hp + power)
			Game.emit_fx({"type": "heal", "who": c, "amount": power})
		C.Effect.BUFF_ATK:
			c.atk_buff = int(s.magnitude)
			c.atk_buff_turns = int(s.duration)
		C.Effect.WARD_SHIELD:
			c.def_buff = int(s.magnitude)
			c.def_buff_turns = int(s.duration)
	return true


## The weapon's own damage and cooldown; no ammo -- that is a resource the
## player manages, and nobody could manage a hire's.
static func _fire(c: Hero, target: Monster) -> bool:
	if c.ranged_type == C.Ranged.NONE or target == null or c.ranged_cooldown > 0:
		return false
	if not Districts.allows(c.x, c.y, C.Act.RANGED):
		return false
	var r := reach(c)
	if (target.x - c.x) * (target.x - c.x) + (target.y - c.y) * (target.y - c.y) > r * r:
		return false
	if not Game.map.line_of_sight(c.x, c.y, target.x, target.y):
		return false
	c.ranged_cooldown = c.ranged_cooldown_max
	var dmg := c.ranged_bonus + (c.base_atk + atk_bonus()) / 3
	Rules._face(c, target.x - c.x, target.y - c.y)
	Game.emit_fx({"type": "attack", "who": c, "target": target})
	Game.emit_fx({"type": "shot", "from": c.pos(), "to": target.pos(), "color": C.RANGED_COLORS[c.ranged_type]})
	_say(c, "%s looses a shot from the %s." % [c.name, c.ranged_name])
	match c.ranged_type:
		C.Ranged.GRENADE:
			Game.emit_fx({"type": "burst", "at": target.pos(), "radius": 2, "color": C.RANGED_COLORS[c.ranged_type]})
			_blast(c, target.pos(), 2, dmg * 2 / 3, 0)
		C.Ranged.BLOWGUN:
			target.slow_turns += 3
			_hurt(c, target, dmg)
		_:
			_hurt(c, target, dmg)
	return true


## A swing: the same roll anybody's blow uses, plus the smith's party bonus.
## A hire with nothing to shoot can still throw something at range, every
## other turn; anybody carrying a weapon or a spellbook reaches through that.
static func _strike(c: Hero, mo: Monster) -> bool:
	if not Districts.allows(c.x, c.y, C.Act.MELEE):
		return false
	var at_range := (mo.x - c.x) * (mo.x - c.x) + (mo.y - c.y) * (mo.y - c.y) > 2
	if at_range:
		if _has_kit(c) or c.shot_cd > 0:
			return false
		c.shot_cd = SHOT_COOLDOWN
	Rules._face(c, mo.x - c.x, mo.y - c.y)
	Game.emit_fx({"type": "attack", "who": c, "target": mo})
	if at_range:
		Game.emit_fx({"type": "shot", "from": c.pos(), "to": mo.pos(), "color": _shot_color(c)})
	var dmg := Rules.damage_after_defence(Districts.eff_atk(c), mo.def, rnd(4) - 1)
	var crit := c.effective_stat(C.Acc.CRIT)
	if crit > 0 and rnd(100) < crit:
		dmg *= 2
	_hurt(c, mo, dmg + atk_bonus())
	return true


## The role's signature move, if it is ready and would help.
static func _ability(c: Hero, target: Monster) -> bool:
	if c.ability_cd > 0:
		return false
	var mx := max_hp(c)
	match c.archetype:
		5:   # Survivor: only when it would actually save them
			if hp_pct(c) > 45:
				return false
			c.hp = mini(mx, c.hp + mx / 3)
			Game.emit_fx({"type": "heal", "who": c, "amount": mx / 3})
			_say(c, "%s gets back up -- Second Wind." % c.name, Gfx.GREEN)
		0:   # Vanguard
			if target == null or c.guard_turns > 0:
				return false
			c.guard_turns = 6
			Game.emit_fx({"type": "burst", "at": c.pos(), "radius": 1, "color": Color8(200, 220, 255)})
			_say(c, "%s plants itself and digs in -- Bulwark." % c.name)
		6:   # Envoy: for the party
			var hurt := 0
			for o in Game.party:
				if is_up(o) and o != c and hp_pct(o) < 70:
					hurt += 1
			if hurt < 1:
				return false
			for o in Game.party:
				if is_up(o):
					o.hp = mini(max_hp(o), o.hp + max_hp(o) / 6)
			var led := Game.hero
			led.atk_buff += 4
			led.atk_buff_turns = maxi(led.atk_buff_turns, 8)
			Game.emit_fx({"type": "burst", "at": c.pos(), "radius": 3, "color": Gfx.GOLD})
			Game.good("%s calls the party back together -- Rally." % c.name)
		1:   # Skirmisher
			if target == null or (target.x - c.x) * (target.x - c.x) + (target.y - c.y) * (target.y - c.y) > 2:
				return false
			_say(c, "%s goes in twice -- Flurry." % c.name)
			_strike(c, target)
			if target.alive:
				_strike(c, target)
		2:   # Marksman: straight through armour
			if target == null:
				return false
			var atk := c.base_atk + atk_bonus()
			var dmg := atk + atk / 2
			Game.emit_fx({"type": "shot", "from": c.pos(), "to": target.pos(), "color": Color8(255, 120, 80)})
			_say(c, "%s takes its time -- Called Shot, %d through the plate." % [c.name, dmg])
			_hurt(c, target, dmg)
		3:   # Arcanist
			if target == null:
				return false
			_say(c, "%s tears the air open -- Detonation." % c.name, C.SCHOOL_COLORS[c.magic_school])
			Game.emit_fx({"type": "cast", "who": c, "color": C.SCHOOL_COLORS[c.magic_school]})
			Game.emit_fx({"type": "burst", "at": target.pos(), "radius": 2, "color": C.SCHOOL_COLORS[c.magic_school]})
			_blast(c, target.pos(), 2, c.base_atk + atk_bonus(), 0)
		4:   # Artificer
			if target == null:
				return false
			_say(c, "%s lobs a charge -- it scatters." % c.name)
			Game.emit_fx({"type": "shot", "from": c.pos(), "to": target.pos(), "color": Color8(255, 160, 64)})
			Game.emit_fx({"type": "burst", "at": target.pos(), "radius": 2, "color": Color8(255, 160, 64)})
			_blast(c, target.pos(), 2, (c.base_atk + atk_bonus()) * 2 / 3, 2)
		_:
			return false
	c.ability_cd = ABILITY_CD
	return true


# ---- time passing -------------------------------------------------------------------
## Time passing for one body the player is not driving. The driven body's
## clock runs in Rules.end_turn, which also tells you about it.
static func tick(h: Hero) -> void:
	if not is_up(h):
		return
	if h.shot_cd > 0: h.shot_cd -= 1
	if h.ability_cd > 0: h.ability_cd -= 1
	if h.guard_turns > 0: h.guard_turns -= 1
	if h.ranged_cooldown > 0: h.ranged_cooldown -= 1
	if h.reflect_turns > 0: h.reflect_turns -= 1
	for i in h.spell_cd.size():
		if int(h.spell_cd[i]) > 0:
			h.spell_cd[i] = int(h.spell_cd[i]) - 1
	if h.aether < h.aether_max:
		h.aether += 1
	if Game.turns % C.RANGED_AMMO_REGEN_TURNS == 0 and h.ranged_ammo < h.ranged_ammo_max:
		h.ranged_ammo += 1
	if h.atk_buff_turns > 0:
		h.atk_buff_turns -= 1
		if h.atk_buff_turns == 0:
			h.atk_buff = 0
	if h.def_buff_turns > 0:
		h.def_buff_turns -= 1
		if h.def_buff_turns == 0:
			h.def_buff = 0
	if h.evasion_buff_turns > 0:
		h.evasion_buff_turns -= 1
		if h.evasion_buff_turns == 0:
			h.evasion_buff = 0
	h.stance_atk_pct = 0
	h.stance_def_pct = 0
	if Game.depth > 0:
		match Game.map.t(h.x, h.y):
			C.Tile.PRISM_RED:
				h.stance_atk_pct = C.PRISM_SWING
				h.stance_def_pct = -C.PRISM_SWING
			C.Tile.PRISM_BLUE:
				h.stance_atk_pct = -C.PRISM_SWING
				h.stance_def_pct = C.PRISM_SWING
			C.Tile.PRISM_GREEN:
				h.hp = mini(max_hp(h), h.hp + C.PRISM_REGEN)
	var regen := h.effective_stat(C.Acc.REGEN)
	if regen > 0 and h.poison_turns == 0:
		h.hp = mini(max_hp(h), h.hp + regen)
	if h.poison_turns > 0:
		h.poison_turns -= 1
		Rules.body_take_damage(h, h.poison_dmg, "poison")
	if h.stun_turns > 0: h.stun_turns -= 1
	if h.held_turns > 0: h.held_turns -= 1


## One turn's decision for every body the player is not driving. Underground
## only: in town they wait at the temple mouth.
static func take_turn() -> void:
	if Game.depth == 0:
		return
	for c in Game.party.duplicate():
		if c == Game.hero or not is_up(c) or Game.won or not Game.hero.alive:
			continue
		tick(c)
		if not is_up(c) or c.stun_turns > 0 or c.held_turns > 0:
			continue
		if c.slow_turns > 0:
			c.slow_turns -= 1
			if rnd(2) == 0:
				continue
		_think(c)
	reap_fallen()


static func _think(c: Hero) -> void:
	# 0. Look after yourself first.
	if _maybe_drink(c) or _ability(c, null):
		return
	var chase := 26
	var sight := 8
	var retreat := 20
	var engage := 30
	match c.personality:
		BOLD:
			chase = 40
			sight = 12
			retreat = 0
			engage = 0
		CAUTIOUS:
			chase = 16
			sight = 5
			retreat = 40
			engage = 55
	var health := hp_pct(c)
	# 1. Hurt badly enough to want out. Bold heroes never are -- which is why
	#    Bold heroes are the ones who die.
	if health <= retreat:
		var threat := _nearest(c, sight)
		if threat:
			var shot := _in_reach(c)
			if shot and reach(c) > 1:
				if _cast(c, shot) or _fire(c, shot) or (c.shot_cd == 0 and _strike(c, shot)):
					return
			if _step_away(c, threat.x, threat.y):
				return
			if shot and _strike(c, shot):
				return
	# 2. Something they can hit from where they stand.
	var target := _in_reach(c)
	if _ability(c, target) or _cast(c, target) or _fire(c, target):
		return
	if target and _strike(c, target):
		return
	# 3. Something worth walking to.
	if health >= engage:
		var seen := _nearest(c, mini(sight, chase))
		if seen:
			var r := reach(c)
			var in_reach := (seen.x - c.x) * (seen.x - c.x) + (seen.y - c.y) * (seen.y - c.y) <= r * r
			var can_see := Game.map.line_of_sight(c.x, c.y, seen.x, seen.y)
			if (not in_reach or not can_see or c.shot_cd > 0) and _step_toward(c, seen.x, seen.y):
				return
	# 4. Loot in sight.
	var loot := _loot_target(c)
	if loot.x >= 0 and _step_toward(c, loot.x, loot.y):
		return
	# 5. Scout -- but commit to a destination, or they orbit a junction.
	if c.scout_x >= 0:
		c.scout_turns += 1
		if (c.x == c.scout_x and c.y == c.scout_y) or c.scout_turns > SCOUT_PATIENCE:
			c.scout_x = -1
			c.scout_turns = 0
	if c.scout_x < 0:
		var f := _frontier(c)
		if f.x >= 0:
			c.scout_x = f.x
			c.scout_y = f.y
			c.scout_turns = 0
	if c.scout_x >= 0:
		_step_toward(c, c.scout_x, c.scout_y)
	# 6. Nothing to scout and nothing to fight: hold position.


# ---- the party will not leave you -----------------------------------------------
## You go down with a charm in the pack and somebody still standing: the
## nearest one reaches you, cracks it, and you come round on one hit point on
## your way home. Returns true if it happened.
static func attempt_rescue() -> bool:
	if Game.depth <= 0 or Game.difficulty == C.Difficulty.HARDCORE or Game.recall_charms() <= 0:
		return false
	var fallen := Game.hero
	var best: Hero = null
	var best_d := 1 << 30
	for c in Game.party:
		if c == fallen or not is_up(c):
			continue
		var d: int = (c.x - fallen.x) * (c.x - fallen.x) + (c.y - fallen.y) * (c.y - fallen.y)
		if d < best_d:
			best_d = d
			best = c
	if best == null or not Game.take("Recall Charm"):
		return false
	Quests.note_recall()
	for d in C.DIRS8:
		var nx: int = fallen.x + d.x
		var ny: int = fallen.y + d.y
		if Game.map.walkable_player(nx, ny) and not Game.map.monster_at(nx, ny) and not body_at(nx, ny):
			best.x = nx
			best.y = ny
			break
	fallen.hp = 1
	fallen.alive = true
	Game.recall_countdown = 1
	Game.warn("You go down. %s reaches you first." % best.name)
	Game.good("A charm breaks over you -- \"Not here. Not today.\"")
	return true


## The body you were driving fell but the party has not: the role moves on.
static func pass_the_torch() -> bool:
	for h in Game.party:
		if h != Game.hero and is_up(h):
			Game.controlled_set(h)
			Game.warn("%s takes up the lantern. The run is theirs now." % h.name)
			return true
	return false


## Take over the next body standing. Instant and free.
static func switch_next() -> bool:
	var n := Game.party.size()
	var from := Game.party.find(Game.hero)
	for step in range(1, n):
		var h: Hero = Game.party[(from + step) % n]
		if is_up(h):
			if Game.depth == 0:
				h.x = Game.hero.x
				h.y = Game.hero.y
			Game.controlled_set(h)
			return true
	return false
