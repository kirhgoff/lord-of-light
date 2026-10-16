#include "vision.h"

#include <stdint.h>

#include "movement.h"

#define NEVER INT64_MAX

static int64_t absolute(int64_t value)
{
    return value < 0 ? -value : value;
}

static int64_t first_crossing(int64_t position, int tile, int direction, int64_t other_axis_span)
{
    if (direction == 0) return NEVER;
    int64_t to_edge = direction > 0 ? (tile + 1) * (int64_t)TILE_MILLIPX - position : position - tile * (int64_t)TILE_MILLIPX;
    return to_edge * other_axis_span;
}

bool line_of_sight(const Battle *battle, int from_x, int from_y, int to_x, int to_y)
{
    int64_t dx = to_x - from_x, dy = to_y - from_y;
    int column = from_x / TILE_MILLIPX, row = from_y / TILE_MILLIPX;
    int end_column = to_x / TILE_MILLIPX, end_row = to_y / TILE_MILLIPX;
    int step_x = (dx > 0) - (dx < 0), step_y = (dy > 0) - (dy < 0);
    int64_t next_x = first_crossing(from_x, column, step_x, absolute(dy));
    int64_t next_y = first_crossing(from_y, row, step_y, absolute(dx));
    int64_t cost_x = TILE_MILLIPX * absolute(dy), cost_y = TILE_MILLIPX * absolute(dx);
    for (;;) {
        if (tile_is_solid(battle, column, row)) return false;
        if (column == end_column && row == end_row) return true;
        if (column == end_column) next_x = NEVER;
        if (row == end_row) next_y = NEVER;
        if (next_x < next_y) {
            column += step_x;
            next_x += cost_x;
        } else if (next_y < next_x) {
            row += step_y;
            next_y += cost_y;
        } else {
            if (tile_is_solid(battle, column + step_x, row) || tile_is_solid(battle, column, row + step_y)) return false;
            column += step_x;
            row += step_y;
            next_x += cost_x;
            next_y += cost_y;
        }
    }
}

void combatant_vision_ranges(const Combatant *combatant, const GameData *data, int range_millipx[3])
{
    bool working[MAX_SLOTS];
    body_working_parts(&combatant->body, combatant->parts, working);
    range_millipx[BAND_CLOSE] = range_millipx[BAND_MEDIUM] = range_millipx[BAND_FAR] = 0;
    for (int s = 0; s < combatant->body.slot_count; s++) {
        if (!working[s]) continue;
        const Part *part = &data->parts[combatant->body.slots[s].part];
        int ranges[3] = {part->vision_close, part->vision_medium, part->vision_far};
        for (int b = 0; b < 3; b++)
            if (ranges[b] * MILLIPX_PER_PX > range_millipx[b]) range_millipx[b] = ranges[b] * MILLIPX_PER_PX;
    }
}

static int band_at(const int range_millipx[3], int64_t distance_squared)
{
    for (int b = 0; b < 3; b++)
        if (range_millipx[b] > 0 && distance_squared <= (int64_t)range_millipx[b] * range_millipx[b]) return b;
    return UNKNOWN;
}

static int health_quarters(const Combatant *combatant)
{
    return combatant->vitality_max ? (combatant->vitality * 4 + combatant->vitality_max - 1) / combatant->vitality_max : 0;
}

static void add(Perception *out, Perceived seen)
{
    out->seen[out->seen_count++] = seen;
}

void perceive(const Battle *battle, const GameData *data, int self, Perception *out)
{
    const Combatant *me = &battle->combatants[self];
    out->seen_count = 0;
    combatant_vision_ranges(me, data, out->range_millipx);

    for (int c = 0; c < battle->combatant_count; c++) {
        const Combatant *other = &battle->combatants[c];
        if (c == self || !other->alive) continue;
        int64_t dx = other->x_millipx - me->x_millipx, dy = other->y_millipx - me->y_millipx;
        int band = band_at(out->range_millipx, dx * dx + dy * dy);
        bool enemy = other->side != me->charm_side;
        if (band == UNKNOWN || (band == BAND_FAR && !enemy)) continue;
        if (!line_of_sight(battle, me->x_millipx, me->y_millipx, other->x_millipx, other->y_millipx)) continue;
        bool detailed = band != BAND_FAR;
        add(out, (Perceived){PERCEIVED_COMBATANT, c, (Band)band, UNKNOWN, detailed ? other->side : UNKNOWN,
                             detailed ? health_quarters(other) : UNKNOWN, detailed ? other->name : NULL,
                             other->x_millipx, other->y_millipx});
    }

    for (int p = 0; p < battle->pickup_count + battle->trap_count; p++) {
        bool is_trap = p >= battle->pickup_count;
        const FloorObject *object = is_trap ? &battle->traps[p - battle->pickup_count] : &battle->pickups[p];
        const ArenaObjectDef *def = is_trap ? &data->traps[object->def] : &data->pickups[object->def];
        if (object->respawn_in > 0) continue;
        int64_t dx = object->x_millipx - me->x_millipx, dy = object->y_millipx - me->y_millipx;
        int band = band_at(out->range_millipx, dx * dx + dy * dy);
        if (band == UNKNOWN || (is_trap && band != BAND_CLOSE) || (band == BAND_FAR && def->heal <= 0)) continue;
        if (!line_of_sight(battle, me->x_millipx, me->y_millipx, object->x_millipx, object->y_millipx)) continue;
        add(out, (Perceived){is_trap ? PERCEIVED_TRAP : PERCEIVED_PICKUP, is_trap ? p - battle->pickup_count : p, (Band)band,
                             band == BAND_FAR ? UNKNOWN : object->def, UNKNOWN, UNKNOWN, NULL,
                             object->x_millipx, object->y_millipx});
    }
}
