#include "autoexplore.h"
#include "mapgen.h"
#include "path.h"
#include "spells.h"
#include "ranged.h"
#include "combat.h"

/* See autoexplore.h for why the decisions live here and the loop does not.

   The three search wrappers below came with them. They are what "where should
   I step" is made of, and leaving them behind in main.c would have meant a
   module that could not answer its own question. */

static const int AUTO_DX[8] = {-1, 1, 0, 0, -1, 1, -1, 1};
static const int AUTO_DY[8] = { 0, 0,-1, 1, -1,-1,  1, 1};

/* Breadth-first search from (sx,sy) to (tx,ty), stepping only through
   tiles the player has actually seen -- this never uses knowledge the
   player doesn't have. On success, out_dx/out_dy is the first step to
   take. avoid_hazards=true refuses to route through lava/miasma/water when
   a clean route exists; callers should retry with avoid_hazards=false if
   that fails, since some floors only connect through a hazard by design. */
/* Auto-explore's "walk to this square" step. The search itself lives in
   path.c because companions need the same thing; the only difference is that
   auto-explore must stay on ground the player has actually revealed, so it
   cannot path through a shortcut the player has no way of knowing about. */
static bool bfs_next_step_known(const Map *m, int sx, int sy, int tx, int ty, bool avoid_hazards, int *out_dx, int *out_dy) {
    return path_next_step(m, sx, sy, tx, ty, avoid_hazards, true, out_dx, out_dy);
}

/* Lava and miasma are walkable for the player but cost HP every step, so
   auto-explore should route around them when it can. is_walkable_monster()
   already encodes exactly "passable and not damaging", which is what a
   hazard-avoiding path wants. Callers try avoid_hazards=true first and fall
   back to the permissive pass, so a hazard can still be crossed when it's
   the only way through. */

/* Finds the nearest already-seen, walkable tile that still borders unknown
   ground, and returns the first step toward it -- i.e. "walk to the edge
   of what you can see," the same thing a human does with the map on
   screen. Never looks at tiles the player hasn't actually revealed. */
/* Auto-explore's "walk to the edge of what you can see". Lives in path.c
   because hired companions explore the same way, over the same shared map
   knowledge. */
static bool bfs_frontier_step(const Map *m, int sx, int sy, bool avoid_hazards, int *out_dx, int *out_dy) {
    return path_frontier_step(m, sx, sy, avoid_hazards, out_dx, out_dy);
}

/* Finds the nearest already-seen floor item (gold, potion, whatever's still
   lying there unclaimed) reachable through known ground, and returns the
   first step toward it. Auto-explore checks this before deciding whether
   to push toward the stairs or the fog, so it detours for loot it can see
   instead of walking straight past it. */
static bool bfs_next_step_to_item(const Map *m, int sx, int sy, bool avoid_hazards, int *out_dx, int *out_dy) {
    /* Generation stamps rather than clearing the scratch arrays, exactly as
       path.c does. Wiping two map-sized arrays cost 2.2 million writes per
       call, and this is called twice on every step auto-explore takes that
       is not a fight. */
    static unsigned int gen = 0;
    static unsigned int stamp[MAP_H_MAX][MAP_W_MAX];
    static signed char parent_dir[MAP_H_MAX][MAP_W_MAX];
    static int qx[MAP_W_MAX * MAP_H_MAX], qy[MAP_W_MAX * MAP_H_MAX];

    /* Nothing left to fetch: say so before touching the map at all. Late on
       a floor this is the common case, and it used to be the most expensive
       one -- proving "no items reachable" meant flooding every revealed tile,
       twice. */
    bool any_unclaimed = false;
    for (int ii = 0; ii < m->item_count; ii++)
        if (!m->items[ii].used) { any_unclaimed = true; break; }
    if (!any_unclaimed) return false;

    /* Where the items are, as a grid, so the inner loop is a lookup instead
       of a scan. The old form checked every dequeued tile against every item
       on the floor: 35,000 revealed tiles times 3,400 items is 119 million
       comparisons for a single step, and it grew as the floor was explored.
       That is the sluggishness the playtest found. */
    static unsigned int item_gen = 0;
    static unsigned int item_here[MAP_H_MAX][MAP_W_MAX];
    if (++item_gen == 0) { memset(item_here, 0, sizeof(item_here)); item_gen = 1; }
    for (int ii = 0; ii < m->item_count; ii++) {
        const FloorItem *fi = &m->items[ii];
        if (fi->used) continue;
        if (fi->x < 0 || fi->x >= MAP_W || fi->y < 0 || fi->y >= MAP_H) continue;
        item_here[fi->y][fi->x] = item_gen;
    }

    if (++gen == 0) { memset(stamp, 0, sizeof(stamp)); gen = 1; }

    int qh = 0, qt = 0;
    qx[qt] = sx; qy[qt] = sy; qt++;
    stamp[sy][sx] = gen;
    int found_x = -1, found_y = -1;

    while (qh < qt) {
        int cx = qx[qh], cy = qy[qh]; qh++;

        if (!(cx == sx && cy == sy) && item_here[cy][cx] == item_gen) {
            found_x = cx; found_y = cy;
            break;
        }

        for (int d = 0; d < 8; d++) {
            int nx = cx + AUTO_DX[d], ny = cy + AUTO_DY[d];
            if (nx < 0 || nx >= MAP_W || ny < 0 || ny >= MAP_H) continue;
            if (stamp[ny][nx] == gen) continue;
            if (!m->tiles[ny][nx].seen) continue;
            /* Same rule as path.c: never route through the way back up. */
            if (m->tiles[ny][nx].type == TILE_STAIRS_UP) continue;
            if (!autoexplore_passable(m, nx, ny, avoid_hazards)) continue;
            stamp[ny][nx] = gen;
            parent_dir[ny][nx] = (signed char)d;
            qx[qt] = nx; qy[qt] = ny; qt++;
        }
    }
    if (found_x < 0) return false;

    int cx = found_x, cy = found_y;
    int step_x = found_x, step_y = found_y;
    while (!(cx == sx && cy == sy)) {
        int d = parent_dir[cy][cx];
        step_x = cx; step_y = cy;
        cx -= AUTO_DX[d];
        cy -= AUTO_DY[d];
    }
    *out_dx = step_x - sx;
    *out_dy = step_y - sy;
    return true;
}


bool autoexplore_passable(const Map *m, int x, int y, bool avoid_hazards) {
    return avoid_hazards ? is_walkable_delegated(m, x, y) : is_walkable_player(m, x, y);
}

unsigned long autoexplore_progress(const Player *p, const Map *m) {
    /* The body being driven, because that is the body the walk below moves.
       This read slot 0 while every step moved party_x/party_y, so driving a
       hire meant the stuck-detector was watching somebody else: the character
       stood still under the AI, the signature stopped changing, and
       auto-explore reported itself stuck while the hire was walking perfectly
       well. It stops "after a while" precisely because the character's AI
       settles down after a while. */
    const Hero *h = hero_driven_c(p);
    unsigned long sig = 0;
    sig = sig * 31u + (unsigned long)h->x;
    sig = sig * 31u + (unsigned long)h->y;
    sig = sig * 31u + (unsigned long)p->floor;
    sig = sig * 31u + (unsigned long)h->hp;
    sig = sig * 31u + (unsigned long)h->kills;

    /* Monster health, so a drawn-out fight reads as progress even while the
       player stands still. */
    for (int i = 0; i < m->monster_count; i++)
        if (m->monsters[i].alive) sig = sig * 31u + (unsigned long)m->monsters[i].hp;

    /* Tiles revealed, so walking into the fog counts even before it finds
       anything worth reporting. Counted as the fog lifts rather than by
       sweeping the map -- the sweep was 1.1 million tiles on every single
       auto-explore step at 1400x800. */
    sig = sig * 31u + fov_reveal_ticks();

    return sig;
}

bool autoexplore_step_toward(const Map *m, int sx, int sy, int tx, int ty,
                             int *out_dx, int *out_dy) {
    if (bfs_next_step_known(m, sx, sy, tx, ty, true, out_dx, out_dy)) return true;
    return bfs_next_step_known(m, sx, sy, tx, ty, false, out_dx, out_dy);
}

AxFight autoexplore_decide_fight(const Player *p, const Map *m,
                                 const Monster *target, int dist) {
    AxFight out = { AX_NOTHING, -1 };
    if (!p || !m || !target || !target->alive) return out;
    if (dist > AUTOEXPLORE_HUNT_RADIUS) return out;

    const Hero *h = hero_driven_c(p);

    /* The proving ground forbids whole verbs, and a delegated walk that picks
       a forbidden one gets a refusal, spends no turn, and picks it again --
       which is the shape of every stuck-walk defect this project has had.
       Asked once here so the decision is never one the floor will reject. */
    bool may_melee  = proving_allows(m, h->x, h->y, PROVE_MELEE_ACT);
    bool may_cast   = proving_allows(m, h->x, h->y, PROVE_ARCANE_ACT);
    bool may_shoot  = proving_allows(m, h->x, h->y, PROVE_RANGED_ACT);

    if (dist <= 1) {
        if (may_melee) { out.kind = AX_MELEE; return out; }
        /* Toe to toe on ground that forbids blades: cast if you can, and if
           you cannot, this is not your fight. Walking away is a real answer
           and the only honest one. */
        int close_slot = may_cast ? spell_pick_attack_slot(h, dist) : -1;
        if (close_slot >= 0) { out.kind = AX_SPELL; out.slot = close_slot; return out; }
        return out;   /* AX_NOTHING -- back to exploring, and out of here */
    }

    /* Hardest to replace first, while the target is still worth it. */
    if ((target->is_boss || target->is_elite) && h->ability_cd == 0) {
        out.kind = AX_ABILITY;
        return out;
    }

    int slot = may_cast ? spell_pick_attack_slot(h, dist) : -1;
    if (slot >= 0) { out.kind = AX_SPELL; out.slot = slot; return out; }

    if (may_shoot && ranged_ready(h) && dist <= ranged_reach(h)) { out.kind = AX_SHOOT; return out; }

    /* Nothing reaches it and nothing here is allowed to: do not walk toward a
       fight this ground will not let you have. */
    if (!may_melee && !may_cast) return out;

    out.kind = AX_CLOSE;
    return out;
}

/* True if standing here means being moved at the end of the turn. Mirrors
   districts_tick(); tests/movement.c asserts the two agree. */
static bool tile_conveys(const Map *m, int x, int y) {
    if (x < 0 || x >= MAP_W || y < 0 || y >= MAP_H) return false;
    TileType t = m->tiles[y][x].type;
    return t == TILE_CURRENT || t == TILE_BELT;
}

bool autoexplore_escape_conveyance(const Map *m, int x, int y,
                                   int *out_dx, int *out_dy) {
    if (!m || !tile_conveys(m, x, y)) return false;

    for (int d = 0; d < 8; d++) {
        int nx = x + AUTO_DX[d], ny = y + AUTO_DY[d];
        if (nx < 0 || nx >= MAP_W || ny < 0 || ny >= MAP_H) continue;
        /* is_walkable_delegated() already refuses conveyed ground, hazards
           and walls, so anything it accepts is genuinely off the channel. */
        if (!is_walkable_delegated(m, nx, ny)) continue;
        if (m->tiles[ny][nx].type == TILE_STAIRS_UP) continue;   /* never up */
        if (out_dx) *out_dx = AUTO_DX[d];
        if (out_dy) *out_dy = AUTO_DY[d];
        return true;
    }
    /* Mid-channel with banks of water or rock on both sides. Nothing to do
       but be carried, which is the aqueduct's whole point -- and the caller's
       ordinary routing takes over. */
    return false;
}

bool autoexplore_route(const Player *p, const Map *m,
                       int *out_dx, int *out_dy, bool *out_stairs_known) {
    int x = party_x(p), y = party_y(p);
    bool stairs_known = m->tiles[m->stairs_down_y][m->stairs_down_x].seen;
    if (out_stairs_known) *out_stairs_known = stairs_known;

    int dx = 0, dy = 0;

    /* Off the conveyor before anything else.
     *
       A body standing in an aqueduct channel is moved CURRENT_PUSH tiles at
       the end of every turn. The route below is goal-directed and cheerfully
       picks a step along or back into the channel; the current then undoes it,
       and the body ends the turn exactly where it started. The progress
       signature is unchanged, sixty times over, and the walk reports "getting
       nowhere here -- stopping".

       Which was true, and was the *detector* working rather than failing. The
       defect was upstream: is_walkable_delegated() stops a walk routing onto
       conveyed ground and there was nothing to get it off ground it was
       already standing on. Measured before this existed: 707 stuck reports in
       15,000 fuzzer keys, and the log line immediately before every single one
       of the 707 was "The current takes you 1 tiles."

       Deliberately first, above loot. While the floor is moving you, no other
       decision survives being made. */
    if (autoexplore_escape_conveyance(m, x, y, &dx, &dy)) {
        *out_dx = dx; *out_dy = dy;
        return true;
    }

    /* Loot first: it is on the way and it is why you came. Then the stairs if
       you know where they are, and the nearest unseen edge if you do not.
       Every leg runs twice -- once refusing hazards, once accepting them --
       because a hunt is not a reason to walk through lava, but a dead end
       is. */
    /* Loot first: it is on the way and it is why you came. Then the stairs if
       you know where they are, and the nearest unseen edge if you do not.
       Every leg runs twice -- once refusing hazards, once accepting them --
       because a hunt is not a reason to walk through lava, but a dead end
       is.

       This leg is the expensive one and it is *known* to be: profiled at
       3,827 of the 7,481 samples inside a single auto-explore step, most of it
       inside bfs_next_step_to_item(). Two attempts to make it cheaper were
       measured and both are gone, which is worth writing down so a third
       person does not spend the afternoon rediscovering them:

         - caching the *failure* ("nothing reachable, stop looking until the
           floor changes") -- 741 turns against 739 in sixty seconds, i.e.
           nothing, because floors nearly always have loot on them and the
           flood therefore stops early;
         - caching the *destination* and walking back to it with a directed
           search -- 662 turns against 749, i.e. **12% worse**, because A* to
           one specific square over seen-only ground costs more than a flood
           that meets the nearest item almost immediately.

       The flood is cheap precisely because it usually stops at once. Leave it
       alone unless a measurement says otherwise. */
    bool ok = bfs_next_step_to_item(m, x, y, true, &dx, &dy);
    if (!ok) ok = bfs_next_step_to_item(m, x, y, false, &dx, &dy);

    if (!ok && stairs_known) {
        ok = bfs_next_step_known(m, x, y, m->stairs_down_x, m->stairs_down_y, true, &dx, &dy);
        if (!ok) ok = bfs_next_step_known(m, x, y, m->stairs_down_x, m->stairs_down_y, false, &dx, &dy);
    }
    /* And if the stairs are known but not *reachable yet*, keep exploring.
     *
       This was an else: seeing the staircase switched the walk permanently
       off the frontier search and onto "go there", so the first time you
       glimpsed the stairs across a room you had not walked to -- through a
       doorway, down a lit corridor, over a bridge whose approach was still
       dark -- the search failed and auto-explore stopped with "no known path
       to the stairs".

       The message was true and the conclusion was wrong. Both searches walk
       only ground the player has revealed (path.h's `require_seen`), so "no
       known path" means *the route has not been uncovered yet*, which is
       precisely the situation exploring exists to fix. Seeing the stairs is
       information, not an instruction, and it should never take a leg of the
       walk away.

       Reported from play as "sometimes there is no path to the stairs, which
       is impossible since there is always a path" -- and the floor was right:
       there was always a path, and never always a *known* one. */
    if (!ok) {
        ok = bfs_frontier_step(m, x, y, true, &dx, &dy);
        if (!ok) ok = bfs_frontier_step(m, x, y, false, &dx, &dy);
    }

    /* Backstop: never take the way back up. The searches route around it, so
       this should be unreachable -- and if it ever fires, stopping is better
       than climbing out of a run the player did not ask to end. */
    if (ok) {
        int nx = x + dx, ny = y + dy;
        if (nx >= 0 && nx < MAP_W && ny >= 0 && ny < MAP_H
            && m->tiles[ny][nx].type == TILE_STAIRS_UP) ok = false;
    }

    if (ok) { *out_dx = dx; *out_dy = dy; }
    return ok;
}
