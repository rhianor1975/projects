/* Rokugan -- the Legend of the Five Rings collectible card game.
 *
 * Two players, two decks each.  The Dynasty deck feeds four face-down
 * Provinces and holds what you recruit: Personalities, Holdings, Events,
 * Regions.  The Fate deck is your hand: Followers, Items, Strategies,
 * Spells and Rings.  Gold comes from bowing your Stronghold and Holdings.
 *
 * You win by starting your turn on 40 Family Honor, by destroying all
 * four of your opponent's Provinces, or with five Rings in play.  You
 * lose at -20.  Those four held from 1995 to 2015; the card pool and the
 * finer rules changed by arc, and the player picks the arc ("era") when
 * the game starts.  Where an era changes a rule, Era says so.
 *
 * The same three rules as Court of Treasons next door:
 *
 *   1. Every change to a Game goes through l5r_apply().
 *   2. A seat decides from what it may see.  The machine opponent reads
 *      its own hand and the table, never the other hand or the decks.
 *   3. The authority sends JSON views and receives action numbers.
 *
 * The cards come from data/ERA/cards.tsv, built from the Oracle of the
 * Void by tools/build-cards.py.  Their text is read into Abilities; what
 * could not be read is still played, by a person, by hand.
 */
#ifndef ROKUGAN_H
#define ROKUGAN_H

#include <stdio.h>

#define NPROV        4
#define HAND_MAX     8      /* checked at the end of your turn             */
#define START_HAND   5
#define HONOR_WIN   40      /* at the start of your own turn               */
#define HONOR_LOSE -20      /* at any moment                               */
#define TURN_CAP   100      /* a draw, so that a headless batch ends       */
#define MAX_DEFS  2600
#define MAX_INST   240      /* both decks and both strongholds             */
#define MAX_ACTS  1024
#define MAX_ABIL     6
#define MAX_EFF      5
#define LOG_KEEP    16
#define HR_NONE   -99       /* a printed "-": no Honor Requirement         */

/* ------------------------------------------------------------------ eras */
/* A rule that changed between arcs is a field here, never an if on the
 * era's name somewhere in the rules. */
typedef struct {
    const char *key, *name;
    int clan_discount;      /* gold off a Personality of your own clan     */
    int recruit_honor;      /* gain Personal Honor recruiting your clan    */
    int holdings_bowed;     /* Holdings enter play bowed                    */
    int pool_per_phase;     /* gold keeps to the end of the phase, rather
                             * than vanishing after each payment           */
    int kill_honor;         /* attacker's honor per card destroyed by
                             * battle resolution                           */
} Era;
extern const Era eras[];
extern const int neras;

/* ----------------------------------------------------------------- cards */
typedef enum {
    T_STRONGHOLD, T_PERSONALITY, T_HOLDING, T_EVENT, T_REGION,
    T_FOLLOWER, T_ITEM, T_STRATEGY, T_SPELL, T_RING, T_OTHER, T_COUNT
} CType;
extern const char *type_name[T_COUNT];

typedef enum { TM_BATTLE, TM_LIMITED, TM_OPEN, TM_ENTER, TM_PRODUCE,
               TM_STATIC, TM_REVEAL, TM_COUNT } Timing;

typedef enum {
    TG_NONE, TG_SELF, TG_OPERS, TG_EPERS, TG_EFOL, TG_ECARD, TG_EUNIT,
    TG_OUNIT, TG_EHOLD, TG_APERS, TG_AFOL, TG_COUNT
} Target;

typedef enum {
    E_FORCE, E_CHI, E_DESTROY, E_BOW, E_STRAIGHTEN, E_HOME, E_GAIN, E_LOSE,
    E_OLOSE, E_DRAW, E_PRODUCE, E_RANGED, E_MELEE, E_FEAR,
    E_ATTFORCE, E_DEFFORCE, E_PSTR, E_PH, E_KW, E_ATTACHONLY, E_DISCOUNT,
    E_NOENLIGHTEN, E_COUNT
} EffOp;

enum { CO_BOW = 1, CO_DESTROY = 2 };

typedef struct { EffOp op; int n; } Eff;

typedef struct {
    Timing timing;
    int    cost;            /* CO_ bits: bow or destroy this card           */
    int    gold;            /* gold cost of the ability                     */
    Target target;
    int    neff;
    Eff    eff[MAX_EFF];
    char   arg[32];         /* kw:, attachonly:, discount: argument         */
} Ability;

enum { KW_UNIQUE = 1, KW_SHUGENJA = 2, KW_CAVALRY = 4, KW_COURTIER = 8,
       KW_MONK = 16, KW_SAMURAI = 32 };

typedef struct {
    int   oid;              /* the Oracle's card id                         */
    char  name[64];
    CType type;
    int   fate;             /* Fate deck card, else Dynasty                 */
    char  clan[28];
    char  kwtext[160];
    unsigned kw;
    int   cost, force, chi, hreq, ph, focus, gold, pstr, shonor;
    int   nabil;
    Ability abil[MAX_ABIL];
    int   understood;       /* every line of its text was read              */
} Def;

extern Def defs[MAX_DEFS];
extern int ndefs;
int  data_load(const char *path, char *err, int errlen);
int  def_by_oid(int oid);
int  def_by_name(const char *name);
int  fx_parse(const char *enc, Def *d, char *err, int errlen);
int  fx_overrides(const char *path, char *err, int errlen);

/* -------------------------------------------------------------- instances */
typedef enum {
    Z_DYNDECK, Z_FATEDECK, Z_HAND, Z_PROVINCE, Z_PLAY, Z_ATTACHED,
    Z_DYNDISC, Z_FATEDISC, Z_REMOVED
} Zone;

typedef struct {
    int  def, owner;
    Zone zone;
    int  order;     /* deck position; lower is nearer the top              */
    int  prov;      /* Z_PROVINCE: which; Z_ATTACHED region: the province   */
    int  down;      /* face down in a province                             */
    int  bowed;
    int  host;      /* Z_ATTACHED: the Personality carrying it, else -1     */
    int  fbonus, cbonus;   /* until the end of the turn                    */
    int  at;        /* -1 home, else the province (of the defender) whose
                     * battlefield this unit is at                         */
} Inst;

typedef struct {
    int  clan;      /* index into the def's clan, via the stronghold        */
    int  honor;
    int  pool;      /* gold produced and not yet spent                     */
    int  alive[NPROV];
    int  stronghold;
    int  first_turn;
    int  won_battle;
    int  face;      /* the Oracle id of the clan's champion, for the portrait */
} Player;

typedef enum {
    PH_ACTION,      /* alternating Open and Limited actions               */
    PH_ATTACK,      /* the active player attacks, or doesn't              */
    PH_ASSIGN,      /* attacker sends units at provinces                  */
    PH_DEFEND,      /* defender meets them                                */
    PH_BATTLE,      /* battle actions at one battlefield, then resolution */
    PH_DYNASTY,     /* recruit from your provinces                        */
    PH_DISCARD,     /* down to eight                                      */
    PH_OVER
} Phase;
extern const char *phase_name[];

typedef enum {
    A_PASS, A_BUY, A_CYCLE, A_PLAY, A_USE, A_ATTACK, A_NOATTACK,
    A_ASSIGN, A_DONE, A_DISCARD, A_COUNT
} AKind;

/* A_PLAY: a card from hand; A_USE: an ability of a card in play.  abil
 * says which of the card's abilities; tgt is its target (or -1); perf is
 * the performer -- the Shugenja casting a Spell, the Personality taking a
 * Follower or Item. */
typedef struct {
    AKind k;
    int src, abil, tgt, perf, prov;
} Action;

typedef enum { WIN_NONE, WIN_HONOR, WIN_MILITARY, WIN_ENLIGHTEN,
               WIN_DISHONOR, WIN_DECK, WIN_DRAW } WinHow;
extern const char *win_name[];

typedef struct {
    unsigned long rng;
    const Era *era;
    Inst   c[MAX_INST];
    int    nc;
    Player p[2];
    int    turn, active;
    Phase  phase;
    int    priority;    /* who acts next in an alternating phase           */
    int    passes;      /* consecutive passes                               */
    int    battle;      /* PH_BATTLE: the defender's province               */
    int    winner;
    WinHow how;
    int    check;       /* rules invariants armed                           */
    int    counts[2];   /* each player's card count, for the invariant      */
    FILE  *trace;
    char   log[LOG_KEEP][200];
    int    nlog;
} Game;

/* rules.c */
int  l5r_setup(Game *g, const Era *era, const int *deck0, int n0,
               const int *deck1, int n1, unsigned long seed);
int  l5r_decider(const Game *g);
int  l5r_legal(const Game *g, Action *out);
void l5r_apply(Game *g, Action a);
void l5r_label(const Game *g, Action a, char *buf, int n);
void l5r_check(Game *g);

/* what a person may do by hand, for abilities the engine could not read */
typedef enum { M_BOW, M_STRAIGHTEN, M_DESTROY, M_HOME, M_FORCE_UP,
               M_FORCE_DOWN, M_HONOR_UP, M_HONOR_DOWN, M_DRAW, M_DISCARD,
               M_COUNT } Manual;
extern const char *manual_name[M_COUNT];
int  l5r_manual(Game *g, int seat, Manual m, int inst);

/* queries shared by the AI and the view */
int  unit_force(const Game *g, int i);
int  card_chi(const Game *g, int i);
int  prov_strength(const Game *g, int p, int prov);
int  gold_available(const Game *g, int p);
int  buy_cost(const Game *g, int p, int i);
int  provinces_left(const Game *g, int p);
int  rings_in_play(const Game *g, int p);
int  army_force(const Game *g, int p, int prov);   /* at that battlefield */
int  has_followers(const Game *g, int i);
const char *player_clan(const Game *g, int p);
void glog(Game *g, const char *fmt, ...);

/* decks.c */
int  deck_load(const char *path, int *out, int max, char *err, int errlen);

/* ai.c */
int  ai_choose(const Game *g, int seat, const Action *acts, int n);
double ai_eval(const Game *g, int seat);

/* view.c */
void view_write(FILE *f, const Game *g, int seat, const Action *acts, int n);

#endif
