#ifndef QUESTS_H
#define QUESTS_H

#include "common.h"

/* ---- the bounty board --------------------------------------------------
 *
 * Six kinds. Three were here; three are ROADMAP Phase 3's, and the reason they
 * are worth writing down is what unblocked them.
 *
 *   KILL / FETCH / CLEAR   the originals
 *   TIMED                  be on floor N before the clock runs out
 *   NORECALL               get there without cracking a charm
 *   ESCORT                 get somebody else there alive
 *
 * TIMED and NORECALL are a deadline and a flag -- "no new machinery", exactly
 * as the roadmap predicted. ESCORT carried "blocked on a friendly-NPC actor
 * that does not exist" from the day it was written, and the blocker cleared
 * without anybody working on it: once there was one kind of body and a
 * constructor that could make one without a roster seat, a client became a
 * party slot and a bounty field. There is no client struct and no client code
 * path -- they walk, fight and die under exactly the same rules as anybody
 * else, which is the whole point.
 *
 * This lives in its own file rather than in main.c because the test suite
 * links every module except main.o. Quest logic that cannot be tested is quest
 * logic that gets found by playing, and this session has spent enough time on
 * that particular lesson.
 */

/* Posts a new bounty, replacing whatever was there. For ESCORT this also takes
   the client on, and falls back to a KILL bounty if the party is full --
   offering a bounty that cannot be started is worse than offering a dull one. */
void quest_offer(Player *p);

/* Settles the three bounties that are decided by arriving somewhere. Call on
   every floor arrival, before anything else reads the quest state. */
void quest_check_arrival(Player *p);

/* A recall charm has been spent. Breaks a NORECALL bounty and does nothing
   otherwise. Called from the one place a charm is actually consumed. */
void quest_note_recall(Player *p);

/* Tears the notice down, releasing the client if there is one. */
void quest_abandon(Player *p);

/* One line for the log or a report, for any bounty kind. */
const char *quest_summary(const Player *p);

#endif
