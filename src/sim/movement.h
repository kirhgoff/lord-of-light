#ifndef SIM_MOVEMENT_H
#define SIM_MOVEMENT_H

#include "battle.h"

#define COMBATANT_RADIUS_MILLIPX (11 * MILLIPX_PER_PX)
#define TICKS_PER_SECOND 20
#define PATH_NONE (-1)

bool tile_is_solid(const Battle *battle, int column, int row);
int combatant_speed_px_per_second(const Combatant *combatant, const GameData *data);
bool combatant_move_target(const Combatant *combatant, int *x_millipx, int *y_millipx);
int find_path(const Battle *battle, int from_column, int from_row, int to_column, int to_row, int *next_column, int *next_row);
void battle_move_combatants(Battle *battle, const GameData *data);

#endif
