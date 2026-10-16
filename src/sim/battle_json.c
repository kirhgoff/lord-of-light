#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "battle.h"
#include "cJSON.h"

static const char *const SIDE_IDS[SIDE_COUNT] = {"pantheon", "horde"};
static const char *const STANCE_IDS[STANCE_COUNT] = {"none", "fight", "retreat", "support", "collect"};
static const char *const EVENT_IDS[EVENT_KIND_COUNT] = {"decision", "hit", "pickup", "death"};

static cJSON *int_array(const int *values, int count)
{
    return cJSON_CreateIntArray(values, count);
}

static cJSON *save_order(const Order *order)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "kind", order_kind_id(order->kind));
    cJSON_AddNumberToObject(o, "target", order->target);
    cJSON_AddNumberToObject(o, "x", order->x);
    cJSON_AddNumberToObject(o, "y", order->y);
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
    cJSON_AddNumberToObject(o, "x", c->x);
    cJSON_AddNumberToObject(o, "y", c->y);
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
    cJSON *decision = cJSON_AddObjectToObject(o, "decision");
    cJSON_AddStringToObject(decision, "stance", STANCE_IDS[c->decision.stance]);
    cJSON_AddNumberToObject(decision, "hurt_level", c->decision.hurt_level);
    cJSON_AddNumberToObject(decision, "vulnerable_element", c->decision.vulnerable_element);
    return o;
}

static cJSON *save_floor(const FloorObject *objects, int count)
{
    cJSON *list = cJSON_CreateArray();
    for (int i = 0; i < count; i++) {
        cJSON *o = cJSON_CreateObject();
        cJSON_AddNumberToObject(o, "def", objects[i].def);
        cJSON_AddNumberToObject(o, "x", objects[i].x);
        cJSON_AddNumberToObject(o, "y", objects[i].y);
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
    cJSON_AddItemToObject(root, "pickups", save_floor(battle->pickups, battle->pickup_count));
    cJSON_AddItemToObject(root, "traps", save_floor(battle->traps, battle->trap_count));
    cJSON *remains = cJSON_AddArrayToObject(root, "remains");
    for (int r = 0; r < battle->remains_count; r++) {
        const Remains *rem = &battle->remains[r];
        cJSON *o = cJSON_CreateObject();
        cJSON_AddNumberToObject(o, "x", rem->x);
        cJSON_AddNumberToObject(o, "y", rem->y);
        cJSON_AddItemToObject(o, "parts", int_array(rem->parts, rem->part_count));
        cJSON_AddItemToArray(remains, o);
    }
    cJSON *log = cJSON_AddArrayToObject(root, "log");
    for (int e = 0; e < battle->log_count; e++) {
        const Event *ev = &battle->log[e];
        int fields[4] = {ev->tick, ev->a, ev->b, ev->value};
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "kind", EVENT_IDS[ev->kind]);
        cJSON_AddItemToObject(o, "v", int_array(fields, 4));
        cJSON_AddItemToArray(log, o);
    }
    char *text = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return text;
}

typedef struct {
    char *err;
    size_t err_size;
} Context;

static bool fail(Context *ctx, const char *what)
{
    snprintf(ctx->err, ctx->err_size, "%s", what);
    return false;
}

static bool read_int(Context *ctx, const cJSON *obj, const char *key, int min, int max, int *out)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (!cJSON_IsNumber(item) || item->valuedouble != (double)(long long)item->valuedouble ||
        item->valuedouble < min || item->valuedouble > max) {
        char what[96];
        snprintf(what, sizeof what, "'%s' missing or not an integer in [%d, %d]", key, min, max);
        return fail(ctx, what);
    }
    *out = (int)item->valuedouble;
    return true;
}

static bool read_u32(Context *ctx, const cJSON *obj, const char *key, uint32_t *out)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (!cJSON_IsNumber(item) || item->valuedouble < 0 || item->valuedouble > 4294967295.0 ||
        item->valuedouble != (double)(uint32_t)item->valuedouble)
        return fail(ctx, "seed or rng is not a 32-bit unsigned integer");
    *out = (uint32_t)item->valuedouble;
    return true;
}

static bool read_bool(Context *ctx, const cJSON *obj, const char *key, bool *out)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (!cJSON_IsBool(item)) return fail(ctx, "expected a boolean");
    *out = cJSON_IsTrue(item);
    return true;
}

static bool read_id(Context *ctx, const cJSON *obj, const char *key, const char *const *ids, int count, int *out)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (!cJSON_IsString(item)) return fail(ctx, "expected an id string");
    for (int i = 0; i < count; i++)
        if (strcmp(ids[i], item->valuestring) == 0) return *out = i, true;
    return fail(ctx, "unknown id");
}

static bool read_text(Context *ctx, const cJSON *obj, const char *key, char *out, size_t size)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (!cJSON_IsString(item) || strlen(item->valuestring) >= size) return fail(ctx, "missing or too long string");
    strcpy(out, item->valuestring);
    return true;
}

static const cJSON *read_list(Context *ctx, const cJSON *obj, const char *key, int max, int *count)
{
    const cJSON *list = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (!cJSON_IsArray(list) || cJSON_GetArraySize(list) > max) {
        fail(ctx, "missing array or too many entries");
        return NULL;
    }
    *count = cJSON_GetArraySize(list);
    return list;
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
    return read_int(ctx, obj, "target", -1, MAX_COMBATANTS - 1, &order->target) &&
           read_int(ctx, obj, "x", -100000, 100000, &order->x) && read_int(ctx, obj, "y", -100000, 100000, &order->y);
}

static bool load_body(Context *ctx, const GameData *data, const cJSON *obj, Body *body)
{
    if (!cJSON_IsObject(obj)) return fail(ctx, "body is not an object");
    memset(body, 0, sizeof *body);
    const cJSON *slots = read_list(ctx, obj, "slots", MAX_SLOTS, &body->slot_count);
    if (!slots || body->slot_count == 0) return fail(ctx, "body needs 1 to MAX_SLOTS slots");
    for (int s = 0; s < body->slot_count; s++) {
        const cJSON *slot = cJSON_GetArrayItem(slots, s);
        char part_id[ID_LEN];
        if (!cJSON_IsObject(slot) || !read_text(ctx, slot, "id", body->slots[s].id, ID_LEN) ||
            !read_bool(ctx, slot, "internal", &body->slots[s].internal) ||
            !read_int(ctx, slot, "x", -1000, 1000, &body->slots[s].anchor_x) ||
            !read_int(ctx, slot, "y", -1000, 1000, &body->slots[s].anchor_y) ||
            !read_text(ctx, slot, "part", part_id, ID_LEN))
            return false;
        body->slots[s].part = find_part(data, part_id);
        if (body->slots[s].part < 0) return fail(ctx, "unknown part id");
    }
    const cJSON *edges = read_list(ctx, obj, "edges", MAX_EDGES, &body->edge_count);
    if (!edges || !read_int(ctx, obj, "core", 0, body->slot_count - 1, &body->core)) return false;
    for (int e = 0; e < body->edge_count; e++) {
        const cJSON *pair = cJSON_GetArrayItem(edges, e);
        if (cJSON_GetArraySize(pair) != 2) return fail(ctx, "edge is not a pair");
        for (int end = 0; end < 2; end++) {
            const cJSON *index = cJSON_GetArrayItem(pair, end);
            int value = cJSON_IsNumber(index) ? (int)index->valuedouble : -1;
            if (value < 0 || value >= body->slot_count) return fail(ctx, "edge index out of range");
            if (end == 0) body->edges[e].a = value;
            else body->edges[e].b = value;
        }
    }
    return true;
}

static bool load_combatant(Context *ctx, const GameData *data, const cJSON *obj, Combatant *c)
{
    int side, charm_side, stance;
    memset(c, 0, sizeof *c);
    if (!cJSON_IsObject(obj) || !read_text(ctx, obj, "name", c->name, NAME_LEN) ||
        !read_id(ctx, obj, "side", SIDE_IDS, SIDE_COUNT, &side) || !read_bool(ctx, obj, "alive", &c->alive) ||
        !read_int(ctx, obj, "x", 0, ARENA_WIDTH_TILES * TILE_SIZE, &c->x) ||
        !read_int(ctx, obj, "y", 0, ARENA_HEIGHT_TILES * TILE_SIZE, &c->y) ||
        !load_body(ctx, data, cJSON_GetObjectItemCaseSensitive(obj, "body"), &c->body))
        return false;
    int part_count;
    const cJSON *parts = read_list(ctx, obj, "parts", MAX_SLOTS, &part_count);
    if (!parts || part_count != c->body.slot_count) return fail(ctx, "parts must match body slots");
    for (int s = 0; s < part_count; s++) {
        const cJSON *part = cJSON_GetArrayItem(parts, s);
        if (!cJSON_IsObject(part) || !read_int(ctx, part, "integrity", 0, 1000000, &c->parts[s].integrity) ||
            !read_int(ctx, part, "cooldown", 0, 1000000, &c->parts[s].cooldown) ||
            !load_order(ctx, cJSON_GetObjectItemCaseSensitive(part, "order"), &c->parts[s].order))
            return false;
    }
    const cJSON *decision = cJSON_GetObjectItemCaseSensitive(obj, "decision");
    if (!cJSON_IsObject(decision) || !read_id(ctx, decision, "stance", STANCE_IDS, STANCE_COUNT, &stance) ||
        !read_int(ctx, decision, "hurt_level", 0, 100, &c->decision.hurt_level) ||
        !read_int(ctx, decision, "vulnerable_element", -1, ELEMENT_COUNT - 1, &c->decision.vulnerable_element))
        return false;
    c->side = (Side)side;
    c->decision.stance = (Stance)stance;
    if (!read_id(ctx, obj, "charm_side", SIDE_IDS, SIDE_COUNT, &charm_side)) return false;
    c->charm_side = (Side)charm_side;
    return read_int(ctx, obj, "vitality", 0, 1000000, &c->vitality) &&
           read_int(ctx, obj, "vitality_max", 0, 1000000, &c->vitality_max) &&
           read_int(ctx, obj, "slow_ticks", 0, 1000000, &c->slow_ticks) &&
           read_int(ctx, obj, "dot_ticks", 0, 1000000, &c->dot_ticks) &&
           read_int(ctx, obj, "charm_ticks", 0, 1000000, &c->charm_ticks);
}

static bool load_floor(Context *ctx, const cJSON *root, const char *key, int defs, FloorObject *objects, int *count)
{
    const cJSON *list = read_list(ctx, root, key, MAX_FLOOR_OBJECTS, count);
    if (!list) return false;
    for (int i = 0; i < *count; i++) {
        const cJSON *o = cJSON_GetArrayItem(list, i);
        if (!cJSON_IsObject(o) || !read_int(ctx, o, "def", 0, defs - 1, &objects[i].def) ||
            !read_int(ctx, o, "x", 0, ARENA_WIDTH_TILES * TILE_SIZE, &objects[i].x) ||
            !read_int(ctx, o, "y", 0, ARENA_HEIGHT_TILES * TILE_SIZE, &objects[i].y) ||
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

static bool load_remains(Context *ctx, const GameData *data, const cJSON *root, Battle *battle)
{
    const cJSON *list = read_list(ctx, root, "remains", MAX_REMAINS, &battle->remains_count);
    if (!list) return false;
    for (int r = 0; r < battle->remains_count; r++) {
        Remains *rem = &battle->remains[r];
        const cJSON *o = cJSON_GetArrayItem(list, r);
        const cJSON *parts = cJSON_IsObject(o) ? read_list(ctx, o, "parts", MAX_SLOTS, &rem->part_count) : NULL;
        if (!parts || !read_int(ctx, o, "x", 0, ARENA_WIDTH_TILES * TILE_SIZE, &rem->x) ||
            !read_int(ctx, o, "y", 0, ARENA_HEIGHT_TILES * TILE_SIZE, &rem->y))
            return false;
        for (int p = 0; p < rem->part_count; p++) {
            const cJSON *index = cJSON_GetArrayItem(parts, p);
            rem->parts[p] = cJSON_IsNumber(index) ? (int)index->valuedouble : -1;
            if (rem->parts[p] < 0 || rem->parts[p] >= data->part_count) return fail(ctx, "remains part index out of range");
        }
    }
    return true;
}

static bool load_log(Context *ctx, const cJSON *root, Battle *battle)
{
    const cJSON *list = read_list(ctx, root, "log", MAX_EVENTS, &battle->log_count);
    if (!list) return false;
    for (int e = 0; e < battle->log_count; e++) {
        const cJSON *o = cJSON_GetArrayItem(list, e);
        const cJSON *fields = cJSON_IsObject(o) ? cJSON_GetObjectItemCaseSensitive(o, "v") : NULL;
        int kind;
        if (!fields || cJSON_GetArraySize(fields) != 4 || !read_id(ctx, o, "kind", EVENT_IDS, EVENT_KIND_COUNT, &kind))
            return fail(ctx, "bad log entry");
        int values[4];
        for (int i = 0; i < 4; i++) {
            const cJSON *v = cJSON_GetArrayItem(fields, i);
            if (!cJSON_IsNumber(v)) return fail(ctx, "bad log entry");
            values[i] = (int)v->valuedouble;
        }
        battle->log[e] = (Event){values[0], (EventKind)kind, values[1], values[2], values[3]};
    }
    return true;
}

static bool load_battle(Context *ctx, const GameData *data, const cJSON *root, Battle *battle)
{
    char pantheon[NAME_LEN];
    if (!cJSON_IsObject(root) || !read_u32(ctx, root, "seed", &battle->seed) || !read_u32(ctx, root, "rng", &battle->rng) ||
        battle->rng == 0 || !read_int(ctx, root, "tick", 0, 2000000000, &battle->tick) ||
        !read_text(ctx, root, "pantheon", pantheon, NAME_LEN))
        return fail(ctx, "bad header (seed, rng, tick or pantheon)");
    battle->pantheon = -1;
    for (int p = 0; p < data->pantheon_count; p++)
        if (strcmp(data->pantheons[p].name, pantheon) == 0) battle->pantheon = p;
    if (battle->pantheon < 0) return fail(ctx, "unknown pantheon");
    if (!load_arena(ctx, root, battle)) return false;
    const cJSON *combatants = read_list(ctx, root, "combatants", MAX_COMBATANTS, &battle->combatant_count);
    if (!combatants) return false;
    for (int c = 0; c < battle->combatant_count; c++)
        if (!load_combatant(ctx, data, cJSON_GetArrayItem(combatants, c), &battle->combatants[c])) return false;
    return load_floor(ctx, root, "pickups", data->pickup_count, battle->pickups, &battle->pickup_count) &&
           load_floor(ctx, root, "traps", data->trap_count, battle->traps, &battle->trap_count) &&
           load_remains(ctx, data, root, battle) && load_log(ctx, root, battle);
}

bool battle_load_json(Battle *battle, const GameData *data, const char *json, char *err, size_t err_size)
{
    Context ctx = {err, err_size};
    cJSON *root = cJSON_Parse(json);
    if (!root) return fail(&ctx, "not valid JSON");
    Battle *scratch = calloc(1, sizeof *scratch);
    bool ok = load_battle(&ctx, data, root, scratch);
    if (ok) *battle = *scratch;
    free(scratch);
    cJSON_Delete(root);
    return ok;
}
