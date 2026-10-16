#include "floor.h"

#include <stdlib.h>

#include "combat.h"
#include "movement.h"

static int64_t squared(int64_t value)
{
    return value * value;
}

static bool tile_area_is_clear(const Battle *battle, int column, int row)
{
    for (int dr = -1; dr <= 1; dr++)
        for (int dc = -1; dc <= 1; dc++)
            if (tile_is_solid(battle, column + dc, row + dr)) return false;
    return true;
}

static bool far_from(const FloorObject *objects, int count, int column, int row)
{
    for (int i = 0; i < count; i++) {
        int dc = abs(objects[i].x_millipx / TILE_MILLIPX - column), dr = abs(objects[i].y_millipx / TILE_MILLIPX - row);
        if (dc < FLOOR_SPACING_TILES && dr < FLOOR_SPACING_TILES) return false;
    }
    return true;
}

static void random_clear_tile(Battle *battle, int *column, int *row)
{
    do {
        *row = rng_below(&battle->rng, ARENA_HEIGHT_TILES);
        *column = FLOOR_MARGIN_TILES + rng_below(&battle->rng, ARENA_WIDTH_TILES - 2 * FLOOR_MARGIN_TILES);
    } while (!tile_area_is_clear(battle, *column, *row));
}

static FloorObject place_one(Battle *battle, int def)
{
    int column = 0, row = 0;
    for (int attempt = 0; attempt < FLOOR_PLACEMENT_TRIES; attempt++) {
        random_clear_tile(battle, &column, &row);
        if (far_from(battle->pickups, battle->pickup_count, column, row) &&
            far_from(battle->traps, battle->trap_count, column, row))
            break;
    }
    return (FloorObject){def, column * TILE_MILLIPX + TILE_MILLIPX / 2, row * TILE_MILLIPX + TILE_MILLIPX / 2, 0};
}

void battle_place_floor_objects(Battle *battle, const GameData *data)
{
    for (int d = 0; d < data->pickup_count; d++) {
        FloorObject placed = place_one(battle, d);
        battle->pickups[battle->pickup_count++] = placed;
    }
    for (int d = 0; d < data->trap_count; d++) {
        FloorObject placed = place_one(battle, d);
        battle->traps[battle->trap_count++] = placed;
    }
}

static bool touching(const Combatant *who, const FloorObject *object, int radius_px)
{
    int64_t reach = (int64_t)radius_px * MILLIPX_PER_PX;
    return squared(who->x_millipx - object->x_millipx) + squared(who->y_millipx - object->y_millipx) <= squared(reach);
}

static int heal_vitality(Combatant *who, int heal)
{
    int gain = who->vitality_max - who->vitality;
    if (gain > heal) gain = heal;
    who->vitality += gain;
    return gain;
}

static int mend_parts(Combatant *who, const GameData *data, int mend)
{
    int total = 0;
    for (int s = 0; s < who->body.slot_count; s++) {
        int max = data->parts[who->body.slots[s].part].integrity;
        int *integrity = &who->parts[s].integrity;
        if (*integrity <= 0 || *integrity >= max) continue;
        int gain = max - *integrity < mend ? max - *integrity : mend;
        *integrity += gain;
        total += gain;
    }
    return total;
}

static int buff(int *percent, int *ticks, int value, int duration)
{
    if (value <= 0) return 0;
    *percent = value;
    *ticks = duration;
    return value;
}

static int apply_pickup(Combatant *who, const GameData *data, const ArenaObjectDef *def)
{
    return heal_vitality(who, def->heal) + mend_parts(who, data, def->mend) +
           buff(&who->damage_bonus_percent, &who->damage_bonus_ticks, def->damage_bonus_percent, def->effect_ticks) +
           buff(&who->haste_percent, &who->haste_ticks, def->speed_bonus_percent, def->effect_ticks) +
           buff(&who->vision_percent, &who->vision_ticks, def->vision_bonus_percent, def->effect_ticks) +
           buff(&who->ward_percent, &who->ward_ticks, def->ward_percent, def->effect_ticks);
}

static void take_pickups(Battle *battle, const GameData *data)
{
    for (int p = 0; p < battle->pickup_count; p++) {
        FloorObject *object = &battle->pickups[p];
        const ArenaObjectDef *def = &data->pickups[object->def];
        if (object->respawn_in > 0) continue;
        for (int c = 0; c < battle->combatant_count; c++) {
            Combatant *who = &battle->combatants[c];
            if (!who->alive || !touching(who, object, def->radius)) continue;
            int amount = apply_pickup(who, data, def);
            if (amount == 0) continue;
            battle_log_event(battle, (Event){0, EVENT_PICKUP, c, -1, -1, -1, -1, amount, p, DECIDER_NONE});
            object->respawn_in = def->respawn_ticks;
            break;
        }
    }
}

static void spring_traps(Battle *battle, const GameData *data)
{
    for (int t = 0; t < battle->trap_count; t++) {
        FloorObject *object = &battle->traps[t];
        const ArenaObjectDef *def = &data->traps[object->def];
        if (object->respawn_in > 0) continue;
        for (int c = 0; c < battle->combatant_count; c++) {
            if (!battle->combatants[c].alive || !touching(&battle->combatants[c], object, def->radius)) continue;
            battle_log_event(battle, (Event){0, EVENT_TRAP, -1, -1, c, -1, def->element, def->damage, t, DECIDER_NONE});
            object->respawn_in = def->respawn_ticks;
            combatant_hit(battle, data, -1, -1, c, def->element, def->damage);
            break;
        }
    }
}

static void count_down(FloorObject *objects, int count)
{
    for (int i = 0; i < count; i++)
        if (objects[i].respawn_in > 0) objects[i].respawn_in--;
}

void battle_tick_floor(Battle *battle, const GameData *data)
{
    count_down(battle->pickups, battle->pickup_count);
    count_down(battle->traps, battle->trap_count);
    take_pickups(battle, data);
    spring_traps(battle, data);
}
