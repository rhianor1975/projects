#include "quests.h"
#include "companions.h"
#include "combat.h"
#include "monsters.h"

/* See quests.h for what the six kinds are and why three of them were blocked
   until they were not. */

/* Flavor names for QUEST_FETCH's target item -- deliberately generic
   Company/temple debris rather than anything with its own stats, since the
   point is the trip, not the item. Each already carries its own article
   so it drops straight into "recover %s" / "bring back %s" templates. */
static const char *QUEST_ITEM_NAMES[] = {
    "a Sealed Dispatch Case", "a Cracked Pressure Gauge", "a Waterlogged Ledger",
    "a Company Signet", "a Corroded Aether Vial", "a Torn Survey Map",
    "a Rusted Union Badge", "a Sealed Specimen Jar",
};
#define QUEST_ITEM_NAME_COUNT (int)(sizeof(QUEST_ITEM_NAMES) / sizeof(QUEST_ITEM_NAMES[0]))

/* Lets the client go, whatever became of them, and forgets the slot. Safe to
   call when there is no client. Every path out of an escort goes through it --
   paid, failed, or abandoned -- because the one that did not was the one that
   stranded them. */
static void quest_release_client(Player *p) {
    int s = p->quest_escort_slot;
    if (s >= 1 && s < MAX_PARTY) {
        p->party[s].in_use = false;
        p->party[s].alive  = false;
    }
    p->quest_escort_slot = -1;
}

/* Takes on the board's client: a free hire who has to be kept alive.
 *
   No client struct and no client code path -- hero_create() builds them like
   anybody else and they walk, fight and die under the same rules. That is what
   "blocked on a friendly-NPC actor that does not exist" turned into once there
   was one kind of body: a party slot and a bounty field. */
static bool quest_take_on_client(Player *p) {
    int slot = -1;
    for (int i = 1; i < MAX_PARTY; i++) if (!p->party[i].in_use) { slot = i; break; }
    if (slot < 0) return false;

    Hero *c = &p->party[slot];
    tavern_candidate(p, rand() % TAVERN_ROSTER, c);
    c->in_use = true;
    c->alive  = true;
    c->roster_idx = -1;          /* not of the Tavern: nobody to rehire them from */
    c->temporary  = false;       /* they stay until the bounty is settled */
    c->tier = 0;
    c->x = hero_driven(p)->x;
    c->y = hero_driven(p)->y;
    p->quest_escort_slot = slot;
    return true;
}

/* Called on every floor arrival. The three arrival-checked bounties settle
   here, because "reach floor N" is a thing that happens when you arrive and
   nowhere else. */
void quest_check_arrival(Player *p) {
    if (!p->quest_active) return;
    if (p->quest_type != QUEST_TIMED && p->quest_type != QUEST_NORECALL
        && p->quest_type != QUEST_ESCORT) return;
    if (p->floor != p->quest_floor) return;

    const char *why = NULL;
    if (p->quest_type == QUEST_TIMED && p->turns > p->quest_deadline)
        why = "The bounty was on the clock, and the clock ran out.";
    if (p->quest_type == QUEST_NORECALL && p->quest_broken)
        why = "The bounty said no charms. The charm is spent.";
    if (p->quest_type == QUEST_ESCORT) {
        int s = p->quest_escort_slot;
        if (s < 1 || s >= MAX_PARTY || !hero_is_up(&p->party[s]))
            why = "You arrive without your client. Nobody is paying for that.";
    }
    if (why) {
        log_msg("%s", why);
        /* Release the client whatever became of them. A failed escort used to
           leave them holding a party slot with no bounty attached -- alive and
           following you for ever, or dead and occupying a seat nobody could
           fill. Either way a slot the player could not get back. */
        quest_release_client(p);
        p->quest_active = false;
        return;
    }

    int gold = player_gain_gold(p, p->quest_gold_reward);
    grant_xp(p, hero_driven(p), p->quest_xp_reward);
    if (p->quest_type == QUEST_ESCORT) {
        log_msg("%s pays out and goes their own way. (+%d gold, +%d xp)",
                p->party[p->quest_escort_slot].name, gold, p->quest_xp_reward);
        quest_release_client(p);
    } else {
        log_msg("Bounty complete! Floor %d, as asked. (+%d gold, +%d xp)",
                p->quest_floor, gold, p->quest_xp_reward);
    }
    p->quest_active = false;
    p->quest_escort_slot = -1;
}

void quest_offer(Player *p) {
    int base = p->deepest_floor > 0 ? p->deepest_floor : 1;
    int lo = base > 5 ? base - 5 : 1;
    int hi = base + 3;
    if (hi > MAX_FLOOR) hi = MAX_FLOOR;
    if (lo > hi) lo = hi;
    p->quest_floor = lo + rand() % (hi - lo + 1);
    p->quest_gold_reward = 40 + p->quest_floor * 4 + rand() % 40;
    p->quest_xp_reward = 15 + p->quest_floor * 3 + rand() % 15;
    p->quest_type = rand() % QUEST_TYPE_COUNT;

    /* Every per-type field is reset here, not just the ones this type
       happens to use -- otherwise a KILL or FETCH bounty silently carries
       the previous CLEAR bounty's kill target around, which is misleading
       to anything that reads it later. */
    p->quest_monster[0] = '\0';
    p->quest_item_found = false;
    p->quest_kills_done = 0;
    p->quest_kills_needed = 0;
    p->quest_deadline = 0;
    p->quest_broken = false;
    /* Any client still standing from a previous bounty goes home first. This
       is belt and braces -- every exit releases them -- but an orphaned body in
       a party slot is unrecoverable, so it is worth being sure. */
    quest_release_client(p);

    switch (p->quest_type) {
        case QUEST_FETCH:
            strncpy(p->quest_monster, QUEST_ITEM_NAMES[rand() % QUEST_ITEM_NAME_COUNT], sizeof(p->quest_monster) - 1);
            p->quest_monster[sizeof(p->quest_monster) - 1] = '\0';
            log_msg("New bounty: recover %s from floor %d and bring it back here.", p->quest_monster, p->quest_floor);
            break;
        case QUEST_CLEAR:
            p->quest_kills_needed = 5 + p->quest_floor / 10 + rand() % 4;
            p->quest_gold_reward = p->quest_gold_reward * 3 / 2;
            p->quest_xp_reward = p->quest_xp_reward * 3 / 2;
            log_msg("New bounty: clear %d foes from floor %d.", p->quest_kills_needed, p->quest_floor);
            break;
        case QUEST_TIMED: {
            /* Enough turns to walk it at a fair pace, and not enough to clear
               every floor on the way. Scaled by depth because a deep floor is
               a longer walk before it is a harder one. */
            long budget = 400 + (long)p->quest_floor * 90;
            p->quest_deadline = p->turns + budget;
            p->quest_gold_reward = p->quest_gold_reward * 2;
            p->quest_xp_reward   = p->quest_xp_reward * 2;
            log_msg("New bounty: be standing on floor %d within %ld turns.",
                    p->quest_floor, budget);
            break;
        }

        case QUEST_NORECALL:
            /* No new machinery, exactly as the ROADMAP said: one flag, set
               where a charm is spent. */
            p->quest_gold_reward = p->quest_gold_reward * 2;
            p->quest_xp_reward   = p->quest_xp_reward * 2;
            log_msg("New bounty: reach floor %d without cracking a recall charm.",
                    p->quest_floor);
            break;

        case QUEST_ESCORT:
            /* The client is hired on the board's coin and joins the party as an
               ordinary Hero. Everything that makes them different is in this
               one bounty's fields; there is no client type, because a body is
               a body -- which is the whole reason this stopped being blocked. */
            if (!quest_take_on_client(p)) {
                /* Nowhere to put them. Fall back rather than offer a bounty
                   that cannot be started. */
                p->quest_type = QUEST_KILL;
                strncpy(p->quest_monster, pick_quest_monster_name(p->quest_floor), sizeof(p->quest_monster) - 1);
                p->quest_monster[sizeof(p->quest_monster) - 1] = '\0';
                log_msg("New bounty: slay a %s on floor %d.", p->quest_monster, p->quest_floor);
                break;
            }
            p->quest_gold_reward = p->quest_gold_reward * 3;
            p->quest_xp_reward   = p->quest_xp_reward * 2;
            log_msg("New bounty: %s wants to reach floor %d. Keep them breathing.",
                    p->party[p->quest_escort_slot].name, p->quest_floor);
            break;

        default:
            p->quest_type = QUEST_KILL;
            strncpy(p->quest_monster, pick_quest_monster_name(p->quest_floor), sizeof(p->quest_monster) - 1);
            p->quest_monster[sizeof(p->quest_monster) - 1] = '\0';
            log_msg("New bounty: slay a %s on floor %d.", p->quest_monster, p->quest_floor);
            break;
    }
    p->quest_active = true;
}


void quest_note_recall(Player *p) {
    if (p && p->quest_active && p->quest_type == QUEST_NORECALL) p->quest_broken = true;
}

void quest_abandon(Player *p) {
    if (!p || !p->quest_active) return;
    quest_release_client(p);
    p->quest_active = false;
}

const char *quest_summary(const Player *p) {
    static char buf[128];
    if (!p || !p->quest_active) return "no bounty";
    switch (p->quest_type) {
        case QUEST_FETCH:
            snprintf(buf, sizeof buf, "recover %s from floor %d", p->quest_monster, p->quest_floor);
            break;
        case QUEST_CLEAR:
            snprintf(buf, sizeof buf, "clear %d foes from floor %d (%d done)",
                     p->quest_kills_needed, p->quest_floor, p->quest_kills_done);
            break;
        case QUEST_TIMED:
            snprintf(buf, sizeof buf, "reach floor %d within %ld turns",
                     p->quest_floor, p->quest_deadline - p->turns);
            break;
        case QUEST_NORECALL:
            snprintf(buf, sizeof buf, "reach floor %d without a charm%s",
                     p->quest_floor, p->quest_broken ? " (broken)" : "");
            break;
        case QUEST_ESCORT: {
            int s = p->quest_escort_slot;
            bool up = (s >= 1 && s < MAX_PARTY && hero_is_up(&p->party[s]));
            snprintf(buf, sizeof buf, "get %s to floor %d%s",
                     up ? p->party[s].name : "your client", p->quest_floor,
                     up ? "" : " (dead)");
            break;
        }
        default:
            snprintf(buf, sizeof buf, "slay a %s on floor %d", p->quest_monster, p->quest_floor);
            break;
    }
    return buf;
}
