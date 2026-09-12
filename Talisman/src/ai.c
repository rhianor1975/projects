#include "talisman.h"
#include <stdlib.h>
#include <string.h>

/*
 * The whole point of a fixed board is that the AI can plan on it without
 * search: every space index is known, so "how far am I from the Sentinel"
 * is arithmetic.  Each AI has one destination at a time and picks the
 * direction that shortens the walk to it.
 */

static int dist_dir(int from, int to, int len, int dir)
{
    int d = (dir > 0) ? (to - from) : (from - to);
    return ((d % len) + len) % len;
}

/* Defined further down, but the movement brain above needs them. */
static int lead_score(Player *q);
int        lead_score_of(Player *q);
static int space_appeal(Player *p, int idx);
static int resident_appeal(Player *p, int sid);
int        ai_attack(Player *att, Player *def);
int        ai_flee_demon(Player *p, int demon_lives);

static int best_stat(Player *p) { return (eff_craft(p) > eff_str(p)) ? eff_craft(p) : eff_str(p); }

/* ------------------------------------------------------ win probability --
 * Every fight in Talisman is `mine + d6` against `theirs + d6`, so the
 * chance of winning one is not a matter of judgement -- it is a sum over
 * the 36 ways two dice fall.  The difference of two dice is triangular:
 * there are 6 - |k| ways to roll a difference of k, for k in -5..5.  You
 * win when d1 - d2 > theirs - mine.
 *
 * Every decision below is phrased in these terms, because the rule is
 * "only fight what you have a chance against, and do not refuse what you
 * are likely to win" -- and both halves of that need a number.
 */
static int win_chance(int mine, int theirs)
{
    int start = 1 - (mine - theirs), k, ways = 0;

    if (start < -5) start = -5;
    for (k = start; k <= 5; k++) ways += 6 - (k < 0 ? -k : k);
    return ways * 100 / 36;          /* a tie is a stand-off, not a win */
}

/* The three lines the whole personality hangs off.  Not a coward, not a
 * fool: take anything better than even, refuse anything worse than a
 * third, and treat the middle as costing a life to find out. */
#define WORTH_FIGHTING 50
#define DESPERATE      33

/* Determines the stat a character will likely use in combat */
/* Will this attacker turn the fight into a battle of minds?  Same test the
 * engine makes in pvp_encounter(), so the AI is judging the fight it will
 * actually get. */
static int goes_psychic(Player *att)
{
    return has_ab(att, AB_PSYCHIC_ATTACK) && eff_craft(att) > eff_str(att);
}

static int combat_stat(Player *p) {
    /* Psychic Combat is fought with Craft, so a Character who can choose it
     * fights with whichever stat is higher. */
    if (goes_psychic(p)) return eff_craft(p);
    return eff_str(p);
}

/* What the defender actually brings to THIS fight.  It is not their best
 * stat -- it is the one the attacker's choice forces them to use.  Reading
 * it as their best made every psychic attacker rate a strong-but-dim
 * opponent as unbeatable, when Craft 10 against Craft 3 is a rout. */
static int defence_stat(Player *att, Player *def)
{
    int psy = goes_psychic(att);
    int v   = psy ? eff_craft(def) : eff_str(def);

    /* Their Henchman is a card lying face up on the table, and they will put
     * him forward if he is the better fighter.  Judging the duel by their
     * own Strength alone meant walking into a Priest-versus-Space-Pirate
     * fight that looked winnable right up until the Henchman stood up. */
    if (henchmen_on && def->hench.ct >= 0 && def->hench.lives > 0) {
        const CharTemplate *h = &char_tbl[def->hench.ct];
        int hs = psy ? h->craft : h->str;
        if (hs > v) v = hs;
    }
    return v;
}

/* A living character on this ring carrying a Talisman we could take -- and
 * near enough that one roll could put us on them.
 *
 * Without the distance test this walked at the square somebody was standing
 * on.  They move; next turn it aimed at the new square.  Over a hundred
 * games that was 1,247 turns of chasing, a mean pursuit of 1.0 turns, 131
 * arrivals and 44 Talismans -- against an eleven-turn walk to the Temple
 * that arrives three times in four and was being pre-empted to do it.  Six
 * spaces is the whole of a die, so within six there is a real chance of
 * landing on them this turn and beyond it there is only trailing after. */
static int carrier_on_ring(Player *p)
{
    int i, len = ring_len(p->region);

    for (i = 0; i < nplayers; i++) {
        Player *q = &players[i];
        int cw, ccw;
        if (q == p || !q->alive || !q->talisman) continue;
        if (q->region != p->region) continue;
        if (combat_stat(q) > combat_stat(p) + 2) continue;   /* too strong to rob */
        cw  = dist_dir(p->idx, q->idx, len,  1);
        ccw = dist_dir(p->idx, q->idx, len, -1);
        if ((cw < ccw ? cw : ccw) > 6) continue;             /* out of one roll */
        return q->idx;
    }
    return -1;
}

/* Is somebody's dropped Talisman lying on this ring? */
/* Somewhere in this region worth standing on while we build ourselves up.
 * "return -1" used to mean "grind adventures", but in the Inner Region the
 * direction code then walked to the Dread Gate anyway -- so a character who
 * had decided not to cross stepped onto the Gate, refused, stepped off, and
 * did it again for sixty turns. */
static int best_grind(Player *p)
{
    int len = ring_len(p->region), i, best = -1, bv = -1000;

    for (i = 0; i < len; i++) {
        int v;
        if (space_at(p->region, i)->kind == SP_GATE) continue;
        v = space_appeal(p, i);
        if (v > bv) { bv = v; best = i; }
    }
    return best;
}

/* What a Character wants before pushing inward, region by region.
 *
 * The regions are not equally forgiving and the old checks did not say so:
 * one of them read `best_stat >= 6 || lives >= 4`, which sent a Strength 2
 * Character at the Portal of Power on the strength of having four Lives.
 *
 * The Inner Region is the one that matters.  Two of its eight spaces are the
 * Valley of Fire, which takes a Life from anyone who ends a move there; you
 * crawl one space a turn; and the Portal only opens inward.  Sixteen per
 * cent of all deaths were characters burning in it. */
static const struct { int stat, lives; } region_gate[] = {
    { 0, 0 },   /* OUTER  -- where everyone begins                       */
    { 5, 3 },   /* MIDDLE -- past a Sentinel that fights at Strength 6   */
    { 7, 4 },   /* INNER  -- one way in, and it burns                    */
    { 0, 0 },   /* CROWN                                                 */
};

/* Ready to go one region further in?  The reasons to go anyway -- a loose
 * Talisman, a carrier worth robbing -- are all tested before this is asked. */
static int ready_for(Player *p, int region)
{
    if (region < 0 || region >= (int)(sizeof region_gate / sizeof region_gate[0]))
        return 1;
    return best_stat(p) >= region_gate[region].stat &&
           p->lives    >= region_gate[region].lives;
}

/* Anyone on this ring we would actually take a swing at. */
static int rival_on_ring(Player *p)
{
    int i, best = -1, best_lead = -1;
    for (i = 0; i < nplayers; i++) {
        Player *q = &players[i];
        if (q == p || !q->alive || q->region != p->region) continue;
        if (!ai_attack(p, q)) continue;
        if (lead_score(q) > best_lead) { best_lead = lead_score(q); best = q->idx; }
    }
    return best;
}

static int talisman_on_ring(Player *p)
{
    int len = ring_len(p->region), i;

    for (i = 0; i < len; i++)
        if (res_tal[space_id(p->region, i)]) return i;
    return -1;
}

/* How much this AI wants to land on a space, given what is sitting on it. */
static int space_appeal(Player *p, int idx)
{
    int sid = space_id(p->region, idx);
    int v = space_at(p->region, idx)->draw;

    /* A wound is an inconvenience at four Lives and the end of the game at
     * one.  These penalties used to be flat, so a character on their last
     * Life walked into the Desert at exactly the same odds as a fresh one. */
    int frail = (p->lives <= 1) ? 6 : (p->lives <= 2) ? 2 : 1;

    /* The Firelands lie on top of the board and this function used to look
     * straight through them.  A Fire Token costs a Life to end a turn on;
     * a Terrain Card goes further and REPLACES the space, so the Temple
     * under one is not a Temple any more.  A fifth of every purposeful walk
     * in the game is a walk to the Temple, and in a hundred games it was
     * walked to after it had burned down four hundred and sixty-six times. */
    if (fire_here(sid)) v -= 4 * frail;
    if (terrain_here(sid)) return v + 1 + resident_appeal(p, sid);

    switch (space_at(p->region, idx)->kind) {
    case SP_FIRE:                       v -= 4 * frail; break;
    case SP_DESERT:                     v -= 3 * frail; break;
    case SP_PIT:                        v -= 3 * frail; break;
    /* The Mines pay well and cave in; on a last Life that is a bad trade. */
    case SP_MINE:   v += (p->lives <= 1) ? -8 : 1; break;
    case SP_GRAVE: case SP_CRYPT:       v -= 1; break;
    case SP_CHAPEL: v += (p->lives < eff_maxlives(p)) ? 3 : 0; break;
    case SP_TEMPLE: v += p->talisman ? 0 : 4;  break;
    case SP_CITY:   v += (p->gold >= 3) ? 3 : 0; break;
    case SP_VILLAGE:                    v += 1; break;
    
    /* No sense walking to the Portal without a Talisman: it will not open. */
    case SP_PORTAL: v += (!p->talisman) ? -50 : 10; break;

    /* --- the spaces the expansions added, none of which were scored -----
     * Every one of these was read as open ground until now.  The Eyrie is
     * the expensive one: the Eagle King fights at Strength and Craft 8, he
     * cannot be evaded, and no Follower may take the fight in your place.
     * Five hundred and sixty-seven landings, two hundred and twenty-four
     * wins and fifty-eight deaths -- a tenth of every death in the game. */
    /* Every one of these is a fight that can be had again next turn, so
     * none of them may score positive.  best_grind() picks the highest
     * space on the ring and walks back to it for as long as it stays the
     * highest -- give a repeatable fight a reward and two characters will
     * farm it until the turn cap.  Seed 169 did exactly that, 1,873 rounds
     * of Pitfiends between two Strength-12 characters who could not lose.
     * The convention the rest of this switch already keeps: hazards are
     * priced at zero or below, and only services are worth walking to. */
    case SP_EYRIE: {
        int win = win_chance(best_stat(p), 8);
        v += (win >= 65) ? 0 : (win >= WORTH_FIGHTING) ? -2 : -6 * frail;
        break;
    }
    /* The Warlock trades a finished Quest for a Talisman, and a Talisman is
     * the whole of what stands between the Outer Region and the Crown.  He
     * waves you away if it is not done, so that trip is worth nothing. */
    case SP_WARLOCK:
        v += p->talisman        ? 0
           : p->quest < 0       ? 5      /* go and be given one   */
           : quest_met(p)       ? 12     /* go and be paid for it */
           :                     -3;     /* he will only wave her off */
        break;
    /* Destiny is a fight at 7, either stat depending on the Path. */
    case SP_DESTINY: {
        int win = win_chance(best_stat(p), 7);
        v += (win >= 65) ? 0 : (win >= WORTH_FIGHTING) ? -1 : -4 * frail;
        break;
    }
    case SP_CROSSROADS: v -= 1; break;      /* it only shuffles your Path */
    /* Both end the turn and carry you off to the City or the Dungeon. */
    case SP_BRIDGE:     v -= 2; break;
    case SP_TUNNEL:     v -= 3; break;
    /* Up to six fights at Strength 3, ending when one of them lands. */
    case SP_PITFIENDS:  v += (win_chance(eff_str(p), 3) >= 70) ? 0 : -3 * frail; break;
    /* A Werewolf at a die roll of Strength: 3.5 on average. */
    case SP_WEREDEN:
        v += (win_chance(eff_str(p), 4) >= 60) ? 0 : -2 * frail; break;

    default: break;
    }
    return v + resident_appeal(p, sid);
}

/* What is lying on a space, scored on its own -- burnt ground has no
 * instructions left of its own but still holds whatever was dropped there. */
static int resident_appeal(Player *p, int sid)
{
    int v = 0, k;

    if (res_tal[sid])  v += 8;
    if (res_gold[sid]) v += 2;
    for (k = 0; k < nplayers; k++) {          /* a robbable carrier standing there */
        Player *q = &players[k];
        if (q != p && q->alive && q->talisman && !p->talisman &&
            space_id(q->region, q->idx) == sid &&
            combat_stat(q) <= combat_stat(p) + 2)
            v += 6;
    }
    for (k = 0; k < res_n[sid]; k++) {
        const Card *c = &deck_proto[res_card[sid][k]];
        if (c->type == C_ENEMY || c->type == C_SPIRIT) {
            int mine = c->craftfight ? eff_craft(p) : eff_str(p);
            int win  = win_chance(mine, c->power);
            /* a trophy worth having, a gamble, or a place to die */
            v += (win >= 65) ? 3 : (win >= WORTH_FIGHTING) ? 1
               : (win >= DESPERATE) ? -2 : -8;
        } else if (c->type == C_PLACE || c->type == C_STRANGER) {
            switch (c->place) {
            case PLACE_SPIRIT: {                  /* this one is a fight */
                int win = win_chance(eff_craft(p), c->power);
                v += (win >= WORTH_FIGHTING) ? 1 : (win >= DESPERATE) ? -2 : -8;
                break;
            }
            case PLACE_MARSH: case PLACE_MAZE: v -= 3; break;
            case PLACE_DEN:                    v -= 1; break;
            case PLACE_POOL:
                v += (p->lives < eff_maxlives(p)) ? 3 : 0;
                break;
            case PLACE_SPELL:
                v += (spell_limit(p) > p->nspells) ? 3 : 0;
                break;
            case PLACE_SHOP:
                v += (p->gold >= 2) ? 2 : 0;
                break;
            default: v += 2; break;               /* boons */
            }
        }
    }
    return v;
}

/* Where does this character want to be right now? -1 = nowhere special. */
/* Defined with the other endgame logic below, but needed here: with the
 * alternative endings in play, where you walk depends on which card it is. */
static Player *belt_wearer_p(void);
static int     should_rush_crown(Player *p);
static int     ai_hedge_crossing(Player *p);
static Player *crown_sitter(void);
int            ai_flee_demon(Player *p, int demon_lives);
int            ai_attack(Player *att, Player *def);

/* Which rule in ai_target() picked the destination.  A goal that changes
 * every single turn and a goal held for ten look the same in the trace
 * unless it says which question produced it. */
static const char *why_rule = "-";

static int ai_target(Player *p)
{
    why_rule = "-";
    int loose, i;
    int crown_holder_exists = 0;

    /* Has anyone reached the Crown of Command yet? */
    for (i = 0; i < nplayers; i++) {
        if (players[i].alive && players[i].region == REG_CROWN) {
            crown_holder_exists = 1;
            break;
        }
    }

    /* The Belt of Hercules is the one ending where the Crown is not the
     * prize: the wearer must leave it to duel, and killing them sends the
     * Belt back.  So hunt the wearer instead -- and a table of humans does
     * this together, which is precisely what makes the Belt survivable. */
    if (ending_kind() == END_BELT) {
        Player *w = belt_wearer_p();
        /* ...but only if we can actually take them.  Walking up to a
         * superhuman you cannot beat is just handing over a Life. */
        if (w && w != p && w->region == p->region && ai_attack(p, w))
            { why_rule = "belt-wearer"; return w->idx; }
    }

    /* EMERGENCY OVERRIDE: Someone holds the Crown! Rush the end game --
     * but only for the endings where rushing is actually the answer. */
    if (crown_holder_exists && p->region != REG_CROWN && should_rush_crown(p)) {
        why_rule = "rush-crown";
        if (p->region == REG_OUTER)  return SENTINEL_IDX;
        if (p->region == REG_MIDDLE) return PORTAL_IDX;
        if (p->region == REG_INNER)  return GATE_IDX;
    }

    /* The Demon Lord is fought in Psychic Combat, so Craft is the only
     * stat that matters.  Walking in on Strength 20 and Craft 3 is suicide;
     * a human would go and find a Mystic first. */
    if (ending_kind() == END_DEMON && ai_flee_demon(p, 4)) {
        int foe;
        if (p->region == REG_MIDDLE) { why_rule = "demon-craft"; return TEMPLE_IDX; }
        /* In the Inner Region there is no going back for Craft and no way
         * past the barrier, so the only thing left that can decide the game
         * is each other.  Orbiting the Gate forever is not patience, it is
         * a stalemate three players walked into together. */
        if (p->region == REG_INNER && (foe = rival_on_ring(p)) >= 0)
            { why_rule = "demon-rival"; return foe; }
    }

    /* Nobody has turned the card yet, and one in six is the Void. */
    if (p->region == REG_INNER && ai_hedge_crossing(p)) { why_rule = "hedging"; return -1; }

    /* "The cursed Character must move by the fastest possible route to the
     * chapel.  Otherwise, all normal rules apply."  Nothing else changes --
     * they still encounter, still cast, still fight; they just have an
     * errand first. */
    if (p->cursed) {
        int i, len = ring_len(p->region);
        const char *goal = (p->align == AL_EVIL) ? "Ruins" : "Chapel";
        for (i = 0; i < len; i++)
            if (!strcmp(space_at(p->region, i)->name, goal))
                { why_rule = "cursed"; return i; }
    }

    switch (p->region) {
    case REG_OUTER:
        if (!p->talisman && (loose = talisman_on_ring(p)) >= 0)
            { why_rule = "loose-talisman"; return loose; }
        if (!p->talisman && (loose = carrier_on_ring(p))  >= 0)
            { why_rule = "chase-carrier"; return loose; }
        /* the Sentinel yields to either stat now, so use the better one */
        if (ready_for(p, REG_MIDDLE)) { why_rule = "ready"; return SENTINEL_IDX; }
        return -1;                                  /* grind adventures */
    case REG_MIDDLE:
        if (!p->talisman && (loose = talisman_on_ring(p)) >= 0)
            { why_rule = "loose-talisman"; return loose; }
        if (!p->talisman && (loose = carrier_on_ring(p))  >= 0)
            { why_rule = "chase-carrier"; return loose; }
        if (!p->talisman) { why_rule = "need-talisman"; return TEMPLE_IDX; }
        /* Under the Hunt the Inner Region is not on the way to anything: the
         * four are gathered out here, and the Portal only opens inward. */
        if (ending_kind() == END_HUNT && p->nether_kills < 4)
            { why_rule = "hunt-stay-out"; return -1; }
        if (ready_for(p, REG_INNER)) { why_rule = "ready"; return PORTAL_IDX; }
        return -1;
    case REG_INNER:
        /* If we already looked at the Gate this game and turned away, there
         * is no sense walking back to it every turn -- draw cards and get
         * stronger until the answer changes. */
        /* We turned the Gate down at a certain Craft; go and get stronger,
         * and only walk back to it once we actually are. */
        if (p->gate_shut && eff_craft(p) + 1 <= p->gate_shut) {
            int g = best_grind(p);
            if (g >= 0) { why_rule = "gate-shut"; return g; }
        }
        why_rule = "the-gate";
        return GATE_IDX;
    default:
        return -1;
    }
}

/* life the walk from `from` to `to` in direction `dir` will cost */
static int path_pain(Player *p, int to, int dir)
{
    int len = ring_len(p->region), i, pain = 0, at = p->idx;
    int n = ((dir > 0 ? to - at : at - to) % len + len) % len;
    /* A wound is a nuisance at four Lives and the end of everything at one.
     * space_appeal() has weighed hazards by this for a while, but the Inner
     * Region is steered by *this* function instead -- and the Inner Region
     * is where the Valley of Fire is.  A flat cost of two sent characters
     * onto a space that takes a Life, with one Life left, again and again:
     * a sixth of every death in the game. */
    int frail = (p->lives <= 1) ? 20 : (p->lives <= 2) ? 3 : 1;

    for (i = 1; i <= n; i++) {
        at = ((p->idx + dir * i) % len + len) % len;
        /* A Fire Token only burns you if you END a turn on it, so it costs
         * nothing to walk over -- except on the last step, which is where
         * this walk stops. */
        if (i == n && fire_here(space_id(p->region, at))) pain += 2 * frail;
        switch (space_at(p->region, at)->kind) {
        case SP_FIRE:   pain += 2 * frail; break;   /* certain wound */
        case SP_DESERT: pain += 1 * frail; break;   /* wound unless you shelter */
        case SP_PIT:    pain += 1 * frail; break;
        default: break;
        }
    }
    return pain;
}

/* Worth of the space this roll would actually put us on.  The roll is
 * known before the direction is chosen, so this is an exact lookup --
 * no search, just the map. */
static int landing_score(Player *p, int at, int target, int len)
{
    int v = space_appeal(p, at) * 2;
    int road;

    if (target >= 0) {
        if (at == target) v += 40;                /* land right on it */
        else {
            int d = dist_dir(at, target, len,  1);
            int e = dist_dir(at, target, len, -1);
            v += 12 - (d < e ? d : e);            /* else: get closer */
        }
    }

    /* The board has proper distance tables -- how many turns from this space
     * to the Crown, computed with and without a Talisman -- and until now the
     * only two questions that ever consulted them were about the Dungeon.
     * Weighted lightly, so it breaks ties between otherwise equal spaces
     * rather than dragging everyone at the Crown before they are ready. */
    road = p->road ? turns_to_crown(p->region, at, p->talisman) : -1;
    if (road >= 0) v -= road * 2;
    return v;
}

/* ------------------------------------------------------- looking ahead --
 * Not a chess search -- there is no opponent to model here and the deck is
 * unknowable -- but the die is a known, uniform distribution, which is more
 * than a chess engine can say about what its opponent will choose.  So:
 * where does landing here leave me?  For each roll I might get next turn,
 * take the better of the two directions it allows, and average over the six.
 *
 * Two directions now x six rolls x two directions then = 24 leaf positions.
 * The value of the space I am standing on still dominates; the expectation
 * of what it opens up breaks the ties, which is roughly how a person plays
 * it: "clockwise puts me on the Village, and the Village reaches the Portal." */
static int lookahead(Player *p, int at, int target, int len, int depth)
{
    int v = landing_score(p, at, target, len);
    int r, sum = 0;

    if (depth <= 0 || len <= 1) return v;

    for (r = 1; r <= 6; r++) {
        int cw  = ((at + r) % len + len) % len;
        int ccw = ((at - r) % len + len) % len;
        /* Only what the space is worth in itself travels back up the tree,
         * never the "I am standing on my goal" bonus.  Letting that leak
         * backwards made hovering one roll away from the Temple score
         * almost as well as arriving, so the AI circled instead of landing. */
        int a   = space_appeal(p, cw);
        int b   = space_appeal(p, ccw);
        sum += (a > b) ? a : b;
    }
    return v + sum / 6 / 3;
}

/* The name of a space, for the decision trace. */
static const char *why_name(Player *p, int idx)
{
    return (idx < 0) ? "grind" : space_at(p->region, idx)->name;
}

int ai_direction(Player *p, int steps)
{
    int len = ring_len(p->region);
    int target = ai_target(p);
    int cw, ccw, cwi, ccwi;

    /* In the Inner Region you crawl one space a turn, so the cheaper
     * route matters more than the shorter one -- and here they differ. */
    if (p->region == REG_INNER) {
        /* This used to walk to the Dread Gate and nothing else, which
         * quietly discarded ai_target() -- so a character who had decided
         * to go and settle things with a rival, or to keep away from a
         * door they could not open, still shuffled towards the Gate.  Two
         * of them could orbit for four thousand turns without ever meeting. */
        int dest     = (target >= 0) ? target : GATE_IDX;
        int pain_cw  = path_pain(p, dest,  1);
        int pain_ccw = path_pain(p, dest, -1);
        /* You move exactly one space here, so the only hazard you are really
         * choosing is the very next one.  Weighing the whole route let a
         * character walk into the fire this turn to reach a cheaper road it
         * would never live to use. */
        int nxt_cw   = ((p->idx + 1) % len + len) % len;
        int nxt_ccw  = ((p->idx - 1) % len + len) % len;
        if (p->lives <= 1) {
            int burn_cw  = space_at(p->region, nxt_cw)->kind  == SP_FIRE;
            int burn_ccw = space_at(p->region, nxt_ccw)->kind == SP_FIRE;
            /* The Valley of Fire sits at two spaces of the eight, and the
             * two spaces between them have fire in both directions.  A
             * Character on their last Life who simply refuses will pace
             * between that pair for the rest of the game, so step around it
             * while there is somewhere to step -- but not forever.  Nothing
             * in the Inner Region heals, and waiting loses just as surely. */
            if (burn_cw != burn_ccw && p->fire_shy < 20) {
                p->fire_shy++;
                wlog("  why %s  goal %s [%s]  steps around the fire  -> %s",
                     p->name, why_name(p, dest), why_rule, burn_cw ? "ccw" : "cw");
                return burn_cw ? -1 : 1;
            }
        }
        cw  = dist_dir(p->idx, dest, len,  1);
        ccw = dist_dir(p->idx, dest, len, -1);
        {   /* here the cost is steps plus what the road takes out of you,
             * so the trace shows both halves rather than one total. */
            int tot_cw  = cw  + pain_cw;
            int tot_ccw = ccw + pain_ccw;
            int take    = (tot_cw != tot_ccw) ? (tot_cw < tot_ccw ? 1 : -1)
                                              : (cw <= ccw ? 1 : -1);
            wlog("  why %s  goal %s [%s]  cw %s %d+%d  ccw %s %d+%d  -> %s by %d",
                 p->name, why_name(p, dest), why_rule,
                 why_name(p, nxt_cw),  cw,  pain_cw,
                 why_name(p, nxt_ccw), ccw, pain_ccw,
                 take > 0 ? "cw" : "ccw",
                 tot_cw > tot_ccw ? tot_cw - tot_ccw : tot_ccw - tot_cw);
            return take;
        }
    }

    cwi  = ((p->idx + steps) % len + len) % len;
    ccwi = ((p->idx - steps) % len + len) % len;
    cw   = lookahead(p, cwi,  target, len, p->look);
    ccw  = lookahead(p, ccwi, target, len, p->look);
    /* One decision in six scores an exact tie, and `cw >= ccw` sent every
     * one of the 3,243 of them clockwise -- which is the whole of the
     * board's 56.7/43.3 drift.  Four characters all circling the same way
     * is not indifference, it is a bias nobody chose.  When the scoring
     * genuinely cannot separate the two, toss for it. */
    {
        int take = (cw != ccw) ? (cw > ccw ? 1 : -1)
                               : ((rand() & 1) ? 1 : -1);
        wlog("  why %s  goal %s [%s]  cw %s %d  ccw %s %d  -> %s by %d",
             p->name, why_name(p, target), why_rule,
             why_name(p, cwi),  cw,
             why_name(p, ccwi), ccw,
             take > 0 ? "cw" : "ccw", cw > ccw ? cw - ccw : ccw - cw);
        return take;
    }
}

/* 0 = not yet, 1 = fight it with Strength, 2 = outwit it with Craft */
int ai_fight_sentinel(Player *p)
{
    int use  = (eff_craft(p) > eff_str(p)) ? 2 : 1;
    int win  = win_chance(best_stat(p), 6);

    /* The Sentinel is only a gate -- losing costs a life and you may try
     * again -- so spare lives buy worse odds, but never hopeless ones. */
    if (win >= WORTH_FIGHTING)                  return use;
    if (win >= DESPERATE && p->lives >= 3)      return use;
    if (win >= 25       && p->lives >= 5)       return use;
    return 0;
}

/* Is there anyone left to aim a spell at? */
static int any_rival(Player *p)
{
    int i;
    for (i = 0; i < nplayers; i++)
        if (players[i].alive && &players[i] != p) return 1;
    return 0;
}

/* How far along somebody is.  Region counts for most -- a character in the
 * Inner Region is nearly there -- then the Talisman that got them in. */
static int lead_score(Player *q)
{
    return q->region * 10 + (q->talisman ? 8 : 0) + best_stat(q);
}

int lead_score_of(Player *q) { return lead_score(q); }

/* Nobody is doing better than this one.  Ganging up on whoever is winning
 * is what a table of humans does, and it is the only thing that stops a
 * runaway leader. */
static int is_leader(Player *q)
{
    int i;
    for (i = 0; i < nplayers; i++) {
        Player *r = &players[i];
        if (!r->alive || r == q) continue;
        if (lead_score(r) > lead_score(q)) return 0;
    }
    return 1;
}

/* ============================================================ endgames ==
 * With the alternative endings in play there is no longer one endgame but
 * six, and each one asks a different question.  The card is face down until
 * somebody crosses the Valley of Fire, so -- exactly like a human -- the AI
 * plays the standard race until the reveal and only then commits.
 *
 *   Crown of Command  the holder kills everyone from the throne.  Rush it.
 *   Demon Lord        Craft 12 in Psychic Combat, behind a mystic barrier
 *                     nobody else can cross.  Rushing is pointless; build
 *                     Craft and wait your turn in the queue.
 *   Pandora's Box     spells rain down at any range.  Rush it hardest.
 *   Belt of Hercules  the wearer must come to you.  Do not rush -- kill the
 *                     wearer instead, and the Belt flies back to the Crown.
 *   Horrible Black Void  the first one across dies.  Never be first.
 *   Dragon King       a lottery: 1/6 an outright win, 1/6 eaten.  Race, but
 *                     do not walk in on your last life.
 */

/* Whoever is wearing the Belt of Hercules is everyone else's problem. */
static Player *belt_wearer_p(void)
{
    int b = belt_holder();
    return (b >= 0 && players[b].alive) ? &players[b] : NULL;
}

/* Does someone already hold the Crown space? */
static Player *crown_sitter(void)
{
    int i;
    for (i = 0; i < nplayers; i++)
        if (players[i].alive && players[i].region == REG_CROWN) return &players[i];
    return NULL;
}

/* The one decision that matters: do I step through the Dread Gate?
 *
 * This is where all six endings come to a point.  Behind the Gate is a
 * different game every time, and half of them punish walking in unprepared,
 * so the answer is not "always yes" -- which is what it used to be. */
/* Fight the Demon Lord, or fall back and let him heal?
 *
 * Four Lives at Craft 12 is a long war, and every exchange you lose costs
 * you one of your own for good.  Fight while the odds are real; the moment
 * they are not, get out -- there is still a board to play.  ai_cross_gate()
 * asks this same function before crossing, so the two can never disagree. */
int ai_flee_demon(Player *p, int demon_lives)
{
    int win = win_chance(eff_craft(p), 12);

    /* On your last Life, only near-certain odds are worth it -- and nothing
     * below may override this.  Patience used to live in ai_cross_gate()
     * instead, where it did not look at Lives at all, so a Character on one
     * Life was told to cross by one rule and to run by another. */
    if (p->lives <= 1) return win < 55;

    if (demon_lives <= 1 && win >= 30) return 0;   /* one blow left -- finish it */
    if (win >= 30) return 0;

    /* Long odds, but we have Lives to spend and have waited outside the door
     * long enough.  Nothing in the Inner Region heals and there is no way
     * back out, so waiting forever loses exactly as surely as losing. */
    if (p->hedged >= 12 && win >= 20) return 0;
    return 1;
}

/* What should the Druid be this Turn?
 *
 * Alignment is only worth changing when it opens or closes something real:
 * an Object she is standing next to and cannot lift, one she is carrying and
 * would have to shed, or the Chapel when she is hurt. */
Alignment ai_pick_align(Player *p)
{
    int sid = space_id(p->region, p->idx), k;

    /* The test was inverted.  "A change would cost us this" is true of an
     * Object that needs the Alignment we ALREADY have -- an Object needing
     * one we do not have is a reason to change, not a reason to refuse.  As
     * written, carrying a Good-only Object while Evil was what stopped the
     * Druid becoming Good, and no character changed Alignment in either
     * direction across a hundred games. */
    for (k = 0; k < p->nitems; k++) {
        unsigned need = deck_proto[p->carried[k]].needs;
        if (need && need != NEEDS_ANY && (need & align_bit(p->align)))
            return p->align;              /* a change would cost us this */
    }
    for (k = 0; k < res_n[sid]; k++) {
        const Card *c = &deck_proto[res_card[sid][k]];
        unsigned need = c->needs;
        if (c->type != C_OBJECT || !need || need == NEEDS_ANY) continue;
        if (need & align_bit(p->align)) continue;
        if (need & NEEDS_EVIL)    return AL_EVIL;
        if (need & NEEDS_GOOD)    return AL_GOOD;
        return AL_NEUTRAL;
    }
    /* Being Evil shuts the Chapel, and the Chapel is the reliable way back
     * to full Lives. */
    if (p->align == AL_EVIL && p->lives < eff_maxlives(p))
        return p->card_align == AL_EVIL ? AL_NEUTRAL : p->card_align;
    return p->align;
}

/* Going back out over the river is for one thing only: you are in the
 * Middle Region without what the next door needs, and the answers are all
 * behind you.  Otherwise the Raft is worth more kept than spent. */
int ai_recross_river(Player *p)
{
    if (p->talisman) return 0;                  /* the Portal is already open */
    if (best_stat(p) >= 6) return 0;            /* strong enough to press on  */
    if (ending_kind() == END_DEMON && eff_craft(p) < 8) return 1;
    return p->lives <= 2;
}

int ai_cross_gate(Player *p)
{
    if (!alt_endings) return 1;                  /* the old game: always */

    /* The Demon Lord is decided first and on its own, because crossing and
     * fleeing must be the SAME question.  They were not: the patience
     * shortcut below ignored Lives, while ai_flee_demon() checks Lives
     * before anything else -- so a Character on their last Life with
     * middling Craft was told to cross by one and to run by the other, and
     * walked through that door nine hundred and forty-eight times. */
    if (ending_kind() == END_DEMON) {
        /* "an impenetrable mystic barrier prevents any other players from
         * entering the Valley of Fire" -- no queue to out-wait. */
        if (crown_sitter()) return 0;
        p->hedged++;                 /* the patience the flee test reads */
        return !ai_flee_demon(p, 4) && p->lives >= 2;
    }

    /* Patience, but not paralysis.  A human waits for better odds; a human
     * also knows that standing outside the door forever loses the game just
     * as surely as walking in badly. */
    if (p->hedged >= 8) return 1;
    p->hedged++;

    switch (ending_kind()) {
    /* The Crown is a one-way door -- nothing in the game moves a Character
     * off it -- and the Hunt is not won there but out on the board, four
     * Nether Enemies at a time.  Crossing early removes you from the game
     * without ending it: in eleven Hunt games the character on the Crown
     * had none of the four in eight of them, and sat there for as many as
     * 1,017 turns while somebody outside reached nine. */
    case END_HUNT:
        /* Four first -- but a Character already inside the Inner Region has
         * nothing to gather there (Nether Enemies come out of the Adventure
         * deck, and the inner ring holds almost no Adventure spaces) and no
         * way back out except through the Crown, which now turns the short
         * of four around.  So going in is refused and going on is the exit. */
        return p->nether_kills >= 4 || p->region == REG_INNER;
    case END_BELT:
        /* Only worth crossing while the Belt is still lying there. */
        return belt_wearer_p() == NULL;
    case END_DRAGON:
        /* "If you lose all your Lives, he has eaten you... You lose this
         * game!"  One face in six is an outright win and one is the end of
         * you, so it is a gamble worth taking -- but not on fumes. */
        return p->lives >= 3 || (p->lives >= 2 && eff_craft(p) >= 5);
    case END_PANDORA:
    case END_CROWN:
        return 1;                                /* the throne is the game */
    default:
        /* Face down.  One card in six annihilates whoever crosses first. */
        return !ai_hedge_crossing(p);
    }
}

/* Is it worth abandoning what I am doing and running for the Crown? */
static int should_rush_crown(Player *p)
{
    Player *sitter = crown_sitter();

    switch (ending_kind()) {
    case END_HUNT:                      /* the four come first, always */
    case END_DEMON:
    case END_BELT:
    case END_DRAGON:
        return ai_cross_gate(p);        /* only if we would go through */
    case END_PANDORA:
        return 1;                       /* the bolts reach everywhere  */
    default:
        return sitter != NULL;          /* the Command Spell: contest it */
    }
}

/* Pre-reveal, with the endings in play, one card in six annihilates whoever
 * crosses first.  A human in second place lets the leader test the water;
 * a human in the lead goes, because waiting loses just as surely. */
static int ai_hedge_crossing(Player *p)
{
    int i, others_inner = 0;

    if (!alt_endings || ending_kind() >= 0) return 0;
    if (is_leader(p)) return 0;                  /* ahead: no time to wait */
    for (i = 0; i < nplayers; i++)
        if (players[i].alive && &players[i] != p && players[i].region >= REG_INNER)
            others_inner++;
    if (!others_inner) return 0;                 /* nobody to send first    */
    p->hedged++;
    return 1;
}

/* Pandora's Box and the Belt both need a name.  Kill the wounded first --
 * after the Crown is reached the dead stay dead, so a life taken off
 * somebody on their last one is a whole player removed -- and otherwise
 * hit whoever is winning. */
int ai_bolt_target(Player *p)
{
    int i, best = -1, best_score = -1;
    int safest = -1, safest_odds = -1;
    int belt = (ending_kind() == END_BELT);

    for (i = 0; i < nplayers; i++) {
        Player *q = &players[i];
        int sc;
        if (q == p || !q->alive) continue;
        {
            int odds = win_chance(combat_stat(p), defence_stat(p, q));
            /* The softest target we have seen, in case none clear the bar.
             * The Belt says "You must move to a space occupied by another
             * Character and challenge them to a duel" -- so the choice is
             * whom, never whether, and it must be the one we can actually
             * beat.  Taking the first name on the list walked a Strength-12
             * Belt into a Strength-15 Zulu over and over. */
            if (odds > safest_odds) { safest_odds = odds; safest = i; }
            if (belt && odds < 40) continue;
        }
        sc = lead_score(q) + (q->lives == 1 ? 25 : 0) + (q->talisman ? 5 : 0);
        if (sc > best_score) { best_score = sc; best = i; }
    }
    if (best < 0 && belt) best = safest;
    return best;
}

/* Is attacking this character worth it?
 *
 * The rule: never fight a war you cannot win, never refuse one you are
 * likely to.  Between those, how much there is to gain decides -- and
 * whoever is winning is worth worse odds than a nobody, because letting
 * them walk to the Crown unopposed loses the game just as surely. */
int ai_attack(Player *att, Player *def)
{
    int win  = win_chance(combat_stat(att), defence_stat(att, def));
    int gain = 0;

    if (def->talisman && !att->talisman) gain += 3;   /* the thing you need */
    if (is_leader(def))                  gain += 2;   /* gang up            */
    if (def->lives == 1)                 gain += 2;   /* finish them        */
    if (def->gold >= 3)                  gain += 1;
    /* Once someone holds the Crown the rest are dying to the Command Spell
     * anyway; sitting still is not safety, it is a slower loss. */
    if (endgame() && att->region != REG_CROWN) gain += 3;

    /* Killing the wearer of the Belt of Hercules sends it back to the Crown
     * and puts the game back on the table, so it is worth long odds -- and
     * being the one who has to fight the Belt is worth none at all. */
    if (belt_wearer_p() == def) gain += 6;
    if (att->belt)              gain += 4;   /* superhumanly strong: swing  */

    /* The Demon Lord stands in a doorway nobody can force.  While he holds
     * it the Crown decides nothing, so the game is decided out here -- which
     * makes picking a fight the productive move, not the wasteful one. */
    if (ending_kind() == END_DEMON) gain += 3;

    if (win < DESPERATE) return 0;                    /* an impossible war  */
    if (att->lives <= 1 && win < 65 && !endgame()) return 0;   /* not your last life */
    return win >= WORTH_FIGHTING - gain * 5;
}

/* Spend a Fate point on a reroll, or keep it?
 *
 * This needs the die you actually rolled, not just how far behind you are.
 * You reroll your own d6, so you need a result greater than what it showed
 * plus the deficit -- rolling a 1 while 2 behind is a coin flip, rolling a
 * 4 while 2 behind cannot be saved at all.  The deficit alone cannot tell
 * those apart, which is why this used to guess.
 *
 * Fate is not scarce here -- the Gnome, the Guide, the Pixie, the Ring and
 * the Tavern all give it back -- so hoarding it is a slow way of dying.
 * Spend on anything better than even, and on anything at all when the
 * alternative is your last life.
 */
int ai_use_fate(Player *p, int deficit, int rolled)
{
    int need, chance;

    if (p->fate <= 0) return 0;
    need   = rolled + deficit;            /* the reroll must beat this */
    chance = (need >= 6) ? 0 : (6 - need) * 100 / 6;

    if (chance == 0)      return 0;       /* no reroll can bridge it */
    if (p->lives <= 1)    return 1;       /* any chance beats dying  */
    if (endgame())        return chance >= 34;   /* no time to hoard */
    return chance >= 50;
}

/* What to buy, or 0 to walk out.  Wounded first, then kit, then magic. */
int ai_shop_choice(Player *p, const ShopLine *lines, int n)
{
    int i, best = 0, bv = 0;

    for (i = 0; i < n; i++) {
        int v = 0;
        if (lines[i].price > p->gold || lines[i].avail == 0) continue;
        switch (lines[i].key) {
        case 'h':                                     /* the Doctor */
            if (p->lives < eff_maxlives(p)) v = (p->lives <= 2) ? 30 : 8;
            break;
        case 'm':                                     /* the Mystic */
            if (spell_limit(p) > p->nspells) v = 12;
            break;
        case 'a':                                     /* the Alchemist */
            /* enforce_limit() has already brought us back to the limit by
             * the time we are standing at a counter, so `>` was never true
             * and this line never fired. */
            if (item_objects(p) >= item_limit(p)) v = 20;
            else if (p->gold < 2 && item_objects(p) > 1) v = 6;   /* broke */
            break;
        case 'c':                                     /* a Horse and Cart */
            if (item_objects(p) >= item_limit(p)) v = 18;
            break;
        default:                                      /* an Object on the shelf */
            if (lines[i].proto >= 0) {
                const Card *c = &deck_proto[lines[i].proto];
                int worth = c->d_str * 6 + c->d_craft * 6 + c->d_life * 4
                          + c->d_fate * 3 + c->carry * 5;
                if (c->carry > 0 && item_objects(p) >= item_limit(p) - 1) worth += 10;
                if (worth <= 0) break;                /* the Raft does nothing */
                if (item_objects(p) < item_limit(p) || c->carry > 0)
                    v = worth - lines[i].price;
            }
            break;
        }
        if (v > bv) { bv = v; best = lines[i].key; }
    }
    return best;
}

/* Which Spell to cast, or -1 to hold on to them all. */
int ai_spell_choice(Player *p, int battle_only)
{
    int i, k;

    for (i = 0; i < p->nspells; i++) {
        const Spell *sp = &spell_proto[p->spells[i]];
        if (SPELL_IN_BATTLE(sp->kind) != battle_only) continue;
        switch (sp->kind) {
        case SK_HEAL:       if (p->lives < eff_maxlives(p)) return i; break;
        case SK_ALCHEMY:    if (item_objects(p) >= item_limit(p)) return i; break;
        case SK_PRESERVE:   if (p->lives <= 2) return i; break;
        case SK_INVISIBLE:  if (p->lives <= 1) return i; break;
        case SK_PSIONIC:    if (eff_craft(p) > 2) return i; break;
        case SK_NULLIFY:    return i;

        /* A Toad drops everything it carries and fights at Strength 1, so
         * this is worth most against whoever is carrying most -- and worth
         * nothing at all cast into an empty space. */
        case SK_TOAD:
            for (k = 0; k < nplayers; k++) {
                Player *q = &players[k];
                if (!q->alive || q == p) continue;
                if (q->region != p->region || q->idx != p->idx) continue;
                if (q->talisman || item_objects(q) > 0 || is_leader(q)) return i;
            }
            break;
        
        /* Check the target exists first: a Spell thrown into an empty space
         * is simply gone. */
        case SK_ACQUIRE:            /* the Miser's Grasp and its like */
            for (k = 0; k < nplayers; k++) {
                if (players[k].alive && players[k].idx == p->idx && players[k].region == p->region && &players[k] != p && item_objects(&players[k]) > 0)
                    return i;
            }
            break;
        case SK_DESTROYMAGIC: {
            /* Destroy Magic removes a Place or a Stranger, not just any
             * card -- checking that the space is merely occupied still
             * fired it on a lone Enemy and printed "there is no magic here
             * to undo" eight times in forty games. */
            int sid = space_id(p->region, p->idx);
            for (k = 0; k < res_n[sid]; k++) {
                CardType t = deck_proto[res_card[sid][k]].type;
                if (t == C_PLACE || t == C_STRANGER) return i;
            }
            break;
        }

        /* The rest do something to a rival, to the board, or to you, and
         * each is only worth a card when there is something to aim at.
         * Holding them all instead left 74 of the 127 Spell cards never
         * cast at all -- including Divination, which is one of the things
         * that gives Fate back. */
        case SK_DIVINATION:  return i;             /* +1 Fate, always good */
        case SK_HEX:
        case SK_IMMOBILITY:
        case SK_MESMERISE:
            if (any_rival(p)) return i;            /* somebody to aim at   */
            break;
        case SK_DESTRUCTION: {
            int sid = space_id(p->region, p->idx);
            for (k = 0; k < res_n[sid]; k++) {
                CardType t = deck_proto[res_card[sid][k]].type;
                if (t == C_ENEMY || t == C_SPIRIT) return i;
            }
            break;
        }
        case SK_TELEPORT:
        case SK_WARP:
            if (p->region < REG_INNER) return i;   /* a way inward         */
            break;
        case SK_COUNTER:
            if (endgame()) return i;               /* worth holding until  */
            break;
        case SK_RANDOM:
            if (p->lives >= 2) return i;           /* a gamble, not a last resort */
            break;

        default:
            break; 
        }
    }
    return -1;
}