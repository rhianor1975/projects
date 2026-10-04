/* Court of Treasons -- a card game of ambition, betrayal and deceit.
 *
 * design.xml is the design and it is the authority.  Where this header
 * says something the design does not, the comment says so.
 *
 * The shape of the thing: two to four Houses build three public Standings,
 * spend them as a per-turn Levy, and quietly build hidden Bonds against
 * each other -- Servitude (what you owe me) and Revolution (what I am
 * plotting against you).  Promises are cards: they come due, they are kept
 * or broken in the open, and the piles they land in are the only thing
 * trust is made of.  A House wins by taking the Throne, by being voted
 * king, by buying the crown, or by outlasting the realm.
 *
 * There is no board.  The board is replaced by a choice of deck, which is
 * a decision rather than a die roll.
 *
 * Three structural rules are worth stating here because every file obeys
 * them and the networking decision (design.xml, <networking>, undecided)
 * depends on all three:
 *
 *   1. State and action in, state out.  Every change to a Game goes
 *      through court_apply().  Nothing else writes to a Game.
 *   2. A seat decides from a View, never from a Game.  The View is built
 *      by court_view() and contains only what that seat is entitled to
 *      see.  The AI cannot cheat because it is not given the chance.
 *   3. Simultaneous choices are collected before any is revealed.  There
 *      is no turn-order advantage in a vote, a commitment, or a Reckoning.
 *
 * If the single-player build is written that way from the first commit,
 * all three networking options stay open.  If it is not, only the trusted
 * host remains, and that choice will have been made by accident.
 */
#ifndef COURT_H
#define COURT_H

#include <stddef.h>
#include <stdio.h>

/* Two seats.  Expanding this game means more Houses to choose from, not
 * more chairs at the table -- the same way a Magic deck is a colour and
 * not a player -- so HouseId grows and this does not. */
#define MAX_HOUSES        2
#define MAX_LORDS        12   /* Minor Lords in play at once             */
#define HAND_LIMIT        7   /* "Hand limit 7, checked at end of turn"   */
#define STANDING_MAX     15
#define STANDING_MIN      0
#define SERVITUDE_MAX     5
#define REVOLUTION_MAX    5
#define UNREST_MAX        5
#define GRIEVANCE_MAX    10
#define FAVOUR_MAX       12   /* the Court's track; the crown is a runtime
                              * value while it is being measured          */
#define FAVOUR_BOUGHT_MAX 3   /* "The last two points cannot be bought."  */
#define AMBITIONS_DEALT   6
#define AMBITIONS_NEEDED  3   /* 4 of 6 in an Epic game                   */
#define ROUND_CAP        30
#define MAX_PROMISES     64
#define MAX_CARDS      1024
#define MAX_PENDING      32   /* instigators and other face-down things   */

/* ---------------------------------------------------------------- houses */
/* Eight Houses and two chairs.  MAX_HOUSES is the seats; H_COUNT is the
 * pool they are drawn from, so adding four does not change the shape of
 * a game, only how much of the game you have not seen.
 *
 * The four added 2026-09-20 each champion a subsystem that was built
 * and idle.  Measured over 200 games before they existed: lords held
 * per House 0.08, Promises 1.69 a game, Grievance 0.80 of a maximum
 * ten.  A system with no House that needs it does not get used, and
 * these are the Houses that need them. */
typedef enum { H_RAVENMARK, H_VIPREN, H_GOLDWYN, H_ALDEMAR,
               H_LEOWARD,    /* the Lion,  the Word   -- Promises  */
               H_STONEGARTH, /* the Ox,    the Yoke   -- Bonds     */
               H_WULFREN,    /* the Wolf,  the Grudge -- Grievance */
               H_EVERHOLD,   /* the Boar,  the Beast  -- Unrest    */
               H_COUNT } HouseId;

/* A Minor Lord's Trait is the whole of its rules text, because the Trait
 * is what a Lever can reach. */
typedef enum { LT_INDEBT, LT_AFRAID, LT_AMBITIOUS, LT_PROUD, LT_OWED,
               LT_COUNT } LordTrait;

/* Military, Capital, Gold.  The three are not interchangeable: Military
 * fights, Capital votes, Gold buys.  Grievance is not a resource -- it is
 * a tracker that cards happen to charge in -- but costs name it, so it
 * rides along in the same array and the code says so where it matters. */
typedef enum { R_MIL, R_CAP, R_GOLD, R_COUNT } Res;
typedef enum { RC_MIL, RC_CAP, RC_GOLD, RC_GRV, RC_COUNT } CostRes;

/* ----------------------------------------------------------------- cards */
/* Nine decks, down from seventeen.  A deck on a table is a pile you
 * point at; a deck on a screen is a menu item, and one Draw action a
 * turn meant reading nine options before acting.  The satellites were
 * folded into the three decks whose resource already paid for them. */
typedef enum {
    D_RAVENMARK, D_VIPREN, D_GOLDWYN, D_ALDEMAR,
    D_LEOWARD, D_STONEGARTH, D_WULFREN, D_EVERHOLD,
    D_WAR,          /* + Combat                                          */
    D_POLITICAL,    /* + Council, Promise                                */
    D_INTRIGUE,     /* + Lever, Minor Lords, Instigator                  */
    D_AMBITION, D_WORLD, D_COURT, D_THRONE,
    D_DEAD,         /* Ghost + Resurrection; a variant only              */
    D_CATACLYSM,    /* "a World card with a larger hammer"; never chosen  */
    D_COUNT
} DeckId;

typedef enum {
    T_PLAY, T_INSTANT, T_REACTION, T_PERMANENT,
    T_LEVER, T_AMBITION, T_PROMISE, T_ONDRAW, T_LORD
} CardType;

typedef enum {
    C_CORE, C_GROWTH, C_COMBAT, C_BOND, C_PROMISE,
    C_REPUTATION, C_COUNCIL, C_CHAOS
} Category;

/* The public state a card inspects, if any.  The design's <reads>
 * attribute, carried through the extractor so the AI can be told which
 * cards are reputation weapons without parsing English at runtime. */
typedef enum {
    READS_NONE = 0,
    READS_BROKEN   = 1 << 0,
    READS_KEPT     = 1 << 1,
    READS_GRIEVANCE= 1 << 2,
    READS_UNREST   = 1 << 3,
    READS_STANDING = 1 << 4,
    READS_THRONE   = 1 << 5,
    READS_FAVOUR   = 1 << 6
} Reads;

/* The opcodes.  One per distinct thing a card does, generated from the
 * effect text by gen-cards.py, which prints any card it could not read.
 *
 * The design's writing rule is the reason this is possible at all:
 * "Effects name Levy or Standing explicitly.  '+2 Military' alone is
 * ambiguous and is not acceptable in this design; the parent game's
 * classifier could not tell them apart and it cost a fortnight."
 */
typedef enum {
    OP_NONE = 0,
    /* --- resources ------------------------------------------------- */
    OP_LEVY,              /* a: res  b: amount   -- to self, this turn   */
    OP_STANDING,          /* a: res  b: amount   -- permanent, to self   */
    OP_LEVY_STEAL,        /* a: res  b: amount   -- from target          */
    OP_LEVY_STEAL_LP,     /* a: res  b: amount   -- from Lord Paramount  */
    OP_STANDING_LOSS,     /* a: res(-1 = chooser's pick) b: amount       */
    OP_STANDING_LOSS_LP,  /* b: amount, LP chooses which                 */
    OP_LEVY_ALL_LOSE,     /* a: res  b: amount   -- every House, you too */
    OP_LEVY_ALL_GAIN,     /* a: res  b: amount   -- every House, you too */
    OP_STANDING_ALL_LOSS, /* a: res  b: amount   -- every House          */
    OP_MERC_MARKET,       /* b Military Levy for 3 Gold, open to all     */
    OP_LEVY_PER_KEPT,     /* a: res  -- +b Levy per Promise kept to you  */
    OP_LEVY_KEEP,         /* a: res  -- that Levy survives end of turn   */
    OP_VASSAL_LEVY,       /* a: res  b: amount, to each revealed vassal  */
    /* --- unrest and grievance -------------------------------------- */
    OP_UNREST_CLEAR,      /* a: res (-1 any)  b: amount, -1 means all    */
    OP_UNREST_TARGET,     /* b: amount to target                         */
    OP_UNREST_SELF,       /* b: amount to self (a rider, never alone)    */
    OP_CRACKDOWN,         /* clear b Unrest, every other House +1 Grv    */
    OP_GRIEVANCE_SELF,    /* b: amount (a rider on a larger effect)      */
    OP_GRIEVANCE_TARGET,  /* b: amount                                   */
    OP_UNREST_ALL,        /* b: amount to every House                    */
    OP_NO_TAX,            /* no tax this round; the LP gains b Unrest    */
    OP_REGENCY,           /* you take the Lord Paramount's tax income    */
    /* --- bonds ------------------------------------------------------ */
    OP_SERVITUDE,         /* b: amount.  Needs a lever; this is one.     */
    OP_BOND_RIDER,        /* a: BondRider, hung on the Bond just advanced*/
    OP_LORD_TRAIT,        /* a: LordTrait -- a Minor Lord card, not a play*/
    OP_LORD_FREE,         /* free a revealed lord; its House +b Unrest   */
    OP_LORD_DIRECT,       /* direct one of the other House's lords       */
    OP_REVOLUTION_ZERO,   /* set one of your vassals' Revolution to 0    */
    OP_REVOLUTION_FREEZE, /* it cannot advance next round                */
    OP_REVOLUTION_ALL,    /* b: amount to every Bond in play             */
    OP_REVOLUTION_THEIRS, /* b: amount, to lords the OTHER House holds   */
    OP_REVOLT_CANCEL,     /* cancel a revolt; vassal loses b Mil Standing*/
    OP_PARDON,            /* end a revolt and take b Servitude           */
    OP_MANUMIT,           /* free any revealed vassal; its Lord +1 Unrest*/
    OP_SPY,               /* learn a Revolution count you are party to   */
    OP_SPY_ANY,           /* learn any, including Bonds you are not in   */
    OP_EXPOSE,            /* reveal a Revolution count to everyone       */
    OP_SELL_SECRET,       /* reveal a Bond you are not in; +b Gold Levy  */
    OP_INSTIGATOR,        /* b: extra Instigators this turn              */
    OP_INSTIGATOR_REDIRECT,
    /* --- promises --------------------------------------------------- */
    OP_PROPOSE,           /* a: term  b: consideration cap               */
    OP_PROPOSE_DOUBLE,    /* a: term, to two Houses; only one can be kept*/
    OP_PROPOSE_BONUS,     /* a: term  b: Capital Standing if kept        */
    OP_PROPOSE_PUBLIC,    /* a: term; breaking costs every House 2 Grv   */
    OP_PROMISE_CANCEL,    /* discarded, not broken                       */
    OP_PROMISE_LAUNDER,   /* break without the pile.  Once per game.     */
    OP_PROMISE_CLAIM,     /* treat a Promise to another as made to you   */
    OP_PROMISE_COERCE,    /* accept my next Promise or lose b Capital!   */
    OP_BROKEN_TRANSFER,   /* move one Broken Word to a target's pile     */
    OP_CALL_DEBT,         /* 1 Levy of each from each who broke to you   */
    OP_BROKEN_TRUCE,      /* attack through a Peace, breaking it now     */
    OP_RUMOUR,            /* target gains 1 Grievance from every House   */
    OP_REALM_REMEMBERS,   /* every House -1 Capital Standing per Broken  */
    OP_RECKONING_NOW,     /* every Promise in play comes due at once     */
    /* --- combat and the throne -------------------------------------- */
    OP_DECLARE_WAR,       /* b: Grievance cost, 0 when the card pays it  */
    OP_COMBAT_LEVY,       /* b: Military Levy, this combat only          */
    OP_COMBAT_LEVY_DEF,   /* b: the same, defending only                 */
    OP_COMBAT_REDUCE,     /* reduce an attacker's commitment by b        */
    OP_COMBAT_ATT_LOSS,   /* the attacker loses b Military Standing      */
    OP_NO_COMBAT,         /* no combat may be declared this round        */
    OP_LOWEST_GAIN,       /* a: res  b: Levy to whoever is lowest in it  */
    OP_DISCARD_TARGET,    /* the other House discards b cards            */
    OP_GRIEVANCE_SCALE,   /* b: 2 doubles every Grievance, 1 halves it   */
    OP_RANDOM_LOSS,       /* a: res, -1 for a random one   b: Standing   */
    OP_LP_PAY,            /* b Gold each way with the Lord Paramount     */
    OP_NO_ASSASSIN,       /* no assassination attempt this round         */
    OP_SKIP_ACTION,       /* the other House loses b actions next turn   */
    OP_LEVY_GIVE,         /* a: res  b: Levy GIVEN to the other House    */
    OP_SURVIVE,           /* death is refused once, at 1 Standing        */
    OP_COMBAT_LEVY_IF,    /* a: a CombatIf self-test  b: Military Levy   */
    OP_LP_GAIN,           /* a: res  b: signed Levy to the Lord Paramount*/
    OP_SERVITUDE_ALL,     /* b: signed, every Bond on the table at once  */
    OP_NO_SPY,            /* your hidden counts cannot be spied this round*/
    OP_CLAIM_THRONE,      /* you become Throneworthy and may challenge   */
    OP_MAKE_PARAMOUNT,    /* a: 0 lowest Military, 1 most Gold, takes it */
    OP_COALITION,         /* add your Military Levy to another's attack  */
    OP_THRONE_LEVY,       /* b: Military Levy in a Throne challenge only */
    OP_THRONE_BAR,        /* one House may not commit against the Throne */
    OP_THRONE_OATH,       /* each House with a Broken Word commits 2 less*/
    OP_THRONE_DISCOUNT,   /* a: res  b: reduce the purchase price        */
    OP_ASSASSINATE,       /* against the Lord Paramount                  */
    OP_EXTRA_DRAW,        /* a second Draw action, AFTER winning a combat */
    OP_DRAW,              /* b: extra Draw actions, with no condition    */
    OP_DRAW_DECK,         /* a: a DeckId -- resolve one card off that pile*/
    OP_NO_PROMISE,        /* the other House may not offer for b rounds  */
    OP_VOTE_DOUBLE,       /* your lords each cast one extra at the next  */
    OP_TIE_BREAK,         /* a tied Council goes your way, once          */
    OP_VOTE_NULLIFY,      /* the other House's lords do not vote, once   */
    OP_RESTORE,           /* b: 1 the design's baseline, 2 half, 3 full  */
    OP_EXTINGUISH,        /* a claim ended for good; the second death    */
    OP_DRAW_ALL,          /* b: a Draw action for every living House     */
    OP_WIN_GOLD,          /* the Golden Throne: Gold Standing alone wins */
    OP_LEVY_DOUBLE,       /* a: res -- that Levy is doubled this turn    */
    OP_INVEST,            /* a: res -- spend b Levy, gain 1 Standing     */
    OP_CANCEL_NEXT,       /* the other House's next card play fizzles    */
    OP_STEAL_CARD,        /* take a card out of the other House's hand   */
    OP_GIVE_CARD,         /* push one of yours into theirs, at a price   */
    OP_NO_SPOIL,          /* lose the combat, keep what it would cost    */
    OP_SHIELD,            /* a: res -- no Standing lost in it this round */
    OP_TAX_DOUBLE,        /* the Lord Paramount takes twice this round   */
    /* --- council ---------------------------------------------------- */
    OP_COUNCIL,           /* b: a CouncilMod                             */
    OP_VOTE_BUY,          /* b: additional votes bought now              */
    OP_VOTE_GAIN,         /* b: additional votes at the next Council     */
    OP_VOTE_PER_KEPT,     /* +1 vote per b Promises in your Kept pile    */
    OP_VOTE_COMMAND,      /* force one House to vote as you say          */
    OP_VOTE_FREE,         /* vassals vote as they wish, not as commanded */
    OP_VOTE_CHANGE,       /* change your vote after the reveal           */
    OP_NO_CROWNING,       /* no House may be crowned by vote next round  */
    OP_OBJECT_FREE,       /* object to a crowning without Military Levy  */
    /* --- ambitions and the court ------------------------------------ */
    OP_AMBITION_DONE,     /* a: res -- complete a revealed Ambition      */
    OP_FAVOUR_OTHER,      /* b: amount, signed, to the other House      */
    OP_FAVOUR,            /* b: amount, signed, to the House named       */
    OP_FAVOUR_LOCK,       /* no Favour may be bought next round          */
    OP_FAVOUR_CMP,        /* a: 0 more-Broken loses, 1 higher-Capital gains
                           * b: signed amount.  The Court, without preference. */
    OP_COURT_SILENT,      /* the Court does not vote at the next Council */
    /* --- information ------------------------------------------------ */
    OP_PEEK_HAND,         /* look at a target's hand                     */
    /* --- ambition conditions (never played; checked) ----------------- */
    OP_AMB_STANDING,      /* a: res  b: threshold                        */
    OP_AMB_VASSALS,       /* b: revealed lords held at once              */
    OP_AMB_UNBROKEN,      /* b: round to reach with 0 Broken Words       */
    OP_AMB_KEPT,          /* b: Promises in your Word Kept pile          */
    OP_AMB_COMBATS,       /* b: combats won                              */
    OP_AMB_PARAMOUNT,     /* b: rounds begun as Lord Paramount           */
    OP_AMB_RIVAL_UNREST,  /* b: a rival at that Unrest at a Consequence  */
    OP_AMB_BEAT_OATHBREAKER, /* b: Broken Words the beaten House holds   */
    OP_COUNT
} Op;

/* The self-tests OP_COMBAT_LEVY_IF can make.  The pool words these as
 * board-game conditions -- outnumbering the enemy, holding more cards,
 * a target that trusts you -- and each is a thing this engine can
 * actually answer about the two Houses at the table. */
typedef enum {
    CIF_STRONG,      /* your Military Standing is 3 or more      */
    CIF_OUTNUMBER,   /* your Military Levy beats the other's     */
    CIF_MORE_CARDS,  /* your hand is the larger one              */
    CIF_TRUSTED      /* you have kept a Promise to the other     */
} CombatIf;

/* A condition on the thing a card targets.  The design writes these into
 * the effect line -- "against a House with 1 or more Broken Words" -- and
 * they are the whole of the Reputation category, so they are structure
 * rather than prose and the AI is given them as structure. */
typedef enum {
    TC_NONE = 0,
    TC_BROKEN1,     /* 1 or more Broken Words                            */
    TC_BROKEN2,     /* 2 or more                                         */
    TC_UNREST3,     /* 3 or more Unrest                                  */
    TC_DEFEATED,    /* you beat them in combat this round                */
    TC_KEPT,        /* you kept a Promise to them                        */
    TC_DEBT,        /* they owe you an unresolved Promise                */
    TC_POOR_BONUS,  /* +1 more if they have 0 Gold Levy                  */
    TC_PEACE,       /* you hold an unresolved Peace with them            */
    TC_VASSAL,      /* a revealed lord                                   */
    /* A Lever names a Trait and reaches only a lord that carries it.
     * These are the conditions that replaced the ones reading a House --
     * "a House with 3 or more Unrest" became "a lord that is Afraid". */
    TC_LORD_INDEBT, TC_LORD_AFRAID, TC_LORD_AMBITIOUS,
    TC_LORD_PROUD, TC_LORD_OWED, TC_LORD_ANY,
    TC_PARAMOUNT,   /* the Lord Paramount                                */
    TC_COUNT
} TargetCond;

/* Riders a Lever hangs on the Bond it advances. */
typedef enum { BR_NONE = 0, BR_NO_BRUTAL, BR_DOUBLE_REV } BondRider;

/* Promise terms.  The design names six; the seventh is the Sworn Alliance,
 * which is Peace and Aid together and breaks as one. */
/* Aid and the Alliance that contained it are gone: when your only
 * opponent attacks you, nobody can come.  What replaced them is
 * forbearance rather than assistance -- promising not to do a thing you
 * plainly could, which is the only shape of promise two players have. */
typedef enum {
    TERM_VOTE, TERM_PEACE, TERM_TRIBUTE, TERM_MANUMISSION,
    TERM_ABSTENTION, TERM_FORBEARANCE, TERM_RESTRAINT, TERM_DISCLOSURE,
    TERM_ANY, TERM_COUNT
} Term;

typedef enum {
    CM_NONE = 0,
    CM_NO_PARAMOUNT,   /* the Lord Paramount may not be voted for        */
    CM_COMMONS,        /* the House with most Unrest casts 2 votes       */
    CM_SEALED,         /* no votes may be bought or commanded            */
    CM_OATH_READ,      /* every House with a Broken Word loses 1 vote    */
    CM_COURT_ALOUD     /* the Court votes first and aloud                */
} CouncilMod;

/* A card as the generator writes it.  Two effects are enough for every
 * card in the seed set: the design writes riders as ";" clauses and no
 * card carries three.  gen-cards.py asserts that. */
typedef struct {
    const char *id, *name, *text;
    unsigned char deck, type, category;
    signed char   cost[RC_COUNT];   /* Levy, or Grievance in RC_GRV      */
    signed char   perm[RC_COUNT];   /* the "2G!" form: paid from Standing */
    unsigned char reads;
    struct { unsigned char op; signed char a, b, cond; } eff[2];
} Card;

/* ------------------------------------------------------------- promises */
typedef enum { P_OFFERED, P_LIVE, P_KEPT, P_BROKEN, P_VOID } PromiseState;

typedef struct {
    short card;                 /* the object it is; -1 for a bare offer */
    signed char promiser, promisee;
    unsigned char term, state;
    signed char cons_res, cons_amt;  /* consideration, paid on acceptance */
    short due_round;                 /* -1 when the condition is an event */
    unsigned char due_on_attack;     /* Aid: due when the promisee is hit */
    unsigned char due_at_council;    /* Vote and Abstention               */
    signed char bonus_capital;       /* Oath Sworn's Standing if kept     */
    unsigned char public_oath;       /* Public Oath doubles the Grievance */
    unsigned char twin;              /* Two Tongues: index of its pair    */
    short sworn_round;
} Promise;

/* ----------------------------------------------------------- minor lords */
/* A lord in play.  Servitude is held by the House and hidden until the
 * lord is revealed; Revolution is held by the lord and hidden from the
 * House that holds it.  When Revolution passes Servitude the lord
 * abstains at Councils, which is how a hidden track resolves in public
 * without ever printing the number. */
typedef struct {
    short card;
    unsigned char trait;
    signed char   holder;        /* -1 unbound, else the seat holding it */
    unsigned char servitude, revolution, revealed;
    unsigned char frozen;        /* Golden Shackles                      */
    unsigned char double_rev;    /* A Hostage, and Ambitious by nature   */
    unsigned char no_betray;     /* A Marriage, and Proud by nature      */
    unsigned char lord_knows;    /* the holder has spied successfully    */
    unsigned char alive;
} Lord;

/* ---------------------------------------------------------------- house */
typedef struct {
    unsigned char id;
    signed char standing[R_COUNT];
    signed char levy[R_COUNT];
    signed char keep_levy[R_COUNT];  /* Winter Camp: survives end of turn */
    /* Unrest is three tracks, one per resource.  One track let the House
     * suffering it spread the loss where it hurt least; three let the
     * House causing it choose where it hurts most. */
    signed char unrest[R_COUNT];
    signed char grievance;
    unsigned char alive, dead_round;
    /* A House at 0 in all three Standings has fallen, not ended.  Its
     * claim outlives it: it draws from the Ghost deck, haunts once a
     * round, does not vote and cannot win, and may be restored if the
     * Court still recognises it.  `extinguished` is the second death,
     * the one that ends the game -- see <victory> and the Court rule
     * in src/phases.c. */
    unsigned char fallen, extinguished;
    /* Levy handed to this House and never spent.  The whole-game
     * figure has sat near 47% all evening, which reads as "costs are
     * too low"; per House it reads as something else entirely, since
     * a flooded House and a starved one want opposite fixes. */
    int   levy_given, levy_unspent;

    short hand[MAX_CARDS / 8];
    int   hand_n;

    /* Ambitions: dealt face down, revealed as completed.  A completed
     * Ambition cannot be un-completed by later events, so this only ever
     * gains bits. */
    short ambition[AMBITIONS_DEALT];
    unsigned char amb_done[AMBITIONS_DEALT];
    unsigned char throneworthy;

    /* The piles.  Public, permanent, and the only inputs to trust. */
    short kept[MAX_PROMISES], broken[MAX_PROMISES];
    int   kept_n, broken_n;

    /* Counters the Ambitions read. */
    unsigned char combats_won, rounds_paramount;
    unsigned char lords_held;

    /* Once-per-game and once-per-round flags. */
    unsigned char laundered;         /* Vipren's Nothing Was Agreed       */
    unsigned char aldemar_first_break;
    unsigned char no_challenge_until; /* a failed challenge sits out a round */
    unsigned char throne_price[R_COUNT]; /* Underwrite / The Crown Bought */

    unsigned char survives;          /* a death refused once              */
    signed char favour;              /* two-player Court only             */
    unsigned char favour_bought;     /* this round, capped at +3 total    */
} House;

/* ----------------------------------------------------------------- deck */
typedef struct {
    short card[MAX_CARDS];
    int   n, draw, discard_n;
    short discard[MAX_CARDS];
} Deck;

/* A Bond is now a field of Lord, above: there is no Bond between the
 * two Houses, because an opponent cannot be bound, only beaten, bought
 * or outlasted. */

/* --------------------------------------------------------------- phases */
typedef enum {
    PH_ACCESSION, PH_TAX, PH_TURNS, PH_RECKONING,
    PH_CONSEQUENCE, PH_WORLD, PH_OVER
} Phase;

/* ----------------------------------------------------------------- steps */
/* design.xml <turn_structure><sequence>.  The order a turn is taken in.
 * ST_COUNT doubles as "no step": outside a turn there is no sequence,
 * and court_actions must not filter anything then. */
typedef enum {
    ST_LEVY, ST_DRAW, ST_COURT1, ST_DECLARE, ST_COURT2, ST_COUNT
} Step;

/* --------------------------------------------------------------- actions */
typedef enum {
    A_DRAW, A_PLAY, A_BOND, A_INSTIGATE, A_PROPOSE, A_RESPOND,
    A_DECLARE_WAR, A_CHALLENGE, A_REVEAL_BOND, A_REVOLT,
    A_SPEND_GRIEVANCE, A_BUY_FAVOUR, A_DISCARD, A_PASS,
    A_KEEP, A_BREAK, A_VOTE, A_COMMIT, A_SIDE, A_OBJECT,
    A_COUNT
} ActionKind;

/* A client proposes; it never asserts.  Every field here is data, never a
 * pointer, so an Action is a message as easily as it is a call. */
typedef struct Action {
    unsigned char kind;
    signed char   seat;
    signed char   target;    /* the other House, or -1                   */
    short         card;      /* index into the card table, or -1         */
    signed char   a, b, c;   /* deck, resource, amount, term, vote, side */
} Action;

/* ------------------------------------------------------------- the seat */
struct View;
struct Action;

/* Five questions the engine asks a seat, and the only five.  A machine
 * seat answers from ai.c; a human seat answers from a UI; a remote seat
 * answers over a wire.  The engine does not know or care which.
 *
 * Every one of them takes a View and nothing else, so a seat cannot be
 * asked a question it is not entitled to answer.  That is the same rule
 * the AI already obeyed, made into the interface rather than a habit.
 *
 * `ctx` is the seat's own: a UI hangs its window on it, a socket its
 * connection.  The engine passes it back untouched.
 *
 * A callback left null falls back to the machine, so a game with one
 * human seat sets one field and changes nothing else.
 */
typedef struct Seat {
    void *ctx;
    /* Your turn: choose one of the legal actions, or the one that passes. */
    struct Action (*choose)(struct Seat *, const struct View *, const struct Action *, int n);
    /* Combat and the Throne: how much Military Levy, committed blind. */
    int (*commit)(struct Seat *, const struct View *, int max, int defending);
    /* Reckoning: keep your word, or do not. */
    int (*keep_promise)(struct Seat *, const struct View *, int promise_idx);
    /* Someone has offered you a promise. */
    int (*accept_promise)(struct Seat *, const struct View *, int promiser,
                          int term, int cons_res, int cons_amt);
    /* A lord you hold could leave.  Does it? */
    int (*lord_turns)(struct Seat *, const struct View *, int holder, int lord_idx);
} Seat;

/* Fill a Seat with the machine's answers.  This is what court_setup
 * does to both seats, so a game plays itself until something replaces
 * one of them. */
void court_seat_ai(Seat *s);

void court_seat_remote(Seat *s, void *remote_ctx);

/* ----------------------------------------------------------------- game */
typedef struct {
    int nhouses, round, seat_turn;
    unsigned char phase;
    unsigned char step;        /* Step; ST_COUNT outside a turn */
    House h[MAX_HOUSES];
    Lord  lord[MAX_LORDS];
    int   lord_n;
    Deck  deck[D_COUNT];

    Promise promise[MAX_PROMISES];
    int     promise_n;

    signed char paramount, winner;
    signed char first_paramount;   /* who led after Accession in round 1 */
    const char *win_reason;

    /* Per-round and per-turn scratch.  Cleared where the design says it
     * is cleared and nowhere else, because a flag that quietly survives a
     * phase boundary is the bug this structure exists to make visible. */
    unsigned char draws_left, bond_used, instigators_left, declare_used;
    unsigned char war[MAX_HOUSES][MAX_HOUSES];   /* Open War, this round */
    unsigned char peace_broken_ok;
    unsigned char no_crowning_until;
    unsigned char fallen_rule;       /* a fallen House may return  */
    unsigned char no_combat_until;   /* The King's Peace and its kin */
    unsigned char no_assassin_until;
    unsigned char no_spy_until[MAX_HOUSES];
    unsigned char no_promise_until[MAX_HOUSES];
    signed char   tie_break;         /* seat a tie goes to, or -1        */
    signed char   vote_nullified;    /* seat whose lords stay seated     */
    unsigned char vote_double[MAX_HOUSES];
    unsigned char banked_draws[MAX_HOUSES];
    unsigned char cancel_next[MAX_HOUSES];  /* their next play fizzles  */
    unsigned char no_spoil;                 /* this combat costs the
                                             * loser nothing extra      */
    unsigned char shield[MAX_HOUSES][R_COUNT];
    unsigned char tax_double;
    unsigned char skip_actions[MAX_HOUSES];
    unsigned char council_pending, council_mod;
    unsigned char favour_locked, court_silent;
    signed char   regent;          /* Aldemar's Regency takes the tax    */
    /* Two different things that both look like a vote count, kept apart
     * because conflating them is how a bought vote silently becomes a
     * commanded one: extra_votes is what you hold, commanded is who holds
     * you (-1-commander, or 0 for nobody). */
    /* The House seat 0 asked for, or -1 for whatever the seed deals.
     * Set before court_setup. */
    signed char   want_house;
    signed char   extra_votes[MAX_HOUSES];
    signed char   commanded[MAX_HOUSES];
    unsigned char free_votes;      /* The Old Rites                      */
    signed char   combat_bonus[MAX_HOUSES];
    signed char   combat_bonus_def[MAX_HOUSES];
    signed char   throne_bonus[MAX_HOUSES];
    unsigned char amb_rival_unrest[MAX_HOUSES];
    signed char   last_lord;       /* the lord a Lever just reached      */
    signed char   directed[MAX_LORDS];  /* Everyone Has a Price           */
    signed char   challenger;      /* a Throne challenge, answered at
                                    * Consequence as the design says     */

    /* Face-down things that resolve at Consequence. */
    struct { signed char from, to; unsigned char kind; } pending[MAX_PENDING];
    int pending_n;

    /* The two-player Court.  Absent at three and four: the other Houses
     * are the Court. */
    /* The Court is always in play now.  It was a fix for the two-player
     * case and the two-player case is the game. */

    unsigned long rng;
    int   epic;
    int   inert_plays, total_plays;
    int   promises_made, promises_kept, promises_broken;
    /* Grievance is sampled every round, not read at the end: "it sat
     * pinned at its maximum in every single game" is a statement about
     * the whole game and cannot be seen from the last frame of one. */
    int   grievance_sum, grievance_samples, grievance_pinned;
    Seat  seat[MAX_HOUSES];
    int   favour_mode;             /* how the Court is impressed         */
    int   favour_crown;            /* Favour that crowns; design.xml says 5 */
    int   throne_gold, throne_cap; /* what The Purchased Throne costs      */
    int   golden_throne;           /* Gold Standing that wins outright     */
    int   council_quorum;          /* votes needed to elect a Paramount  */
    int   levy_unspent, levy_given, turns_taken;
} Game;

/* ------------------------------------------------------------- the view */
/* What one seat is entitled to see.  The AI takes this and never a Game.
 * Everything hidden is either absent or marked unknown; there is no field
 * here that a seat could read to learn something it should not.
 *
 * The invariant checker builds a View for every seat every turn and
 * asserts that no AI decision changed when the Game behind it changed in
 * a way that seat could not see.  A machine House that reads another's
 * hidden Bond is a bug, and this is how it is caught. */
typedef struct View {
    signed char me;
    unsigned char house[MAX_HOUSES];  /* which House each seat plays */
    int nhouses, round;
    unsigned char phase;
    unsigned char step;                   /* Step, or ST_COUNT   */

    signed char standing[MAX_HOUSES][R_COUNT];
    signed char levy[R_COUNT];            /* only your own               */
    signed char unrest[MAX_HOUSES][R_COUNT], grievance[MAX_HOUSES];
    unsigned char alive[MAX_HOUSES], throneworthy[MAX_HOUSES];
    int kept_n[MAX_HOUSES], broken_n[MAX_HOUSES];
    signed char paramount;
    signed char favour[MAX_HOUSES];
    int favour_crown;   /* what the Court crowns at, so a seat can aim */
    int golden_throne;  /* Gold Standing that wins, for the same reason */

    short hand[HAND_LIMIT * 2];
    int   hand_n;
    short ambition[AMBITIONS_DEALT];
    unsigned char amb_done[AMBITIONS_DEALT];

    /* Lords.  A seat sees every lord in play, who holds it and whether it
     * is revealed; it sees the Servitude it holds itself, and a
     * Revolution count only where it has legitimately spied.  The rest
     * reads -1. */
    struct {
        short card; unsigned char trait, revealed, alive;
        signed char holder, servitude, revolution;
    } lord[MAX_LORDS];
    int lord_n;

    /* Promises in play: everything about a Promise is public except what
     * the promiser intends to do about it. */
    Promise promise[MAX_PROMISES];
    int     promise_n;
} View;

/* -------------------------------------------------------------- the API */
extern const Card  CARDS[];
extern const int   CARD_COUNT;
extern const short DECK_FIRST[D_COUNT], DECK_SIZE[D_COUNT];
/* Indexed by HouseId and not by seat: there are two chairs and four
 * Houses, and expanding this game adds Houses rather than chairs. */
extern const char *HOUSE_NAME[H_COUNT];
extern const char *RES_NAME[R_COUNT];
extern const char *OP_NAME[OP_COUNT];

/* The authority loop that serves a seat on the wire.  The same loop is a
 * central server and a trusted host; only who runs it and what the
 * interface says about trust differ. */
struct Chan;
int  court_serve(Game *g, int seat, struct Chan *c);
/* Every seat with a channel is played by the person on the other end of
 * it; every seat without one is played by the machine.  cs is indexed by
 * seat and must hold g->nhouses entries. */
int  court_serve_n(Game *g, struct Chan **cs);

/* Password hashing.  court_pw_selftest() checks all of it against
 * published vectors and must pass before any login is accepted. */
void court_sha256(const void *p, size_t n, unsigned char out[32]);
void court_pbkdf2(const char *pw, const unsigned char *salt, size_t slen,
                  unsigned long rounds, unsigned char out[32]);
int  court_pw_selftest(void);

/* 1 the password is right (including a name used for the first time,
 * which is created), 0 it is wrong, -1 the name is unusable. */
int  court_user_check(const char *name, const char *pass);

struct Listener;
/* The room: many people at once, talking, and starting games with each
 * other.  Does not return. */
int  court_lobby(struct Listener *l, int nh, int epic, unsigned long seed);
int  court_play_client(const char *host, int port);

void court_seed(Game *g, unsigned long seed);
void court_setup(Game *g, int nhouses, int epic);
int  court_round(Game *g);                  /* 0 while the game runs     */

/* State and action in, state out.  Returns 0 and changes nothing if the
 * action is not legal; the caller is a client and clients may be wrong. */
int  court_apply(Game *g, const Action *a);
int  court_legal(const Game *g, const Action *a);
int  court_actions(const Game *g, int seat, Action *out, int max);

void court_view(const Game *g, int seat, View *v);

/* Trust, derived every time it is needed, from the piles and nothing else.
 * Zero is ignorance, not suspicion. */
int  court_trust(const Game *g, int of, int toward);
int  court_total_power(const Game *g, int seat);


/* The AI.  One entry point, taking a View, returning an Action. */
Action ai_choose(const View *v, const Action *opts, int n, unsigned long *rng);
int  ai_vote(const View *v);
int  ai_commit(const View *v, int max, int defending);
int  ai_keep_promise(const View *v, int promise_idx);
int  ai_accept_promise(const View *v, int promiser, int term,
                       int cons_res, int cons_amt);
int  ai_side(const View *v, int holder, int lord_idx);
int  ai_price_promise(const View *v, int term, int promisee);

/* The wire.  One format for all three networking options, because they
 * differ in where the authority runs, not in what is said. */
/* Buf and Chan live in net.h, which is the one file that knows about
 * operating systems.  Named by tag here so court.h need not include it
 * and the engine stays free of anything platform-shaped -- and not
 * typedefed again, because repeating a typedef is C11 and this is C99. */
struct Buf;
void wire_write_view(struct Buf *f, const View *v);
void wire_write_actions(struct Buf *f, const Action *a, int n);
int  wire_read_action(const char *line, Action *out);

void court_log(const char *fmt, ...);

/* Told about every action that succeeds, so a server can tell the other
 * seat what was just done to it.  The engine decides WHICH actions are
 * public -- see tell_action in serve.c -- and the client decides how to
 * word them, because the deck and House names already live there. */
typedef void (*CourtWatch)(const Game *g, const Action *a);
void court_watch(CourtWatch fn);
void court_open_trace(const char *path);
void court_set_check(int on);
int  court_checking(void);
void court_inv(const char *fmt, ...);

#endif /* COURT_H */
