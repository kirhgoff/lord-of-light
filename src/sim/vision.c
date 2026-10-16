#include "vision.h"

#include <stdint.h>

#include "movement.h"

#define LOS_SAMPLE_MILLIPX (2 * MILLIPX_PER_PX)

bool line_of_sight(const Battle *battle, int from_x, int from_y, int to_x, int to_y)
{
    int64_t dx = to_x - from_x, dy = to_y - from_y;
    int64_t longest = (dx < 0 ? -dx : dx) > (dy < 0 ? -dy : dy) ? (dx < 0 ? -dx : dx) : (dy < 0 ? -dy : dy);
    int64_t samples = longest / LOS_SAMPLE_MILLIPX + 1;
    for (int64_t i = 0; i <= samples; i++) {
        int x = (int)(from_x + dx * i / samples), y = (int)(from_y + dy * i / samples);
        if (tile_is_solid(battle, x / TILE_MILLIPX, y / TILE_MILLIPX)) return false;
    }
    return true;
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
        add(out, (Perceived){PERCEIVED_COMBATANT, c, (Band)band, UNKNOWN,
                             band == BAND_FAR ? UNKNOWN : other->side, band == BAND_FAR ? UNKNOWN : health_quarters(other)});
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
                             band == BAND_FAR ? UNKNOWN : object->def, UNKNOWN, UNKNOWN});
    }
}
