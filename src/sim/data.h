#ifndef SIM_DATA_H
#define SIM_DATA_H

#include <stdbool.h>
#include <stddef.h>

#define ID_LEN 32
#define NAME_LEN 64
#define MAX_PARTS 128
#define MAX_BODY_PLANS 16
#define MAX_SLOTS 24
#define MAX_EDGES 24
#define MAX_ABILITIES 4
#define MAX_PANTHEONS 8
#define MAX_ARENA_OBJECTS 16
#define DEITIES_PER_PANTHEON 4

typedef enum {
    ELEMENT_KINETIC,
    ELEMENT_FIRE,
    ELEMENT_FROST,
    ELEMENT_LIGHTNING,
    ELEMENT_RADIANCE,
    ELEMENT_VENOM,
    ELEMENT_MIND,
    ELEMENT_VOID,
    ELEMENT_COUNT
} Element;

typedef enum {
    ORDER_STRIKE,
    ORDER_CAST,
    ORDER_BLOCK,
    ORDER_MOVE_TO,
    ORDER_PICK_UP,
    ORDER_SUPPORT,
    ORDER_SPAWN,
    ORDER_IDLE,
    ORDER_KIND_COUNT
} OrderKind;

typedef struct {
    int dot_damage;
    int dot_ticks;
    int slow_percent;
    int slow_ticks;
    int chain_targets;
    int chain_range;
    int pierce_targets;
    bool internal_first;
    int charm_ticks;
    int buff_strip_count;
} ElementDef;

typedef enum {
    SLOT_ROLE_ANY,
    SLOT_ROLE_HEART,
    SLOT_ROLE_MOVER,
    SLOT_ROLE_ATTACK,
    SLOT_ROLE_SENSE,
    SLOT_ROLE_COUNT
} SlotRole;

typedef struct {
    OrderKind order;
    Element element;
    int damage;
    int heal;
    int range;
    int cooldown_ticks;
    int area;
} Ability;

typedef struct {
    char id[ID_LEN];
    char name[NAME_LEN];
    bool internal;
    bool deity_only;
    bool minion_only;
    int integrity;
    Ability abilities[MAX_ABILITIES];
    int ability_count;
    int resistance_percent[ELEMENT_COUNT];
    int vitality;
    int initiative;
    int vision_close;
    int vision_medium;
    int vision_far;
    int move_speed_px_per_second;
    int spawn_plan;
    int spawn_cap;
    char sprite[ID_LEN];
} Part;

typedef struct {
    char id[ID_LEN];
    bool internal;
    SlotRole role;
    char fixed_part_id[ID_LEN];
    int anchor_x;
    int anchor_y;
    int part;
} BodySlot;

typedef struct {
    int a;
    int b;
} BodyEdge;

typedef struct {
    BodySlot slots[MAX_SLOTS];
    int slot_count;
    BodyEdge edges[MAX_EDGES];
    int edge_count;
    int core;
} Body;

typedef struct {
    char id[ID_LEN];
    Body body;
} BodyPlan;

typedef struct {
    char name[NAME_LEN];
    char epithet[NAME_LEN * 2];
    Body body;
} Deity;

typedef struct {
    char name[NAME_LEN];
    Deity deities[DEITIES_PER_PANTHEON];
} Pantheon;

typedef struct {
    char id[ID_LEN];
    char name[NAME_LEN];
    Element element;
    int damage;
    int heal;
    int radius;
    int slow_percent;
    int effect_ticks;
    int damage_bonus_percent;
    int respawn_ticks;
    char sprite[ID_LEN];
} ArenaObjectDef;

typedef struct {
    int integrity;
    int vitality;
    int damage;
    int range;
    int resistance;
    int initiative;
    int vision;
    int move_speed;
    int spawning;
} PowerWeights;

typedef struct {
    ElementDef elements[ELEMENT_COUNT];
    Part parts[MAX_PARTS];
    int part_count;
    BodyPlan body_plans[MAX_BODY_PLANS];
    int body_plan_count;
    Pantheon pantheons[MAX_PANTHEONS];
    int pantheon_count;
    ArenaObjectDef pickups[MAX_ARENA_OBJECTS];
    int pickup_count;
    ArenaObjectDef traps[MAX_ARENA_OBJECTS];
    int trap_count;
    PowerWeights power;
} GameData;

typedef struct cJSON cJSON;

bool json_to_int(const cJSON *item, int *out);
const char *element_id(Element element);
const char *order_kind_id(OrderKind kind);
int find_part(const GameData *data, const char *id);
int find_body_plan(const GameData *data, const char *id);
int find_body_slot(const Body *body, const char *id);
bool part_fits_role(const Part *part, SlotRole role);

bool load_elements(GameData *data, const char *path, char *err, size_t err_size);
bool load_body_plans(GameData *data, const char *path, char *err, size_t err_size);
bool load_parts(GameData *data, const char *path, char *err, size_t err_size);
bool load_arena_objects(GameData *data, const char *path, char *err, size_t err_size);
bool load_power(GameData *data, const char *path, char *err, size_t err_size);
bool load_pantheons(GameData *data, const char *folder, char *err, size_t err_size);
bool load_pantheon(GameData *data, const char *path, char *err, size_t err_size);
bool load_game_data(GameData *data, const char *dir, char *err, size_t err_size);

#endif
