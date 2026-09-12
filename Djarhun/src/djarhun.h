/* Djarhun -- The Quest for the Book of Avrakar, as a console game.
 *
 * The rules are in RULES-djarhun.txt, extracted straight out of
 * Djarhun_Rules.pdf (which, unusually, carries real text rather than
 * scanned images).  Line numbers in comments below refer to it.
 */
#ifndef DJARHUN_H
#define DJARHUN_H

#include <stdarg.h>

#define MAX_PLAYERS   4
#define MAX_ITEMS     8         /* the most any Strength allows */
#define MAX_SPELLS    4         /* the most any Sorcery allows  */
#define MAX_HENCH     4
#define MAX_ON_SPACE  4         /* cards left lying on a space  */

/* ---------------------------------------------------------------- dice --
 * Djarhun is not a d6 game: "you will need a d4, d6, d8, d10, d12 & d20".
 * Movement uses whichever die your Speed has earned, battle is always d8,
 * and a defence roll is always d20. */
int die(int sides);

/* --------------------------------------------------------------- board --
 * The board is four concentric rings with the Abyss at the middle:
 * Frostburn outermost, then the Tar'ri Ocean, Durach, Aldun, and the
 * Abyss spiral in the centre.  Urthe is a separate board reached only by
 * time travel.  Rings are walked either way; the Abyss is one-way inwards
 * "in a spiral... until they reach Gharad's tower". */
typedef enum {
    LAND_FROST = 0, LAND_TARRI, LAND_DURACH, LAND_ALDUN,
    LAND_ABYSS, LAND_URTHE, LAND_COUNT
} Land;

typedef enum { TOPO_RING, TOPO_SPIRAL } Topology;

typedef enum {
    SP_PLAIN,        /* nothing happens here                        */
    SP_DRAW,         /* "Draw N Cards" from this land's deck        */
    SP_MARKET,       /* a town: buy, sell, heal                     */
    SP_HEALER,
    SP_CROSSING,     /* a named way into the neighbouring land      */
    SP_HOME,         /* somebody's starting space                   */
    SP_ELIDOR,       /* where a Fair hero must carry the Book       */
    SP_GYPSY,        /* where a Vile hero must carry it             */
    SP_ABYSS_GATE,   /* the Oasis of Ezrabar: the way down          */
    SP_LAKE,         /* Lake of Tears -- every descent starts here  */
    SP_GHARAD        /* his tower, at the centre                    */
} SpaceKind;

typedef struct {
    const char *name;    /* <= 13 chars: it has to fit a board cell */
    SpaceKind   kind;
    int         draw;    /* cards drawn on landing */
    int         to_land; /* SP_CROSSING: which land it opens onto, else -1 */
    int         to_idx;
} Space;

/* --------------------------------------------------------------- ships --
 * "Ships can be purchased from the shipbuilders of Elidor... Below the
 * picture of the ship, you will see the Hull (this value represents the
 * Health of the Ship) and Speed values.  At the bottom right, you will see
 * the value of the ship."
 *
 * The ship sheets are the one part of this set the slicer could not read:
 * each card's text comes out split across two columns, so "The Seaside
 * Humility" arrives as "The S" and "easide Humility".  The numbers did
 * survive -- Hull, Speed and value print as a block -- so the table in
 * data.c is those numbers with the names that were legible beside them.
 * The pairing is a reconstruction, and is the one place in this game where
 * the data is mine rather than the sheets'. */
typedef struct { const char *name; int hull, speed, value; } ShipDef;
extern const ShipDef ship_tbl[];
extern const int ship_count;

typedef struct {
    const char  *name;
    const Space *spaces;
    int          n;
    Topology     topo;
    int          base;      /* first space id, for the per-space card piles */
    /* The level a hero wants behind her before setting foot here.  The
     * lands are not equally dangerous -- Frostburn will take a beginner,
     * the Abyss will not -- and this is what stops the AI walking into
     * somewhere that will only kill it.  It gates nothing for a human:
     * you may go wherever you like and find out. */
    int          min_level;
} LandDef;

extern const LandDef land_tbl[LAND_COUNT];
extern int space_id(int land, int idx);
extern int land_len(int land);
extern const Space *space_at(int land, int idx);

/* --------------------------------------------------------------- heroes --
 * Reference sheet, front: three tables that turn a statistic into what it
 * lets you do.  These are the whole of why the statistics matter. */
int items_allowed(int strength);   /* 1-2:3  3-5:4  6-9:5  10-13:6  14-19:7  20+:8 */
int move_die(int speed);           /* 1-3:d4  4-9:d6  10-15:d8  16-21:d10  22+:d12 */
int spells_allowed(int sorcery);   /* 3-5:1  6-9:2  10-16:3  17+:4 */

/* "The table above shows what amount of total Experience Points are needed
 * to achieve the next Level": 7, 21, 42, 70, 105, 147, 196, 252, 315, 385.
 * Each step is 7 more than the last, so the threshold is 7*n*(n+1)/2. */
int xp_for_level(int level);
#define MAX_LEVEL 10

/* The cards carry three Moralities, not two: Fair, Vile and Kind.  Kind
 * sides with Elidor as Fair does -- it is only the Vile who take the Book
 * to the Gypsies. */
typedef enum { MOR_FAIR, MOR_KIND, MOR_VILE } Morality;
#define MOR_GOOD(m) ((m) != MOR_VILE)

typedef struct {
    const char *name;
    const char *race;
    int   str, spd, sor;
    Morality morality;
    int   home_land, home_idx;
    const char *skill;
} HeroTemplate;

/* Upper bound on the Hero table, so anything sized by it cannot fall
 * behind when more cards are read off the sheets. */
#define MAX_HERO_TEMPLATES 128
extern const HeroTemplate hero_tbl[];
extern const int hero_count;

typedef struct {
    char        name[24];
    const char *race;
    const char *skill;
    int   str, spd, sor;      /* current, counters and all */
    int   base_str, base_spd, base_sor;   /* starting quota: a floor */
    int   health, gems, xp, level;
    Morality morality;
    int   land, idx;
    int   home_land, home_idx;
    int   alive, ai;
    int   book;               /* carrying the Book of Avrakar */
    int   miss;               /* turns lost to a curse        */
    int   items[MAX_ITEMS];   int nitems;
    int   spells[MAX_SPELLS]; int nspells;
    /* "He will add 2 to your Strength during battle" -- Henchmen fight
     * alongside you.  MAX_HENCH was written with the header and nothing
     * ever put anybody in it. */
    int   hench[MAX_HENCH];   int nhench;
    /* "A Hero may only own one Ship at a time."  -1 for none; `hull` is the
     * counters on the card, and the Ship is destroyed when it reaches 0. */
    int   ship, hull;
    /* The statistic this Hero is building, chosen from her printed card and
     * then held to: 0 Strength, 2 Sorcery -- the two Gharad has, and so the
     * two that can end the game.  Speed takes the spare point.
     *
     * It was being re-derived from the current numbers at every Level, so a
     * single Pentacle or a found sword could flip a Sorceress into building
     * Strength halfway up the ladder and leave her middling at both.  A plan
     * is only a plan if it survives the next good card. */
    int   plan;
} Hero;

extern Hero heroes[MAX_PLAYERS];
extern int  nheroes, cur_hero;

int max_health(const Hero *h);   /* 4, or the Level once it passes 4 */

/* ---------------------------------------------------------------- cards --
 * Each land has its own deck -- "Heroes draw the cards appropriate to the
 * area they are in (Heroes in Aldun only draw the Desert cards)" -- plus
 * shared Spell and Treasure decks. */
/* The sheets name more kinds of card than this used to carry.  PLACE, TRAP
 * and SHIP were all being folded into C_EVENT, which is why 65 Places, 41
 * Traps and 5 Ships sat on the board printing their text and doing nothing.
 * MARKET, DWELLING and GUILD are Places; BEAST, ANIMAL and UNDEAD are Foes
 * with a keyword some Henchmen care about. */
typedef enum { PK_NONE, PK_STAT, PK_HEAL, PK_DRAW, PK_TOLL } PlaceKind;
/* No LK_HURT: see gen-cards.py.  The "lose 1 Health" Luck cards are almost
 * all conditional and the condition is the half that will not read. */
typedef enum { LK_NONE, LK_TURN, LK_STAT, LK_DROP } LuckKind;

/* Not everything in the Treasure deck is a thing you carry.  "Along your
 * travels, you have found a Gem.  Take 1 Gem token and place this card on
 * the discard pile."  Headed ITEM on the sheet, so it was taken into the
 * pack, where it did nothing and took up a slot. */
typedef enum { TK_NONE, TK_GEM, TK_SPELL } TreasureKind;

/* What a Stranger sells.  "Unless otherwise noted, they usually stay on the
 * space" -- so one that sells something is a fixture worth walking to, and
 * one that sells nothing this game can read is met once and moves on. */
typedef enum { NK_NONE, NK_FLY, NK_TRAIN, NK_SPELL, NK_HEAL,
               NK_BUY } StrangerKind;

typedef enum { SK_NONE, SK_SLAY, SK_HENCH, SK_HEAL, SK_BOOST, SK_GUARD,
               SK_EXTRA, SK_DRAW, SK_EVADE, SK_STEAL, SK_MOVE, SK_CURSE,
               SK_HARM } SpellKind;

typedef enum { C_FOE, C_TREASURE, C_ITEM, C_SPELL, C_EVENT, C_HENCH,
               C_PLACE, C_TRAP, C_SHIP, C_STRANGER } CardType;

typedef struct {
    const char *name;
    CardType    type;
    int   str, spd, sor;   /* a Foe's characteristics */
    int   ranged;          /* the bow-and-arrow symbol: it shoots back */
    int   gems;            /* value, for buying and bartering         */
    int   d_str, d_spd, d_sor;   /* what an Item lends you            */
    int   defends;         /* protective: allows the d20 defence roll */
    /* What a Spell does.  139 of them are printed, each written its own
     * way, so gen-cards.py sorts them into shapes rather than the game
     * carrying 139 handlers: what can be recognised gets a mechanic and
     * the rest are held and read.  `sval` is the number on the card --
     * "add 3 to your Strength" is a 3. */
    int   skind;
    int   sval;
    /* What a Place does, and how many times.  "Place value of 5 counter
     * here.  Every time a Hero visits here, they may gain an additional
     * Strength point.  They must then deduct a counter.  Once all the
     * counters are gone, the Pentacle crumbles to the discard pile." */
    int   pkind;
    int   counters;
    int   lkind;           /* what a Luck card does */
    int   tkind;           /* an Item that is spent rather than carried */
    int   nkind;           /* what a Stranger sells; .gems is the price */
    int   land;            /* which deck it belongs to, or -1 for any */
    /* The roman numeral at the top right.  "The lowest number is dealt with
     * first, and so on, until they have all been dealt with" -- so when
     * several cards share a space this is the order they resolve in.  It has
     * been parsed since the sheets were read and never once consulted. */
    int   order;
    int   copies;
    const char *text;
} Card;

extern const Card card_proto[];
extern int card_count(void);

/* What is lying on each space, dealt with in the order it was drawn.
 * This is a capacity, not a count: board_check() asserts that the land
 * table fits inside it, because it did not -- Urthe's spaces run to 164
 * and this was 160, so the last five wrote past the end of the array. */
#define MAX_SPACES 256
extern int space_card[MAX_SPACES][MAX_ON_SPACE];
extern int space_ncard[MAX_SPACES];
/* Counters left on a Place lying here, indexed alongside space_card. */
extern int space_left[MAX_SPACES][MAX_ON_SPACE];
void board_check(void);

/* ------------------------------------------------------------------ ui --*/
void ui_init(void);
void ui_end(void);
void ui_draw(void);
int  ui_prompt(const char *msg, const char *valid);
void ui_set_log(const char *path);
void glog(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

/* The decision trace, and the thing that found every real AI bug in the
 * sibling project: glog() records what happened, wlog() records why -- the
 * move taken, the move refused, the rule that chose the destination and the
 * margin between them.  Trace file only, and only under DJARHUN_WHY, because
 * it is a line a turn and nobody playing wants to read it. */
extern int ai_why;
void wlog(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

/* Rules invariants, asserted as the game runs under DJARHUN_CHECK.  A log
 * with `INV ` in it is a bug rather than a quirk. */
extern int checking;
void check_all(const char *when);
void ui_die(const char *label, int sides, int rolled);
void ui_dice_clear(void);
/* so the in-game help can show what is in force, not just its name */
unsigned ui_seed(void);

#endif
