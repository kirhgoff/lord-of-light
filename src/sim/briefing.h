#ifndef SIM_BRIEFING_H
#define SIM_BRIEFING_H

#include "battle.h"

#define BRIEFING_MAX 1200

int briefing_write(const Battle *battle, const GameData *data, int combatant, char *out, size_t size);

#endif
