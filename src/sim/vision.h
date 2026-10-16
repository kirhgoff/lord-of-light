#ifndef SIM_VISION_H
#define SIM_VISION_H

#include "battle.h"

#define MAX_PERCEIVED (MAX_COMBATANTS + 2 * MAX_ARENA_OBJECTS)
#define UNKNOWN (-1)

typedef enum { BAND_CLOSE, BAND_MEDIUM, BAND_FAR } Band;
typedef enum { PERCEIVED_COMBATANT, PERCEIVED_PICKUP, PERCEIVED_TRAP } PerceivedKind;

typedef struct {
    PerceivedKind kind;
    int index;
    Band band;
    int def;
    int side;
    int health_quarters;
    const char *name;
    int x_millipx;
    int y_millipx;
} Perceived;

typedef struct {
    int range_millipx[3];
    Perceived seen[MAX_PERCEIVED];
    int seen_count;
} Perception;

bool line_of_sight(const Battle *battle, int from_x_millipx, int from_y_millipx, int to_x_millipx, int to_y_millipx);
void combatant_vision_ranges(const Combatant *combatant, const GameData *data, int range_millipx[3]);
void perceive(const Battle *battle, const GameData *data, int combatant, Perception *out);

#endif
