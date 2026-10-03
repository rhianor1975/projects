#include "common.h"
#include "autoplay.h"
#include "autoexplore.h"
#include "town.h"
#include "record.h"
#include "mapgen.h"
#include "monsters.h"
#include "items.h"
#include "combat.h"
#include "dda.h"
#include "bands.h"
#include "quests.h"
#include "spells.h"
#include "ranged.h"
#include "abilities.h"
#include "render.h"
#include "save.h"
#include "classes.h"
#include "features.h"
#include "gearsets.h"
#include "companions.h"
#include "vision.h"
#include "path.h"

#include <stdlib.h>   /* setenv, for the silent run's own state directory */

static void init_new_player(Player *p) {
    memset(p, 0, sizeof(*p));
    hero_driven(p)->hp = 40;
    hero_driven(p)->maxhp = 40;
    hero_driven(p)->base_atk = 5;
    hero_driven(p)->base_def = 2;
    hero_driven(p)->weapon_bonus = 0;
    hero_driven(p)->armor_bonus = 0;
    strncpy(hero_driven(p)->weapon_name, "Bare Fists", sizeof(hero_driven(p)->weapon_name) - 1);
    strncpy(hero_driven(p)->armor_name, "Tattered Coat", sizeof(hero_driven(p)->armor_name) - 1);
    /* The shipping purse, and the reason is worth keeping next to it: floor 1
       is the one place in the game where the fight is close to even (see
       docs/EVALUATION §5) -- no levels, no gear, no potions. 25 gold bought
       one draught; 60 buys a weapon and a couple of heals, which blunts the
       spike without touching a single mechanic.

       This sat at 600,000,000 for most of the six-actor rebuild, set at the
       project owner's request so the Tavern, the smith and the bank could be
       exercised from turn one without farming for them. It is back to 60 now
       because the balance pass has started, and every winnability number this
       project has ever quoted is a fair-start number -- a run that opens with
       six hundred million is not one.

       tests/balance.c asserts this. If it needs raising again for a play
       session, raise it there too and put both back together; a testing purse
       that outlives the testing is how a game ships unbalanced. */
    p->gold = NEW_GAME_GOLD;
    p->gold_mult = 1;   /* the bank sells shares of this, never gives one away */

    hero_driven(p)->level = 1;
    hero_driven(p)->xp = 0;
    hero_driven(p)->xp_next = 20;
    p->floor = 0;
    p->deepest_floor = 0;
    hero_driven(p)->kills = 0;
    p->turns = 0;
    p->inv_count = 0;
    hero_driven(p)->class_id = 0;
    p->difficulty = DIFFICULTY_NORMAL;
    hero_driven(p)->fov_radius = 7;
    hero_driven(p)->recall_channel_left = 0;
    hero_driven(p)->ring_stat = ACC_NONE;
    hero_driven(p)->trinket_stat = ACC_NONE;
    p->keys = 0;
    p->quest_active = false;
    hero_driven(p)->last_spell_slot = -1;
    hero_driven(p)->weapon_set = -1;
    hero_driven(p)->armor_set = -1;
    hero_driven(p)->ring_set = -1;
    hero_driven(p)->trinket_set = -1;
    strncpy(hero_driven(p)->name, "Wanderer", sizeof(hero_driven(p)->name) - 1);
}

static bool confirm_quit(void) {
    mvprintw(LINES - 1, 0, "Quit to desktop? (y/n)");
    clrtoeol();
    refresh();
    int ch = getch();
    return ch == 'y' || ch == 'Y';
}

static bool confirm_prompt(const char *fmt, ...) {
    char buf[160];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    mvprintw(LINES - 1, 0, "%s (y/n)", buf);
    clrtoeol();
    refresh();
    int ch = getch();
    return ch == 'y' || ch == 'Y';
}

/* Buying gear overwrites the slot outright, and dungeon set pieces are far
   stronger than anything the shop stocks -- so a mis-keyed purchase can
   destroy the best item in the run, and break a set bonus along with it.
   Only interrupts when there's something real to lose. */
static bool confirm_gear_replacement(const char *slot_label,
                                     const char *new_name, int new_bonus,
                                     int cur_bonus, int cur_set) {
    if (cur_set >= 0) {
        return confirm_prompt("The %s is a set piece (+%d). Replace it with %s (+%d)?",
                              slot_label, cur_bonus, new_name, new_bonus);
    }
    if (new_bonus <= cur_bonus) {
        return confirm_prompt("%s (+%d) is no better than what you carry (+%d). Buy it anyway?",
                              new_name, new_bonus, cur_bonus);
    }
    return true;
}

static bool key_to_delta(int ch, int *dx, int *dy) {
    switch (ch) {
        case 'h': case KEY_LEFT:  *dx = -1; *dy =  0; return true;
        case 'l': case KEY_RIGHT: *dx =  1; *dy =  0; return true;
        case 'k': case KEY_UP:    *dx =  0; *dy = -1; return true;
        case 'j': case KEY_DOWN:  *dx =  0; *dy =  1; return true;
        case 'y': *dx = -1; *dy = -1; return true;
        case 'u': *dx =  1; *dy = -1; return true;
        case 'b': *dx = -1; *dy =  1; return true;
        case 'n': *dx =  1; *dy =  1; return true;
        default: return false;
    }
}

static MapFeature *feature_at(Map *m, int x, int y) {
    for (int i = 0; i < m->feature_count; i++) {
        if (m->features[i].x == x && m->features[i].y == y) return &m->features[i];
    }
    return NULL;
}

static void log_floor_event(const Map *m) {
    if (m->event_name[0]) {
        log_msg("[Floor event] %s -- %s", m->event_name, m->event_desc);
    }
}

/* Called just before a temple floor is generated, so the monsters that floor
   creates already carry the run's accumulated escalation. */
static void enter_floor(Player *p) {
    dda_floor_begin(p);
    p->floor_entries++;
    /* The Bazaar re-prices between trips, and a descent is what makes a trip.
       Advanced here rather than on arrival in town so that walking in and out
       of the plaza cannot re-roll the stalls -- a shop you can reload by
       stepping through a door twice is not a shop that makes timing matter. */
    p->bazaar_day++;
    quest_check_arrival(p);
    /* A new floor is new ground: whatever district you were standing in on
       the last one must not suppress the first arrival on this one. */
    p->last_district = -1;
    /* Plus whatever boredom has earned. Zero unless the dungeon master is
       switched on and the run has been coasting -- see dda.h. */
    /* Both of the things a floor's monsters are made from: how escalated they
       are, and whether this mode builds them out of horde. */
    monsters_set_difficulty(p->difficulty);
    monsters_set_escalation(monsters_escalation_for(hero_driven(p)->level, p->floor_entries)
                            + avenger_escalation(p));
}

static void log_wilds(const Map *m);

static void log_overrun_warning(const Map *m) {
    if (m->overrun) {
        log_msg("This floor is OVERRUN -- far more of them than there should be,");
        log_msg("and they keep coming. Fight it or find the stairs and go.");
    }
    if (m->gold_rush) {
        log_msg("A GOLD RUSH floor -- the Company left a payroll down here and");
        log_msg("never came back for it. Same monsters. Twenty times the coin.");
    }
}

/* Said on arrival, next to the overrun and gold-rush warnings. A floor with
   a bog on it should announce itself; walking into one unannounced reads as
   the map being broken rather than the floor being unusual. */
static void log_wilds(const Map *m) {
    for (int k = 0; k < WILD_KIND_MAX; k++) {
        if (!(m->wild_kinds & (1u << (unsigned)k))) continue;
        const char *note = wild_kind_note(k);
        if (note) log_msg("%s", note);
    }
    if (m->haven_w > 0) {
        log_msg("Somebody holds a camp down here. Look for the palisade.");
    }
}

static void log_gate_hint(int floor_num) {
    if (floor_num % waygate_interval() == 0 && floor_num < MAX_FLOOR) {
        log_msg("A stable waygate hums somewhere on this floor -- free passage back to town.");
    }
    if (is_biome_boss_floor(floor_num)) {
        log_msg("Something far stronger than the rest is waiting on this floor. Be ready.");
    }
}

/* What an actor is called in the log. The one holding the human is "you";
   everybody else is named. This is the only place the distinction is made in
   prose, so that no message has to decide it for itself. */
static const char *actor_name(const Player *p, const Hero *h) {
    if (h == hero_driven_c(p)) return "You";
    return h->name[0] ? h->name : "Someone";
}

static int apply_resist(const Hero *h, int dmg) {
    dmg -= dmg * (h->hazard_resist_pct + h->set_bonus_hazard_resist) / 100;
    return dmg < 0 ? 0 : dmg;
}

/* A snare costs you the step you took into it. Damage once, then held --
   the hold is the real price, because being unable to move for two turns
   with something walking toward you is worse than the bite was. */
static void apply_snare(Player *p, Hero *h, const Map *m, int x, int y) {
    if (m->tiles[y][x].type != TILE_SNARE) return;

    bool sand = district_at(m, x, y) == DIST_QUICKSAND;
    int dmg  = sand ? SNARE_SAND_DAMAGE : SNARE_GARDEN_DAMAGE;
    int hold = sand ? SNARE_SAND_HOLD   : SNARE_GARDEN_HOLD;

    dmg = apply_resist(h, dmg);
    if (dmg < 1) dmg = 1;
    h->hp -= dmg;
    if (h->hp < 0) h->hp = 0;
    if (h->stun_turns_left < hold) h->stun_turns_left = hold;

    if (h == hero_driven(p))
        log_msg(sand ? "The sand takes your legs -- %d damage, and you are not going anywhere for %d."
                     : "It snaps shut on you -- %d damage, and it does not let go for %d.",
                dmg, hold);
    else
        log_msg(sand ? "%s is caught in the sand -- %d damage, held for %d."
                     : "It snaps shut on %s -- %d damage, held for %d.",
                actor_name(p, h), dmg, hold);
}

static void apply_terrain_hazard(Player *p, Hero *h, TileType t) {
    int dmg = 0;
    const char *you = NULL, *them = NULL;
    /* The salt corrodes: on the Wastes the ground costs more than it looks.
       Added before resistance, so Fortitude still answers it. */
    int bite = biome_hazard_extra(p);
    if (t == TILE_LAVA) {
        dmg = apply_resist(h, 12 + rand() % 9 + bite);
        you = "The lava sears you for %d damage!";
        them = "The lava sears %s for %d.";
    } else if (t == TILE_MIASMA) {
        dmg = apply_resist(h, 2 + rand() % 4 + bite);
        you = "The miasma burns in your lungs for %d damage.";
        them = "The miasma burns in %s's lungs for %d.";
    }
    if (!you) return;

    h->hp -= dmg;
    if (h->hp < 0) h->hp = 0;
    if (h == hero_driven(p)) log_msg(you, dmg);
    else                     log_msg(them, actor_name(p, h), dmg);
}

static void apply_reveal_perks(Map *m, Player *p) {
    /* What the Oracle was paid for, spent here. Folded in with the two innate
       reveals rather than given its own hook, because "what does the player
       already know when they arrive" is one question with three answers, and
       three call sites was already one too many to keep in step. The body
       lives in common.c so the suite can drive it -- everything in this file
       is static and unreachable from tests/. */
    oracle_spend_reading(m, p);

    if (hero_driven_c(p)->scribing_reveal) {
        for (int y = 0; y < MAP_H; y++) {
            for (int x = 0; x < MAP_W; x++) {
                if (m->tiles[y][x].type != TILE_WALL) m->tiles[y][x].seen = true;
            }
        }
    }
    if (hero_driven_c(p)->aether_reveal) {
        if (m->has_portals) {
            m->tiles[m->portal_ay][m->portal_ax].seen = true;
            m->tiles[m->portal_by][m->portal_bx].seen = true;
        }
        for (int i = 0; i < m->feature_count; i++) {
            if (m->features[i].type == FEATURE_RELIC) {
                m->tiles[m->features[i].y][m->features[i].x].seen = true;
            }
        }
    }
}

/* Information as a purchasable good -- the one thing this economy did not
   sell. What is bought here is spent on arrival, in enter_floor's wake, and a
   second reading replaces the first rather than stacking: three readings held
   at once would be a way to buy the whole descent in one visit, and the point
   of the shop is that you choose *when* the answer is worth it. */
static void run_oracle(Player *p) {
    log_reset();
    for (;;) {
        draw_oracle(p, hero_driven_c(p));
        int ch = getch();
        if (ch == 27 || ch == 'q') break;

        int price = 0, floors = 0;
        bool full = false;
        if      (ch == 'a') { price = ORACLE_STAIRS_PRICE; floors = 1; full = false; }
        else if (ch == 'b') { price = ORACLE_FLOOR_PRICE;  floors = 1; full = true;  }
        else if (ch == 'c') { price = ORACLE_LONG_PRICE;   floors = ORACLE_LONG_FLOORS; full = true; }
        else continue;

        price = discounted_price(p, price);
        if (p->gold < price) {
            log_msg("\"Coin first.\" She wants %d; you have %d.", price, p->gold);
            continue;
        }
        p->gold -= price;
        p->oracle_floors = floors;
        p->oracle_full = full;
        log_msg(full
                ? "She describes the ground below in detail you did not ask for. (%d floor%s)"
                : "\"Down, and down again, and then left.\" You will know the stairs. (%d floor%s)",
                floors, floors == 1 ? "" : "s");
    }
}

/* The reallocation sink. Two stages rather than two cursors on one screen,
   because the second choice depends on the first -- you cannot know what you
   can afford to take until you have said what you are giving up. */
static void run_altar(Player *p) {
    log_reset();
    int stage = 0, give = 0, take = 0;
    for (;;) {
        draw_altar(p, hero_driven_c(p), stage, give, take);
        int ch = getch();
        if (ch == 27 || ch == 'q') {
            if (stage == 0) break;
            stage = 0;
            continue;
        }
        if (ch == '\n' || ch == '\r' || ch == KEY_ENTER) {
            Hero *h = hero_driven(p);
            if (stage == 0) {
                if (h->attrs[give] <= ALTAR_MIN_ATTR) {
                    log_msg("\"There is nothing there to take.\" %s is already %d.",
                            ATTR_NAMES[give], h->attrs[give]);
                    continue;
                }
                take = give;            /* start the second choice somewhere sane */
                stage = 1;
                continue;
            }
            if (take == give) {
                log_msg("\"That is not a trade.\"");
                continue;
            }
            int price = discounted_price(p, training_price(h->attrs[take])
                                            * ALTAR_PRICE_NUM / ALTAR_PRICE_DEN);
            if (p->gold < price) {
                log_msg("The fee is %d. You have %d.", price, p->gold);
                continue;
            }
            if (!trade_attribute(h, give, take)) {
                log_msg("\"Not that one.\"");
                continue;
            }
            p->gold -= price;
            log_msg("%s for %s. It is done, and it cost %d.",
                    ATTR_NAMES[give], ATTR_NAMES[take], price);
            stage = 0;
            continue;
        }
        int *cur = (stage == 0) ? &give : &take;
        switch (ch) {
            case KEY_UP: case 'k':    if (*cur > 0) (*cur)--; break;
            case KEY_DOWN: case 'j':  if (*cur < ATTR_COUNT - 1) (*cur)++; break;
            case KEY_LEFT: case 'h':  *cur -= 10; if (*cur < 0) *cur = 0; break;
            case KEY_RIGHT: case 'l': *cur += 10; if (*cur > ATTR_COUNT - 1) *cur = ATTR_COUNT - 1; break;
            default: break;
        }
    }
}

/* The same bottles the Apothecary sells, at a price that moves between trips.
   The percentage comes from render.c's bazaar_price_pct() -- the same call the
   screen makes -- so what is quoted is what is charged. */
static void run_bazaar(Player *p) {
    log_reset();
    for (;;) {
        draw_bazaar(p, hero_driven_c(p));
        int ch = getch();
        if (ch == 27 || ch == 'q') break;
        if (ch < 'a' || ch >= 'a' + APOTHECARY_STOCK_COUNT) continue;

        int i = ch - 'a';
        const ConsumableTemplate *t = &APOTHECARY_STOCK[i];
        int price = discounted_price(p, t->price * bazaar_price_pct(p, i) / 100);
        if (p->gold < price) {
            log_msg("The stallholder shrugs. %d for the %s, and you have %d.",
                    price, t->name, p->gold);
        } else if (!give_consumable(p, t, 1)) {
            log_msg("Your pack is full.");
        } else {
            p->gold -= price;
            log_msg("You buy a %s for %d.", t->name, price);
        }
    }
}

static void run_gladiator_school(Player *p) {
    /* A fresh log. These screens draw the message log under their content,
       and without this the first thing under the Bank's ledger is whatever
       was said out in the plaza -- which reads as the screen having failed
       to clear rather than as an old message. Only what happens *in* here
       belongs under here. */
    log_reset();
    int cursor = 0;
    for (;;) {
        draw_gladiator_school(p, hero_driven_c(p), cursor);
        int ch = getch();
        if (ch == 27 || ch == 'q') break;
        if (ch == 'w' || ch == 'W') {
            /* A writ pays the fee outright. It does not skip the School --
               that is the whole point of it being a writ. */
            if (p->training_writs <= 0) {
                log_msg("\"A writ, or coin. Those are the terms.\"");
            } else {
                p->training_writs--;
                train_attribute(hero_driven(p), cursor);
                log_msg("The writ is stamped and filed. %s is %d now. (%d left)",
                        ATTR_NAMES[cursor], hero_driven(p)->attrs[cursor], p->training_writs);
            }
            continue;
        }
        if (ch == '\n' || ch == '\r' || ch == KEY_ENTER) {
            int price = discounted_price(p, training_price(hero_driven(p)->attrs[cursor]));
            if (p->gold < price) {
                log_msg("They want %d for that. You have %d.", price, p->gold);
            } else {
                p->gold -= price;
                train_attribute(hero_driven(p), cursor);
                log_msg("Weeks of it. %s is %d now.", ATTR_NAMES[cursor], hero_driven(p)->attrs[cursor]);
            }
            continue;
        }
        switch (ch) {
            case KEY_UP: case 'k': if (cursor > 0) cursor--; break;
            case KEY_DOWN: case 'j': if (cursor < ATTR_COUNT - 1) cursor++; break;
            case KEY_NPAGE: cursor += 10; if (cursor > ATTR_COUNT - 1) cursor = ATTR_COUNT - 1; break;
            case KEY_PPAGE: cursor -= 10; if (cursor < 0) cursor = 0; break;
            default: break;
        }
    }
}

static bool gold_boon_active(const Player *p);

/* Paid on completion, never in instalments -- being driven off a job three
   turns from the end has to cost you the job. */
static void work_finish(Player *p, Map *m) {
    int kind = hero_driven(p)->work_kind;
    int wx = hero_driven(p)->work_x, wy = hero_driven(p)->work_y;
    hero_driven(p)->work_turns_left = 0;
    hero_driven(p)->work_kind = WORK_NONE;

    if (wx < 0 || wx >= MAP_W || wy < 0 || wy >= MAP_H) return;

    if (kind == WORK_HARVEST_ROD || kind == WORK_HARVEST_VENT) {
        /* The rod is consumed: it stops being a lightning target too, which
           is a second reason to clear them and a reason the storm-cage gets
           safer as you work it. The vent field works the same way, and the
           cancel below is what makes that true for both -- it matches on the
           square, not on the kind. */
        m->tiles[wy][wx].type = TILE_FLOOR;
        if (m->storm_x == wx && m->storm_y == wy) { m->storm_x = m->storm_y = -1; }
        int paid = player_gain_gold(p, 60 + m->floor_num * 12);
        log_msg(kind == WORK_HARVEST_VENT
                ? "The crust comes away in sheets. (+%d gold)"
                : "The ore comes free. (+%d gold)", paid);
    } else if (kind == WORK_MINE) {
        m->tiles[wy][wx].type = TILE_WALL;
        int tier = material_tier_for_floor(m->floor_num);
        /* A vein is the deliberate way to get materials -- you stop, you commit
           three turns, and you are exposed for all of them. It paid 2-4, which
           against an upgrade wanting several of them made mining a rounding
           error next to just walking. Doubled, because the thing you went out
           of your way for should be the thing that pays. */
        int got = 4 + rand() % 5;
        p->mat_count[tier] += got;
        p->mat_value[tier] += junk_value_for_floor(m->floor_num) * got;
        log_msg("The vein gives up %d %s.", got, material_name(tier));
    } else if (kind == WORK_SALVAGE) {
        m->tiles[wy][wx].type = TILE_FLOOR;
        int tier = material_tier_for_floor(m->floor_num);
        int got = 6;
        p->mat_count[tier] += got;
        p->mat_value[tier] += junk_value_for_floor(m->floor_num) * got * (gold_boon_active(p) ? 2 : 1);
        int paid = player_gain_gold(p, 40 + m->floor_num * 8);
        log_msg("You strip the wreck out: %d %s, and %d gold.", got, material_name(tier), paid);
    } else if (kind == WORK_CORE) {
        /* Ten turns inside a dead golem, and the longest commitment in the
           game pays like it. The core is the deep material at whatever tier
           the floor is on, which makes the boneyard the one district you go
           looking for when the smith is asking for something you do not have. */
        m->tiles[wy][wx].type = TILE_FLOOR;
        int tier = material_tier_for_floor(m->floor_num);
        int got = 10 + rand() % 7;
        p->mat_count[tier] += got;
        p->mat_value[tier] += junk_value_for_floor(m->floor_num) * got * (gold_boon_active(p) ? 2 : 1);
        int paid = player_gain_gold(p, 90 + m->floor_num * 14);
        log_msg("The core comes out whole: %d %s, and %d gold.", got, material_name(tier), paid);
    }
}

static void work_abandon(Player *p, const char *why) {
    if (hero_driven(p)->work_turns_left <= 0) return;
    hero_driven(p)->work_turns_left = 0;
    hero_driven(p)->work_kind = WORK_NONE;
    log_msg("%s", why);
}

/* Start a job on the tile you are standing on, or any of the eight around
   you. Nothing to do is a message rather than a wasted turn. */
static void work_begin(Player *p, const Map *m) {
    for (int d = -1; d < 8; d++) {
        int x = hero_driven(p)->x + (d < 0 ? 0 : PATH_DX[d]);
        int y = hero_driven(p)->y + (d < 0 ? 0 : PATH_DY[d]);
        int kind = work_available_at(m, x, y);
        if (kind == WORK_NONE) continue;

        hero_driven(p)->work_kind = kind;
        hero_driven(p)->work_turns_left = work_turns_for(kind);
        hero_driven(p)->work_x = x;
        hero_driven(p)->work_y = y;
        log_msg("You set to %s -- %d turns, and you cannot answer for any of them.",
                work_verb(kind), hero_driven(p)->work_turns_left);
        return;
    }
    log_msg("Nothing here worth the time.");
}

/* The lizard track.
 *
   This exists to break the loop the rest of the economy is caught in: gold
   from the dungeon comes with experience, experience raises escalation, and
   escalation raises the gold you need. Money won in town carries no
   experience at all, so it is the one income that does not make the game
   harder by being earned.
 *
   Which is exactly why it has to be bounded. The book pays over the odds for
   the first few races of a visit and under them afterwards, so a visit is
   worth a stake and not a fortune, and the only way to reset him is to go
   back down. Town income is gated on *trips*, not on time. */
static void run_races(Player *p) {
    log_reset();

    static const char *NAMES[RACE_RUNNERS] = {
        "Ashken's Ruin", "Tin Whistle", "The Baroness",
        "Sixpenny Grin", "Marrow", "Quick Lily"
    };

    int cursor = 0, stake = 50;
    for (;;) {
        /* Odds are redrawn each visit to the screen so the souring is
           visible rather than something you infer from losing. */
        int odds[RACE_RUNNERS];
        race_odds(p->races_this_visit, odds);

        draw_races(p, odds, NAMES, cursor, stake);
        int ch = getch();
        if (ch == 27 || ch == 'q') break;

        if (ch == KEY_UP   || ch == 'k') { if (cursor > 0) cursor--; continue; }
        if (ch == KEY_DOWN || ch == 'j') { if (cursor < RACE_RUNNERS - 1) cursor++; continue; }
        /* Two axes on the stake: fifty at a time on Left/Right, ten times
           the stake on +/-. Reaching a 5,000 bet fifty at a time is ninety
           nine keypresses. Not Up/Down -- those pick the runner. */
        if (ch == KEY_LEFT || ch == 'h') { stake = stake > 50 ? stake - 50 : 10; continue; }
        if (ch == KEY_RIGHT|| ch == 'l') { if (stake < RACE_STAKE_MAX) stake += 50; continue; }
        if (ch == '+' || ch == '=') {
            stake *= 10;
            if (stake > RACE_STAKE_MAX) stake = RACE_STAKE_MAX;
            continue;
        }
        if (ch == '-' || ch == '_') {
            stake /= 10;
            if (stake < 10) stake = 10;
            continue;
        }

        bool bet_and_watch = (ch == 'w' || ch == 'W');
        if (ch == '\n' || ch == '\r' || ch == KEY_ENTER || bet_and_watch) {
            if (p->gold < stake) {
                log_msg("\"Coin first,\" says the bookmaker, without looking up.");
                continue;
            }
            p->gold -= stake;
            int winner = race_run(NAMES, odds, bet_and_watch);
            p->races_this_visit++;

            if (winner == cursor) {
                /* Credited flat: this is stake plus winnings, and the stake
                   was already the player's money. Running it through the
                   income funnel multiplied it by the bank share, which turned
                   a book paying 0.82 per gold staked into one paying 1.64 --
                   an unbounded printer, found in play at 2,805 races. */
                int paid = player_credit_gold(p, (long)stake * (odds[cursor] + 1));
                log_msg("%s comes in. The bookmaker counts out %d.", NAMES[winner], paid);
            } else {
                log_msg("%s takes it. Your %d stays on the table.", NAMES[winner], stake);
            }
            if (p->races_this_visit == RACE_GENEROUS_RACES)
                log_msg("The bookmaker shortens his prices. \"You've had the good of me.\"");
            continue;
        }
    }
}

/* A service night. Seven covers, one at a time, and the only resource is
   what you carried up. Unlike the track there is no stake and nothing to
   lose -- the worst night still pays something -- but a night run badly
   costs reputation, which is the thing that compounds. */
static void run_kitchen(Player *p) {
    log_reset();

    if (p->kitchen_rep <= 0 && p->kitchen_earned == 0)
        p->kitchen_rep = KITCHEN_REP_START;

    /* Two different ways to be turned round at the door, and they want
       different words -- "you have nothing to cook" and "you already cooked
       tonight" are not the same problem, and telling the player the wrong
       one sends them back down for meat they already have. Logged before
       the first draw, or the line lands a frame too late to be read. */
    if (meat_total(p) == 0) {
        log_msg("\"Come back with something,\" the cook says, \"and I'll cook it.\"");
        for (;;) {
            draw_kitchen(p, NULL, 0, 0, 0, 0);
            int ch = getch();
            if (ch == 27 || ch == 'q') return;
        }
    }

    if (p->kitchen_nights >= KITCHEN_NIGHTS_PER_VISIT) {
        log_msg("The chairs are up. You cooked tonight; there isn't another one.");
        for (;;) {
            draw_kitchen(p, NULL, 0, 0, 0, 0);
            int ch = getch();
            if (ch == 27 || ch == 'q') return;
        }
    }

    Patron patrons[KITCHEN_PATRONS];
    kitchen_roll_patrons(p, patrons, KITCHEN_PATRONS);

    int cursor = 0;
    long taken = 0;
    int served = 0;
    bool walked_out = false;

    for (int i = 0; i < KITCHEN_PATRONS && !walked_out; i++) {
        /* Nobody left to cook for ends the night early -- an empty larder is
           not a menu you should have to page through seven times. */
        if (meat_total(p) == 0) {
            log_msg("The larder is bare. The cook calls last orders.");
            break;
        }

        Patron *pat = &patrons[i];
        bool done = false;

        while (!done) {
            draw_kitchen(p, pat, served, KITCHEN_PATRONS, cursor, taken);
            int ch = getch();

            if (ch == 27 || ch == 'q') { walked_out = true; break; }

            if (ch == KEY_UP   || ch == 'k') { if (cursor > 0) cursor--; continue; }
            if (ch == KEY_DOWN || ch == 'j') { if (cursor < MEAT_GRADE_COUNT - 1) cursor++; continue; }

            if (ch == 'x' || ch == 'X') {
                p->kitchen_rep += kitchen_rep_delta(pat, -1);
                log_msg("%s is turned away, and says so on the way out.", pat->name);
                served++;
                done = true;
                continue;
            }

            if (ch == '\n' || ch == '\r' || ch == KEY_ENTER) {
                if (p->meat[cursor] <= 0) {
                    log_msg("There is no %s left.", meat_name(cursor));
                    continue;
                }
                p->meat[cursor]--;

                int pay = kitchen_payout(p, pat, cursor);
                int rep = kitchen_rep_delta(pat, cursor);

                /* Kitchen money is income like any other -- the bank's share
                   applies here too, same as the track and the junkyard. */
                int paid = player_gain_gold(p, pay);
                taken += paid;
                p->kitchen_earned += paid;
                p->kitchen_rep += rep;

                if (rep > 0)
                    log_msg("%s eats well. (+%d gold, rep %+d)", pat->name, paid, rep);
                else
                    log_msg("%s pushes the plate back. (+%d gold, rep %+d)", pat->name, paid, rep);

                served++;
                done = true;
                continue;
            }
        }

        if (p->kitchen_rep < 0) p->kitchen_rep = 0;
        if (p->kitchen_rep > KITCHEN_REP_MAX) p->kitchen_rep = KITCHEN_REP_MAX;
    }

    /* The night is spent only if you actually cooked. Charging for it at the
       door meant opening the pass to read the room and backing out cost you
       the whole service -- which reads as a bug even when it is deliberate. */
    if (served == 0) {
        /* Walked straight back out. There is nothing to summarise, and
           showing the end-of-night screen here made leaving take two
           keypresses and then told you the room was empty when you were the
           one who left. */
        log_msg("You look at the room, and leave the knife where it is.");
        return;
    }

    p->kitchen_nights++;
    log_msg("Service is over. %ld gold on the night, and the house stands at %d.",
            taken, p->kitchen_rep);

    for (;;) {
        draw_kitchen(p, NULL, served, KITCHEN_PATRONS, 0, taken);
        int ch = getch();
        if (ch == 27 || ch == 'q') return;
    }
}

/* Sells levels. No limit, no cooldown, no floor tied to how deep you have
   been -- selling down and re-earning the cheap levels somewhere safe is a
   supported way to play, not an exploit that slipped through. The only
   refusal is at level 1, and that is arithmetic rather than balance. */
static void run_black_market(Player *p) {
    log_reset();
    int count = 1;

    for (;;) {
        int max = level_sale_max(hero_driven(p)->level);
        if (count > max) count = max;
        if (count < 1)   count = 1;

        draw_black_market(p, hero_driven_c(p), count);
        int ch = getch();
        if (ch == 27 || ch == 'q') break;

        /* All four arrows steer the one number: horizontal fine, vertical
           coarse. There is only one axis on this screen, so nothing is lost
           by spending Up/Down on it -- and a laptop keyboard has no PgUp or
           PgDn without a modifier. Those are kept as aliases for keyboards
           that do have them. */
        if (ch == KEY_LEFT  || ch == 'h') { if (count > 1)   count--; continue; }
        if (ch == KEY_RIGHT || ch == 'l') { if (count < max) count++; continue; }

        /* Snapped to tens rather than stepped by ten, so the coarse key lands
           on 10, 20, 30 from any starting point -- the player is steering by
           a round target level ("I am 70, I want to be 50"), and 1 -> 11 -> 21
           makes them do arithmetic to hit it. */
        if (ch == KEY_UP || ch == 'k' || ch == KEY_PPAGE) {
            count = (count / 10 + 1) * 10;
            if (count > max) count = max;
            continue;
        }
        if (ch == KEY_DOWN || ch == 'j' || ch == KEY_NPAGE) {
            count = (count % 10 == 0) ? count - 10 : (count / 10) * 10;
            if (count < 1) count = 1;
            continue;
        }
        if (ch == 'a' || ch == 'A')       { count = max > 0 ? max : 1;     continue; }

        if (ch == '\n' || ch == '\r' || ch == KEY_ENTER) {
            if (max <= 0) {
                log_msg("\"There's nothing under the first,\" he says. \"Go and earn some.\"");
                continue;
            }

            int  from  = hero_driven(p)->level;
            long price = level_sale_batch_price(hero_driven(p)->level, count, p->deepest_floor);

            /* Sold one at a time so the stat arithmetic stays in the single
               place that mirrors the level-up, rather than being duplicated
               here in a batched form that could drift from it. */
            int sold = 0;
            while (sold < count && player_sell_level(p)) sold++;

            if (sold <= 0) {
                log_msg("\"There's nothing under the first,\" he says. \"Go and earn some.\"");
                continue;
            }

            /* Paid through the same funnel as everything else, so the bank's
               share applies to it exactly like a kill or a bounty. */
            int paid = player_gain_gold(p, (int)(price > 2000000000L ? 2000000000L : price));
            p->levels_sold      += sold;
            p->levels_sold_gold += paid;

            log_msg("%d level%s across the counter. (+%d gold, %d -> %d)",
                    sold, sold == 1 ? "" : "s", paid, from, hero_driven(p)->level);

            count = 1;
            continue;
        }
    }
}

static void run_bank(Player *p) {
    log_reset();
    for (;;) {
        draw_bank(p, hero_driven_c(p));
        int ch = getch();
        if (ch == 27 || ch == 'q') break;
        if (ch == '\n' || ch == '\r' || ch == KEY_ENTER) {
            long price = bank_price(p->gold_mult);
            if (price < 0) {
                log_msg("\"There is nothing above a hundred shares. Go and spend it.\"");
                continue;
            }
            /* The clerk's discount comes off like anywhere else, and the
               price can exceed what an int holds, so this stays long. */
            long paid = price - price * hero_driven(p)->shop_discount_pct / 100;
            if (paid < 0) paid = 0;
            if (p->gold < paid) {
                log_msg("The clerk closes the ledger. \"%ld. You have %d.\"", paid, p->gold);
                continue;
            }
            p->gold -= (int)paid;
            p->gold_mult++;
            log_msg("Signed and stamped. Every coin you earn is worth x%d now.", p->gold_mult);
            continue;
        }
    }
}

static void run_tavern(Player *p) {
    log_reset();
    int cursor = 0;
    for (;;) {
        draw_tavern(p, cursor);
        int ch = getch();
        if (ch == 27 || ch == 'q') break;
        if (ch == '\n' || ch == '\r' || ch == KEY_ENTER) { companion_hire(p, cursor); continue; }
        if (ch == 'r' || ch == 'R') {
            /* Confirm: this is permanent and there is no refund, and at the
               top tiers the mistake costs ten million to undo. */
            if (tavern_can_release(p, cursor)) {
                Hero c;
                tavern_candidate(p, cursor, &c);
                if (confirm_prompt("Let %s go? No refund, and they don't come back.", c.name))
                    companion_release(p, cursor);
            } else {
                companion_release(p, cursor);   /* logs why it can't be done */
            }
            continue;
        }
        switch (ch) {
            case KEY_UP: case 'k': if (cursor > 0) cursor--; break;
            case KEY_DOWN: case 'j': if (cursor < TAVERN_ROSTER - 1) cursor++; break;
            default: break;
        }
    }
}

static void run_shop_general(Player *p) {
    log_reset();
    for (;;) {
        draw_shop_general(p, hero_driven_c(p));
        int ch = getch();
        if (ch == 27 || ch == 'q') break;
        if (ch >= 'a' && ch < 'a' + GENERAL_STOCK_COUNT) {
            const ConsumableTemplate *t = &GENERAL_STOCK[ch - 'a'];
            int price = discounted_price(p, t->price);
            if (p->gold < price) {
                log_msg("Not enough gold for the %s.", t->name);
            } else if (!give_consumable(p, t, 1)) {
                log_msg("Your pack is full.");
            } else {
                p->gold -= price;
                log_msg("You buy a %s.", t->name);
            }
        }
    }
}

static void run_shop_apothecary(Player *p) {
    log_reset();
    for (;;) {
        draw_shop_apothecary(p, hero_driven_c(p));
        int ch = getch();
        if (ch == 27 || ch == 'q') break;
        if (ch == (char)('a' + APOTHECARY_STOCK_COUNT)) {
            /* The one thing here that is not a bottle: a permanent lift to
               the bar itself. HP is the term that decides how many hits you
               survive, and levelling was the only thing feeding it. */
            int price = discounted_price(p, upgrade_price(hero_driven(p)->hp_plus));
            if (p->gold < price) {
                log_msg("The apothecary wants %d for that. You have %d.", price, p->gold);
            } else {
                p->gold -= price;
                hero_driven(p)->hp_plus++;
                hero_driven(p)->maxhp += UPGRADE_HP_STEP;
                hero_driven(p)->hp += UPGRADE_HP_STEP;
                log_msg("Something bitter, and your constitution takes it. (+%d max HP, now +%d)",
                        UPGRADE_HP_STEP, hero_driven(p)->hp_plus);
            }
            continue;
        }
        if (ch >= 'a' && ch < 'a' + APOTHECARY_STOCK_COUNT) {
            const ConsumableTemplate *t = &APOTHECARY_STOCK[ch - 'a'];
            int price = discounted_price(p, t->price);
            if (p->gold < price) {
                log_msg("Not enough gold for the %s.", t->name);
            } else if (!give_consumable(p, t, 1)) {
                log_msg("Your pack is full.");
            } else {
                p->gold -= price;
                log_msg("You buy a %s.", t->name);
            }
        }
    }
}

static void run_shop_ranged(Player *p) {
    log_reset();
    for (;;) {
        draw_shop_ranged(p, hero_driven_c(p));
        int ch = getch();
        if (ch == 27 || ch == 'q') break;
        if (ch == (char)('a' + RANGED_STOCK_COUNT)) {
            /* "Extra shot" -- a bigger magazine on whatever you carry, so a
               ranged build is not rationed to a couple of volleys a floor. */
            if (hero_driven(p)->ranged_type == RANGED_NONE) {
                log_msg("Buy something to load first.");
                continue;
            }
            int price = discounted_price(p, upgrade_price(hero_driven(p)->ammo_plus));
            if (p->gold < price) {
                log_msg("The gunsmith wants %d for that. You have %d.", price, p->gold);
            } else {
                p->gold -= price;
                hero_driven(p)->ammo_plus++;
                hero_driven(p)->ranged_ammo_max += UPGRADE_AMMO_STEP;
                hero_driven(p)->ranged_ammo = hero_driven(p)->ranged_ammo_max;
                log_msg("A deeper magazine. (+%d shots, now +%d)",
                        UPGRADE_AMMO_STEP, hero_driven(p)->ammo_plus);
            }
            continue;
        }
        if (ch < 'a' || ch >= 'a' + RANGED_STOCK_COUNT) continue;

        const RangedTemplate *t = &RANGED_STOCK[ch - 'a'];
        int price = discounted_price(p, t->price);
        if (p->gold < price) {
            log_msg("Not enough gold for the %s.", t->name);
        } else {
            p->gold -= price;
            int bonus = t->bonus + t->bonus * hero_driven(p)->gear_bonus_pct / 100;
            equip_ranged(hero_driven(p), t->name, t->type, bonus, t->ammo_cost, t->cooldown);
            /* The magazine work follows you to the new weapon: it is your
               bandolier, not the gun's. */
            hero_driven(p)->ranged_ammo_max += hero_driven(p)->ammo_plus * UPGRADE_AMMO_STEP;
            hero_driven(p)->ranged_ammo = hero_driven(p)->ranged_ammo_max;
            log_msg("You arm yourself with a %s.", t->name);
        }
    }
}

static void run_shop_armory(Player *p) {
    log_reset();
    /* Two more letters at the end: sharpen what you carry, reinforce what you
       wear. They are the only entries here that never run out. */
    int total = WEAPON_STOCK_COUNT + ARMOR_STOCK_COUNT + ACCESSORY_STOCK_COUNT + 2;
    for (;;) {
        draw_shop_armory(p, hero_driven_c(p));
        int ch = getch();
        if (ch == 27 || ch == 'q') break;
        if (ch == 'w') {
            run_shop_ranged(p);
            continue;
        }
        if (ch < 'a' || ch >= 'a' + total) continue;

        int idx = ch - 'a';
        if (idx < WEAPON_STOCK_COUNT) {
            const GearTemplate *g = &WEAPON_STOCK[idx];
            int price = discounted_price(p, g->price);
            if (p->gold < price) {
                log_msg("Not enough gold for the %s.", g->name);
            } else if (!confirm_gear_replacement("weapon you carry", g->name,
                                                 g->bonus, hero_driven(p)->weapon_bonus, hero_driven(p)->weapon_set)) {
                log_msg("You leave the %s on the rack.", g->name);
            } else {
                p->gold -= price;
                hero_driven(p)->weapon_bonus = g->bonus + g->bonus * hero_driven(p)->gear_bonus_pct / 100;
                hero_driven(p)->weapon_plus = 0;    /* the smith's work stayed with the old one */
                strncpy(hero_driven(p)->weapon_name, g->name, sizeof(hero_driven(p)->weapon_name) - 1);
                hero_driven(p)->weapon_name[sizeof(hero_driven(p)->weapon_name) - 1] = '\0';
                hero_driven(p)->weapon_set = -1;
                recompute_set_bonus(hero_driven(p));
                log_msg("You arm yourself with a %s.", g->name);
            }
        } else if (idx < WEAPON_STOCK_COUNT + ARMOR_STOCK_COUNT) {
            const GearTemplate *g = &ARMOR_STOCK[idx - WEAPON_STOCK_COUNT];
            int price = discounted_price(p, g->price);
            if (p->gold < price) {
                log_msg("Not enough gold for the %s.", g->name);
            } else if (!confirm_gear_replacement("armour you wear", g->name,
                                                 g->bonus, hero_driven(p)->armor_bonus, hero_driven(p)->armor_set)) {
                log_msg("You leave the %s on the rack.", g->name);
            } else {
                p->gold -= price;
                hero_driven(p)->armor_bonus = g->bonus + g->bonus * hero_driven(p)->gear_bonus_pct / 100;
                hero_driven(p)->armor_plus = 0;
                hero_driven(p)->armor_set = -1;
                recompute_set_bonus(hero_driven(p));
                strncpy(hero_driven(p)->armor_name, g->name, sizeof(hero_driven(p)->armor_name) - 1);
                hero_driven(p)->armor_name[sizeof(hero_driven(p)->armor_name) - 1] = '\0';
                log_msg("You don a %s.", g->name);
            }
        } else if (idx < WEAPON_STOCK_COUNT + ARMOR_STOCK_COUNT + ACCESSORY_STOCK_COUNT) {
            const AccessoryTemplate *a = &ACCESSORY_STOCK[idx - WEAPON_STOCK_COUNT - ARMOR_STOCK_COUNT];
            int price = discounted_price(p, a->price);
            if (p->gold < price) {
                log_msg("Not enough gold for the %s.", a->name);
            } else {
                p->gold -= price;
                equip_accessory(p, a->name, a->stat, a->bonus);
                log_msg("You fit the %s.", a->name);
            }
        } else {
            bool weapon = (idx == total - 2);
            int cur = weapon ? hero_driven(p)->weapon_plus : hero_driven(p)->armor_plus;
            int price = discounted_price(p, upgrade_price(cur));
            int mtier = upgrade_material_tier(cur);
            int mcost = upgrade_material_cost(cur);
            if (p->gold < price) {
                log_msg("The smith wants %d for that. You have %d.", price, p->gold);
            } else if (p->mat_count[mtier] < mcost) {
                log_msg("\"Past this I need %d %s, and coin won't stand in for it.\"",
                        mcost, material_name(mtier));
                log_msg("You have %d. It comes up from the deep floors.", p->mat_count[mtier]);
            } else {
                p->gold -= price;
                p->mat_count[mtier] -= mcost;
                if (weapon) {
                    hero_driven(p)->weapon_plus++;
                    hero_driven(p)->weapon_bonus += UPGRADE_STEP;
                    log_msg("The smith works your %s up to +%d.", hero_driven(p)->weapon_name, hero_driven(p)->weapon_plus);
                } else {
                    hero_driven(p)->armor_plus++;
                    hero_driven(p)->armor_bonus += UPGRADE_STEP;
                    log_msg("The smith works your %s up to +%d.", hero_driven(p)->armor_name, hero_driven(p)->armor_plus);
                }
                if (companion_count(p) > 0)
                    log_msg("Your hired hands re-fit their own kit to match.");
            }
        }
    }
}

/* The records screen. A read-only page, so the loop is the whole of it. */
static void run_records(Player *p) {
    for (;;) {
        draw_records_screen(p, hero_driven_c(p));
        int ch = getch();
        if (ch == 27 || ch == 'q' || ch == 'R') break;
    }
}

static void run_quest_board(Player *p) {
    log_reset();
    for (;;) {
        if (p->quest_active && p->quest_type == QUEST_FETCH && p->quest_item_found) {
            int gold = player_gain_gold(p, p->quest_gold_reward);
            grant_xp(p, hero_driven(p), p->quest_xp_reward);
            log_msg("Bounty complete! You hand over %s. (+%d gold, +%d xp)", p->quest_monster, gold, p->quest_xp_reward);
            p->quest_active = false;
        }

        draw_quest_board(p, hero_driven_c(p));
        int ch = getch();
        if (ch == 27 || ch == 'q') break;
        if (ch == 'a') {
            if (p->quest_active) {
                log_msg("You tear down the notice: %s.", quest_summary(p));
                quest_abandon(p);
            } else {
                quest_offer(p);
            }
        }
    }
}

#define SPELL_PAGE_SIZE 18 /* keep in sync with render.c's SPELL_PAGE_SIZE */

static void run_shop_arcanist(Player *p) {
    log_reset();
    int cursor = 0;
    /* Browsing is per-school now, so `cursor` walks the caster's own school
       and spell_school_index turns it back into the pool index that gets
       stored in known_spells. */
    int total = spell_school_count(hero_driven(p)->magic_school);
    for (;;) {
        if (cursor < 0) cursor = 0;
        draw_shop_arcanist(p, hero_driven_c(p), cursor);
        int ch = getch();
        if (ch == 27 || ch == 'q') break;
        switch (ch) {
            case KEY_UP: case 'k': if (cursor > 0) cursor--; break;
            case KEY_DOWN: case 'j': if (cursor < total - 1) cursor++; break;
            case KEY_NPAGE: cursor += SPELL_PAGE_SIZE; if (cursor > total - 1) cursor = total - 1; break;
            case KEY_PPAGE: cursor -= SPELL_PAGE_SIZE; if (cursor < 0) cursor = 0; break;
            case 'A': case 'a': {
                /* A deeper reservoir rather than another spell -- the guild
                   will widen the channel for anyone who can pay. */
                int price = discounted_price(p, upgrade_price(hero_driven(p)->aether_plus));
                if (p->gold < price) {
                    log_msg("The guild wants %d to widen your channel. You have %d.", price, p->gold);
                } else {
                    p->gold -= price;
                    hero_driven(p)->aether_plus++;
                    hero_driven(p)->aether_max += UPGRADE_AETHER_STEP;
                    hero_driven(p)->aether = hero_driven(p)->aether_max;
                    log_msg("They widen the channel. (+%d aether, now +%d)",
                            UPGRADE_AETHER_STEP, hero_driven(p)->aether_plus);
                }
                break;
            }
            case '\n': case '\r': case KEY_ENTER: {
                if (total <= 0) break;   /* nothing to learn */
                int idx = spell_school_index(hero_driven(p)->magic_school, cursor);
                const SpellTemplate *t = spell_pool_get(idx);
                if (!t) break;
                bool known = false;
                for (int k = 0; k < hero_driven(p)->spell_count; k++) {
                    if (hero_driven(p)->known_spells[k] == idx) { known = true; break; }
                }
                if (known) {
                    log_msg("You already know %s.", t->name);
                } else if (!player_can_learn(hero_driven(p), idx)) {
                    /* Unreachable through this screen, which only lists the
                       caster's own school -- kept because player_can_learn is
                       the rule, not the list. */
                    log_msg("%s is locked to %s casters.", t->name, school_name(t->school));
                } else if (hero_driven(p)->spell_count >= MAX_KNOWN_SPELLS) {
                    log_msg("You can't hold any more spells in mind.");
                } else {
                    int price = discounted_price(p, t->learn_price);
                    if (p->gold < price) {
                        log_msg("Not enough gold to learn %s.", t->name);
                    } else {
                        p->gold -= price;
                        hero_driven(p)->known_spells[hero_driven(p)->spell_count] = idx;
                        hero_driven(p)->spell_cd[hero_driven(p)->spell_count] = 0;
                        hero_driven(p)->spell_count++;
                        log_msg("You learn %s.", t->name);
                    }
                }
                break;
            }
            default: break;
        }
    }
}

static void run_shop_merchant(Player *p) {
    log_reset();
    for (;;) {
        draw_shop_merchant(p, hero_driven_c(p));
        int ch = getch();
        if (ch == 27 || ch == 'q') break;
        if (ch >= 'a' && ch < 'a' + MERCHANT_STOCK_COUNT) {
            const ConsumableTemplate *t = &MERCHANT_STOCK[ch - 'a'];
            int price = discounted_price(p, t->price);
            if (p->gold < price) {
                log_msg("Not enough gold for the %s.", t->name);
            } else if (!give_consumable(p, t, 1)) {
                log_msg("Your pack is full.");
            } else {
                p->gold -= price;
                log_msg("You buy a %s from the trader.", t->name);
            }
        }
    }
}

/* Returns how many items were drunk, not merely whether any were.
 *
   The screen used to close on the first use, so drinking five potions meant
   opening the pack five times. It stays open now -- but each draught still
   has to cost its own turn, or standing in the inventory would be free
   healing. Hence a count rather than a flag: the caller resolves one turn per
   item, exactly as it would have if the player had reopened the pack. */
static int run_inventory(Player *p) {
    int used = 0;
    int page = 0;
    for (;;) {
        int pages = (p->inv_count + INV_PAGE - 1) / INV_PAGE;
        if (pages < 1) pages = 1;
        if (page >= pages) page = pages - 1;
        if (page < 0) page = 0;

        draw_inventory_screen(p, hero_driven_c(p), page);
        int ch = getch();
        if (ch == 27 || ch == 'q') break;
        if (ch == KEY_LEFT)  { if (page > 0) page--; continue; }
        if (ch == KEY_RIGHT) { if (page < pages - 1) page++; continue; }
        /* Letters address the page, not the pack: 'a' is always the first
           row on screen. */
        int on_page = p->inv_count - page * INV_PAGE;
        if (on_page > INV_PAGE) on_page = INV_PAGE;
        /* An upper-case letter drinks ten.
         *
           A pack in this game reaches four figures of a single item -- the
           Elixir of Vigor is bought by the hundred, being the only permanent
           stat you can just keep purchasing -- and one keypress per bottle
           makes that unusable. Reported from play as the Elixir not working:
           the effect is fine and was measured to be fine, but a player holding
           the key down and watching the bar not move has been told the same
           thing as a player whose potion is broken. Ten at a time is the
           difference between a stat you can buy and a stat you can only
           theoretically buy.

           Ten rather than the whole stack because most of the pack is healing,
           and "use all" on a healing draught at full health is a way to lose a
           thousand potions to one keypress. */
        bool bulk = (ch >= 'A' && ch < 'A' + on_page);
        if (bulk) ch = (char)(ch - 'A' + 'a');
        if (ch >= 'a' && ch < 'a' + on_page) {
            int idx = page * INV_PAGE + (ch - 'a');
            if (p->inventory[idx].is_recall) {
                log_msg("Press 'r' in the dungeon to channel a recall charm.");
                continue;
            }
            int want = bulk ? INV_BULK_USE : 1;
            for (int n = 0; n < want; n++) {
                /* The stack can empty under us, and using the last one shuffles
                   the array down -- so re-check rather than trusting `idx`. */
                if (idx >= p->inv_count) break;
                int before = p->inventory[idx].count;
                use_inventory_item(p, idx);
                used++;
                if (idx >= p->inv_count) break;            /* stack gone */
                if (p->inventory[idx].count >= before) break;  /* nothing spent */
            }
            /* Stay on the screen. The one thing that cannot is a charm that
               moves you somewhere else, and that is refused above. */
        }
    }
    return used;
}

/* One lighting pass for the whole party. The player's own field of view
   first (which clears last frame's), then every living companion's on top --
   they are people standing in the dungeon, not drones, so there is no fog
   around them, and whatever they can see the player can see. Every place
   that used to call compute_fov directly calls this instead, so a companion
   can never end up lit on one code path and dark on another. */
/* How far you can see from where you are standing. Everywhere but the mire
   this is just your Vision-derived radius; inside one, the mist wins
   regardless of how good your eyes are -- which is the point of it. */
/* How far this body can see from where this body is standing. Both halves
   used to read slot 0 -- so driving a hire took the character's Vision score
   and asked whether the *character* was standing in the mire. The comment
   that justified it ("a hire has no Vision attribute of their own -- the
   thirty are the MC's") stopped being true when hero_create() started
   building hires: every body has the thirty now, so every body sees with
   its own eyes. */
static int hero_fov_radius(const Player *p, const Hero *h, const Map *m) {
    int r = h->fov_radius;
    if (m->floor_num > 0 && district_at(m, h->x, h->y) == DIST_MIRE && r > MIRE_FOV_RADIUS)
        r = MIRE_FOV_RADIUS;

    /* The Abyss is dark. Aether-Sense is what answers it -- a body with the
       attribute keeps more of its sight, which is the point of the rule
       (ROADMAP Phase 3: "Abyss suppresses FOV, making Aether-Sense matter").
       Floored so the map stays navigable at 1400x800. */
    int blind = biome_sight_penalty(p);
    if (blind > 0) {
        if (h->aether_reveal) blind /= 2;
        r -= blind;
        if (r < BIOME_ABYSS_SIGHT_FLOOR) r = BIOME_ABYSS_SIGHT_FLOOR;
    }
    return r;
}

/* Every body looks with its own eyes, and the screen is drawn from the union.
 *
   This used to be compute_fov() around one point plus companions_reveal()
   bolting the hires' sight on afterwards -- one set of eyes belonging to
   nobody, which is the same premise that gave Player two meanings, one layer
   down. Each body keeps its own map now (see vision.h); this composites them
   into the Tile flags that the renderer and the searches read, so nothing
   downstream had to change. */
static void refresh_vision(Player *p, Map *m) {
    const Hero *eyes = hero_driven_c(p);
    hero_vision_update(m, p, hero_fov_radius(p, eyes, m));
}

/* An altar's bargain covers the ten floors after the one it was taken on.
   Checked at pickup rather than baked into the item, so a pile left behind
   and come back for is worth what it is worth *then*. */
static bool gold_boon_active(const Player *p) {
    return p->floor > 0 && p->floor <= p->gold_boon_until_floor;
}

static void pickup_items_at(Player *p, Map *m, int x, int y) {
    for (int i = 0; i < m->item_count; i++) {
        FloorItem *fi = &m->items[i];
        if (fi->used || fi->x != x || fi->y != y) continue;
        fi->used = true;
        if (fi->is_gold) {
            int amount = player_gain_gold(p, fi->gold_amount);
            log_msg("You find %d gold.", amount);
        } else if (fi->is_junk) {
            /* Silent on purpose: there are hundreds of these and a line each
               would bury the log. The sidebar carries the running total. */
            int tier = material_tier_for_floor(m->floor_num);
            p->mat_count[tier]++;
            p->mat_value[tier] += fi->junk_value * (gold_boon_active(p) ? 2 : 1);
        } else if (fi->is_key) {
            p->keys++;
            log_msg("You find a brass key.");
        } else if (fi->is_quest_item) {
            p->quest_item_found = true;
            log_msg("You recover %s! Bring it back to the quest board in town.", fi->name);
        } else {
            ConsumableTemplate t;
            memset(&t, 0, sizeof(t));
            strncpy(t.name, fi->name, sizeof(t.name) - 1);
            t.heal = fi->heal;
            t.atk_buff = fi->atk_buff;
            t.atk_buff_turns = fi->atk_buff_turns;
            t.def_buff = fi->def_buff;
            t.def_buff_turns = fi->def_buff_turns;
            t.is_recall = fi->is_recall;
            if (give_consumable(p, &t, 1)) {
                log_msg("You find a %s.", fi->name);
            } else {
                log_msg("You find a %s, but your pack is full.", fi->name);
            }
        }
    }
}

/* Say where you have just walked into.
 *
   A third of a floor is wild now, and the districts change the rules you are
   playing by -- the comb wakes together, the quiet quarter does not wake at
   all until you start something, the crystal field throws your own spells
   back. Walking into one silently meant finding that out by dying in it.
 *
   Tracked by district *index*, not kind: two separate bogs on one floor are
   two arrivals, and crossing between them should say so. */
static void announce_district(Player *p, const Map *m) {
    if (m->floor_num <= 0) return;

    int here = district_index_at(m, hero_driven(p)->x, hero_driven(p)->y);
    if (here == p->last_district) return;
    p->last_district = here;

    if (here < 0) return;                  /* stepping out is not an event */
    const char *name = wild_kind_name(m->districts[here].kind);
    if (name) log_msg("You cross into %s.", name);

    /* And the proving ground says which fight it is, every time. Its rule is
       the one district property a player must know *before* the first blow
       rather than after it -- finding out that your spells do not work here by
       casting one into a monster's face is the mechanic failing, not
       arriving. */
    if (m->districts[here].kind == DIST_PROVING)
        log_msg("The rule on this ground is %s.",
                proving_rule_name(m->districts[here].state));
}

static void resolve_after_player_turn(Player *p, Map *m, GameState *state) {
    int hp_before = hero_driven(p)->hp;
    announce_district(p, m);
    /* The party moves on the player's turn, before the monsters do -- they
       are allies, not a second wave of enemies. */
    /* Every actor the human is not attached to, in one loop. The character
       used to move here separately, ahead of the hires, "because they are
       still the one the party forms up on" -- nobody forms up on anybody now. */
    /* Time passes first, then everybody decides with the clock they actually
       have. This order is not arbitrary and it is not free to change: the
       hires' tick used to be the first statement of their AI loop, so a
       weapon with a cooldown of N fired every N turns. Ticking after the
       decision instead makes it every N+1 -- a quiet nerf to every ranged
       weapon in the game, arrived at by moving two lines. */
    party_tick(p, m);

    /* Somebody's blow ended the run while the world was moving. */
    if (companions_boss_felled()) { *state = STATE_WIN; return; }

    /* The character's six separate tick calls used to sit here, and the hires
       had their own version inline in their AI loop. Both are gone into
       hero_tick(), run once for each of the six actors -- including the one
       the human is attached to, because time passes for them too. */
    if (m->floor_num > 0) companions_take_turn(p, m);
    if (hero_driven(p)->hp <= 0) {
        if (!guardian_catch(p) && !companions_attempt_rescue(p, m)
            && !party_pass_the_torch(p)) { *state = STATE_GAMEOVER; return; }
        guardian_follow_through(p, m);
    }
    p->turns++;

    /* Scavenging as you go: a piece of scrap every hundred steps below
       ground. It is not loot you have to see and walk to -- it is what comes
       off the walls and out of the wreckage while you are down there, which
       is why it accrues with steps rather than with rooms cleared.

       Dungeon only. Pacing the plaza would mint money out of nothing. */
    if (m->floor_num > 0 && p->turns % JUNK_STEPS_PER_PIECE == 0) {
        int tier = material_tier_for_floor(m->floor_num);
        p->mat_count[tier]++;
        p->mat_value[tier] += junk_value_for_floor(m->floor_num) * (gold_boon_active(p) ? 2 : 1);
    }
    bool died = false;
    process_monster_turns(m, p, &died);
    if (died) {
        *state = STATE_GAMEOVER;
        return;
    }
    if (hero_driven(p)->slow_turns_left > 0) {
        /* slowed blood means the temple moves twice for every one of your
           steps */
        hero_driven(p)->slow_turns_left--;
        process_monster_turns(m, p, &died);
        if (died) {
            *state = STATE_GAMEOVER;
            return;
        }
    }
    if (hero_driven(p)->hp < hp_before) {
        g_hp_flash = true;
        anim_flash_cell(m, p, hero_driven(p)->x, hero_driven(p)->y, '@', CP_UI_WARN, 1);
    }
    /* End of turn is the one safe place to do this: every combat routine
       that holds a Monster* (melee, spells, ranged pierce/AoE) has already
       returned by now, so nothing can be left pointing at a moved element. */
    districts_tick(m, p);
    if (hero_driven(p)->hp <= 0) {
        if (!guardian_catch(p) && !companions_attempt_rescue(p, m)
            && !party_pass_the_torch(p)) { *state = STATE_GAMEOVER; return; }
        guardian_follow_through(p, m);
    }

    compact_dead_monsters(m);
    maybe_respawn_monsters(m, p);
    refresh_vision(p, m);
}

static void handle_town_move(Player *p, Map *m, int dx, int dy, GameState *state) {
    int nx = hero_driven(p)->x + dx, ny = hero_driven(p)->y + dy;
    if (!is_walkable_player(m, nx, ny)) return;
    hero_driven(p)->x = nx;
    hero_driven(p)->y = ny;

    TileType t = m->tiles[ny][nx].type;
    if (t == TILE_SHOP_GENERAL) {
        run_shop_general(p);
    } else if (t == TILE_SHOP_ARMORY) {
        run_shop_armory(p);
    } else if (t == TILE_SHOP_APOTHECARY) {
        run_shop_apothecary(p);
    } else if (t == TILE_QUEST_BOARD) {
        run_quest_board(p);
    } else if (t == TILE_SHOP_ARCANIST) {
        run_shop_arcanist(p);
    } else if (t == TILE_TAVERN) {
        run_tavern(p);
    } else if (t == TILE_GLADIATOR) {
        run_gladiator_school(p);
    } else if (t == TILE_BANK) {
        run_bank(p);
    } else if (t == TILE_ORACLE) {
        run_oracle(p);
    } else if (t == TILE_ALTAR) {
        run_altar(p);
    } else if (t == TILE_BAZAAR) {
        run_bazaar(p);
    } else if (t == TILE_RACES) {
        run_races(p);
    } else if (t == TILE_KITCHEN) {
        run_kitchen(p);
    } else if (t == TILE_BLACKMARKET) {
        run_black_market(p);
    } else if (t == TILE_JUNKYARD) {
        /* No menu, like the Inn: you are not choosing which bolt to sell.
           Walk in, the whole armful is weighed, you walk out with coin. */
        int total = 0;
        for (int t2 = 0; t2 < MAT_COUNT; t2++) total += p->mat_count[t2];

        if (total <= 0) {
            log_msg("The scrap-master looks you over. \"Come back with something.\"");
        } else {
            /* Scrap sells. Platinum and diamond sell too -- but the shops want
               them for the deep upgrade rungs, so selling those is a choice
               you can regret rather than free money. Weighed tier by tier so
               you can see what you just gave up. */
            long paid_total = 0;
            for (int t2 = 0; t2 < MAT_COUNT; t2++) {
                if (p->mat_count[t2] <= 0) continue;
                long raw = p->mat_value[t2];
                if (raw > 2000000000L) raw = 2000000000L;
                long paid = player_gain_gold(p, (int)raw);
                paid_total += paid;
                log_msg("  %d of %s -- %ld gold.", p->mat_count[t2], material_name(t2), paid);
                p->mat_count[t2] = 0;
                p->mat_value[t2] = 0;
            }
            log_msg("The scales come back at %ld in total.", paid_total);
        }
    } else if (t == TILE_INN) {
        /* The innkeeper takes a fifth of whatever you are carrying, every
           time, so resting is never free and gets more expensive the richer
           you are. Rounding down means a broke character can still sleep.

           This used to have no prompt at all -- walking in *was* the whole
           interaction, deliberately, like the Junkyard. The Junkyard can get
           away with it because walking in only ever gives you money. The Inn
           takes a fifth of the purse and moves the checkpoint, and the plaza
           is somewhere you walk through on the way to the temple. Reported
           from play: "a person could save and don't even realise". Quite --
           and the more gold you are carrying, the more that mistake costs,
           which is the wrong way round for an accident. */
        int fee = p->gold / 5;
        bool no_return = (p->difficulty == DIFFICULTY_HARDCORE);
        if (!confirm_prompt(no_return
                ? "A room is %d gold. Hardcore -- it mends you, and nothing more. Take it?"
                : "A room is %d gold, and this becomes where you wake up. Take it?", fee)) {
            log_msg("You put your head back out into the plaza.");
            return;
        }
        p->gold -= fee;
        hero_driven(p)->hp = hero_driven(p)->maxhp;

        if (p->difficulty == DIFFICULTY_HARDCORE) {
            /* Hardcore is the mode whose whole promise is that death is
               final. The bed is real; the resurrection is not. */
            log_msg("You take a room at the Inn. %d gold, and you sleep like the dead.", fee);
            log_msg("Hardcore: no one is coming to fetch you back. Rest well.");
        } else {
            save_inn_snapshot(p);
            log_msg("You take a room at the Inn. %d gold, a hot meal, and a bed.", fee);
            log_msg("If the temple takes you, this is where you'll wake up.");
        }
    } else if (t == TILE_TEMPLE_ENTRANCE) {
        /* The one prompt the self-play policy answers rather than fuzzes: it
           is not a menu, it is "which floor", and escaping out of it restarts
           at 1 every single time. */
        autoplay_scene(AP_SCENE_DESCEND, p, m);
        int target = prompt_floor_choice(p->deepest_floor);
        int ux, uy;
        dda_floor_end(p, m);               /* read how the floor you are leaving went */
        /* NOT where the per-visit counters reset. Walking through this door
           and straight back out is two keypresses and no time at all, and
           resetting here meant the track's generous races and the pub's one
           service night could both be had again for free, forever. They are
           the only guard those two systems have. See the descent below. */
        companions_dismiss_temporary(p);   /* built things stay where they were built */
        enter_floor(p);
        generate_temple_floor(m, target, &ux, &uy, p);
        record_floor(m, p);
        p->floor = target;
        if (p->deepest_floor < target) p->deepest_floor = target;
        hero_driven(p)->x = ux;
        hero_driven(p)->y = uy;
        apply_reveal_perks(m, p);
        companions_place(p, m);
        refresh_vision(p, m);
        log_msg("You descend into floor %d of the temple.", target);
        log_floor_event(m);
        log_gate_hint(target);
        log_overrun_warning(m);
        log_wilds(m);
        *state = STATE_TEMPLE;
    }
}

static void run_spell_menu(Player *p, Map *m, GameState *state, const char *floor_label) {
    if (hero_driven(p)->spell_count == 0) {
        draw_spell_menu(hero_driven_c(p), 0);
        getch();
        return;
    }

    int cursor = 0;
    for (;;) {
        draw_spell_menu(hero_driven_c(p), cursor);
        int ch = getch();
        if (ch == 27 || ch == 'q') return;
        switch (ch) {
            case KEY_UP: case 'k': if (cursor > 0) cursor--; break;
            case KEY_DOWN: case 'j': if (cursor < hero_driven(p)->spell_count - 1) cursor++; break;
            case KEY_NPAGE: cursor += SPELL_PAGE_SIZE; if (cursor > hero_driven(p)->spell_count - 1) cursor = hero_driven(p)->spell_count - 1; break;
            case KEY_PPAGE: cursor -= SPELL_PAGE_SIZE; if (cursor < 0) cursor = 0; break;
            case 'A': case 'a': {
                /* A deeper reservoir rather than another spell -- the guild
                   will widen the channel for anyone who can pay. */
                int price = discounted_price(p, upgrade_price(hero_driven(p)->aether_plus));
                if (p->gold < price) {
                    log_msg("The guild wants %d to widen your channel. You have %d.", price, p->gold);
                } else {
                    p->gold -= price;
                    hero_driven(p)->aether_plus++;
                    hero_driven(p)->aether_max += UPGRADE_AETHER_STEP;
                    hero_driven(p)->aether = hero_driven(p)->aether_max;
                    log_msg("They widen the channel. (+%d aether, now +%d)",
                            UPGRADE_AETHER_STEP, hero_driven(p)->aether_plus);
                }
                break;
            }
            case '\n': case '\r': case KEY_ENTER: {
                bool boss_killed = false;
                /* cast_spell_slot's animations overlay whatever's currently
                   on screen -- restore the real map here first, since the
                   spell list (just drawn above) is what's showing right now. */
                draw_game_screen(m, p, hero_driven_c(p), floor_label, true);
                if (!cast_spell_slot(p, hero_driven(p), m, cursor, &boss_killed)) break;
                if (boss_killed) {
                    *state = STATE_WIN;
                    return;
                }
                resolve_after_player_turn(p, m, state);
                return;
            }
            default: break;
        }
    }
}


/* What happens to *the party* when the body holding the main-character role
   lands on a square: the stairs, the waygate home.
 *
 * Split out because the driven body is not always the original character, and
 * the governing rule is that whoever holds the role IS the main character --
 * so a hire standing on a staircase takes it, and the party goes with them.
 * Scoping this to "only the original character may descend" was a mistake I
 * made and the project owner caught: it would have meant switching back
 * before every staircase, which is the rule contradicting itself.
 *
 * Body-level effects -- terrain damage, snares -- are deliberately NOT here.
 * Those happen to whoever walked into them, and belong with the mover.
 *
 * Returns true if the party moved floors, in which case the caller must not
 * keep using the old map.
 */
/* Coming up for air.
 *
   This used to be `p->controlled = 0` -- the character is who walks the
   streets -- written before a run could outlive the character. It handed the
   role back to slot 0 unconditionally, so a party whose original character had
   already fallen walked into town as a corpse: nought hit points, the
   character sheet of somebody dead, and every shop happy to serve them.

   The rule the project settled on answers it, and it is now what the code
   does: whoever holds the role walks into town, shops there, and sleeps
   there. The role does not move. Coming up for air is a change of floor, not
   a change of who you are. */
static void party_enter_town(Player *p) {
    /* Anyone who went down stays down, but they stop holding a seat. Without
       this a hire killed on the way out keeps their slot all the way home, so
       the party is full of a corpse and the Tavern refuses to take them back. */
    companions_reap_fallen(p);
    Hero *walker = hero_driven(p);
    walker->x = TEMPLE_DOOR_X;
    walker->y = TEMPLE_DOOR_Y;
}

/* Two more duplicate paths used to stand here: hire_landing_damage(), the
   terrain damage a driven hire took, and party_landing_effects(), a second
   implementation of loot, doors, portals and the stairs for when a hire was
   the one standing on them. Both are gone into actor_step(), which is the
   only place any actor's step has consequences. They are the clearest
   measure of what the split was costing: two functions whose entire reason
   for existing was that a hire could not use the character's. */

/* ---- six actors ---------------------------------------------------------
 *
 * The party is six actors. Not a hero and five followers, and not a player
 * plus AI -- six of the same thing, taking turns on the same floor under the
 * same physics.
 *
 * The only asymmetry in the whole system is *where one actor's decision comes
 * from this turn*: one of them has the human attached, and the other five ask
 * actor_decide(). That is an input source, not a different kind of creature,
 * and it is the only `if` about it anywhere.
 *
 * Everything that used to be special about "the player" is now one of two
 * things:
 *
 *   - a property of the *body*, which every actor has (hit points, a weapon,
 *     hazards underfoot, loot picked up into the shared pack);
 *   - a property of *the human deciding*, which is not about the body at all
 *     (opening a shop, taking the party downstairs, going home through the
 *     waygate). `player_led` below is that, and it is deliberately not called
 *     `is_mc`: the party goes where the person playing says it goes, and the
 *     person playing is attached to whichever actor they chose.
 *
 * This replaced three implementations of "an actor moves one square" --
 * handle_temple_move for the character, companion_player_move for a driven
 * hire, companion_move_to for the AI. Three cannot agree, and every bug
 * reported from play was them disagreeing: a driven hire got a different
 * portal, no lever, no camp, no merchant, because it was walking through the
 * poorest of the three.
 *
 * Returns true if the step spent a turn.
 */
static bool actor_step(Player *p, Hero *h, Map *m, int dx, int dy, GameState *state) {
    if (!hero_is_up(h)) return false;
    const bool player_led = (h == hero_driven(p));
    int nx = h->x + dx, ny = h->y + dy;
    if (nx < 0 || nx >= MAP_W || ny < 0 || ny >= MAP_H) return false;

    Monster *target = monster_at(m, nx, ny);
    if (target) {
        bool boss_killed = false;
        hero_attack_monster(p, h, m, target, &boss_killed);
        /* Only the human's actor can win the game by killing the Warden --
           not because a hire's blow counts for less, but because STATE_WIN
           ends the run and that is the human's turn to be told. A hire that
           lands the killing blow still kills it; the state change waits for
           the turn to resolve. */
        if (boss_killed && player_led) { *state = STATE_WIN; return true; }
        return true;
    }

    if (m->tiles[ny][nx].type == TILE_LOCKED_DOOR) {
        if (p->keys > 0) {
            p->keys--;
            m->tiles[ny][nx].type = TILE_FLOOR;
            log_msg("%s unlocks the door with a brass key.", actor_name(p, h));
        } else if (player_led) {
            log_msg("The door is locked. You need a key.");
        }
        return p->keys >= 0 && m->tiles[ny][nx].type == TILE_FLOOR;
    }

        if (!is_walkable_player(m, nx, ny)) return false;
    /* An ally in the way is somebody to squeeze past. The rule and the reason
       live in party_displace_into(); this is the one caller that also has to
       say so out loud. */
    if (party_displace_into(p, h, nx, ny)) {
        if (player_led) log_msg("You squeeze past somebody.");
        return true;
    }
    h->x = nx;
    h->y = ny;
    h->still_turns = 0;          /* the mats felt that */
    TileType landed = m->tiles[ny][nx].type;

    /* Said once, the first time you cross the palisade. The camp is rare
       enough that arriving in one should be an event. */
    if (in_haven(m, nx, ny) && !m->haven_entered && player_led) {
        m->haven_entered = true;
        log_msg("A camp, walled and held. Nothing hunts you inside the palisade.");
    }

    if (landed == TILE_LEVER && m->lever_x == nx && m->lever_y == ny
        && m->tiles[m->lever_door_y][m->lever_door_x].type == TILE_SEALED_DOOR) {
        m->tiles[m->lever_door_y][m->lever_door_x].type = TILE_FLOOR;
        log_msg("%s wrenches the lever down. Somewhere nearby, machinery grinds and a sealed door falls open.",
                actor_name(p, h));
    }

    /* The ground does not care who walked into it. This is the half that used
       to be missing from a driven hire's step: lava, miasma and snares were
       the character's problem and nobody else's. */
    apply_terrain_hazard(p, h, landed);
    apply_snare(p, h, m, nx, ny);
    if (h->hp <= 0) {
        h->alive = false;
        if (!guardian_catch(p) && !companions_attempt_rescue(p, m)
            && !party_pass_the_torch(p)) { *state = STATE_GAMEOVER; return true; }
        guardian_follow_through(p, m);
    }

    pickup_items_at(p, m, nx, ny);

    /* Features are where the human decides something -- a shop opens, a
       shrine is accepted, the waygate takes everybody home. An actor under AI
       walks past them; it has no way to answer the question they ask. */
    MapFeature *f = player_led ? feature_at(m, nx, ny) : NULL;
    if (f) {
        if (f->type == FEATURE_MERCHANT) {
            run_shop_merchant(p);
        } else if (f->type == FEATURE_TOWN_GATE) {
            generate_town_map(m);
            p->floor = 0;
            party_enter_town(p);
            *state = STATE_TOWN;
            log_msg("The waygate hums and sets you back on solid streets.");
            return true;
        } else {
            trigger_feature(m, f, p);
        }
    }

    if (landed == TILE_PORTAL && m->has_portals) {
        /* A portal moves whoever stepped in it. Nobody else. */
        bool at_a = (nx == m->portal_ax && ny == m->portal_ay);
        h->x = at_a ? m->portal_bx : m->portal_ax;
        h->y = at_a ? m->portal_by : m->portal_ay;
        if (player_led) log_msg("The portal folds space around you -- you step out somewhere else.");
        else log_msg("%s steps into the portal and is gone.", actor_name(p, h));
        if (rand() % 100 < 25 && m->monster_count < MAX_MONSTERS) {
            for (int tries = 0; tries < 10; tries++) {
                int mx = h->x + (rand() % 5 - 2), my = h->y + (rand() % 5 - 2);
                if ((mx == h->x && my == h->y) || !is_walkable_monster(m, mx, my) || monster_at(m, mx, my)) continue;
                m->monsters[m->monster_count] = make_monster_for_floor(m->floor_num, mx, my);
                m->monsters[m->monster_count].aggro = true;
                m->monster_count++;
                log_msg("Something followed you through.");
                break;
            }
        }
    }

    /* The stairs and the climb are party-level: they move all six actors and
       replace the floor. So they answer to the human, not to whoever happens
       to path over them -- an actor under AI standing on the steps is standing
       on the steps, and that is all. */
    if (landed == TILE_STAIRS_DOWN && player_led) {
        int next_floor = p->floor + 1;
        if (p->deepest_floor < next_floor) p->deepest_floor = next_floor;
        int ux, uy;
        dda_floor_end(p, m);               /* read how the floor you are leaving went */

        /* The one event that resets the per-visit counters, and the only one
           that costs anything: going *down*. You have to find a staircase and
           walk to it, which is a floor's worth of time -- so a second service
           night or another three generous races is earned rather than clicked
           for. Entering the temple and climbing back up are both free, and
           both used to reset these. */
        p->races_this_visit = 0;           /* the bookmaker's memory is short */
        p->kitchen_nights   = 0;           /* and the pub opens again tomorrow */

        companions_dismiss_temporary(p);   /* built things stay where they were built */
        enter_floor(p);
        generate_temple_floor(m, next_floor, &ux, &uy, p);
        record_floor(m, p);
        p->floor = next_floor;
        h->x = ux;
        h->y = uy;
        apply_reveal_perks(m, p);
        companions_place(p, m);
        refresh_vision(p, m);
        log_msg("You descend to floor %d.", p->floor);
        log_floor_event(m);
        log_gate_hint(next_floor);
        log_overrun_warning(m);
        log_wilds(m);
        return true;
    }

    /* A hole in the mine floor. The stairs' branch above with a price on it and
       none of its bookkeeping: you arrive on the floor below at wherever its
       up-stairs are, having skipped whatever was left of this one.
     *
       Answers to the human like the stairs do, and for the same reason -- an
       actor under AI standing beside a hole is standing beside a hole. The
       cost is a percentage of maximum health rather than a flat number, so it
       stays a real question at depth instead of decaying into free travel. */
    if (landed == TILE_PIT && player_led && p->floor > 0 && p->floor < MAX_FLOOR) {
        int dmg = h->maxhp * PIT_FALL_HP_PCT / 100;
        if (dmg < PIT_MIN_DAMAGE) dmg = PIT_MIN_DAMAGE;
        dmg -= dmg * (h->hazard_resist_pct + h->set_bonus_hazard_resist) / 100;
        if (dmg < 1) dmg = 1;
        h->hp -= dmg;
        if (h->hp < 0) h->hp = 0;
        log_msg("The floor is not a floor. You go down hard -- %d damage.", dmg);

        if (h->hp <= 0) {
            h->alive = false;
            if (!guardian_catch(p) && !companions_attempt_rescue(p, m)
                && !party_pass_the_torch(p)) { *state = STATE_GAMEOVER; return true; }
            guardian_follow_through(p, m);
        }

        int next_floor = p->floor + 1;
        if (p->deepest_floor < next_floor) p->deepest_floor = next_floor;
        int ux, uy;
        dda_floor_end(p, m);
        p->races_this_visit = 0;
        p->kitchen_nights   = 0;
        companions_dismiss_temporary(p);
        enter_floor(p);
        generate_temple_floor(m, next_floor, &ux, &uy, p);
        record_floor(m, p);
        p->floor = next_floor;
        hero_driven(p)->x = ux;
        hero_driven(p)->y = uy;
        apply_reveal_perks(m, p);
        companions_place(p, m);
        refresh_vision(p, m);
        log_msg("You land on floor %d, in the dark, somewhere you did not choose.", p->floor);
        log_floor_event(m);
        log_gate_hint(next_floor);
        log_overrun_warning(m);
        log_wilds(m);
        return true;
    }

    if (landed == TILE_STAIRS_UP && player_led) {
        if (p->floor <= 1) {
            generate_town_map(m);
            p->floor = 0;
            party_enter_town(p);
            *state = STATE_TOWN;
            log_msg("You climb back out into the ruined plaza.");
        } else {
            int prev = p->floor - 1;
            int ux, uy;
            dda_floor_end(p, m);           /* read how the floor you are leaving went */
            companions_dismiss_temporary(p);
            enter_floor(p);
            generate_temple_floor(m, prev, &ux, &uy, p);
            record_floor(m, p);
            p->floor = prev;
            h->x = (m->stairs_down_x >= 0) ? m->stairs_down_x : ux;
            h->y = (m->stairs_down_y >= 0) ? m->stairs_down_y : uy;
            apply_reveal_perks(m, p);
            companions_place(p, m);
        refresh_vision(p, m);
            log_msg("You climb back to floor %d.", p->floor);
            log_floor_event(m);
            log_gate_hint(prev);
            log_overrun_warning(m);
            log_wilds(m);
        }
        return true;
    }

    return true;
}


/* ---- a whole turn for one actor ---------------------------------------
 *
 * The step, then the consequences of the step, then the world's reply. This
 * is the only way a turn is ever spent, and it takes the actor spending it.
 *
 * `party_step()` used to live here and its job was to *choose between two
 * movement functions* depending on who was driving. There is one movement
 * function now, so the choosing is gone and what is left is the sequence.
 */
static bool actor_turn(Player *p, Hero *h, Map *m, int dx, int dy, GameState *state) {
    if (!actor_step(p, h, m, dx, dy, state)) return false;
    if (*state != STATE_TEMPLE && *state != STATE_TOWN) return true;
    /* Only the human's turn advances the world. The other five act inside
       resolve_after_player_turn, which is the world moving. */
    if (h == hero_driven(p)) resolve_after_player_turn(p, m, state);
    return true;
}



/* Auto-explore used to stop after 600 steps whether or not it had finished
   anything, which on a 140x80 floor meant handing control back mid-room for
   no reason the player could see. It runs until the floor is done now.

   A loop with no step cap needs some other guarantee it can't spin, so it
   watches for progress instead of counting steps: if nothing observable has
   changed for this many consecutive iterations -- not position, not HP, not
   kills, not the map you've revealed, not even the health of whatever you
   are hitting -- then it is stuck rather than busy, and says so. At the
   per-step pause that is a few seconds of genuinely nothing happening, so
   it cannot fire during a long fight or a long walk. */
#define AUTOEXPLORE_STUCK_LIMIT 60

/* Milliseconds between auto-explore steps.
 *
   This is not a performance number -- the work behind a step is 8ms even on
   a Well floor with five hires and eighty thousand monsters on it. It is a
   deliberate pause so the walk is watchable, and at 60ms it was 85% of the
   wall clock: auto-explore was capped at sixteen steps a second while
   walking by hand was as fast as the player could press a key. On a floor
   that takes tens of thousands of steps to cross, that is the difference
   between a long walk and an evening.

   Lowered rather than made adaptive. An earlier attempt sped up when nothing
   was happening and slowed down for fights, and it read as jumping -- the
   objection was that it stopped looking like walking. A smaller constant is
   still every step, drawn, in order; just a brisker one.

   Twenty-four rather than twenty: a round-ish 40 steps a second, chosen by
   eye rather than by measurement, which is the right way to pick the speed
   something is *watched* at. */
#define AUTOEXPLORE_TICK_MS 24

/* Everything auto-explore could reasonably call "getting somewhere",
   collapsed to one number. Cheap enough to recompute every step. */

/* How far auto-explore will go out of its way to pick a fight. Bounded on
   purpose: "hunt what's near you" and "clear the floor" are different
   requests, and an unbounded hunter would spend a 600-step budget walking
   across a 140x80 map to something it happened to glimpse. Targets also have
   to be in line of sight (find_nearest_target's rule), so this never chases
   a monster it only remembers seeing. */
#define AUTOEXPLORE_HUNT_RADIUS 10

static int step_toward(int from, int to) { return (to > from) - (to < from); }

/* Auto-explore's combat turn. Returns true if it spent the step fighting;
   false means there is nothing worth attacking and the caller should get on
   with exploring.

   The order is the point of the function: hit what's already adjacent, then
   spend the things that work at range, and only close the distance when
   neither is available. Doing it the other way round -- walk first, attack
   on arrival -- is how you end up meleeing with a bow on your back. */
/* Carries out whatever autoexplore_decide_fight() chose. The choosing is in
   autoexplore.c and is testable; this is the half that spends a turn. */
static bool autoexplore_fight_step(Player *p, Map *m, GameState *state) {
    Monster *target = find_nearest_target(m, p, AUTOEXPLORE_HUNT_RADIUS);
    if (!target) return false;

    /* Copy the coordinates out now. Dead monsters are compacted out of the
       array at the end of the turn, so this pointer does not survive the
       first thing that kills something. */
    int tx = target->x, ty = target->y;
    int adx = abs(tx - hero_driven(p)->x), ady = abs(ty - hero_driven(p)->y);
    int dist = adx > ady ? adx : ady;      /* Chebyshev: movement is 8-way */

    AxFight act = autoexplore_decide_fight(p, m, target, dist);
    bool boss_killed = false;

    switch (act.kind) {
        case AX_NOTHING:
            return false;

        case AX_MELEE:
            /* Moving into a monster is an attack -- actor_step covers the
               kill, the boss case and the monsters' reply. */
            actor_turn(p, hero_driven(p), m, step_toward(hero_driven(p)->x, tx),
                       step_toward(hero_driven(p)->y, ty), state);
            return true;

        case AX_ABILITY:
            if (!use_ability(p, hero_driven(p), m, &boss_killed)) return false;
            break;

        case AX_SPELL:
            if (!cast_spell_slot(p, hero_driven(p), m, act.slot, &boss_killed)) return false;
            break;

        case AX_SHOOT:
            if (!fire_ranged(p, hero_driven(p), m, &boss_killed)) return false;
            break;

        case AX_CLOSE: {
            /* Nothing reaches it: close in. Same two-pass hazard preference as
               every other leg of the route -- a hunt is not a reason to walk
               through lava. */
            int dx, dy;
            if (!autoexplore_step_toward(m, hero_driven(p)->x, hero_driven(p)->y,
                                         tx, ty, &dx, &dy))
                return false;        /* visible but unreachable -- back to exploring */
            actor_turn(p, hero_driven(p), m, dx, dy, state);
            return true;
        }
    }

    if (boss_killed) *state = STATE_WIN;
    else resolve_after_player_turn(p, m, state);
    return true;
}

/* Draws the current frame, then gives auto-explore's per-step pause -- but
   bails immediately if the player pressed anything during that pause,
   instead of leaving it sitting in the input queue to silently fire later
   against whatever state auto-explore happens to stop in. Without this, a
   key typed while auto-explore was still running (a very real thing to do,
   since a single call can run for tens of real seconds) would only take
   effect once the call finally returns -- by which point the screen the
   player was reacting to no longer matches what's on screen. flushinp()
   clears the whole queue, not just the one character getch() consumed, in
   case more than one key piled up. */
static bool autoexplore_tick(Player *p, Map *m) {
    char label[64];
    snprintf(label, sizeof(label), "Floor %d -- %s", p->floor, biome_name(m->biome));
    draw_game_screen(m, p, hero_driven_c(p), label, true);

    /* "Is a key waiting?", not "give me a key". The self-play hook only knows
       how to answer the second question, so under --silentrun this poll came
       back with a keypress every time and auto-explore stopped after one
       step, always. See autoplay_peek. */
    int peek;
    if (autoplay_active()) {
        peek = autoplay_peek();
    } else {
        nodelay(stdscr, TRUE);
        peek = getch();
        nodelay(stdscr, FALSE);
    }
    if (peek != ERR) {
        log_msg("Auto-explore: stopped -- go ahead.");
        return true;
    }

    /* The pause is so a watching player can see the walk happen. Nobody is
       watching a silent run, and paying it anyway capped auto-explore at
       about twenty-five steps a second of wall clock -- which is what a
       fifteen-thousand-key run was actually spending its five minutes on. */
    if (!autoplay_active()) napms(AUTOEXPLORE_TICK_MS);
    return false;
}

/* Auto-explore used to hand control back the moment it took a staircase.
   That is one keypress per floor, which is fine when you are driving and
   wrong when you are not -- the point of delegating the walk is that you can
   stop watching, and a floor boundary is not a decision, it is a doorway.
 *
   Carrying on means re-checking the two things the top of the run checked,
   because they are properties of the floor you are now standing on rather
   than of the run: there has to be a way down, and it must not be the
   Warden's floor. Everything else that stops auto-explore -- a keypress,
   being badly hurt with nothing to drink, or making no observable progress
   for sixty steps -- still applies exactly as before, on every floor.

   The floor-scoped bookkeeping has to be reset too. The progress signature
   folds in position, hit points, revealed tiles and monster health, all of
   which change wholesale on arrival; leaving the old value in place would
   compare a fresh floor against the last step of the previous one. */
static bool autoexplore_continue_below(Player *p, Map *m, int *start_floor,
                                       unsigned long *last_progress, int *stuck) {
    if (m->floor_num >= MAX_FLOOR) {
        log_msg("Floor %d. Auto-explore stops at the Warden's door -- that one is yours.",
                p->floor);
        return false;
    }
    if (m->stairs_down_x < 0) {
        log_msg("Floor %d, and no way onward from here. Auto-explore stops.", p->floor);
        return false;
    }

    *start_floor   = p->floor;
    *last_progress = autoexplore_progress(p, m);
    *stuck         = 0;
    return true;
}


/* You have walked into the room the waygate is in.
 *
 * Auto-explore is a delegated walk, and the one thing it must not do is walk
 * you *past* a decision. The gate home is exactly that: the only way to bank a
 * run's gold, restock and come back, and easy to miss because it is one glyph
 * on a floor you are watching scroll by. The staircase and the gate often
 * share a room, so "explore until the stairs" happily carries you over the
 * gate and down a floor with a full pack and nowhere to spend it.
 *
 * The test is *the room*, not a radius around the tile. A radius would be a
 * number I picked; "the room with the Y in it" is the thing the player can
 * actually see, and it is the room they would have stopped in themselves.
 *
 * Once per floor, or a walk that re-entered the room would spend the rest of
 * the floor arguing with you. */
static bool paused_at_waygate(Player *p, Map *m) {
    if (m->floor_num <= 0 || m->waygate_noticed) return false;
    if (m->waygate_room_w <= 0) return false;          /* no gate on this floor */

    int px = party_x(p), py = party_y(p);
    if (px < m->waygate_room_x || px >= m->waygate_room_x + m->waygate_room_w) return false;
    if (py < m->waygate_room_y || py >= m->waygate_room_y + m->waygate_room_h) return false;

    m->waygate_noticed = true;
    log_msg("The waygate is in this room. Auto-explore stops here --");
    log_msg("step onto the Y to go home, or press x again to walk on.");
    return true;
}

static void run_auto_explore(Player *p, Map *m, GameState *state) {
    if (m->floor_num >= MAX_FLOOR) {
        log_msg("Auto-explore refuses to lead you to the Warden. Some fights you take yourself.");
        return;
    }
    if (m->stairs_down_x < 0) {
        log_msg("There is no path onward from here.");
        return;
    }

    log_msg("Auto-exploring -- hunting anything in sight.");
    /* Walks whichever body holds the role. It used to read p->x/p->y and
       move through the character.s mover, so pressing 'x' while driving a hire
       auto-explored the *character* -- the map scrolled somewhere you were
       not and your body stood still. */
    int start_floor = p->floor;

    unsigned long last_progress = autoexplore_progress(p, m);
    int stuck = 0;

    for (;;) {
        unsigned long now = autoexplore_progress(p, m);
        if (now == last_progress) {
            if (++stuck >= AUTOEXPLORE_STUCK_LIMIT) {
                log_msg("Auto-explore: getting nowhere here -- stopping.");
                break;
            }
        } else {
            last_progress = now;
            stuck = 0;
        }

        if (hero_driven(p)->hp < hero_driven(p)->maxhp / 2) {
            int best_idx = -1, best_heal = 0;
            for (int i = 0; i < p->inv_count; i++) {
                if (p->inventory[i].is_recall || p->inventory[i].heal <= 0) continue;
                if (p->inventory[i].heal > best_heal) { best_heal = p->inventory[i].heal; best_idx = i; }
            }
            if (best_idx >= 0) {
                use_inventory_item(p, best_idx);
                resolve_after_player_turn(p, m, state);
                if (*state != STATE_TEMPLE) { break; }
                if (autoexplore_tick(p, m)) { break; }
                continue;
            } else if (hero_driven(p)->hp < hero_driven(p)->maxhp / 4) {
                log_msg("Too hurt to press on blindly -- auto-explore stops.");
                break;
            }
        }

        if (consume_stun_if_active(p)) {
            resolve_after_player_turn(p, m, state);
            if (*state != STATE_TEMPLE) { break; }
            if (autoexplore_tick(p, m)) { break; }
            continue;
        }

        /* Fight before looting or exploring: a monster in sight is the thing
           that decides whether the next fifty steps happen at all. */
        if (autoexplore_fight_step(p, m, state)) {
            if (*state != STATE_TEMPLE) { break; }
            if (p->floor != start_floor
                && !autoexplore_continue_below(p, m, &start_floor, &last_progress, &stuck)) break;
            if (autoexplore_tick(p, m)) { break; }
            continue;
        }

        /* Every leg of the route prefers a hazard-free path and only walks
           through lava or miasma when there's no other way -- the same
           two-pass approach the stairs path already used. */
        /* Every leg prefers a hazard-free path and only walks through lava or
           miasma when there is no other way. The staircase problem -- landing
           on one takes it, which is right for a keypress and wrong for a
           delegated walk -- is handled inside the searches themselves, which
           route around the way back up rather than over it. */
        int dx = 0, dy = 0;
        bool stairs_known = false;
        if (!autoexplore_route(p, m, &dx, &dy, &stairs_known)) {
            /* Reaching here now means all three legs failed -- no loot, no
               route to the stairs, and no frontier left to walk to -- so the
               floor really is walked out. It used to be reachable merely by
               *seeing* the staircase before uncovering a way to it, which is
               the common case and not an ending at all. */
            log_msg(stairs_known
                    ? "Auto-explore: the floor is walked out and the stairs are cut off."
                    : "Auto-explore: nothing left to explore, and no stairs found.");
            break;
        }

        actor_turn(p, hero_driven(p), m, dx, dy, state);
        if (*state != STATE_TEMPLE) { break; }
        if (paused_at_waygate(p, m)) break;
        if (p->floor != start_floor
            && !autoexplore_continue_below(p, m, &start_floor, &last_progress, &stuck)) break;

        if (autoexplore_tick(p, m)) { break; }
    }

    /* Whatever stopped the loop, discard any input that piled up while it
       was running -- the alternative is a keypress from several real
       seconds ago silently firing against a screen the player never
       actually saw it happen against. */
    flushinp();

    /* Every way out of the loop above logs its own reason, so there is
       nothing left to report here. */
}

/* Set by --seed on the command line; 0 means "pick one". Held here rather
   than threaded through because character creation is the only place that
   consumes it, and only once. */
static unsigned int g_forced_seed = 0;

static void print_usage(const char *argv0) {
    printf("aether-descent -- a roguelike\n\n");
    printf("  %s [--seed N]\n\n", argv0);
    printf("  --seed N   Start a new character on run seed N. Floor layouts,\n");
    printf("             the Tavern roster and the town are the same every\n");
    printf("             time for a given seed, so a run can be handed to\n");
    printf("             someone else exactly as you played it.\n");
    printf("             Your current run's seed is on the character sheet.\n");
    printf("\n");
    printf("  %s --silentrun [--log PATH] [--keys N] [--seed N]\n", argv0);
    printf("             Play the game with nobody at the keyboard: navigation\n");
    printf("             by policy, everything else by weighted-random keys,\n");
    printf("             frames drawn into /dev/null so drawing code is still\n");
    printf("             under test. Writes a keystroke-and-message log.\n");
    printf("             Keeps its saves in ./silentrun-state rather than your\n");
    printf("             own -- it dies a great many times per run, and dying\n");
    printf("             deletes a suspended game. Set AETHER_STATE_DIR to put\n");
    printf("             them somewhere else. Needs the ncurses build.\n");
    printf("\n");
    printf("  %s --guardian [0-100]\n", argv0);
    printf("             The guardian angel: a cheat, not a difficulty. Points\n");
    printf("             of crit and evasion, and a monster critical that lands\n");
    printf("             less often. Largest on floor 1 and gone by the middle\n");
    printf("             of the descent, plus a small safety net whenever you\n");
    printf("             are nearly dead, at any depth. Nothing is shown on any\n");
    printf("             screen. Off unless you ask; a number dials it down.\n");
    printf("             Works with every difficulty and changes none of them:\n");
    printf("             Hardcore is still permadeath, still no recall.\n");
    printf("  --record PATH\n");
    printf("             Debug mode. Writes one JSON line per turn to PATH:\n");
    printf("             every body's position, the viewport's terrain and its\n");
    printf("             seen/lit flags, the monsters on screen, and everything\n");
    printf("             logged that turn. Hundreds of megabytes for a long\n");
    printf("             session, and the point is that a bug can be pointed at\n");
    printf("             rather than described.\n");
    printf("  --help     This.\n");
}

/* The keystroke that produced the state the tape is about to record.
 *
   The tick is written at the *top* of a frame rather than after the turn, so
   it captures the world as it stands once the previous key has been fully
   resolved -- animations, companion turns, district ticks and all. Recording
   straight after getch() would catch the world mid-turn, and recording at the
   end of the case would need a hook on every one of its exits. */
static int g_record_last_key = 0;

int main(int argc, char **argv) {
    bool silent = false;
    const char *log_path = "silentrun.log";
    long key_budget = 200000;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--silentrun") == 0) { silent = true; continue; }
        /* The cheat. Bare `--guardian` is full strength; a number dials it
           down, so "a bit of help" is expressible and so the effect can be
           swept rather than argued about. Deliberately not part of the
           difficulty screen -- see dda.h. */
        if (strcmp(argv[i], "--guardian") == 0) {
            int pct = 100;
            if (i + 1 < argc && argv[i + 1][0] >= '0' && argv[i + 1][0] <= '9')
                pct = (int)strtol(argv[++i], NULL, 10);
            guardian_set_strength(pct);
            continue;
        }
        if (strcmp(argv[i], "--log") == 0 && i + 1 < argc) { log_path = argv[++i]; continue; }
        /* Debug mode: a per-tick tape of the whole game. See record.h. */
        if (strcmp(argv[i], "--record") == 0 && i + 1 < argc) {
            record_open(argv[++i]);
            continue;
        }
        if (strcmp(argv[i], "--keys") == 0 && i + 1 < argc) {
            key_budget = strtol(argv[++i], NULL, 10); continue;
        }
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_usage(argv[0]);
            return 0;
        }
        if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc) {
            g_forced_seed = (unsigned int)strtoul(argv[++i], NULL, 10);
            continue;
        }
        fprintf(stderr, "unrecognised argument: %s\n", argv[i]);
        print_usage(argv[0]);
        return 2;
    }

    unsigned int boot_seed = g_forced_seed ? g_forced_seed : (unsigned)time(NULL);
    srand(boot_seed);

    if (silent) {
#if defined(TUI_NOTCURSES)
        (void)log_path; (void)key_budget;
        fprintf(stderr, "--silentrun needs the ncurses build (make, not make notcurses)\n");
        return 2;
#else
        /* A real curses screen whose output goes nowhere. The game still
           draws every frame -- which is the point, because a crash in the
           drawing code is exactly the kind of bug this is looking for -- it
           just draws into /dev/null instead of a terminal. Suppressing the
           draw calls instead would skip the code under test. */
        /* A silent run must not play in the project owner's save slot.
         *
           It quits through the game's own save prompt, it dies two hundred
           times a run, and dying writes a highscore, a class record and an
           Inn snapshot and deletes the suspended run. Pointed at $HOME -- the
           default -- a fuzzer left going overnight will quietly eat a game
           somebody was in the middle of. Found the hard way, on this machine,
           by running it three times.

           AETHER_STATE_DIR already exists for `make serve`, where several
           people share one OS user; this is the same problem with a robot
           instead of a second person. An explicit setting is still honoured,
           so a run can be pointed somewhere deliberately. */
        if (!getenv("AETHER_STATE_DIR"))
            setenv("AETHER_STATE_DIR", "silentrun-state", 1);

        FILE *sink = fopen("/dev/null", "w");
        if (!sink) { fprintf(stderr, "cannot open /dev/null\n"); return 2; }
        SCREEN *scr = newterm("xterm", sink, stdin);
        if (!scr) { fprintf(stderr, "cannot create an offscreen terminal\n"); return 2; }
        set_term(scr);
        if (!autoplay_begin(log_path, boot_seed, key_budget)) {
            fprintf(stderr, "cannot write %s\n", log_path);
            return 2;
        }
        render_set_headless(true);   /* animations would only burn wall clock */
#endif
    } else {
        initscr();
    }
    noecho();
    cbreak();
    keypad(stdscr, TRUE);
    curs_set(0);
    init_colors();
    /* Falls back to text on its own if the stored mode isn't something this
       terminal can do -- the same home directory gets used from more than one
       terminal. */
    render_set_tile_pixels(load_tile_pixels());
    render_set_mode(load_render_mode());

    /* Sizes the map viewport to the real terminal instead of a fixed 58x18,
       so a bigger window shows more of the dungeon. render.c owns this now
       because the answer also depends on the render mode, and it re-runs on
       a resize. render_set_mode above has already called it once; this is
       belt and braces for the case where the mode didn't change. */
    render_layout_viewport();

    int best_floor = load_highscore();
    static Player player;
    static Map map;
    GameState state = STATE_INTRO;

    if (has_saved_run()) {
        bool resumed = false;
        if (screen_continue_prompt() && load_run(&player, &map)) {
            resumed = true;
            g_msg_log_count = 0;
            refresh_vision(&player, &map);
            log_msg("Welcome back, %s. The temple remembers you.", hero_driven(&player)->name);
            state = (player.floor == 0) ? STATE_TOWN : STATE_TEMPLE;
        }
        if (!resumed) delete_saved_run();
    }

    while (state != STATE_QUIT) {
        switch (state) {
            case STATE_INTRO: {
                screen_intro();
                init_new_player(&player);
                player.difficulty = screen_mode_select();
                /* Before anything generates a floor or clamps a viewport:
                   every bound in the game reads g_map_w/g_map_h. */
                player.world_size = screen_world_size_select();
                world_size_apply(player.world_size);
                render_layout_viewport();
                g_msg_log_count = 0;
                state = STATE_CHARCREATE;
                break;
            }

            case STATE_CHARCREATE: {
                /* Role first, then a class inside it -- and Esc on the class
                   list comes back here rather than out of character creation,
                   so changing your mind about the role costs nothing. */
                int cls = -1;
                while (cls < 0) {
                    int arch = screen_archetype_select();
                    cls = screen_class_select(arch);
                }
                apply_class_to_player(&player, cls);
                /* A new character must not be able to die into the previous
                   one's body. The Inn snapshot is per-character, so creating
                   a character retires whatever was banked before it. */
                delete_inn_snapshot();
                /* Fixes this character's twenty Tavern candidates. Stored as
                   a seed so the roster is stable across visits and saves
                   without carrying twenty structs in Player. */
                /* Fix the run before anything random is drawn from it. A
                   forced seed reproduces a run exactly; otherwise one is
                   drawn once and recorded, so a run you stumble into can
                   still be handed to someone else afterwards. */
                player.run_seed = g_forced_seed ? g_forced_seed
                                                : ((unsigned int)rand() ^ 0x9E3779B9u);
                if (player.run_seed == 0) player.run_seed = 1u;
                srand(player.run_seed);
                player.tavern_seed = (unsigned int)rand() ^ 0x5bf03635u;
                /* Two rations in the pack, for the same reason as the gold:
                   dying on floor 1 with nothing to drink is the worst first
                   impression the game can make. */
                grant_starting_kit(&player);
                /* Which game this run is actually playing. The mode and the
                   world size are both chosen on fuzzed screens, so "deepest
                   floor 1" means one thing on Normal in the Shaft and
                   something entirely different on Swarm in the largest world
                   -- where the balance harness's own agent has a median of 1.
                   Without this line the log cannot tell those apart, and the
                   depth counter in the footer reads as a verdict on the
                   policy when it may be a verdict on the mode. */
                autoplay_note("(new character: class %d, %s, world %s)",
                              cls, difficulty_name(player.difficulty),
                              world_size_name(player.world_size));
                screen_name_entry(hero_driven(&player)->name, sizeof(hero_driven(&player)->name));
                generate_town_map(&map);
                hero_driven(&player)->x = PLAYER_TOWN_START_X;
                hero_driven(&player)->y = PLAYER_TOWN_START_Y;
                log_msg("%s steps out of the wreck-trail and into the plaza.", hero_driven(&player)->name);
                state = STATE_TOWN;
                break;
            }

            case STATE_TOWN: {
                /* Same reasoning as the temple loop below: the frame is
                   skipped while the player is still ahead of us. Town is
                   cheaper to draw than a floor, but the plaza is where
                   somebody holds a direction to cross to a shop. */
                record_tick(&map, &player, g_record_last_key);
                if (!input_waiting())
                    draw_game_screen(&map, &player, hero_driven_c(&player), "The Lost City", false);
                /* Armed here and consumed by the next key, so that anything
                   this keypress opens is back to being a menu -- see
                   autoplay.h. Costs nothing when nobody is self-playing. */
                autoplay_scene(AP_SCENE_TOWN, &player, &map);
                int ch = getch();
                g_record_last_key = ch;
                int dx, dy;
                if (key_to_delta(ch, &dx, &dy)) {
                    handle_town_move(&player, &map, dx, dy, &state);
                } else if (ch == 'i') {
                    run_inventory(&player);
                } else if (ch == 'c') {
                    screen_character_sheet(&player, hero_driven_c(&player));
                } else if (ch == 'D') {
                    screen_display_settings();   /* the plaza is a fair test too */
                } else if (ch == '?') {
                    screen_help(true);
                } else if (ch == 'q') {
                    if (confirm_quit()) {
                        save_run(&player, &map);
                        state = STATE_QUIT;
                    }
                }
                break;
            }

            case STATE_TEMPLE: {
                char label[64];
                snprintf(label, sizeof(label), "Floor %d -- %s", player.floor, biome_name(map.biome));
                if (player.difficulty != DIFFICULTY_NORMAL) {
                    char tag[24];
                    snprintf(tag, sizeof(tag), "  [%s]", difficulty_name(player.difficulty));
                    strncat(label, tag, sizeof(label) - strlen(label) - 1);
                }
                /* Skip the frame when the player is already ahead of us.
                 *
                   This loop is draw-then-read-then-act, one frame per key,
                   which is right until a frame costs more than the terminal's
                   key-repeat interval. In tile mode it does: the queue then
                   fills faster than it drains, and every buffered key is still
                   spent -- one slow frame at a time -- after the key is
                   released. Reported from play as the character walking on for
                   seconds after you stop typing, and named exactly:
                   "it's like ghosting".

                   Every turn still happens, in order; only the *drawing* is
                   dropped, and only while there is another key behind this
                   one. The last key in a burst always draws, because by then
                   nothing is waiting. That is what lets the game keep up with
                   the typing instead of running on after it. */
                record_tick(&map, &player, g_record_last_key);
                if (!input_waiting())
                    draw_game_screen(&map, &player, hero_driven_c(&player), label, true);
                autoplay_scene(AP_SCENE_TEMPLE, &player, &map);
                int ch = getch();
                g_record_last_key = ch;

                /* A job in progress owns the turn, exactly as a recall
                   channel does. Anything that hurts you ends it, which is
                   what makes committing to ten turns a decision: you have to
                   have made the ground safe first. */
                if (hero_driven(&player)->work_turns_left > 0) {
                    if (ch == 27) {
                        work_abandon(&player, "You straighten up and leave it.");
                    } else {
                        int hp_before = hero_driven(&player)->hp;
                        hero_driven(&player)->work_turns_left--;
                        hero_tick_status(&player, hero_driven(&player));
                        if (hero_driven(&player)->hp <= 0) {
                            if (!guardian_catch(&player) && !companions_attempt_rescue(&player, &map)
                                && !party_pass_the_torch(&player)) { state = STATE_GAMEOVER; break; }
                            guardian_follow_through(&player, &map);
                        }
                        player.turns++;
                        bool died = false;
                        process_monster_turns(&map, &player, &died);
                        if (died) { state = STATE_GAMEOVER; break; }
                        districts_tick(&map, &player);
                        if (hero_driven(&player)->hp <= 0) {
                            if (!guardian_catch(&player) && !companions_attempt_rescue(&player, &map)
                                && !party_pass_the_torch(&player)) { state = STATE_GAMEOVER; break; }
                            guardian_follow_through(&player, &map);
                        }

                        if (hero_driven(&player)->hp < hp_before) {
                            work_abandon(&player, "Something gets a hand on you. The work is ruined.");
                        } else if (hero_driven(&player)->work_turns_left <= 0) {
                            work_finish(&player, &map);
                        }
                        compact_dead_monsters(&map);
                        maybe_respawn_monsters(&map, &player);
                        refresh_vision(&player, &map);
                    }
                    continue;
                }

                if (hero_driven(&player)->recall_channel_left > 0) {
                    if (ch == 27) {
                        hero_driven(&player)->recall_channel_left = 0;
                        log_msg("You steady your grip -- the recall fades.");
                    } else {
                        hero_driven(&player)->recall_channel_left--;
                        hero_tick_status(&player, hero_driven(&player));
                        if (hero_driven(&player)->hp <= 0 && !guardian_catch(&player)
                            && !companions_attempt_rescue(&player, &map)
                            && !party_pass_the_torch(&player)) {
                            state = STATE_GAMEOVER;
                            break;
                        }
                        player.turns++;
                        bool died = false;
                        process_monster_turns(&map, &player, &died);
                        if (died) {
                            state = STATE_GAMEOVER;
                            break;
                        }
                        if (hero_driven(&player)->recall_channel_left <= 0) {
                            generate_town_map(&map);
                            player.floor = 0;
                            hero_driven(&player)->x = TEMPLE_DOOR_X;
                            hero_driven(&player)->y = TEMPLE_DOOR_Y;
                            state = STATE_TOWN;
                            log_msg("The recall charm flares -- the temple lets go of you.");
                        } else {
                            maybe_respawn_monsters(&map, &player);
                            refresh_vision(&player, &map);
                        }
                    }
                    break;
                }

                int dx, dy;
                if (key_to_delta(ch, &dx, &dy)) {
                    if (consume_stun_if_active(&player)) {
                        resolve_after_player_turn(&player, &map, &state);
                    } else {
                        actor_turn(&player, hero_driven(&player), &map, dx, dy, &state);
                    }
                } else if (ch == 'i') {
                    int used = run_inventory(&player);
                    /* One turn per draught, and stop the moment something
                       ends the run -- five potions must not tick five turns
                       past a death. */
                    for (int u = 0; u < used && state == STATE_TEMPLE; u++)
                        resolve_after_player_turn(&player, &map, &state);
                } else if (ch == 'r') {
                    if (player.difficulty == DIFFICULTY_HARDCORE) {
                        log_msg("Recall is disabled in Hardcore mode. You're on your own.");
                    } else if (consume_recall_charm(&player)) {
                        hero_driven(&player)->recall_channel_left = hero_driven(&player)->recall_turns;
                        dda_note_retreat(&player);   /* that floor won */
                        log_msg("You crack the recall charm -- hold steady for %d turns.", hero_driven(&player)->recall_turns);
                    } else {
                        log_msg("You have no recall charm.");
                    }
                } else if (ch == '.' || ch == '5') {
                    /* Wait. Costs a turn and nothing else, which is exactly
                       what the mycelium's stillness rule needs to be usable:
                       without a key for doing nothing, "hold still for two
                       turns" is not a move a player can make. */
                    hero_driven(&player)->still_turns++;
                    if (district_at(&map, hero_driven(&player)->x, hero_driven(&player)->y) == DIST_MYCELIUM
                        && hero_driven(&player)->still_turns == MYCELIUM_FORGET_TURNS) {
                        log_msg("The mats go quiet under you. Whatever was listening has lost the thread.");
                    }
                    resolve_after_player_turn(&player, &map, &state);
                } else if (ch == 'g') {
                    work_begin(&player, &map);
                } else if (ch == 'x') {
                    run_auto_explore(&player, &map, &state);
                } else if (ch == 'c') {
                    screen_character_sheet(&player, hero_driven_c(&player));
                } else if (ch == '\t' && PARTY_SWITCH_ENABLED) {
                    /* Take control of the next body. Instant and free: the
                       point of it is that every fight becomes a question of
                       who is best placed *right now*, and a switch that costs
                       a turn is one you stop making.
                     *
                       Gated OFF until the other half exists -- see
                       PARTY_SWITCH_ENABLED in common.h. Camera, vision and the
                       panel already follow the driven body, but movement does
                       not, so enabling this now would hand you a view of a
                       hire and then walk the main character when you pressed
                       an arrow. A control scheme that lies about who it is
                       moving is worse than no control scheme. */
                    if (party_switch_next(&player)) {
                        const Hero *body = party_body(&player);
                        log_msg("You are %s.", body ? body->name : hero_driven(&player)->name);
                        refresh_vision(&player, &map);
                    } else {
                        log_msg("There is nobody else standing.");
                    }
                } else if (ch == 'M') {
                    screen_minimap(&map, hero_driven_c(&player));
                } else if (ch == 'R') {
                    run_records(&player);
                } else if (ch == 'D') {
                    /* The display screen, mid-run. It was reachable only from
                       the intro, which is the one moment a player has nothing
                       to compare against -- you cannot judge a render mode
                       from a title card, and switching meant abandoning the
                       floor you wanted to look at. render_set_mode() re-lays
                       the viewport itself, so there is nothing to do after. */
                    screen_display_settings();
                } else if (ch == '?') {
                    screen_help(false);
                } else if (ch == 'm') {
                    run_spell_menu(&player, &map, &state, label);
                } else if (ch == 's') {
                    if (hero_driven(&player)->last_spell_slot >= 0 && hero_driven(&player)->last_spell_slot < hero_driven(&player)->spell_count) {
                        bool boss_killed = false;
                        if (cast_spell_slot(&player, hero_driven(&player), &map, hero_driven(&player)->last_spell_slot, &boss_killed)) {
                            if (boss_killed) {
                                state = STATE_WIN;
                            } else {
                                resolve_after_player_turn(&player, &map, &state);
                            }
                        }
                    } else {
                        run_spell_menu(&player, &map, &state, label);
                    }
                } else if (ch == 'f') {
                    bool boss_killed = false;
                    if (fire_ranged(&player, hero_driven(&player), &map, &boss_killed)) {
                        if (boss_killed) {
                            state = STATE_WIN;
                        } else {
                            resolve_after_player_turn(&player, &map, &state);
                        }
                    }
                } else if (ch == 'a') {
                    bool boss_killed = false;
                    if (use_ability(&player, hero_driven(&player), &map, &boss_killed)) {
                        if (boss_killed) {
                            state = STATE_WIN;
                        } else {
                            resolve_after_player_turn(&player, &map, &state);
                        }
                    }
                } else if (ch == 'q') {
                    if (confirm_quit()) {
                        save_run(&player, &map);
                        state = STATE_QUIT;
                    }
                }
                break;
            }

            case STATE_GAMEOVER: {
                /* The descent is over either way -- the suspended run is gone
                   and the record stands. What differs is whether there is
                   anybody left to keep playing with. */
                delete_saved_run();
                best_floor = save_highscore_if_better(player.deepest_floor);
                save_class_record_if_better(hero_driven(&player)->class_id, player.deepest_floor);
                /* And onto the roster the barrows are populated from. Here
                   and not in STATE_WIN: the Gauntlet of the Fallen is for the
                   runs you lost, and a character who killed the Warden is not
                   one of them. */
                record_fallen(&player);

                bool permadeath = (player.difficulty == DIFFICULTY_HARDCORE);
                /* Read before the snapshot overwrites it. load_inn_snapshot
                   replaces the whole Player, floor included, and the floor in
                   a snapshot is always 0 because snapshots are taken in a bed
                   in town -- so "Whatever happened on floor %d" reported
                   floor 0 for every death the game has ever narrated. Found
                   by --silentrun, which died 150 times in one run and printed
                   the same wrong sentence 150 times. */
                int died_on = player.floor;
                if (!permadeath && load_inn_snapshot(&player)) {
                    /* Back to the character as they were when they last paid
                       for a bed -- level, gold, spells, gear, the lot -- and
                       back to town. The snapshot is not consumed: it stands
                       until the next rest, so the Inn is a checkpoint rather
                       than a one-shot revive. */
                    generate_town_map(&map);
                    hero_driven(&player)->x = PLAYER_TOWN_START_X;
                    hero_driven(&player)->y = PLAYER_TOWN_START_Y;
                    player.floor = 0;
                    hero_driven(&player)->hp = hero_driven(&player)->maxhp;
                    g_msg_log_count = 0;
                    log_msg("You wake in your room at the Inn, whole, and some hours older.");
                    log_msg("Whatever happened on floor %d, you are not carrying it.", died_on);
                    state = STATE_TOWN;
                    break;
                }

                screen_gameover(&player, hero_driven_c(&player), best_floor);
                if (permadeath) delete_inn_snapshot();
                state = STATE_INTRO;
                break;
            }

            case STATE_WIN: {
                delete_saved_run();
                if (player.deepest_floor < MAX_FLOOR) player.deepest_floor = MAX_FLOOR;
                best_floor = save_highscore_if_better(player.deepest_floor);
                save_class_record_if_better(hero_driven(&player)->class_id, player.deepest_floor);
                screen_win(&player, hero_driven_c(&player), best_floor);
                state = STATE_INTRO;
                break;
            }

            default:
                break;
        }
    }

    record_close();

    endwin();
    return 0;
}
