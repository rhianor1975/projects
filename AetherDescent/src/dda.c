#include "dda.h"

void dda_floor_begin(Player *p) {
    guardian_floor_reset();   /* one save per floor -- see guardian_catch */
    p->dda_floor_start_hp = hero_driven(p)->hp;
    p->dda_heals_used     = 0;
    p->dda_retreated      = false;
}

void dda_note_heal(Player *p)    { if (p->dda_heals_used < 1000) p->dda_heals_used++; }
void dda_note_retreat(Player *p) { p->dda_retreated = true; }

/* How the floor being stood on is going, right now, without waiting for it to
 * be left.
 *
 * This is the blocker the ROADMAP names, and it is worth stating precisely
 * because the shape of the fix falls out of it. dda_floor_end() was the only
 * thing that moved pressure, and it runs when a floor is *left*. A run that
 * dies on floor 1 never leaves it, so DDA read a flat zero for it -- and per
 * EVALUATION 86 that is the *median* run in all three hard modes. Any
 * help-the-struggling-player system riding on pressure would therefore have
 * helped exactly the players who were already going to be fine.
 *
 * Recomputed on demand rather than accumulated, so it cannot double-count with
 * the verdict dda_floor_end() files at the end of the same floor.
 *
 * **It only ever reads negative.** A floor going badly says so immediately; a
 * floor going well says nothing until it is finished and dda_floor_end() can
 * see how it actually ended. That asymmetry is the module's stated principle --
 * quick to forgive, slow to punish -- applied to time as well as to size, and
 * it is what stops a strong opening on a floor you are about to die on from
 * reading as "this run is coasting".
 */
int dda_floor_so_far(const Player *p) {
    if (!p) return 0;
    if (p->floor <= 0) return 0;                    /* town is not a performance */
    const Hero *h = hero_driven_c(p);
    if (h->maxhp <= 0) return 0;

    int comfort = h->hp * 100 / h->maxhp;
    if (p->dda_retreated || comfort < 20) return DDA_ROUTED;
    if (comfort < 45 || p->dda_heals_used >= 3) return DDA_HURT;
    return 0;
}

/* What a consumer reads: the settled history of floors finished, plus how the
   one underfoot is going. */
int dda_pressure(const Player *p) {
    int v = p->dda_pressure + dda_floor_so_far(p);
    if (v > DDA_MAX) v = DDA_MAX;
    if (v < DDA_MIN) v = DDA_MIN;
    return v;
}

/* The settled half alone -- floors that have actually been finished. For
   reporting, and for anything that wants the history without the floor in
   progress mixed into it. */
int dda_settled(const Player *p) { return p ? p->dda_pressure : 0; }

void dda_floor_end(Player *p, const Map *m) {
    if (!m || m->floor_num <= 0) return;      /* town is not a performance */
    if (hero_driven(p)->maxhp <= 0) return;

    int comfort = hero_driven(p)->hp * 100 / hero_driven(p)->maxhp;
    int delta;

    if (p->dda_retreated || comfort < 20) {
        delta = DDA_ROUTED;
    } else if (comfort < 45 || p->dda_heals_used >= 3) {
        delta = DDA_HURT;
    } else if (comfort >= 85 && p->dda_heals_used == 0) {
        delta = DDA_CRUISED;
    } else {
        delta = DDA_STEADY;
    }

    p->dda_pressure += delta;
    if (p->dda_pressure > DDA_MAX) p->dda_pressure = DDA_MAX;
    if (p->dda_pressure < DDA_MIN) p->dda_pressure = DDA_MIN;
}

const char *dda_label(const Player *p) {
    /* Describes what dda_pressure() reports, which now includes the floor in
       progress. Reading the raw field here meant the label said "even" while
       the number said -14, and a label that disagrees with its own gauge is
       worse than no label. */
    int v = dda_pressure(p);
    if (v >= 25)  return "coasting";
    if (v >= 10)  return "comfortable";
    if (v > -10)  return "even";
    if (v > -25)  return "pressed";
    return "drowning";
}

/* ---- the guardian angel ------------------------------------------------
 *
 * See dda.h for why this reads hit points rather than dda_pressure.
 *
 * GUARDIAN_MAX_GRACE is the one number, and it is deliberately the only one:
 * a single knob can be swept and reported as a curve, where a bonus per
 * difficulty, per band and per stat is five numbers I chose and a finding I
 * authored (EVALUATION §73). Sweep it before trusting any value of it.
 */
/* The angel has two halves, and the first version only had the second.
 *
 * Measured, that version did nothing at all: A/B at 60 runs a difficulty,
 * angel off against full strength, every median identical and every win rate
 * within noise -- Hardcore came out digit for digit the same. The reason is
 * that it only fired below 45% health, and 45% is the exact line the player
 * drinks at. Its whole operating range was the range a competent player
 * spends no time in, and the deaths it was meant to prevent are bursts that
 * cross from comfortable to dead without pausing in between.
 *
 * So the load-bearing half is depth, not health:
 *
 *   THE ESCORT   Large, unconditional, and strongest on floor 1, fading to
 *                nothing by the middle of the descent. This is what makes the
 *                opening a cakewalk on purpose -- the first bands are meant
 *                to show the player the world and make them want to continue,
 *                not to filter them, and measured 52 of 100 Normal runs died
 *                in the first nine floors (EVALUATION §83).
 *   THE NET      Small, conditional on nearly dying, and active at every
 *                depth including the bottom. The original angel, kept: past
 *                the middle the escort is gone and this is all there is.
 *
 * The escort is the design statement -- the game carries you until you know
 * how to walk -- and the net is the safety rail that never goes away.
 */
#define GUARDIAN_ESCORT_GRACE 15  /* points of crit and evasion on floor 1 */
#define GUARDIAN_FADES_BY     50  /* gone by here: the middle of the descent */
#define GUARDIAN_NET_GRACE    20  /* points when nearly dead, at any depth */
#define GUARDIAN_MAX_GRACE    90  /* the ceiling after difficulty weighting */

/* How hard the master has to work, per mode.
 *
 * This does *not* make the angel a difficulty setting -- it is still opt-in
 * and still orthogonal, and Hardcore with it on is still Hardcore, with
 * permadeath, no recall and its 10x experience penalty intact. What scales is
 * how much help it takes to deliver the same thing in each mode: a player who
 * switches the master on is asking to see the game, and on Hardcore that
 * costs far more than on Normal.
 *
 * The sizes are set by what each mode does to the player rather than picked
 * to feel right. Hard raises monster density; Swarm raises it further and cuts
 * experience by 10x; Hardcore does both and removes recall, the game's own
 * escape hatch, so the master is the only way out of a fight that has gone
 * wrong. Measured: with an unweighted angel, Hardcore's deaths in floors 1-9
 * were 60 out of 60 runs -- identical to no angel at all (EVALUATION §87).
 *
 * Order must match the DIFFICULTY_* enum in common.h.
 */
static const int GUARDIAN_BY_DIFFICULTY[DIFFICULTY_COUNT] = {
    100,   /* Normal   -- the baseline the escort was tuned against */
    180,   /* Hard     -- density */
    220,   /* Swarm    -- density and a tenth of the experience */
    260,   /* Hardcore -- both, and no recall to run home with */
};

static int guardian_mode_weight(const Player *p) {
    int d = p->difficulty;
    if (d < 0 || d >= DIFFICULTY_COUNT) d = 0;
    return GUARDIAN_BY_DIFFICULTY[d];
}

/* Off. The angel is a cheat code, not a difficulty tier -- see dda.h. A
   difficulty is a claim about the run; a cheat is a thing the player asked
   for, and neither should be the other by default. It also means every
   measurement taken before the angel existed stays reproducible: the harness
   has to be *told* to switch it on, so no published number changes underneath
   itself. */
static int g_guardian_strength = 0;

void guardian_set_strength(int pct) {
    if (pct < 0)   pct = 0;
    if (pct > 100) pct = 100;
    g_guardian_strength = pct;
}

int guardian_strength(void) { return g_guardian_strength; }

/* Unconditional, and a function of depth alone. Floor 0 is town, where there
   is nothing to help with. */
static int guardian_escort(const Player *p) {
    if (p->floor <= 0 || p->floor >= GUARDIAN_FADES_BY) return 0;
    return GUARDIAN_ESCORT_GRACE * (GUARDIAN_FADES_BY - p->floor) / GUARDIAN_FADES_BY;
}

/* Conditional on being in trouble, and a function of health alone. The band
   is dda_floor_end's own "hurt" line, so "the game thinks you are struggling"
   keeps one definition in this file rather than three. */
static int guardian_net(const Player *p) {
    if (hero_driven_c(p)->maxhp <= 0) return 0;
    int comfort = hero_driven_c(p)->hp * 100 / hero_driven_c(p)->maxhp;
    if (comfort >= 45) return 0;
    return GUARDIAN_NET_GRACE * (45 - comfort) / 45;
}

int guardian_grace(const Player *p) {
    if (!p || g_guardian_strength <= 0) return 0;

    int grace = guardian_escort(p) + guardian_net(p);
    grace = grace * guardian_mode_weight(p) / 100;
    grace = grace * g_guardian_strength / 100;
    if (grace < 0) grace = 0;
    if (grace > GUARDIAN_MAX_GRACE) grace = GUARDIAN_MAX_GRACE;
    return grace;
}

/* ---- the screen ---------------------------------------------------------
 *
 * The thing a dungeon master actually does for new players, which is not
 * handing them a bonus. It is deciding, quietly and after the fact, that the
 * blow which would have ended the character leaves them on one hit point
 * instead -- and then describing it so that the table remembers the moment.
 *
 * This is the correction to the first two versions of the angel. Both were
 * flat bonuses, and a flat bonus large enough to stop the deaths also stops
 * the fights being fights: at 45 points of escort, measured, health between
 * floors went *up* from 81% to 87% and deaths in the first nine floors fell
 * from 32 to 2. Nobody was in danger. That is not what hooks a player -- the
 * near miss is, and a flat bonus removes exactly the near misses it is meant
 * to survive.
 *
 * So the escort shrank to a nudge and the saving is done here instead:
 *
 *   - once per floor, so a bad floor can still kill you and the drama has
 *     teeth. The second mistake on a floor is yours.
 *   - only above GUARDIAN_FADES_BY, so it is the opening that is protected,
 *     not the run. By the middle the player knows the game and the screen
 *     comes down.
 *   - never in town, and never from a fall to exactly zero that was already
 *     zero -- it converts a killing blow, it does not resurrect.
 *
 * The target signature to measure against is not "few deaths". It is *low
 * health between floors and few deaths at the same time*: constantly nearly
 * dying and constantly getting away with it, which is the shape of a session
 * somebody wants to keep playing.
 */
static bool g_caught_this_floor = false;

void guardian_floor_reset(void) { g_caught_this_floor = false; }

bool guardian_catch(Player *p) {
    if (!p || g_guardian_strength <= 0) return false;
    if (hero_driven(p)->hp > 0) return false;                 /* not dying; nothing to catch */
    if (p->floor <= 0) return false;             /* town does not kill anyone */
    if (p->floor >= GUARDIAN_FADES_BY) return false;
    if (g_caught_this_floor) return false;
    if (hero_driven(p)->maxhp <= 0) return false;

    g_caught_this_floor = true;
    hero_driven(p)->hp = 1;
    /* And *standing*. The callers set `alive = false` the moment the hit lands
       and then ask the saves whether anybody wants to undo it, so a save that
       restores the hit points and not the flag leaves a body with 1 HP that
       `hero_is_up()` says is not there.
     *
       What that looked like: the guardian catches you, and from that moment
       `actor_step()` refuses every move, `hero_tick()` stops advancing your
       cooldowns, and the district rules skip you. Auto-explore then spins
       against a body that cannot move and reports "getting nowhere here",
       which is exactly true and says nothing about why -- 35 stalls per 14,000
       fuzzer keys, every one of them a run the guardian had already saved.

       companions_attempt_rescue() has always set this; this one never did. */
    hero_driven(p)->alive = true;
    /* Said out loud, because the moment is the whole point. A save the player
       never notices buys nothing: what makes somebody keep playing is knowing
       exactly how close that was. It does not say *why* they lived -- the
       screen stays up. */
    log_msg("You should be dead. You are not. Something in the dark was not finished with you.");
    return true;
}

int guardian_softened_crit(int base_pct, const Player *p) {
    if (base_pct <= 0) return base_pct;
    int grace = guardian_grace(p);
    if (grace <= 0) return base_pct;

    /* At most half, at maximum grace. The unavoidable critical is the one
       thing in the game that ignores every defence a character has built, and
       that is deliberate -- it is what stops the endgame becoming untouchable.
       The angel makes it rarer for someone who is nearly dead; it does not get
       to switch it off, or a character sitting at 10% health would be immune
       to the only attack that was ever going to reach them. */
    /* Rounded up, not truncated. The ceiling on grace rose when the angel
       gained its per-mode weighting, and integer division promptly turned
       every ordinary grace into *no relief at all* -- the test caught it as
       "rarer when you are dying" failing on a character who was dying. A
       softening that silently does nothing is worse than one that is absent,
       because the header claims it. */
    int relief = (base_pct * grace + (2 * GUARDIAN_MAX_GRACE) - 1)
               / (2 * GUARDIAN_MAX_GRACE);

    /* Halved at most, whatever the mode weighting says. The unavoidable
       critical is what keeps the temple able to hurt an overbuilt character,
       and 'god mode' is meant to describe the dice, not the removal of the
       one attack that ignores them. */
    int cap = base_pct / 2;
    if (relief > cap) relief = cap;

    int out = base_pct - relief;
    if (out < 1) out = 1;
    return out;
}

/* ---- the avenging angel ------------------------------------------------ */

/* Ceiling on what boredom can add. Escalation is already the biggest single
   term in the monster attack curve (EVALUATION §58 settled the game on its
   cap), so this is deliberately a fraction of it: the master leans on a
   coasting table, they do not rewrite the encounter. */
#define AVENGER_MAX_ESCALATION 25

/* The two faces cross over at the same floor, in opposite directions: the
   escort is strongest at the top and gone by GUARDIAN_FADES_BY, and the
   avenger is quietest at the top and at full weight from there down. The
   descent therefore has a shape -- carried, then let go, then leaned on --
   rather than one constant assist.

   It is a floor rather than a fade to zero, deliberately. A player who is
   genuinely steamrolling floor 5 has earned something in the corridor even
   though the master is still mostly on their side, and a second playthrough
   should not be able to coast the opening untouched just because it is the
   opening. */
#define AVENGER_EARLY_WEIGHT 25   /* percent of full weight on floor 1 */

static int avenger_depth_weight(const Player *p) {
    if (p->floor >= GUARDIAN_FADES_BY) return 100;
    if (p->floor <= 0) return 0;                       /* town */
    return AVENGER_EARLY_WEIGHT
         + (100 - AVENGER_EARLY_WEIGHT) * p->floor / GUARDIAN_FADES_BY;
}

int avenger_escalation(const Player *p) {
    if (!p || g_guardian_strength <= 0) return 0;

    /* dda_label's own bands: below "comfortable" nothing happens at all, so a
       run that is merely even is left alone and a run that is losing can
       never be pushed further down. Full weight at the top of the scale,
       which dda_label calls "coasting". */
    int pressure = dda_pressure(p);
    if (pressure <= 10) return 0;

    int span = DDA_MAX - 10;
    if (span <= 0) return 0;
    int extra = AVENGER_MAX_ESCALATION * (pressure - 10) / span;
    extra = extra * avenger_depth_weight(p) / 100;
    extra = extra * g_guardian_strength / 100;
    if (extra < 0) extra = 0;
    if (extra > AVENGER_MAX_ESCALATION) extra = AVENGER_MAX_ESCALATION;
    return extra;
}

/* How long the things standing over you are too surprised to act. Long enough
   to drink, to run, or to swing back -- a turn is not a rescue, it is a
   chance, which is the difference between a dungeon master and a cheat that
   plays for you. */
#define GUARDIAN_RECOIL_TURNS 3

void guardian_follow_through(Player *p, Map *m) {
    if (!p || !m || g_guardian_strength <= 0) return;

    int stunned = 0;
    for (int i = 0; i < m->monster_count; i++) {
        Monster *mo = &m->monsters[i];
        if (!mo->alive) continue;
        int dx = mo->x - hero_driven(p)->x, dy = mo->y - hero_driven(p)->y;
        if (dx < -1 || dx > 1 || dy < -1 || dy > 1) continue;   /* adjacent only */
        if (mo->stun_turns_left < GUARDIAN_RECOIL_TURNS)
            mo->stun_turns_left = GUARDIAN_RECOIL_TURNS;
        stunned++;
    }

    if (stunned > 0)
        log_msg("Whatever was standing over you recoils -- you have a moment. Use it.");
}
