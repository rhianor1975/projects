#include "features.h"
#include "combat.h"
#include "companions.h"
#include "monsters.h"
#include "items.h"
#include "gearsets.h"

static void trigger_shrine(Player *p) {
    switch (rand() % 4) {
        case 0:
            hero_driven(p)->maxhp += 5;
            hero_driven(p)->hp += 5;
            log_msg("The shrine remembers you passing. +5 max HP, permanently.");
            break;
        case 1:
            hero_driven(p)->base_atk += 1;
            log_msg("The shrine remembers you passing. +1 attack, permanently.");
            break;
        case 2:
            hero_driven(p)->base_def += 1;
            log_msg("The shrine remembers you passing. +1 defence, permanently.");
            break;
        default:
            hero_driven(p)->hp = hero_driven(p)->maxhp;
            log_msg("The shrine remembers you passing. You are fully healed.");
            break;
    }
}

static void trigger_fountain(Player *p) {
    int roll = rand() % 100;
    int bad_chance = 5 - hero_driven(p)->fortune_luck_pct / 6;
    if (bad_chance < 0) bad_chance = 0;
    int def_cut = 100 - bad_chance;
    if (roll < 45) {
        int heal = 20 + rand() % 20;
        hero_driven(p)->hp += heal;
        if (hero_driven(p)->hp > hero_driven(p)->maxhp) hero_driven(p)->hp = hero_driven(p)->maxhp;
        log_msg("The water is cold and clean. You drink, and feel it -- +%d HP.", heal);
    } else if (roll < 75) {
        hero_driven(p)->atk_buff = 5;
        hero_driven(p)->atk_buff_turns = 25;
        log_msg("The water tingles like static. +5 attack for 25 turns.");
    } else if (roll < def_cut) {
        hero_driven(p)->def_buff = 5;
        hero_driven(p)->def_buff_turns = 25;
        log_msg("The water settles your nerves. +5 defence for 25 turns.");
    } else {
        int dmg = 6 + rand() % 8;
        hero_driven(p)->hp -= dmg;
        if (hero_driven(p)->hp < 0) hero_driven(p)->hp = 0;
        log_msg("The water was not water. It costs you %d HP.", dmg);
    }
}

static void trigger_machine(Map *m, Player *p, int floor_num) {
    int roll = rand() % 100;
    int ambush_width = 25 - hero_driven(p)->machine_luck_pct;
    if (ambush_width < 5) ambush_width = 5;
    int ambush_cut = 60 + ambush_width;
    if (roll < 35) {
        int gold = player_gain_gold(p, 15 + floor_num + rand() % 30);
        log_msg("Ancient machinery disgorges a handful of coin -- +%d gold.", gold);
    } else if (roll < 60) {
        for (int y = hero_driven(p)->y - 12; y <= hero_driven(p)->y + 12; y++) {
            for (int x = hero_driven(p)->x - 12; x <= hero_driven(p)->x + 12; x++) {
                if (x < 0 || x >= MAP_W || y < 0 || y >= MAP_H) continue;
                int dx = x - hero_driven(p)->x, dy = y - hero_driven(p)->y;
                if (dx * dx + dy * dy <= 144) m->tiles[y][x].seen = true;
            }
        }
        log_msg("The machine's lamps flare -- the passage ahead is briefly lit.");
    } else if (roll < ambush_cut) {
        int mx = -1, my = -1;
        for (int tries = 0; tries < 20; tries++) {
            int nx = hero_driven(p)->x + (rand() % 7 - 3);
            int ny = hero_driven(p)->y + (rand() % 7 - 3);
            if ((nx == hero_driven(p)->x && ny == hero_driven(p)->y) || !is_walkable_monster(m, nx, ny) || monster_at(m, nx, ny)) continue;
            mx = nx;
            my = ny;
            break;
        }
        if (mx >= 0 && m->monster_count < MAX_MONSTERS) {
            m->monsters[m->monster_count] = make_monster_for_floor(floor_num, mx, my);
            m->monsters[m->monster_count].aggro = true;
            m->monster_count++;
            log_msg("Gears grind and something wakes nearby. That was a mistake.");
        } else {
            log_msg("The machine shudders and falls still.");
        }
    } else {
        log_msg("The machine hisses steam and does nothing else. Someone got here first.");
    }
}

/* Half your health, now, for double gold and scrap for the next ten floors.
 *
 * It is priced in the one currency that cannot be bought back at a shop, and
 * it is a real gamble: the floors immediately after an altar are the ones
 * where a run is thinnest, and taking the bargain at 40% health on floor 60
 * is how a run ends. Nothing warns you twice. */
static void trigger_altar(MapFeature *f, Player *p) {
    if (hero_driven(p)->hp <= 1) {
        log_msg("The altar wants blood you do not have.");
        return;
    }
    f->used = true;

    int cost = hero_driven(p)->hp / 2;
    hero_driven(p)->hp -= cost;
    if (hero_driven(p)->hp < 1) hero_driven(p)->hp = 1;

    p->gold_boon_until_floor = p->floor + 10;
    log_msg("You open a vein across the basin. It costs you %d.", cost);
    log_msg("Everything down here glitters twice as bright, until floor %d.",
            p->gold_boon_until_floor);
}

/* Hand over the purse and they show you the way on.
 *
   Worth having only because the floors got big: on the Well, "lost, hurt, and
   a long way from the stairs" is a real position to be in, and until now the
   only answers were a recall charm or dying. This one always exists and
   always costs everything, which makes it a decision rather than a rescue. */
static void trigger_toll(Map *m, Player *p) {
    if (p->gold <= 0) {
        log_msg("The tollkeeper looks at your empty hands and goes back to sleep.");
        return;
    }
    if (m->stairs_down_x < 0) {
        log_msg("\"There is no onward from here,\" the tollkeeper says. \"Not for coin.\"");
        return;
    }

    int paid = p->gold;
    p->gold = 0;
    hero_driven(p)->x = m->stairs_down_x;
    hero_driven(p)->y = m->stairs_down_y;
    log_msg("You tip out %d gold. They walk you to the stairs without a word.", paid);
}

/* Three keys, and the line builds you a body to put between you and them.
 *
   The keys are the *same* keys that open locked doors, deliberately: it turns
   a currency you were hoarding into a choice, which is more interesting than
   inventing a second kind of key nobody has a use for anywhere else. */
static void trigger_console(Map *m, Player *p) {
    if (p->keys < CONSOLE_KEY_COST) {
        log_msg("The console wants %d keys turned at once. You have %d.",
                CONSOLE_KEY_COST, p->keys);
        return;
    }
    if (!companion_grant_golem(p, m->floor_num)) return;
    p->keys -= CONSOLE_KEY_COST;
    log_msg("Three keys turn together and the line runs for the first time in an age.");
}

/* The strongbox at the heart of a watch. Worth opening only while the wardens
   still do not know you are there -- which is the entire district stated as
   one branch.
 *
   What it pays is a permanent increase to how much coin the body that opened
   it finds, not a purse. See WATCH_FIND_PCT in common.h for why: §3.1's rule 3
   says flat gold is flavour at this economy's scale, and names a permanent %
   gold find as one of the two shapes a reward can take if it is meant to
   matter. */
static void trigger_strongbox(Map *m, MapFeature *f, Player *p) {
    int post = district_index_at(m, f->x, f->y);
    if (post < 0 || m->districts[post].kind != DIST_WATCH) return;

    if (m->districts[post].state == WATCH_BROKEN) {
        /* Not a punishment so much as an absence: what made the box worth
           taking was taking it quietly. */
        log_msg("The box is open and there is nothing in it worth the noise you made.");
        return;
    }
    if (m->districts[post].state == WATCH_TAKEN) return;

    Hero *h = hero_driven(p);
    int before = h->gold_bonus_pct;
    h->gold_bonus_pct += WATCH_FIND_PCT;
    if (h->gold_bonus_pct > WATCH_FIND_CAP) h->gold_bonus_pct = WATCH_FIND_CAP;
    m->districts[post].state = WATCH_TAKEN;

    if (h->gold_bonus_pct > before) {
        log_msg("Ledgers, routes, and where the good floors are. You are +%d%% on coin found,",
                h->gold_bonus_pct - before);
        log_msg("and nobody has looked up. Walk out the way you came.");
    } else {
        log_msg("More of what you already know. You take it anyway, quietly.");
    }
}

static void trigger_relic(MapFeature *f, Player *p) {
    apply_set_gear(hero_driven(p), f->relic_set_id, f->relic_kind);
}

void trigger_feature(Map *m, MapFeature *f, Player *p) {
    if (f->type == FEATURE_MERCHANT) return;
    if (f->used) return;

    switch (f->type) {
        case FEATURE_SHRINE:   trigger_shrine(p); break;
        case FEATURE_FOUNTAIN: trigger_fountain(p); break;
        case FEATURE_MACHINE:  trigger_machine(m, p, m->floor_num); break;
        case FEATURE_RELIC:    trigger_relic(f, p); break;
        case FEATURE_ALTAR:    trigger_altar(f, p); break;
        case FEATURE_TOLL:     trigger_toll(m, p); break;
        case FEATURE_CONSOLE:  trigger_console(m, p); break;
        case FEATURE_STRONGBOX: trigger_strongbox(m, f, p); break;
        default: break;
    }
    f->used = true;
}
