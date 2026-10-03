#include "bands.h"
#include "dda.h"
#include "mapgen.h"

/* See bands.h for the three decisions this file rests on: invisible,
   asymmetric, and one knob. */

int band_help(const Player *p) {
    if (!p) return 0;
    if (p->floor <= 0) return 0;               /* town is not a place that helps */
    if (BAND_MAX_HELP <= 0) return 0;

    /* Negative pressure only. A run that is coasting gets the game as written;
       there is no band that takes anything away. */
    int pressure = dda_pressure(p);
    if (pressure >= 0) return 0;

    /* Linear from the first point of trouble to the floor of the scale, and
       rounded *up*, which is load-bearing rather than tidy.
     *
       Floored division made the whole system inert for the case it exists for.
       A run dying on floor 1 has no settled history to speak of -- it has not
       finished a floor -- so all it carries is the live reading, which bottoms
       out at DDA_ROUTED (-14) against a scale that runs to -40. Floored, that
       is help = 1, and two of the five gifts divide it back down to nothing.
       The band that is supposed to catch a floor-1 death caught nothing at
       all. Measured on a probe before this changed.

       Rounding up means any trouble at all buys the smallest real step, and
       the scale still runs its full length for a run that is genuinely
       drowning. */
    int depth = -pressure;                      /* 1..-DDA_MIN */
    int span  = -DDA_MIN;
    int help  = (depth * BAND_MAX_HELP + span - 1) / span;
    if (help > BAND_MAX_HELP) help = BAND_MAX_HELP;
    return help;
}

/* Each gift is gated on its own band and expresses band_help in its own
   units. The units are the only per-band numbers, and each is the smallest
   step that is a step at all in that system. */

static bool on_band(const Player *p, Biome b) {
    return p && p->floor > 0 && get_biome_for_floor(p->floor) == b;
}

int band_regen_bonus(const Player *p) {
    /* Growth that feeds you. Half a step per point, so the Roots at their most
       generous are +2 a turn -- noticeable across a fight, invisible in any
       single turn, and never a substitute for a draught. Rounded up, so a run
       in trouble at all gets the +1 rather than nothing. */
    if (!on_band(p, BIOME_JUNGLE)) return 0;
    return (band_help(p) + 1) / 2;              /* 1..2 hit points a turn */
}

int band_recharge_bonus(const Player *p) {
    /* Machinery still under power. One extra point per regeneration tick per
       two steps of help: the Works give back what you spend a little faster,
       which is the band where a caster first runs dry. */
    if (!on_band(p, BIOME_INDUSTRIAL)) return 0;
    return (band_help(p) + 1) / 2;              /* 1..2 extra per tick */
}

int band_ward_bonus(const Player *p) {
    /* Wards that still hold. Three points of ward per step: at full help that
       is twelve per cent of blows turned aside, which is real without being
       the reason a floor was survived. */
    if (!on_band(p, BIOME_RUINS)) return 0;
    return band_help(p) * 3;
}

bool band_supply_saved(const Player *p) {
    /* Salt that preserves. A one-in-ten chance per step that a consumable is
       not spent -- the Wastes are where a run's supplies run out, and this
       stretches them rather than replacing them. */
    if (!on_band(p, BIOME_WASTES)) return false;
    int help = band_help(p);
    if (help <= 0) return false;
    return (rand() % 100) < (help * 10);
}

int band_aggro_reduction(const Player *p) {
    /* Dark that hides as it blinds. The Abyss is the band that suppresses
       sight; the same dark takes a tile off what notices you, which is the
       only gift here that is also the band's own punishment turned around. */
    if (!on_band(p, BIOME_ABYSS)) return 0;
    return band_help(p);
}

/* ---- what the place always does ----------------------------------------
   See bands.h. Unconditional, the same for everybody, and meant to be learned
   rather than hidden -- the opposite decision from the gifts above, for the
   opposite reason: the player did not choose to be struggling, but they did
   choose to walk into the Abyss. */

#define BIOME_GROWTH_COVER   2   /* tiles of notice the Roots take off both sides */
#define BIOME_RUINS_WARD_PCT 12  /* blows a warded monster turns aside */
#define BIOME_WASTES_BITE    3   /* extra damage the salt does */
#define BIOME_ABYSS_BLIND    3   /* tiles of sight the dark takes */

int biome_growth_cover(const Player *p) {
    return on_band(p, BIOME_JUNGLE) ? BIOME_GROWTH_COVER : 0;
}

int biome_monster_ward_pct(const Player *p) {
    return on_band(p, BIOME_RUINS) ? BIOME_RUINS_WARD_PCT : 0;
}

int biome_hazard_extra(const Player *p) {
    return on_band(p, BIOME_WASTES) ? BIOME_WASTES_BITE : 0;
}

int biome_sight_penalty(const Player *p) {
    return on_band(p, BIOME_ABYSS) ? BIOME_ABYSS_BLIND : 0;
}

