#include "battle.h"

#include "combat.h"
#include "decide.h"
#include "floor.h"
#include "horde.h"
#include "movement.h"
#include "power.h"

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

static int row_y(int index, int row_count)
{
    return ((index * 2 + 1) * ARENA_HEIGHT_TILES / (row_count * 2)) * TILE_MILLIPX;
}

void battle_setup(Battle *battle, const GameData *data, uint32_t seed)
{
    battle_setup_pantheon(battle, data, seed, -1);
}

void battle_setup_pantheon(Battle *battle, const GameData *data, uint32_t seed, int forced_pantheon)
{
    battle_free(battle);
    memset(battle, 0, sizeof *battle);
    battle->seed = seed;
    battle->rng = scramble_seed(seed);
    battle->pantheon = rng_below(&battle->rng, data->pantheon_count);
    if (forced_pantheon >= 0) battle->pantheon = forced_pantheon;
    for (int i = 0; i < 12; i++) {
        int row = rng_below(&battle->rng, ARENA_HEIGHT_TILES);
        battle->obstacle[row][6 + rng_below(&battle->rng, ARENA_WIDTH_TILES - 12)] = 1;
    }
    const Pantheon *pantheon = &data->pantheons[battle->pantheon];
    for (int d = 0; d < DEITIES_PER_PANTHEON; d++)
        battle_add_combatant(battle, data, pantheon->deities[d].name, SIDE_PANTHEON, &pantheon->deities[d].body,
                             2 * TILE_MILLIPX, row_y(d, DEITIES_PER_PANTHEON));
    Horde horde = generate_horde(data, pantheon_power(data, battle->pantheon), &battle->rng);
    for (int m = 0; m < horde.count; m++)
        battle_add_combatant(battle, data, data->body_plans[horde.monsters[m].plan].id, SIDE_HORDE, &horde.monsters[m].body,
                             (ARENA_WIDTH_TILES - 2) * TILE_MILLIPX, row_y(m, horde.count));
    battle_place_floor_objects(battle, data);
}

int battle_winner(const Battle *battle)
{
    bool alive[SIDE_COUNT] = {false};
    for (int c = 0; c < battle->combatant_count; c++)
        if (battle->combatants[c].alive && battle->combatants[c].charm_side == battle->combatants[c].side)
            alive[battle->combatants[c].side] = true;
    if (alive[SIDE_PANTHEON] && !alive[SIDE_HORDE]) return SIDE_PANTHEON;
    if (alive[SIDE_HORDE] && !alive[SIDE_PANTHEON]) return SIDE_HORDE;
    return -1;
}

void battle_step(Battle *battle, const GameData *data, Decider *decider)
{
    if (decider) battle_decide(battle, data, decider);
    battle_act(battle, data);
    battle_tick_effects(battle, data);
    battle_move_combatants(battle, data);
    battle_tick_floor(battle, data);
    battle->tick++;
}
