#ifndef COMMON_H
#define COMMON_H

#include "tui.h"

/* Silent self-play (src/autoplay.c) supplies keystrokes in place of a person.
   Every screen in the game reads input through getch(), so redirecting it
   here reaches all of them at once -- including the ones nobody remembered to
   make testable. tui.c is not affected: it includes tui.h directly and never
   sees this macro, so the notcurses backend's own getch() definition stands. */
int aether_getch(void);
#undef getch          /* ncurses defines it as wgetch(stdscr) */
#define getch() aether_getch()

/* Character creation asks for a name through getnstr, which reads the
   terminal directly and so is a second, separate way into the game's input.
   Missing it meant a silent run stalled on the very first prompt: three
   thousand keystrokes that never got past the name entry, and a log with
   seven lines of game messages in it. */
int aether_getnstr(char *buf, int n);
#undef getnstr
#define getnstr(s, n) aether_getnstr((s), (n))
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdarg.h>
#include <time.h>
#include <math.h>

/* The dungeon is a big scrolling world; VIEW_W/VIEW_H is the terminal
   window we actually draw at once (camera follows the player). Town uses
   the same grid but never scrolls -- its whole layout fits inside one
   viewport, camera pinned at (0,0).

   The view size is set once at startup from the real terminal size (see
   main()), clamped to [58,18] .. [MAP_W,MAP_H] so it never shrinks below
   what town's fixed building layout needs. Everywhere else in the codebase
   just reads VIEW_W/VIEW_H as if they were constants. */
/* ---- how big a floor is -------------------------------------------------
 * Chosen once per run, on the new-game screen. MAP_W/MAP_H are *runtime*
 * values, exactly like VIEW_W/VIEW_H below -- which means every loop and
 * bounds check in the codebase became size-aware without being touched.
 *
 * The _MAX forms are the compile-time ceiling, and they are what every array
 * is actually sized by. A Map is therefore always the largest size in
 * memory, whichever world you picked: the alternative is dynamic allocation
 * threaded through every system, for a saving nobody asked for. */
#define MAP_W_MAX 1400
#define MAP_H_MAX 800
extern int g_map_w, g_map_h;
#define MAP_W (g_map_w)
#define MAP_H (g_map_h)

typedef enum {
    WORLD_SHAFT = 0,   /* 140x80    -- the original: a floor cleared in minutes */
    WORLD_HALLS,       /* 350x200   -- roomy, still one sitting */
    WORLD_DEEPS,       /* 700x400   -- a journey per floor */
    WORLD_WELL,        /* 1400x800  -- the whole descent is the walk */
    WORLD_SIZE_COUNT
} WorldSize;

const char *world_size_name(int size);
const char *world_size_blurb(int size);
void world_size_dims(int size, int *out_w, int *out_h);
/* Sets g_map_w/g_map_h. Must be called before a floor is generated or a
   viewport clamped -- every bound in the game reads from it. */
void world_size_apply(int size);

/* How often a floor carries a waygate back to town.
 *
   Five floors apart is a reasonable walk on the Shaft and the Halls. On the
   Deeps and the Well it is not a walk at all -- a floor is 157,000 to
   627,000 walkable tiles, so "climb four floors to reach a gate" means an
   evening of retracing ground you have already cleared. The big worlds get
   one on every floor: the same convenience, priced against the same amount
   of *walking* rather than the same number of floors. */
int waygate_interval(void);

extern int g_view_w, g_view_h;

/* The town's own size, which is NOT the viewport's.
 *
   generate_town_map() used to lay the plaza out to VIEW_W x VIEW_H, on the
   reasoning that town fits in one screen with no scrolling. That stopped being
   true when tile mode started shrinking the viewport to fit a pixel budget: the
   plaza was then rebuilt narrower every time you climbed the stairs, and the
   buildings past the new edge -- the Armory, the Inn, the lizard track -- were
   placed into ground that had never been floored. Walled in, unlit, unreachable.
   Measured at a 46-wide viewport: three of the thirteen doors cut off, and at 50
   wide, two.

   The town is a place. It is the same size whoever is looking at it, and the
   camera scrolls if the window cannot hold it.

   Named TOWN_MAP_* and not TOWN_W/TOWN_H, which is what they were called for
   about a minute: `#define TOWN_H` collides with town.h's own include guard
   (`#ifndef TOWN_H`), so defining it here silently skipped that entire header
   and everything in it -- TEMPLE_DOOR_X, PLAYER_TOWN_START_X,
   generate_town_map -- went undeclared at every call site at once. */
/* ---- the Oracle -------------------------------------------------------
   Information as a purchasable good, which nothing else in this game sells.
   The three readings are one mechanism at three scopes: mark the way down,
   map the next floor, map the next few. Priced steeply because on a
   1400x800 floor knowing where the stairs are is worth more than any item
   at the same price, and because it is the only shop whose value *rises*
   with the world size rather than falling. */
#define ORACLE_STAIRS_PRICE   500
#define ORACLE_FLOOR_PRICE   1800
#define ORACLE_LONG_PRICE    4200
#define ORACLE_LONG_FLOORS      3

/* ---- the Altar --------------------------------------------------------
   The reallocation sink the attribute rule keeps asking for. You give up a
   point to gain a point, and still pay -- half the School's price for the
   rung you are buying, on the same 200v^2+300 curve, because a trade that
   only costs a point is a free respec and this game does not do those.
   Nothing may be reduced below 1: a zeroed attribute is a body that cannot
   act, and the Altar is not a way to build one. */
#define ALTAR_MIN_ATTR          1
#define ALTAR_PRICE_NUM         1
#define ALTAR_PRICE_DEN         2

/* ---- the Bazaar -------------------------------------------------------
   The only shop where *when* you buy matters. Every stall re-rolls between
   runs, in a band wide enough that waiting a trip is sometimes right and
   sometimes costs you the potion you needed. The band is deliberately
   asymmetric around 100: a market that averaged out to the list price would
   be a shop with a decoration on it. */
#define BAZAAR_MIN_PCT         55
#define BAZAAR_MAX_PCT        150

/* What a new character walks into town with. See the note at its use in
   main.c, and the guard in tests/balance.c. */
/* How many an upper-case letter uses at once on the inventory screen. See the
   note at its use in main.c: packs reach four figures and one keypress per
   bottle is not a usable way to spend them. */
#define INV_BULK_USE 10

#define NEW_GAME_GOLD 60

#define TOWN_MAP_W 58
#define TOWN_MAP_H 18
#define VIEW_W (g_view_w)
#define VIEW_H (g_view_h)

/* Floor size is one knob, and everything that fills a floor is counted off
   it. The caps used to be flat numbers tuned for 140x80; on a bigger map a
   flat sixty rooms and two thousand monsters just scatter across four times
   the rock and leave a near-empty plain, which is the objection that killed
   the first attempt at this (EVALUATION §30).
 *
 * MAP_AREA_SCALE is the multiple of the original 140x80 = 11,200 tiles, in
 * sixteenths so the arithmetic stays integer and a fractional size still
 * lands somewhere sensible. Room shapes and wild districts already work in
 * counts-per-10,000-tiles and need nothing here. */
#define MAP_AREA_SCALE ((MAP_W * MAP_H * 16) / 11200)
#define SCALE_BY_AREA(n) (((n) * MAP_AREA_SCALE) / 16)

/* The same accounting at the compile-time ceiling, for anything that has to
   be an array bound rather than a loop bound. */
#define MAP_AREA_SCALE_MAX ((MAP_W_MAX * MAP_H_MAX * 16) / 11200)
#define SCALE_BY_AREA_MAX(n) (((n) * MAP_AREA_SCALE_MAX) / 16)

/* Raised from 2,000 once the activity radius made a monster's *existence*
   nearly free -- the old cap was not a design choice, it was the point where
   an O(n) turn loop over everything on the floor stopped being affordable.
   With only what is near the player simulating, the ceiling can be what the
   invasion wants rather than what the loop could bear. */
#define MAX_MONSTERS SCALE_BY_AREA(6000)
#define MAX_FLOOR_ITEMS SCALE_BY_AREA(140)
#define MAX_FEATURES SCALE_BY_AREA(16)
#define MAX_MONSTERS_MAX SCALE_BY_AREA_MAX(6000)
#define MAX_FLOOR_ITEMS_MAX SCALE_BY_AREA_MAX(140)
#define MAX_FEATURES_MAX SCALE_BY_AREA_MAX(16)
/* Wild districts. In common.h rather than mapgen.c because the Map carries
   them now -- their rules are read during play, not just at generation. */
/* The ceiling on wild districts per floor.
 *
   Not scaled by area, unlike almost everything else here, because district
   *size* scales with the world too (mapgen.c) -- so the number needed to
   cover a given share of a floor is roughly the same on the Shaft as on the
   Well. Scaling this one by area as well asked a Well floor for 1400
   districts, which cannot be placed without overlapping and only burned
   eighty thousand failed placement attempts finding that out.

   The real count comes from district_count_for_area(); this is only the
   ceiling the "overgrown floor" roll reaches for, and the array bound. */
#define MAX_DISTRICTS     220
#define MAX_DISTRICTS_MAX 220
/* Distinct stacks the pack holds. Quantities never had a limit --
   give_consumable stacks by name -- so this only ever bit as "you may not
   pick up a *kind* of thing you do not already carry", which is the least
   interesting refusal a game can make. Set well above the number of distinct
   items a run can accumulate, so the pack does not fill.

   Note this is not the only ceiling: the inventory screen selects with a
   single letter, so anything past the 26th entry was unreachable regardless
   of what this said. Raising one without the other would have moved the wall
   rather than removed it -- see draw_inventory_screen's paging. */
#define INV_CAP 120
#define INV_PAGE 20
#define MSG_LOG_CAP 200
#define MSG_LOG_VISIBLE 3

#define MAX_FLOOR 100
#define NUM_CLASSES 100

/* ncurses color pair ids, shared between monsters.c (assigns them) and
   render.c (defines them via init_colors) */
#define CP_PLAYER        1
#define CP_WALL          2
#define CP_FLOOR         3
#define CP_DECOR         4
#define CP_STAIRS        5
#define CP_SHOP          6
#define CP_GOLD          7
#define CP_POTION        8
#define CP_MON_VERMIN    9
#define CP_MON_CLOCKWORK 10
#define CP_MON_RUINS     11
#define CP_MON_OUTRIDER  12
#define CP_MON_ABYSSAL   13
#define CP_MON_BOSS      14
#define CP_UI_HEADER     15
#define CP_UI_WARN       16
#define CP_WATER         17
#define CP_LAVA          18
#define CP_MIASMA        19
#define CP_PORTAL        20
#define CP_SHRINE        21
#define CP_FOUNTAIN      22
#define CP_MERCHANT      23
#define CP_MACHINE       24
#define CP_RELIC         25
#define CP_GATE          26
#define CP_LOCKED_DOOR   27
#define CP_KEY           28
#define CP_QUEST_BOARD   29
#define CP_HP_OK         30
#define CP_HP_MID        31
#define CP_FRAME         32
#define CP_MAGIC         33
#define CP_LEVER         34

/* Combat-animation colors: a neutral one for plain physical hits and class
   abilities, then a distinct one per magic school and per ranged weapon
   type so a bolt or burst reads as "what kind of attack was that" at a
   glance, not just "something happened". */
#define CP_HIT                 35
#define CP_ABILITY             36
#define CP_SCHOOL_CONDUIT      37
#define CP_SCHOOL_RESONANCE    38
#define CP_SCHOOL_WARDING      39
#define CP_SCHOOL_AETHER_SENSE 40
#define CP_SCHOOL_SCRIBING     41
#define CP_SCHOOL_JINX         42
#define CP_RANGED_BOW          43
#define CP_RANGED_GUN          44
#define CP_RANGED_LASER        45
#define CP_RANGED_BLOWGUN      46
#define CP_RANGED_THROWN       47
#define CP_RANGED_GRENADE      48
#define CP_THICKET             49

/* One colour per wild district, so a floor reads as *places* rather than as
   grey corridor with occasional furniture. Only the neutral terrain inside a
   district is tinted -- stairs, doors, water and features keep their own
   colours, because those mean something and a district is scenery. */
#define CP_DIST_JUNGLE         50
#define CP_DIST_SEA            51
#define CP_DIST_SWAMP          52
#define CP_DIST_RUINS          53
#define CP_DIST_MYCELIUM       54
#define CP_DIST_HIVE           55
#define CP_DIST_MIRE           56
#define CP_DIST_CRYSTAL        57
#define CP_DIST_QUIET          58
#define CP_DIST_STORM          59
#define CP_DIST_ARENA          60
#define CP_DIST_AQUEDUCT       61
#define CP_DIST_BLOODMARSH     62
#define CP_DIST_PRISM          63
#define CP_DIST_ASSEMBLY       64
#define CP_DIST_GARDEN         65
#define CP_DIST_QUICKSAND      66
#define CP_DIST_WATCH          67
#define CP_DIST_GAUNTLET       68
#define CP_DIST_EYE            69
#define CP_DIST_MIRROR         70
#define CP_DIST_PETRIFIED      71
#define CP_DIST_SHAFT          72
#define CP_DIST_BONEYARD       73
#define CP_DIST_PROVING        74

/* One colour per *floor* biome, laid under everything as a wash.
 *
   Wild districts colour the places that are special; this colours the rock
   between them. Without it a floor called the Sunken Jungle Roots was grey
   corridor everywhere a district did not happen to land -- and districts
   covered about five per cent of a floor, so the name was the only jungle
   there was. These are the quieter colours of the two: a district still has
   to read as a place you have walked into, so it keeps the brighter tone and
   paints over this. */
/* These start at 80 rather than following the district block, and there is a
   gap on purpose. They were originally 67-71, written when the district list
   ended at 66 -- and then the district list grew. DIST_WATCH through
   DIST_PETRIFIED took 67-71 as well, so ten names shared five curses pairs and
   every one of these five was re-initialised by a district a few lines later.
   The districts won, being second, and every biome wash in text and block mode
   came out in a district's colour: jungle drew blue, industrial magenta, ruins
   cyan, wastes white, abyss yellow. Nothing warns about this -- init_pair on a
   live pair is a legal redefinition, not an error.

   The gap at 75-79 is room for the district list to grow again without
   reaching this block. tests/invariants.c asserts no two CP_ names collide. */
/* Inside the chapel nothing carries. A monster there notices you at three
   tiles and only by sight -- the aggro radius it would otherwise use is
   hearing, and there is none. The player pays the same coin: `hearing_radius`
   is zero on chapel ground, so the sense that reaches through walls is exactly
   the sense the district takes away. Symmetrical on purpose; a district that
   only removes something from the player is a penalty, not a place. */
#define CHAPEL_AGGRO_RADIUS     3

#define CP_DIST_CHAPEL         75
#define CP_DIST_GEOTHERMAL     76

/* The mark on a square you can hear but not see. Its own pair because it has
   to read against remembered terrain in every biome -- see vision.c. */
#define CP_HEARD               79

#define CP_BIOME_JUNGLE        80
#define CP_BIOME_INDUSTRIAL    81
#define CP_BIOME_RUINS         82
#define CP_BIOME_WASTES        83
#define CP_BIOME_ABYSS         84

typedef enum {
    TILE_WALL = 0,
    TILE_FLOOR,
    TILE_STAIRS_DOWN,
    TILE_STAIRS_UP,
    TILE_SHOP_GENERAL,
    TILE_SHOP_ARMORY,
    TILE_SHOP_APOTHECARY,
    TILE_TEMPLE_ENTRANCE,
    TILE_DECOR,
    TILE_WATER,
    TILE_BRIDGE,
    TILE_LAVA,
    TILE_MIASMA,
    TILE_PORTAL,
    TILE_LOCKED_DOOR,
    TILE_QUEST_BOARD,
    TILE_SHOP_ARCANIST,
    TILE_SEALED_DOOR, /* opened only by pulling the floor's lever, never by a key */
    TILE_LEVER,
    TILE_INN,       /* town: rest, and the point you come back to if you die */
    TILE_TAVERN,    /* town: hire heroes to come down with you */
    TILE_JUNKYARD,  /* town: weighs the scrap you dragged back up */
    TILE_GLADIATOR, /* town: trains one of the thirty, for coin */
    /* Appended, never inserted: these values live in save files.
       Dense growth -- jungle undergrowth, swamp reeds. Blocks movement and
       sight both, which is what makes a jungle district feel unlike a room
       full of furniture. */
    TILE_THICKET,
    TILE_BANK,      /* town: sells a share of everything you will ever earn */
    /* Aether-charged crystal. Blocks the way but not the view -- you can see
       straight through a district of them and still not walk through it,
       which is the opposite of thicket and reads very differently. */
    TILE_CRYSTAL,
    TILE_ROD,       /* storm-cage: a metal spire, and a lightning target */
    /* Running water in a cut channel. Walkable, and it carries whatever is
       standing in it along the district's flow at the end of every turn. */
    TILE_CURRENT,
    /* Blood marsh: shallow, walkable, and it knits anything standing in it
       back together. The first terrain in the game that helps them. */
    TILE_BLOODPOOL,
    /* Chromatic abyss: liquid light. Standing on one is a stance -- the only
       terrain that rewards being somewhere rather than avoiding it. */
    TILE_PRISM_RED,     /* hit harder, fold faster */
    TILE_PRISM_BLUE,    /* hold, but not hurt */
    TILE_PRISM_GREEN,   /* mend, slowly */
    /* Assembly line: a moving belt. Lanes alternate direction, so the
       district is navigated *with* the belts rather than despite them. */
    TILE_BELT,
    /* A snare: teeth in a flower bed, or sand that will not let go. You can
       walk into one and they cannot -- which is the trade, because walking in
       also means you are not walking out for a turn or two. */
    TILE_SNARE,
    /* A hole in the mine floor. Walkable on purpose -- stepping into one is
       the decision, not an accident -- and it drops you a floor. Appended,
       never inserted: a floor's tiles are saved with it. */
    TILE_PIT,
    /* An ore vein. Worked with `g`, and the only thing in the game that
       yields the material the deep upgrade rungs demand. */
    TILE_ORE,
    TILE_RACES,     /* town: the lizard track */
    TILE_KITCHEN,   /* town: the pub you cook for */
    TILE_BLACKMARKET,/* town: buys levels off you, no questions */
    TILE_ORACLE,    /* town: sells a true answer about what is below */
    TILE_ALTAR,     /* town: trades one attribute point for another */
    TILE_BAZAAR,    /* town: the same goods, at today's price */
    /* A geothermal vent. The Storm-Cage's rod with different scenery: it is
       struck on a timer, it kills what is beside it, and it can be worked for
       what has crusted around the mouth. Appended, never inserted. */
    TILE_VENT
} TileType;

/* The six schools of magic. Each maps to one of the "magic-coded" attributes
   that otherwise did little on their own -- see spells.c for the mapping
   from attribute to school and the effects each school specializes in.
   Kept here (rather than spells.h) so common.h's Player struct can size
   arrays by it without a circular include. */
typedef enum {
    SCHOOL_CONDUIT,      /* offense -- bolts, blasts, scorched ground */
    SCHOOL_RESONANCE,    /* enchantment -- attack/defence/crit buffs */
    SCHOOL_WARDING,      /* protection -- shields, barriers, purging hazards */
    SCHOOL_AETHER_SENSE, /* utility -- blink, reveal, conjured bridges */
    SCHOOL_SCRIBING,     /* control -- stun, slow, unbinding locks */
    SCHOOL_JINX,         /* death -- drain, curses, fear, decay */
    SCHOOL_COUNT
} SchoolId;

/* Every class also gets one innate active ability, free (no charge/ammo
   cost, just a cooldown), auto-assigned from whichever physical/combat
   attribute it leans highest toward -- the non-magic counterpart to
   magic_school. Order matters: index i is driven by CANDIDATE_ATTRS[i] in
   classes.c (Might/Brawn/Agility/Reflexes/Precision/Grit/Fortitude/
   Stamina/Balance/Instinct, in that order). */
typedef enum {
    ABILITY_CRUSHING_BLOW,   /* Might: a single heavy strike */
    ABILITY_ADRENALINE,      /* Brawn: instant self-heal */
    ABILITY_EVASIVE_ROLL,    /* Agility: temporary evasion buff */
    ABILITY_RIPOSTE,         /* Reflexes: temporary attack buff */
    ABILITY_CALLED_SHOT,     /* Precision: a guaranteed heavy hit */
    ABILITY_UNBREAKABLE,     /* Grit: temporary defence buff */
    ABILITY_IRON_RESOLVE,    /* Fortitude: a bigger self-heal */
    ABILITY_SECOND_WIND,     /* Stamina: heal plus a brief attack buff */
    ABILITY_STEADY_FOOTING,  /* Balance: evasion plus defence buff */
    ABILITY_PREDATORS_SENSE, /* Instinct: stuns the nearest foe and reveals nearby */
    ABILITY_COUNT
} AbilityId;

/* A player only ever knows a handful of spells even though the world pool
   is in the hundreds -- same pattern as INV_CAP for inventory. */
#define MAX_KNOWN_SPELLS 30

typedef struct {
    TileType type;
    bool visible;
    bool seen;
} Tile;

typedef struct {
    char name[32];
    int x, y;
    int hp, maxhp;
    int atk, def;
    int xp_reward, gold_reward;
    bool alive;
    bool aggro;
    bool is_boss;
    bool is_elite;
    char glyph;
    int color_pair;
    int jinx_atk_penalty;   /* temporary, applied by a player's Jinx attribute */
    int jinx_turns_left;
    int stun_turns_left;    /* can't move or attack while >0 -- Scribing Stun / Jinx Fear spells */
    int slow_turns_left;    /* 50% chance to skip its turn while >0 -- Scribing Slow spells */
} Monster;

typedef struct {
    int x, y;
    bool used;
    bool is_gold;
    int gold_amount;
    char name[40];
    int heal;
    int atk_buff, atk_buff_turns;
    int def_buff, def_buff_turns;
    bool is_recall;
    bool is_key;
    bool is_quest_item; /* the fetch-quest's target; picking it up doesn't complete the quest by itself, see quest_item_found */
    /* Scrap: a separate kind of thing, not a variant of the others. Worth
       nothing until it is carried back up and sold, and worth nothing else
       ever -- it cannot be drunk, worn or thrown. */
    bool is_junk;
    int  junk_value;
} FloorItem;

typedef struct {
    char name[40];
    int heal;
    int atk_buff, atk_buff_turns;
    int def_buff, def_buff_turns;
    int perm_maxhp;
    bool is_recall;
    int count;
    int heal_pct;      /* share of max HP restored, on top of `heal` */
} InvStack;

/* ---- hired companions ------------------------------------------------
   Heroes taken on at the Tavern. They are not a pet system: nobody equips
   them, nobody orders them around. They walk the floor on their own, fight
   what they find, and level off their own kills. */
#define MAX_COMPANIONS   5    /* plus the player: a party of six */
#define TAVERN_ROSTER    20   /* candidates on offer at any time */
/* How wide the Tavern list renders a name. Name generation honours it so a
   hero's honorific is never the thing that gets cut off. */
#define TAVERN_NAME_SHOWN 24

typedef enum {
    PERSONALITY_BOLD = 0,   /* ranges furthest, engages first */
    PERSONALITY_STEADY,     /* stays near the party, fights what comes */
    PERSONALITY_CAUTIOUS,   /* hangs back, needs a real reason to close */
    PERSONALITY_COUNT
} Personality;

/* Something a companion turned up down there. Companions have no inventory
   and no equip screen -- the bonus is folded into their stats the moment
   they pick it up, and this is the record of what and why, for the Tavern
   list to show. */
#define COMPANION_GEAR_SLOTS 3
typedef struct {
    char name[28];
    int atk, def, hp;
} CompanionGear;

/* A healing draught a companion picked up off the floor and is keeping for
   itself. Only healing ones: they have no buff or status model to spend the
   others on, so anything else they find is handed to the player instead of
   quietly disappearing into a pocket. */
#define COMPANION_POTION_SLOTS 3

/* How many spells a hired caster carries. Three is enough for a rotation --
   something big, something cheap, something for when the big one is on
   cooldown -- without turning a hire into a second spellbook to read. */
#define COMPANION_SPELL_SLOTS 3
typedef struct {
    char name[24];
    int heal;
    int heal_pct;
} CompanionPotion;

/* The Companion struct used to stand here: a hire, carved down to the fields
   somebody thought a follower needed. There is one body struct now -- see
   Hero, below, just above the party it lives in. */

typedef enum {
    FEATURE_SHRINE,
    FEATURE_FOUNTAIN,
    FEATURE_MERCHANT,
    FEATURE_MACHINE,
    FEATURE_RELIC,
    FEATURE_TOWN_GATE,
    /* Appended, never inserted: these values go into save files, so
       renumbering the ones above would make an old save describe a
       different dungeon. */
    FEATURE_ALTAR,   /* every 10th floor: health for a run of doubled gold */
    /* The toll: everything in your purse, and they walk you to the stairs.
       Appended, never inserted -- these values reach save files. */
    FEATURE_TOLL,
    /* Assembly line: the control console. Three keys and it builds you
       something to stand in front of you. */
    FEATURE_CONSOLE,
    /* The watch: what the wardens are posted around. Worth opening only
       while they are still unaware of you. Appended, never inserted. */
    FEATURE_STRONGBOX
} FeatureType;

/* Quest board bounty types. KILL is the original -- slay a specific named
   monster on a specific floor. FETCH sends the player after an item
   planted on a floor, which must then be brought back to the board.
   CLEAR asks for a kill count anywhere on one floor, not a specific
   monster -- "thin the place out" rather than "hunt this one down". */
/* Appended, never inserted: a bounty is saved with the run.
 *
   TIMED, NORECALL and ESCORT are the three ROADMAP Phase 3 asked for. The
   first two "need no new machinery and slot straight into the existing switch"
   -- which was true. ESCORT carried "blocked on a friendly-NPC actor that does
   not exist" from the day it was written; hired heroes are that actor, and
   have been since a companion could exist without a roster seat. */
typedef enum {
    QUEST_KILL, QUEST_FETCH, QUEST_CLEAR,
    QUEST_TIMED,      /* reach a floor before the clock runs out */
    QUEST_NORECALL,   /* reach it without taking the easy way home */
    QUEST_ESCORT,     /* get somebody else there alive */
    QUEST_TYPE_COUNT
} QuestType;

/* Dungeon-only gear slot a relic fills. Split into explicit RING/TRINKET
   (rather than one generic ACCESSORY that lands wherever's empty) because
   a 4-piece gear set needs to reliably fill both accessory slots, not
   whichever happens to be free. */
typedef enum { RELIC_WEAPON, RELIC_ARMOR, RELIC_RING, RELIC_TRINKET, RELIC_KIND_COUNT } RelicKind;

/* Ranged weapon categories -- a distinct tactical role each, not just a
   damage-number reskin. RANGED_NONE means no ranged weapon equipped. */
typedef enum {
    RANGED_BOW,
    RANGED_GUN,
    RANGED_LASER,
    RANGED_BLOWGUN,
    RANGED_THROWN,
    RANGED_GRENADE,
    RANGED_NONE
} RangedType;

typedef struct {
    FeatureType type;
    int x, y;
    bool used;
    int relic_kind;   /* RelicKind -- which gear slot this relic fills */
    int relic_set_id; /* Biome this relic's gear set belongs to -- every dungeon relic is part of one;
                          gearsets.c looks the actual name/bonus up fresh from this pair on pickup */
} MapFeature;

typedef enum {
    BIOME_JUNGLE,
    BIOME_INDUSTRIAL,
    BIOME_RUINS,
    BIOME_WASTES,
    BIOME_ABYSS,
    BIOME_COUNT
} Biome;

typedef enum {
    DIFFICULTY_NORMAL,
    DIFFICULTY_HARD,     /* a step up from Normal: 2-3x density, still fair aggro */
    DIFFICULTY_SWARM,
    DIFFICULTY_HARDCORE, /* Swarm's density and aggression, plus no respawns and no recall */
    DIFFICULTY_COUNT
} Difficulty;

/* What a ring or trinket can boost. ACC_NONE means "no accessory here". */
typedef enum {
    ACC_CRIT,
    ACC_EVASION,
    ACC_WARD,
    ACC_GOLD,
    ACC_XP,
    /* Regeneration: HP restored per turn. The one accessory that answers the
       game's actual long-run problem -- there is no passive healing, so past
       the mid floors a bar you can't refill is what ends runs. Priced to be
       a late-game capstone rather than a purchase. */
    ACC_REGEN,
    ACC_NONE
} AccessoryStat;

/* The 30-attribute character system. Every class gets a value 1-10 (3 is
   "average person") for each; classes.c derives the actual gameplay
   numbers (crit chance, evasion, etc.) from these once at character
   creation. */
/* What the temple gives up, by depth. Appended never inserted -- these index
   arrays that live in save files. */
typedef enum {
    MAT_SCRAP = 0,   /* the Jungle and the Works */
    MAT_PLATINUM,    /* the Ruins and the Wastes */
    MAT_DIAMOND,     /* the Abyss, and nowhere else */
    MAT_COUNT
} MaterialTier;

/* What comes off a body worth cooking. Appended never inserted -- these index
   the larder array, which lives in save files. */
typedef enum {
    MEAT_STRINGY = 0,   /* vermin, and anything from the first floors */
    MEAT_FAIR,
    MEAT_PRIME,
    MEAT_MYTHIC,        /* bosses, and only bosses */
    MEAT_GRADE_COUNT
} MeatGrade;

const char *material_name(int tier);
/* Which material a floor yields. One place, so the accrual, the Junkyard and
   the upgrade gate cannot disagree about where diamond starts. */
int material_tier_for_floor(int floor_num);

/* The rung above which an upgrade stops taking scrap and starts wanting
   something you can only dig up further down. */
#define UPGRADE_PLATINUM_FROM 20
#define UPGRADE_DIAMOND_FROM  50

typedef enum {
    ATTR_MIGHT, ATTR_BRAWN, ATTR_AGILITY, ATTR_REFLEXES, ATTR_PRECISION,
    ATTR_GRIT, ATTR_FORTITUDE, ATTR_STAMINA, ATTR_BALANCE, ATTR_VISION,
    ATTR_HEARING, ATTR_INTELLECT, ATTR_CUNNING, ATTR_MEMORY, ATTR_RESOLVE,
    ATTR_INSTINCT, ATTR_CHARM, ATTR_GUILE, ATTR_PRESENCE, ATTR_EMPATHY,
    ATTR_TECH_WIT, ATTR_CRAFTING, ATTR_CHEMISTRY, ATTR_SCRIBING, ATTR_AETHER_SENSE,
    ATTR_CONDUIT, ATTR_RESONANCE, ATTR_WARDING, ATTR_FORTUNE, ATTR_JINX,
    ATTR_COUNT
} Attribute;

/* The wild district kinds. Mirrored by mapgen.c's carve table; appended
   never inserted, because a floor's districts are saved with it. */
typedef enum {
    DIST_JUNGLE = 0,
    DIST_SEA,
    DIST_SWAMP,
    DIST_RUINS,
    DIST_MYCELIUM,   /* growth that carries sound; stillness hides you */
    DIST_HIVE,       /* hurt one and they all know */
    DIST_MIRE,       /* mist: you see three tiles */
    DIST_CRYSTAL,    /* spells and shot ricochet off the crystals */
    DIST_QUIET,      /* nothing is awake until you start something */
    DIST_STORM,      /* lightning strikes the rods; stand clear, or aim it */
    DIST_ARENA,      /* step onto the sand and the gates shut behind you */
    DIST_AQUEDUCT,   /* channels that carry you -- the fast way, not your way */
    DIST_BLOODMARSH, /* the water mends them, not you */
    DIST_PRISM,      /* coloured ground: pick where you fight */
    DIST_ASSEMBLY,   /* belts that carry you, alternating lane by lane */
    DIST_GARDEN,     /* flowers with teeth; the safe path runs through pollen */
    DIST_QUICKSAND,  /* it holds longer and hurts less, and they will not follow */
    DIST_WATCH,      /* they are awake and watching; start nothing and take the box */
    DIST_GAUNTLET,   /* the runs you have already lost, waiting for you */
    DIST_EYE,        /* one quarter is safe at a time, and it moves */
    DIST_MIRROR,     /* it makes a smaller copy of you and sets it walking */
    DIST_PETRIFIED,  /* stone trees; what you cannot see gets the first blow */
    DIST_SHAFT,      /* holes in the floor: a floor skipped, paid for in blood */
    DIST_BONEYARD,   /* dead golems, and time to spend cracking one open */
    DIST_PROVING,    /* fight their way, or do not fight here */
    DIST_CHAPEL,     /* the silence takes your ears, and theirs */
    DIST_GEOTHERMAL, /* the rod field, in stone */
    DIST_KIND_COUNT
} DistrictKind;

/* `Map.wild_kinds` is a 32-bit mask indexed by DistrictKind, so the 33rd kind
   would shift by 32 -- undefined behaviour, not a truncated answer, and
   nothing would say so at runtime. Twenty-seven kinds today. Whoever adds the
   thirty-third gets a compile error and a choice, which is the whole point of
   putting this here rather than finding out from a floor that forgot which
   districts it had. */
_Static_assert(DIST_KIND_COUNT <= 32,
               "wild_kinds is a 32-bit mask -- widen it before adding more district kinds");

/* The three rules the districts above apply while you are standing in them. */
#define MYCELIUM_HEAR_RADIUS   20   /* footfalls carry this far through the mats */
#define MYCELIUM_FORGET_TURNS   2   /* stand still this long and they lose you */
#define HIVE_ALARM_RADIUS      20   /* hurt one, and this far around finds out */
#define MIRE_FOV_RADIUS         3   /* mist: you see this far, whatever your Vision */
#define CRYSTAL_RICOCHET_PCT   30   /* chance a cast or a shot comes back at you */
#define QUIET_AGGRO_RADIUS      3   /* they are not watching for you -- yet */
#define STORM_STRIKE_EVERY      5   /* turns between bolts */
#define STORM_BLAST_RADIUS      2   /* how close to a struck rod is too close */
#define STORM_DAMAGE           10

/* The arena. Ten waves; the gates open again after five, so pressing on is a
   choice rather than a sentence, and only the tenth pays the real prize. */
#define ARENA_WAVES            10
#define ARENA_FREE_AFTER        5
#define ARENA_BASE_ENEMIES      3
#define CURRENT_PUSH            3   /* tiles the water carries you each turn */
#define BLOODPOOL_REGEN         5   /* HP a monster standing in it recovers */
#define PRISM_SWING            50   /* per-cent the coloured ground swings a stat */
#define PRISM_REGEN             3   /* HP the green light gives back per turn */
#define BELT_PUSH               2   /* tiles a belt moves you each turn */
/* One snare, two temperaments: the garden bites hard and briefly, the sand
   barely hurts and will not give you back. */
#define SNARE_GARDEN_DAMAGE     8
#define SNARE_GARDEN_HOLD       2
#define SNARE_SAND_DAMAGE       3
#define SNARE_SAND_HOLD         4
#define CONSOLE_KEY_COST        3   /* keys to start the line up again */

/* The watch. The one district in the game where the answer is not to fight.
 *
   Its wardens are awake -- that is what makes it different from the quiet
   quarter, where nothing is -- but they are watching for trouble rather than
   hunting, so they notice you at arm's length and no further. Hurt one and
   every warden in the district knows, permanently, and the strongbox is not
   worth opening any more.

   The prize is a permanent gold-find increase rather than a purse, because
   §3.1's rule 3 is explicit that flat coin is flavour: a full descent earns
   ~438,000 against the ~200M a floor-100 build costs, so a pickup that is
   meant to *matter* has to be multiplicative, and a permanent % gold find is
   one of the two shapes that rule names. It grows linearly with how many
   watches you walk rather than compounding like a bank share, which is what
   makes it safe to hand out once per district.

   It lands on the *body* that crossed, not on the party, which is the shared/
   not-shared line drawn where take-control already draws it: the purse is the
   party's, and knowing where to look is yours. */
#define WATCH_AGGRO_RADIUS      2   /* they are watching, not hunting */
#define WATCH_FIND_PCT          5   /* permanent gold find, per box carried out */
#define WATCH_FIND_CAP         60   /* and no further, however many you walk */

/* The gauntlet. The runs you have already lost, standing between you and the
   way out.
 *
   Structurally the arena's twin -- step in, it shuts, clear it, it opens --
   and deliberately so: that shape is built, played and understood. What is
   different is where the enemies come from. These are read off the fallen
   roster in save.h, so the thing walking at you carries the name, the class,
   the level and the purse of a character who actually died on floor 47.

   The reward is the gold that ghost was carrying and nothing else. Not an
   attribute point: §3.1 says four separate times that districts pay in School
   credit or in nothing, because more than a third of the hundred candidates
   wanted to hand out permanent points and the Gladiator School charges
   200v^2 + 300 for one.

   GAUNTLET_MIN_GHOSTS is why the district can fail to place: a barrow with
   nobody in it is a room. Below that many dead runs on file, mapgen picks
   something else. */
#define GAUNTLET_MAX_GHOSTS     5
#define GAUNTLET_MIN_GHOSTS     1

/* The eye of the storm. The rod field's system with the polarity reversed:
   there, one tile is about to be dangerous and you stand clear of it; here,
   one quarter is safe and everything else is not, so you are not dodging a
   hazard but riding a moving shelter.
 *
   The safe quarter is *derived* from the floor's turn counter rather than
   stored -- (turns_on_floor / EYE_ROTATE_EVERY) % 4 -- so it costs a district
   nothing to remember, it saves and reloads correctly for free, and it cannot
   drift out of step with itself. It also means the player can learn the
   rhythm, which is the whole game here. */
#define EYE_ROTATE_EVERY        8   /* turns the shelter stays put */
#define EYE_DAMAGE              7   /* per strike, to anything caught out */
#define EYE_STRIKE_EVERY        3   /* turns between strikes */

/* The mirror. A copy of you at half your health and half your damage --
   deliberately *not* at parity, because a mirror match at parity is a coin
   flip that ignores every build decision the player made, which is the
   opposite of what a mirror is for (ROADMAP 3.1, on candidate 51). */
#define MIRROR_HP_PCT          50
#define MIRROR_DAMAGE_PCT      50

/* The stone wood. The one rule kept from Petrified Forest: everything else in
   that candidate was a thicket reskin, and the thicket already exists.
 *
   What you cannot see hits first and hits twice. It reads the tile's own
   visible flag rather than remembering anything per monster: Monster is 92
   bytes and there can be 600,000 of them, so a field here costs megabytes to
   express something the map already knows. */
#define AMBUSH_EXTRA_BLOWS      1

/* The shaft. A hole you can choose to go down: it costs health scaled to the
   depth you are at and it skips the rest of the floor.
 *
   ROADMAP kept the shafts and dropped the "Listen action to reveal them"
   until multi-turn actions existed. They exist now, and the listening is
   still not built -- a hidden hole that damages you for finding it by
   accident is a trap, and the interesting version is the one you can see and
   step into on purpose. So they are visible, and the decision is whether the
   floor below is worth the fall.

   The damage is a percentage of maximum rather than a flat number so that it
   stays a real question at depth instead of decaying into free travel. */
#define PIT_FALL_HP_PCT        18
#define PIT_MIN_DAMAGE          6

/* The boneyard. Ten turns inside a dead golem, fully vulnerable throughout --
   the salvage half of Clockwork Boneyard, which is the half ROADMAP said to
   keep. The pressure-plate labyrinth it also wanted is the lever-and-sealed-
   door puzzle that already exists.

   No new tile: rubble in a boneyard is a machine worth stripping, exactly as
   rubble in an old quarter already is. */
#define WORK_CORE_TURNS        10

/* ---- the proving ground ------------------------------------------------
 * §3.1's candidates 76, 78 and 80 were three districts and one mechanic: a
 * fight with part of your kit taken away. Duelling Grounds is melee only,
 * Arcane Duel is spells only, Gladiator Gauntlet locks your weapon. Built
 * once with three skins rather than three times, which is what that section
 * asked for.
 *
 * It is the only thing in the hundred that attacks *build identity*. Every
 * other district changes the ground; this one changes what you are allowed to
 * be while you stand on it, and a hundred classes that all funnel into one
 * loadout you never change is exactly the complaint it answers.
 *
 * The restriction is rolled once when the district is carved and kept in
 * MapDistrict.state, low bits, so it is a property of the place and not of
 * the turn -- you can be told what it is on arrival and plan around it.
 */
#define PROVE_KIND_MASK   0x0f
#define PROVE_MELEE       0   /* blades only: no casting, no shooting */
#define PROVE_ARCANE      1   /* casting only: no blades, no shooting */
#define PROVE_UNARMED     2   /* your weapon counts for nothing here */
#define PROVE_KIND_COUNT  3

/* Kills made inside under the restriction, and whether the writ has been
   paid. Packed above the kind because a district may hold only one int and
   this is what the arena would have needed if it had been written second. */
#define PROVE_KILLS_SHIFT 8
#define PROVE_PAID_BIT    0x80
#define PROVE_KILLS_WANTED 6

static inline int proving_kind(int state)  { return state & PROVE_KIND_MASK; }
static inline int proving_kills(int state) { return state >> PROVE_KILLS_SHIFT; }
static inline bool proving_paid(int state) { return (state & PROVE_PAID_BIT) != 0; }

/* What the ground under (x,y) forbids. Returns true when the action is
   allowed, which is the common answer and the one worth reading quickly.
   `what` is one of PROVE_MELEE / PROVE_ARCANE / PROVE_RANGED_ACT below.

   Defined here rather than in combat.c because five callers in four files ask
   it -- the player's three ways of hurting something, the AI's, and
   auto-explore's decision. A restriction that only some of them respected
   would be worse than none: it would be a rule the player obeys and their
   hires do not. */
#define PROVE_MELEE_ACT   0
#define PROVE_ARCANE_ACT  1
#define PROVE_RANGED_ACT  2
/* Defined below district_index_at(), which it needs and which needs Map. */

/* The lizard track. Six runners, and the book is generous early and mean
   late: the first few races of a visit pay over the odds, the rest under.
 *
   A runner's chance is weight/total with weight = 120/(odds+1), so every
   runner on a ladder is the same bet and the ladder's return collapses to
   one number: 1 / SUM(1/(odds_j+1)). That identity is the whole design, and
   it is easy to get backwards -- the first ladder shipped here was 2..7,
   which sums to 1.218 and so paid 0.82 per gold staked. It read as generous
   and was a money sink. Both ladders below are stated as tables rather than
   derived from a loop so the number is checked, not assumed:

     GENEROUS  3,4,6,8,10,12  ->  1/0.872 = 1.15   (a 15% edge to the player)
     SOUR      2,3,4,5,6,7    ->  1/1.218 = 0.82   (an 18% edge to the house)

   tests/invariants.c asserts both, so a future edit to either ladder that
   flips the sign of the edge fails the suite instead of the economy. */
#define RACE_RUNNERS            6
#define RACE_STAKE_MAX     1000000   /* the bookmaker's ceiling on one bet */
#define RACE_GENEROUS_RACES     3   /* how many before the book turns */

static inline void race_odds(int races_this_visit, int *out) {
    static const int GENEROUS[RACE_RUNNERS] = { 3, 4, 6, 8, 10, 12 };
    static const int SOUR    [RACE_RUNNERS] = { 2, 3, 4, 5,  6,  7 };
    const int *ladder = races_this_visit >= RACE_GENEROUS_RACES ? SOUR : GENEROUS;
    for (int i = 0; i < RACE_RUNNERS; i++) out[i] = ladder[i];
}

/* The pub. You hunt the meat in the temple and cook it upstairs.
 *
   A service night is a handful of patrons, each of whom wants a particular
   grade and pays best for it. Serving well raises the house's reputation,
   which multiplies every later payout -- so the restaurant is a second
   progression track that runs entirely on decisions, and never on your
   character level. Rate-limited per visit for the same reason the track is:
   an income you can sit and repeat is the only thing anyone would ever do. */
/* How much of one grade you can carry, by level. A flat 99 per grade meant a
   level-2 character could haul as much as a level-60 one, and it read as an
   arbitrary wall rather than a capacity. Carrying is a thing that improves. */
/* How long a hire keeps walking to one frontier before re-deciding. Long
   enough to cross a room, short enough that a target which quietly became
   uninteresting is dropped. */
/* What the black market pays per point of experience handed back. Flat by
   design: the experience curve already makes deep floors pay far more per
   lap, so the price does not have to know where you have been. */
/* What a swarm monster is worth, as a fraction of an ordinary one: a tenth
   of the hit points and a tenth of the experience, from one number, so the
   payment and the thing being paid for cannot drift apart. monsters.c cuts
   the stats; combat.c's grant_xp divides the reward. */
#define SWARM_MONSTER_FRACTION 10

/* What each mode's floor 1 opens at, as a multiple of Normal's density, and
   how much denser Hardcore is than Swarm.
 *
 * These are the settled values from the sweep in EVALUATION §89, and they are
 * the numbers that put the difficulty ladder the right way up: Normal easiest,
 * then Hard, then Swarm, then Hardcore, monotone across rush, farm and
 * prestige at 100 runs a cell. They were 2, 1 and 100 before, which had Hard
 * as the hardest mode in the game and the two horde modes as the easiest
 * after Normal.
 *
 * HARDCORE_DENSITY_PCT is the only thing in the codebase that distinguishes
 * Hardcore from Swarm at generation time. Until §89 the two produced
 * byte-identical floors and differed solely in having no respawns and no
 * recall. */
#define HARD_FLOOR1_DENSITY    1
#define SWARM_FLOOR1_DENSITY   3
#define HARDCORE_DENSITY_PCT 160

/* Every balance constant above is overridable from the environment, so a
   tuning pass can walk a grid without a rebuild per point and without a dozen
   more positional arguments on the simulator -- the same idiom
   AETHER_STATE_DIR and AETHER_SIM_VERBOSE already use. The default is always
   the shipped value, so an unset environment is the real game. */
int tunable(const char *env_name, int fallback);

#define MARKET_GOLD_PER_XP 1

#define COMPANION_SCOUT_PATIENCE 40

#define MEAT_LARDER_BASE      12
#define MEAT_LARDER_PER_LEVEL  2
#define MEAT_LARDER_CEIL      99   /* the pack has a size whatever your level */
#define KITCHEN_PATRONS        7   /* per service night */
#define KITCHEN_NIGHTS_PER_VISIT 1
#define KITCHEN_REP_MAX      100
#define KITCHEN_REP_START     10

/* Meat only comes off a clean kill, and only sometimes. Swarm floors throw
   thousands of bodies at you; a high rate here would fill the larder in a
   corridor and make the grade distinction meaningless. */
#define MEAT_DROP_PCT         12

/* The expected return per gold staked, as a permille so it stays integer. */
static inline int race_return_permille(const int *odds) {
    int weight[RACE_RUNNERS], total = 0;
    for (int i = 0; i < RACE_RUNNERS; i++) {
        weight[i] = 120 / (odds[i] + 1);
        if (weight[i] < 1) weight[i] = 1;
        total += weight[i];
    }
    /* Same for every runner; runner 0 is representative. */
    return weight[0] * (odds[0] + 1) * 1000 / total;
}

/* How far out the world actually thinks.
 *
   Every monster used to take a turn every turn, including ones several
   hundred tiles away that had never seen the player -- a random walk nobody
   could observe, paid for on every keypress. On a Well floor that is 5,800 of
   them.

   Chosen to sit well outside everything that reaches beyond sight: a field of
   view of 7, an aggro radius of 8, and the 20-tile mycelium and hive senses.
   Nothing within reach of any rule is ever asleep, so this is invisible in
   play and the difference is entirely in what is *not* simulated. */
#define MONSTER_ACTIVE_RADIUS  40
/* Something already hunting you keeps hunting further out -- but a pursuit
   from half a map away is theatre for an audience of nobody. */
#define MONSTER_LEASH_RADIUS  120

/* Multi-turn work. Each entry is (turns it costs) and the tile it is done on.
   Appended, never inserted: the kind is saved mid-action. */
typedef enum {
    WORK_NONE = 0,
    WORK_HARVEST_ROD,   /* a storm rod, for the conductive ore in it */
    WORK_SALVAGE,       /* a collapsed machine in the ruins */
    WORK_MINE,          /* an ore vein: the only source of deep material */
    WORK_CORE,          /* a dead golem: ten turns inside it, and no guard */
    /* The vent field's version of the rod: same turns, same reward, different
       words. A separate kind rather than a branch on the tile because
       work_verb() and the completion message only ever see the kind, and the
       alternative was threading the tile through both -- for two strings.
       Appended, never inserted: `work_kind` rides in the save. */
    WORK_HARVEST_VENT
} WorkKind;

#define WORK_ROD_TURNS      5
#define WORK_SALVAGE_TURNS 10
#define WORK_MINE_TURNS     8


typedef struct {
    int x, y, w, h;
    int kind;        /* DistrictKind */
    /* Which way the water runs, for DIST_AQUEDUCT. One direction for the
       whole district rather than a per-tile field: a channel that changed
       its mind every few tiles would be noise, and Tile has no room to spare
       at 1.1 million of them. */
    int flow_dx, flow_dy;
    /* Whatever this kind needs to remember about itself between turns, in
       one field rather than one field per kind: 220 districts fit in a Map
       and a per-kind union would cost every floor the size of the widest.
       DIST_WATCH stores whether the watch has been broken. Kinds that need
       nothing leave it at 0, which is what mapgen zeroes it to. */
    int state;
} MapDistrict;

/* DIST_WATCH's use of MapDistrict.state. */
#define WATCH_KEEPING  0   /* nobody has started anything yet */
#define WATCH_BROKEN   1   /* you did, and they will not forget it */
#define WATCH_TAKEN    2   /* the box is open and the prize is paid */

typedef struct {
    Tile tiles[MAP_H_MAX][MAP_W_MAX];

    Monster monsters[MAX_MONSTERS_MAX];
    int monster_count;

    FloorItem items[MAX_FLOOR_ITEMS_MAX];
    int item_count;

    MapFeature features[MAX_FEATURES_MAX];
    int feature_count;

    int stairs_down_x, stairs_down_y;
    int stairs_up_x, stairs_up_y;
    int floor_num; /* 0 = town */
    Biome biome;
    int turns_on_floor; /* drives periodic monster respawns */

    /* Hard only: this floor rolled the 1-in-8 and generated at Swarm density
       instead of Hard's usual 2-3x, and keeps respawning hard while you're on
       it. Stored rather than re-rolled because a resumed run has to come back
       to the same floor it left. */
    bool overrun;
    /* A gold rush floor: same monsters, same danger, twenty times the coin
       lying on it. The counterpart to overrun -- one floor in eight that is
       worth going out of your way for rather than one that is worth
       avoiding. */
    bool gold_rush;

    /* How many of the three dividing lines survived on this floor. 0 on the
       small worlds, where the floor is crossed in minutes and quartering it
       would be a wall in a corridor. Recorded so the arrival note can mention
       it and so a test can tell "not attempted" from "attempted and rolled
       back". */
    int quarter_lines;

    bool has_portals;
    int portal_ax, portal_ay, portal_bx, portal_by;

    /* At most one lever-and-sealed-door pair per floor. lever_x/y is -1
       when this floor has none. */
    int lever_x, lever_y;
    int lever_door_x, lever_door_y;

    /* The population an infested floor refills toward -- set from the living
       count on the first respawn tick, so it is whatever mapgen actually
       produced rather than a second copy of the density formula. Appended at
       the end of the Map's own additions; Map is not saved field-by-field. */
    int infest_target;

    /* The room the waygate stands in, recorded at generation because rooms
       are local to mapgen and nothing else keeps them. Auto-explore stops
       when you are *in the room*, not within some radius of the tile: a
       radius is a number I would have had to pick, and "the room with the Y
       in it" is what a player actually sees. w is 0 when this floor has no
       gate. */
    int waygate_room_x, waygate_room_y, waygate_room_w, waygate_room_h;

    /* Already stopped you here once. One announcement per floor: a delegated
       walk that argued with you every time it re-entered the room would be
       worse than one that missed it. */
    bool waygate_noticed;

    char event_name[48];
    char event_desc[96];

    /* A camp: somewhere down here that somebody walled off and held. Nothing
       spawns inside it and nothing respawns into it, and there is usually a
       trader and clean water. haven_w == 0 means this floor has none.
       Appended to the struct, so SAVE_VERSION moves with it. */
    int haven_x, haven_y, haven_w, haven_h;
    bool haven_entered;

    /* Bitmask of the wild districts this floor generated, so arriving on one
       can say so -- you should be told there is a bog on the floor before
       you walk into it, the same way you are told about an overrun. */
    /* One bit per district kind that turned up on this floor. See the guard
       below DistrictKind: at 32 kinds this silently becomes undefined
       behaviour rather than a wrong answer, which is the worst of both. */
    unsigned wild_kinds;

    /* Where they are, not just that they exist. A district with a *rule*
       -- growth that carries sound, mist that shortens your sight -- has to
       know whether you are standing in it, which the bitmask cannot answer. */
    MapDistrict districts[MAX_DISTRICTS_MAX];
    int district_count;

    /* The storm-cage's next bolt: which rod, and how many turns until it
       lands. Telegraphed one turn ahead, because a hazard you cannot see
       coming is a dice roll rather than a decision. -1 when this floor has
       no storm district. */
    int storm_x, storm_y, storm_countdown;
    /* Which skin is about to go off -- the Storm-Cage's rod or the vent
       field's mouth. Only the wording and the target tile differ; see
       districts_tick(). */
    bool storm_is_vent;


    /* The arena, if this floor has one. `arena_wave` is 0 before it starts,
       1..ARENA_WAVES while it runs, and ARENA_WAVES+1 once it is finished
       and paid out. `arena_district` is the index into districts[], so two
       arenas on one floor stay separate (EVALUATION §44). */
    int arena_district, arena_wave;

    /* The gauntlet, same three fields and the same meanings -- see the arena
       above. Kept separate rather than generalised into one "wave fight"
       block because a floor can hold one of each, and a player who walked
       out of the arena at wave six should not find the barrow already spent. */
    int gauntlet_district, gauntlet_wave;
    bool gauntlet_paid;
    bool arena_paid;
} Map;

/* Which wild district (a DIST_* kind) covers this tile, or -1 for none.
   Hot: called once or twice a turn, and once per monster in the hive rule,
   so it stays a linear scan over a handful of rectangles rather than another
   map-sized grid. */
/* What is worth doing on this tile, or WORK_NONE. One place, so the key
   handler and the resolver cannot disagree about what is workable. */
int work_available_at(const Map *m, int x, int y);
int work_turns_for(int kind);
const char *work_verb(int kind);

/* Which way the belt under (x,y) runs, or 0,0 if it is not a belt.
 *
   Lane direction is derived from the lane's index rather than stored, so two
   hundred belt tiles cost nothing to remember and a lane cannot end up
   half-reversed. Lanes are carved every three rows, so (y - district top) / 3
   is the lane number and its parity is its direction. */
static inline void belt_flow_at(const Map *m, int x, int y, int *out_dx, int *out_dy);

static inline int district_index_at(const Map *m, int x, int y) {
    for (int i = 0; i < m->district_count; i++) {
        const MapDistrict *d = &m->districts[i];
        if (x >= d->x && x < d->x + d->w && y >= d->y && y < d->y + d->h)
            return i;
    }
    return -1;
}

static inline int district_at(const Map *m, int x, int y) {
    int i = district_index_at(m, x, y);
    return i < 0 ? -1 : m->districts[i].kind;
}

/* The proving ground's rule, in one place. See the block above for why it is
   here and not in combat.c. */
static inline bool proving_allows(const Map *m, int x, int y, int what) {
    int i = district_index_at(m, x, y);
    if (i < 0 || m->districts[i].kind != DIST_PROVING) return true;
    switch (proving_kind(m->districts[i].state)) {
        case PROVE_MELEE:   return what == PROVE_MELEE_ACT;
        case PROVE_ARCANE:  return what == PROVE_ARCANE_ACT;
        /* Unarmed keeps both close verbs and takes the *weapon* away, in
           hero_eff_atk_on(). It still refuses the bow, and the suite is what
           insisted: a skin that suppresses your blade and leaves your bow
           alone does not restrict anything, it just tells you which button to
           press. All three skins are about closing with somebody. */
        default:            return what != PROVE_RANGED_ACT;
    }
}

/* True where your weapon counts for nothing. */
static inline bool proving_unarmed_at(const Map *m, int x, int y) {
    int i = district_index_at(m, x, y);
    if (i < 0 || m->districts[i].kind != DIST_PROVING) return false;
    return proving_kind(m->districts[i].state) == PROVE_UNARMED;
}

static inline void belt_flow_at(const Map *m, int x, int y, int *out_dx, int *out_dy) {
    *out_dx = 0; *out_dy = 0;
    if (m->tiles[y][x].type != TILE_BELT) return;
    int i = district_index_at(m, x, y);
    if (i < 0 || m->districts[i].kind != DIST_ASSEMBLY) return;
    int lane = (y - m->districts[i].y) / 3;
    *out_dx = (lane % 2 == 0) ? 1 : -1;
}

/* Is (x,y) inside this floor's camp? Safe on floors that have none. */
static inline bool in_haven(const Map *m, int x, int y) {
    return m->haven_w > 0 &&
           x >= m->haven_x && x < m->haven_x + m->haven_w &&
           y >= m->haven_y && y < m->haven_y + m->haven_h;
}

/* ---- a playable body ---------------------------------------------------
 *
 * One struct for everybody. The main character is not special: they are a
 * Hero with the controller attached, and the hires are Heroes without one.
 * `Player` below is the *party* -- six of these plus the state they share.
 *
 * This was two structs once, a Player and a reduced Companion, and the split
 * produced one bug per system: the AI took a turn for the body you were
 * steering, the screen's "you" stayed pinned to the character, auto-explore
 * read the character's position while you were somebody else, the side panel
 * had no aether bar to draw for a hire. Six symptoms, one premise -- that a
 * hire is a lesser thing than a character. Patching them one at a time is
 * what produced the list; every system that said `p->` was another place the
 * premise leaked.
 *
 * The root was never really the two structs. It was two *constructors*:
 * one built a character out of a class seed, the other built a hire out of an
 * archetype tilt, and every difference downstream fell out of that. There is
 * one constructor now -- hero_create() -- and everything goes through it: the
 * character at new-game, a hire at the Tavern, a golem off the assembly line.
 *
 * Two things fall out for free, and they are the point rather than a side
 * effect: a hire is rolled with a real class instead of an archetype tilt,
 * and the character stops being the only body with attributes.
 */
typedef struct {
    /* ---- who they are ------------------------------------------------- */
    char name[32];
    int  class_id;     /* CLASS_TABLE index -- everybody is a class now */
    int  archetype;    /* Archetype, always CLASS_TABLE[class_id].archetype */
    int  personality;  /* Personality -- how the AI plays them when it does */
    int  color_pair;

    /* `in_use` says this slot holds somebody. It is what `hired` used to be,
       renamed because the character was never hired and the golem was never
       paid for, and a flag that lies about half its holders is a flag that
       gets tested wrong. Slot 0 is always in use: it is the character. */
    bool in_use;
    bool alive;
    bool temporary;    /* dismissed when the floor that made them is left */

    /* Which Tavern candidate this is, or -1 for one that was never hired --
       a golem built out of the works, or the character themselves.
       Everything that masks the roster by this must check it is >= 0 first:
       `1u << -1` is undefined behaviour, and it is the exact shape of bug
       this struct has produced before. */
    int  roster_idx;
    int  tier;         /* which price step was paid, 0..MAX_COMPANIONS-1 */

    /* ---- where they are ----------------------------------------------- */
    int  x, y;

    /* The square this body stood on last turn.
     *
       Scouting picks the nearest frontier by breadth-first search, and when
       two frontiers are the same distance away the winner can flip as the
       body moves between them -- so it walks one step toward A, finds B is
       now nearer, walks back, and repeats. Measured at fifteen per cent of
       every hire's moves. Refusing to step straight back onto the square
       just left breaks the two-cycle without needing a sticky target or a
       second search. -1 when they have not moved yet. */
    int  last_x, last_y;

    /* ---- vitals -------------------------------------------------------- */
    int  hp, maxhp;
    int  level, xp, xp_next;
    int  kills;
    int  base_atk, base_def;

    /* ---- what they carry ------------------------------------------------
       How many times the smith has been back to the equipped piece. The
       bonus itself is already folded into weapon_bonus/armor_bonus -- these
       are for the "+3" on the name, and for pricing the next one. Buying a
       different weapon starts its count over: you upgraded the old one. */
    int  weapon_bonus, armor_bonus;
    int  weapon_plus, armor_plus;
    /* The rest of the smith's counter, on the things that are not gear:
       the health bar, the aether pool, the magazine. Same rule -- the bonus
       is already folded into maxhp/aether_max/ranged_ammo_max, these are for
       the "+3" and for pricing the next one. Unlike weapon and armour these
       never reset: you cannot buy a different body. */
    int  hp_plus, aether_plus, ammo_plus;
    char weapon_name[40];
    char armor_name[40];

    char ring_name[40];
    int  ring_stat;   /* AccessoryStat */
    int  ring_bonus;
    char trinket_name[40];
    int  trinket_stat; /* AccessoryStat */
    int  trinket_bonus;

    /* Which biome-set (if any) the item in each gear slot belongs to --
       -1 if that slot holds shop gear, starting gear, or nothing. Set by
       trigger_relic()/gearsets.c on pickup, cleared on any shop purchase,
       and read by recompute_set_bonus() to work out how many pieces of a
       given set are currently worn. */
    int  weapon_set, armor_set, ring_set, trinket_set;

    /* Additive bonuses from worn set pieces -- 2 pieces of the same set
       grant set_bonus_atk/def, a full 4 pieces additionally grant one
       biome-flavored bonus (only one of the rest is ever nonzero at once,
       since wearing 4 pieces of two different sets isn't possible with
       only 4 gear slots). Recomputed from scratch by recompute_set_bonus()
       any time gear changes -- never incremented/decremented in place. */
    int  set_bonus_atk, set_bonus_def;
    int  set_bonus_evasion, set_bonus_ward, set_bonus_hazard_resist, set_bonus_crit;

    /* Which biome's full four pieces are worn, or -1. Recomputed alongside the
       flat bonuses above and never set by hand.
     *
       The flat bonuses are what a set is worth on the character sheet. This is
       what it *does*: a set of four carries a proc, and the proc is the reason
       to finish a set rather than wear the best four pieces you happen to
       have. Deferred twice while the damage model was in doubt (ROADMAP Phase
       3); the model is measured and guarded now, so the procs are safe to
       hang off it. Only one set can ever be complete -- there are four gear
       slots -- so this is one value rather than a mask. */
    int  set_complete;

    /* Something this body turned up down there while the AI had it. There is
       no equip screen for a hire -- the bonus is folded into their stats the
       moment they pick it up, and this is the record of what and why, for the
       Tavern list to show. */
    int  gear_count;
    CompanionGear gear[COMPANION_GEAR_SLOTS];
    int  potion_count;
    CompanionPotion potions[COMPANION_POTION_SLOTS];

    /* ---- the thirty ---------------------------------------------------- */
    int  attrs[ATTR_COUNT];

    /* Derived once by hero_create() from attrs[]; everything else in the
       game reads these rather than the raw attributes. */
    int  fov_radius;
    int  hearing_radius;
    int  evasion_pct;
    int  crit_pct;
    int  ward_pct;
    int  hazard_resist_pct;
    int  xp_bonus_pct;
    int  gold_bonus_pct;
    int  shop_discount_pct;
    int  potion_potency_pct;
    int  gear_bonus_pct;
    int  buff_duration_pct;
    int  aggro_reduction;
    int  ambush_resist_pct;
    int  recall_turns;
    int  machine_luck_pct;
    int  fortune_luck_pct;
    int  jinx_pct;
    int  relic_quality_bonus;
    bool scribing_reveal;
    bool aether_reveal;
    int  spell_power_pct;   /* bonus spell damage/effect, from Resonance */

    /* ---- magic ----------------------------------------------------------
       Known spells are stored as indices into the global spell pool (see
       spells.c), parallel to spell_cd by slot -- same shape as the inventory
       array, since the pool itself is far too large to track a flag per
       possible spell. A hire is given COMPANION_SPELL_SLOTS of them at
       creation rather than the full thirty: three is enough for a rotation
       nobody has to read. */
    int  magic_school;  /* SchoolId, or -1 for anyone who does not cast */
    int  known_spells[MAX_KNOWN_SPELLS];
    int  spell_cd[MAX_KNOWN_SPELLS];
    int  spell_count;

    /* Slot (index into known_spells) of the last spell actually cast, -1 if
       none yet. Lets a quick-cast key skip the pick-a-spell menu once this
       body has cast anything at all. */
    int  last_spell_slot;

    int  aether, aether_max;   /* the mana-equivalent pool, from Conduit */

    /* ---- a ranged weapon ------------------------------------------------
       One slot, like the melee weapon -- not a known-list like spells, since
       only one can be carried at a time. ranged_ammo_max/dmg_bonus_pct are
       derived from Precision the same way aether_max/spell_power_pct are
       derived from Conduit and Resonance.

       ranged_slot is the RANGED_STOCK index PLUS ONE, so that zero means
       "carries nothing" -- provenance, kept alongside the real fields rather
       than instead of them. A -1 sentinel reads better but is wrong by
       default: a memset slot would claim to be holding RANGED_STOCK[0], and
       the same trap caught magic_school, where zero is a real school. */
    char ranged_name[40];
    int  ranged_type;       /* RangedType */
    int  ranged_slot;
    int  ranged_bonus;      /* base damage */
    int  ranged_ammo_cost;
    int  ranged_cooldown_max;
    int  ranged_cooldown;
    int  ranged_ammo;
    int  ranged_ammo_max;
    int  ranged_dmg_bonus_pct;
    int  shot_cd;      /* the AI's own firing pace; melee ignores it */

    int  ability_id;   /* AbilityId, assigned by hero_create */
    int  ability_cd;   /* turns until their signature move is ready again */

    /* ---- what is happening to them right now --------------------------- */
    int  atk_buff, atk_buff_turns;
    int  def_buff, def_buff_turns;
    int  evasion_buff, evasion_buff_turns;
    int  poison_turns_left, poison_dmg_per_turn;
    int  stun_turns_left;
    int  slow_turns_left;
    int  guard_turns;  /* Vanguard's Bulwark: halved incoming damage while >0 */

    /* Consecutive turns spent not moving. The mycelium reads footfalls, so
       holding still is a real action there -- the only place in the game
       where doing nothing is a move. */
    int  still_turns;

    /* Work in progress: an action that costs several turns and is abandoned
       if anything hits you. The game had exactly one of these -- channelling
       a recall charm -- hard-coded into the key loop. This is the general
       form, because "commit N turns and be unable to answer" is the tension
       a dozen ideas in the pool are built on.

       work_turns_left == 0 means idle. */
    int  work_turns_left, work_kind, work_x, work_y;

    /* Warding's reflect: a share of what hits you goes back the way it came,
       for a few turns. The only one of the variety-pass effects that needs
       state on the body -- the other five act and are done. */
    int  reflect_pct, reflect_turns;

    int  recall_channel_left; /* >0 while channelling a recall charm */

    /* Set from the ground under this body every turn, zero everywhere else.
       Lives here rather than being passed because hero_eff_atk() takes only
       a Hero -- adding a Map to that signature would touch every call site
       in the game to express something the ground already told us once a
       turn. */
    int  stance_atk_pct, stance_def_pct;

    /* ---- the AI's scratch space -----------------------------------------
       Only meaningful while nobody is driving this body. Kept on the Hero
       rather than in a side table because a body switches between driven and
       undriven mid-run, and a side table keyed on a slot index is one more
       thing that can disagree with `controlled`. */
    bool heading_back; /* on the way home after over-running their leash */

    /* The frontier tile this body is currently walking to, and how long it
       has been committed to it. Re-choosing the nearest frontier every turn
       is what makes a hire orbit a junction: two or more candidates trade
       places as it moves between them, and a per-tick position trace showed
       one running a clean ten-tile loop for hundreds of ticks while a
       two-cycle detector reported nothing wrong. Commit to a destination and
       walk to it. -1 when not scouting. */
    int  scout_x, scout_y;
    int  scout_turns;
} Hero;

/* The party: six bodies and everything they share. */
#define MAX_PARTY (MAX_COMPANIONS + 1)

/* ---- the party ---------------------------------------------------------
 *
 * Read this as the party's shared state plus six bodies -- not as "the
 * protagonist". The shared half outlives any particular body; `controlled`
 * says which body is currently the protagonist.
 *
 * The line between the two halves was settled deliberately: **gold and the
 * pack are the party's. Everything else is the body's.** A hire you take over
 * fights with their own kit, not the character's. The obvious-sounding
 * alternative -- one pack any body can equip from -- would mean equipment
 * stops being fields on a Hero and becomes "an item in the pack, currently
 * worn by body N", which is an ownership field on every item and a rewrite of
 * combat, the shops, the smith, the character sheet and recompute_set_bonus.
 * That is not the design. The party is six kits that travel together and
 * share a purse.
 */
typedef struct {
    /* Slot 0 is the character the run was started as. 1..MAX_COMPANIONS are
       the hires. That is an origin, not a rank: whoever `controlled` points
       at is the main character, and the one you left is an ordinary hire. */
    Hero party[MAX_PARTY];

    /* ---- whose body you are driving ---------------------------------
       An index into party[]. Zero is the character the run started as, which
       is also what a memset gives, so an old save loads into the right body
       by default.

       The party are lives, not followers: control passes on death and the run
       ends only when there is nobody left standing. */
    int controlled;

    /* ---- the purse --------------------------------------------------- */
    int gold;
    /* What the bank multiplies every coin by. 1 until the first vault share
       is bought; see bank_price(). Applies to every source of income there
       is -- kills, floor gold, bounties, machines, scrap -- because a
       multiplier with exceptions is a multiplier nobody can reason about. */
    int gold_mult;

    /* ---- the pack ------------------------------------------------------ */
    InvStack inventory[INV_CAP];
    int inv_count;
    int keys; /* opens TILE_LOCKED_DOOR */

    /* Scrap hauled back up, waiting for the junkyard. Kept as a count and a
       total rather than as inventory: it is not a decision, it is weight,
       and making the player manage it would be busywork.

       Three materials rather than one heap. Scrap comes off the shallow
       biomes, platinum out of the middle, diamond only from the Abyss -- and
       the deep upgrade tiers will not take anything else. That is the point:
       it makes grinding floor 5 forever buy you nothing above a certain rung,
       so going deeper is the only way to afford going deeper. */
    int  mat_count[MAT_COUNT];
    long mat_value[MAT_COUNT];

    /* Races run since you last came up. The track is the one income in the
       game that is not paid for in experience -- which is the whole point of
       it -- so it cannot also be unbounded, or it becomes the only thing
       anyone does. The bookmaker's odds sour as the day goes on; going back
       down and coming up again is what resets him. */
    int  races_this_visit;

    /* The larder, and the reputation of the house you cook for.
     *
       Meat comes off monsters killed with a blade -- not spells, not arrows,
       not a companion's teeth. A body burst by fire has nothing on it worth
       serving. The value of the larder is realised in town, at the pub, on a
       service night, and a service night costs no experience at all: the
       restaurant's standing goes up, and yours does not.

       That is the whole reason this exists. The complaint was never that
       gold was scarce -- it was that every way of earning it also raised
       your level, which raised what the floors demanded, which meant you
       needed more gold. Meat does not break that loop by being free of the
       dungeon; it breaks it by paying far more per point of experience than
       the kill itself did, so the curve you are climbing tilts your way. */
    int  meat[MEAT_GRADE_COUNT];
    int  kitchen_rep;            /* 0..KITCHEN_REP_MAX, the house's standing */
    int  kitchen_nights;         /* service nights since you last came up */
    long kitchen_earned;         /* lifetime, for the character sheet */

    /* The black market. It buys experience off the driven body: a level, its
       hit points, its attack and its share of defence, gone, and coin in the
       party's hand.
     *
       Deliberately unguarded. There is no once-per-level limit and no floor
       tied to how deep you have been, so selling down and re-earning the
       cheap levels somewhere safe is a legitimate way to play -- it is the
       loop docs/EVALUATION.md 63 measures at seven to eleven times a kill's
       honest income. That is the intent, not an oversight. The only bound is
       level 1, because level 0 divides through the stat maths.

       Tracked for the character sheet, and so a future build can guard the
       hardcore modes alone without having to reconstruct the history. */
    int  levels_sold;            /* lifetime */
    long levels_sold_gold;       /* lifetime, what they paid for them */

    /* Writs of training: one free point at the Pit School, each.
     *
       The rule they exist to enforce: a third of the district ideas in the
       pool wanted to hand out permanent attribute points directly, which
       would empty the School of its purpose and flatten a `200v^2 + 300`
       curve that is the only sink of its kind in the game. A writ is the
       compromise -- a real reward, redeemable only *at* the School, so the
       School stays the one place an attribute ever moves. */
    int training_writs;

    /* What the Oracle was paid for, spent on arrival at the next floor (or
       the next few). Stored on the player rather than the map because it is
       bought in town, before the floor it applies to exists. */
    int  oracle_floors;      /* how many upcoming floors are covered */
    bool oracle_full;        /* the whole floor, or only the way down */

    /* Which day it is at the Bazaar. Advanced on each return to town, and
       the only input to today's prices, so the same trip always quotes the
       same numbers however many times you walk back in. */
    unsigned bazaar_day;

    /* Set by an altar: the last floor on which gold and scrap are still
       doubled. Stored as a floor number rather than a countdown so it
       survives a trip back to town without the player losing the bargain
       they paid health for. */
    int gold_boon_until_floor;

    /* ---- where the run has got to -------------------------------------- */
    int floor;
    int deepest_floor;
    int turns;

    /* How many times this run has stepped onto a temple floor. Monsters scale
       with it (see monsters_set_escalation), so a run gets harder the longer
       it runs -- and walking back up to a cleared floor no longer hands you
       an easy one. */
    int floor_entries;

    int difficulty; /* Difficulty */

    /* Which floor size this run was started at. Restored on load, because
       every bound in the game depends on it. */
    int world_size;

    /* Which wild district the party was standing in last turn, by index, so
       crossing into one can be announced exactly once. -1 for open rock.
       By index rather than kind: two bogs on a floor are two places. */
    int  last_district;

    /* Dynamic difficulty: how this run is actually going. See dda.h -- the
       signals are gathered per floor and folded into one bounded pressure
       value. Nothing reads the pressure yet; phase one is measurement. */
    int  dda_pressure;
    int  dda_floor_start_hp;
    int  dda_heals_used;
    bool dda_retreated;

    /* ---- the Tavern -----------------------------------------------------
       `tavern_seed` regenerates the same 20 candidates every visit without
       storing them, and the two masks remember which of those have been taken
       and which have been buried. */
    /* The one number a run is reproducible from. Floor layouts derive from
       it rather than from the global stream, so floor N of a given seed is
       always the same floor however you played to get there -- which is what
       makes a bug report reproducible instead of a story. */
    unsigned int run_seed;
    unsigned int tavern_seed;
    unsigned int tavern_hired_mask;
    unsigned int tavern_fallen_mask;
    /* Bumped when a hero is let go, so that line of the roster produces a
       different person. Cheaper than storing the roster to edit it. */
    unsigned char tavern_reroll[TAVERN_ROSTER];

    /* ---- the quest board ----------------------------------------------- */
    bool quest_active;
    int quest_type; /* QuestType */
    char quest_monster[40]; /* QUEST_KILL: the target's name. QUEST_FETCH: the item's name. unused for QUEST_CLEAR. */
    int quest_floor;
    int quest_gold_reward;
    int quest_xp_reward;
    bool quest_item_found;   /* QUEST_FETCH: picked up, not yet turned in at the board */
    int quest_kills_done;    /* QUEST_CLEAR: progress so far */
    int quest_kills_needed;  /* QUEST_CLEAR: target count */

    /* QUEST_TIMED: the turn count the bounty expires on. Stored as an absolute
       turn rather than a countdown so it cannot drift with anything that ticks
       turns in bulk. */
    long quest_deadline;
    /* QUEST_NORECALL: set the moment a charm is used, and never cleared until
       the next bounty. Failing is the point of it. */
    bool quest_broken;
    /* QUEST_ESCORT: which party slot the client is in, or -1. They are an
       ordinary Hero -- there is no client struct, because a body is a body. */
    int  quest_escort_slot;
} Player;

/* ---- reaching a body ---------------------------------------------------
 *
 * `hero_driven` is whoever has the controller. `hero_mc` is slot 0, the
 * character the run started as -- which is an origin, not a role, and is
 * almost never what a caller wants. If you are reaching for hero_mc() in new
 * code, check you do not mean hero_driven().
 */
/* Is this body standing? The one predicate, used for every slot including
   slot 0 -- "somebody is in this seat, they have not been killed, and they
   have hit points left".

   Both halves are load-bearing and neither implies the other. `alive` is what
   a hire's death clears; `hp > 0` is how the character's death has always been
   detected, at a few dozen call sites that would each have had to learn to
   clear a flag. Requiring both means a body is down the moment either says so,
   and the guardian angel putting somebody back on one hit point brings them
   up again without a second flag to remember. */
static inline bool hero_is_up(const Hero *h) {
    return h && h->in_use && h->alive && h->hp > 0;
}

/* Whoever has the controller.
 *
   Falls back to slot 0 when `controlled` points at a body that is no longer
   standing. That is not defensive padding -- control can be left pointing at
   somebody who died between one frame and the next, before the death handler
   has run, and the honest answer to "where is the party" in that gap is "with
   the character". party_pass_the_torch() is what resolves it properly. */
static inline int hero_driven_idx(const Player *p) {
    int i = p->controlled;
    if (i < 0 || i >= MAX_PARTY) return 0;
    return hero_is_up(&p->party[i]) ? i : 0;
}
static inline Hero *hero_driven(Player *p) { return &p->party[hero_driven_idx(p)]; }
static inline const Hero *hero_driven_c(const Player *p) { return &p->party[hero_driven_idx(p)]; }

/* Slot 0: the character the run was started as. An origin, not a role, and
   almost never what a caller wants -- if you are reaching for this in new
   code, check you do not mean hero_driven(). */
static inline Hero *hero_mc(Player *p) { return &p->party[0]; }
static inline const Hero *hero_mc_c(const Player *p) { return &p->party[0]; }

/* ---- whose body you are driving ----------------------------------------
 *
 * Take-control, GTA-style: Tab hands the run to another party member, the one
 * you are driving *is* the protagonist, and the party are lives -- control
 * passes on death and the run ends only when nobody is standing.
 *
 * Main character is a *role*, not a character. Whoever holds it is the MC and
 * everything attached to the role attaches to them -- the purse, the shared
 * recall charms, the Inn's bed, the game-over condition. The one you left
 * becomes an ordinary hire, run by the companion AI, their reach taken from
 * their class's archetype.
 *
 * What is shared and what is not: **gold and recall charms are the party's,
 * everything else is the body's.** Both shared things already were, by
 * construction -- only Player has a purse, and count_recall_charms() reads one
 * inventory -- so every shop, the smith, the bank and the black market keep
 * working untouched whoever is driving. A hire fights with their own weapon,
 * their own spells and their own level.
 *
 * Declared here rather than beside the balance constants because all of it
 * takes a Player, and Player is defined immediately above. */
#define PARTY_SWITCH_ENABLED 1

/* Where the party's eyes and the camera actually are: the body being driven,
   which is the MC unless control has been handed to a hire. Every system that
   used to read p->x/p->y for "where the player is standing" reads these. */
int party_x(const Player *p);
int party_y(const Player *p);

/* The hire currently being driven, or NULL for the MC. */
const Hero *party_body(const Player *p);

/* Hand control to the next living body, wrapping through MC and hires.
   Returns false if there is nobody else to hand it to. */
bool party_switch_next(Player *p);

/* Somebody, anybody, still standing. False is the game over. */
bool party_anyone_alive(const Player *p);

/* Called wherever a death would have ended the run. If anyone is still up,
   the role passes to them and the run continues; false is the real game over.
   Idempotent, and safe to call when the current body is fine. */
bool party_pass_the_torch(Player *p);


typedef enum {
    STATE_INTRO,
    STATE_CHARCREATE,
    STATE_TOWN,
    STATE_TEMPLE,
    STATE_GAMEOVER,
    STATE_WIN,
    STATE_QUIT
} GameState;

typedef enum {
    SHOP_GENERAL,
    SHOP_ARMORY,
    SHOP_APOTHECARY
} ShopId;

extern char g_msg_log[MSG_LOG_CAP][96];
extern int g_msg_log_count;

/* Set whenever the player's HP drops (a hit lands or a status tick bites).
   draw_game_screen consumes it as a one-frame red border pulse, then clears
   it -- so it fires at most once per turn regardless of how many draws
   happen while resolving that turn. */
extern bool g_hp_flash;

/* Wipes the message log. Full-screen menus call this on entry so the only
   lines under them are the ones they produced -- see log_reset()'s use in
   main.c. */
void log_reset(void);
void log_msg(const char *fmt, ...);
int hero_eff_atk(const Hero *h);
/* Attack as it counts *on this square* -- see the proving ground. */
int hero_eff_atk_on(const Hero *h, const Map *m);
int hero_eff_def(const Hero *h);
const char *difficulty_name(int difficulty);

/* Base derived-stat value for `which` (crit/evasion/ward/gold-find/xp-find)
   plus whatever ring/trinket bonus is currently equipped against it. */
int effective_stat(const Hero *h, int which);

/* Player may cross water via bridges but not lava/wall; monsters avoid
   water, lava and miasma entirely (walls block both). */
bool is_walkable_player(const Map *m, int x, int y);

/* Spend one of the Oracle's readings on a freshly generated floor. */
void oracle_spend_reading(Map *m, Player *p);
bool is_walkable_monster(const Map *m, int x, int y);
/* is_walkable_monster plus "not an aqueduct current" -- for the player on
   auto-explore and for hired companions, who must not be swept off a route
   they were told to walk. See the definition. */
bool is_walkable_delegated(const Map *m, int x, int y);

Monster *monster_at(Map *m, int x, int y);

#endif
