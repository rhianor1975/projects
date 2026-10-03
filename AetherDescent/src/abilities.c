#include "abilities.h"
#include "combat.h"
#include "render.h"

static const char *ABILITY_NAMES[ABILITY_COUNT] = {
    "Crushing Blow", "Adrenaline Surge", "Evasive Roll", "Riposte Stance",
    "Called Shot", "Unbreakable", "Iron Resolve", "Second Wind",
    "Steady Footing", "Predator's Sense"
};

static const char *ABILITY_DESCS[ABILITY_COUNT] = {
    "a single heavy strike against the nearest foe",
    "instantly heals you",
    "a burst of evasion for a few turns",
    "a burst of attack power for a few turns",
    "a guaranteed heavy hit against the nearest foe",
    "a burst of defence for a few turns",
    "a large instant heal",
    "heals you and sharpens your attack briefly",
    "a burst of evasion and defence together",
    "locks the nearest foe in place and lights up your surroundings"
};

static const int ABILITY_COOLDOWNS[ABILITY_COUNT] = {
    6, 8, 7, 7, 6, 8, 9, 8, 8, 7
};

const char *ability_name(int id) {
    if (id < 0 || id >= ABILITY_COUNT) return "?";
    return ABILITY_NAMES[id];
}

const char *ability_desc(int id) {
    if (id < 0 || id >= ABILITY_COUNT) return "";
    return ABILITY_DESCS[id];
}

int ability_cooldown_max(int id) {
    if (id < 0 || id >= ABILITY_COUNT) return 8;
    return ABILITY_COOLDOWNS[id];
}

void tick_ability(Hero *h) {
    if (h->ability_cd > 0) h->ability_cd--;
}

static void reveal_around(Map *m, int cx, int cy, int radius) {
    for (int y = cy - radius; y <= cy + radius; y++) {
        for (int x = cx - radius; x <= cx + radius; x++) {
            if (x < 0 || x >= MAP_W || y < 0 || y >= MAP_H) continue;
            int dx = x - cx, dy = y - cy;
            if (dx * dx + dy * dy <= radius * radius) m->tiles[y][x].seen = true;
        }
    }
}

bool use_ability(Player *p, Hero *h, Map *m, bool *out_boss_killed) {
    if (out_boss_killed) *out_boss_killed = false;

    if (h->ability_cd > 0) {
        log_msg("%s isn't ready yet (%d turns).", ability_name(h->ability_id), h->ability_cd);
        return false;
    }

    const int *a = h->attrs;
    const char *name = ability_name(h->ability_id);

    switch (h->ability_id) {
        case ABILITY_CRUSHING_BLOW: {
            Monster *target = find_nearest_target(m, p, 2);
            if (!target) {
                log_msg("There's nothing in reach for %s.", name);
                return false;
            }
            int dmg = 25 + a[ATTR_MIGHT] * 3;
            log_msg("%s crashes into the %s for %d.", name, target->name, dmg);
            anim_flash_cell(m, p, target->x, target->y, (chtype)target->glyph, CP_HIT, 3);
            monster_take_damage(p, h, m, target, dmg, out_boss_killed);
            break;
        }
        case ABILITY_ADRENALINE: {
            int heal = 20 + a[ATTR_BRAWN] * 4;
            h->hp += heal;
            if (h->hp > h->maxhp) h->hp = h->maxhp;
            anim_flash_cell(m, p, h->x, h->y, '@', CP_ABILITY, 2);
            log_msg("%s surges through you, healing %d HP.", name, heal);
            break;
        }
        case ABILITY_EVASIVE_ROLL: {
            int bonus = 15 + a[ATTR_AGILITY] * 2;
            h->evasion_buff = bonus;
            h->evasion_buff_turns = 8;
            anim_flash_cell(m, p, h->x, h->y, '@', CP_ABILITY, 2);
            log_msg("%s -- +%d%% evasion for 8 turns.", name, bonus);
            break;
        }
        case ABILITY_RIPOSTE: {
            int bonus = 10 + a[ATTR_REFLEXES] * 2;
            h->atk_buff = bonus;
            h->atk_buff_turns = 8;
            anim_flash_cell(m, p, h->x, h->y, '@', CP_ABILITY, 2);
            log_msg("%s -- +%d attack for 8 turns.", name, bonus);
            break;
        }
        case ABILITY_CALLED_SHOT: {
            Monster *target = find_nearest_target(m, p, 6);
            if (!target) {
                log_msg("There's nothing in sight for %s.", name);
                return false;
            }
            int dmg = 25 + a[ATTR_PRECISION] * 4;
            log_msg("%s finds its mark on the %s for %d.", name, target->name, dmg);
            anim_bolt(m, p, h->x, h->y, target->x, target->y, '*', CP_HIT);
            anim_flash_cell(m, p, target->x, target->y, (chtype)target->glyph, CP_HIT, 2);
            monster_take_damage(p, h, m, target, dmg, out_boss_killed);
            break;
        }
        case ABILITY_UNBREAKABLE: {
            int bonus = 12 + a[ATTR_GRIT] * 2;
            h->def_buff = bonus;
            h->def_buff_turns = 10;
            anim_flash_cell(m, p, h->x, h->y, '@', CP_ABILITY, 2);
            log_msg("%s -- +%d defence for 10 turns.", name, bonus);
            break;
        }
        case ABILITY_IRON_RESOLVE: {
            int heal = 25 + a[ATTR_FORTITUDE] * 4;
            h->hp += heal;
            if (h->hp > h->maxhp) h->hp = h->maxhp;
            anim_flash_cell(m, p, h->x, h->y, '@', CP_ABILITY, 2);
            log_msg("%s steadies you, healing %d HP.", name, heal);
            break;
        }
        case ABILITY_SECOND_WIND: {
            int heal = 15 + a[ATTR_STAMINA] * 3;
            h->hp += heal;
            if (h->hp > h->maxhp) h->hp = h->maxhp;
            h->atk_buff = 8;
            h->atk_buff_turns = 8;
            anim_flash_cell(m, p, h->x, h->y, '@', CP_ABILITY, 2);
            log_msg("%s heals %d HP and sharpens your next attacks.", name, heal);
            break;
        }
        case ABILITY_STEADY_FOOTING: {
            int ev = 10 + a[ATTR_BALANCE] * 2;
            h->evasion_buff = ev;
            h->evasion_buff_turns = 8;
            h->def_buff = 8;
            h->def_buff_turns = 8;
            anim_flash_cell(m, p, h->x, h->y, '@', CP_ABILITY, 2);
            log_msg("%s -- +%d%% evasion and +8 defence for 8 turns.", name, ev);
            break;
        }
        case ABILITY_PREDATORS_SENSE: {
            Monster *target = find_nearest_target(m, p, 6);
            reveal_around(m, h->x, h->y, 6);
            anim_burst(m, p, h->x, h->y, 6, '.', CP_ABILITY);
            if (target) {
                target->stun_turns_left = 3 + a[ATTR_INSTINCT] / 3;
                anim_flash_cell(m, p, target->x, target->y, (chtype)target->glyph, CP_ABILITY, 2);
                log_msg("%s locks the %s in place and lights up your surroundings.", name, target->name);
            } else {
                log_msg("%s lights up your surroundings.", name);
            }
            break;
        }
        default:
            return false;
    }

    h->ability_cd = ability_cooldown_max(h->ability_id);
    return true;
}
