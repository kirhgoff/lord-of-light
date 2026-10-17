#ifndef SIM_BATTLE_H
#define SIM_BATTLE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "arena.h"
#include "data.h"

#define MAX_COMBATANTS 64
#define MAX_REMAINS MAX_COMBATANTS
#define EVENT_AMOUNT_LIMIT 1000000000

typedef enum { SIDE_PANTHEON,
               SIDE_HORDE,
               SIDE_COUNT } Side;

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
    EVENT_EFFECT,
    EVENT_SPAWN,
    EVENT_TRAP,
    EVENT_KIND_COUNT
} EventKind;

typedef enum { EFFECT_DOT,
               EFFECT_SLOW,
               EFFECT_CHARM,
               EFFECT_STRIP,
               EFFECT_HEAL } EffectKind;

typedef struct {
    OrderKind kind;
    int target;
    int x_millipx;
    int y_millipx;
} Order;

typedef struct {
    int integrity;
    int cooldown;
    Order order;
} PartState;

typedef enum { HURT_UNHURT,
               HURT_WOUNDED,
               HURT_BADLY_WOUNDED,
               HURT_NEAR_DEATH,
               HURT_COUNT } HurtLevel;

typedef enum { DECIDER_NONE,
               DECIDER_REFLEX,
               DECIDER_LAYA,
               DECIDER_JEV,
               DECIDER_COUNT } DeciderKind;

typedef struct {
    Stance stance;
    HurtLevel hurt_level;
    int vulnerable_element;
} Decision;

typedef struct {
    char name[NAME_LEN];
    Side side;
    bool alive;
    int x_millipx;
    int y_millipx;
    Body body;
    PartState parts[MAX_SLOTS];
    int vitality;
    int vitality_max;
    int slow_ticks;
    int dot_ticks;
    int charm_ticks;
    Side charm_side;
    int dot_damage;
    int dot_element;
    int dot_source;
    int slow_percent;
    int spawner;
    int damage_bonus_percent;
    int damage_bonus_ticks;
    int next_decision_tick;
    int next_waiting_tick;
    int haste_percent;
    int haste_ticks;
    int vision_percent;
    int vision_ticks;
    int ward_percent;
    int ward_ticks;
    Decision decision;
} Combatant;

typedef struct {
    int def;
    int x_millipx;
    int y_millipx;
    int respawn_in;
} FloorObject;

typedef struct {
    int x_millipx;
    int y_millipx;
    int part_count;
    int parts[MAX_SLOTS];
} Remains;

typedef enum {
    FALLBACK_NONE,
    FALLBACK_WAITING,
    FALLBACK_UNREACHABLE,
    FALLBACK_TIMEOUT,
    FALLBACK_MALFORMED,
    FALLBACK_LOW_CONFIDENCE,
    FALLBACK_COUNT
} Fallback;

typedef struct {
    int tick;
    EventKind kind;
    int actor;
    int actor_part;
    int target;
    int target_part;
    int element;
    int amount;
    int detail;
    DeciderKind decider;
    int confidence_permille;
    int answer_ms;
    Fallback fallback;
    int stance_permille[STANCE_COUNT];
} Event;

typedef struct Decider Decider;

typedef struct {
    uint32_t seed;
    uint32_t rng;
    int tick;
    int pantheon;
    unsigned char obstacle[ARENA_HEIGHT_TILES][ARENA_WIDTH_TILES];
    Combatant combatants[MAX_COMBATANTS];
    int combatant_count;
    FloorObject pickups[MAX_ARENA_OBJECTS];
    int pickup_count;
    FloorObject traps[MAX_ARENA_OBJECTS];
    int trap_count;
    Remains remains[MAX_REMAINS];
    int remains_count;
    Event *log;
    int log_count;
    int log_capacity;
} Battle;

uint32_t scramble_seed(uint32_t seed);
uint32_t rng_next(uint32_t *state);
int rng_below(uint32_t *state, int bound);

/* A Battle must be zero-initialised (Battle b = {0};) before its first setup or load. */
void battle_setup(Battle *battle, const GameData *data, uint32_t seed);
void battle_setup_pantheon(Battle *battle, const GameData *data, uint32_t seed, int forced_pantheon);
int battle_add_combatant(Battle *battle, const GameData *data, const char *name, Side side, const Body *body, int x_millipx, int y_millipx);
void battle_step(Battle *battle, const GameData *data, Decider *decider);
void battle_free(Battle *battle);
bool battle_log_event(Battle *battle, Event event);
int battle_winner(const Battle *battle);

void body_working_parts(const Body *body, const PartState *parts, bool working[MAX_SLOTS]);

const char *stance_id(Stance stance);
const char *fallback_id(Fallback fallback);
const char *decider_name(DeciderKind decider);

char *battle_save_json(const Battle *battle, const GameData *data);
bool battle_load_json(Battle *battle, const GameData *data, const char *json, char *err, size_t err_size);

#endif
