#include "combat.h"
#include "dda.h"
#include "bands.h"
#include "mapgen.h"
#include "render.h"
#include "companions.h"
#include "gearsets.h"
#include "monsters.h"
#include "kitchen.h"
#include "save.h"   /* the fallen roster the gauntlet raises */

static const int MOVE_DX[8] = {-1, 1, 0, 0, -1, 1, -1, 1};
static const int MOVE_DY[8] = { 0, 0,-1, 1, -1,-1,  1, 1};

static void proving_note_kill(Map *m, Player *p, int x, int y);

Monster *find_nearest_target_excluding(Map *m, const Player *p, int radius, const Monster *exclude) {
    Monster *best = NULL;
    int best_dist2 = radius * radius + 1;
    for (int i = 0; i < m->monster_count; i++) {
        Monster *mo = &m->monsters[i];
        if (!mo->alive || mo == exclude) continue;
        int dx = mo->x - hero_driven_c(p)->x, dy = mo->y - hero_driven_c(p)->y;
        int dist2 = dx * dx + dy * dy;
        if (dist2 > best_dist2) continue;
        if (!m->tiles[mo->y][mo->x].visible) continue;
        if (!line_of_sight(m, hero_driven_c(p)->x, hero_driven_c(p)->y, mo->x, mo->y)) continue;
        best = mo;
        best_dist2 = dist2;
    }
    return best;
}

bool crystal_ricochet(Map *m, Player *p, int magnitude, const char *what) {
    if (m->floor_num <= 0) return false;
    if (district_at(m, hero_driven(p)->x, hero_driven(p)->y) != DIST_CRYSTAL) return false;
    if (rand() % 100 >= CRYSTAL_RICOCHET_PCT) return false;

    int dmg = magnitude / 2;
    if (dmg < 1) dmg = 1;
    dmg -= dmg * (hero_driven(p)->hazard_resist_pct + hero_driven(p)->set_bonus_hazard_resist) / 100;
    if (dmg < 1) dmg = 1;

    hero_driven(p)->hp -= dmg;
    if (hero_driven(p)->hp < 0) hero_driven(p)->hp = 0;
    log_msg("The %s glances off a crystal and comes back at you -- %d damage.", what, dmg);
    return true;
}

int displace_actor(Map *m, int *x, int *y, int dx, int dy, int dist,
                   const Monster *ignore, const Player *avoid, const Hero *self) {
    /* A zero direction is a no-op, not `dist` steps onto the tile you are
       already standing on. Cheap to get wrong and it would have shipped as
       "the current pushed you nowhere, three times". */
    if (dx == 0 && dy == 0) return 0;

    int moved = 0;
    for (int step = 0; step < dist; step++) {
        int nx = *x + dx, ny = *y + dy;
        if (nx < 0 || nx >= MAP_W || ny < 0 || ny >= MAP_H) break;
        if (!is_walkable_player(m, nx, ny)) break;

        /* Never carried onto a staircase. Landing on one takes it, and being
           swept to the next floor by a current you did not choose to enter is
           not a mechanic, it is a bug report. */
        TileType t = m->tiles[ny][nx].type;
        if (t == TILE_STAIRS_DOWN || t == TILE_STAIRS_UP) break;
        /* Nor down a hole. Same reasoning as the staircases above: being swept
           to the next floor by a current you did not choose to enter is not a
           mechanic, it is a bug report -- and this one costs health on the way. */
        if (t == TILE_PIT) break;

        /* Nor into somebody. Two actors on one tile is the one invariant the
           whole combat system assumes. */
        Monster *occupant = monster_at(m, nx, ny);
        if (occupant && occupant != ignore && occupant->alive) break;
        /* No body, driven or not. This checked only the actor the human was
           attached to, so a belt would carry a monster straight onto a hire --
           two actors on one tile, which is the one thing the combat system
           assumes never happens. The hires were invisible to it for the same
           reason they were invisible to everything else before the Hero
           refactor: the code knew about "the player" and not about bodies. */
        if (avoid) {
            bool occupied = false;
            for (int b = 0; b < MAX_PARTY; b++) {
                const Hero *o = &avoid->party[b];
                if (o == self || !hero_is_up(o)) continue;
                if (o->x == nx && o->y == ny) { occupied = true; break; }
            }
            if (occupied) break;
        }

        *x = nx; *y = ny;
        moved++;
    }
    return moved;
}

/* ---- the arena ---------------------------------------------------------
 * Step onto the sand and it starts. Ten waves; after the fifth the gates
 * open again, so pressing on for the prize is a decision you make five times
 * rather than a door that locks behind you once.
 */
static void arena_seal(Map *m, const MapDistrict *d, bool shut) {
    /* The ring of seating is already wall; this closes the one gate. */
    int cy = d->y + d->h / 2;
    for (int x = d->x; x < d->x + d->w; x++) {
        if (x < 1 || x >= MAP_W - 1) continue;
        TileType t = m->tiles[cy][x].type;
        if (shut) {
            if (t == TILE_FLOOR && (x < d->x + 2 || x > d->x + d->w - 3))
                m->tiles[cy][x].type = TILE_SEALED_DOOR;
        } else if (t == TILE_SEALED_DOOR) {
            m->tiles[cy][x].type = TILE_FLOOR;
        }
    }
}

static int arena_live_enemies(const Map *m, const MapDistrict *d) {
    int n = 0;
    for (int i = 0; i < m->monster_count; i++) {
        const Monster *mo = &m->monsters[i];
        if (!mo->alive) continue;
        if (mo->x < d->x || mo->x >= d->x + d->w) continue;
        if (mo->y < d->y || mo->y >= d->y + d->h) continue;
        n++;
    }
    return n;
}

static void arena_spawn_wave(Map *m, const MapDistrict *d, int wave, int floor_num) {
    int want = ARENA_BASE_ENEMIES + wave;
    for (int k = 0; k < want && m->monster_count < MAX_MONSTERS - 4; k++) {
        for (int tries = 0; tries < 40; tries++) {
            int x = d->x + 1 + rand() % (d->w - 2);
            int y = d->y + 1 + rand() % (d->h - 2);
            if (m->tiles[y][x].type != TILE_FLOOR) continue;
            if (monster_at(m, x, y)) continue;
            /* Each wave fights as if the floor were deeper than it is. */
            Monster mo = make_monster_for_floor(floor_num + wave * 2, x, y);
            mo.aggro = true;
            m->monsters[m->monster_count++] = mo;
            break;
        }
    }
}

static void arena_tick(Map *m, Player *p) {
    int here = district_index_at(m, hero_driven(p)->x, hero_driven(p)->y);
    bool on_sand = here >= 0 && m->districts[here].kind == DIST_ARENA;

    if (m->arena_wave == 0) {
        if (!on_sand || m->arena_paid) return;
        m->arena_district = here;
        m->arena_wave = 1;
        arena_seal(m, &m->districts[here], true);
        arena_spawn_wave(m, &m->districts[here], 1, m->floor_num);
        log_msg("The gates come down. Something is let in at the far end.");
        log_msg("Wave 1 of %d. They open again after %d.", ARENA_WAVES, ARENA_FREE_AFTER);
        return;
    }

    if (m->arena_district < 0) return;
    const MapDistrict *d = &m->districts[m->arena_district];
    if (arena_live_enemies(m, d) > 0) return;

    if (m->arena_wave >= ARENA_WAVES) {
        arena_seal(m, d, false);
        m->arena_wave = ARENA_WAVES + 1;
        if (!m->arena_paid) {
            m->arena_paid = true;
            /* Cast explicit: Biome is unsigned and the setter takes int.
               The three baseline sign-conversion warnings are enough. */
            apply_set_gear(hero_driven(p), (int)m->biome, rand() % RELIC_KIND_COUNT);
            int purse = player_gain_gold(p, 400 + m->floor_num * 60);
            p->training_writs++;
            log_msg("The last of them goes down. The gates grind open.");
            log_msg("A chest comes up through the sand: set gear, %d gold,", purse);
            log_msg("and a writ of training -- the Pit School honours those.");
        }
        return;
    }

    m->arena_wave++;
    if (m->arena_wave > ARENA_FREE_AFTER) arena_seal(m, d, false);
    arena_spawn_wave(m, d, m->arena_wave, m->floor_num);
    if (m->arena_wave == ARENA_FREE_AFTER + 1)
        log_msg("The gates lift. You may walk out -- or take wave %d.", m->arena_wave);
    else
        log_msg("Wave %d of %d.", m->arena_wave, ARENA_WAVES);
}

/* ---- the gauntlet of the fallen ----------------------------------------
 * The arena's structure, and a different question. There the enemies are
 * whatever the floor could produce; here they are read off the roster of runs
 * this save directory has actually lost, so the thing walking at you has a
 * name you chose and died on a floor you remember.
 *
 * One ghost per wave rather than a crowd: a ghost is a person, and five of
 * them at once would be a mob. It also keeps the fight legible -- you are
 * meant to read the name.
 */
static void gauntlet_seal(Map *m, const MapDistrict *d, bool shut, const Player *p) {
    /* The whole rim, not the one door carve_gauntlet cut. The two roads
       mapgen runs to a district's anchor punch their own holes through the
       wall, so a seal that knew only about the door would have two ways
       around it -- which is the leak the arena's seal still has and which is
       not worth reproducing. */
    for (int y = d->y; y < d->y + d->h; y++) {
        for (int x = d->x; x < d->x + d->w; x++) {
            if (x < 1 || x >= MAP_W - 1 || y < 1 || y >= MAP_H - 1) continue;
            bool rim = (x == d->x || x == d->x + d->w - 1 ||
                        y == d->y || y == d->y + d->h - 1);
            if (!rim) continue;
            /* Never seal the tile somebody is standing on -- *anybody*, not
               just whoever holds the controller. A body on a sealed door can
               still step off it, so this is not a stuck state, but it reads as
               one, and a rule that protected the driven body and walled its
               hires into the doorway would be the same asymmetry the prism
               floor had. */
            bool stood_on = false;
            for (int b = 0; b < MAX_PARTY; b++) {
                const Hero *o = &p->party[b];
                if (!hero_is_up(o)) continue;
                if (o->x == x && o->y == y) { stood_on = true; break; }
            }
            if (stood_on) continue;
            TileType t = m->tiles[y][x].type;
            if (shut) {
                if (t == TILE_FLOOR || t == TILE_BRIDGE) m->tiles[y][x].type = TILE_SEALED_DOOR;
            } else if (t == TILE_SEALED_DOOR) {
                m->tiles[y][x].type = TILE_FLOOR;
            }
        }
    }
}

static int gauntlet_live_enemies(const Map *m, const MapDistrict *d) {
    int n = 0;
    for (int i = 0; i < m->monster_count; i++) {
        const Monster *mo = &m->monsters[i];
        if (!mo->alive) continue;
        if (mo->x < d->x || mo->x >= d->x + d->w) continue;
        if (mo->y < d->y || mo->y >= d->y + d->h) continue;
        n++;
    }
    return n;
}

/* Stands one ghost up. Returns the gold it is carrying, or 0 if the roster
   has run out -- which ends the fight rather than failing it. */
static int gauntlet_raise(Map *m, const MapDistrict *d, int wave, int floor_num) {
    FallenRecord ring[FALLEN_MAX];
    int have = load_fallen(ring, FALLEN_MAX);
    if (wave < 1 || wave > have) return 0;

    const FallenRecord *fr = &ring[wave - 1];   /* newest death first */
    if (m->monster_count >= MAX_MONSTERS - 2) return 0;

    for (int tries = 0; tries < 80; tries++) {
        int x = d->x + 1 + rand() % (d->w - 2);
        int y = d->y + 1 + rand() % (d->h - 2);
        if (m->tiles[y][x].type != TILE_FLOOR) continue;
        if (monster_at(m, x, y)) continue;

        /* Built from the record rather than from the floor. A ghost that
           scaled to where you are now would just be another monster with a
           name on it; the point is that it is exactly as strong as the run
           that made it, which is why a shallow death makes a soft ghost and
           the one that got to 47 does not. */
        Monster mo;
        memset(&mo, 0, sizeof mo);
        snprintf(mo.name, sizeof mo.name, "%s", fr->name[0] ? fr->name : "the nameless");
        mo.x = x; mo.y = y;
        mo.maxhp = fr->maxhp > 1 ? fr->maxhp : 10;
        mo.hp    = mo.maxhp;
        mo.atk   = fr->atk > 0 ? fr->atk : 1;
        mo.def   = fr->def > 0 ? fr->def : 0;
        mo.alive = true;
        mo.aggro = true;
        mo.is_elite = true;          /* it was a person, not a rat */
        mo.glyph = '@';              /* and it is drawn as one */
        mo.color_pair = CP_DIST_GAUNTLET;
        /* No experience. Killing what you used to be is not training, and an
           XP source that scales with your own past best is a loop. */
        mo.xp_reward = 0;
        mo.gold_reward = fr->gold > 0 ? fr->gold : 0;
        (void)floor_num;

        m->monsters[m->monster_count++] = mo;
        log_msg("%s, who got as far as floor %d, stands up.", mo.name, fr->floor);
        return mo.gold_reward;
    }
    return 0;
}

static void gauntlet_tick(Map *m, Player *p) {
    int here = district_index_at(m, hero_driven(p)->x, hero_driven(p)->y);
    bool inside = here >= 0 && m->districts[here].kind == DIST_GAUNTLET;

    if (m->gauntlet_wave == 0) {
        if (!inside || m->gauntlet_paid) return;
        FallenRecord ring[FALLEN_MAX];
        int have = load_fallen(ring, FALLEN_MAX);
        if (have < GAUNTLET_MIN_GHOSTS) return;   /* nobody is buried here */

        m->gauntlet_district = here;
        m->gauntlet_wave = 1;
        gauntlet_seal(m, &m->districts[here], true, p);
        gauntlet_raise(m, &m->districts[here], 1, m->floor_num);
        log_msg("The door goes down behind you. This is not a tomb, it is a queue.");
        return;
    }

    if (m->gauntlet_district < 0) return;
    const MapDistrict *d = &m->districts[m->gauntlet_district];
    if (gauntlet_live_enemies(m, d) > 0) return;

    int limit = GAUNTLET_MAX_GHOSTS;
    {
        FallenRecord ring[FALLEN_MAX];
        int have = load_fallen(ring, FALLEN_MAX);
        if (have < limit) limit = have;
    }

    if (m->gauntlet_wave >= limit) {
        gauntlet_seal(m, d, false, p);
        m->gauntlet_wave = limit + 1;
        if (!m->gauntlet_paid) {
            m->gauntlet_paid = true;
            /* Nothing is granted here. The gold each ghost was carrying was
               already paid as its own drop when it went down, which is the
               reward §3.1 asked for and the whole of it -- no set piece, no
               writ, and above all no attribute point. */
            log_msg("The last of them lies down again. The door grinds up.");
            log_msg("You are the only one of you still walking.");
        }
        return;
    }

    m->gauntlet_wave++;
    gauntlet_raise(m, d, m->gauntlet_wave, m->floor_num);
}

/* ---- the eye of the storm ----------------------------------------------
 * The rod field with the polarity reversed. There, one tile is about to be
 * dangerous and you stand clear of it. Here, one quarter is safe and the rest
 * is not, so you are not dodging a hazard -- you are riding a shelter that
 * will not wait for you.
 *
 * Which quarter is derived from the floor's turn counter rather than stored,
 * so it costs nothing to remember, survives a save without a field, and is a
 * rhythm the player can learn rather than a roll they cannot.
 */
static int eye_safe_quadrant(const Map *m) {
    return (m->turns_on_floor / EYE_ROTATE_EVERY) % 4;
}

static bool eye_sheltered(const MapDistrict *d, const Map *m, int x, int y) {
    int q = eye_safe_quadrant(m);
    bool right = (x >= d->x + d->w / 2);
    bool below = (y >= d->y + d->h / 2);
    int here = (below ? 2 : 0) + (right ? 1 : 0);
    return here == q;
}

static void eye_tick(Map *m, Player *p) {
    if (m->turns_on_floor % EYE_STRIKE_EVERY != 0) return;

    for (int i = 0; i < m->district_count; i++) {
        const MapDistrict *d = &m->districts[i];
        if (d->kind != DIST_EYE) continue;

        /* Everything caught out, on both sides. A shelter you can share with
           what is chasing you is a different fight from one you cannot, and
           this one you can -- which is what makes standing in the safe
           quarter a decision rather than a rest. */
        for (int b = 0; b < MAX_PARTY; b++) {
            Hero *h = &p->party[b];
            if (!hero_is_up(h)) continue;
            if (district_index_at(m, h->x, h->y) != i) continue;
            if (eye_sheltered(d, m, h->x, h->y)) continue;
            int dmg = EYE_DAMAGE;
            dmg -= dmg * (h->hazard_resist_pct + h->set_bonus_hazard_resist) / 100;
            if (dmg < 1) dmg = 1;
            h->hp -= dmg;
            if (h->hp < 0) h->hp = 0;
            if (h == hero_driven(p))
                log_msg("The ceiling turns over you -- %d damage. The quiet is elsewhere.", dmg);
        }
        for (int k = 0; k < m->monster_count; k++) {
            Monster *mo = &m->monsters[k];
            if (!mo->alive) continue;
            if (district_index_at(m, mo->x, mo->y) != i) continue;
            if (eye_sheltered(d, m, mo->x, mo->y)) continue;
            monster_take_damage(p, hero_driven(p), m, mo, EYE_DAMAGE, NULL);
        }
    }
}

/* ---- the mirror --------------------------------------------------------
 * Stand on the glass and it stands something up: you, at half your health and
 * half your damage. Half rather than parity on purpose -- a mirror match at
 * parity is a coin flip that ignores every decision the player made about
 * their build, which is the opposite of what a mirror is for.
 *
 * Built as a Monster from the driven Hero, exactly as the barrow's ghosts
 * are, so it needs no new actor machinery and dies like anything else.
 */
static void mirror_tick(Map *m, Player *p) {
    int here = district_index_at(m, hero_driven(p)->x, hero_driven(p)->y);
    if (here < 0 || m->districts[here].kind != DIST_MIRROR) return;
    if (m->districts[here].state != 0) return;          /* already answered */
    if (m->monster_count >= MAX_MONSTERS - 2) return;

    const Hero *h = hero_driven_c(p);
    const MapDistrict *d = &m->districts[here];

    for (int tries = 0; tries < 60; tries++) {
        int x = d->x + 1 + rand() % (d->w - 2);
        int y = d->y + 1 + rand() % (d->h - 2);
        if (m->tiles[y][x].type != TILE_FLOOR) continue;
        if (monster_at(m, x, y)) continue;
        if (x == h->x && y == h->y) continue;

        Monster mo;
        memset(&mo, 0, sizeof mo);
        snprintf(mo.name, sizeof mo.name, "%s in the glass",
                 h->name[0] ? h->name : "something");
        mo.x = x; mo.y = y;
        mo.maxhp = h->maxhp * MIRROR_HP_PCT / 100;
        if (mo.maxhp < 1) mo.maxhp = 1;
        mo.hp  = mo.maxhp;
        mo.atk = hero_eff_atk(h) * MIRROR_DAMAGE_PCT / 100;
        if (mo.atk < 1) mo.atk = 1;
        mo.def = hero_eff_def(h);
        mo.alive = true;
        mo.aggro = true;
        mo.is_elite = true;
        mo.glyph = '@';
        mo.color_pair = CP_DIST_MIRROR;
        /* It is you, so it is worth what you are worth and no more: no
           experience, because levelling off your own reflection is a loop,
           and no purse, because it never carried one. */
        mo.xp_reward = 0;
        mo.gold_reward = 0;

        m->monsters[m->monster_count++] = mo;
        m->districts[here].state = 1;
        log_msg("The glass keeps pace, and then steps out of it.");
        return;
    }
}

/* ---- the proving ground ------------------------------------------------
 * Kills made on the marked ground, under whatever rule the ground keeps.
 * Reach PROVE_KILLS_WANTED and it pays a writ of training -- the arena's
 * currency, and the right one: §3.1 says four times over that a district pays
 * in School credit or in nothing, never in raw attribute points.
 *
 * Counted here rather than in monster_take_damage() so that the count is of
 * things that died *on the ground*, which is what the rule is about.
 */
static void proving_note_kill(Map *m, Player *p, int x, int y) {
    int i = district_index_at(m, x, y);
    if (i < 0 || m->districts[i].kind != DIST_PROVING) return;
    MapDistrict *d = &m->districts[i];
    if (proving_paid(d->state)) return;

    int kills = proving_kills(d->state) + 1;
    d->state = (d->state & (PROVE_KIND_MASK | PROVE_PAID_BIT)) | (kills << PROVE_KILLS_SHIFT);

    if (kills < PROVE_KILLS_WANTED) return;

    d->state |= PROVE_PAID_BIT;
    p->training_writs++;
    int purse = player_gain_gold(p, 250 + m->floor_num * 40);
    log_msg("That is the last of them, on their terms and not yours.");
    log_msg("A writ of training, and %d gold. The Pit School honours these.", purse);
}

const char *proving_rule_name(int state) {
    switch (proving_kind(state)) {
        case PROVE_MELEE:  return "blades only";
        case PROVE_ARCANE: return "the arts only";
        default:           return "no weapons";
    }
}

/* The rods do not care who is standing near them, which is the whole appeal:
   it is the first hazard in the game you can put something else in front of. */
void districts_tick(Map *m, Player *p) {
    if (m->floor_num <= 0) return;

    arena_tick(m, p);
    gauntlet_tick(m, p);
    eye_tick(m, p);
    mirror_tick(m, p);

    /* The ground you are standing on is a stance, and it is re-read every
       turn rather than applied as a buff -- step off and it is gone, which is
       the entire mechanic.
     *
       For every body, because the rule is about the ground. It read and
       cleared the *driven* body's stance only, which was wrong in both
       directions: a hire on coloured ground got nothing, and -- the sharp half
       -- a body that was driven while standing on the prism kept its swing for
       the rest of the run, because the only code that clears the stance runs
       for whoever holds the controller *now*. Tab away from red ground and the
       character you left was permanently +50%/-50%, anywhere on the floor.

       `hero_eff_atk()` and `hero_eff_def()` have applied `stance_*` for all
       six actors since §92. Only the setting of it was still written as though
       there were one body, which is the same premise the Hero refactor was
       supposed to have deleted -- found by grepping this file for
       `hero_driven(p)` inside the per-turn world. */
    for (int b = 0; b < MAX_PARTY; b++) {
        Hero *h = &p->party[b];
        if (!hero_is_up(h)) continue;
        h->stance_atk_pct = 0;
        h->stance_def_pct = 0;
        switch (m->tiles[h->y][h->x].type) {
            case TILE_PRISM_RED:  h->stance_atk_pct =  PRISM_SWING;
                                  h->stance_def_pct = -PRISM_SWING; break;
            case TILE_PRISM_BLUE: h->stance_atk_pct = -PRISM_SWING;
                                  h->stance_def_pct =  PRISM_SWING; break;
            case TILE_PRISM_GREEN:
                if (h->hp < h->maxhp) {
                    h->hp += PRISM_REGEN;
                    if (h->hp > h->maxhp) h->hp = h->maxhp;
                }
                break;
            default: break;
        }
    }

    /* The blood marsh mends them. Not you -- you are not what it is for. */
    for (int i = 0; i < m->monster_count; i++) {
        Monster *mo = &m->monsters[i];
        if (!mo->alive || mo->hp >= mo->maxhp) continue;
        if (m->tiles[mo->y][mo->x].type != TILE_BLOODPOOL) continue;
        mo->hp += BLOODPOOL_REGEN;
        if (mo->hp > mo->maxhp) mo->hp = mo->maxhp;
    }

    /* Belts. Same helper as the current, opposite feeling: a current is a
       shortcut you step into, a belt is a lane you have to work with. */
    {
        int bdx, bdy;
        /* Every body, not just the one being driven.
         *
           The belts and the current moved the human's actor and every monster
           on the floor, and left the other five party members standing in a
           running river as though the water were scenery. That is the premise
           the Hero refactor was supposed to have deleted -- "the player" as a
           thing the world acts on, and hires as things it does not -- and it
           survived here because this code was written against `p->x`. A hire
           in a channel now goes where the channel goes. */
        for (int b = 0; b < MAX_PARTY; b++) {
            Hero *h = &p->party[b];
            if (!hero_is_up(h)) continue;
            belt_flow_at(m, h->x, h->y, &bdx, &bdy);
            if (!bdx && !bdy) continue;
            int went = displace_actor(m, &h->x, &h->y, bdx, bdy, BELT_PUSH, NULL, p, h);
            if (went == 0 && h == hero_driven(p)) log_msg("The belt grinds you against the stop.");
        }
        for (int i = 0; i < m->monster_count; i++) {
            Monster *mo = &m->monsters[i];
            if (!mo->alive) continue;
            belt_flow_at(m, mo->x, mo->y, &bdx, &bdy);
            if (!bdx && !bdy) continue;
            displace_actor(m, &mo->x, &mo->y, bdx, bdy, BELT_PUSH, mo, p, NULL);
        }
    }

    /* The water moves whatever is standing in it -- you and them alike. Read
       from the district the actor is *in*, so two aqueducts on one floor can
       run in different directions (EVALUATION §44). */
    {
        for (int b = 0; b < MAX_PARTY; b++) {
            Hero *h = &p->party[b];
            if (!hero_is_up(h)) continue;
            int di = district_index_at(m, h->x, h->y);
            if (di < 0 || m->districts[di].kind != DIST_AQUEDUCT) continue;
            if (m->tiles[h->y][h->x].type != TILE_CURRENT) continue;
            const MapDistrict *d = &m->districts[di];
            int went = displace_actor(m, &h->x, &h->y, d->flow_dx, d->flow_dy,
                                      CURRENT_PUSH, NULL, p, h);
            /* Said out loud only for the body the human is looking through.
               Five hires being carried is the water working; five log lines a
               turn about it is the log becoming unreadable. */
            if (h != hero_driven(p)) continue;
            if (went > 0) log_msg("The current takes you %d tiles.", went);
            else          log_msg("The current shoves you against the channel wall.");
        }

        for (int i = 0; i < m->monster_count; i++) {
            Monster *mo = &m->monsters[i];
            if (!mo->alive) continue;
            int mi = district_index_at(m, mo->x, mo->y);
            if (mi < 0 || m->districts[mi].kind != DIST_AQUEDUCT) continue;
            if (m->tiles[mo->y][mo->x].type != TILE_CURRENT) continue;
            const MapDistrict *d = &m->districts[mi];
            displace_actor(m, &mo->x, &mo->y, d->flow_dx, d->flow_dy,
                           CURRENT_PUSH, mo, p, NULL);
        }
    }

    /* One system, two skins -- the Storm-Cage and the vent field. Both are
       "telegraphed, timed, area damage, harvestable"; the rods stand in a
       field and the vents are cracks in rock, and that is the whole of the
       difference. Kept as one tick rather than two on purpose: the day these
       diverge is the day one of them quietly stops being maintained. */
    bool storm_here = false, vents_here = false;
    for (int i = 0; i < m->district_count; i++) {
        if (m->districts[i].kind == DIST_STORM)      storm_here = true;
        if (m->districts[i].kind == DIST_GEOTHERMAL) vents_here = true;
    }
    if (!storm_here && !vents_here) return;

    if (m->storm_countdown > 0) m->storm_countdown--;

    if (m->storm_countdown == 0 && m->storm_x >= 0) {
        int rx = m->storm_x, ry = m->storm_y;
        m->storm_x = m->storm_y = -1;

        int ddx = hero_driven(p)->x - rx, ddy = hero_driven(p)->y - ry;
        if (ddx * ddx + ddy * ddy <= STORM_BLAST_RADIUS * STORM_BLAST_RADIUS) {
            int dmg = STORM_DAMAGE;
            dmg -= dmg * (hero_driven(p)->hazard_resist_pct + hero_driven(p)->set_bonus_hazard_resist) / 100;
            if (dmg < 1) dmg = 1;
            hero_driven(p)->hp -= dmg;
            if (hero_driven(p)->hp < 0) hero_driven(p)->hp = 0;
            log_msg(m->storm_is_vent
                    ? "The vent lets go and you are far too close -- %d damage."
                    : "The rod takes the bolt and you are far too close -- %d damage.", dmg);
        }

        /* Anything touching a struck rod is simply gone. Lure something onto
           it and the floor does the work. */
        int killed = 0;
        for (int i = 0; i < m->monster_count; i++) {
            Monster *mo = &m->monsters[i];
            if (!mo->alive) continue;
            int mdx = mo->x - rx, mdy = mo->y - ry;
            if (mdx * mdx + mdy * mdy > 2) continue;   /* the eight around it */
            monster_take_damage(p, hero_driven(p), m, mo, mo->hp, NULL);
            killed++;
        }
        if (killed > 0)
            log_msg(m->storm_is_vent
                    ? "The blast takes %d of them with it."
                    : "The arc earths itself through %d of them.", killed);
    }

    /* Line up the next one, and say which. */
    if (m->storm_x < 0) {
        for (int tries = 0; tries < 60; tries++) {
            int i = rand() % m->district_count;
            const MapDistrict *d = &m->districts[i];
            bool vent = (d->kind == DIST_GEOTHERMAL);
            if (d->kind != DIST_STORM && !vent) continue;
            int x = d->x + rand() % d->w, y = d->y + rand() % d->h;
            if (m->tiles[y][x].type != (vent ? TILE_VENT : TILE_ROD)) continue;
            m->storm_x = x; m->storm_y = y;
            m->storm_is_vent = vent;
            m->storm_countdown = STORM_STRIKE_EVERY;
            break;
        }
    } else if (m->storm_countdown == 1 && m->tiles[m->storm_y][m->storm_x].visible) {
        log_msg(m->storm_is_vent
                ? "A vent starts to draw. Whatever is beside it has one turn."
                : "A rod begins to sing. Whatever is beside it has one turn.");
    }
}

Monster *find_nearest_target(Map *m, const Player *p, int radius) {
    return find_nearest_target_excluding(m, p, radius, NULL);
}

int damage_all_in_reach(Map *m, Player *p, int radius, int dmg, bool *out_boss_killed) {
    int hits = 0;
    for (int i = 0; i < m->monster_count; i++) {
        Monster *mo = &m->monsters[i];
        if (!mo->alive) continue;
        int dx = mo->x - hero_driven(p)->x, dy = mo->y - hero_driven(p)->y;
        if (dx * dx + dy * dy > radius * radius) continue;
        if (!m->tiles[mo->y][mo->x].visible) continue;
        if (!line_of_sight(m, hero_driven(p)->x, hero_driven(p)->y, mo->x, mo->y)) continue;
        monster_take_damage(p, hero_driven(p), m, mo, dmg, out_boss_killed);
        hits++;
    }
    return hits;
}

/* Applies dmg to mo and handles death -- kill count, gold, xp, quest
   completion, boss flag -- the same way regardless of whether the damage
   came from a melee swing or a spell. */
/* Hurt one of them in the comb and the rest find out. Costs the neighbours a
   point each -- the hive shares the wound as well as the news -- and there is
   no equivalent of the mycelium's stillness here. That asymmetry is the whole
   design: one district where holding still saves you, one where nothing does. */
static void hive_alarm(Map *m, const Monster *hurt) {
    /* Scoped to *this* comb, by index rather than by kind. A floor can hold
       more than one district of a kind -- measured at a quarter of floors on
       the larger worlds, and as many as fifteen of one kind on a single Well
       floor -- and news does not travel between two separate hives. */
    int comb = district_index_at(m, hurt->x, hurt->y);
    if (comb < 0) return;

    for (int i = 0; i < m->monster_count; i++) {
        Monster *other = &m->monsters[i];
        if (!other->alive || other == hurt) continue;
        int ddx = other->x - hurt->x, ddy = other->y - hurt->y;
        if (ddx * ddx + ddy * ddy > HIVE_ALARM_RADIUS * HIVE_ALARM_RADIUS) continue;
        if (district_index_at(m, other->x, other->y) != comb) continue;
        other->aggro = true;
        if (other->hp > 1) other->hp -= 1;
    }
}

/* Set immediately before the one melee call and consumed on the way in, so
   it cannot leak into the next source of damage. A parameter would be more
   honest, but every one of the fourteen call sites would have to pass
   `false` and the one that mattered would be no easier to find. The latch
   is read once, cleared once, and both happen in this file. */
static bool g_blade_clean = false;

/* `killer` is the actor whose blow this is. Everything that belongs to the
   body -- the kill tally, the experience -- goes to them; everything that
   belongs to the world goes to the party, whoever swung.
 *
   It used to take no killer and credit hero_driven(p), because only the player
   ever reached it: the AI's own strike ended at `mo->hp -= dmg` and did its own
   bookkeeping. So a monster killed by a hire produced no gold, no bounty
   progress, no larder, and -- the one that matters -- **no boss kill**. A hire
   landing the last blow on the Warden left it dead on the floor with the run
   still going, because STATE_WIN is reached only through out_boss_killed and
   nothing else ever looks. Measured: 3 of 3 on a probe.

   May be called with killer == NULL for damage with no author (a hazard, a
   collapsing floor); the world half still happens and nobody is credited. */
void monster_take_damage(Player *p, Hero *killer, Map *m, Monster *mo, int dmg, bool *out_boss_killed) {
    bool clean = g_blade_clean;
    g_blade_clean = false;

    if (out_boss_killed) *out_boss_killed = false;

    if (dmg > 0 && district_at(m, mo->x, mo->y) == DIST_HIVE) hive_alarm(m, mo);

    /* Break the quiet and it stays broken: everything in the quarter, not
       just what could see or hear it. You had the option of walking through. */
    if (dmg > 0 && district_at(m, mo->x, mo->y) == DIST_QUIET) {
        /* This quarter, by index. Matching on *kind* woke every quiet quarter
           on the floor at once -- fine when a kind appeared at most once, and
           badly wrong on a Well floor that can carry fifteen of them. */
        int quarter = district_index_at(m, mo->x, mo->y);
        int woke = 0;
        for (int i = 0; i < m->monster_count && quarter >= 0; i++) {
            Monster *other = &m->monsters[i];
            if (!other->alive || other->aggro) continue;
            if (district_index_at(m, other->x, other->y) != quarter) continue;
            other->aggro = true;
            woke++;
        }
        if (woke > 0) log_msg("The quiet goes out of the place. All %d of them are up.", woke);
    }

    /* Break the watch. One blow does it, wherever it landed and whoever threw
       it -- a hire's arrow counts, and so does a spell that clipped a warden
       on its way past. That is the whole mechanic: the district asks you not
       to start anything, and it does not care about your reasons.
     *
       Scoped by index like the two above, so one broken watch on a Well floor
       does not blow the other fourteen. */
    if (dmg > 0) {
        int post = district_index_at(m, mo->x, mo->y);
        if (post >= 0 && m->districts[post].kind == DIST_WATCH &&
            m->districts[post].state == WATCH_KEEPING) {
            m->districts[post].state = WATCH_BROKEN;
            int woke = 0;
            for (int i = 0; i < m->monster_count; i++) {
                Monster *other = &m->monsters[i];
                if (!other->alive || other->aggro) continue;
                if (district_index_at(m, other->x, other->y) != post) continue;
                other->aggro = true;
                woke++;
            }
            log_msg("A shout goes up, and it is taken up all the way down the line.");
            if (woke > 0) log_msg("The watch is broken. %d of them are coming.", woke);
        }
    }

    mo->hp -= dmg;
    if (mo->hp > 0) return;

    mo->hp = 0;
    mo->alive = false;
    if (killer) killer->kills++;

    /* Counted where it fell: the proving ground's rule is about what dies on
       the marked ground, not about what you happened to kill while standing
       on it. */
    proving_note_kill(m, p, mo->x, mo->y);

    int gold = player_gain_gold(p, mo->gold_reward);
    log_msg("The %s falls. (+%d gold)", mo->name, gold);

    /* Only a blade leaves anything worth carrying up. */
    if (clean) {
        int cut = meat_on_clean_kill(p, p->floor, mo->maxhp, mo->is_boss);
        if (cut >= 0) log_msg("You take a %s cut off the %s.", meat_name(cut), mo->name);
    }

    grant_xp(p, killer, mo->xp_reward);
    if (mo->is_boss && out_boss_killed) *out_boss_killed = true;

    if (p->quest_active && p->floor == p->quest_floor) {
        if (p->quest_type == QUEST_KILL && strcmp(mo->name, p->quest_monster) == 0) {
            p->quest_active = false;
            int qgold = player_gain_gold(p, p->quest_gold_reward);
            grant_xp(p, killer, p->quest_xp_reward);
            log_msg("Bounty complete! You've slain the %s. (+%d gold, +%d xp)", mo->name, qgold, p->quest_xp_reward);
        } else if (p->quest_type == QUEST_CLEAR) {
            p->quest_kills_done++;
            if (p->quest_kills_done >= p->quest_kills_needed) {
                p->quest_active = false;
                int qgold = player_gain_gold(p, p->quest_gold_reward);
                grant_xp(p, killer, p->quest_xp_reward);
                log_msg("Bounty complete! Floor %d is clear enough now. (+%d gold, +%d xp)",
                        p->quest_floor, qgold, p->quest_xp_reward);
            } else {
                log_msg("Bounty progress: %d/%d cleared on floor %d.",
                        p->quest_kills_done, p->quest_kills_needed, p->quest_floor);
            }
        }
    }

    /* A small extra chance for a strong kill to shake loose a piece of
       dungeon gear, on top of whatever the floor itself might already
       offer -- bosses (biome bosses and the Warden alike) roll far more
       often than an ordinary elite. */
    if (mo->is_boss) {
        if (rand() % 100 < 60) grant_random_set_piece(hero_driven(p), m->biome);
    } else if (mo->is_elite) {
        if (rand() % 100 < 12) grant_random_set_piece(hero_driven(p), m->biome);
    }
}

int damage_after_defence(int attack, int defence, int variance) {
    if (attack < 1) return 1; /* even a fully sapped attacker grazes you */
    if (defence < 0) defence = 0;

    int dmg = (attack * attack) / (attack + defence) + variance;
    if (dmg < 1) dmg = 1;
    return dmg;
}

/* One swing, for any of the six.
 *
   There were two of these -- this one, which read the character's attack,
   crit, jinx and position, and companion_strike(), which was a hire hitting
   something with a smaller version of the same idea. Two implementations of
   "an actor hits a monster" is two damage models, and they drifted: only one
   of them applied jinx, only one of them could crit, only one of them cleaned
   the blade for the larder.

   The actor is passed in. Nothing here asks which one it is except the log
   line, which has to know whether to say "you". */
/* One damage roll for any actor, against any monster.
 *
   Extracted because there were two, and the second one -- the AI's, in
   companion_strike() -- read `base_atk + atk_buff` and nothing else. So a hire
   got no weapon bonus, no set bonus, no stance, no crit, and, since crit is
   where the accessories land, **no benefit whatsoever from a ring or a
   trinket**. You could buy a hero a trinket and it would sit on their sheet
   doing nothing.

   Two damage models is two balance curves that drift, and this is what the
   drift looked like: an entire equipment slot that worked for one actor and
   was decoration for the other five. */
int hero_damage_roll(Player *p, const Hero *h, const Map *m, const Monster *mo, bool *out_crit) {
    /* hero_eff_atk_on() rather than hero_eff_atk(): on the proving ground's
       unarmed skin your weapon counts for nothing, and this is the one place
       all six actors' melee damage is decided, so it is the one place the
       rule has to be applied to reach all of them. */
    int dmg = damage_after_defence(hero_eff_atk_on(h, m), mo->def, rand() % 4 - 1); /* -1..2 */

    /* The angel's first lever: an actor who is nearly dead lands more of them.
       See dda.h -- zero unless they are under 45% health. Accessories reach
       the roll here, through effective_stat's ring/trinket terms. */
    int eff_crit = effective_stat(h, ACC_CRIT) + guardian_grace(p);
    bool crit = (eff_crit > 0 && rand() % 100 < eff_crit);
    if (crit) dmg *= 2;
    if (out_crit) *out_crit = crit;
    return dmg;
}

/* The set proc rides on the roll rather than on any one attack, so it fires
   for a spell, a shot and a swing alike -- and for all six actors, since the
   roll is shared. */
int hero_damage_with_procs(Player *p, Hero *h, const Map *m, Monster *mo, bool *out_crit) {
    int dmg = hero_damage_roll(p, h, m, mo, out_crit);
    return dmg + set_proc_on_hit(p, h, mo, dmg);
}

void hero_attack_monster(Player *p, Hero *h, Map *m, Monster *mo, bool *out_boss_killed) {
    /* Asked on the attacker's own square, not the target's: the rule is about
       where you are standing when you swing. */
    if (!proving_allows(m, h->x, h->y, PROVE_MELEE_ACT)) {
        if (h == hero_driven(p))
            log_msg("Not here. This ground was marked out for the arts.");
        return;
    }

    /* The Ruins' old wards did not all fail. Checked before the roll so a
       warded blow costs the turn and nothing else -- the band where raw damage
       stops being the whole answer. */
    int mward = biome_monster_ward_pct(p);
    if (mward > 0 && rand() % 100 < mward) {
        log_msg("An old ward flares around the %s and your blow slides off.", mo->name);
        return;
    }

    bool crit = false;
    int dmg = hero_damage_with_procs(p, h, m, mo, &crit);

    bool mine = (h == hero_driven(p));
    if (mine) log_msg(crit ? "A vicious opening! You strike the %s for %d."
                           : "You strike the %s for %d.", mo->name, dmg);
    else      log_msg(crit ? "%s finds an opening on the %s -- %d."
                           : "%s strikes the %s for %d.", h->name, mo->name, dmg);

    if (h->jinx_pct > 0 && mo->alive && rand() % 100 < h->jinx_pct) {
        mo->jinx_atk_penalty = 3 + rand() % 3;
        mo->jinx_turns_left = 4;
        log_msg("A flicker of bad luck settles over the %s.", mo->name);
    }

    anim_flash_cell(m, p, h->x, h->y, '@', CP_HIT, 1);
    anim_flash_cell(m, p, mo->x, mo->y, (chtype)mo->glyph, CP_HIT, crit ? 3 : 2);

    g_blade_clean = true;   /* the only place this is ever set */
    monster_take_damage(p, hero_driven(p), m, mo, dmg, out_boss_killed);
}

#define MONSTER_CRIT_PCT 5

/* Every biome's monsters carry a signature condition -- venomous jungle
   and salt-wastes creatures poison, industrial and abyssal things stun on
   impact, and the ruins' crystal-touched things slow your blood. Fortitude
   and Resolve (via hazard_resist_pct) give partial resistance, but never
   full immunity. */
static void maybe_inflict_status(const Monster *mo, Player *p, Hero *h) {
    int proc = 15 - (h->hazard_resist_pct + h->set_bonus_hazard_resist) / 4;
    if (proc < 3) proc = 3;
    if (rand() % 100 >= proc) return;

    /* "you" for the body the human is holding, the name for anybody else.
       Hires were never poisoned, stunned or slowed at all before this took a
       Hero -- the whole function only ever reached the driven body. */
    const bool led = (h == hero_driven(p));
    const char *who = led ? "you" : h->name;

    switch (mo->color_pair) {
        case CP_MON_VERMIN:
        case CP_MON_OUTRIDER:
            h->poison_turns_left = 4 + rand() % 3;
            h->poison_dmg_per_turn = 2 + rand() % 3;
            log_msg("The %s's bite leaves %s poisoned!", mo->name, who);
            break;
        case CP_MON_CLOCKWORK:
        case CP_MON_ABYSSAL:
        case CP_MON_BOSS:
            h->stun_turns_left += 1;
            log_msg("The %s's blow leaves %s reeling, stunned!", mo->name, who);
            break;
        case CP_MON_RUINS:
            h->slow_turns_left = 3 + rand() % 3;
            log_msg("Something about the %s's touch slows %s.", mo->name,
                    led ? "your blood" : who);
            break;
        default:
            break;
    }
}

/* ---- a monster hits a body ---------------------------------------------
 *
 * One function, for any of the six. There were two: this one, which gave the
 * driven body the unavoidable-crit rule, evasion, warding, its real defence
 * and the status roll -- and hero_take_damage(), which gave an undriven
 * body a flat subtraction and nothing else. So a hire never dodged, never
 * warded, was never poisoned, and could not be crit; and the moment the
 * character could be undriven, none of that applied to them either.
 *
 * The guardian angel's levers stay pointed at whoever the human is playing.
 * That is deliberate and it is not a slot-0 special case: the angel exists to
 * keep *the person holding the controller* in the run, and softening a blow
 * aimed at an AI-driven body would be spending it on somebody who cannot
 * notice. `led` is that question, asked once.
 */
/* Warding's reflect: a share of what landed goes back the way it came.
 *
   Placed on both of monster_hit_hero's damage paths -- the crit and the
   ordinary blow -- rather than inside hero_take_damage(), because that one is
   also called by lava, by snares and by the eye of the storm, and a ward that
   punished the *ceiling* for falling on you would be a ward that had stopped
   meaning anything. Reflect is about being struck by something that can be
   struck back. */
static void reflect_back(Player *p, Hero *h, Map *m, Monster *mo, int dmg) {
    if (!m || !mo || !mo->alive || h->reflect_turns <= 0 || h->reflect_pct <= 0) return;
    int back = dmg * h->reflect_pct / 100;
    if (back < 1) return;
    if (h == hero_driven(p))
        log_msg("The ward turns %d of it back into the %s.", back, mo->name);
    monster_take_damage(p, h, m, mo, back, NULL);
}

static void monster_hit_hero(Monster *mo, Player *p, Map *m, Hero *h) {
    if (!hero_is_up(h)) return;
    const bool led = (h == hero_driven(p));

    /* A rare, unavoidable critical: no evasion, warding or stacked armor
       stops it, and it isn't reduced by defence. However overbuilt a
       character gets, this keeps the temple able to actually hurt them.

       The angel's third lever, and the one that matters most: "unavoidable"
       is what kills a body that has already run out of health, because it
       ignores every defence they built. It stays unavoidable for anyone above
       45% -- the term is there to stop the late game becoming untouchable,
       and that is a problem the nearly-dead do not have. */
    int monster_crit = led ? guardian_softened_crit(MONSTER_CRIT_PCT, p) : MONSTER_CRIT_PCT;
    if (rand() % 100 < monster_crit) {
        int dmg = (h->maxhp * (4 + rand() % 4)) / 100; /* 4-7% of max HP */
        if (dmg < 1) dmg = 1;
        if (led) log_msg("A critical blow from the %s finds a gap in your guard! (-%d)", mo->name, dmg);
        else     log_msg("A critical blow from the %s catches %s. (-%d)", mo->name, h->name, dmg);
        hero_take_damage(p, h, dmg);
    reflect_back(p, h, m, mo, dmg);
        reflect_back(p, h, m, mo, dmg);
        maybe_inflict_status(mo, p, h);
        return;
    }

    /* The second lever: the monsters start missing. */
    int eff_evasion = h->evasion_pct + h->evasion_buff + h->set_bonus_evasion
                    + (led ? guardian_grace(p) : 0);
    if (eff_evasion > 0 && rand() % 100 < eff_evasion) {
        if (led) log_msg("You dodge the %s's attack.", mo->name);
        else     log_msg("%s slips the %s's attack.", h->name, mo->name);
        return;
    }
    /* The Ruins' old wards still hold, for somebody who needs them. */
    int eff_ward = h->ward_pct + h->set_bonus_ward + band_ward_bonus(p);
    if (eff_ward > 0 && rand() % 100 < eff_ward) {
        if (led) log_msg("Something wards off the %s's blow entirely.", mo->name);
        else     log_msg("Something wards the %s's blow off %s.", mo->name, h->name);
        return;
    }

    int atk = mo->atk;
    if (mo->jinx_turns_left > 0) atk -= mo->jinx_atk_penalty;
    if (atk < 0) atk = 0;

    /* The body's own defence, plus the smith's work, which is the party's. */
    int def = hero_eff_def(h) + party_def_bonus(p);
    int dmg = damage_after_defence(atk, def, rand() % 4 - 1);
    dmg = set_proc_on_hurt(p, h, dmg);
    if (dmg <= 0) return;                       /* the wards took all of it */

    if (led) log_msg("The %s hits you for %d.", mo->name, dmg);
    else     log_msg("The %s hits %s for %d.", mo->name, h->name, dmg);

    hero_take_damage(p, h, dmg);
    reflect_back(p, h, m, mo, dmg);
    maybe_inflict_status(mo, p, h);
}

void monster_take_turn(Map *m, Monster *mo, Player *p) {
    if (!mo->alive) return;

    if (mo->jinx_turns_left > 0) mo->jinx_turns_left--;

    if (mo->stun_turns_left > 0) {
        mo->stun_turns_left--;
        return;
    }
    if (mo->slow_turns_left > 0) {
        mo->slow_turns_left--;
        if (rand() % 2 == 0) return;
    }

    /* A hired hero standing next to a monster gets hit like anyone else.
       Without this they would be scenery the monsters walk past, and the
       whole point of paying for them is that they draw blows the player
       would otherwise take. Checked before the player so a monster boxed in
       by the party fights the party. */
    for (int cy = -1; cy <= 1; cy++) {
        for (int cx = -1; cx <= 1; cx++) {
            if (!cx && !cy) continue;
            Hero *c = companion_at(p, mo->x + cx, mo->y + cy);
            if (!c) continue;
            monster_hit_hero(mo, p, m, c);
            return;
        }
    }

    int dx = hero_driven(p)->x - mo->x, dy = hero_driven(p)->y - mo->y;
    int adx = abs(dx), ady = abs(dy);

    if (adx <= 1 && ady <= 1 && !(dx == 0 && dy == 0)) {
        monster_hit_hero(mo, p, m, hero_driven(p));

        /* The stone wood: what you cannot see gets the first blow, and gets it
           twice. The whole of Petrified Forest that was worth building -- the
           rest of that candidate was a thicket reskin, and the thicket already
           exists.
         *
           It asks the *tile* whether you can see the thing hitting you rather
           than remembering anything per monster. Monster is 92 bytes and a Well
           floor can hold 231,000 of them, so a flag here would cost megabytes
           to express something the map already knows -- and the map's answer is
           the better one anyway, because it is the same "can you see it" the
           renderer used to decide whether to draw it. */
        if (district_at(m, mo->x, mo->y) == DIST_PETRIFIED
            && !m->tiles[mo->y][mo->x].visible && mo->alive) {
            for (int extra = 0; extra < AMBUSH_EXTRA_BLOWS; extra++) {
                if (!mo->alive) break;
                monster_hit_hero(mo, p, m, hero_driven(p));
            }
            log_msg("It was behind the stone. You never saw it move.");
        }
        return;
    }

    /* The Abyss hides as it blinds: on that band, a run in trouble is noticed
       a tile or two later. The only band gift that is the band's own
       punishment turned around. See bands.h. */
    int aggro_radius = 8 - hero_driven(p)->aggro_reduction
                         - band_aggro_reduction(p)      /* the Abyss, if struggling */
                         - biome_growth_cover(p);       /* the Roots, always */
    if (aggro_radius < 2) aggro_radius = 2;

    /* The mycelium hears. Fungal mats carry every footfall, so inside one a
       monster does not need line of sight -- it needs you to have moved. Hold
       still for two turns and the mats stop reporting you, which is the only
       place in this game where standing there is a tactic.

       Deliberately checked on the *monster's* tile: the growth is what
       listens, so stepping out of it does not stop the ones still inside
       from knowing roughly where you went. */
    /* The quiet quarter is not watching. Until something happens, they
       notice you only at arm's length -- and once something does happen,
       hive_alarm's sibling below wakes the entire district for good. */
    if (district_at(m, mo->x, mo->y) == DIST_QUIET && aggro_radius > QUIET_AGGRO_RADIUS)
        aggro_radius = QUIET_AGGRO_RADIUS;

    /* The watch is the quiet quarter's opposite number: these are awake, and
       the short radius is discipline rather than sleep. A warden on post
       looks for trouble and finds you at two tiles; break the watch and the
       clamp comes off for good, because a broken watch is just a district
       full of things that know exactly where you are. */
    {
        int post = district_index_at(m, mo->x, mo->y);
        if (post >= 0 && m->districts[post].kind == DIST_WATCH &&
            m->districts[post].state != WATCH_BROKEN &&
            aggro_radius > WATCH_AGGRO_RADIUS)
            aggro_radius = WATCH_AGGRO_RADIUS;
    }

    /* The chapel deafens them. Aggro at range is hearing -- that is what the
       radius has always modelled -- so inside the silence nothing notices you
       until it is close, and then only down a clear line. The player pays the
       matching price in vision.c, where `hearing_radius` goes to zero on this
       ground. Applied before the mycelium block so a floor carrying both
       districts still gets one rule per square. */
    if (district_at(m, mo->x, mo->y) == DIST_CHAPEL &&
        aggro_radius > CHAPEL_AGGRO_RADIUS)
        aggro_radius = CHAPEL_AGGRO_RADIUS;

    bool in_mycelium = district_at(m, mo->x, mo->y) == DIST_MYCELIUM;
    if (in_mycelium) {
        if (hero_driven(p)->still_turns >= MYCELIUM_FORGET_TURNS) {
            mo->aggro = false;
        } else if (!mo->aggro) {
            int heard = MYCELIUM_HEAR_RADIUS;
            if (dx * dx + dy * dy <= heard * heard) mo->aggro = true;
        }
    }

    if (!mo->aggro && dx * dx + dy * dy <= aggro_radius * aggro_radius &&
        line_of_sight(m, mo->x, mo->y, hero_driven(p)->x, hero_driven(p)->y)) {
        mo->aggro = true;
    }

    if (!mo->aggro) {
        if (rand() % 4 == 0) {
            int wdx = rand() % 3 - 1, wdy = rand() % 3 - 1;
            int nx = mo->x + wdx, ny = mo->y + wdy;
            /* wandering (non-hunting) monsters still politely step around
               each other -- only monsters actively chasing push through */
            if (is_walkable_monster(m, nx, ny) && !monster_at(m, nx, ny) && !(nx == hero_driven(p)->x && ny == hero_driven(p)->y)) {
                mo->x = nx;
                mo->y = ny;
            }
        }
        return;
    }

    /* Chasing monsters ignore each other entirely -- only walls and the
       player himself block movement. Without this, a pack jams solid the
       moment two monsters are shoulder to shoulder in a corridor: nothing
       behind the leaders can ever reach you, so only the lucky few already
       touching you ever get to attack. */
    int sx = (dx > 0) - (dx < 0);
    int sy = (dy > 0) - (dy < 0);
    int nx = mo->x, ny = mo->y;
    bool moved = false;

    if (adx >= ady && sx != 0 &&
        is_walkable_monster(m, mo->x + sx, mo->y) && !(mo->x + sx == hero_driven(p)->x && mo->y == hero_driven(p)->y)) {
        nx = mo->x + sx;
        moved = true;
    } else if (sy != 0 &&
        is_walkable_monster(m, mo->x, mo->y + sy) && !(mo->x == hero_driven(p)->x && mo->y + sy == hero_driven(p)->y)) {
        ny = mo->y + sy;
        moved = true;
    } else if (sx != 0 &&
        is_walkable_monster(m, mo->x + sx, mo->y) && !(mo->x + sx == hero_driven(p)->x && mo->y == hero_driven(p)->y)) {
        nx = mo->x + sx;
        moved = true;
    }

    if (!moved) {
        /* the two preferred directions are both walled off -- try every
           other direction and take whichever legal step lands closest to
           the player, so a corner never freezes a monster in place for good */
        int best_d = -1;
        long best_dist2 = -1;
        for (int d = 0; d < 8; d++) {
            int tx = mo->x + MOVE_DX[d], ty = mo->y + MOVE_DY[d];
            if (tx == hero_driven(p)->x && ty == hero_driven(p)->y) continue;
            if (!is_walkable_monster(m, tx, ty)) continue;
            long ddx = hero_driven(p)->x - tx, ddy = hero_driven(p)->y - ty;
            long dist2 = ddx * ddx + ddy * ddy;
            if (best_d < 0 || dist2 < best_dist2) {
                best_d = d;
                best_dist2 = dist2;
            }
        }
        if (best_d >= 0) {
            nx = mo->x + MOVE_DX[best_d];
            ny = mo->y + MOVE_DY[best_d];
        }
    }

    mo->x = nx;
    mo->y = ny;
}

void process_monster_turns(Map *m, Player *p, bool *out_player_died) {
    *out_player_died = false;

    /* Only what is near enough to matter gets a turn. See
       MONSTER_ACTIVE_RADIUS -- the radii are set beyond every rule that
       reaches past line of sight, so nothing that could act on you is
       skipped, and everything that could not is free. */
    const long active2 = (long)MONSTER_ACTIVE_RADIUS * MONSTER_ACTIVE_RADIUS;
    const long leash2  = (long)MONSTER_LEASH_RADIUS  * MONSTER_LEASH_RADIUS;

    for (int i = 0; i < m->monster_count; i++) {
        Monster *mo = &m->monsters[i];
        if (!mo->alive) continue;

        long ddx = mo->x - hero_driven(p)->x, ddy = mo->y - hero_driven(p)->y;
        long d2 = ddx * ddx + ddy * ddy;
        if (d2 > (mo->aggro ? leash2 : active2)) continue;

        monster_take_turn(m, &m->monsters[i], p);
        if (hero_driven(p)->hp <= 0) {
            if (!guardian_catch(p) && !companions_attempt_rescue(p, m)
                && !party_pass_the_torch(p)) { *out_player_died = true; return; }
            guardian_follow_through(p, m);   /* space, not just a hit point */
        }
    }
}

/* Regeneration from a worn accessory. There is no innate regen -- the whole
   point of the Ouroboros Coil is that it is the only way to get any, so this
   is a no-op until one is equipped. Deliberately does nothing while poisoned:
   a trickle of HP should not quietly cancel a damage-over-time effect. */
void hero_tick_regen(Player *p, Hero *h) {
    /* The Roots feed you. Nothing is said about it; the bar simply moves a
       little on a floor that is going badly. See bands.h. */
    int rate = effective_stat(h, ACC_REGEN) + band_regen_bonus(p);
    if (rate <= 0) return;
    if (h->hp <= 0 || h->hp >= h->maxhp) return;
    if (h->poison_turns_left > 0) return;

    h->hp += rate;
    if (h->hp > h->maxhp) h->hp = h->maxhp;
}

/* Buffs and poison, for one actor. The messages say "you" only for the actor
   the human is attached to -- a hire's tonic wearing off is not narrated, and
   was not before, but that is now a statement about narration rather than
   about which struct the code happened to be holding. */
void hero_tick_buffs(Player *p, Hero *h) {
    bool mine = (h == hero_driven(p));
    if (h->atk_buff_turns > 0) {
        h->atk_buff_turns--;
        if (h->atk_buff_turns == 0) {
            h->atk_buff = 0;
            if (mine) log_msg("The ether tonic wears off.");
        }
    }
    if (h->def_buff_turns > 0) {
        h->def_buff_turns--;
        if (h->def_buff_turns == 0) {
            h->def_buff = 0;
            if (mine) log_msg("Your guard draught wears off.");
        }
    }
    if (h->evasion_buff_turns > 0) {
        h->evasion_buff_turns--;
        if (h->evasion_buff_turns == 0) {
            h->evasion_buff = 0;
            if (mine) log_msg("Your footing settles back to normal.");
        }
    }
}

void hero_tick_status(Player *p, Hero *h) {
    if (h->poison_turns_left > 0) {
        h->poison_turns_left--;
        h->hp -= h->poison_dmg_per_turn;
        if (h->hp < 0) h->hp = 0;
        if (h == hero_driven(p)) {
            log_msg("The poison burns for %d damage.", h->poison_dmg_per_turn);
            if (h->poison_turns_left == 0) log_msg("The poison finally fades from your veins.");
        } else if (h->hp == 0) {
            log_msg("%s is carried off by the poison.", h->name);
        }
        if (h->hp <= 0) h->alive = false;
    }
}

bool consume_stun_if_active(Player *p) {
    if (hero_driven(p)->stun_turns_left <= 0) return false;
    hero_driven(p)->stun_turns_left--;
    log_msg("You are stunned and can't act!");
    return true;
}

int player_gain_gold(Player *p, int amount) {
    if (amount <= 0) return 0;

    long g = amount;
    if (hero_driven(p)->gold_bonus_pct > 0) g += g * hero_driven(p)->gold_bonus_pct / 100;

    /* The altar's bargain: half your health for ten floors of doubled coin. */
    if (p->floor > 0 && p->floor <= p->gold_boon_until_floor) g *= 2;

    int mult = p->gold_mult > 0 ? p->gold_mult : 1;
    g *= mult;

    /* p->gold is an int, and a deep run with a large multiplier can reach
       for the ceiling in a single haul. */
    long room = 2000000000L - p->gold;
    if (room < 0) room = 0;
    if (g > room) g = room;

    p->gold += (int)g;
    return (int)g;
}

/* Coin that must NOT be multiplied.
 *
   player_gain_gold() applies the bank's share, the gold-find bonus and the
   altar's doubling. That is right for income -- a kill, a bounty, a sale --
   because the whole amount is new money.

   It is catastrophic for a *gross return on a stake you already owned*. The
   lizard track pays back stake plus winnings, so multiplying the payout
   multiplies the stake too: a book paying 0.82 per gold staked, which is
   supposed to bleed anyone who stands there all day, pays 1.64 at two bank
   shares and prints without bound. Measured in play at 2,805 races and a
   million gold from a standing start.

   So a gambling return is credited flat. The bookmaker pays what is chalked
   on the board; he has never heard of your shares. Any future mechanic that
   hands back a stake belongs here too. */
int player_credit_gold(Player *p, long amount) {
    if (amount <= 0) return 0;
    long room = 2000000000L - p->gold;
    if (room < 0) room = 0;
    if (amount > room) amount = room;
    p->gold += (int)amount;
    return (int)amount;
}

/* 10 is the shipped value; see combat.h. */
static int g_xp_divisor = SWARM_MONSTER_FRACTION;

void xp_penalty_set_divisor(int n) { g_xp_divisor = n < 1 ? 1 : n; }
int  xp_penalty_divisor(void)      { return g_xp_divisor; }

/* Experience goes to the body that earned it. This read hero_driven(p), which
   was right while the player was the only thing that could earn any. */
void grant_xp(Player *p, Hero *h, int xp) {
    if (!h) h = hero_driven(p);
    if (p->difficulty == DIFFICULTY_SWARM || p->difficulty == DIFFICULTY_HARDCORE) {
        /* A tenth, *on top of* the per-kill reward mapgen has already cut.
         *
           The two are separate and both real, and it is worth being exact
           because this comment was once "corrected" to say the opposite. The
           per-kill cut lives in mapgen.c, applied after make_monster_for_floor
           returns: heavy floors (Swarm, Hardcore, and overrun Hard floors)
           take xp_reward/5, ordinary Hard floors take /2, and an overrun floor
           takes a further /4. Reading make_monster_for_floor on its own shows
           no difficulty term and invites the conclusion that this line is the
           only one -- it is not, and measuring the wrong function is how that
           conclusion got written down.

           Measured off *generated floors*, floors 1-3: Normal pays 6 xp a
           kill, Hard 1-2, Swarm and Hardcore 1 -- and then this line divides
           that 1 by ten again, where the clamp below pins it back at 1.

           Which is the real point: for the whole early game the Swarm and
           Hardcore experience rate is a flat 1 per kill, and no value of this
           divisor changes it, because the reward reaching this line is already
           smaller than the divisor. Sweeping it from 10 to 1 moved nothing at
           all -- not one run in sixty (EVALUATION §87). If the early curve on
           those modes is ever to be tuned, it is mapgen's cut and this clamp
           that have to move, not this number. */
        xp /= g_xp_divisor;
        if (xp < 1) xp = 1;
    }
    if (h->xp_bonus_pct > 0) xp += xp * h->xp_bonus_pct / 100;
    h->xp += xp;
    while (h->xp >= h->xp_next) {
        h->xp -= h->xp_next;
        h->level++;
        h->maxhp += 8;
        h->hp = h->maxhp;
        h->base_atk += 1;
        if (h->level % 2 == 0) h->base_def += 1;
        h->xp_next = 20 + (h->level - 1) * 15;
        log_msg("You reach level %d! Max HP now %d.", h->level, h->maxhp);
    }
}

/* The exact inverse of the loop above, and deliberately sitting next to it:
   the two must stay mirrored, and they will not if they live in different
   files. Note the defence rule -- a point of defence is granted on reaching
   an *even* level, so it is taken back when an even level is the one being
   given up.

   Progress toward the next level is forfeit. That is not a balance guard; it
   falls out of the fact that there is nowhere to put it. Selling at 99% of
   the way to the next level loses that 99%. */
bool player_sell_level(Player *p) {
    if (hero_driven(p)->level <= 1) return false;

    if (hero_driven(p)->level % 2 == 0) hero_driven(p)->base_def -= 1;
    hero_driven(p)->base_atk -= 1;
    hero_driven(p)->maxhp    -= 8;
    hero_driven(p)->level--;

    hero_driven(p)->xp = 0;
    hero_driven(p)->xp_next = 20 + (hero_driven(p)->level - 1) * 15;

    if (hero_driven(p)->maxhp < 1) hero_driven(p)->maxhp = 1;      /* the class floor, not a balance knob */
    if (hero_driven(p)->base_atk < 1) hero_driven(p)->base_atk = 1;
    if (hero_driven(p)->base_def < 0) hero_driven(p)->base_def = 0;
    if (hero_driven(p)->hp > hero_driven(p)->maxhp) hero_driven(p)->hp = hero_driven(p)->maxhp;

    return true;
}
