#include "ranged.h"
#include "combat.h"
#include "render.h"

static const char *RANGED_TYPE_NAMES[] = {
    "Bow", "Gun", "Laser", "Blowgun", "Thrown", "Grenade", "None"
};

static const int RANGED_COLOR_PAIRS[] = {
    CP_RANGED_BOW, CP_RANGED_GUN, CP_RANGED_LASER,
    CP_RANGED_BLOWGUN, CP_RANGED_THROWN, CP_RANGED_GRENADE
};

static const chtype RANGED_GLYPHS[] = { '/', '-', '=', '.', '*', 'o' };

const char *ranged_type_name(int type) {
    if (type < 0 || type > RANGED_NONE) return "?";
    return RANGED_TYPE_NAMES[type];
}

int ranged_color_pair(int type) {
    if (type < 0 || type >= RANGED_NONE) return CP_HIT;
    return RANGED_COLOR_PAIRS[type];
}

chtype ranged_glyph(int type) {
    if (type < 0 || type >= RANGED_NONE) return '*';
    return RANGED_GLYPHS[type];
}

void tick_ranged(Player *p, Hero *h) {
    if (h->ranged_cooldown > 0) h->ranged_cooldown--;
    /* Paced off the turn counter rather than a field of its own, so this
       doesn't change sizeof(Player) and invalidate everyone's saved run. */
    if (p->turns % RANGED_AMMO_REGEN_TURNS != 0) return;
    if (h->ranged_ammo < h->ranged_ammo_max) h->ranged_ammo++;
}

void equip_ranged(Hero *h, const char *name, int type, int bonus, int ammo_cost, int cooldown) {
    strncpy(h->ranged_name, name, sizeof(h->ranged_name) - 1);
    h->ranged_name[sizeof(h->ranged_name) - 1] = '\0';
    h->ranged_type = type;
    h->ranged_bonus = bonus;
    h->ranged_ammo_cost = ammo_cost;
    h->ranged_cooldown_max = cooldown;
    h->ranged_cooldown = 0;
}

/* How far each weapon type can reach out and find a target. Not tiered --
   like spell reach, the weapon's tier scales damage, not range. */
static int reach_for_type(int type) {
    switch (type) {
        case RANGED_BOW:     return 6;
        case RANGED_GUN:     return 8;
        case RANGED_LASER:   return 7;
        case RANGED_BLOWGUN: return 5;
        case RANGED_THROWN:  return 4;
        case RANGED_GRENADE: return 5;
        default:              return 5;
    }
}

bool fire_ranged(Player *p, Hero *h, Map *m, bool *out_boss_killed) {
    if (out_boss_killed) *out_boss_killed = false;

    if (h->ranged_type == RANGED_NONE) {
        log_msg("You have no ranged weapon equipped.");
        return false;
    }
    /* Every skin of the proving ground forbids shooting: both duels are about
       closing with somebody, and a bow is the way out of that. */
    if (!proving_allows(m, h->x, h->y, PROVE_RANGED_ACT)) {
        log_msg("Not here. Nothing is settled at a distance on this ground.");
        return false;
    }
    if (h->ranged_cooldown > 0) {
        log_msg("%s needs a moment to reset. (%d)", h->ranged_name, h->ranged_cooldown);
        return false;
    }
    if (h->ranged_ammo < h->ranged_ammo_cost) {
        log_msg("Not enough ammo left for %s.", h->ranged_name);
        return false;
    }

    int reach = reach_for_type(h->ranged_type);
    int dmg = h->ranged_bonus + h->ranged_bonus * h->ranged_dmg_bonus_pct / 100;
    int color = ranged_color_pair(h->ranged_type);
    chtype glyph = ranged_glyph(h->ranged_type);

    /* Crystal district: a shot can come off a facet and find you. The round
       is spent regardless -- it left the barrel. */
    if (crystal_ricochet(m, p, dmg, h->ranged_name)) {
        h->ranged_ammo -= h->ranged_ammo_cost;
        h->ranged_cooldown = h->ranged_cooldown_max;
        return true;
    }

    switch (h->ranged_type) {
        case RANGED_BOW:
        case RANGED_GUN: {
            Monster *target = find_nearest_target(m, p, reach);
            if (!target) {
                log_msg("Nothing in sight for %s to hit.", h->ranged_name);
                return false;
            }
            log_msg("%s strikes the %s for %d.", h->ranged_name, target->name, dmg);
            anim_bolt(m, p, h->x, h->y, target->x, target->y, glyph, color);
            anim_flash_cell(m, p, target->x, target->y, (chtype)target->glyph, color, 2);
            monster_take_damage(p, h, m, target, dmg, out_boss_killed);
            break;
        }
        case RANGED_LASER: {
            Monster *first = find_nearest_target(m, p, reach);
            if (!first) {
                log_msg("Nothing in sight for %s to hit.", h->ranged_name);
                return false;
            }
            log_msg("%s lances through the %s for %d.", h->ranged_name, first->name, dmg);
            anim_bolt(m, p, h->x, h->y, first->x, first->y, glyph, color);
            anim_flash_cell(m, p, first->x, first->y, (chtype)first->glyph, color, 2);
            monster_take_damage(p, h, m, first, dmg, out_boss_killed);
            if (!(out_boss_killed && *out_boss_killed)) {
                Monster *second = find_nearest_target_excluding(m, p, reach, first);
                if (second) {
                    log_msg("The beam pierces on into the %s for %d.", second->name, dmg);
                    anim_bolt(m, p, first->x, first->y, second->x, second->y, glyph, color);
                    anim_flash_cell(m, p, second->x, second->y, (chtype)second->glyph, color, 2);
                    monster_take_damage(p, h, m, second, dmg, out_boss_killed);
                }
            }
            break;
        }
        case RANGED_BLOWGUN: {
            Monster *target = find_nearest_target(m, p, reach);
            if (!target) {
                log_msg("Nothing in sight for %s to hit.", h->ranged_name);
                return false;
            }
            log_msg("%s needles the %s for %d -- its attacks falter.", h->ranged_name, target->name, dmg);
            anim_bolt(m, p, h->x, h->y, target->x, target->y, glyph, color);
            anim_flash_cell(m, p, target->x, target->y, (chtype)target->glyph, color, 2);
            target->jinx_atk_penalty = dmg / 3 + 1;
            target->jinx_turns_left = 4;
            monster_take_damage(p, h, m, target, dmg, out_boss_killed);
            break;
        }
        case RANGED_THROWN: {
            Monster *target = find_nearest_target(m, p, reach);
            if (!target) {
                log_msg("Nothing in sight for %s to hit.", h->ranged_name);
                return false;
            }
            int eff_crit = h->crit_pct + h->set_bonus_crit;
            bool crit = (eff_crit > 0 && rand() % 100 < eff_crit);
            int final_dmg = crit ? dmg * 2 : dmg;
            if (crit) log_msg("A perfect throw! %s finds the %s for %d.", h->ranged_name, target->name, final_dmg);
            else log_msg("%s strikes the %s for %d.", h->ranged_name, target->name, final_dmg);
            anim_bolt(m, p, h->x, h->y, target->x, target->y, glyph, color);
            anim_flash_cell(m, p, target->x, target->y, (chtype)target->glyph, color, crit ? 3 : 2);
            monster_take_damage(p, h, m, target, final_dmg, out_boss_killed);
            break;
        }
        case RANGED_GRENADE: {
            int hits = damage_all_in_reach(m, p, 2, dmg, out_boss_killed);
            if (hits == 0) {
                log_msg("%s finds nothing nearby to hit.", h->ranged_name);
                return false;
            }
            anim_burst(m, p, h->x, h->y, 2, glyph, color);
            log_msg("%s explodes, striking %d foe%s for %d each.", h->ranged_name, hits, hits == 1 ? "" : "s", dmg);
            break;
        }
        default:
            return false;
    }

    h->ranged_ammo -= h->ranged_ammo_cost;
    h->ranged_cooldown = h->ranged_cooldown_max;
    return true;
}

int ranged_reach(const Hero *h) {
    if (!h || h->ranged_type == RANGED_NONE) return 0;
    /* A grenade searches out to reach_for_type but only damages within the
       blast radius fire_ranged passes to damage_all_in_reach -- report the
       radius that actually kills something. */
    if (h->ranged_type == RANGED_GRENADE) return 2;
    return reach_for_type(h->ranged_type);
}

bool ranged_ready(const Hero *h) {
    if (!h || h->ranged_type == RANGED_NONE) return false;
    if (h->ranged_cooldown > 0) return false;
    if (h->ranged_ammo < h->ranged_ammo_cost) return false;
    return true;
}
