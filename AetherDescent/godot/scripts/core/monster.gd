class_name Monster
extends RefCounted
## One monster on a floor, and the factory that makes them -- a port of
## src/monsters.c: the curated roster per depth tier, the generated variants
## (prefix + noun over an archetype multiplier), escalation, swarm scaling,
## elites, the four biome bosses and the Warden.

var name := ""
var glyph := "r"
var x := 0
var y := 0
var hp := 1
var maxhp := 1
var atk := 1
var def := 0
var xp_reward := 0
var gold_reward := 0
var alive := true
var aggro := false
var is_boss := false     # the Warden: killing it ends the game
var is_elite := false
var biome_boss := false
var family := 0          # depth tier 0..4
var kind := 0            # body index into ArtLayout.MON_KINDS
var jinx_pen := 0
var jinx_turns := 0
var stun_turns := 0
var slow_turns := 0
var facing_left := true
var apparition := 0      # 1: a barrow ghost, 2: your reflection
var look := -1           # a person, not a beast: drawn with this hero look (barrow ghosts, the mirror)

# ---- the pool ------------------------------------------------------------------
const GEN_PER_TIER := 148
static var _pool: Array = []      # per tier: Array of [name, glyph, hp, atk, def, xp, gold]

# Which body each glyph wears. Anything unlisted is a humanoid.
const GLYPH_KIND := {
	"r": "rat", "l": "serpent", "s": "serpent", "b": "hound", "f": "hound", "h": "hound",
	"D": "hound", "t": "hound", "A": "automaton", "I": "automaton", "M": "automaton",
	"G": "golem", "&": "golem", "x": "golem", "K": "golem", "B": "golem",
	"w": "wisp", "e": "wisp", "v": "wisp", "W": "wraith", "V": "wraith", "S": "wraith",
	"N": "wraith", "c": "wraith", "k": "wraith", "F": "horror", "R": "horror", "O": "horror",
	"T": "horror", "g": "horror", "U": "horror", "J": "wraith",
}
# Which palette row a tier uses, and the per-kind overrides that read better.
const TIER_FAMILY := ["jungle", "clockwork", "ruins", "outrider", "abyssal"]


static func _build() -> void:
	if not _pool.is_empty():
		return
	for t in 5:
		var tier: Array = []
		var cur: Array = MonstersData.CURATED[t]
		for row in cur:
			tier.append([row[0], row[1], row[3], row[4], row[5], row[6], row[7]])
		var pre: Array = MonstersData.PREFIXES[t]
		var nouns: Array = MonstersData.NOUNS[t]
		var total := pre.size() * nouns.size()
		var want := mini(GEN_PER_TIER, total)
		var base: Array = MonstersData.TIER_BASE[t]
		for gi in want:
			var combo := gi % total
			var a: Array = MonstersData.ARCHETYPES[gi % MonstersData.ARCHETYPES.size()]
			tier.append(["%s %s" % [pre[combo % pre.size()], nouns[combo / pre.size()]],
				cur[gi % cur.size()][1],
				maxi(1, int(base[0] * a[0])), maxi(1, int(base[1] * a[1])), int(base[2] * a[2]),
				int(base[3] * a[3]), int(base[4] * a[4])])
		_pool.append(tier)


static func kind_for_glyph(g: String) -> int:
	var k: String = GLYPH_KIND.get(g, "tribal")
	return ArtLayout.MON_KINDS.find(k)


static func family_row(m: Monster) -> int:
	if m.is_boss or m.biome_boss:
		return ArtLayout.MON_FAMILIES.find("boss")
	var kname: String = ArtLayout.MON_KINDS[m.kind]
	if kname == "wisp" and m.family <= 1:
		return ArtLayout.MON_FAMILIES.find("flame")
	if kname == "wraith" and m.family <= 2:
		return ArtLayout.MON_FAMILIES.find("spirit")
	if kname == "rat":
		return ArtLayout.MON_FAMILIES.find("vermin")
	return ArtLayout.MON_FAMILIES.find(TIER_FAMILY[m.family])


static func escalation_for(level: int, floor_entries: int) -> int:
	var pct := (maxi(level, 1) * C.ESCALATION_PER_LEVEL + maxi(floor_entries, 0) * C.ESCALATION_PER_ENTRY) / 10
	return mini(pct, C.ESCALATION_CAP)


static func _instantiate(row: Array, tier: int, floor_num: int, x: int, y: int, rng: RandomNumberGenerator,
		m_hp := 1.0, m_atk := 1.0, m_def := 1.0, m_reward := 1.0, prefix := "") -> Monster:
	var m := Monster.new()
	m.name = prefix + row[0]
	m.glyph = row[1]
	m.x = x
	m.y = y
	m.family = tier
	m.kind = kind_for_glyph(m.glyph)
	var hp := int(row[2]) + floor_num * 3 + rng.randi() % (floor_num / 2 + 2)
	var atk := int(row[3]) + floor_num
	var df := int(row[4]) + floor_num / 5
	var xp := int(row[5]) + floor_num / 2 + floor_num * floor_num / 60
	var gold := int(row[6]) + floor_num / 2 + floor_num * floor_num / 50 + rng.randi() % (floor_num / 3 + 2)
	m.maxhp = int(hp * m_hp)
	m.atk = int(atk * m_atk) * (100 + Game.escalation_pct) / 100
	m.def = int(df * m_def)
	if Game.is_swarm():
		m.maxhp = maxi(1, m.maxhp / C.SWARM_MONSTER_FRACTION)
		m.atk = maxi(1, m.atk * C.SWARM_ATTACK_PCT / 100)
		m.def = maxi(0, m.def / 2)
	m.hp = m.maxhp
	m.xp_reward = int(xp * m_reward)
	m.gold_reward = int(gold * m_reward)
	return m


static func for_floor(floor_num: int, x: int, y: int, rng: RandomNumberGenerator) -> Monster:
	_build()
	var t := C.tier_for_floor(floor_num)
	var tier: Array = _pool[t]
	return _instantiate(tier[rng.randi() % tier.size()], t, floor_num, x, y, rng)


static func elite_for_floor(floor_num: int, x: int, y: int, rng: RandomNumberGenerator) -> Monster:
	_build()
	var t := C.tier_for_floor(floor_num)
	var tier: Array = _pool[t]
	var m := _instantiate(tier[rng.randi() % tier.size()], t, floor_num, x, y, rng, 1.8, 1.3, 1.3, 2.5, "Elder ")
	m.is_elite = true
	return m


static func quest_name(floor_num: int, rng: RandomNumberGenerator) -> String:
	_build()
	var tier: Array = _pool[C.tier_for_floor(floor_num)]
	return tier[rng.randi() % tier.size()][0]


static func is_biome_boss_floor(f: int) -> bool:
	return f == 15 or f == 35 or f == 60 or f == 85


static func make_biome_boss(floor_num: int, x: int, y: int) -> Monster:
	var m := Monster.new()
	m.x = x
	m.y = y
	m.aggro = true
	m.is_elite = true
	m.biome_boss = true
	m.maxhp = 220 + floor_num * 8
	m.atk = 16 + floor_num * 9 / 10
	m.def = 6 + floor_num * 7 / 20
	m.family = C.tier_for_floor(floor_num)
	match floor_num:
		15:
			m.name = "Root-Drowned Matriarch"; m.xp_reward = 90; m.gold_reward = 120
			m.kind = ArtLayout.MON_KINDS.find("serpent")
		35:
			m.name = "Foreman's Engine"; m.xp_reward = 200; m.gold_reward = 260
			m.kind = ArtLayout.MON_KINDS.find("automaton")
		60:
			m.name = "Flooded Custodian"; m.xp_reward = 350; m.gold_reward = 420
			m.kind = ArtLayout.MON_KINDS.find("golem")
		_:
			m.name = "Salt-Cured Colossus"; m.xp_reward = 500; m.gold_reward = 600
			m.kind = ArtLayout.MON_KINDS.find("horror")
	m.glyph = "&"
	m.hp = m.maxhp
	return m


static func warden(x: int, y: int) -> Monster:
	var m := Monster.new()
	m.name = "Warden of the Deep Well"
	m.glyph = "&"
	m.x = x
	m.y = y
	m.maxhp = 1100
	m.hp = 1100
	m.atk = 100
	m.def = 40
	m.xp_reward = 800
	m.gold_reward = 1000
	m.aggro = true
	m.is_boss = true
	m.family = 4
	m.kind = ArtLayout.MON_KINDS.find("golem")
	return m


func pos() -> Vector2i:
	return Vector2i(x, y)


func to_dict() -> Dictionary:
	var d := {}
	for p in get_property_list():
		if p.usage & PROPERTY_USAGE_SCRIPT_VARIABLE:
			d[p.name] = get(p.name)
	return d


static func from_dict(d: Dictionary) -> Monster:
	var m := Monster.new()
	for k in d:
		if typeof(m.get(k)) == TYPE_INT:
			m.set(k, int(d[k]))
		else:
			m.set(k, d[k])
	return m
