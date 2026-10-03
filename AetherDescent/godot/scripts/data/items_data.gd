class_name ItemsData
## Stock tables, ported row for row from src/items.c and src/gearsets.c.
## Consumable: name, price, heal, heal_pct, atk_buff, atk_turns, def_buff,
## def_turns, perm_maxhp, recall.


static func _c(n: String, price: int, heal: int, heal_pct: int, ab: int, at: int, db: int, dt: int,
		perm: int, recall: bool) -> Dictionary:
	return {"name": n, "price": price, "heal": heal, "heal_pct": heal_pct, "atk_buff": ab,
		"atk_turns": at, "def_buff": db, "def_turns": dt, "perm_maxhp": perm, "recall": recall}


static var GENERAL: Array = [
	_c("Ration Pack", 6, 8, 3, 0, 0, 0, 0, 0, false),
	_c("Traveler's Bandages", 9, 12, 5, 0, 0, 0, 0, 0, false),
	_c("Minor Healing Draught", 14, 14, 8, 0, 0, 0, 0, 0, false),
	_c("Ether Tonic (Lesser)", 15, 0, 0, 3, 15, 0, 0, 0, false),
	_c("Guard Tonic (Lesser)", 13, 0, 0, 0, 0, 3, 15, 0, false),
	_c("Field Rations, Preserved", 20, 18, 10, 0, 0, 0, 0, 0, false),
]

static var APOTHECARY: Array = [
	_c("Greater Healing Draught", 32, 25, 18, 0, 0, 0, 0, 0, false),
	_c("Superior Healing Draught", 55, 35, 30, 0, 0, 0, 0, 0, false),
	_c("Ether Tonic (Greater)", 35, 0, 0, 6, 20, 0, 0, 0, false),
	_c("Ether Tonic (Volatile)", 55, 0, 0, 10, 15, 0, 0, 0, false),
	_c("Guard Draught", 25, 0, 0, 0, 0, 5, 20, 0, false),
	_c("Guard Draught, Reinforced", 45, 0, 0, 0, 0, 9, 20, 0, false),
	_c("Elixir of Vigor", 5000, 0, 0, 0, 0, 0, 0, 5, false),
	_c("Recall Charm", 45, 0, 0, 0, 0, 0, 0, 0, true),
]

static var MERCHANT: Array = [
	_c("Minor Healing Draught", 20, 14, 8, 0, 0, 0, 0, 0, false),
	_c("Greater Healing Draught", 45, 25, 18, 0, 0, 0, 0, 0, false),
	_c("Ether Tonic (Greater)", 48, 0, 0, 6, 20, 0, 0, 0, false),
	_c("Guard Draught", 35, 0, 0, 0, 0, 5, 20, 0, false),
	_c("Recall Charm", 60, 0, 0, 0, 0, 0, 0, 0, true),
]

static var LOOT_LOW: Array = [
	_c("Ration Pack", 0, 8, 3, 0, 0, 0, 0, 0, false),
	_c("Minor Healing Draught", 0, 14, 8, 0, 0, 0, 0, 0, false),
	_c("Ether Tonic (Lesser)", 0, 0, 0, 3, 15, 0, 0, 0, false),
	_c("Guard Tonic (Lesser)", 0, 0, 0, 0, 0, 3, 15, 0, false),
]
static var LOOT_MID: Array = [
	_c("Greater Healing Draught", 0, 25, 18, 0, 0, 0, 0, 0, false),
	_c("Ether Tonic (Greater)", 0, 0, 0, 6, 20, 0, 0, 0, false),
	_c("Guard Draught", 0, 0, 0, 0, 0, 5, 20, 0, false),
	_c("Elixir of Vigor", 0, 0, 0, 0, 0, 0, 0, 5, false),
]
static var LOOT_HIGH: Array = [
	_c("Superior Healing Draught", 0, 35, 30, 0, 0, 0, 0, 0, false),
	_c("Ether Tonic (Volatile)", 0, 0, 0, 10, 15, 0, 0, 0, false),
	_c("Guard Draught, Reinforced", 0, 0, 0, 0, 0, 9, 20, 0, false),
	_c("Recall Charm", 0, 0, 0, 0, 0, 0, 0, 0, true),
]

static var WEAPONS: Array = [
	{"name": "Rusty Cutlass", "price": 15, "bonus": 2},
	{"name": "Brass Rapier", "price": 55, "bonus": 5},
	{"name": "Riveted Cleaver", "price": 110, "bonus": 8},
	{"name": "Steam-Forged Sabre", "price": 200, "bonus": 12},
	{"name": "Clockwork Estoc", "price": 350, "bonus": 16},
	{"name": "Star-iron Edge", "price": 600, "bonus": 21},
	{"name": "Ether-Etched Blade", "price": 1000, "bonus": 27},
]
static var ARMORS: Array = [
	{"name": "Worn Leathers", "price": 15, "bonus": 2},
	{"name": "Riveted Jacket", "price": 55, "bonus": 5},
	{"name": "Padded Brigandine", "price": 110, "bonus": 8},
	{"name": "Brass Plate", "price": 200, "bonus": 12},
	{"name": "Sealed Pressure Suit", "price": 350, "bonus": 16},
	{"name": "Star-iron Mail", "price": 600, "bonus": 21},
	{"name": "Void-tempered Plate", "price": 1000, "bonus": 27},
]
# name, type, price, damage, ammo cost, cooldown
static var RANGED: Array = [
	{"name": "Recurve Longbow", "type": C.Ranged.BOW, "price": 60, "bonus": 12, "ammo": 1, "cd": 2},
	{"name": "Reinforced Compound Bow", "type": C.Ranged.BOW, "price": 200, "bonus": 20, "ammo": 1, "cd": 2},
	{"name": "Aether-Fletched Warbow", "type": C.Ranged.BOW, "price": 500, "bonus": 32, "ammo": 1, "cd": 2},
	{"name": "Brass Revolver", "type": C.Ranged.GUN, "price": 80, "bonus": 16, "ammo": 1, "cd": 3},
	{"name": "Steam-Cycled Rifle", "type": C.Ranged.GUN, "price": 260, "bonus": 26, "ammo": 1, "cd": 3},
	{"name": "Company Autoloader", "type": C.Ranged.GUN, "price": 650, "bonus": 40, "ammo": 2, "cd": 3},
	{"name": "Prototype Beam Emitter", "type": C.Ranged.LASER, "price": 150, "bonus": 10, "ammo": 2, "cd": 4},
	{"name": "Refined Aether Cannon", "type": C.Ranged.LASER, "price": 400, "bonus": 16, "ammo": 2, "cd": 4},
	{"name": "Sunfire Projector", "type": C.Ranged.LASER, "price": 900, "bonus": 24, "ammo": 3, "cd": 4},
	{"name": "Bone Blowgun", "type": C.Ranged.BLOWGUN, "price": 50, "bonus": 6, "ammo": 1, "cd": 2},
	{"name": "Venom-Coil Pipe", "type": C.Ranged.BLOWGUN, "price": 180, "bonus": 10, "ammo": 1, "cd": 2},
	{"name": "Widowmaker's Reed", "type": C.Ranged.BLOWGUN, "price": 450, "bonus": 16, "ammo": 1, "cd": 2},
	{"name": "Rusted Throwing Stars", "type": C.Ranged.THROWN, "price": 45, "bonus": 8, "ammo": 1, "cd": 1},
	{"name": "Balanced Razor-Discs", "type": C.Ranged.THROWN, "price": 160, "bonus": 14, "ammo": 1, "cd": 1},
	{"name": "Company Star-Set", "type": C.Ranged.THROWN, "price": 420, "bonus": 22, "ammo": 1, "cd": 1},
	{"name": "Tin Grenade", "type": C.Ranged.GRENADE, "price": 100, "bonus": 14, "ammo": 2, "cd": 5},
	{"name": "Fragmentation Charge", "type": C.Ranged.GRENADE, "price": 320, "bonus": 22, "ammo": 3, "cd": 5},
	{"name": "Company Ordnance", "type": C.Ranged.GRENADE, "price": 750, "bonus": 34, "ammo": 3, "cd": 5},
]
static var ACCESSORIES: Array = [
	{"name": "Ring of Precision", "price": 200, "stat": C.Acc.CRIT, "bonus": 8},
	{"name": "Ring of Evasion", "price": 200, "stat": C.Acc.EVASION, "bonus": 8},
	{"name": "Charm of Warding", "price": 200, "stat": C.Acc.WARD, "bonus": 6},
	{"name": "Lucky Coin-Charm", "price": 180, "stat": C.Acc.GOLD, "bonus": 10},
	{"name": "Scholar's Locket", "price": 180, "stat": C.Acc.XP, "bonus": 10},
	{"name": "Ouroboros Coil", "price": 5000, "stat": C.Acc.REGEN, "bonus": 6},
]

# The five biome sets: weapon, armour, ring, trinket. [name, bonus, accessory stat]
static var SET_GEAR: Array = [
	[["Root-Drowned Thornblade", 14, C.Acc.NONE], ["Root-Drowned Bark Plating", 15, C.Acc.NONE],
		["Root-Drowned Vine-Ring", 9, C.Acc.EVASION], ["Root-Drowned Seed-Pouch", 9, C.Acc.XP]],
	[["Foreman's Piston Maul", 23, C.Acc.NONE], ["Foreman's Riveted Plating", 25, C.Acc.NONE],
		["Foreman's Signet", 15, C.Acc.WARD], ["Foreman's Ledger", 15, C.Acc.GOLD]],
	[["Custodian's Crystal Halberd", 35, C.Acc.NONE], ["Custodian's Drowned Mail", 37, C.Acc.NONE],
		["Custodian's Ward-Band", 23, C.Acc.WARD], ["Custodian's Reliquary", 23, C.Acc.CRIT]],
	[["Salt-Cured Cleaver", 49, C.Acc.NONE], ["Salt-Cured Hide Plating", 51, C.Acc.NONE],
		["Salt-Cured Wanderer's Band", 33, C.Acc.EVASION], ["Salt-Cured Waterskin", 33, C.Acc.GOLD]],
	[["Warden's Reaping Scythe", 67, C.Acc.NONE], ["Warden's Black Shroud", 70, C.Acc.NONE],
		["Warden's Signet", 45, C.Acc.CRIT], ["Warden's Locket", 45, C.Acc.WARD]],
]
const SET_NAMES := ["Root-Drowned", "Foreman's", "Custodian's", "Salt-Cured", "Warden's"]


static func consumable_by_name(n: String) -> Dictionary:
	for table in [GENERAL, APOTHECARY, MERCHANT, LOOT_LOW, LOOT_MID, LOOT_HIGH]:
		for t in table:
			if t.name == n:
				return t
	return {}


static func pick_floor_loot(rng: RandomNumberGenerator, floor_num: int) -> Dictionary:
	if floor_num <= 30:
		return LOOT_LOW[rng.randi() % 4]
	if floor_num <= 70:
		return LOOT_MID[rng.randi() % 4] if rng.randi() % 100 < 60 else LOOT_LOW[rng.randi() % 4]
	return LOOT_HIGH[rng.randi() % 4] if rng.randi() % 100 < 55 else LOOT_MID[rng.randi() % 4]


static func prop_for_consumable(t: Dictionary) -> String:
	if t.get("recall", false): return "recall"
	if t.get("perm_maxhp", 0) > 0: return "elixir"
	if t.get("atk_buff", 0) > 0: return "tonic"
	if t.get("def_buff", 0) > 0: return "guard"
	if String(t.name).begins_with("Ration") or String(t.name).begins_with("Field"): return "ration"
	return "potion"


static func upgrade_price(current_plus: int) -> int:
	var n := maxi(current_plus, 0) + 1
	return 20 * n * n + 100 * n + 20


static func training_price(value: int) -> int:
	value = maxi(value, 1)
	return 200 * value * value + 300


static func bank_price(current_mult: int) -> int:
	current_mult = maxi(current_mult, 1)
	if current_mult >= C.BANK_MULT_MAX:
		return -1
	return 1500 * current_mult * current_mult * current_mult


static func level_sale_price(level: int) -> int:
	if level < 2:
		return 0
	return C.MARKET_GOLD_PER_XP * (20 + (level - 1) * 15)


static func junk_worth(floor_num: int) -> int:
	var biome_step := floor_num / 20
	return (8 + floor_num) * (1 << mini(biome_step, 4))
