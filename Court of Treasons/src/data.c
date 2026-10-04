/* The tables.  Everything here is generated from design.xml except the
 * names, which exist so a log line is readable. */
#include "court.h"

#include "cards.inc"

const char *HOUSE_NAME[H_COUNT] = {
    "Ravenmark", "Vipren", "Goldwyn", "Aldemar",
    "Leoward", "Stonegarth", "Wulfren", "Everhold"
};
const char *RES_NAME[R_COUNT] = { "Military", "Capital", "Gold" };

/* One name per opcode, in enum order.  The compiler will not check that
 * this list stays aligned with the enum, so tests.c does: a table with a
 * hole in it prints the wrong effect in every log line after the hole,
 * which is the kind of thing that survives a long time unnoticed. */
const char *OP_NAME[OP_COUNT] = {
    "none",
    "levy", "standing", "levy-steal", "levy-steal-lp", "standing-loss",
    "standing-loss-lp", "levy-all-lose", "levy-all-gain",
    "standing-all-loss", "merc-market", "levy-per-kept", "levy-keep",
    "vassal-levy",
    "unrest-clear", "unrest-target", "unrest-self", "crackdown",
    "grievance-self", "grievance-target", "unrest-all", "no-tax", "regency",
    "servitude", "bond-rider", "lord-trait", "lord-free", "lord-direct",
    "revolution-zero", "revolution-freeze",
    "revolution-all", "revolution-theirs", "revolt-cancel", "pardon", "manumit", "spy",
    "spy-any", "expose", "sell-secret", "instigator", "instigator-redirect",
    "propose", "propose-double", "propose-bonus", "propose-public",
    "promise-cancel", "promise-launder", "promise-claim", "promise-coerce",
    "broken-transfer", "call-debt", "broken-truce", "rumour",
    "realm-remembers", "reckoning-now",
    "declare-war", "combat-levy", "combat-levy-def", "combat-reduce",
    "combat-att-loss", "no-combat", "lowest-gain",
    "discard-target", "grievance-scale",
    "random-loss", "lp-pay", "no-assassin", "skip-action",
    "levy-give", "survive", "combat-levy-if",
    "lp-gain", "servitude-all", "no-spy",
    "claim-throne", "make-paramount",
    "draw", "draw-deck", "no-promise",
    "vote-double", "tie-break", "vote-nullify", "restore", "extinguish", "draw-all", "win-gold", "levy-double", "invest",
    "cancel-next", "steal-card", "give-card", "no-spoil",
    "shield", "tax-double",
    "coalition", "throne-levy", "throne-bar", "throne-oath",
    "throne-discount", "assassinate", "extra-draw",
    "council", "vote-buy", "vote-gain", "vote-per-kept", "vote-command",
    "vote-free", "vote-change", "no-crowning", "object-free",
    "ambition-done", "favour-other", "favour", "favour-lock", "favour-cmp",
    "court-silent",
    "peek-hand",
    "amb-standing", "amb-vassals", "amb-unbroken", "amb-kept",
    "amb-combats", "amb-paramount", "amb-rival-unrest",
    "amb-beat-oathbreaker"
};
