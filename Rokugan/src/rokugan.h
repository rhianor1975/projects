/* Rokugan -- the Legend of the Five Rings collectible card game, as it
 * was played between 1997 and 2000.
 *
 * Two players, two decks each.  The Dynasty deck feeds four face-down
 * Provinces and holds what you buy: Personalities, Holdings, Events.  The
 * Fate deck is your hand: Followers, Items, Strategies, Spells and Rings.
 * Gold comes from bowing your Stronghold and Holdings, and it does not
 * keep -- what is not spent by the end of the phase is lost.
 *
 * You win by beginning your turn on 40 Family Honor, by destroying all
 * four of your opponent's Provinces, or by having all five elemental
 * Rings in play.  You lose by falling to -20.
 *
 * The same three rules as Court of Treasons, next door, and for the same
 * reason -- a client in another process changes nothing here:
 *
 *   1. Every change to a Game goes through l5r_apply().
 *   2. A seat decides from a View.  The machine opponent included: it is
 *      given the hand it holds, never the one it does not.
 *   3. The authority sends JSON views and receives action numbers.
 *
 * Where this deviates from a printed rulebook it says HOUSE RULE beside
 * it.  Those are the places where the period rules either disagree with
 * each other between editions or needed a card pool this one does not
 * have.
 */
#ifndef ROKUGAN_H
#define ROKUGAN_H

#include <stdio.h>

#define NPROV         4     /* "Each player starts with four provinces"   */
#define HAND_MAX      8     /* checked at the end of your turn            */
#define START_HAND    5
#define HONOR_WIN    40     /* at the start of your own turn              */
#define HONOR_LOSE  -20     /* at any moment                              */
#define TURN_CAP    120     /* a draw; a headless batch must end          */
#define MAX_DEFS    256
#define MAX_INST    200     /* both players' decks, stronghold included   */
#define MAX_ACTS    512
#define LOG_KEEP     14

/* ----------------------------------------------------------------- clans */
typedef enum { CL_NONE, CL_CRAB, CL_CRANE, CL_DRAGON, CL_LION, CL_PHOENIX,
               CL_SCORPION, CL_UNICORN, CL_COUNT } Clan;
extern const char *clan_name[CL_COUNT];

/* ----------------------------------------------------------------- cards */
typedef enum {
    T_STRONGHOLD, T_PERSONALITY, T_HOLDING, T_EVENT,          /* Dynasty */
    T_FOLLOWER, T_ITEM, T_STRATEGY, T_SPELL, T_RING,          /* Fate    */
    T_COUNT
} CType;
extern const char *type_name[T_COUNT];
#define IS_DYNASTY(t) ((t) <= T_EVENT)

/* When a Fate card may be played.  Open: either player, in the Action
 * phase.  Limited: only the player whose turn it is, Action phase.
 * Battle: only in a battle, by a player with a unit in it. */
typedef enum { W_NONE, W_OPEN, W_LIMITED, W_BATTLE } When;

/* What a card does, by keyword.  cards.tsv names these in its fx column
 * and data.c refuses to start on one it does not know, so a typo in the
 * card list is a load error rather than a card that silently does
 * nothing. */
typedef enum {
    FX_NONE,
    /* holdings */
    FX_PROVSTR,     /* your provinces +N strength while in play         */
    FX_HONORBOW,    /* bow: gain N honor, instead of producing gold     */
    /* events, resolved as they are turned face up */
    FX_EV_HONOR,    /* you gain N honor                                 */
    FX_EV_DRAW,     /* you draw N fate cards                            */
    FX_EV_STORM,    /* bow every personality your opponent controls     */
    FX_EV_PLAGUE,   /* destroy every personality with Chi N or less     */
    /* strategies and spells */
    FX_FORCE,       /* battle: your unit in this battle +N force        */
    FX_AMBUSH,      /* battle, defending only: your unit +N force       */
    FX_KILLFOL,     /* battle: destroy an opposing follower, force <= N */
    FX_RALLY,       /* battle: straighten one of your units             */
    FX_WITHDRAW,    /* battle: one of your units leaves, bowed          */
    FX_DUEL,        /* open: challenge an opposing personality          */
    FX_GAINHONOR,   /* gain N honor                                     */
    FX_LOSEHONOR,   /* your opponent loses N honor                      */
    FX_INSULT,      /* bow an opposing personality; its side loses N    */
    FX_ASSASSIN,    /* destroy an opposing personality, PH <= N         */
    FX_DRAW,        /* draw N fate cards                                */
    FX_STRAIGHTEN,  /* straighten one of your units                     */
    /* rings: the condition that lets one be played */
    FX_RING_EARTH, FX_RING_WATER, FX_RING_FIRE, FX_RING_AIR, FX_RING_VOID,
    FX_COUNT
} Fx;
extern const char *fx_name[FX_COUNT];

enum { KW_UNIQUE = 1, KW_SHUGENJA = 2, KW_CAVALRY = 4 };

typedef struct {
    char name[48];
    CType type;
    Clan  clan;
    int   cost, force, chi, hreq, phonor, focus;
    int   gold;            /* production, holdings and strongholds    */
    int   pstr, shonor;    /* strongholds: province strength, honor   */
    When  when;
    Fx    fx;
    int   n;               /* the fx's number                          */
    unsigned kw;
    int   copies[CL_COUNT];/* how many of this card each clan's deck holds */
    char  text[160];
} Def;

extern Def defs[MAX_DEFS];
extern int ndefs;
int  data_load(const char *path, char *err, int errlen);
int  def_find(const char *name);

/* ----------------------------------------------------------- instances */
typedef enum {
    Z_DYNDECK, Z_FATEDECK, Z_HAND, Z_PROVINCE, Z_PLAY, Z_ATTACHED,
    Z_DYNDISC, Z_FATEDISC
} Zone;

typedef struct {
    int  def;
    int  owner;
    Zone zone;
    int  order;      /* position in a deck; lower is nearer the top     */
    int  prov;       /* Z_PROVINCE: which province                      */
    int  down;       /* face down in a province                         */
    int  bowed;
    int  host;       /* Z_ATTACHED: the personality carrying this       */
    int  bonus;      /* force until the end of the battle               */
    int  inbattle;   /* 1 attacking, 2 defending, 0 not in a battle      */
    int  target;     /* the province being fought over, while inbattle  */
} Inst;

typedef struct {
    Clan clan;
    int  honor;
    int  pool;              /* gold produced and not yet spent        */
    int  alive[NPROV];
    int  stronghold;        /* instance                               */
    int  cycled;            /* HOUSE RULE: one province discard a turn */
    int  won_battle;        /* this turn, for the Ring of Fire        */
} Player;

/* ----------------------------------------------------------------- phases */
typedef enum {
    PH_ACTION,      /* alternating open and limited actions               */
    PH_ATTACK,      /* the active player declares an attack, or doesn't   */
    PH_ASSIGN,      /* attacker sends units at provinces                  */
    PH_DEFEND,      /* defender meets them                                */
    PH_BATTLE,      /* battle actions at one province, then resolution    */
    PH_DYNASTY,     /* buy from your provinces                            */
    PH_DISCARD,     /* down to eight                                      */
    PH_DUEL_ANSWER, /* the challenged side accepts or refuses             */
    PH_DUEL_FOCUS,  /* each side focuses cards until both strike          */
    PH_OVER
} Phase;
extern const char *phase_name[];

typedef enum {
    A_PASS, A_BOWGOLD, A_BUY, A_CYCLE, A_PLAY, A_ATTACK, A_NOATTACK,
    A_ASSIGN, A_DONE, A_DISCARD, A_ACCEPT, A_REFUSE, A_FOCUS, A_STRIKE,
    A_COUNT
} AKind;

typedef struct {
    AKind k;
    int src;     /* an instance, or -1                                   */
    int tgt;     /* an instance, or -1                                   */
    int prov;    /* a province index, or -1                              */
} Action;

typedef enum { WIN_NONE, WIN_HONOR, WIN_MILITARY, WIN_ENLIGHTEN,
               WIN_DISHONOR, WIN_DRAW } WinHow;
extern const char *win_name[];

typedef struct {
    unsigned long rng;
    Inst  c[MAX_INST];
    int   nc;
    Player p[2];
    int   turn, active;
    Phase phase;
    int   priority;          /* who acts next in an alternating phase     */
    int   passes;            /* consecutive passes                         */
    int   attacked;          /* the active player declared this turn      */
    int   battle;            /* the province being resolved, PH_BATTLE    */
    /* a duel in progress */
    int   duel_a, duel_b;    /* challenger, challenged (instances)        */
    int   duel_focus[2];
    int   duel_ret;          /* the phase to go back to                   */
    int   duel_prio;
    int   winner;
    WinHow how;
    int   check;             /* rules invariants armed                    */
    FILE *trace;
    char  log[LOG_KEEP][160];
    int   nlog;              /* total lines ever logged                   */
} Game;

/* rules.c */
void l5r_setup(Game *g, Clan a, Clan b, unsigned long seed);
int  l5r_decider(const Game *g);
int  l5r_legal(const Game *g, Action *out);
void l5r_apply(Game *g, Action a);
void l5r_label(const Game *g, Action a, char *buf, int n);
void l5r_check(Game *g);

/* queries the AI and the view share */
int  card_force(const Game *g, int i);      /* unit force: person + attachments */
int  card_chi(const Game *g, int i);
int  prov_strength(const Game *g, int p, int prov);
int  gold_available(const Game *g, int p);  /* pool + unbowed production  */
int  buy_cost(const Game *g, int p, int i);
int  count_in(const Game *g, int p, Zone z);
int  rings_in_play(const Game *g, int p);
int  provinces_left(const Game *g, int p);
int  ring_ready(const Game *g, int p, Fx fx);
void glog(Game *g, const char *fmt, ...);

/* ai.c */
int  ai_choose(const Game *g, int seat, const Action *acts, int n);

/* view.c */
void view_write(FILE *f, const Game *g, int seat, const Action *acts, int n);

#endif
