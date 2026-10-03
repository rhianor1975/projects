#ifndef FEATURES_H
#define FEATURES_H

#include "common.h"

/* Resolves the effect of stepping onto a shrine, fountain, ancient machine
   or relic. Does nothing for FEATURE_MERCHANT (handled separately as a
   shop loop in main.c) or for an already-used one-shot feature. */
void trigger_feature(Map *m, MapFeature *f, Player *p);

#endif
