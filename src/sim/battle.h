#ifndef SIM_BATTLE_H
#define SIM_BATTLE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "arena.h"
#include "data.h"

#define MAX_COMBATANTS 16
#define MAX_FLOOR_OBJECTS 16
#define MAX_REMAINS 16
#define MAX_EVENTS 1024
#define HORDE_SIZE 4

typedef enum { SIDE_PANTHEON, SIDE_HORDE, SIDE_COUNT } Side;

typedef enum {
    STANCE_NONE,
    STANCE_FIGHT,
    STANCE_RETREAT,
    STANCE_SUPPORT,
    STANCE_COLLECT,
    STANCE_COUNT
} Stance;

typedef enum {
    EVENT_DECISION,
    EVENT_HIT,
    EVENT_PICKUP,
    EVENT_DEATH,
    EVENT_KIND_COUNT
} EventKind;

typedef struct {
    OrderKind kind;
    int target;
    int x;
    int y;
} Order;

typedef struct {
    int integrity;
    int cooldown;
    Order order;
} PartState;

typedef struct {
    Stance stance;
    int hurt_level;
    int vulnerable_element;
} Decision;

typedef struct {
    char name[NAME_LEN];
    Side side;
    bool alive;
    int x;
    int y;
    Body body;
    PartState parts[MAX_SLOTS];
    int vitality;
    int vitality_max;
    int slow_ticks;
    int dot_ticks;
    int charm_ticks;
    Side charm_side;
    Decision decision;
} Combatant;

typedef struct {
    int def;
    int x;
    int y;
    int respawn_in;
} FloorObject;

typedef struct {
    int x;
    int y;
    int part_count;
    int parts[MAX_SLOTS];
} Remains;

typedef struct {
    int tick;
    EventKind kind;
    int a;
    int b;
    int value;
} Event;

typedef struct {
    uint32_t seed;
    uint32_t rng;
    int tick;
    int pantheon;
    unsigned char obstacle[ARENA_HEIGHT_TILES][ARENA_WIDTH_TILES];
    Combatant combatants[MAX_COMBATANTS];
    int combatant_count;
    FloorObject pickups[MAX_FLOOR_OBJECTS];
    int pickup_count;
    FloorObject traps[MAX_FLOOR_OBJECTS];
    int trap_count;
    Remains remains[MAX_REMAINS];
    int remains_count;
    Event log[MAX_EVENTS];
    int log_count;
} Battle;

uint32_t rng_next(uint32_t *state);
int rng_below(uint32_t *state, int bound);

void battle_setup(Battle *battle, const GameData *data, uint32_t seed);
void battle_step(Battle *battle, const GameData *data);
void battle_log_event(Battle *battle, EventKind kind, int a, int b, int value);
int battle_winner(const Battle *battle);

void body_working_parts(const Body *body, const PartState *parts, bool working[MAX_SLOTS]);

char *battle_save_json(const Battle *battle, const GameData *data);
bool battle_load_json(Battle *battle, const GameData *data, const char *json, char *err, size_t err_size);

#endif
