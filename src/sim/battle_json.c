#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "battle.h"
#include "cJSON.h"

#define MAX_TICK 1000000000

static const char *const SIDE_IDS[SIDE_COUNT] = {"pantheon", "horde"};
static const char *const STANCE_IDS[STANCE_COUNT] = {"none", "fight", "retreat", "support", "collect"};
static const char *const EVENT_IDS[EVENT_KIND_COUNT] = {"decision", "hit", "pickup", "death", "effect", "spawn", "trap"};

static const char *object_id(const ArenaObjectDef *defs, int index)
{
    return defs[index].id;
}

static int find_object(const ArenaObjectDef *defs, int count, const char *id)
{
    for (int i = 0; i < count; i++)
        if (strcmp(defs[i].id, id) == 0) return i;
    return -1;
}

static cJSON *int_array(const int *values, int count)
{
    return cJSON_CreateIntArray(values, count);
}

static cJSON *save_order(const Order *order)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "kind", order_kind_id(order->kind));
    cJSON_AddNumberToObject(o, "target", order->target);
    cJSON_AddNumberToObject(o, "x_millipx", order->x_millipx);
    cJSON_AddNumberToObject(o, "y_millipx", order->y_millipx);
    return o;
}

static cJSON *save_body(const Body *body, const GameData *data)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddNumberToObject(o, "core", body->core);
    cJSON *slots = cJSON_AddArrayToObject(o, "slots");
    for (int s = 0; s < body->slot_count; s++) {
        cJSON *slot = cJSON_CreateObject();
        cJSON_AddStringToObject(slot, "id", body->slots[s].id);
        cJSON_AddBoolToObject(slot, "internal", body->slots[s].internal);
        cJSON_AddNumberToObject(slot, "x", body->slots[s].anchor_x);
        cJSON_AddNumberToObject(slot, "y", body->slots[s].anchor_y);
        cJSON_AddStringToObject(slot, "part", data->parts[body->slots[s].part].id);
        cJSON_AddItemToArray(slots, slot);
    }
    cJSON *edges = cJSON_AddArrayToObject(o, "edges");
    for (int e = 0; e < body->edge_count; e++) {
        int pair[2] = {body->edges[e].a, body->edges[e].b};
        cJSON_AddItemToArray(edges, int_array(pair, 2));
    }
    return o;
}

static cJSON *save_combatant(const Combatant *c, const GameData *data)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "name", c->name);
    cJSON_AddStringToObject(o, "side", SIDE_IDS[c->side]);
    cJSON_AddBoolToObject(o, "alive", c->alive);
    cJSON_AddNumberToObject(o, "x_millipx", c->x_millipx);
    cJSON_AddNumberToObject(o, "y_millipx", c->y_millipx);
    cJSON_AddItemToObject(o, "body", save_body(&c->body, data));
    cJSON *parts = cJSON_AddArrayToObject(o, "parts");
    for (int s = 0; s < c->body.slot_count; s++) {
        cJSON *part = cJSON_CreateObject();
        cJSON_AddNumberToObject(part, "integrity", c->parts[s].integrity);
        cJSON_AddNumberToObject(part, "cooldown", c->parts[s].cooldown);
        cJSON_AddItemToObject(part, "order", save_order(&c->parts[s].order));
        cJSON_AddItemToArray(parts, part);
    }
    cJSON_AddNumberToObject(o, "vitality", c->vitality);
    cJSON_AddNumberToObject(o, "vitality_max", c->vitality_max);
    cJSON_AddNumberToObject(o, "slow_ticks", c->slow_ticks);
    cJSON_AddNumberToObject(o, "dot_ticks", c->dot_ticks);
    cJSON_AddNumberToObject(o, "charm_ticks", c->charm_ticks);
    cJSON_AddStringToObject(o, "charm_side", SIDE_IDS[c->charm_side]);
    cJSON_AddNumberToObject(o, "dot_damage", c->dot_damage);
    cJSON_AddNumberToObject(o, "dot_element", c->dot_element);
    cJSON_AddNumberToObject(o, "dot_source", c->dot_source);
    cJSON_AddNumberToObject(o, "slow_percent", c->slow_percent);
    cJSON_AddNumberToObject(o, "spawner", c->spawner);
    cJSON_AddNumberToObject(o, "damage_bonus_percent", c->damage_bonus_percent);
    cJSON_AddNumberToObject(o, "damage_bonus_ticks", c->damage_bonus_ticks);
    cJSON_AddNumberToObject(o, "haste_percent", c->haste_percent);
    cJSON_AddNumberToObject(o, "haste_ticks", c->haste_ticks);
    cJSON_AddNumberToObject(o, "sight_percent", c->sight_percent);
    cJSON_AddNumberToObject(o, "sight_ticks", c->sight_ticks);
    cJSON_AddNumberToObject(o, "ward_percent", c->ward_percent);
    cJSON_AddNumberToObject(o, "ward_ticks", c->ward_ticks);
    cJSON *decision = cJSON_AddObjectToObject(o, "decision");
    cJSON_AddStringToObject(decision, "stance", STANCE_IDS[c->decision.stance]);
    cJSON_AddNumberToObject(decision, "hurt_level", c->decision.hurt_level);
    cJSON_AddNumberToObject(decision, "vulnerable_element", c->decision.vulnerable_element);
    return o;
}

static cJSON *save_floor(const FloorObject *objects, int count, const ArenaObjectDef *defs)
{
    cJSON *list = cJSON_CreateArray();
    for (int i = 0; i < count; i++) {
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "def", object_id(defs, objects[i].def));
        cJSON_AddNumberToObject(o, "x_millipx", objects[i].x_millipx);
        cJSON_AddNumberToObject(o, "y_millipx", objects[i].y_millipx);
        cJSON_AddNumberToObject(o, "respawn_in", objects[i].respawn_in);
        cJSON_AddItemToArray(list, o);
    }
    return list;
}

static cJSON *save_arena(const Battle *battle)
{
    cJSON *rows = cJSON_CreateArray();
    for (int r = 0; r < ARENA_HEIGHT_TILES; r++) {
        char row[ARENA_WIDTH_TILES + 1];
        for (int c = 0; c < ARENA_WIDTH_TILES; c++) row[c] = battle->obstacle[r][c] ? '#' : '.';
        row[ARENA_WIDTH_TILES] = '\0';
        cJSON_AddItemToArray(rows, cJSON_CreateString(row));
    }
    return rows;
}

char *battle_save_json(const Battle *battle, const GameData *data)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "seed", battle->seed);
    cJSON_AddNumberToObject(root, "rng", battle->rng);
    cJSON_AddNumberToObject(root, "tick", battle->tick);
    cJSON_AddStringToObject(root, "pantheon", data->pantheons[battle->pantheon].name);
    cJSON_AddItemToObject(root, "arena", save_arena(battle));
    cJSON *combatants = cJSON_AddArrayToObject(root, "combatants");
    for (int c = 0; c < battle->combatant_count; c++)
        cJSON_AddItemToArray(combatants, save_combatant(&battle->combatants[c], data));
    cJSON_AddItemToObject(root, "pickups", save_floor(battle->pickups, battle->pickup_count, data->pickups));
    cJSON_AddItemToObject(root, "traps", save_floor(battle->traps, battle->trap_count, data->traps));
    cJSON *remains = cJSON_AddArrayToObject(root, "remains");
    for (int r = 0; r < battle->remains_count; r++) {
        const Remains *rem = &battle->remains[r];
        cJSON *o = cJSON_CreateObject();
        cJSON_AddNumberToObject(o, "x_millipx", rem->x_millipx);
        cJSON_AddNumberToObject(o, "y_millipx", rem->y_millipx);
        cJSON *part_ids = cJSON_AddArrayToObject(o, "parts");
        for (int p = 0; p < rem->part_count; p++)
            cJSON_AddItemToArray(part_ids, cJSON_CreateString(data->parts[rem->parts[p]].id));
        cJSON_AddItemToArray(remains, o);
    }
    cJSON *log = cJSON_AddArrayToObject(root, "log");
    for (int e = 0; e < battle->log_count; e++) {
        const Event *ev = &battle->log[e];
        cJSON *o = cJSON_CreateObject();
        cJSON_AddNumberToObject(o, "tick", ev->tick);
        cJSON_AddStringToObject(o, "kind", EVENT_IDS[ev->kind]);
        cJSON_AddNumberToObject(o, "actor", ev->actor);
        cJSON_AddNumberToObject(o, "actor_part", ev->actor_part);
        cJSON_AddNumberToObject(o, "target", ev->target);
        cJSON_AddNumberToObject(o, "target_part", ev->target_part);
        cJSON_AddNumberToObject(o, "element", ev->element);
        cJSON_AddNumberToObject(o, "amount", ev->amount);
        cJSON_AddNumberToObject(o, "detail", ev->detail);
        cJSON_AddItemToArray(log, o);
    }
    char *text = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return text;
}

typedef struct {
    char *err;
    size_t err_size;
    const GameData *data;
    int combatant_count;
} Context;

static bool fail(Context *ctx, const char *what)
{
    snprintf(ctx->err, ctx->err_size, "%s", what);
    return false;
}

static bool fail_key(Context *ctx, const char *key, const char *what)
{
    snprintf(ctx->err, ctx->err_size, "'%s': %s", key, what);
    return false;
}

static bool read_int(Context *ctx, const cJSON *obj, const char *key, int min, int max, int *out)
{
    int value;
    if (!json_to_int(cJSON_GetObjectItemCaseSensitive(obj, key), &value) || value < min || value > max) {
        char what[64];
        snprintf(what, sizeof what, "missing or not an integer in [%d, %d]", min, max);
        return fail_key(ctx, key, what);
    }
    *out = value;
    return true;
}

static bool read_u32(Context *ctx, const cJSON *obj, const char *key, uint32_t *out)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    bool valid = cJSON_IsNumber(item) && item->valuedouble >= 0 && item->valuedouble <= 4294967295.0 &&
                 item->valuedouble == floor(item->valuedouble);
    if (!valid) return fail_key(ctx, key, "missing or not a 32-bit unsigned integer");
    *out = (uint32_t)item->valuedouble;
    return true;
}

static bool read_bool(Context *ctx, const cJSON *obj, const char *key, bool *out)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (!cJSON_IsBool(item)) return fail_key(ctx, key, "missing or not a boolean");
    *out = cJSON_IsTrue(item);
    return true;
}

static bool read_id(Context *ctx, const cJSON *obj, const char *key, const char *const *ids, int count, int *out)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (!cJSON_IsString(item)) return fail_key(ctx, key, "missing or not a string");
    for (int i = 0; i < count; i++)
        if (strcmp(ids[i], item->valuestring) == 0) return *out = i, true;
    return fail_key(ctx, key, "unknown id");
}

static bool read_text(Context *ctx, const cJSON *obj, const char *key, char *out, size_t size)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (!cJSON_IsString(item) || strlen(item->valuestring) >= size) return fail_key(ctx, key, "missing or too long string");
    strcpy(out, item->valuestring);
    return true;
}

static const cJSON *read_list(Context *ctx, const cJSON *obj, const char *key, int max, int *count)
{
    const cJSON *list = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (!cJSON_IsArray(list) || cJSON_GetArraySize(list) > max) {
        fail_key(ctx, key, "missing array or too many entries");
        return NULL;
    }
    *count = cJSON_GetArraySize(list);
    return list;
}

static bool read_object(Context *ctx, const cJSON *obj, const char *key, const cJSON **out)
{
    *out = cJSON_GetObjectItemCaseSensitive(obj, key);
    return cJSON_IsObject(*out) || fail_key(ctx, key, "missing or not an object");
}

static bool read_slot_ref(Context *ctx, const cJSON *obj, const char *key, int *out)
{
    return read_int(ctx, obj, key, -1, MAX_SLOTS - 1, out);
}

static bool read_combatant_ref(Context *ctx, const cJSON *obj, const char *key, int *out)
{
    return read_int(ctx, obj, key, -1, ctx->combatant_count - 1, out);
}

static bool read_position(Context *ctx, const cJSON *obj, int *x, int *y)
{
    return read_int(ctx, obj, "x_millipx", 0, ARENA_WIDTH_MILLIPX, x) &&
           read_int(ctx, obj, "y_millipx", 0, ARENA_HEIGHT_MILLIPX, y);
}

static bool load_order(Context *ctx, const cJSON *obj, Order *order)
{
    int kind;
    if (!cJSON_IsObject(obj)) return fail(ctx, "order is not an object");
    const cJSON *name = cJSON_GetObjectItemCaseSensitive(obj, "kind");
    if (!cJSON_IsString(name)) return fail(ctx, "order kind missing");
    for (kind = 0; kind < ORDER_KIND_COUNT; kind++)
        if (strcmp(order_kind_id((OrderKind)kind), name->valuestring) == 0) break;
    if (kind == ORDER_KIND_COUNT) return fail(ctx, "unknown order kind");
    order->kind = (OrderKind)kind;
    return read_combatant_ref(ctx, obj, "target", &order->target) &&
           read_position(ctx, obj, &order->x_millipx, &order->y_millipx);
}

static bool load_edge(Context *ctx, const cJSON *pair, int slot_count, BodyEdge *edge)
{
    if (!cJSON_IsArray(pair) || cJSON_GetArraySize(pair) != 2) return fail(ctx, "edge is not a pair");
    int ends[2];
    for (int end = 0; end < 2; end++)
        if (!json_to_int(cJSON_GetArrayItem(pair, end), &ends[end]) || ends[end] < 0 || ends[end] >= slot_count)
            return fail(ctx, "edge index out of range");
    *edge = (BodyEdge){ends[0], ends[1]};
    return true;
}

static bool load_body(Context *ctx, const cJSON *obj, Body *body)
{
    memset(body, 0, sizeof *body);
    const cJSON *slots = read_list(ctx, obj, "slots", MAX_SLOTS, &body->slot_count);
    if (!slots || body->slot_count == 0) return fail(ctx, "body needs 1 to MAX_SLOTS slots");
    for (int s = 0; s < body->slot_count; s++) {
        const cJSON *slot = cJSON_GetArrayItem(slots, s);
        char part_id[ID_LEN];
        if (!cJSON_IsObject(slot)) return fail(ctx, "slot is not an object");
        if (!read_text(ctx, slot, "id", body->slots[s].id, ID_LEN) ||
            !read_bool(ctx, slot, "internal", &body->slots[s].internal) ||
            !read_int(ctx, slot, "x", -1000, 1000, &body->slots[s].anchor_x) ||
            !read_int(ctx, slot, "y", -1000, 1000, &body->slots[s].anchor_y) ||
            !read_text(ctx, slot, "part", part_id, ID_LEN))
            return false;
        body->slots[s].part = find_part(ctx->data, part_id);
        if (body->slots[s].part < 0) return fail(ctx, "unknown part id");
    }
    const cJSON *edges = read_list(ctx, obj, "edges", MAX_EDGES, &body->edge_count);
    if (!edges || !read_int(ctx, obj, "core", 0, body->slot_count - 1, &body->core)) return false;
    for (int e = 0; e < body->edge_count; e++)
        if (!load_edge(ctx, cJSON_GetArrayItem(edges, e), body->slot_count, &body->edges[e])) return false;
    return true;
}

static bool load_part_state(Context *ctx, const cJSON *part, PartState *state)
{
    const cJSON *order;
    return cJSON_IsObject(part) && read_int(ctx, part, "integrity", 0, 1000000, &state->integrity) &&
           read_int(ctx, part, "cooldown", 0, 1000000, &state->cooldown) && read_object(ctx, part, "order", &order) &&
           load_order(ctx, order, &state->order);
}

static bool load_decision(Context *ctx, const cJSON *obj, Decision *decision)
{
    const cJSON *source;
    int stance;
    if (!read_object(ctx, obj, "decision", &source) || !read_id(ctx, source, "stance", STANCE_IDS, STANCE_COUNT, &stance) ||
        !read_int(ctx, source, "hurt_level", 0, 100, &decision->hurt_level) ||
        !read_int(ctx, source, "vulnerable_element", -1, ELEMENT_COUNT - 1, &decision->vulnerable_element))
        return false;
    decision->stance = (Stance)stance;
    return true;
}

static bool load_combatant(Context *ctx, const cJSON *obj, Combatant *c)
{
    int side, charm_side, part_count;
    const cJSON *body;
    memset(c, 0, sizeof *c);
    if (!cJSON_IsObject(obj)) return fail(ctx, "combatant is not an object");
    if (!read_text(ctx, obj, "name", c->name, NAME_LEN) || !read_id(ctx, obj, "side", SIDE_IDS, SIDE_COUNT, &side) ||
        !read_bool(ctx, obj, "alive", &c->alive) || !read_position(ctx, obj, &c->x_millipx, &c->y_millipx) ||
        !read_object(ctx, obj, "body", &body) || !load_body(ctx, body, &c->body))
        return false;
    const cJSON *parts = read_list(ctx, obj, "parts", MAX_SLOTS, &part_count);
    if (!parts || part_count != c->body.slot_count) return fail(ctx, "parts must match body slots");
    for (int s = 0; s < part_count; s++)
        if (!load_part_state(ctx, cJSON_GetArrayItem(parts, s), &c->parts[s])) return false;
    if (!load_decision(ctx, obj, &c->decision) || !read_id(ctx, obj, "charm_side", SIDE_IDS, SIDE_COUNT, &charm_side))
        return false;
    c->side = (Side)side;
    c->charm_side = (Side)charm_side;
    return read_int(ctx, obj, "vitality", 0, 1000000, &c->vitality) &&
           read_int(ctx, obj, "vitality_max", 0, 1000000, &c->vitality_max) &&
           read_int(ctx, obj, "slow_ticks", 0, 1000000, &c->slow_ticks) &&
           read_int(ctx, obj, "dot_ticks", 0, 1000000, &c->dot_ticks) &&
           read_int(ctx, obj, "charm_ticks", 0, 1000000, &c->charm_ticks) &&
           read_int(ctx, obj, "dot_damage", 0, 1000000, &c->dot_damage) &&
           read_int(ctx, obj, "dot_element", -1, ELEMENT_COUNT - 1, &c->dot_element) &&
           read_combatant_ref(ctx, obj, "dot_source", &c->dot_source) &&
           read_int(ctx, obj, "slow_percent", 0, 100, &c->slow_percent) &&
           read_combatant_ref(ctx, obj, "spawner", &c->spawner) &&
           read_int(ctx, obj, "damage_bonus_percent", 0, 1000, &c->damage_bonus_percent) &&
           read_int(ctx, obj, "damage_bonus_ticks", 0, 1000000, &c->damage_bonus_ticks) &&
           read_int(ctx, obj, "haste_percent", 0, 1000, &c->haste_percent) &&
           read_int(ctx, obj, "haste_ticks", 0, 1000000, &c->haste_ticks) &&
           read_int(ctx, obj, "sight_percent", 0, 1000, &c->sight_percent) &&
           read_int(ctx, obj, "sight_ticks", 0, 1000000, &c->sight_ticks) &&
           read_int(ctx, obj, "ward_percent", 0, 1000, &c->ward_percent) &&
           read_int(ctx, obj, "ward_ticks", 0, 1000000, &c->ward_ticks);
}

static bool read_object_def(Context *ctx, const cJSON *obj, const ArenaObjectDef *defs, int def_count, int *out)
{
    char id[ID_LEN];
    if (!read_text(ctx, obj, "def", id, ID_LEN)) return false;
    *out = find_object(defs, def_count, id);
    return *out >= 0 || fail_key(ctx, "def", "unknown pickup or trap id");
}

static bool load_floor(Context *ctx, const cJSON *root, const char *key, const ArenaObjectDef *defs, int def_count,
                       FloorObject *objects, int *count)
{
    const cJSON *list = read_list(ctx, root, key, MAX_ARENA_OBJECTS, count);
    if (!list) return false;
    for (int i = 0; i < *count; i++) {
        const cJSON *o = cJSON_GetArrayItem(list, i);
        if (!cJSON_IsObject(o)) return fail_key(ctx, key, "entry is not an object");
        if (!read_object_def(ctx, o, defs, def_count, &objects[i].def) ||
            !read_position(ctx, o, &objects[i].x_millipx, &objects[i].y_millipx) ||
            !read_int(ctx, o, "respawn_in", 0, 1000000, &objects[i].respawn_in))
            return false;
    }
    return true;
}

static bool load_arena(Context *ctx, const cJSON *root, Battle *battle)
{
    const cJSON *rows = cJSON_GetObjectItemCaseSensitive(root, "arena");
    if (!cJSON_IsArray(rows) || cJSON_GetArraySize(rows) != ARENA_HEIGHT_TILES) return fail(ctx, "arena needs one string per row");
    for (int r = 0; r < ARENA_HEIGHT_TILES; r++) {
        const cJSON *row = cJSON_GetArrayItem(rows, r);
        if (!cJSON_IsString(row) || strlen(row->valuestring) != ARENA_WIDTH_TILES) return fail(ctx, "arena row has wrong width");
        for (int c = 0; c < ARENA_WIDTH_TILES; c++) {
            char tile = row->valuestring[c];
            if (tile != '#' && tile != '.') return fail(ctx, "arena tile is not '#' or '.'");
            battle->obstacle[r][c] = tile == '#';
        }
    }
    return true;
}

static bool load_remains(Context *ctx, const cJSON *root, Battle *battle)
{
    const cJSON *list = read_list(ctx, root, "remains", MAX_REMAINS, &battle->remains_count);
    if (!list) return false;
    for (int r = 0; r < battle->remains_count; r++) {
        Remains *rem = &battle->remains[r];
        const cJSON *o = cJSON_GetArrayItem(list, r);
        if (!cJSON_IsObject(o)) return fail(ctx, "remains entry is not an object");
        const cJSON *parts = read_list(ctx, o, "parts", MAX_SLOTS, &rem->part_count);
        if (!parts || !read_position(ctx, o, &rem->x_millipx, &rem->y_millipx)) return false;
        for (int p = 0; p < rem->part_count; p++) {
            const cJSON *id = cJSON_GetArrayItem(parts, p);
            rem->parts[p] = cJSON_IsString(id) ? find_part(ctx->data, id->valuestring) : -1;
            if (rem->parts[p] < 0) return fail(ctx, "remains part is not a known part id");
        }
    }
    return true;
}

static bool load_event(Context *ctx, const cJSON *o, Event *event)
{
    int kind;
    if (!cJSON_IsObject(o)) return fail(ctx, "log entry is not an object");
    if (!read_int(ctx, o, "tick", 0, MAX_TICK, &event->tick) || !read_id(ctx, o, "kind", EVENT_IDS, EVENT_KIND_COUNT, &kind) ||
        !read_combatant_ref(ctx, o, "actor", &event->actor) || !read_slot_ref(ctx, o, "actor_part", &event->actor_part) ||
        !read_combatant_ref(ctx, o, "target", &event->target) || !read_slot_ref(ctx, o, "target_part", &event->target_part) ||
        !read_int(ctx, o, "element", -1, ELEMENT_COUNT - 1, &event->element) ||
        !read_int(ctx, o, "amount", -EVENT_AMOUNT_LIMIT, EVENT_AMOUNT_LIMIT, &event->amount) ||
        !read_int(ctx, o, "detail", -EVENT_AMOUNT_LIMIT, EVENT_AMOUNT_LIMIT, &event->detail))
        return false;
    event->kind = (EventKind)kind;
    return true;
}

static bool load_log(Context *ctx, const cJSON *root, Battle *battle)
{
    const cJSON *list = cJSON_GetObjectItemCaseSensitive(root, "log");
    if (!cJSON_IsArray(list)) return fail_key(ctx, "log", "missing array");
    int count = cJSON_GetArraySize(list);
    battle->log = calloc((size_t)count + 1, sizeof(Event));
    if (!battle->log) return fail(ctx, "out of memory");
    battle->log_capacity = count + 1;
    for (; battle->log_count < count; battle->log_count++)
        if (!load_event(ctx, cJSON_GetArrayItem(list, battle->log_count), &battle->log[battle->log_count])) return false;
    return true;
}

static bool load_header(Context *ctx, const cJSON *root, Battle *battle)
{
    char pantheon[NAME_LEN];
    if (!read_u32(ctx, root, "seed", &battle->seed) || !read_u32(ctx, root, "rng", &battle->rng) ||
        !read_int(ctx, root, "tick", 0, MAX_TICK, &battle->tick) || !read_text(ctx, root, "pantheon", pantheon, NAME_LEN))
        return false;
    if (battle->rng == 0) return fail_key(ctx, "rng", "must not be zero");
    battle->pantheon = -1;
    for (int p = 0; p < ctx->data->pantheon_count; p++)
        if (strcmp(ctx->data->pantheons[p].name, pantheon) == 0) battle->pantheon = p;
    return battle->pantheon >= 0 || fail_key(ctx, "pantheon", "unknown pantheon");
}

static bool load_battle(Context *ctx, const cJSON *root, Battle *battle)
{
    if (!cJSON_IsObject(root)) return fail(ctx, "top level is not an object");
    const GameData *data = ctx->data;
    const cJSON *combatants = read_list(ctx, root, "combatants", MAX_COMBATANTS, &ctx->combatant_count);
    if (!combatants || !load_header(ctx, root, battle) || !load_arena(ctx, root, battle)) return false;
    battle->combatant_count = ctx->combatant_count;
    for (int c = 0; c < battle->combatant_count; c++)
        if (!load_combatant(ctx, cJSON_GetArrayItem(combatants, c), &battle->combatants[c])) return false;
    return load_floor(ctx, root, "pickups", data->pickups, data->pickup_count, battle->pickups, &battle->pickup_count) &&
           load_floor(ctx, root, "traps", data->traps, data->trap_count, battle->traps, &battle->trap_count) &&
           load_remains(ctx, root, battle) && load_log(ctx, root, battle);
}

bool battle_load_json(Battle *battle, const GameData *data, const char *json, char *err, size_t err_size)
{
    Context ctx = {err, err_size, data, 0};
    if (err_size > 0) err[0] = '\0';
    cJSON *root = cJSON_Parse(json);
    if (!root) return fail(&ctx, "not valid JSON");
    Battle *scratch = calloc(1, sizeof *scratch);
    if (!scratch) return cJSON_Delete(root), fail(&ctx, "out of memory");
    bool ok = load_battle(&ctx, root, scratch);
    if (ok) {
        battle_free(battle);
        *battle = *scratch;
    } else {
        battle_free(scratch);
        if (err_size > 0 && err[0] == '\0') fail(&ctx, "invalid battle state");
    }
    free(scratch);
    cJSON_Delete(root);
    return ok;
}
