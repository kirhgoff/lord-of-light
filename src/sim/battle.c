#include "battle.h"

#include "movement.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

uint32_t scramble_seed(uint32_t seed)
{
    uint32_t z = seed + 0x9e3779b9u;
    z = (z ^ (z >> 16)) * 0x85ebca6bu;
    z = (z ^ (z >> 13)) * 0xc2b2ae35u;
    z ^= z >> 16;
    return z ? z : 1;
}

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

void battle_free(Battle *battle)
{
    free(battle->log);
    battle->log = NULL;
    battle->log_count = battle->log_capacity = 0;
}

bool battle_log_event(Battle *battle, Event event)
{
    if (battle->log_count == battle->log_capacity) {
        int capacity = battle->log_capacity ? battle->log_capacity * 2 : 256;
        Event *grown = realloc(battle->log, sizeof(Event) * (size_t)capacity);
        if (!grown) return false;
        battle->log = grown;
        battle->log_capacity = capacity;
    }
    event.tick = battle->tick;
    battle->log[battle->log_count++] = event;
    return true;
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

static void init_combatant(Combatant *c, const GameData *data, const char *name, Side side, const Body *body, int x_millipx, int y_millipx)
{
    memset(c, 0, sizeof *c);
    snprintf(c->name, sizeof c->name, "%s", name);
    c->side = side;
    c->alive = true;
    c->x_millipx = x_millipx;
    c->y_millipx = y_millipx;
    c->body = *body;
    c->charm_side = side;
    c->dot_element = -1;
    c->dot_source = -1;
    c->spawner = -1;
    c->decision.vulnerable_element = -1;
    for (int s = 0; s < body->slot_count; s++) {
        const Part *part = &data->parts[body->slots[s].part];
        c->parts[s].integrity = part->integrity;
        c->parts[s].order = (Order){ORDER_IDLE, -1, 0, 0};
        c->vitality_max += part->vitality;
    }
    c->vitality = c->vitality_max;
}

int battle_add_combatant(Battle *battle, const GameData *data, const char *name, Side side, const Body *body, int x_millipx, int y_millipx)
{
    int index = battle->combatant_count++;
    init_combatant(&battle->combatants[index], data, name, side, body, x_millipx, y_millipx);
    return index;
}

static int random_part(Battle *battle, const GameData *data, const BodySlot *slot)
{
    if (slot->part >= 0) return slot->part;
    int eligible[MAX_PARTS], count = 0;
    for (int p = 0; p < data->part_count; p++)
        if (fills_horde_slot(data, p, slot)) eligible[count++] = p;
    return eligible[rng_below(&battle->rng, count)];
}

static void add_monster(Battle *battle, const GameData *data, int x_millipx, int y_millipx)
{
    int plans[MAX_BODY_PLANS], count = 0;
    for (int p = 0; p < data->body_plan_count; p++)
        if (!is_spawn_plan(data, p)) plans[count++] = p;
    const BodyPlan *plan = &data->body_plans[plans[rng_below(&battle->rng, count)]];
    Body body = plan->body;
    for (int s = 0; s < body.slot_count; s++) body.slots[s].part = random_part(battle, data, &body.slots[s]);
    battle_add_combatant(battle, data, plan->id, SIDE_HORDE, &body, x_millipx, y_millipx);
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
        objects[(*count)++] = (FloorObject){d, column * TILE_MILLIPX + TILE_MILLIPX / 2, row * TILE_MILLIPX + TILE_MILLIPX / 2, 0};
    }
}

static int row_y(int index)
{
    return ((index * 2 + 1) * ARENA_HEIGHT_TILES / (DEITIES_PER_PANTHEON * 2)) * TILE_MILLIPX;
}

void battle_setup(Battle *battle, const GameData *data, uint32_t seed)
{
    battle_free(battle);
    memset(battle, 0, sizeof *battle);
    battle->seed = seed;
    battle->rng = scramble_seed(seed);
    battle->pantheon = rng_below(&battle->rng, data->pantheon_count);
    for (int i = 0; i < 12; i++) {
        int row = rng_below(&battle->rng, ARENA_HEIGHT_TILES);
        battle->obstacle[row][6 + rng_below(&battle->rng, ARENA_WIDTH_TILES - 12)] = 1;
    }
    const Pantheon *pantheon = &data->pantheons[battle->pantheon];
    for (int d = 0; d < DEITIES_PER_PANTHEON; d++)
        battle_add_combatant(battle, data, pantheon->deities[d].name, SIDE_PANTHEON, &pantheon->deities[d].body,
                             2 * TILE_MILLIPX, row_y(d));
    for (int m = 0; m < HORDE_SIZE; m++) add_monster(battle, data, (ARENA_WIDTH_TILES - 2) * TILE_MILLIPX, row_y(m));
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

void battle_step(Battle *battle, const GameData *data)
{
    battle_move_combatants(battle, data);
    battle->tick++;
}
