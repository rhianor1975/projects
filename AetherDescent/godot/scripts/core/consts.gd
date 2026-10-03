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
	QUEST_BOARD,
	# the later wild districts: appended, never inserted -- floors are saved
	ROD, CURRENT, BELT, PIT, ORE, VENT,
}

enum Difficulty { NORMAL, HARD, SWARM, HARDCORE }
const DIFFICULTY_NAMES := ["Normal", "Hard", "Swarm", "Hardcore"]
const DIFFICULTY_BLURBS := [
	"The baseline.",
	"2-3x the monsters. One floor in eight is overrun.",
	"10-20x the monsters, each weaker. XP and gold per kill are cut.",
	"Swarm's density, and no safety net: no recall, death is final.",
]

enum WorldSize { SHAFT, HALLS, DEEPS, WELL }
const WORLD_NAMES := ["The Shaft", "The Halls", "The Deeps", "The Well"]
const WORLD_DIMS := [Vector2i(140, 80), Vector2i(350, 200), Vector2i(700, 400), Vector2i(1400, 800)]
const WORLD_BLURBS := [
	"140 x 80. Cleared in minutes; the stairs are never far.",
	"350 x 200. Room to get lost without losing the afternoon.",
	"700 x 400. Each floor is a journey. A waygate on every floor.",
	"1400 x 800. Vast: a hundred times the Shaft. Crossing a single floor is the evening's work.",
]

enum Biome { JUNGLE, INDUSTRIAL, RUINS, WASTES, ABYSS }
const BIOME_NAMES := ["the Sunken Jungle Roots", "the Company Works", "the Flooded Ruins",
	"the Salt Wastes", "the Abyssal Approach"]
const BIOME_SHORT := ["Jungle Roots", "Company Works", "Flooded Ruins", "Salt Wastes", "Abyssal Approach"]

enum Feature { SHRINE, FOUNTAIN, MERCHANT, MACHINE, RELIC, TOWN_GATE, ALTAR, TOLL, CONSOLE, STRONGBOX }
const FEATURE_PROPS := ["shrine", "fountain", "merchant", "machine", "relic", "waygate", "altar", "toll", "console", "strongbox"]
const FEATURE_NAMES := ["Shrine", "Fountain", "Merchant", "Machine", "Relic", "Waygate", "Altar", "Toll", "Console", "Strongbox"]

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
enum District { JUNGLE, SEA, SWAMP, RUINS, MYCELIUM, HIVE, MIRE, CRYSTAL, QUIET, BLOODMARSH, PRISM, GARDEN, QUICKSAND,
	STORM, ARENA, AQUEDUCT, ASSEMBLY, WATCH, GAUNTLET, EYE, MIRROR, PETRIFIED, SHAFT, BONEYARD, PROVING, CHAPEL, GEOTHERMAL }
const DISTRICT_NAMES := ["Jungle", "Sea", "Swamp", "Ruins", "Mycelium", "Hive", "Mire", "Crystal",
	"Quiet Quarter", "Blood Marsh", "Chromatic Abyss", "Carnivorous Garden", "Quicksand Basin",
	"Storm-Cage", "Arena", "Aqueduct", "Assembly Line", "The Watch", "Barrow of the Fallen", "Eye of the Storm",
	"Mirror", "Stone Wood", "Workings", "Boneyard", "Proving Ground", "Chapel of Silence", "Vent Field"]
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
	"a rod sings, then the lightning takes it -- stand clear, or lure them close",
	"step onto the sand and the gates shut: ten waves, free to leave after five",
	"channels that carry whatever stands in them",
	"belts that carry you, lanes alternating; three keys at the console build a golem",
	"awake and watching -- start nothing, and the strongbox is yours",
	"the runs you have already lost, waiting for you",
	"one quarter is quiet at a time, and it moves",
	"it makes a copy of you and sets it walking",
	"stone trees; what you cannot see gets the first blow",
	"holes in the floor: a floor skipped, paid for in blood; ore in the galleries",
	"dead golems -- ten turns cracking one open (g) for its core",
	"fight their way, or do not fight here",
	"nothing carries here: they notice you only up close",
	"vents on a timer -- stand clear when one draws",
]
const DISTRICT_TINTS := [Color8(96, 200, 96), Color8(80, 140, 230), Color8(120, 170, 90),
	Color8(210, 210, 220), Color8(170, 120, 230), Color8(230, 180, 60), Color8(170, 190, 200),
	Color8(150, 220, 255), Color8(190, 190, 160), Color8(220, 60, 70), Color8(230, 120, 230),
	Color8(240, 120, 170), Color8(230, 210, 150),
	Color8(150, 170, 255), Color8(230, 200, 140), Color8(110, 170, 240), Color8(200, 170, 120),
	Color8(210, 190, 150), Color8(170, 150, 200), Color8(150, 200, 230), Color8(220, 230, 255),
	Color8(170, 170, 160), Color8(190, 150, 110), Color8(180, 170, 150), Color8(240, 210, 140),
	Color8(200, 200, 230), Color8(240, 150, 90)]
# per biome: jungle sea swamp ruins myc hive mire cryst quiet blood prism garden sand
# then storm arena aque asm watch barrow eye mirror stone shaft bone prov chapel vent
const DIST_WEIGHT := [
	[18, 8, 11, 5, 9, 6, 4, 4, 6, 5, 3, 10, 3, 5, 3, 5, 2, 3, 1, 2, 2, 8, 2, 1, 2, 2, 2],
	[5, 4, 9, 20, 5, 6, 5, 6, 6, 2, 3, 2, 3, 8, 3, 8, 11, 9, 2, 5, 3, 2, 9, 10, 4, 3, 8],
	[12, 7, 5, 17, 6, 5, 6, 7, 8, 3, 2, 4, 4, 4, 3, 7, 5, 7, 3, 4, 5, 5, 5, 6, 6, 6, 3],
	[3, 3, 17, 17, 4, 5, 10, 6, 5, 5, 2, 3, 10, 8, 2, 4, 3, 5, 3, 7, 3, 6, 6, 4, 4, 4, 8],
	[3, 16, 12, 9, 7, 7, 7, 8, 6, 4, 5, 4, 5, 5, 2, 5, 3, 4, 4, 6, 7, 3, 4, 3, 5, 6, 6],
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
# the later districts -- src/common.h
const STORM_STRIKE_EVERY := 5
const STORM_BLAST_RADIUS := 2
const STORM_DAMAGE := 10
const ARENA_WAVES := 10
const ARENA_FREE_AFTER := 5
const ARENA_BASE_ENEMIES := 3
const CURRENT_PUSH := 3
const BELT_PUSH := 2
const CONSOLE_KEY_COST := 3
const WATCH_AGGRO_RADIUS := 2
const WATCH_FIND_PCT := 5
const WATCH_FIND_CAP := 60
enum { WATCH_KEEPING, WATCH_BROKEN, WATCH_TAKEN }
const GAUNTLET_MAX_GHOSTS := 5
const GAUNTLET_MIN_GHOSTS := 1
const FALLEN_MAX := 16
const EYE_ROTATE_EVERY := 8
const EYE_DAMAGE := 7
const EYE_STRIKE_EVERY := 3
const MIRROR_HP_PCT := 50
const MIRROR_DAMAGE_PCT := 50
const AMBUSH_EXTRA_BLOWS := 1
const PIT_FALL_HP_PCT := 18
const PIT_MIN_DAMAGE := 6
const CHAPEL_AGGRO_RADIUS := 3
enum Prove { MELEE, ARCANE, UNARMED }
const PROVE_RULES := ["blades only", "the arts only", "no weapons"]
const PROVE_KILLS_WANTED := 6
enum Act { MELEE, ARCANE, RANGED }
# work: multi-turn jobs with `g`
enum Work { NONE, MINE, HARVEST_ROD, SALVAGE, CORE, HARVEST_VENT }
const WORK_TURNS := [0, 8, 5, 10, 10, 5]
const WORK_VERBS := ["working", "cutting the vein out", "cutting the ore out of the rod",
	"stripping the wreck", "cracking the core open", "breaking the crust off the vent"]
# materials: what the deep upgrade rungs want
enum Mat { SCRAP, PLATINUM, DIAMOND }
const MAT_NAMES := ["scrap", "platinum", "diamond"]
const UPGRADE_PLATINUM_FROM := 20
const UPGRADE_DIAMOND_FROM := 50

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
