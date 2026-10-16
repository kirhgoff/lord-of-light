#include "battle.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

uint32_t rng_next(uint32_t *state)
{
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

int rng_below(uint32_t *state, int bound)
{
    return (int)(rng_next(state) % (uint32_t)bound);
}

void battle_log_event(Battle *battle, EventKind kind, int a, int b, int value)
{
    if (battle->log_count == MAX_EVENTS) return;
    battle->log[battle->log_count++] = (Event){battle->tick, kind, a, b, value};
}

void body_working_parts(const Body *body, const PartState *parts, bool working[MAX_SLOTS])
{
    int queue[MAX_SLOTS];
    int head = 0, tail = 0;
    memset(working, 0, sizeof(bool) * MAX_SLOTS);
    if (parts[body->core].integrity <= 0) return;
    working[body->core] = true;
    queue[tail++] = body->core;
    while (head < tail) {
        int at = queue[head++];
        for (int e = 0; e < body->edge_count; e++) {
            int next = body->edges[e].a == at ? body->edges[e].b : body->edges[e].b == at ? body->edges[e].a : -1;
            if (next < 0 || working[next] || parts[next].integrity <= 0) continue;
            working[next] = true;
            queue[tail++] = next;
        }
    }
}

static void init_combatant(Combatant *c, const GameData *data, const char *name, Side side, const Body *body, int x, int y)
{
    memset(c, 0, sizeof *c);
    snprintf(c->name, sizeof c->name, "%s", name);
    c->side = side;
    c->alive = true;
    c->x = x;
    c->y = y;
    c->body = *body;
    c->charm_side = side;
    c->decision.vulnerable_element = -1;
    for (int s = 0; s < body->slot_count; s++) {
        const Part *part = &data->parts[body->slots[s].part];
        c->parts[s].integrity = part->integrity;
        c->parts[s].order = (Order){ORDER_IDLE, -1, 0, 0};
        c->vitality_max += part->vitality;
    }
    c->vitality = c->vitality_max;
}

static bool is_spawn_plan(const GameData *data, int plan)
{
    for (int p = 0; p < data->part_count; p++)
        if (data->parts[p].spawn_plan == plan) return true;
    return false;
}

static int random_part(Battle *battle, const GameData *data, const BodySlot *slot)
{
    int named = find_part(data, slot->id);
    if (named >= 0 && data->parts[named].internal == slot->internal) return named;
    int eligible[MAX_PARTS], count = 0;
    for (int p = 0; p < data->part_count; p++)
        if (!data->parts[p].deity_only && data->parts[p].internal == slot->internal) eligible[count++] = p;
    return eligible[rng_below(&battle->rng, count)];
}

static void add_monster(Battle *battle, const GameData *data, int x, int y)
{
    int plans[MAX_BODY_PLANS], count = 0;
    for (int p = 0; p < data->body_plan_count; p++)
        if (!is_spawn_plan(data, p)) plans[count++] = p;
    const BodyPlan *plan = &data->body_plans[plans[rng_below(&battle->rng, count)]];
    Body body = plan->body;
    for (int s = 0; s < body.slot_count; s++) body.slots[s].part = random_part(battle, data, &body.slots[s]);
    init_combatant(&battle->combatants[battle->combatant_count++], data, plan->id, SIDE_HORDE, &body, x, y);
}

static int free_tile_column(Battle *battle, int row)
{
    int column;
    do column = 6 + rng_below(&battle->rng, ARENA_WIDTH_TILES - 12);
    while (battle->obstacle[row][column]);
    return column;
}

static void scatter_objects(Battle *battle, FloorObject *objects, int *count, int defs)
{
    for (int d = 0; d < defs; d++) {
        int row = rng_below(&battle->rng, ARENA_HEIGHT_TILES);
        int column = free_tile_column(battle, row);
        objects[(*count)++] = (FloorObject){d, column * TILE_SIZE + TILE_SIZE / 2, row * TILE_SIZE + TILE_SIZE / 2, 0};
    }
}

static int row_y(int index)
{
    return ((index * 2 + 1) * ARENA_HEIGHT_TILES / (DEITIES_PER_PANTHEON * 2)) * TILE_SIZE;
}

void battle_setup(Battle *battle, const GameData *data, uint32_t seed)
{
    memset(battle, 0, sizeof *battle);
    battle->seed = seed;
    battle->rng = seed ? seed : 1;
    battle->pantheon = rng_below(&battle->rng, data->pantheon_count);
    for (int i = 0; i < 12; i++) {
        int row = rng_below(&battle->rng, ARENA_HEIGHT_TILES);
        battle->obstacle[row][6 + rng_below(&battle->rng, ARENA_WIDTH_TILES - 12)] = 1;
    }
    const Pantheon *pantheon = &data->pantheons[battle->pantheon];
    for (int d = 0; d < DEITIES_PER_PANTHEON; d++)
        init_combatant(&battle->combatants[battle->combatant_count++], data, pantheon->deities[d].name, SIDE_PANTHEON,
                       &pantheon->deities[d].body, 2 * TILE_SIZE, row_y(d));
    for (int m = 0; m < HORDE_SIZE; m++) add_monster(battle, data, (ARENA_WIDTH_TILES - 2) * TILE_SIZE, row_y(m));
    scatter_objects(battle, battle->pickups, &battle->pickup_count, data->pickup_count);
    scatter_objects(battle, battle->traps, &battle->trap_count, data->trap_count);
}

int battle_winner(const Battle *battle)
{
    bool alive[SIDE_COUNT] = {false};
    for (int c = 0; c < battle->combatant_count; c++)
        if (battle->combatants[c].alive) alive[battle->combatants[c].side] = true;
    if (alive[SIDE_PANTHEON] && !alive[SIDE_HORDE]) return SIDE_PANTHEON;
    if (alive[SIDE_HORDE] && !alive[SIDE_PANTHEON]) return SIDE_HORDE;
    return -1;
}

static int step_toward(int from, int to)
{
    return from < to ? 1 : from > to ? -1 : 0;
}

static bool blocked(const Battle *battle, int x, int y)
{
    int column = x / TILE_SIZE, row = y / TILE_SIZE;
    if (column < 0 || row < 0 || column >= ARENA_WIDTH_TILES || row >= ARENA_HEIGHT_TILES) return true;
    return battle->obstacle[row][column];
}

static const Combatant *nearest_enemy(const Battle *battle, const Combatant *self)
{
    const Combatant *best = NULL;
    int best_distance = 0;
    for (int c = 0; c < battle->combatant_count; c++) {
        const Combatant *other = &battle->combatants[c];
        if (!other->alive || other->side == self->side) continue;
        int distance = abs(other->x - self->x) + abs(other->y - self->y);
        if (!best || distance < best_distance) best = other, best_distance = distance;
    }
    return best;
}

static void advance_toward_enemy(const Battle *battle, Combatant *self)
{
    const Combatant *enemy = nearest_enemy(battle, self);
    if (!enemy) return;
    int nx = self->x + step_toward(self->x, enemy->x);
    int ny = self->y + step_toward(self->y, enemy->y);
    if (!blocked(battle, nx, self->y)) self->x = nx;
    if (!blocked(battle, self->x, ny)) self->y = ny;
}

void battle_step(Battle *battle, const GameData *data)
{
    (void)data;
    for (int c = 0; c < battle->combatant_count; c++)
        if (battle->combatants[c].alive) advance_toward_enemy(battle, &battle->combatants[c]);
    battle->tick++;
}
