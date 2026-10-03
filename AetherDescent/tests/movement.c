/* Can anything get stuck? -- the movement suite.
 *
 * Everything else in tests/ asks whether a rule is right. This file asks a
 * narrower question that no other suite covers and that play surfaces late
 * and expensively: **is there anywhere on a generated floor that a body can
 * end up and not get out of?**
 *
 * The two shapes it looks for are the two the districts can actually
 * produce, and they are not the same shape at all:
 *
 *   1. **Ground you cannot walk to.** Districts are carved *before* any room
 *      is, and each one is joined to the floor by two roads run to a single
 *      `district_anchor()` tile. A district whose interior is cut into more
 *      than one walkable piece therefore ships with the pieces the anchor is
 *      not in stranded -- reachable only if a corridor happened to cut
 *      through on its way somewhere else. Nobody gets *stuck* in one, because
 *      nobody can get in; what is lost is the loot, the monsters and the
 *      district's own rule, silently, on some fraction of floors. The stairs
 *      invariant in tests/invariants.c cannot see this: it asks only whether
 *      the way down is reachable.
 *
 *   2. **Ground that moves you.** A belt and a current take the actor
 *      standing on them every turn, whether or not the actor chose it. For a
 *      person that is the mechanic. For a *delegated* walk -- auto-explore,
 *      a hire following orders, the fuzzer -- it is a trap with no floor: it
 *      paths, gets carried off the route, re-paths, steps back on, and is
 *      carried again, forever. It never trips the no-progress detector,
 *      because the position keeps changing. common.c already knows this and
 *      says so at length above `is_walkable_delegated()`; the property below
 *      is that knowledge written down as a test, so the next conveying tile
 *      that gets added is caught by the suite instead of by a play session.
 *
 * Links against the real modules like every other suite here, and touches
 * nothing on disk. Run with `make test`.
 */
#include "../src/common.h"
#include "../src/mapgen.h"
#include "../src/combat.h"
#include "../src/classes.h"
#include "../src/save.h"
#include "../src/town.h"
#include "../src/autoexplore.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;

static void check(bool cond, const char *what) {
    if (!cond) { printf("  FAIL: %s\n", what); failures++; }
}

/* sizeof(Map) is 65 MB. One shared scratch world, for the same reason
   tests/invariants.c has one: a static Map per test function puts this
   binary within reach of the linker's 2 GB range limit. */
static Map m;
static Player p;

static bool seen[MAP_H_MAX][MAP_W_MAX];
static int  qx[MAP_W_MAX * MAP_H_MAX], qy[MAP_W_MAX * MAP_H_MAX];

/* Everything a body can walk to from (sx,sy), left in `seen`. Uses the
   player's own walkability rather than the delegated one: the question here
   is whether the ground is reachable at all, not whether auto-explore likes
   the route. */
static void flood(const Map *mm, int sx, int sy) {
    memset(seen, 0, sizeof seen);
    int head = 0, tail = 0;
    seen[sy][sx] = true;
    qx[tail] = sx; qy[tail] = sy; tail++;

    while (head < tail) {
        int x = qx[head], y = qy[head]; head++;
        static const int DX[4] = { 1, -1, 0, 0 };
        static const int DY[4] = { 0, 0, 1, -1 };
        for (int k = 0; k < 4; k++) {
            int nx = x + DX[k], ny = y + DY[k];
            if (nx < 0 || nx >= MAP_W || ny < 0 || ny >= MAP_H) continue;
            if (seen[ny][nx]) continue;
            if (!is_walkable_player(mm, nx, ny)) continue;
            seen[ny][nx] = true;
            qx[tail] = nx; qy[tail] = ny; tail++;
        }
    }
}

static void fresh_player(unsigned seed) {
    memset(&p, 0, sizeof p);
    p.run_seed = seed;
    p.party[0].level = 10;
    p.party[0].maxhp = 150;
    p.party[0].fov_radius = 7;
    p.difficulty = DIFFICULTY_NORMAL;
}

/* ---- 1. no district is carved into pieces the roads never reach ---------
 *
 * Reported per kind rather than as one number, because "3% of district
 * ground is stranded" hides "one kind strands a third of itself and the
 * other sixteen strand none" -- which is the only version of this result
 * that says what to fix.
 */
static long g_pits;

static void test_districts_are_reachable(void) {
    printf("districts are reachable\n");

    long walkable[DIST_KIND_COUNT]  = {0};
    long stranded[DIST_KIND_COUNT]  = {0};
    long instances[DIST_KIND_COUNT] = {0};
    long badinst[DIST_KIND_COUNT]   = {0};
    int floors = 0;

    /* The Shaft carries the sweep because it generates fastest and districts
       are placed by the same code at every size. The Halls are sampled to
       confirm the result is not an artefact of a small map -- a district is
       a fixed size in tiles, so a bigger floor holds proportionally more of
       them and the roads have further to run. */
    const struct { int size; int seeds; int step; } SWEEP[] = {
        { WORLD_SHAFT, 40, 9 },
        { WORLD_HALLS,  4, 31 },
    };

    for (unsigned w = 0; w < sizeof SWEEP / sizeof SWEEP[0]; w++) {
        world_size_apply(SWEEP[w].size);
        for (int seed = 1; seed <= SWEEP[w].seeds; seed++) {
            for (int f = 1; f <= 99; f += SWEEP[w].step) {
                int sx, sy;
                fresh_player((unsigned)(seed + w * 104729));
                memset(&m, 0, sizeof m);
                generate_temple_floor(&m, f, &sx, &sy, &p);
                flood(&m, sx, sy);
                floors++;

                for (int i = 0; i < m.district_count; i++) {
                    const MapDistrict *d = &m.districts[i];
                    int k = d->kind;
                    if (k < 0 || k >= DIST_KIND_COUNT) continue;
                    long here = 0, lost = 0;
                    for (int y = d->y; y < d->y + d->h; y++) {
                        for (int x = d->x; x < d->x + d->w; x++) {
                            if (x < 0 || x >= MAP_W || y < 0 || y >= MAP_H) continue;
                            if (!is_walkable_player(&m, x, y)) continue;
                            here++;
                            if (!seen[y][x]) lost++;
                        }
                    }
                    instances[k]++;
                    walkable[k] += here;
                    stranded[k] += lost;
                    if (k == DIST_SHAFT) {
                        for (int y2 = d->y; y2 < d->y + d->h; y2++)
                            for (int x2 = d->x; x2 < d->x + d->w; x2++)
                                if (x2 >= 0 && x2 < MAP_W && y2 >= 0 && y2 < MAP_H
                                    && m.tiles[y2][x2].type == TILE_PIT) g_pits++;
                    }
                    /* One tile behind a crystal vein is scenery. A district
                       missing a tenth of itself is a district with a room
                       nobody will ever open. */
                    if (here > 0 && lost * 10 > here) badinst[k]++;
                }
            }
        }
    }
    world_size_apply(WORLD_SHAFT);

    printf("  %d floors\n", floors);
    printf("  %-12s %8s %8s %7s  %s\n", "kind", "tiles", "stranded", "pct", "bad instances");
    long tot_w = 0, tot_s = 0;
    for (int k = 0; k < DIST_KIND_COUNT; k++) {
        if (!instances[k]) continue;
        tot_w += walkable[k];
        tot_s += stranded[k];
        double pct = walkable[k] ? 100.0 * (double)stranded[k] / (double)walkable[k] : 0.0;
        printf("  %-12s %8ld %8ld %6.2f%%  %ld/%ld\n",
               wild_kind_name(k) ? wild_kind_name(k) : "?",
               walkable[k], stranded[k], pct, badinst[k], instances[k]);
    }
    printf("  %-12s %8ld %8ld %6.2f%%\n", "ALL", tot_w, tot_s,
           tot_w ? 100.0 * (double)tot_s / (double)tot_w : 0.0);

    check(floors > 0, "the sweep generated floors");
    /* A mine with no holes in it is a mine, not a shaft. The holes are the
       whole district: the rest is galleries and ore, which the rod field and
       the old quarter already do. */
    printf("  %ld holes in the mine floors\n", g_pits);
    check(instances[DIST_SHAFT] == 0 || g_pits > 0,
          "the workings have holes in them");

    /* The property, per kind: a district hands the floor its ground.
     *
       The bars are set where they are because district_relink() puts the
       worst kind (the overgrowth) at 4 bad instances in 308 and 3.9% of its
       ground stranded, and every other kind well below that. A bar at a
       tenth would pass whether the relink pass ran or not, which is not a
       guard -- it is a comment. These would both fail on the code as it stood
       before that pass existed, which is the only test of a threshold that
       means anything. */
    for (int k = 0; k < DIST_KIND_COUNT; k++) {
        if (instances[k] < 10) continue;
        char msg[128];
        snprintf(msg, sizeof msg, "%s: instances are reachable throughout",
                 wild_kind_name(k) ? wild_kind_name(k) : "?");
        check(badinst[k] * 20 <= instances[k], msg);
    }
    check(tot_w == 0 || tot_s * 100 <= tot_w * 3,
          "district ground is overwhelmingly reachable");
}

/* ---- 2. nothing under orders can be walked onto conveyed ground ---------
 *
 * The whole property, and it is one line: a tile that moves you is a tile a
 * delegated walk must refuse. Stated over the tile *types* rather than over
 * a generated floor, because it is a fact about the rules and not about any
 * particular map -- and because a type-level assertion catches the next
 * conveying tile somebody adds, which a map-level one would only catch once
 * that tile happened to generate.
 */
static void test_delegated_walks_refuse_conveyed_ground(void) {
    printf("delegated walks refuse conveyed ground\n");

    /* Every tile type that displaces whatever stands on it at end of turn.
       districts_tick() is the authority; this list mirrors it. */
    static const TileType CONVEYING[] = { TILE_CURRENT, TILE_BELT };

    memset(&m, 0, sizeof m);
    world_size_apply(WORLD_SHAFT);

    for (unsigned i = 0; i < sizeof CONVEYING / sizeof CONVEYING[0]; i++) {
        m.tiles[5][5].type = CONVEYING[i];
        char msg[128];
        snprintf(msg, sizeof msg,
                 "tile type %d moves you, so a delegated walk must not route onto it",
                 (int)CONVEYING[i]);
        check(!is_walkable_delegated(&m, 5, 5), msg);

        /* And it stays walkable for a person: refusing it under orders is
           the point, refusing it outright would delete the district. */
        snprintf(msg, sizeof msg,
                 "tile type %d is still walkable when a person chooses it",
                 (int)CONVEYING[i]);
        check(is_walkable_player(&m, 5, 5), msg);
    }
}

/* ---- 3. being conveyed always ends -------------------------------------
 *
 * Even for a person who stepped in deliberately, a ride has to stop. The
 * failure this rules out is a closed loop of conveyed tiles: the actor is
 * moved every turn, forever, taking no damage and reaching nowhere -- the
 * one state in the game that no key can end.
 *
 * Walked over the real generated tiles rather than argued from the carve
 * functions, because the loop would be a property of how two districts
 * happened to land next to each other, which is exactly the thing a carve
 * function cannot see.
 */
static void test_conveyance_terminates(void) {
    printf("conveyance terminates\n");

    int rides = 0, floors = 0;
    world_size_apply(WORLD_SHAFT);

    for (int seed = 1; seed <= 25; seed++) {
        for (int f = 1; f <= 99; f += 13) {
            int sx, sy;
            fresh_player((unsigned)seed);
            memset(&m, 0, sizeof m);
            generate_temple_floor(&m, f, &sx, &sy, &p);
            floors++;

            for (int i = 0; i < m.district_count; i++) {
                const MapDistrict *d = &m.districts[i];
                if (d->kind != DIST_AQUEDUCT && d->kind != DIST_ASSEMBLY) continue;

                for (int y = d->y; y < d->y + d->h; y++) {
                    for (int x = d->x; x < d->x + d->w; x++) {
                        if (x < 0 || x >= MAP_W || y < 0 || y >= MAP_H) continue;
                        TileType t = m.tiles[y][x].type;
                        if (t != TILE_CURRENT && t != TILE_BELT) continue;

                        /* Ride it the way districts_tick() would, and give it
                           more turns than any honest ride needs. */
                        int cx = x, cy = y;
                        int turns = 0;
                        bool ended = false;
                        for (; turns < 200; turns++) {
                            int dx = 0, dy = 0;
                            TileType on = m.tiles[cy][cx].type;
                            if (on == TILE_BELT) {
                                belt_flow_at(&m, cx, cy, &dx, &dy);
                            } else if (on == TILE_CURRENT) {
                                int di = district_index_at(&m, cx, cy);
                                if (di >= 0 && m.districts[di].kind == DIST_AQUEDUCT) {
                                    dx = m.districts[di].flow_dx;
                                    dy = m.districts[di].flow_dy;
                                }
                            }
                            if (!dx && !dy) { ended = true; break; }

                            int px = cx, py = cy;
                            int dist = (on == TILE_BELT) ? BELT_PUSH : CURRENT_PUSH;
                            displace_actor(&m, &cx, &cy, dx, dy, dist, NULL, NULL, NULL);
                            /* Ground against the stop: the ride is over even
                               though the tile still conveys. A person walks
                               off; a delegated walk trips the no-progress
                               detector, which is the correct outcome. */
                            if (cx == px && cy == py) { ended = true; break; }
                        }
                        rides++;
                        check(ended, "a ride on a belt or a current comes to an end");
                        if (!ended) {
                            printf("    loop at (%d,%d) on floor %d seed %d\n", x, y, f, seed);
                            return;   /* one report is enough to act on */
                        }
                    }
                }
            }
        }
    }

    printf("  %d rides over %d floors, every one of them ended\n", rides, floors);
    check(rides > 0, "the sweep actually found belts and currents to ride");
}

/* ---- 2b. quartering a big floor never costs it the way down -------------
 *
 * carve_quarters() is the only pass in mapgen that *removes* connectivity on
 * purpose, and it runs last, after everything has been placed. It verifies
 * itself -- walls a line, floods, repairs, and rolls the line back if the
 * floor is worse for it -- and this is the independent check on that, because
 * a self-verifying pass that verifies itself wrongly is exactly the shape of a
 * bug nobody finds.
 *
 * Asserted on the big worlds only, because that is where it runs: on the
 * Shaft and the Halls a floor is crossed in minutes and quartering it would be
 * a wall in a corridor.
 */
static void test_quarters_keep_the_floor_whole(void) {
    printf("quartering keeps the floor whole\n");

    int floors = 0, quartered = 0, lines = 0;
    long walk_total = 0, walk_lost = 0;
    long dist_total = 0, dist_lost = 0;

    world_size_apply(WORLD_DEEPS);
    for (int seed = 1; seed <= 2; seed++) {
        for (int f = 1; f <= 99; f += 33) {
            int sx, sy;
            fresh_player((unsigned)seed);
            memset(&m, 0, sizeof m);
            generate_temple_floor(&m, f, &sx, &sy, &p);
            floors++;
            lines += m.quarter_lines;
            if (m.quarter_lines > 0) quartered++;

            /* The property the pass exists to preserve. */
            check(tile_reachable(&m, sx, sy, m.stairs_down_x, m.stairs_down_y),
                  "the way down is still reachable from where you arrive");

            /* And it has not quietly walled off a third of the floor: count
               what is walkable against what is reachable. */
            flood(&m, sx, sy);

            /* The districts on a quartered floor, in aggregate. The sweep in
               test_districts_are_reachable() runs on the Shaft and the Halls,
               where quartering does not happen at all -- so without this the
               guard on district reachability has a hole exactly the shape of
               the pass most likely to break it.

               Aggregate, not per district, and that is a correction. Written
               first as "no single district may lose more than half of itself",
               it failed on its first run against a jungle that had lost 811 of
               814 tiles -- and the district turned out to be severed with
               quartering *switched off*. Measured across 1,364 districts at all
               four world sizes, one is severed like that: a rate of about one
               in fourteen hundred, present since long before this pass and
               unrelated to it. A per-district assertion on a six-floor sample
               is therefore a test that fails now and then for reasons the
               person reading it cannot act on, which is worse than no test.
               The aggregate is what quartering can actually move. */
            for (int i = 0; i < m.district_count; i++) {
                const MapDistrict *d = &m.districts[i];
                for (int y = d->y; y < d->y + d->h; y++)
                    for (int x = d->x; x < d->x + d->w; x++) {
                        if (x < 0 || x >= MAP_W || y < 0 || y >= MAP_H) continue;
                        if (!is_walkable_player(&m, x, y)) continue;
                        dist_total++;
                        if (!seen[y][x]) dist_lost++;
                    }
            }
            for (int y = 0; y < MAP_H; y++)
                for (int x = 0; x < MAP_W; x++) {
                    if (!is_walkable_player(&m, x, y)) continue;
                    walk_total++;
                    if (!seen[y][x]) walk_lost++;
                }
        }
    }
    world_size_apply(WORLD_SHAFT);

    double pct = walk_total ? 100.0 * (double)walk_lost / (double)walk_total : 0.0;
    printf("  %d Deeps floors, %d quartered, %d lines, %.2f%% of ground unreachable\n",
           floors, quartered, lines, pct);

    check(floors > 0, "the sweep generated big floors");
    check(quartered > 0, "the big worlds actually get quartered");
    /* The districts alone strand about 1.1% (see the table above); quartering
       must not meaningfully add to that. Two per cent is the bar, and the
       measured figure is what it is -- if this ever fires, the line's own
       repair-or-roll-back has stopped working. */
    check(pct < 2.0, "and quartering does not strand the floor");

    /* And the districts on those floors are no worse off than districts
       anywhere else: the unquartered sweep above measures 1.04% across every
       kind, and the bar here is the 3% that sweep uses. Quartering spares
       district ground entirely -- a dividing line stops at the edge of an
       authored place and makes it a way through instead -- so this should sit
       where the ordinary figure sits. */
    double dpct = dist_total ? 100.0 * (double)dist_lost / (double)dist_total : 0.0;
    printf("  district ground on quartered floors: %.2f%% unreachable\n", dpct);
    check(dpct < 3.0, "and the districts on them are no worse off than any others");

    /* Small worlds are left alone, deliberately. */
    world_size_apply(WORLD_SHAFT);
    int sx, sy;
    fresh_player(3u);
    memset(&m, 0, sizeof m);
    generate_temple_floor(&m, 20, &sx, &sy, &p);
    check(m.quarter_lines == 0, "and a small floor is not quartered at all");
}

/* ---- 3b. and a delegated walk can always get off it ---------------------
 *
 * The other half of check 2, and the half that was missing.
 *
 * is_walkable_delegated() stops a walk *routing onto* conveyed ground. It says
 * nothing about ground the walk is already standing on -- and a body in an
 * aqueduct channel is moved CURRENT_PUSH tiles at the end of every turn, which
 * undoes whatever step the goal-directed route just took. The body ends each
 * turn where it began, the progress signature never changes, and after sixty
 * turns auto-explore reports "getting nowhere here -- stopping".
 *
 * Reported from play in those words -- "the aqueduct... the AI normally gets
 * stuck there, I sometimes have to exit the auto explore to exit those areas"
 * -- and confirmed by the fuzzer: 707 stuck reports in 15,000 keys, and the
 * line immediately before every one of the 707 was "The current takes you 1
 * tiles." After autoexplore_escape_conveyance(): none at all.
 */
static void test_conveyed_ground_can_be_left(void) {
    printf("conveyed ground can be left\n");

    int tiles = 0, escapable = 0, stranded = 0, wrong = 0;
    world_size_apply(WORLD_SHAFT);

    for (int seed = 1; seed <= 20; seed++) {
        for (int f = 1; f <= 99; f += 17) {
            int sx, sy;
            fresh_player((unsigned)seed);
            memset(&m, 0, sizeof m);
            generate_temple_floor(&m, f, &sx, &sy, &p);

            for (int i = 0; i < m.district_count; i++) {
                const MapDistrict *d = &m.districts[i];
                if (d->kind != DIST_AQUEDUCT && d->kind != DIST_ASSEMBLY) continue;
                for (int y = d->y; y < d->y + d->h; y++) {
                    for (int x = d->x; x < d->x + d->w; x++) {
                        if (x < 0 || x >= MAP_W || y < 0 || y >= MAP_H) continue;
                        TileType t = m.tiles[y][x].type;
                        if (t != TILE_CURRENT && t != TILE_BELT) continue;
                        tiles++;

                        int dx = 0, dy = 0;
                        if (!autoexplore_escape_conveyance(&m, x, y, &dx, &dy)) {
                            /* Legitimate only when there is genuinely nothing
                               to step onto -- mid-channel between two banks of
                               water or rock. Counted, not asserted away. */
                            stranded++;
                            continue;
                        }
                        escapable++;
                        int nx = x + dx, ny = y + dy;
                        TileType nt = m.tiles[ny][nx].type;
                        if (nt == TILE_CURRENT || nt == TILE_BELT ||
                            nt == TILE_STAIRS_UP || !is_walkable_player(&m, nx, ny))
                            wrong++;
                    }
                }
            }
        }
    }

    printf("  %d conveyed tiles: %d can be stepped off, %d are mid-channel\n",
           tiles, escapable, stranded);
    check(tiles > 0, "the sweep found channels and belts to stand in");
    check(wrong == 0, "every escape lands on ground that does not move you");
    /* Most conveyed ground has a bank. If this ever inverts, the aqueduct has
       stopped being a channel with sides and become a river nobody can leave. */
    check(escapable > stranded, "most conveyed ground has a bank to step onto");

    /* And the predicate is honest about ground that does not move you: no
       escape offered from ordinary floor, because there is nothing to escape. */
    memset(&m, 0, sizeof m);
    m.tiles[5][5].type = TILE_FLOOR;
    int dx = 0, dy = 0;
    check(!autoexplore_escape_conveyance(&m, 5, 5, &dx, &dy),
          "and no escape is offered from ground that stays still");
}

/* ---- 4. a hold is a hold, not a sentence -------------------------------
 *
 * The snares are the only thing in the game that takes the controls away,
 * so the constants that govern them get asserted rather than assumed: a
 * hold has to be finite and short enough that it reads as a price. And
 * nothing walking under orders should route into one at all -- which is
 * also what keeps a hire from standing in the garden being eaten.
 */
static void test_a_hold_ends(void) {
    printf("a hold ends\n");

    check(SNARE_GARDEN_HOLD > 0 && SNARE_GARDEN_HOLD <= 5, "the garden lets go");
    check(SNARE_SAND_HOLD   > 0 && SNARE_SAND_HOLD   <= 5, "the sand lets go");

    memset(&m, 0, sizeof m);
    world_size_apply(WORLD_SHAFT);
    m.tiles[5][5].type = TILE_SNARE;
    check(is_walkable_player(&m, 5, 5),
          "a person may step into a snare -- that is the decision");
    check(!is_walkable_delegated(&m, 5, 5),
          "nothing under orders routes into a snare");
    check(!is_walkable_monster(&m, 5, 5),
          "and nothing follows you into one");

    /* The mine shaft's holes are the same rule wearing different clothes:
       ground a person may choose and nothing else may be walked into. The
       difference is what it costs -- a snare takes a turn and a hole takes a
       floor, so being routed into one by auto-explore would end the floor
       nobody asked to leave. */
    m.tiles[6][6].type = TILE_PIT;
    check(is_walkable_player(&m, 6, 6),
          "a person may step into a hole -- health for a floor skipped");
    check(!is_walkable_delegated(&m, 6, 6),
          "nothing under orders routes into one");
    check(!is_walkable_monster(&m, 6, 6),
          "and nothing follows you down");

    /* And nothing carries you into one either. A current that swept you down a
       floor would be the staircase bug the displacement code already refuses. */
    m.tiles[7][7].type = TILE_CURRENT;
    for (int x = 8; x < 14; x++) m.tiles[7][x].type = TILE_FLOOR;
    m.tiles[7][10].type = TILE_PIT;
    int px = 7, py = 7;
    displace_actor(&m, &px, &py, 1, 0, 6, NULL, NULL, NULL);
    check(px < 10, "a current stops short of a hole rather than dropping you in");
}

/* Its own state directory, and a roster in it. The barrow only generates
   where there are dead runs to put in it, so without this the sweeps below
   would cover eighteen district kinds on a machine whose owner had never lost
   a run and nineteen on one whose owner had -- and the reachability numbers
   would quietly mean something different on each. */
static void use_scratch_state_dir(void) {
    setenv("AETHER_STATE_DIR", "build/test-state", 1);

    Player ghost;
    memset(&ghost, 0, sizeof ghost);
    apply_class_to_player(&ghost, 0);
    for (int i = 0; i < 4; i++) {
        snprintf(ghost.party[0].name, sizeof ghost.party[0].name, "Testwright %d", i + 1);
        ghost.party[0].level = 4 + i * 3;
        ghost.party[0].maxhp = 40 + i * 25;
        ghost.deepest_floor  = 6 + i * 9;
        ghost.gold           = 900 + i * 400;
        record_fallen(&ghost);
    }
}


/* Can you reach every door in town, at every viewport the game can produce?
 *
   The third shape, and it arrived from a direction the two above do not cover:
   not a floor the generator cut badly, but the *town*, rebuilt to a different
   size depending on the render mode. generate_town_map() laid the plaza out to
   VIEW_W x VIEW_H, which was true enough while every mode showed 58 columns.
   Tile mode shrinks the viewport to fit a pixel budget, so climbing the stairs
   in tile mode rebuilt the plaza narrower -- and the buildings past the new
   edge were written into ground that had never been floored. The Armory, the
   Inn and the lizard track: walled in, unlit, and unreachable, with no message
   and nothing to walk into.

   Sweeps the widths rather than checking one, because the failure is a
   function of the width and the interesting ones are the narrow ones. */
static void test_every_door_in_town_is_reachable(void) {
    printf("\nevery door in town, at every width\n");

    static const struct { const char *name; int x, y; } DOOR[] = {
        { "general",     7,  5 }, { "armory",      50, 5 },
        { "kitchen",    13,  5 }, { "apothecary",   7, 12 },
        { "arcanist",   20,  5 }, { "inn",         46, 12 },
        { "tavern",     36, 12 }, { "junkyard",    14, 12 },
        { "black market", 21, 12 }, { "gladiator",  38, 5 },
        { "bank",       27,  5 }, { "races",       54, 12 },
        { "oracle",     44,  5 }, { "altar",       33, 5 },
        { "bazaar",     41, 12 },
        { "temple",     TEMPLE_DOOR_X, TEMPLE_DOOR_Y },
    };
    const int NDOOR = (int)(sizeof DOOR / sizeof DOOR[0]);
    static const int WIDTH[] = { 80, 58, 52, 46, 40, 30 };

    int save_w = g_view_w, save_h = g_view_h;
    Map *m = calloc(1, sizeof *m);
    if (!m) { printf("  FAIL: out of memory\n"); failures++; return; }

    int bad = 0;
    for (size_t wi = 0; wi < sizeof WIDTH / sizeof WIDTH[0]; wi++) {
        g_view_w = WIDTH[wi];
        g_view_h = 18;
        generate_town_map(m);

        /* Flood from where the player actually stands on arrival. */
        static char seen[MAP_H_MAX][MAP_W_MAX];
        memset(seen, 0, sizeof seen);
        int qx[16384], qy[16384], head = 0, tail = 0;
        qx[tail] = PLAYER_TOWN_START_X; qy[tail++] = PLAYER_TOWN_START_Y;
        while (head < tail) {
            int x = qx[head], y = qy[head]; head++;
            if (x < 0 || y < 0 || x >= MAP_W || y >= MAP_H || seen[y][x]) continue;
            seen[y][x] = 1;
            if (!is_walkable_player(m, x, y)) continue;   /* a door is an endpoint */
            if (tail + 4 >= 16384) continue;
            qx[tail] = x + 1; qy[tail++] = y;
            qx[tail] = x - 1; qy[tail++] = y;
            qx[tail] = x; qy[tail++] = y + 1;
            qx[tail] = x; qy[tail++] = y - 1;
        }

        int cut = 0;
        for (int i = 0; i < NDOOR; i++)
            if (!seen[DOOR[i].y][DOOR[i].x]) {
                printf("  FAIL: at %d columns the %s is walled off\n",
                       WIDTH[wi], DOOR[i].name);
                cut++;
            }
        if (cut == 0) printf("  %2d columns: all %d doors reachable\n", WIDTH[wi], NDOOR);
        bad += cut;
    }

    free(m);
    g_view_w = save_w; g_view_h = save_h;
    check(bad == 0, "the town is the same town whatever the window can show of it");
}

int main(void) {
    printf("=== movement: can anything get stuck? ===\n\n");
    use_scratch_state_dir();

    test_districts_are_reachable();
    printf("\n");
    test_delegated_walks_refuse_conveyed_ground();
    printf("\n");
    test_quarters_keep_the_floor_whole();
    printf("\n");
    test_conveyance_terminates();
    printf("\n");
    test_conveyed_ground_can_be_left();
    printf("\n");
    test_a_hold_ends();
    test_every_door_in_town_is_reachable();

    printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "PASSED",
           failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
