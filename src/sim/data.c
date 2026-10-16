#include "data.h"

#include <dirent.h>
#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"

#define PATH_LEN 160

static const char *const ELEMENT_IDS[ELEMENT_COUNT] = {
    "kinetic", "fire", "frost", "lightning", "radiance", "venom", "mind", "void"
};

static const char *const ORDER_KIND_IDS[ORDER_KIND_COUNT] = {
    "strike", "cast", "block", "move_to", "pick_up", "support", "spawn", "idle"
};

static const char *const SLOT_ROLE_IDS[SLOT_ROLE_COUNT] = {
    "any", "heart", "mover", "attack", "sense"
};

typedef struct {
    const char *file;
    char *err;
    size_t err_size;
} Context;

const char *element_id(Element element)
{
    return ELEMENT_IDS[element];
}

const char *order_kind_id(OrderKind kind)
{
    return ORDER_KIND_IDS[kind];
}

int find_part(const GameData *data, const char *id)
{
    for (int i = 0; i < data->part_count; i++)
        if (strcmp(data->parts[i].id, id) == 0) return i;
    return -1;
}

int find_body_plan(const GameData *data, const char *id)
{
    for (int i = 0; i < data->body_plan_count; i++)
        if (strcmp(data->body_plans[i].id, id) == 0) return i;
    return -1;
}

int find_body_slot(const Body *body, const char *id)
{
    for (int i = 0; i < body->slot_count; i++)
        if (strcmp(body->slots[i].id, id) == 0) return i;
    return -1;
}

static bool has_strike_or_cast(const Part *part)
{
    for (int a = 0; a < part->ability_count; a++)
        if (part->abilities[a].order == ORDER_STRIKE || part->abilities[a].order == ORDER_CAST) return true;
    return false;
}

bool part_fits_role(const Part *part, SlotRole role)
{
    switch (role) {
    case SLOT_ROLE_HEART: return part->internal && part->vitality > 0;
    case SLOT_ROLE_MOVER: return part->move_speed_px_per_second > 0;
    case SLOT_ROLE_ATTACK: return has_strike_or_cast(part) && part->vision_far == 0;
    case SLOT_ROLE_SENSE: return part->vision_far > 0;
    default: return true;
    }
}

static bool fail(Context *ctx, const char *path, const char *format, ...)
{
    int used = snprintf(ctx->err, ctx->err_size, path[0] ? "%s: %s: " : "%s: ", ctx->file, path);
    if (used < 0 || (size_t)used >= ctx->err_size) return false;
    va_list args;
    va_start(args, format);
    vsnprintf(ctx->err + used, ctx->err_size - (size_t)used, format, args);
    va_end(args);
    return false;
}

static void join_key(char *out, const char *parent, const char *key)
{
    snprintf(out, PATH_LEN, parent[0] ? "%s.%s" : "%s%s", parent, key);
}

static void join_index(char *out, const char *parent, int index)
{
    snprintf(out, PATH_LEN, "%s[%d]", parent, index);
}

static const cJSON *get_item(Context *ctx, const cJSON *obj, const char *parent, const char *key,
                             bool required, bool *ok)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    char path[PATH_LEN];
    join_key(path, parent, key);
    *ok = true;
    if (!item && required) *ok = fail(ctx, path, "missing");
    return item;
}

static bool get_string(Context *ctx, const cJSON *obj, const char *parent, const char *key,
                       char *out, size_t size, bool required)
{
    bool ok;
    const cJSON *item = get_item(ctx, obj, parent, key, required, &ok);
    if (!ok) return false;
    if (!item) {
        out[0] = '\0';
        return true;
    }
    char path[PATH_LEN];
    join_key(path, parent, key);
    if (!cJSON_IsString(item)) return fail(ctx, path, "expected a string");
    if (strlen(item->valuestring) >= size) return fail(ctx, path, "string too long");
    strcpy(out, item->valuestring);
    return true;
}

bool json_to_int(const cJSON *item, int *out)
{
    if (!cJSON_IsNumber(item)) return false;
    double value = item->valuedouble;
    if (!(value == floor(value) && value >= INT_MIN && value <= INT_MAX)) return false;
    *out = (int)value;
    return true;
}

static bool number_to_int(Context *ctx, const char *path, const cJSON *item, int *out)
{
    if (!cJSON_IsNumber(item)) return fail(ctx, path, "expected a number");
    return json_to_int(item, out) || fail(ctx, path, "expected an integer");
}

static bool get_int(Context *ctx, const cJSON *obj, const char *parent, const char *key, int *out,
                    bool required)
{
    bool ok;
    const cJSON *item = get_item(ctx, obj, parent, key, required, &ok);
    if (!ok) return false;
    if (!item) return true;
    char path[PATH_LEN];
    join_key(path, parent, key);
    return number_to_int(ctx, path, item, out);
}

static bool get_count(Context *ctx, const cJSON *obj, const char *parent, const char *key, int *out,
                      bool required)
{
    if (!get_int(ctx, obj, parent, key, out, required)) return false;
    char path[PATH_LEN];
    join_key(path, parent, key);
    return *out >= 0 || fail(ctx, path, "must not be negative");
}

static bool get_bool(Context *ctx, const cJSON *obj, const char *parent, const char *key, bool *out)
{
    bool ok;
    const cJSON *item = get_item(ctx, obj, parent, key, false, &ok);
    if (!item) return true;
    if (!cJSON_IsBool(item)) {
        char path[PATH_LEN];
        join_key(path, parent, key);
        return fail(ctx, path, "expected true or false");
    }
    *out = cJSON_IsTrue(item);
    return true;
}

static bool get_array(Context *ctx, const cJSON *obj, const char *parent, const char *key,
                      const cJSON **out, bool required)
{
    bool ok;
    *out = get_item(ctx, obj, parent, key, required, &ok);
    if (!ok) return false;
    if (*out && !cJSON_IsArray(*out)) {
        char path[PATH_LEN];
        join_key(path, parent, key);
        return fail(ctx, path, "expected an array");
    }
    return true;
}

static bool get_object(Context *ctx, const cJSON *obj, const char *parent, const char *key,
                       const cJSON **out, bool required)
{
    bool ok;
    *out = get_item(ctx, obj, parent, key, required, &ok);
    if (!ok) return false;
    if (*out && !cJSON_IsObject(*out)) {
        char path[PATH_LEN];
        join_key(path, parent, key);
        return fail(ctx, path, "expected an object");
    }
    return true;
}

static bool lookup_element(Context *ctx, const char *path, const char *name, Element *out)
{
    for (int i = 0; i < ELEMENT_COUNT; i++) {
        if (strcmp(ELEMENT_IDS[i], name) == 0) {
            *out = (Element)i;
            return true;
        }
    }
    return fail(ctx, path, "unknown element \"%s\"", name);
}

static bool get_element(Context *ctx, const cJSON *obj, const char *parent, const char *key,
                        Element *out, bool required)
{
    char name[ID_LEN];
    char path[PATH_LEN];
    join_key(path, parent, key);
    if (!get_string(ctx, obj, parent, key, name, sizeof name, required)) return false;
    return name[0] == '\0' || lookup_element(ctx, path, name, out);
}

static bool lookup_order_kind(Context *ctx, const char *path, const char *name, OrderKind *out)
{
    for (int i = 0; i < ORDER_KIND_COUNT; i++) {
        if (strcmp(ORDER_KIND_IDS[i], name) == 0) {
            *out = (OrderKind)i;
            return true;
        }
    }
    return fail(ctx, path, "unknown order kind \"%s\"", name);
}

static char *read_file(const char *path)
{
    FILE *file = fopen(path, "rb");
    if (!file) return NULL;
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);
    char *text = malloc((size_t)size + 1);
    if (text && fread(text, 1, (size_t)size, file) == (size_t)size) {
        text[size] = '\0';
    } else {
        free(text);
        text = NULL;
    }
    fclose(file);
    return text;
}

static cJSON *parse_file(Context *ctx, const char *path)
{
    char *text = read_file(path);
    if (!text) {
        fail(ctx, "", "cannot read file");
        return NULL;
    }
    cJSON *root = cJSON_Parse(text);
    if (!root) {
        const char *near = cJSON_GetErrorPtr();
        fail(ctx, "", "invalid JSON near \"%.20s\"", near ? near : "");
    }
    free(text);
    return root;
}

typedef bool (*RootLoader)(Context *ctx, const cJSON *root, GameData *data);

static bool load_file(GameData *data, const char *path, char *err, size_t err_size, RootLoader loader)
{
    Context ctx = { path, err, err_size };
    cJSON *root = parse_file(&ctx, path);
    if (!root) return false;
    static GameData scratch;
    scratch = *data;
    bool ok = cJSON_IsObject(root) ? loader(&ctx, root, &scratch) : fail(&ctx, "", "expected an object");
    cJSON_Delete(root);
    if (ok) *data = scratch;
    return ok;
}

static bool read_element_def(Context *ctx, const cJSON *obj, const char *path, ElementDef *def)
{
    return get_int(ctx, obj, path, "dot_damage", &def->dot_damage, false)
        && get_int(ctx, obj, path, "dot_ticks", &def->dot_ticks, false)
        && get_int(ctx, obj, path, "slow_percent", &def->slow_percent, false)
        && get_int(ctx, obj, path, "slow_ticks", &def->slow_ticks, false)
        && get_int(ctx, obj, path, "chain_targets", &def->chain_targets, false)
        && get_int(ctx, obj, path, "chain_range", &def->chain_range, false)
        && get_int(ctx, obj, path, "pierce_targets", &def->pierce_targets, false)
        && get_bool(ctx, obj, path, "internal_first", &def->internal_first)
        && get_int(ctx, obj, path, "charm_ticks", &def->charm_ticks, false)
        && get_int(ctx, obj, path, "buff_strip_count", &def->buff_strip_count, false);
}

static bool elements_from_root(Context *ctx, const cJSON *root, GameData *data)
{
    const cJSON *list;
    if (!get_array(ctx, root, "", "elements", &list, true)) return false;
    ElementDef loaded[ELEMENT_COUNT] = { 0 };
    bool seen[ELEMENT_COUNT] = { false };
    int index = 0;
    const cJSON *item;
    cJSON_ArrayForEach(item, list) {
        char path[PATH_LEN], id[ID_LEN];
        join_index(path, "elements", index++);
        Element element;
        if (!cJSON_IsObject(item)) return fail(ctx, path, "expected an object");
        if (!get_string(ctx, item, path, "id", id, sizeof id, true)) return false;
        char id_path[PATH_LEN];
        join_key(id_path, path, "id");
        if (!lookup_element(ctx, id_path, id, &element)) return false;
        if (seen[element]) return fail(ctx, id_path, "duplicate element \"%s\"", id);
        seen[element] = true;
        if (!read_element_def(ctx, item, path, &loaded[element])) return false;
    }
    for (int i = 0; i < ELEMENT_COUNT; i++)
        if (!seen[i]) return fail(ctx, "elements", "missing element \"%s\"", ELEMENT_IDS[i]);
    memcpy(data->elements, loaded, sizeof loaded);
    return true;
}

static bool read_slot_position(Context *ctx, const cJSON *obj, const char *path, BodySlot *slot)
{
    return get_string(ctx, obj, path, "id", slot->id, sizeof slot->id, true)
        && get_int(ctx, obj, path, "x", &slot->anchor_x, false)
        && get_int(ctx, obj, path, "y", &slot->anchor_y, false);
}

static bool read_slot_role(Context *ctx, const cJSON *obj, const char *path, BodySlot *slot)
{
    char role[ID_LEN], role_path[PATH_LEN];
    if (!get_string(ctx, obj, path, "role", role, sizeof role, false)) return false;
    join_key(role_path, path, "role");
    if (role[0]) {
        slot->role = SLOT_ROLE_COUNT;
        for (int r = 0; r < SLOT_ROLE_COUNT; r++)
            if (strcmp(role, SLOT_ROLE_IDS[r]) == 0) slot->role = (SlotRole)r;
        if (slot->role == SLOT_ROLE_COUNT) return fail(ctx, role_path, "unknown role \"%s\"", role);
    }
    if (slot->role == SLOT_ROLE_HEART && !slot->internal) return fail(ctx, role_path, "role \"heart\" needs an internal slot");
    if (slot->role != SLOT_ROLE_ANY && slot->role != SLOT_ROLE_HEART && slot->internal)
        return fail(ctx, role_path, "role \"%s\" needs an external slot", SLOT_ROLE_IDS[slot->role]);
    return true;
}

static bool read_plan_slot(Context *ctx, const cJSON *obj, const char *path, const GameData *data,
                           BodySlot *slot)
{
    (void)data;
    char kind[ID_LEN];
    if (!read_slot_position(ctx, obj, path, slot)) return false;
    if (!get_string(ctx, obj, path, "kind", kind, sizeof kind, true)) return false;
    char kind_path[PATH_LEN];
    join_key(kind_path, path, "kind");
    if (strcmp(kind, "internal") == 0) slot->internal = true;
    else if (strcmp(kind, "external") != 0) return fail(ctx, kind_path, "expected \"external\" or \"internal\"");
    slot->part = -1;
    return read_slot_role(ctx, obj, path, slot)
        && get_string(ctx, obj, path, "part", slot->fixed_part_id, sizeof slot->fixed_part_id, false);
}

static bool read_deity_slot(Context *ctx, const cJSON *obj, const char *path, const GameData *data,
                            BodySlot *slot)
{
    char part_id[ID_LEN];
    if (!read_slot_position(ctx, obj, path, slot)) return false;
    if (!get_string(ctx, obj, path, "part", part_id, sizeof part_id, true)) return false;
    char part_path[PATH_LEN];
    join_key(part_path, path, "part");
    slot->part = find_part(data, part_id);
    if (slot->part < 0) return fail(ctx, part_path, "unknown part \"%s\"", part_id);
    slot->internal = data->parts[slot->part].internal;
    return true;
}

typedef bool (*SlotReader)(Context *ctx, const cJSON *obj, const char *path, const GameData *data,
                           BodySlot *slot);

static bool read_slots(Context *ctx, const cJSON *obj, const char *path, const char *key,
                       const GameData *data, SlotReader read_slot, Body *body)
{
    const cJSON *list;
    char list_path[PATH_LEN];
    join_key(list_path, path, key);
    if (!get_array(ctx, obj, path, key, &list, true)) return false;
    const cJSON *item;
    cJSON_ArrayForEach(item, list) {
        char slot_path[PATH_LEN];
        join_index(slot_path, list_path, body->slot_count);
        if (body->slot_count == MAX_SLOTS) return fail(ctx, list_path, "more than %d slots", MAX_SLOTS);
        if (!cJSON_IsObject(item)) return fail(ctx, slot_path, "expected an object");
        BodySlot *slot = &body->slots[body->slot_count];
        if (!read_slot(ctx, item, slot_path, data, slot)) return false;
        if (find_body_slot(body, slot->id) >= 0) {
            char id_path[PATH_LEN];
            join_key(id_path, slot_path, "id");
            return fail(ctx, id_path, "duplicate slot \"%s\"", slot->id);
        }
        body->slot_count++;
    }
    return true;
}

static bool lookup_slot(Context *ctx, const char *path, const Body *body, const cJSON *name, int *out)
{
    if (!cJSON_IsString(name)) return fail(ctx, path, "expected a slot id");
    *out = find_body_slot(body, name->valuestring);
    if (*out < 0) return fail(ctx, path, "unknown slot \"%s\"", name->valuestring);
    return true;
}

static bool has_edge(const Body *body, int a, int b)
{
    for (int i = 0; i < body->edge_count; i++) {
        const BodyEdge *edge = &body->edges[i];
        if ((edge->a == a && edge->b == b) || (edge->a == b && edge->b == a)) return true;
    }
    return false;
}

static bool read_edges(Context *ctx, const cJSON *obj, const char *path, Body *body)
{
    const cJSON *list;
    char list_path[PATH_LEN];
    join_key(list_path, path, "edges");
    if (!get_array(ctx, obj, path, "edges", &list, false)) return false;
    if (!list) return true;
    const cJSON *pair;
    cJSON_ArrayForEach(pair, list) {
        char edge_path[PATH_LEN], end_path[PATH_LEN];
        join_index(edge_path, list_path, body->edge_count);
        if (body->edge_count == MAX_EDGES) return fail(ctx, list_path, "more than %d edges", MAX_EDGES);
        if (!cJSON_IsArray(pair) || cJSON_GetArraySize(pair) != 2)
            return fail(ctx, edge_path, "expected a pair of slot ids");
        BodyEdge *edge = &body->edges[body->edge_count];
        join_index(end_path, edge_path, 0);
        if (!lookup_slot(ctx, end_path, body, cJSON_GetArrayItem(pair, 0), &edge->a)) return false;
        join_index(end_path, edge_path, 1);
        if (!lookup_slot(ctx, end_path, body, cJSON_GetArrayItem(pair, 1), &edge->b)) return false;
        if (edge->a == edge->b) return fail(ctx, edge_path, "edge joins a slot to itself");
        if (has_edge(body, edge->a, edge->b)) return fail(ctx, edge_path, "duplicate edge");
        body->edge_count++;
    }
    return true;
}

static bool read_core(Context *ctx, const cJSON *obj, const char *path, Body *body)
{
    char core_path[PATH_LEN];
    join_key(core_path, path, "core");
    const cJSON *name = cJSON_GetObjectItemCaseSensitive(obj, "core");
    if (!name) return fail(ctx, core_path, "missing core");
    return lookup_slot(ctx, core_path, body, name, &body->core);
}

static bool check_reachable_from_core(Context *ctx, const char *path, const char *slots_key,
                                      const Body *body)
{
    bool reached[MAX_SLOTS] = { false };
    reached[body->core] = true;
    for (bool grew = true; grew;) {
        grew = false;
        for (int i = 0; i < body->edge_count; i++) {
            const BodyEdge *edge = &body->edges[i];
            if (reached[edge->a] != reached[edge->b]) {
                reached[edge->a] = reached[edge->b] = true;
                grew = true;
            }
        }
    }
    for (int i = 0; i < body->slot_count; i++) {
        if (reached[i]) continue;
        char list_path[PATH_LEN], slot_path[PATH_LEN];
        join_key(list_path, path, slots_key);
        join_index(slot_path, list_path, i);
        return fail(ctx, slot_path, "slot \"%s\" is not connected to the core", body->slots[i].id);
    }
    return true;
}

static bool read_body(Context *ctx, const cJSON *obj, const char *path, const char *slots_key,
                      const GameData *data, SlotReader read_slot, Body *body)
{
    memset(body, 0, sizeof *body);
    return read_slots(ctx, obj, path, slots_key, data, read_slot, body)
        && read_edges(ctx, obj, path, body)
        && read_core(ctx, obj, path, body)
        && check_reachable_from_core(ctx, path, slots_key, body);
}

bool is_spawn_plan(const GameData *data, int plan)
{
    for (int p = 0; p < data->part_count; p++)
        if (data->parts[p].spawn_plan == plan) return true;
    return false;
}

static bool is_core_part(const GameData *data, int part)
{
    for (int p = 0; p < data->body_plan_count; p++) {
        const Body *body = &data->body_plans[p].body;
        if (body->slots[body->core].part == part) return true;
    }
    return false;
}

bool fills_horde_slot(const GameData *data, int part, const BodySlot *slot)
{
    const Part *candidate = &data->parts[part];
    return !candidate->deity_only && !candidate->minion_only && !is_core_part(data, part)
        && candidate->internal == slot->internal && part_fits_role(candidate, slot->role);
}

static bool resolve_fixed_part(Context *ctx, const GameData *data, bool spawn_plan, const char *path, BodySlot *slot)
{
    char part_path[PATH_LEN];
    join_key(part_path, path, "part");
    slot->part = find_part(data, slot->fixed_part_id);
    if (slot->part < 0) return fail(ctx, part_path, "unknown part \"%s\"", slot->fixed_part_id);
    const Part *part = &data->parts[slot->part];
    if (part->internal != slot->internal)
        return fail(ctx, part_path, "part \"%s\" is %s", part->id, part->internal ? "internal" : "external");
    if (!part_fits_role(part, slot->role))
        return fail(ctx, part_path, "part \"%s\" does not fit role \"%s\"", part->id, SLOT_ROLE_IDS[slot->role]);
    if (part->deity_only) return fail(ctx, part_path, "part \"%s\" is deity only", part->id);
    if (part->minion_only && !spawn_plan) return fail(ctx, part_path, "part \"%s\" is minion only", part->id);
    return true;
}

static bool check_spawn_slots_fixed(Context *ctx, const GameData *data, int index)
{
    const Body *body = &data->body_plans[index].body;
    for (int s = 0; s < body->slot_count; s++) {
        if (body->slots[s].part >= 0) continue;
        char path[PATH_LEN];
        join_index(path, "bodyplans", index);
        join_key(path, path, "slots");
        join_index(path, path, s);
        return fail(ctx, path, "spawn plan slot \"%s\" has no part", body->slots[s].id);
    }
    return true;
}

static bool has_eligible_part(const GameData *data, const BodySlot *slot)
{
    for (int p = 0; p < data->part_count; p++)
        if (fills_horde_slot(data, p, slot)) return true;
    return false;
}

static bool check_horde_slots_fillable(Context *ctx, const GameData *data, int index)
{
    const Body *body = &data->body_plans[index].body;
    if (is_spawn_plan(data, index)) return check_spawn_slots_fixed(ctx, data, index);
    for (int s = 0; s < body->slot_count; s++) {
        if (body->slots[s].part >= 0 || has_eligible_part(data, &body->slots[s])) continue;
        char path[PATH_LEN];
        join_index(path, "bodyplans", index);
        join_key(path, path, "slots");
        join_index(path, path, s);
        return fail(ctx, path, "no part fills slot \"%s\"", body->slots[s].id);
    }
    return true;
}

static bool slot_can_move(const GameData *data, const BodySlot *slot)
{
    return slot->role == SLOT_ROLE_MOVER || (slot->part >= 0 && data->parts[slot->part].move_speed_px_per_second > 0);
}

static bool resolve_body_plan(Context *ctx, GameData *data, int index)
{
    Body *body = &data->body_plans[index].body;
    bool has_heart = false, can_move = false;
    for (int s = 0; s < body->slot_count; s++) {
        char path[PATH_LEN], list_path[PATH_LEN];
        join_index(list_path, "bodyplans", index);
        join_key(list_path, list_path, "slots");
        join_index(path, list_path, s);
        if (body->slots[s].fixed_part_id[0] && !resolve_fixed_part(ctx, data, is_spawn_plan(data, index), path, &body->slots[s])) return false;
        has_heart |= body->slots[s].role == SLOT_ROLE_HEART;
        can_move |= slot_can_move(data, &body->slots[s]);
    }
    char path[PATH_LEN];
    join_index(path, "bodyplans", index);
    if (!has_heart) return fail(ctx, path, "body plan \"%s\" has no heart slot", data->body_plans[index].id);
    if (!can_move) return fail(ctx, path, "body plan \"%s\" has no mover slot", data->body_plans[index].id);
    return true;
}

static bool resolve_body_plans(Context *ctx, GameData *data)
{
    if (data->part_count == 0 || data->body_plan_count == 0) return true;
    for (int p = 0; p < data->body_plan_count; p++)
        if (!resolve_body_plan(ctx, data, p)) return false;
    for (int p = 0; p < data->body_plan_count; p++)
        if (!check_horde_slots_fillable(ctx, data, p)) return false;
    for (int p = 0; p < data->body_plan_count; p++)
        if (!is_spawn_plan(data, p)) return true;
    return fail(ctx, "bodyplans", "no body plan for the Horde");
}

static bool body_plans_from_root(Context *ctx, const cJSON *root, GameData *data)
{
    const cJSON *list;
    if (!get_array(ctx, root, "", "bodyplans", &list, true)) return false;
    int count = 0;
    BodyPlan plans[MAX_BODY_PLANS];
    const cJSON *item;
    cJSON_ArrayForEach(item, list) {
        char path[PATH_LEN];
        join_index(path, "bodyplans", count);
        if (count == MAX_BODY_PLANS) return fail(ctx, "bodyplans", "more than %d body plans", MAX_BODY_PLANS);
        if (!cJSON_IsObject(item)) return fail(ctx, path, "expected an object");
        BodyPlan *plan = &plans[count];
        if (!get_string(ctx, item, path, "id", plan->id, sizeof plan->id, true)) return false;
        for (int i = 0; i < count; i++)
            if (strcmp(plans[i].id, plan->id) == 0) {
                char id_path[PATH_LEN];
                join_key(id_path, path, "id");
                return fail(ctx, id_path, "duplicate body plan \"%s\"", plan->id);
            }
        if (!read_body(ctx, item, path, "slots", data, read_plan_slot, &plan->body)) return false;
        count++;
    }
    memcpy(data->body_plans, plans, sizeof(BodyPlan) * (size_t)count);
    data->body_plan_count = count;
    return resolve_body_plans(ctx, data);
}

static bool check_non_negative(Context *ctx, const char *path, const char *key, int value)
{
    char field_path[PATH_LEN];
    join_key(field_path, path, key);
    return value >= 0 || fail(ctx, field_path, "must not be negative");
}

static bool read_ability(Context *ctx, const cJSON *obj, const char *path, Ability *ability)
{
    char order[ID_LEN];
    if (!get_string(ctx, obj, path, "order", order, sizeof order, true)) return false;
    char order_path[PATH_LEN];
    join_key(order_path, path, "order");
    return lookup_order_kind(ctx, order_path, order, &ability->order)
        && get_element(ctx, obj, path, "element", &ability->element, true)
        && get_count(ctx, obj, path, "damage", &ability->damage, false)
        && get_count(ctx, obj, path, "heal", &ability->heal, false)
        && get_count(ctx, obj, path, "range", &ability->range, false)
        && get_count(ctx, obj, path, "cooldown", &ability->cooldown_ticks, false)
        && get_count(ctx, obj, path, "area", &ability->area, false)
        && check_non_negative(ctx, path, "damage", ability->damage)
        && check_non_negative(ctx, path, "heal", ability->heal);
}

static bool read_abilities(Context *ctx, const cJSON *obj, const char *path, Part *part)
{
    const cJSON *list;
    char list_path[PATH_LEN];
    join_key(list_path, path, "abilities");
    if (!get_array(ctx, obj, path, "abilities", &list, false)) return false;
    const cJSON *item;
    cJSON_ArrayForEach(item, list) {
        char ability_path[PATH_LEN];
        join_index(ability_path, list_path, part->ability_count);
        if (part->ability_count == MAX_ABILITIES)
            return fail(ctx, list_path, "more than %d abilities", MAX_ABILITIES);
        if (!cJSON_IsObject(item)) return fail(ctx, ability_path, "expected an object");
        if (!read_ability(ctx, item, ability_path, &part->abilities[part->ability_count])) return false;
        part->ability_count++;
    }
    return true;
}

static bool read_resistances(Context *ctx, const cJSON *obj, const char *path, Part *part)
{
    const cJSON *map;
    if (!get_object(ctx, obj, path, "resistances", &map, false)) return false;
    const cJSON *item;
    cJSON_ArrayForEach(item, map) {
        char item_path[PATH_LEN];
        Element element;
        join_key(item_path, path, "resistances");
        join_key(item_path, item_path, item->string);
        if (!lookup_element(ctx, item_path, item->string, &element)) return false;
        if (!number_to_int(ctx, item_path, item, &part->resistance_percent[element])) return false;
        if (part->resistance_percent[element] < -100 || part->resistance_percent[element] > 100)
            return fail(ctx, item_path, "resistance must be between -100 and 100");
    }
    return true;
}

static bool read_vision(Context *ctx, const cJSON *obj, const char *path, Part *part)
{
    const cJSON *vision;
    if (!get_object(ctx, obj, path, "vision", &vision, false)) return false;
    if (!vision) return true;
    char vision_path[PATH_LEN];
    join_key(vision_path, path, "vision");
    if (!(get_count(ctx, vision, vision_path, "close", &part->vision_close, false)
          && get_count(ctx, vision, vision_path, "medium", &part->vision_medium, false)
          && get_count(ctx, vision, vision_path, "far", &part->vision_far, false)))
        return false;
    if (part->vision_close > part->vision_medium || part->vision_medium > part->vision_far)
        return fail(ctx, vision_path, "bands must satisfy close <= medium <= far");
    return true;
}

static bool read_spawning(Context *ctx, const cJSON *obj, const char *path, const GameData *data,
                          Part *part)
{
    const cJSON *spawning;
    part->spawn_plan = -1;
    if (!get_object(ctx, obj, path, "spawning", &spawning, false)) return false;
    if (!spawning) return true;
    char spawning_path[PATH_LEN], plan_path[PATH_LEN], plan_id[ID_LEN];
    join_key(spawning_path, path, "spawning");
    join_key(plan_path, spawning_path, "plan");
    if (!get_string(ctx, spawning, spawning_path, "plan", plan_id, sizeof plan_id, true)) return false;
    part->spawn_plan = find_body_plan(data, plan_id);
    if (part->spawn_plan < 0) return fail(ctx, plan_path, "unknown body plan \"%s\"", plan_id);
    if (!get_int(ctx, spawning, spawning_path, "cap", &part->spawn_cap, true)) return false;
    char cap_path[PATH_LEN];
    join_key(cap_path, spawning_path, "cap");
    return part->spawn_cap >= 1 || fail(ctx, cap_path, "must be at least 1");
}

static bool read_part_kind(Context *ctx, const cJSON *obj, const char *path, Part *part)
{
    char kind[ID_LEN], kind_path[PATH_LEN];
    if (!get_string(ctx, obj, path, "kind", kind, sizeof kind, true)) return false;
    join_key(kind_path, path, "kind");
    if (strcmp(kind, "internal") == 0) part->internal = true;
    else if (strcmp(kind, "external") != 0) return fail(ctx, kind_path, "expected \"external\" or \"internal\"");
    return true;
}

static bool read_part_stats(Context *ctx, const cJSON *obj, const char *path, Part *part)
{
    if (!get_int(ctx, obj, path, "integrity", &part->integrity, true)) return false;
    char integrity_path[PATH_LEN];
    join_key(integrity_path, path, "integrity");
    if (part->integrity <= 0) return fail(ctx, integrity_path, "must be positive");
    return get_count(ctx, obj, path, "vitality", &part->vitality, false)
        && get_count(ctx, obj, path, "initiative", &part->initiative, false)
        && get_count(ctx, obj, path, "move_speed", &part->move_speed_px_per_second, false);
}

static bool read_part(Context *ctx, const cJSON *obj, const char *path, const GameData *data, Part *part)
{
    return get_string(ctx, obj, path, "id", part->id, sizeof part->id, true)
        && get_string(ctx, obj, path, "name", part->name, sizeof part->name, true)
        && read_part_kind(ctx, obj, path, part)
        && get_bool(ctx, obj, path, "deity_only", &part->deity_only)
        && get_bool(ctx, obj, path, "minion_only", &part->minion_only)
        && read_part_stats(ctx, obj, path, part)
        && get_string(ctx, obj, path, "sprite", part->sprite, sizeof part->sprite, true)
        && read_abilities(ctx, obj, path, part)
        && read_resistances(ctx, obj, path, part)
        && read_vision(ctx, obj, path, part)
        && read_spawning(ctx, obj, path, data, part);
}

static bool parts_from_root(Context *ctx, const cJSON *root, GameData *data)
{
    const cJSON *list;
    if (!get_array(ctx, root, "", "parts", &list, true)) return false;
    static Part parts[MAX_PARTS];
    int count = 0;
    const cJSON *item;
    cJSON_ArrayForEach(item, list) {
        char path[PATH_LEN];
        join_index(path, "parts", count);
        if (count == MAX_PARTS) return fail(ctx, "parts", "more than %d parts", MAX_PARTS);
        if (!cJSON_IsObject(item)) return fail(ctx, path, "expected an object");
        Part *part = &parts[count];
        memset(part, 0, sizeof *part);
        if (!read_part(ctx, item, path, data, part)) return false;
        for (int i = 0; i < count; i++)
            if (strcmp(parts[i].id, part->id) == 0) {
                char id_path[PATH_LEN];
                join_key(id_path, path, "id");
                return fail(ctx, id_path, "duplicate part \"%s\"", part->id);
            }
        count++;
    }
    if (count == 0) return fail(ctx, "parts", "no parts");
    memcpy(data->parts, parts, sizeof(Part) * (size_t)count);
    data->part_count = count;
    return resolve_body_plans(ctx, data);
}

static bool read_arena_object(Context *ctx, const cJSON *obj, const char *path, ArenaObjectDef *def)
{
    return get_string(ctx, obj, path, "id", def->id, sizeof def->id, true)
        && get_string(ctx, obj, path, "name", def->name, sizeof def->name, true)
        && get_string(ctx, obj, path, "sprite", def->sprite, sizeof def->sprite, true)
        && get_element(ctx, obj, path, "element", &def->element, false)
        && get_count(ctx, obj, path, "damage", &def->damage, false)
        && get_count(ctx, obj, path, "heal", &def->heal, false)
        && get_count(ctx, obj, path, "radius", &def->radius, false)
        && get_count(ctx, obj, path, "slow_percent", &def->slow_percent, false)
        && get_count(ctx, obj, path, "effect_ticks", &def->effect_ticks, false)
        && get_count(ctx, obj, path, "damage_bonus_percent", &def->damage_bonus_percent, false)
        && get_count(ctx, obj, path, "respawn_ticks", &def->respawn_ticks, true);
}

static bool read_arena_object_list(Context *ctx, const cJSON *root, const char *key,
                                   ArenaObjectDef *out, int *out_count)
{
    const cJSON *list;
    if (!get_array(ctx, root, "", key, &list, true)) return false;
    int count = 0;
    const cJSON *item;
    cJSON_ArrayForEach(item, list) {
        char path[PATH_LEN];
        join_index(path, key, count);
        if (count == MAX_ARENA_OBJECTS) return fail(ctx, key, "more than %d objects", MAX_ARENA_OBJECTS);
        if (!cJSON_IsObject(item)) return fail(ctx, path, "expected an object");
        memset(&out[count], 0, sizeof out[count]);
        if (!read_arena_object(ctx, item, path, &out[count])) return false;
        for (int i = 0; i < count; i++)
            if (strcmp(out[i].id, out[count].id) == 0) {
                char id_path[PATH_LEN];
                join_key(id_path, path, "id");
                return fail(ctx, id_path, "duplicate id \"%s\"", out[count].id);
            }
        count++;
    }
    *out_count = count;
    return true;
}

static bool arena_objects_from_root(Context *ctx, const cJSON *root, GameData *data)
{
    return read_arena_object_list(ctx, root, "pickups", data->pickups, &data->pickup_count)
        && read_arena_object_list(ctx, root, "traps", data->traps, &data->trap_count);
}

static bool power_from_root(Context *ctx, const cJSON *root, GameData *data)
{
    PowerWeights *w = &data->power;
    return get_int(ctx, root, "", "integrity", &w->integrity, true)
        && get_int(ctx, root, "", "vitality", &w->vitality, true)
        && get_int(ctx, root, "", "damage", &w->damage, true)
        && get_int(ctx, root, "", "range", &w->range, true)
        && get_int(ctx, root, "", "resistance", &w->resistance, true)
        && get_int(ctx, root, "", "initiative", &w->initiative, true)
        && get_int(ctx, root, "", "vision", &w->vision, true)
        && get_int(ctx, root, "", "move_speed", &w->move_speed, true)
        && get_int(ctx, root, "", "spawning", &w->spawning, true);
}

static bool read_deity(Context *ctx, const cJSON *obj, const char *path, const GameData *data,
                       Deity *deity)
{
    const cJSON *body;
    char body_path[PATH_LEN];
    join_key(body_path, path, "body");
    return get_string(ctx, obj, path, "name", deity->name, sizeof deity->name, true)
        && get_string(ctx, obj, path, "epithet", deity->epithet, sizeof deity->epithet, false)
        && get_object(ctx, obj, path, "body", &body, true)
        && read_body(ctx, body, body_path, "parts", data, read_deity_slot, &deity->body);
}

static bool pantheon_from_root(Context *ctx, const cJSON *root, GameData *data)
{
    if (data->pantheon_count == MAX_PANTHEONS) return fail(ctx, "", "more than %d pantheons", MAX_PANTHEONS);
    static Pantheon pantheon;
    memset(&pantheon, 0, sizeof pantheon);
    const cJSON *list;
    if (!get_string(ctx, root, "", "name", pantheon.name, sizeof pantheon.name, true)) return false;
    if (!get_array(ctx, root, "", "deities", &list, true)) return false;
    if (cJSON_GetArraySize(list) != DEITIES_PER_PANTHEON)
        return fail(ctx, "deities", "expected %d deities, found %d", DEITIES_PER_PANTHEON, cJSON_GetArraySize(list));
    int index = 0;
    const cJSON *item;
    cJSON_ArrayForEach(item, list) {
        char path[PATH_LEN];
        join_index(path, "deities", index);
        if (!cJSON_IsObject(item)) return fail(ctx, path, "expected an object");
        if (!read_deity(ctx, item, path, data, &pantheon.deities[index])) return false;
        index++;
    }
    for (int i = 0; i < data->pantheon_count; i++)
        if (strcmp(data->pantheons[i].name, pantheon.name) == 0)
            return fail(ctx, "name", "duplicate pantheon \"%s\"", pantheon.name);
    data->pantheons[data->pantheon_count++] = pantheon;
    return true;
}

bool load_elements(GameData *data, const char *path, char *err, size_t err_size)
{
    return load_file(data, path, err, err_size, elements_from_root);
}

bool load_body_plans(GameData *data, const char *path, char *err, size_t err_size)
{
    return load_file(data, path, err, err_size, body_plans_from_root);
}

bool load_parts(GameData *data, const char *path, char *err, size_t err_size)
{
    return load_file(data, path, err, err_size, parts_from_root);
}

bool load_arena_objects(GameData *data, const char *path, char *err, size_t err_size)
{
    return load_file(data, path, err, err_size, arena_objects_from_root);
}

bool load_power(GameData *data, const char *path, char *err, size_t err_size)
{
    return load_file(data, path, err, err_size, power_from_root);
}

bool load_pantheon(GameData *data, const char *path, char *err, size_t err_size)
{
    return load_file(data, path, err, err_size, pantheon_from_root);
}

static int compare_names(const void *a, const void *b)
{
    return strcmp((const char *)a, (const char *)b);
}

static bool append_pantheons(GameData *data, const char *folder, char *err, size_t err_size)
{
    DIR *handle = opendir(folder);
    if (!handle) {
        snprintf(err, err_size, "%s: cannot open directory", folder);
        return false;
    }
    static char names[MAX_PANTHEONS][NAME_LEN];
    int count = 0;
    struct dirent *entry;
    while ((entry = readdir(handle)) != NULL) {
        size_t length = strlen(entry->d_name);
        if (length < 6 || strcmp(entry->d_name + length - 5, ".json") != 0) continue;
        if (count == MAX_PANTHEONS || length >= NAME_LEN) {
            snprintf(err, err_size, "%s/%s: too many pantheon files or file name too long", folder, entry->d_name);
            closedir(handle);
            return false;
        }
        strcpy(names[count++], entry->d_name);
    }
    closedir(handle);
    if (count == 0) {
        snprintf(err, err_size, "%s: no pantheons found", folder);
        return false;
    }
    qsort(names, (size_t)count, NAME_LEN, compare_names);
    for (int i = 0; i < count; i++) {
        char path[PATH_LEN * 2];
        snprintf(path, sizeof path, "%s/%s", folder, names[i]);
        if (!load_pantheon(data, path, err, err_size)) return false;
    }
    return true;
}

bool load_pantheons(GameData *data, const char *folder, char *err, size_t err_size)
{
    static GameData scratch;
    scratch = *data;
    bool ok = append_pantheons(&scratch, folder, err, err_size);
    if (ok) *data = scratch;
    return ok;
}

bool load_game_data(GameData *data, const char *dir, char *err, size_t err_size)
{
    char path[PATH_LEN];
    static GameData fresh;
    memset(&fresh, 0, sizeof fresh);
#define LOAD(loader, file) \
    (snprintf(path, sizeof path, "%s/" file, dir), loader(&fresh, path, err, err_size))
    bool ok = LOAD(load_elements, "elements.json")
        && LOAD(load_body_plans, "bodyplans.json")
        && LOAD(load_parts, "parts.json")
        && LOAD(load_arena_objects, "arena_objects.json")
        && LOAD(load_power, "power.json")
        && (snprintf(path, sizeof path, "%s/pantheons", dir), append_pantheons(&fresh, path, err, err_size));
#undef LOAD
    if (ok) *data = fresh;
    return ok;
}
