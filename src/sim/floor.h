#ifndef SIM_FLOOR_H
#define SIM_FLOOR_H

#include "battle.h"

#define FLOOR_MARGIN_TILES 8
#define FLOOR_SPACING_TILES 3
#define FLOOR_PLACEMENT_TRIES 64

void battle_place_floor_objects(Battle *battle, const GameData *data);
void battle_tick_floor(Battle *battle, const GameData *data);

#endif
