class_name C
## Shared enums and tuning constants. Numbers carry the C game's names where
## they come from it (common.h, monsters.c, mapgen.c), so a grep across the
## two codebases finds both halves of a rule.

enum Tile {
	WALL, FLOOR, STAIRS_DOWN, STAIRS_UP, DECOR, WATER, BRIDGE, LAVA, MIASMA, PORTAL,
	LOCKED_DOOR, SEALED_DOOR, LEVER, THICKET, CRYSTAL, BLOODPOOL, PRISM_RED, PRISM_BLUE,
	PRISM_GREEN, SNARE, GRASS, FLOWERS,
	# town
	TOWN_FLOOR, ROOF, HOUSE_WALL, DOOR, TOWN_GRASS, TOWN_FLOWERS, PLANTER, FOUNTAIN, TEMPLE,
}

enum Difficulty { NORMAL, HARD, SWARM, HARDCORE }
const DIFFICULTY_NAMES := ["Normal", "Hard", "Swarm", "Hardcore"]
const DIFFICULTY_BLURBS := [
	"The baseline.",
	"2-3x the monsters. One floor in eight is overrun.",
	"10-20x the monsters, each weaker. XP and gold per kill are cut.",
	"Swarm's density, and no safety net: no recall, death is final.",
]

enum WorldSize { SHAFT, HALLS, DEEPS }
const WORLD_NAMES := ["The Shaft", "The Halls", "The Deeps"]
const WORLD_DIMS := [Vector2i(140, 80), Vector2i(350, 200), Vector2i(700, 400)]
const WORLD_BLURBS := [
	"140 x 80. Cleared in minutes; the stairs are never far.",
	"350 x 200. Room to get lost without losing the afternoon.",
	"700 x 400. Each floor is a journey. A waygate on every floor.",
]

enum Biome { JUNGLE, INDUSTRIAL, RUINS, WASTES, ABYSS }
const BIOME_NAMES := ["the Sunken Jungle Roots", "the Company Works", "the Flooded Ruins",
	"the Salt Wastes", "the Abyssal Approach"]
const BIOME_SHORT := ["Jungle Roots", "Company Works", "Flooded Ruins", "Salt Wastes", "Abyssal Approach"]

enum Feature { SHRINE, FOUNTAIN, MERCHANT, MACHINE, RELIC, TOWN_GATE, ALTAR, TOLL }
const FEATURE_PROPS := ["shrine", "fountain", "merchant", "machine", "relic", "waygate", "altar", "toll"]
const FEATURE_NAMES := ["Shrine", "Fountain", "Merchant", "Machine", "Relic", "Waygate", "Altar", "Toll"]

enum Attr {
	MIGHT, BRAWN, AGILITY, REFLEXES, PRECISION, GRIT, FORTITUDE, STAMINA, BALANCE, VISION,
	HEARING, INTELLECT, CUNNING, MEMORY, RESOLVE, INSTINCT, CHARM, GUILE, PRESENCE, EMPATHY,
	TECH_WIT, CRAFTING, CHEMISTRY, SCRIBING, AETHER_SENSE, CONDUIT, RESONANCE, WARDING, FORTUNE, JINX,
}
const ATTR_COUNT := 30
const ATTR_NAMES := ["Might", "Brawn", "Agility", "Reflexes", "Precision", "Grit", "Fortitude",
	"Stamina", "Balance", "Vision", "Hearing", "Intellect", "Cunning", "Memory", "Resolve",
	"Instinct", "Charm", "Guile", "Presence", "Empathy", "Tech-Wit", "Crafting", "Chemistry",
	"Scribing", "Aether-Sense", "Conduit", "Resonance", "Warding", "Fortune", "Jinx"]

const ARCHETYPE_NAMES := ["Vanguard", "Skirmisher", "Marksman", "Arcanist", "Artificer", "Survivor", "Envoy"]
const ARCHETYPE_BLURBS := [
	"Stands in front and stays there. Might, brawn, and the patience to be hit.",
	"Fast, slippery, hard to pin down. Fights by not being where the blow lands.",
	"Kills at a distance. Precision and eyesight over anything that swings.",
	"Works in aether. The six schools, and the charge to spend on them.",
	"Builds, brews and rigs. Turns scrap and chemistry into an advantage.",
	"Outlasts. Fortitude, stamina, instinct, and a nose for what's worth taking.",
	"Leads, bluffs, and is listened to. Presence and guile over muscle.",
]

enum School { CONDUIT, RESONANCE, WARDING, AETHER_SENSE, SCRIBING, JINX }
const SCHOOL_NAMES := ["Conduit", "Resonance", "Warding", "Aether-Sense", "Scribing", "Jinx"]
const SCHOOL_COLORS := [Color8(255, 196, 72), Color8(255, 120, 200), Color8(120, 200, 255),
	Color8(160, 255, 220), Color8(220, 200, 140), Color8(176, 96, 255)]

enum Ability { CRUSHING_BLOW, ADRENALINE, EVASIVE_ROLL, RIPOSTE, CALLED_SHOT, UNBREAKABLE,
	IRON_RESOLVE, SECOND_WIND, STEADY_FOOTING, PREDATORS_SENSE }
const ABILITY_NAMES := ["Crushing Blow", "Adrenaline Surge", "Evasive Roll", "Riposte Stance",
	"Called Shot", "Unbreakable", "Iron Resolve", "Second Wind", "Steady Footing", "Predator's Sense"]
const ABILITY_DESCS := [
	"a single heavy strike against the nearest foe",
	"instantly heals you",
	"a burst of evasion for a few turns",
	"a burst of attack power for a few turns",
	"a guaranteed heavy hit against the nearest foe",
	"a burst of defence for a few turns",
	"a large instant heal",
	"heals you and sharpens your attack briefly",
	"a burst of evasion and defence together",
	"locks the nearest foe in place and lights up your surroundings",
]
const ABILITY_COOLDOWNS := [6, 8, 7, 7, 6, 8, 9, 8, 8, 7]

enum Ranged { BOW, GUN, LASER, BLOWGUN, THROWN, GRENADE, NONE }
const RANGED_NAMES := ["Bow", "Gun", "Laser", "Blowgun", "Thrown", "Grenade", "None"]
const RANGED_REACH := [6, 8, 7, 5, 4, 5]
const RANGED_COLORS := [Color8(220, 200, 150), Color8(255, 230, 120), Color8(255, 80, 96),
	Color8(150, 230, 110), Color8(210, 220, 240), Color8(255, 160, 64)]
const RANGED_AMMO_REGEN_TURNS := 4

enum Acc { CRIT, EVASION, WARD, GOLD, XP, REGEN, NONE }
const ACC_NAMES := ["crit", "evasion", "ward", "gold find", "XP find", "regen", ""]

enum Relic { WEAPON, ARMOR, RING, TRINKET }

enum Effect {
	DAMAGE, DAMAGE_NOVA, SCORCH, BUFF_ATK, HEAL_SELF, WARD_SHIELD, BARRIER, PURGE, BLINK, REVEAL,
	BRIDGE, STUN, SLOW, UNBIND, LIFE_DRAIN, CURSE, DOT_BURN, CHAIN, PARTY_HEAL, HASTE, REFLECT,
	SENSE_LIFE, MASS_SLOW,
}

# Districts carried over: the terrain-and-rule kinds. See mapgen.gd.
enum District { JUNGLE, SEA, SWAMP, RUINS, MYCELIUM, HIVE, MIRE, CRYSTAL, QUIET, BLOODMARSH, PRISM, GARDEN, QUICKSAND }
const DISTRICT_NAMES := ["Jungle", "Sea", "Swamp", "Ruins", "Mycelium", "Hive", "Mire", "Crystal",
	"Quiet Quarter", "Blood Marsh", "Chromatic Abyss", "Carnivorous Garden", "Quicksand Basin"]
const DISTRICT_NOTES := [
	"ragged ground, thicket and pools -- fights start at four paces",
	"standing water, islands, causeways laid over it",
	"mottled water, floor and gas",
	"streets and small buildings, a third of the walls collapsed",
	"the mats carry your footsteps -- stand still and they forget you",
	"hurt one and they all know",
	"mist: you see three tiles",
	"spells and shot ricochet back",
	"nothing is awake -- until you start something",
	"what wades in it stops staying hurt",
	"the ground you fight on is a choice",
	"flowers with teeth: 8 damage, held 2 turns",
	"sand with a ripple in it: 3 damage, held 4 turns",
]
const DISTRICT_TINTS := [Color8(96, 200, 96), Color8(80, 140, 230), Color8(120, 170, 90),
	Color8(210, 210, 220), Color8(170, 120, 230), Color8(230, 180, 60), Color8(170, 190, 200),
	Color8(150, 220, 255), Color8(190, 190, 160), Color8(220, 60, 70), Color8(230, 120, 230),
	Color8(240, 120, 170), Color8(230, 210, 150)]
# per biome: jungle sea swamp ruins myc hive mire cryst quiet blood prism garden sand
const DIST_WEIGHT := [
	[18, 8, 11, 5, 9, 6, 4, 4, 6, 5, 3, 10, 3],
	[5, 4, 9, 20, 5, 6, 5, 6, 6, 2, 3, 2, 3],
	[12, 7, 5, 17, 6, 5, 6, 7, 8, 3, 2, 4, 4],
	[3, 3, 17, 17, 4, 5, 10, 6, 5, 5, 2, 3, 10],
	[3, 16, 12, 9, 7, 7, 7, 8, 6, 4, 5, 4, 5],
]
const MYCELIUM_HEAR_RADIUS := 20
const MYCELIUM_FORGET_TURNS := 2
const HIVE_ALARM_RADIUS := 20
const MIRE_FOV_RADIUS := 3
const CRYSTAL_RICOCHET_PCT := 30
const QUIET_AGGRO_RADIUS := 3
const BLOODPOOL_REGEN := 5
const PRISM_SWING := 50
const PRISM_REGEN := 3
const SNARE_GARDEN_DAMAGE := 8
const SNARE_GARDEN_HOLD := 2
const SNARE_SAND_DAMAGE := 3
const SNARE_SAND_HOLD := 4

const MAX_FLOOR := 100
const NEW_GAME_GOLD := 60
const ESCALATION_PER_LEVEL := 10
const ESCALATION_PER_ENTRY := 5
const ESCALATION_CAP := 25
const SWARM_MONSTER_FRACTION := 10
const SWARM_ATTACK_PCT := 35
const HARD_FLOOR1_DENSITY := 1
const SWARM_FLOOR1_DENSITY := 3
const HARDCORE_DENSITY_PCT := 160
const MONSTER_CRIT_PCT := 5
const GOLD_RUSH_MULT := 20
const JUNK_STEPS_PER_PIECE := 100
const UPGRADE_STEP := 15
const UPGRADE_HP_STEP := 10
const UPGRADE_AMMO_STEP := 2
const BANK_MULT_MAX := 100
const MARKET_GOLD_PER_XP := 1
const SET_2PC := 8
const SET_4PC := 14
const SET_PROC_PCT := 15
const RESPAWN_MIN_DIST := 15
const DOUBLING_MIN_WAVE := 8
const ACTIVE_RADIUS := 40   # monsters further than this from you do not take turns
const ORACLE_STAIRS_PRICE := 500
const ORACLE_FLOOR_PRICE := 1800

const DIRS8 := [Vector2i(-1, 0), Vector2i(1, 0), Vector2i(0, -1), Vector2i(0, 1),
	Vector2i(-1, -1), Vector2i(1, -1), Vector2i(-1, 1), Vector2i(1, 1)]


static func biome_for_floor(f: int) -> int:
	if f <= 15: return Biome.JUNGLE
	if f <= 35: return Biome.INDUSTRIAL
	if f <= 60: return Biome.RUINS
	if f <= 85: return Biome.WASTES
	return Biome.ABYSS


static func tier_for_floor(f: int) -> int:
	return biome_for_floor(f)
