#ifndef TALISMAN_H
#define TALISMAN_H

#include <ncurses.h>

/* ------------------------------------------------------------------ *
 * The board is one 7x7 grid of cells.  Its concentric perimeters are
 *   7x7 -> 24 spaces  (Outer Region)
 *   5x5 -> 16 spaces  (Middle Region)
 *   3x3 ->  8 spaces  (Inner Region)
 *   1x1 ->  1 space   (Crown of Command)
 * 24+16+8+1 = 49, so every cell of the grid is a real space.
 * ------------------------------------------------------------------ */

#define GRID        7
#define OUTER_N     24
#define MIDDLE_N    16
#define INNER_N     8
#define MAX_PLAYERS 4
/* Menu key for expansion set i: 1-9, then a, b, c... */
#define SET_KEY(i)  ((i) < 10 ? (char)('0' + (i)) : (char)('a' + (i) - 10))
#define MULE_CARRY  8    /* Adventure: "Mules be restricted to carrying eight objects" */
#define MAX_CHARS   96   /* character cards: 14 base + 38 expansion, with room */
#define MAX_ITEMS   16
#define BASE_CARRY  4     /* rulebook: four Objects, Followers excepted */
#define MAX_KILLS   64
#define MAX_SPELLS  4     /* the limit is 3; one of slack before discarding */    /* the trophy pile: what you actually killed */
#define MAX_DECK    1024
#define MAX_SDECK   256
/* Capacity, not a count: the resident arrays are sized by it, and the
 * region table's total must fit inside.  board_init() checks that it does,
 * so adding a board that overflows fails loudly instead of scribbling. */
#define MAX_BOARDS  6           /* the Kingdom, plus room for the expansions */
#define NSPACES     128
#define MAINBOARD_N (OUTER_N + MIDDLE_N + INNER_N + 1)   /* 49 */
#define MAX_RES     16     /* things that can sit on one space at once */

/* the three fixed crossing points -- the only ways inward */
#define SENTINEL_IDX    3   /* outer  : guarded bridge, Strength 6      */
#define MID_ENTRY_IDX   2   /* middle : where you arrive from Sentinel  */
#define TEMPLE_IDX      6   /* middle : chance of gaining a Talisman    */
#define PORTAL_IDX     10   /* middle : needs a Talisman to pass        */
#define INNER_ENTRY_IDX 5   /* inner  : where you arrive from Portal    */
#define GATE_IDX        1   /* inner  : step from here into the Crown   */

/* Expansions.  The base game is always in; the rest are shuffled in at
 * the player's choice, exactly as the boxes intend. */
typedef enum {
    SET_BASE = 0, SET_REAPER, SET_FROSTMARCH, SET_SACRED, SET_BLOODMOON,
    SET_FIRELANDS, SET_CATACLYSM, SET_DUNGEON, SET_HIGHLAND, SET_WOODLAND,
    SET_HARBINGER,
    /* Not expansions of the main deck but separate stacks, drawn only on
     * their own boards, so deck_build() leaves them out. */
    SET_CITYD, SET_TIMED,
    SET_COUNT
} CardSet;

extern const char *set_name[SET_COUNT];
extern int enabled_sets;                 /* bitmask of (1 << CardSet) */
#define SET_ON(s) (enabled_sets & (1 << (s)))

/* The rulebook's own word.  Its Dungeon section says the Dungeon "counts
 * as a Region for the purposes of casting spells" and of Events, so the
 * expansion boards slot in here rather than beside here: a Region is a run
 * of spaces with one shape, and a board is one or more Regions drawn on a
 * single grid.  The four below are the main board. */
typedef enum { REG_OUTER = 0, REG_MIDDLE = 1, REG_INNER = 2, REG_CROWN = 3,
               REG_DUNGEON = 4, REG_CITY = 5, REG_DONJON = 6,
               REG_TIME = 7 } Region;


typedef enum {
    SP_PLAIN, SP_ADV, SP_CITY, SP_TAVERN, SP_CHAPEL, SP_GRAVE, SP_VILLAGE,
    SP_TEMPLE, SP_MINE, SP_DESERT, SP_PIT, SP_PITFIENDS, SP_WEREDEN, SP_FIRE, SP_CRYPT,
    SP_SENTINEL, SP_PORTAL, SP_GATE, SP_CROWN_SP,
    /* the Dungeon's rooms, each with its own printed rule */
    SP_D_ENTRANCE, SP_D_GUARD, SP_D_LIBRARY, SP_D_CELL,
    SP_D_TORTURE, SP_D_KITCHEN, SP_D_DARK, SP_D_TREASURE,
    /* the City's Locations, each with its own printed rule */
    SP_C_GATE, SP_C_TEMPLE, SP_C_APOTHECARY, SP_C_BANK, SP_C_WHARF,
    SP_C_INN, SP_C_ARMOURY, SP_C_STABLES, SP_C_SURGERY, SP_C_SQUARE,
    SP_C_EMPORIUM, SP_C_CASTLE, SP_C_ENCHANTRESS, SP_C_DONJON,
    /* the Timescape's realities */
    SP_T_WARPGATE, SP_T_TIMELOOP, SP_T_VORTEX, SP_T_DEMON, SP_T_NEXUS,
    SP_T_RADZONE, SP_T_DEATHWORLD, SP_T_FORTRESS, SP_T_SENTINEL, SP_T_DRAW
} SpaceKind;

#define DUNGEON_N   25          /* 16 outer + 8 inner + the Treasure Chamber */
#define CITY_N      24          /* streets and Locations, turn and turn about */
#define TIME_N      16          /* the realities of the Timescape             */

typedef struct {
    const char *name;   /* <= 11 chars: it has to fit one board cell */
    SpaceKind   kind;
    int         draw;   /* adventure cards drawn on landing */
} Space;

/* A RING wraps: walking off one end brings you round.  A PATH stops at its
 * ends -- the Dungeon is a spiral you may "only move towards the center"
 * along, and "must stop moving if you reach the Entrance space itself".
 * Wrapping a PATH would teleport a character from the deepest chamber back
 * to the door without a word, so the two cannot share a step function. */
typedef enum {
    TOPO_RING,   /* walk it either way, and it wraps                     */
    TOPO_PATH,   /* the Dungeon spiral: one way, and the ends stop you   */
    TOPO_WARP    /* the Timescape: a directed graph, the die picks a line*/
} Topology;

typedef struct {
    const char  *name;
    const Space *spaces;
    int          n;
    Topology     topo;
    int          base;      /* its first space_id                        */
    int          board;     /* which board draws it                      */
} RegionDef;

typedef struct {
    const char *name;       /* shown as the screen title while you are on it */
    int         first_region, nregions;
    int         rows, cols;
} BoardDef;

typedef enum {
    C_ENEMY, C_SPIRIT, C_OBJECT, C_FOLLOWER, C_EVENT, C_GOLD, C_PLACE, C_STRANGER
} CardType;

/* Place cards are never discarded -- once drawn they stay on that space
 * for the rest of the game, and everyone who lands there meets them. */
/* Rulebook keywords: only one Weapon and one Armour may be used in an
 * attack, so kit no longer simply stacks. */
#define KW_WEAPON 1
#define KW_ARMOUR 2

/* Rule 5 and the worked example: "The Assassin discovers the Holy Lance (a
 * Magic Object) which can only be used by Good or Neutral Characters.  He
 * cannot use it because he is of Evil Alignment.  He must leave it face up
 * in the Space where he Encountered it."  A card with NEEDS_ANY is open to
 * everyone; otherwise the bits say who may carry it. */
#define NEEDS_GOOD    1u
#define NEEDS_NEUTRAL 2u
#define NEEDS_EVIL    4u
#define NEEDS_ANY     (NEEDS_GOOD | NEEDS_NEUTRAL | NEEDS_EVIL)

/* Character special abilities, read off the printed 2e character cards
 * (see CHARACTERS-2e.tsv).  The real cards carry two to six each, so this
 * is a bitmask and not an enum -- the Dwarf alone has six. */
#define AB_TWO_WEAPONS    (1u <<  0)  /* Warrior:    two Weapons at once      */
#define AB_COMBAT_2DICE   (1u <<  1)  /* Warrior:    roll 2, keep the higher  */
#define AB_CRAFT_TO_STR   (1u <<  2)  /* Monk:       adds Craft to Strength   */
#define AB_PSYCHIC_ATTACK (1u <<  3)  /* Wizard etc: attacker may go Psychic  */
#define AB_STEAL          (1u <<  4)  /* Thief:      robs without a fight     */
#define AB_DESTROY_SPIRIT (1u <<  5)  /* Priest:     destroys Spirits outright*/
#define AB_MINER          (1u <<  6)  /* Dwarf:      only 2 dice in the Mines */
#define AB_ALWAYS_SPELL   (1u <<  7)  /* Wizard:     never without a Spell    */
#define AB_ENCHANT        (1u <<  8)  /* Sorceress:  a turn lost, not a Life  */
#define AB_DRAW_EXTRA     (1u <<  9)  /* Prophetess: draw one more, discard   */
#define AB_LIFE_STEAL     (1u << 10)  /* Ghoul:      a taken Life is her own  */
#define AB_ASSASSINATE    (1u << 11)  /* Assassin:   the victim adds no die   */
#define AB_CHARM_ANIMAL   (1u << 12)  /* Minstrel:   Animals will not attack  */
#define AB_NO_SWORD       (1u << 13)  /* Monk, Priest: forbidden edged steel  */
#define AB_EVADE_SPIRIT   (1u << 14)  /* Sorceress:  may Evade Spirits        */
#define AB_ANY_ALIGN      (1u << 15)  /* Druid: "change your Alignment at will" */
#define AB_EVIL_EYE       (1u << 16)  /* Witch Doctor: Curse instead of fighting */
#define AB_BOW            (1u << 17)  /* Samurai: fire at range instead of moving */

/* Talisman the Adventure, 3: HENCHMEN.  "The player can use any or all of
 * the Henchman's Special abilities as if they were the Character's normal
 * abilities.  These must be abilities the Henchman could pass on or use for
 * you.  For example, you could not gain a Henchman's resistance to a Siren's
 * song but he could steal for you."
 *
 * So the line is between what he *does* on your behalf and what he simply
 * *is*: thieving, mining, scouting and the destruction of Spirits are
 * services; resistances, evasions and his own way of swinging a sword are
 * not, and stay with him for the fights he takes himself. */
#define HENCH_PASSES (AB_STEAL | AB_DESTROY_SPIRIT | AB_MINER | AB_DRAW_EXTRA)

typedef enum {
    PLACE_NONE,
    PLACE_SHOP,      /* Market                                    */
    PLACE_PORTAL,    /* Magic Portal                              */
    PLACE_SHRINE,    /* Shrine                                    */
    PLACE_DEN,       /* Cave -- it breeds Enemies                 */
    PLACE_FOUNTAIN,  /* Fountain of Wisdom, Hermit, Mage: +Craft  */
    PLACE_POOL,      /* Magic Stream, Pool of Life, Healer: heals */
    PLACE_MAZE,      /* Maze: you lose your way                   */
    PLACE_MARSH,     /* Marsh: it wounds                          */
    PLACE_FAIRY,     /* Fairy: +Fate                              */
    PLACE_SPIRIT,    /* Phantom, Sorcerer: a psychic combat       */
    PLACE_WITCH,     /* Witch: her mood decides                   */
    PLACE_SPELL,     /* Enchanter, Mage: they teach a Spell       */
    PLACE_DOORWAY    /* the way down: it opens the Dungeon        */
} PlaceKind;

typedef struct {
    const char *name;
    CardType    type;
    int power;       /* enemy strength / spirit craft */
    int animal;      /* the Minstrel charms these, and will not be attacked */
    int craftfight;  /* 1 = fought with Craft, not Strength */
    int d_str, d_craft, d_life, d_fate, d_gold;
    int talisman;    /* grants the Talisman */
    int miss;        /* turns missed */
    int place;       /* PlaceKind, for C_PLACE cards */
    int carry;       /* extra Object slots (Mule, Horse and Cart, ...) */
    int kw;          /* KW_WEAPON / KW_ARMOUR */
    unsigned needs;  /* NEEDS_* : which Alignments may carry it (0 = any) */
    int util;        /* worth beyond its stats: a named effect elsewhere  */
    int copies;      /* how many of this card are in the deck */
    int set;         /* which expansion it comes from */
    const char *text;
} Card;

/* Spells live in their own deck, drawn and discarded separately. */
typedef enum {
    SK_HEAL, SK_HEX, SK_IMMOBILITY, SK_TOAD, SK_TELEPORT, SK_WARP, SK_ACQUIRE,
    SK_MESMERISE, SK_ALCHEMY, SK_DESTRUCTION, SK_DESTROYMAGIC, SK_DIVINATION,
    SK_COUNTER, SK_RANDOM,
    /* cast during a battle, before the attack roll */
    SK_PSIONIC, SK_INVISIBLE, SK_PRESERVE, SK_NULLIFY
} SpellKind;

#define SPELL_IN_BATTLE(k) ((k) >= SK_PSIONIC)

typedef struct {
    const char *name;
    SpellKind   kind;
    int         copies;
    int         set;
    const char *text;
} Spell;

extern const Spell spell_proto[];

/* The Purchase deck -- what a Village or City actually sells.  Finite:
 * the shop can run out of Swords. */
typedef struct {
    const char *name;   /* resolved against deck_proto by name */
    int         price;
    int         copies;
} Wares;
extern const Wares purchase_proto[];

/* One selectable line in a shop screen. */
typedef struct {
    char        key;
    const char *label;
    const char *note;
    int         price;
    int         avail;    /* stock left, or -1 for a service */
    int         proto;    /* deck_proto index, or -1 for a service */
} ShopLine;

typedef enum { AL_GOOD, AL_NEUTRAL, AL_EVIL } Alignment;

/* "Henchmen cannot themselves possess any Gold, Objects or Followers and
 * they cannot increase their Strength, Craft or Lives.  They can therefore
 * never have more than 4 Lives... If they are killed, they are out of the
 * game and may not be replaced."  A card, a count of Lives, and nothing
 * else -- which is the whole point of them. */
typedef struct {
    int ct;          /* Character-card index, or -1 for none */
    int lives;
} Henchman;

typedef struct {
    const char *cls;
    int str, craft, lives, fate, gold;
    unsigned    abil;       /* bitmask of AB_*                        */
    Alignment   align;      /* printed on the card                    */
    const char *start;      /* the space named on the card, by name   */
    const char *safe;       /* a space she is never troubled in, or 0 */
    int         spells;     /* Spells held at the start of the game   */
    /* Which set she comes with.  The expansions add playable characters as
     * well as cards, so the roster follows whatever is shuffled in. */
    CardSet     set;
    const char *power;      /* one line, shown on the character sheet */
    const char *blurb;
} CharTemplate;

typedef struct {
    char        name[24];
    const char *cls;
    /* BASE values: the character card plus trophies, temples and purchases.
     * Carried Objects and Followers add to these -- see eff_str() below --
     * so that handing an item to someone else moves its bonus with it. */
    int base_str, base_craft, base_maxlives, base_maxfate;
    int lives, fate, gold;
    int region, idx;
    int alive, ai, colour;
    int talisman, miss;
    int belt;        /* wearing the Belt of Hercules (alternative ending) */
    int hedged;      /* turns spent letting somebody else cross first    */
    int gate_shut;   /* we looked at the Dread Gate and said not yet     */
    int fire_shy;    /* turns spent refusing to cross the Valley of Fire */
    int start_spells;/* what she began with -- the Wand keeps her above it */
    int toad;        /* Turns left as a Toad: Strength 1, Craft 1, one step */
    int cursed;      /* the Evil Eye: make for the Chapel (or Ruins, if Evil) */
    int pitfiends;   /* 14:8 -- how many are still waiting for you */
    int look;        /* plies of movement lookahead (0 = the old one-ply) */
    int road;        /* consult the distance-to-Crown tables when moving  */
    Alignment   card_align;  /* the Alignment printed on the Character Card */
    int         realigned;   /* 7:2 -- already changed Alignment this Turn  */
    Henchman    hench;       /* the optional Henchman rule                 */
    unsigned    abil;            /* bitmask of AB_*, from the character card */
    Alignment   align;
    const char *safe;            /* the space that never troubles her, or 0  */
    int     warrant;             /* the Watch want a word: the gates are shut */
    int     warp_from;           /* the Time Loop sends you back here         */
    int     at_doorway;          /* stood on a Doorway: the Entrance is open  */
    int     just_entered;        /* 5.2: cannot enter and leave in one breath */
    int     dungeon_out;         /* driven back: move toward the Entrance     */
    int     fleeing;             /* declared, and cannot be taken back        */
    int     raft;                /* a Raft built or found, good for one turn */
    int     rerolled_move;   /* the Elf's retake, once per turn */
    int troph_str, troph_craft;   /* rulebook: two pools, not interchangeable */
    int carried[MAX_ITEMS];       /* deck_proto indices, transferable */
    int nitems;
    int kills[MAX_KILLS];         /* deck_proto indices of everything slain */
    int nkills;
    int spells[MAX_SPELLS];       /* spell_proto indices                    */
    int nspells;
    int casts_left;               /* rulebook: as many as held at turn start */
    int preserve;                 /* a Preservation spell is standing by     */
} Player;

typedef struct { int region, idx; } CellRef;

/* --- data.c ------------------------------------------------------- */
extern const Space  outer_ring[OUTER_N];
extern const Space  middle_ring[MIDDLE_N];
extern const Space  inner_ring[INNER_N];
extern const Space  crown_space;
extern const Space  dungeon_path[DUNGEON_N];
extern const Space  city_ring[CITY_N];
extern const Space  donjon_space;
extern const Space  time_scape[TIME_N];
extern const int    warp_line[TIME_N][3];   /* where 1-2, 3-4 and 5-6 lead */
void dungeon_cell(int idx, int *row, int *col);
extern const RegionDef region_tbl[];
extern const int       region_count;
extern const BoardDef  board_tbl[];
extern const int       board_count;
int region_board(int region);
int topology(int region);
extern const Card   deck_proto[];        /* terminated by a NULL name */

/* A Character's own abilities, plus whatever their Henchman can do for
 * them.  Restrictions (AB_NO_SWORD) are never picked up this way -- a
 * Henchman cannot make you forbidden to carry a sword. */
int  player_abil(const Player *p);
#define has_ab(p, a)  ((player_abil(p) & (a)) != 0)

int outer_by_name(const char *name);
int space_by_name(const char *name, int *region, int *idx);
int turns_to_crown(int region, int idx, int with_talisman);

extern const CharTemplate char_tbl[];
extern const int    char_count;

/* --- board.c ------------------------------------------------------ */
/* One grid per board.  Only the board the current player stands on is
 * drawn, so the boards never have to share the screen. */
extern CellRef gridmap[MAX_BOARDS][GRID][GRID];
void            board_init(void);
int             ring_len(int region);
const Space    *space_at(int region, int idx);
void            ring_cell(int region, int idx, int *row, int *col);

/* --- what is lying on the board (board.c) ------------------------- */
extern int res_card[NSPACES][MAX_RES];  /* deck_proto indices left behind */
extern int res_n[NSPACES];
extern int res_gold[NSPACES];           /* loose gold on the ground      */
extern int res_tal[NSPACES];            /* a Talisman lying there        */
int  space_id(int region, int idx);
int  across_river(int outer_idx);   /* the Middle space directly opposite */
int  back_across_river(int middle_idx); /* and the Outer space, coming back */

/* --- effective stats: base + whatever is currently carried (items.c) --- */
int  eff_str(const Player *p);
int  eff_craft(const Player *p);
int  eff_maxlives(const Player *p);
int  eff_maxfate(const Player *p);
void clamp_pools(Player *p);
int  item_give(Player *p, int ci);       /* 1 if taken, 0 if hands are full */
void item_drop(Player *p, int slot);     /* remove slot, leave it on the space */
int  item_best(const Player *p);         /* slot an AI would most want to steal */
int  item_worst(const Player *p);        /* slot an AI would give up first     */
int  item_limit(const Player *p);        /* 4, plus whatever a pack beast adds */
int  item_objects(const Player *p);
int  item_best_swap(const Player *p, int ci);
int  item_best_of(const Player *p, int type);  /* best card of a type, by value */
int  align_allows(const Player *p, int ci);   /* may this Character carry it? */
unsigned align_bit(Alignment a);      /* Followers do not count towards it  */
int  res_add(int sid, int card);   /* 0 if the space is already full */
void res_remove(int sid, int slot);

/* --- game.c ------------------------------------------------------- */
extern Player players[MAX_PLAYERS];
extern int    nplayers, cur_player;
int  roll(void);

/* --- ui.c --------------------------------------------------------- */
void ui_set_log(const char *path);   /* call before ui_init */
void ui_init(void);
void ui_end(void);
void ui_draw(void);
/* Tell the compiler this is a printf: with several hundred call sites a
 * mismatched format is otherwise silent until it prints nonsense or reads
 * off the stack. */
void glog(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
/* so the in-game help can show what is actually in force, not just what
 * the switches are called */
int      endgame(void);
int      ending_kind(void);   /* the revealed ending, or -1 while face down */
int      belt_holder(void);  /* player index wearing the Belt, or -1        */
extern int alt_endings;
extern int henchmen_on;   /* the optional Henchmen rule is in play */
extern int chaos_on;      /* Chaos Bloodbath: one Talisman, death is final */   /* has anyone reached the Crown? */

/* "Shuffle the six cards and pick one randomly; without looking at the card
 * chosen place it facedown on the Crown of Command.  The first player to
 * reach the space reveals the card."  Optional, and only "if all the
 * players agree to their use" -- so it is a choice at startup. */
typedef enum {
    END_CROWN,      /* the Command Spell, as always                     */
    END_DEMON,      /* a Spirit of Craft 12 and 4 Lives, in the dark    */
    END_PANDORA,    /* a chest that hands you Spells to throw           */
    END_BELT,       /* the Belt of Hercules: go and duel them yourself  */
    END_VOID,       /* the first across the Bridge of Fire is annihilated */
    END_DRAGON,     /* the Dragon King, and a die that decides it all   */
    END_COUNT
} Ending;
extern const char *ending_name[END_COUNT];
unsigned ui_seed(void);
int      ui_delay(void);
int  ui_prompt(const char *msg, const char *valid);
int  ui_prompt_row(void);   /* the row menus must stay above */
int  ui_too_small(void);    /* window too small: message shown, 1 returned */
void ui_pause(const char *msg);
int  ui_wait_key(int ms);      /* ms < 0 blocks; ERR if the time runs out */
void ui_sheet(Player *p);
void ui_snapshot(void);    /* dump the current screen (tools/screenshot.sh) */
void ui_shot_help(void);   /* draw the help page without waiting on a key  */
int  ui_pick_item(Player *p, const char *title, int objects_only);
int  ui_pick_spell(Player *p, const char *title, int battle_only);
int  ui_shop(Player *p, const char *title, const ShopLine *lines, int n);
int  spell_limit(const Player *p);
int  has_armour(const Player *p);      /* full-screen character sheet + trophy pile */
void ui_banner(const char *msg);
void ui_dice_clear(void);
void ui_dice_one(const char *label, int die, int total);
void ui_dice_two(const char *l1, int d1, int t1, const char *l2, int d2, int t2);

/* --- ai.c --------------------------------------------------------- */
int ai_direction(Player *p, int steps);
int ai_fight_sentinel(Player *p);   /* 0 = no, 1 = with Strength, 2 = with Craft */
int ai_attack(Player *att, Player *def);
int ai_bolt_target(Player *p);
int ai_cross_gate(Player *p);
int lead_score_of(Player *q);
Alignment ai_pick_align(Player *p);
int ai_recross_river(Player *p);
int ai_flee_demon(Player *p, int demon_lives);
int ai_spell_choice(Player *p, int battle_only);
int ai_use_fate(Player *p, int deficit, int rolled);
int ai_shop_choice(Player *p, const ShopLine *lines, int n);

#endif
