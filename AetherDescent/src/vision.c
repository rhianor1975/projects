#include "vision.h"
#include "mapgen.h"
#include "companions.h"

/* One map per party slot. Module-global rather than a field on Hero: a Hero
   is copied by value in a dozen places -- the Tavern list rolls candidates,
   the School derives two throwaway sheets, hiring assigns a whole struct --
   and none of those want 2.2 MB coming with them. The maps belong to the
   slots, which are stable; the bodies move between them. */
static HeroVision g_vision[MAX_PARTY];

/* Per-slot lit-list, the same trick compute_fov uses and for the same reason:
   clearing 1.1 million tiles to undo a few hundred is how the FOV pass became
   the largest per-turn cost in the game at 1400x800 (EVALUATION 57). Six of
   them would be six times that. */
#define VIS_LIT_MAX 8192
static int  g_lit[MAX_PARTY][VIS_LIT_MAX];
static int  g_lit_n[MAX_PARTY];
static bool g_lit_over[MAX_PARTY];

/* The composite's own lit-list, for the same reason again. */
static int  g_comp_lit[VIS_LIT_MAX * MAX_PARTY];
static int  g_comp_n = 0;
static bool g_comp_over = false;

/* Which actor's memory the Map is currently painted with. -1 = nobody's. */
static int  g_painted_for = -1;

const HeroVision *hero_vision(int slot) {
    if (slot < 0 || slot >= MAX_PARTY) return NULL;
    return &g_vision[slot];
}

bool hero_has_seen(int slot, int x, int y) {
    if (slot < 0 || slot >= MAX_PARTY) return false;
    if (x < 0 || x >= MAP_W || y < 0 || y >= MAP_H) return false;
    return g_vision[slot].seen[y][x] != 0;
}

/* The heard set -- see the long note above heard_rebuild(). Declared up here
   with the other module state because hero_vision_forget() clears it and is
   written before the rest of the machinery. */
#define HEARD_MAX 64
static int g_heard[HEARD_MAX];
static int g_heard_n = 0;

void hero_vision_forget(void) {
    g_heard_n = 0;
    for (int i = 0; i < MAX_PARTY; i++) {
        /* Only the rows the current world size actually uses. MAP_H is the
           runtime height; the arrays are sized for the largest world so that
           changing worlds does not reallocate anything. */
        for (int y = 0; y < MAP_H; y++) {
            for (int x = 0; x < MAP_W; x++) {
                g_vision[i].vis[y][x]  = 0;
                g_vision[i].seen[y][x] = 0;
            }
        }
        g_lit_n[i] = 0;
        g_lit_over[i] = false;
    }
    g_comp_n = 0;
    g_comp_over = false;
    g_painted_for = -1;   /* a new floor is nobody's memory yet */
}

void hero_vision_seed_from_map(const Map *m, const Player *p) {
    if (!m || !p) return;
    for (int i = 0; i < MAX_PARTY; i++) {
        if (!hero_is_up(&p->party[i])) continue;
        for (int y = 0; y < MAP_H; y++)
            for (int x = 0; x < MAP_W; x++)
                g_vision[i].seen[y][x] = m->tiles[y][x].seen ? 1u : 0u;
    }
}

/* ---- one body's own look ------------------------------------------------ */

static void vis_clear_previous(int slot) {
    HeroVision *v = &g_vision[slot];
    if (g_lit_over[slot]) {
        for (int y = 0; y < MAP_H; y++)
            for (int x = 0; x < MAP_W; x++) v->vis[y][x] = 0;
    } else {
        for (int i = 0; i < g_lit_n[slot]; i++)
            v->vis[g_lit[slot][i] / MAP_W][g_lit[slot][i] % MAP_W] = 0;
    }
    g_lit_n[slot] = 0;
    g_lit_over[slot] = false;
}

static void vis_light(int slot, int x, int y) {
    HeroVision *v = &g_vision[slot];
    if (!v->vis[y][x]) {
        if (g_lit_n[slot] < VIS_LIT_MAX) g_lit[slot][g_lit_n[slot]++] = y * MAP_W + x;
        else g_lit_over[slot] = true;
    }
    v->vis[y][x]  = 1u;
    v->seen[y][x] = 1u;
}

/* Line of sight is mapgen's, so a body and the renderer can never disagree
   about what a wall blocks. */
static void vis_look(Map *m, int slot, int px, int py, int radius) {
    vis_clear_previous(slot);
    if (px < 0 || px >= MAP_W || py < 0 || py >= MAP_H) return;

    for (int y = py - radius; y <= py + radius; y++) {
        for (int x = px - radius; x <= px + radius; x++) {
            if (x < 0 || x >= MAP_W || y < 0 || y >= MAP_H) continue;
            int ddx = x - px, ddy = y - py;
            if (ddx * ddx + ddy * ddy > radius * radius) continue;
            if (line_of_sight(m, px, py, x, y)) vis_light(slot, x, y);
        }
    }
    vis_light(slot, px, py);   /* you can always see your own square */
}

/* ---- the composite the screen is drawn from ----------------------------- */

static void comp_clear_previous(Map *m) {
    if (g_comp_over) {
        for (int y = 0; y < MAP_H; y++)
            for (int x = 0; x < MAP_W; x++) m->tiles[y][x].visible = false;
    } else {
        for (int i = 0; i < g_comp_n; i++)
            m->tiles[g_comp_lit[i] / MAP_W][g_comp_lit[i] % MAP_W].visible = false;
    }
    g_comp_n = 0;
    g_comp_over = false;
}

static void comp_light(Map *m, int active, int x, int y) {
    if (!m->tiles[y][x].visible) {
        if (g_comp_n < (int)(sizeof(g_comp_lit) / sizeof(g_comp_lit[0])))
            g_comp_lit[g_comp_n++] = y * MAP_W + x;
        else g_comp_over = true;
    }
    /* Anything any actor can see is told to the active one, so the map you are
       looking at includes what your party is looking at. */
    if (!g_vision[active].seen[y][x]) fov_note_reveal();
    g_vision[active].seen[y][x] = 1u;
    m->tiles[y][x].visible = true;
    m->tiles[y][x].seen = true;   /* keeps the painted memory in step, one tile at a time */
}

/* The active actor's memory, painted onto the Map for the renderer and the
   searches to read.
 *
   This is map-sized work and therefore runs only when the answer changes --
   when the human attaches to a different actor, or arrives on a new floor.
   Between those, comp_light() keeps the Map in step a tile at a time as the
   fog lifts.

   The first cut of this painted every turn, and it is worth recording why
   that was wrong even though it was correct: at the Well it is 1.1 million
   writes per turn to express a picture that changes by a few dozen tiles,
   which is precisely the map-sized per-turn sweep EVALUATION 57 removed and
   57 measured it as the largest cost in the game. It made the sanitiser build
   too slow to finish a run, which is how it was caught. "I do not care about
   performance" buys memory and it buys constant factors; it does not buy an
   O(map) loop per turn, because that one stops the instruments working. */
static void comp_paint_memory(Map *m, int active) {
    const HeroVision *v = &g_vision[active];
    for (int y = 0; y < MAP_H; y++)
        for (int x = 0; x < MAP_W; x++)
            m->tiles[y][x].seen = v->seen[y][x] != 0;
    g_painted_for = active;
}

/* Rebuilds the Tile flags from the six maps.
 *
   `visible` is the union of what every actor can see right now -- your party
   reports what is in front of it, which is the scouting that hiring buys and
   has always bought.

   `seen` is the *active* actor's own memory, and that is the point of the six
   maps rather than a detail of them. Switch to a hire who has been off in the
   east wing and the map redraws as the floor they know: their explored ground,
   their blank spaces. It is their map, so it is the map you get. Anything
   another actor lights is added to the active actor's memory as it happens --
   they are telling you about it -- so nothing shown to you is ever forgotten
   by switching back. */
/* ---- the other sense ---------------------------------------------------
 *
 * `hearing_radius` is derived from ATTR_HEARING, trained at the Gladiator
 * School like every other attribute, and printed on the character sheet as
 * "How far you hear what you cannot see". Until now it was read by the
 * character sheet and by nothing else -- the only one of the twenty-two
 * derived stats with no consumer, on a stat the School charges 200v^2+300 for
 * and five of the hundred classes build around at 6-8.
 *
 * So: a monster inside somebody's hearing radius that nobody can see is
 * *heard*. Deliberately short (the radius clamps to 0..6) and deliberately
 * through walls -- hearing what sight cannot reach is the whole point, and a
 * sense that stopped at a wall would just be a worse FOV.
 *
 * What it reports is a position, not an identity: the renderer draws a mark,
 * not the monster. "Something is behind that wall" is the information the
 * stat promises, and it is also the information that stays interesting.
 *
 * Kept as a short list rather than a map-sized layer because it is read once
 * per drawn cell -- a couple of thousand a frame -- and there are only ever a
 * handful of entries. A 1.1-million-tile layer to answer a question with
 * twenty answers would cost more to clear than to search.
 */
bool vision_heard_at(int x, int y) {
    int key = y * MAP_W + x;
    for (int i = 0; i < g_heard_n; i++) if (g_heard[i] == key) return true;
    return false;
}

static void heard_rebuild(const Map *m, const Player *p) {
    g_heard_n = 0;
    if (m->floor_num == 0) return;       /* nothing to hear in the plaza */

    /* Who is listening, and how far -- worked out once rather than per monster.
     *
       The chapel's half of the bargain lives here: standing in the silence
       costs you the sense that reaches through walls, the same sense the
       monsters standing there have also lost (see combat.c). A body *outside*
       the district still hears normally into it, which is correct and is the
       reason to stand outside.

       `district_at` is a linear scan over the floor's districts, and it was
       originally called inside the monster loop below -- once per monster per
       body per turn. On a Swarm floor with 1,471 monsters and 8 districts that
       was **0.151 ms per update against 0.056 hoisted**, three runs each: the
       scan was roughly sixty per cent of the whole vision update.

       Recorded because the first measurement was misread. A single timing of
       0.146 ms looked small in absolute terms and got written up as "cost
       nothing worth reporting" -- which was true of the number and false of
       the proportion, and the A/B against the unhoisted version is what said
       so. Small and unnecessary are not the same measurement. */
    int ear_x[MAX_PARTY], ear_y[MAX_PARTY], ear_r2[MAX_PARTY], ears = 0;
    for (int i = 0; i < MAX_PARTY; i++) {
        const Hero *h = &p->party[i];
        if (!hero_is_up(h) || h->hearing_radius <= 0) continue;
        if (district_at(m, h->x, h->y) == DIST_CHAPEL) continue;
        ear_x[ears] = h->x;
        ear_y[ears] = h->y;
        ear_r2[ears] = h->hearing_radius * h->hearing_radius;
        ears++;
    }
    if (ears == 0) return;

    /* HEARD_MAX is a cap on how many are reported, and hitting it stops the
       scan -- so on a floor with more than sixty-four audible monsters, which
       ones you hear is arbitrary. That is acceptable for a sense that marks
       "something is there" and would not be for one that named them. */
    for (int k = 0; k < m->monster_count && g_heard_n < HEARD_MAX; k++) {
        const Monster *mo = &m->monsters[k];
        if (!mo->alive) continue;
        if (mo->x < 0 || mo->x >= MAP_W || mo->y < 0 || mo->y >= MAP_H) continue;
        /* Anything already on screen is seen, not heard. */
        if (m->tiles[mo->y][mo->x].visible) continue;

        for (int i = 0; i < ears; i++) {
            int dx = mo->x - ear_x[i], dy = mo->y - ear_y[i];
            if (dx * dx + dy * dy > ear_r2[i]) continue;
            g_heard[g_heard_n++] = mo->y * MAP_W + mo->x;
            break;
        }
    }
}

void hero_vision_update(Map *m, const Player *p, int driven_radius) {
    if (!m || !p) return;

    int driven = p->controlled;
    if (driven < 0 || driven >= MAX_PARTY) driven = 0;

    for (int i = 0; i < MAX_PARTY; i++) {
        if (!hero_is_up(&p->party[i])) { vis_clear_previous(i); continue; }
        /* In town nobody scouts: the hires wait at the temple mouth, and the
           plaza is drawn lit regardless. */
        if (m->floor_num == 0 && i != driven) { vis_clear_previous(i); continue; }
        int r = (i == driven) ? driven_radius : COMPANION_FOV;
        vis_look(m, i, p->party[i].x, p->party[i].y, r);
    }

    /* Town is lit by construction and not by anybody's eyes.
     *
       generate_town_map() marks its whole footprint seen and visible, and the
       composite below would then paint the *driven body's* memory straight
       over that -- which is right underground and wrong here, because in town
       the map is the authority and there is no fog to composite.

       It showed up as the dungeon's fog of war cut out of the plaza: lit where
       the last floor had been explored, black where it had not. Forgetting the
       old floor (which generate_town_map now also does, and should) is only
       half the answer -- a body that remembers nothing paints the town *dark*,
       which is the same bug with the stencil inverted. The other half is not
       painting at all. */
    if (m->floor_num == 0) { g_heard_n = 0; return; }

    comp_clear_previous(m);
    for (int i = 0; i < MAX_PARTY; i++) {
        if (!hero_is_up(&p->party[i])) continue;
        if (m->floor_num == 0 && i != driven) continue;
        const HeroVision *v = &g_vision[i];
        /* Walk the body's own lit-list rather than the map: the point of
           keeping the list is that the union costs what was lit, not what
           exists. */
        if (g_lit_over[i]) {
            for (int y = 0; y < MAP_H; y++)
                for (int x = 0; x < MAP_W; x++)
                    if (v->vis[y][x]) comp_light(m, driven, x, y);
        } else {
            for (int k = 0; k < g_lit_n[i]; k++)
                comp_light(m, driven, g_lit[i][k] % MAP_W, g_lit[i][k] / MAP_W);
        }
    }

    /* Only when the picture is a different actor's. */
    if (g_painted_for != driven) comp_paint_memory(m, driven);

    /* After the composite, because "can anybody see it" is a question about
       the finished picture. */
    heard_rebuild(m, p);
}
