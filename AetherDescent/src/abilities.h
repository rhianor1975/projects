#ifndef ABILITIES_H
#define ABILITIES_H

#include "common.h"

const char *ability_name(int id);
const char *ability_desc(int id);
int ability_cooldown_max(int id);

/* Decrements the ability's cooldown. Call once per player turn, alongside
   tick_spells/tick_ranged. */
void tick_ability(Hero *h);

/* Uses the player's innate ability. Logs the reason and returns false (no
   turn consumed) if it's still on cooldown. */
bool use_ability(Player *p, Hero *h, Map *m, bool *out_boss_killed);

#endif
