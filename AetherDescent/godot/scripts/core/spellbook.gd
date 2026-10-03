class_name SpellBook
## The 180-spell pool: six schools x thirty curated spells, stats built from
## the level tables times the spell's archetype -- make_template() in
## src/spells.c. Casting itself lives in rules.gd with the other actions.

const LEVEL_MAGNITUDE := [0, 10, 16, 24, 34, 48]
const LEVEL_DURATION := [0, 8, 10, 14, 18, 24]
const LEVEL_CHARGE := [0, 2, 3, 4, 5, 6]
const LEVEL_COOLDOWN := [0, 2, 3, 4, 5, 7]
const LEVEL_PRICE := [0, 60, 180, 450, 1100, 2600]
const LEVEL_REACH := [0, 5, 6, 7, 8, 9]
const LEVEL_AOE := [0, 1, 2, 2, 3, 3]
# magnitude, cost, cooldown, price
const ARCHS := [[1.00, 1.00, 1.00, 1.00], [1.35, 1.25, 1.15, 1.15], [0.75, 0.75, 0.70, 0.85],
	[1.15, 1.00, 1.40, 1.05], [0.90, 0.60, 1.00, 0.90]]
const EFFECT_WORDS := ["bolt", "nova", "scorch", "attack up", "heal", "ward", "barrier", "purge",
	"blink", "reveal", "bridge", "stun", "slow", "unlock", "drain", "curse", "rot", "chain",
	"party heal", "haste", "reflect", "sense life", "mass slow"]

static var _pool: Array = []


static func _build() -> void:
	if not _pool.is_empty():
		return
	for school in 6:
		for row in SpellsData.CURATED[school]:
			var lv: int = row[1]
			var a: Array = ARCHS[row[3]]
			_pool.append({
				"name": row[0], "school": school, "level": lv, "effect": row[2],
				"magnitude": maxi(1, int(LEVEL_MAGNITUDE[lv] * a[0])),
				"duration": int(LEVEL_DURATION[lv] * a[0]),
				"cost": maxi(1, int(LEVEL_CHARGE[lv] * a[1])),
				"cooldown": maxi(1, int(LEVEL_COOLDOWN[lv] * a[2])),
				"price": int(LEVEL_PRICE[lv] * a[3]),
			})


static func count() -> int:
	_build()
	return _pool.size()


static func get_spell(idx: int) -> Dictionary:
	_build()
	return _pool[idx] if idx >= 0 and idx < _pool.size() else {}


static func school_count(_school: int) -> int:
	return 30


static func school_index(school: int, n: int) -> int:
	return school * 30 + n


static func is_offensive(effect: int) -> bool:
	return effect in [C.Effect.DAMAGE, C.Effect.DAMAGE_NOVA, C.Effect.LIFE_DRAIN, C.Effect.DOT_BURN, C.Effect.CHAIN]


static func describe(s: Dictionary) -> String:
	var e: int = s.effect
	var word: String = EFFECT_WORDS[e]
	var parts := ["Lv%d %s" % [s.level, word]]
	if e in [C.Effect.DAMAGE, C.Effect.DAMAGE_NOVA, C.Effect.LIFE_DRAIN, C.Effect.DOT_BURN, C.Effect.CHAIN,
			C.Effect.HEAL_SELF, C.Effect.PARTY_HEAL, C.Effect.BUFF_ATK, C.Effect.WARD_SHIELD, C.Effect.CURSE]:
		parts.append("power %d" % s.magnitude)
	if e in [C.Effect.BUFF_ATK, C.Effect.WARD_SHIELD, C.Effect.STUN, C.Effect.SLOW, C.Effect.CURSE,
			C.Effect.DOT_BURN, C.Effect.REFLECT, C.Effect.MASS_SLOW]:
		parts.append("%d turns" % s.duration)
	parts.append("AE %d" % s.cost)
	parts.append("cd %d" % s.cooldown)
	return "  ".join(parts)
