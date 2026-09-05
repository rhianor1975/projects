#ifndef PROPHECY_H
#define PROPHECY_H

#include <ncurses.h>

/* ------------------------------------------------------------------ *
 * The world is 20 spaces in a ring (the rulebook's number), and the
 * perimeter of a 6x6 grid is exactly 6*4-4 = 20.  So the ring is drawn
 * as the border of a 6x6 grid and the 4x4 interior holds the five
 * Astral Planes and the sea between them.
 * ------------------------------------------------------------------ */

#define GRID        6
#define RING_N      20
#define PLANES      5
#define MAX_PLAYERS 5

/* rulebook caps */
#define MAX_STRENGTH  8
#define MAX_WILL     10
#define MAX_GOLD     15
#define MAX_EXP      15
#define MAX_ITEMS     7
#define MAX_ABILITIES 7
#define CARD_SLOTS   16   /* headroom: Race, expansion and Artifact all raise the cap */
#define WIN_ARTIFACTS 4      /* 4 of the 5 Artifacts wins */

typedef enum {
    SP_PLAINS, SP_FOREST, SP_MOUNTAINS,      /* wilderness: Opportunities */
    SP_VILLAGE, SP_CITY,                     /* civilisation             */
    SP_MONASTERY, SP_FOREST_CAMP, SP_MAGIC_TOWER,
    SP_THIEVES_GUILD, SP_FORTRESS,           /* the five guilds          */
    SP_WILDERNESS                            /* the Enchanted Wilderness */
} SpaceKind;

#define IS_GUILD(k) ((k) >= SP_MONASTERY && (k) <= SP_FORTRESS)
#define IS_CIV(k)   ((k) == SP_VILLAGE || (k) == SP_CITY || IS_GUILD(k))
#define IS_WILD(k)  ((k) <= SP_MOUNTAINS)

/* which guild a space teaches for, and which guilds a character belongs to */
typedef enum {
    G_NONE = -1, G_MONASTERY, G_CAMP, G_TOWER, G_THIEVES, G_FORTRESS, G_COUNT
} Guild;

typedef struct {
    const char *name;        /* <= 12 chars: it must fit one board cell */
    SpaceKind   kind;
    int         port;        /* a boat may sail between Ports           */
    int         gate;        /* a Magic Gate reaches any other Gate     */
    int         safe;        /* no character may attack you here        */
} Space;

/* Battles are fought with one characteristic or the other. */
typedef enum { B_STRENGTH, B_WILL } BattleKind;

/* ---- cards ---- */
typedef enum { A_CREATURE, A_OPPORTUNITY } AdvType;

/* Where a card came from.  The 2nd-edition card list is the canon; some
 * cards here are genuine but belong to another edition or an expansion,
 * and a few are ours.  Keeping all three in one deck is a fine way to
 * play -- but the mix changes how often any real card turns up, so
 * PROPHECY_CANON=1 plays the printed deck and nothing else. */
typedef enum {
    SRC_BASE,       /* on the 2nd-edition card list                    */
    SRC_EXPANSION,  /* a real card, but from Water Realm or elsewhere  */
    SRC_HOUSE       /* ours: invented to fill the deck out             */
} CardSource;

/* Every Creature card carries a type on its corner tab, and a surprising
 * number of cards key off it: Hunting (+1 vs animals), the Axe (+1 vs
 * demons), the Radiant Shield (+2 vs demons and undead), the Astral Sword
 * (+1 more vs humanoids), Pickpocketing and the Goblin's Trophies.  A few
 * cards give a character a type too -- the Water Realm's Beast and Path of
 * the Dead both do -- so this is a bitmask, not a single value. */
typedef enum {
    CT_NONE     = 0,
    CT_ANIMAL   = 1 << 0,
    CT_HUMANOID = 1 << 1,
    CT_DEMON    = 1 << 2,
    CT_UNDEAD   = 1 << 3
} CreatureType;

/* The specials the real Adventure cards actually carry.  Each is named
 * on its card, so the enum reads as the card text does. */
typedef enum {
    AS_NONE,
    AS_TWO_BATTLES,   /* Vampire: win a Battle of Wills, THEN one of Strength */
    AS_STUN,          /* Giant: if it beats you it takes 2 Magic as well      */
    AS_STONE_SKIN,    /* Stone Guardian: your weapon is damaged after it      */
    AS_SWAP_REWARD,   /* Headless Knight: beat it with Str and gain Will      */
    AS_NOBILITY,      /* Noble Lion: you need not fight it at all             */
    AS_THIEVERY, AS_FIST,      /* Highwayman: he takes an Item instead of a Health     */
    AS_MIRROR,        /* Doppelganger: its Strength is twice your Health      */
    AS_POISON,        /* Poisonous Snake: lose 2 Health even in victory       */
    AS_SPARE          /* Sold One, Centaur: spare it for an Item, not Exp     */
} AdvSpecial;

typedef struct {
    const char *name;
    AdvType     type;
    CardSource  src;
    unsigned    ctype;    /* CT_* bitmask; 0 for Opportunities            */
    int str, will;        /* a Creature's characteristics; 0 = it has none */
    int str_first;        /* attacks with Strength, so Wills costs 2 Magic */
    int lives;            /* 3 for a band: you must win three rolls       */
    int exp;              /* Experience for beating it                    */
    int t_gold, t_str, t_will, t_item;      /* treasure  (check mark)     */
    int l_health, l_magic, l_gold;          /* worse than usual (the X)   */
    int o_bargain;   /* pay per point, as much as you can afford */
    int o_heal, o_magic, o_gold, o_exp, o_str, o_will;   /* Opportunity   */
    int o_full_heal, o_full_magic;          /* heals/recharges everything  */
    int o_bless;                            /* Chapel: keep for one battle */
    int cost_gold, cost_exp, cost_health;
    AdvSpecial special;
    int copies;
    const char *text;
} Adventure;

/* What KIND of weapon a thing is.  Half a dozen Abilities key off this --
 * Edged/Crushing/Long/Two-handed Weapons, Staff Skill, Ambidexterity -- and
 * a few Items are more than one at once: the Kingslayer is long, crushing
 * AND edged, which is exactly why it is an Artifact. */
typedef enum {
    WT_NONE     = 0,
    WT_EDGED    = 1 << 0,
    WT_CRUSHING = 1 << 1,
    WT_LONG     = 1 << 2,
    WT_TWOHAND  = 1 << 3,
    WT_STAFF    = 1 << 4,
    WT_WAND     = 1 << 5,
    WT_SHIELD   = 1 << 6,
    WT_HAND     = 1 << 7   /* held, but no sort of weapon at all */
} WeaponType;

/* What a scroll or a pair of boots actually DOES.  A dozen Items had no
 * mechanical effect at all and simply occupied one of your seven slots,
 * which is worse than not being in the deck.  Where the printed effect
 * would need engine surgery it is approximated rather than skipped -- the
 * comment on each says which. */
typedef enum {
    IU_NONE,
    IU_TELEPORT,    /* go anywhere on the board                        */
    IU_DIVINE,      /* this battle: you roll 6, your foe rolls 2       */
    IU_TRAIN_FREE,  /* the next training costs no Gold                 */
    IU_TRADE_EXP,   /* Gold counts as Experience and back again        */
    IU_STEAL,       /* take an Item from one who shares your space     */
    IU_DECAY,       /* an Item of theirs is discarded                  */
    IU_DESTROY,     /* a Creature here is destroyed, and gives nothing */
    IU_REDEAL,      /* approx: this space is dealt afresh              */
    IU_WISH,        /* approx: a Rare Item, which is what wishes buy   */
    IU_AGAIN,       /* approx: another turn, for "the round begins anew" */
    IU_STRIDE       /* passive: two spaces cost nothing                */
} ItemUse;

typedef struct {
    const char *name;
    int price, d_str, d_will, rare, copies;
    ItemUse use;
    unsigned wtype;         /* WT_* mask                                  */
    unsigned vs_type;       /* CT_* mask this Item is especially good vs   */
    int vs_bonus;           /* and by how much                             */
    /* Rulebook: throwing weapons may be thrown in a Battle of Strength for
     * an extra bonus, but the throw damages them.  `thrown` is that bonus;
     * 0 means the Item cannot be thrown.  `fragile` marks the shields and
     * armour a Creature can damage when it wins a round. */
    int thrown, fragile;
    /* Potions and scrolls: a third of the real Item deck is used once and
     * gone.  Without this the whole class had to be left out, which is why
     * the deck was thirteen Items rather than sixty-one. */
    CardSource src;
    int oneshot;            /* consumed when used                          */
    int u_heal, u_magic;    /* Health and Magic it gives back              */
    int u_str, u_will;      /* a bonus lasting until the end of your turn  */
    const char *text;
} Item;

/* Rulebook: "Some Abilities are called Spells... activated by paying one
 * or two Magic - the cost is stated on the card below its name.  The
 * effect of a Spell is treated the same as other Abilities." */
typedef enum {
    SP_PASSIVE,        /* an ordinary Ability: a standing bonus       */
    SP_BATTLE_STR,     /* in a Battle of Strength, add to your side   */
    SP_BATTLE_WILL,    /* in a Battle of Wills, add to your side      */
    SP_MOVE_FOREST,    /* move to any Forest space                    */
    SP_MOVE_MOUNTAIN,  /* move to any Mountains space                 */
    SP_MEDITATE,       /* instead of moving: recharge Magic           */
    SP_HEAL,           /* instead of moving: heal                     */
    SP_SACRIFICE       /* instead of moving: a Health for Magic       */
} SpellKind;

/* Abilities that change what things cost rather than what you can do. */
typedef enum {
    PK_NONE,
    PK_HAGGLE,        /* better prices buying and selling in town     */
    PK_COUNTERFEIT,   /* once a round, one payment is 2 Gold lighter  */
    /* the type- and weapon-keyed bonuses: +1 in the right battle */
    PK_HUNT,          /* Hunting:   +1 against animals                */
    PK_EXORCISM,      /* Exorcism:  +1 against undead and demons      */
    PK_DUEL,          /* Duelling:  +1 against humanoids and players  */
    PK_EDGED,         /* Edged Weapons                                */
    PK_CRUSHING,      /* Crushing Weapons                             */
    PK_LONG,          /* Long Weapons                                 */
    PK_THROWN,        /* Thrown Weapons                               */
    PK_STAFF,         /* Staff Skill                                  */
    PK_TWOHAND,       /* Two-handed Combat                            */
    PK_AMBI,          /* Ambidexterity: two one-handed weapons at once */
    /* the ones that change how a battle resolves */
    PK_TOUGH,         /* Toughness:   lose by exactly 1 and it is a draw */
    PK_FANATIC,       /* Fanaticism:  a draw may be fought again        */
    PK_POISON,        /* Poisoned Blade                                 */
    PK_DISGUISE,      /* Disguise:    +1 in Wills vs humanoids/players  */
    /* the ones that change the world rather than the battle */
    PK_STEALTH,       /* use an Opportunity before you fight           */
    PK_WHARF,         /* ships are free                                */
    PK_NAUTICAL,      /* earn a Gold when you sail                     */
    PK_HORSE,         /* ride for nothing                              */
    PK_FLEET,         /* Fleet of Foot: a free extra step              */
    PK_STAMINA,       /* walk up to three spaces                       */
    PK_SMITH,         /* repair Items; earn Gold in a Civilization     */
    PK_TRAINING,      /* Experience when you beat a Creature           */
    PK_PICKPOCKET,    /* extra Gold from humanoids and characters      */
    PK_SPELLCAST,     /* Effective Spellcasting: a Spell costs 1 less  */
    PK_MAGICDRAIN,    /* Magic Drain: take a Magic in a Battle of Wills */
    PK_TURNBACK,      /* Turn Back: reroll after the dice              */
    PK_TIMELOOP,      /* Time Loop: another turn                       */
    PK_SANCTITY,      /* Sanctity of Life                              */
    PK_FORESTWISE,    /* Forest Wisdom: +1 in a Forest                 */
    /* Dragonslayer Abilities: these work ONLY inside the Dragon Realm */
    PK_DRAGONSLAY,    /* +2 Strength vs a Realm Creature               */
    PK_DRAGONMIND,    /* +3 Wills vs a Realm Creature                  */
    PK_DRAGONMAGIC,   /* Magic is cheaper in the Realm                 */
    PK_DRAKE,         /* free entry, and no being thrown back          */
    PK_DRAGONLORE,    /* see ahead, and slip past obstacles            */
    /* the five Underwater Abilities (Water Realm) */
    PK_WATERBREATH,   /* a cheaper bag, and a Health with it            */
    PK_UNDERCONTACT,  /* three more Bubbles in the bag                  */
    PK_COMMUNE,       /* +2 in Wills there, and Bubbles back on a win   */
    PK_UNDERWATER,    /* no penalty for the Items in your hands         */
    PK_SWIMMING       /* a Bubble for an extra turn, or a Bubble for a move */
} Perk;

typedef struct {
    const char *name;
    Guild guild;
    CardSource src;
    int   cost, d_str, d_will;
    Perk  perk;
    int   magic;        /* Magic to cast; 0 means it is not a Spell   */
    SpellKind kind;
    int   power;        /* how much it adds, heals or recharges       */
    const char *text;
} AbilityCard;

int is_spell(const AbilityCard *a);


/* Ancient Races (Dragon Realm expansion).  Ten Race cards, six Races -- some
 * appear more than once, which is how the box reflects how common each is.
 * A Race is dealt alongside the character card and drafted the same way.
 *
 * Stat adjustments move the CAP as well as the starting value: an elven
 * paladin begins at Strength 4 / Willpower 5 and is still held to 8 red and
 * 11 blue cubes. */
typedef enum {
    RACE_NONE = -1, RACE_HUMAN, RACE_ELF, RACE_DWARF,
    RACE_TROLL, RACE_GOBLIN, RACE_HALFLING, RACE_N
} RaceId;

typedef struct {
    const char *name;
    int copies;             /* how many of the ten cards are this Race    */
    int d_str, d_will;      /* and the cap moves with them                */
    int abil_cost;          /* Human -1, Troll +2 on every Ability        */
    int max_abil;           /* Human +1, Troll -2                         */
    int bank;               /* Dwarf: 3 Gold that nothing can touch       */
    int lucky;              /* Halfling: a rolled 1 counts as an 8        */
    int shield_bonus;       /* Goblin: +1 when a shield is used           */
    int trophy;             /* Goblin: at least 1 Gold and 3 Experience   */
    int regen;              /* Troll: a Magic heals a Health in Mountains */
    int forest_magic;       /* Elf: spells and Wills cost 1 less in Forest */
    int frail;              /* Elf: you leave the game at 0 Health        */
    int long_penalty;       /* Halfling: -1 with a long weapon            */
    int throw_bonus;        /* Halfling: +1 when a weapon is thrown       */
    int steadfast;          /* Troll: Clover Meadow and the Mummy do nothing */
    const char *text;
} Race;
extern const Race race_proto[RACE_N];

/* The five Artifacts of the base game, transcribed from the cards.
 *
 * Until now an Artifact was only a number: `p->artifacts` counted them and
 * nothing else changed, so the objects the whole game is a race for did not
 * affect how a character fought.  They are the strongest items in the game
 * and two of them fire every single round.
 *
 * The two Banners are deliberate mirrors -- one heals the table, one harms
 * it -- and the Mirrored Shield has a printed interaction with the Banner of
 * Destruction, which is why they are one table and not five special cases. */
typedef enum {
    ART_CROWN,        /* Crown of the Ancient Kings                     */
    ART_SWORD,        /* Astral Sword                                   */
    ART_MIRROR,       /* Mirrored Shield                                */
    ART_HOPE,         /* Banner of Hope                                 */
    ART_CAPE,         /* Royal Cape                                     */
    ART_N
} ArtifactId;

typedef struct {
    const char *name;
    int d_str;          /* Crown: +1 Strength, simply for carrying it    */
    int d_will;         /* Royal Cape: +1 Willpower, likewise            */
    int free_train;     /* Crown: no Gold for training outside my guilds */
    int battle_str;     /* Sword: +2 in a Battle of Strength             */
    int vs_char;        /* Sword: +1 more against humanoids and players  */
    int drain;          /* Sword: beating a character drains 1 Magic     */
    int save;           /* Shield: lose, but roll >= this and it is a draw */
    int banner;         /* Banner of Hope: rolled at the start of a round */
    int goanywhere;     /* Royal Cape: 2 Magic and you are anywhere       */
    const char *text;
} Artifact;
extern const Artifact art_proto[ART_N];

/* The real Guardians all levy a toll before the battle proper -- Agony,
 * Lightning, Shadow Battle, the Anti-magic Aura -- so the fight starts
 * with you already lessened.  That toll is what makes them Guardians
 * rather than merely large Creatures. */
typedef struct {
    const char *name;
    unsigned ctype;             /* the Guardians have types too */
    int greater, str, will, str_first, exp;
    int lives;                  /* Tortured Souls and Shapeless Things: 3 */
    int t_health, t_magic;      /* paid before a blow is struck */
    int no_spells;              /* Anti-magic Aura              */
    AdvSpecial special;         /* AS_TWO_BATTLES, and the rest */
    const char *text;
} Guardian;

/* The real 22-card Chance deck, read off the cards themselves. */
typedef enum {
    CH_TERRAIN,     /* an Adventure card in EVERY space of one terrain */
    CH_TRAINING,    /* a new Ability at one named guild                */
    CH_STOCK,       /* the Village or City takes delivery              */
    CH_BOON,        /* you gain, everyone else gains less              */
    CH_CHARITY,     /* the poorest of each kind are helped             */
    CH_PEACE,       /* two consecutive turns this round                */
    CH_PROPHETIC,   /* reveal the next card in an Astral Plane         */
    CH_ECONOMIC     /* the die and the head-count decide               */
} ChanceKind;

typedef struct {
    const char *name;
    ChanceKind  kind;
    SpaceKind   terrain;    /* CH_TERRAIN                              */
    Guild       guild;      /* CH_TRAINING                             */
    int         rare;       /* CH_STOCK: 1 = City/Rare, 0 = Village    */
    int         count;      /* items to stock, or gold/health/magic    */
    int         other;      /* what everyone else gets                 */
    int         copies;
    const char *text;
} Chance;
extern const Chance chance_proto[];

typedef struct {
    char        name[24];
    const char *cls;
    Guild       guild_a, guild_b;   /* trains cheaply at these two */
    /* Strength IS Health and Willpower IS Magic: the right-hand cubes are
     * what you have now, the left-hand ones are what you have lost. */
    int str_now,  str_lost;
    int will_now, will_lost;
    int gold, exp;
    int idx;                        /* position on the ring */
    int alive, ai;
    int artifacts;
    unsigned art_mask;       /* which Artifacts, not merely how many */
    int hope_round;          /* Banner of Hope rolled 1-3: +1 all Battles */
    RaceId race;             /* RACE_NONE when playing without Races      */
    int banked;              /* Dwarf: Gold set aside, and safe           */
    int rage_on;             /* Goblin: the Rage token is placed          */
    int in_realm;            /* 1 while inside the Dragon Realm           */
    int dr_path, dr_pos;     /* which path, and how far along it          */
    int dragon_heart;        /* taken from the Lake of Fire               */
    int in_water;            /* 1 while under the sea                     */
    int wr_pos;              /* which of the 24 squares                   */
    int bubbles, bag;        /* air left, and how big the bag was         */
    int pearl;               /* the Pearl of the Abyss                    */
    int wr_tries;            /* how often she has gone under                */
    int dr_tries;            /* and how often into the Dragon Realm          */
    int idle;                /* rounds spent standing still                 */
    unsigned ctype;          /* the Beast and Path of the Dead make you one */
    int potion_str, potion_will;  /* a potion's bonus, until the turn ends */
    int divine;              /* Scroll of Divine Will: the next roll is fixed */
    int free_training;       /* Letter of Recommendation: one lesson unpaid  */
    int refought;            /* Fanaticism has already been used this fight */
    int spared;              /* Sanctity of Life has already saved her once */
    int looped;              /* Time Loop has already fired this round      */
    int safe_until_turn;            /* paid lodging in the Village */
    int forged_this_round;          /* Counterfeiting is once a round */
    int blessed;                    /* Chapel: +2 on the next first roll */
    int team;                       /* Team Play: -1 when playing alone   */
    /* The caps MOVE: a Human carries one Ability more, Dragon Realm raises
     * both to 8, and the Royal Signet Ring adds three of each again.  The
     * arrays are sized for the largest of those, and the runtime limit is
     * card_cap() -- sizing them to MAX_ITEMS was a one-past-the-end write
     * the moment Races went in. */
    int items[CARD_SLOTS];   int nitems;
    int dmg[MAX_ITEMS];             /* face-down: damaged, and unusable */
    int abils[CARD_SLOTS];   int nabils;
} Player;

typedef struct { int region, idx; } CellRef;

/* --- data.c ------------------------------------------------------- */
extern const Space ring[RING_N];
extern const char *guild_name[G_COUNT];
extern const Adventure   adv_proto[];
extern const Item        item_proto[];
extern const AbilityCard abil_proto[];
extern const Guardian    guard_proto[];

/* effective characteristics: what you have now, plus what you carry */
/* Which Artifacts a character actually holds.  `artifacts` stays the count,
 * because the win condition and half the UI are written in terms of it. */
#define has_art(p, id) (((p)->art_mask >> (id)) & 1u)

int eff_str(const Player *p);
int eff_will(const Player *p);

/* --- board.c ------------------------------------------------------ */
void ring_cell(int idx, int *row, int *col);
int  ring_step(int idx, int delta);
int  nearest_port(int from, int dir);
int  plane_adjacent(int plane, int which);   /* the 2 spaces it is attacked from */
int  plane_at(int idx);
const char *plane_name(int plane);                      /* plane reachable from here, or -1 */

/* --- game.c ------------------------------------------------------- */
extern Player players[MAX_PLAYERS];
extern int    nplayers, cur_player;
int  roll(void);

#endif

/* --- ui.c --------------------------------------------------------- */
void ui_set_log(const char *path);   /* call before ui_init */
void ui_init(void);
int  ui_wait_key(int ms);            /* ms < 0 blocks; ERR on timeout */
void ui_end(void);
void ui_draw(void);
/* Tell the compiler this is a printf: with several hundred call sites a
 * mismatched format is otherwise silent until it prints nonsense. */
void glog(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
/* so the in-game help can show what is in force, not just its name */
unsigned ui_seed(void);
int      ui_delay(void);
int  ui_prompt(const char *msg, const char *valid);
int  ui_prompt_row(void);   /* the row full-screen menus must stay above */
int  ui_too_small(void);    /* window too small: message shown, 1 returned */
void ui_snapshot(void);     /* dump the current screen (tools/screenshot.sh) */
void ui_shot_help(void);    /* draw the help page without waiting on a key   */
void ui_banner(const char *msg);

/* --- the Dragon Realm (Expansion 1) --------------------------------
 *
 * A separate board of three paths that start at the Gate and climb to the
 * Lake of Fire, where the Greater Guardian, the Artifact and the Dragon
 * Hearts wait.  The rulebook's own summary of the choice you make on
 * entering: "the middle path is the shortest, but it has the fewest
 * positive cards.  The path on the right is the longest, but it has the
 * most positive cards."  So the three are deliberately different lengths.
 *
 * The Realm replaces one Astral Plane -- the Gate to the Dragon Realm is
 * shuffled in among the Lesser Guardians, so nobody knows which Plane it
 * is until it is revealed. */
#define DR_PATHS      3
#define DR_MAX_LEN    8
#define DR_ENTRY_GOLD 3          /* "rent a drake" to get in            */

/* The Realm's own cards, which never reach the main board.  Fifteen
 * negative (Obstacles and Creatures) and twelve positive (Items, Abilities
 * and Opportunities); twenty-one go on the board and, as the rulebook says,
 * "you will have three of each type of card left over". */
typedef struct {
    const char *name;
    int negative;                /* 1 = an Obstacle or a Creature       */
    int str, will;               /* a Creature's numbers; 0 if neither  */
    int lives;
    int exp;
    int obstacle;                /* it stays on its space for ever      */
    int toll_health, toll_magic; /* paid before you may pass or fight   */
    int gift_gold, gift_exp, gift_heal, gift_magic;
    int gift_str, gift_will;     /* a cube from the bank                */
    const char *text;
} DragonCard;
extern const DragonCard dr_proto[];

typedef struct {
    int len;                     /* how many spaces this path has       */
    int card[DR_MAX_LEN];        /* adv_proto index, or -1 for an empty */
    int seen[DR_MAX_LEN];        /* has it been turned face up          */
} DragonPath;

/* --- the Water Realm (Expansion 2) ---------------------------------
 *
 * A 4x6 grid rather than paths, and the defining rule is not the board but
 * the air: you buy a Bubble Bag on the way in, spend one Bubble at the
 * start of every round, and when they run out you pay a Health a round
 * instead.  Out of both is drowning.  The bag cannot be refilled inside.
 *
 * Combat there costs -1 for every hand holding an Item, so a two-handed
 * weapon is -2 -- the one rule in either expansion that inverts the usual
 * "more gear is better". */
#define WR_W          4
#define WR_H          6
#define WR_SPACES     (WR_W * WR_H)
#define WR_BAG_GOLD   1          /* a bag of three Bubbles              */
#define WR_BAG_SIZE   3          /* Bubbles per Gold                    */
#define WR_MIN_AIR   12          /* six moves in, six back, plus tolls  */

typedef struct {
    const char *name;
    int negative;
    int str, will, lives, exp;
    int obstacle;                /* Reefs and Whirlpools stay put       */
    int no_entry;                /* Reefs: the space cannot be entered  */
    int toll_bubble;             /* Bubbles taken before anything else  */
    int toll_health, toll_magic;
    int gift_gold, gift_exp, gift_heal, gift_magic, gift_bubble;
    int gift_str, gift_will;
    const char *text;
} WaterCard;
extern const WaterCard wr_proto[];

/* --- the five Astral Planes (game.c) ------------------------------ */
typedef struct {
    int lesser;      /* 1 = the Lesser Guardian still stands  */
    int greater;     /* 1 = the Greater Guardian still stands */
    int artifact;    /* 1 = the Artifact is still here        */
    int revealed;    /* has anyone looked inside yet          */
    int closed;      /* someone took the Artifact             */
} Plane;
extern Plane planes[PLANES];

/* What is lying on the world, so the board can actually show it. */
#define MAX_ON_SPACE 4
extern int space_adv[RING_N][MAX_ON_SPACE];   /* adv_proto indices */
extern int space_nadv[RING_N];
extern int space_item[RING_N];                /* on sale here, or -1  */
extern int guild_abil[G_COUNT];               /* taught here, or -1   */
Guild guild_of(SpaceKind k);
