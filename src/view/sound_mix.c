#include "sound_mix.h"

#include <math.h>

static float clampf(float value, float low, float high)
{
    return value < low ? low : value > high ? high : value;
}

static float outside_distance(float offset, float half_extent)
{
    float outside = fabsf(offset) - half_extent;
    return outside > 0 ? outside : 0;
}

Placement sound_place(float world_x, float world_y, const Viewport *view)
{
    float half_width = (float)view->width / view->zoom / 2;
    float half_height = (float)view->height / view->zoom / 2;
    float dx = world_x - view->centre_x;
    float away = hypotf(outside_distance(dx, half_width), outside_distance(world_y - view->centre_y, half_height));
    return (Placement){clampf(1 - away / half_width, 0, 1), clampf(dx / half_width, -1, 1)};
}

float sound_gain(SoundCategory category)
{
    switch (category) {
    case SOUND_STEP: return GAIN_STEP;
    case SOUND_ABILITY: return GAIN_ABILITY;
    case SOUND_EFFECT: return GAIN_EFFECT;
    default: return GAIN_CUE;
    }
}

float sound_cue_volume(const SoundCue *cue, const Viewport *view, float *pan)
{
    Placement placement = sound_place(cue->x, cue->y, view);
    *pan = placement.pan;
    return placement.volume * sound_gain(cue->category);
}

float raylib_pan(float pan)
{
    return 0.5f - pan / 2;
}

static void snapshot(SoundTracker *t, const Battle *b)
{
    for (int c = 0; c < b->combatant_count; c++) {
        t->prev_x[c] = b->combatants[c].x_millipx;
        t->prev_y[c] = b->combatants[c].y_millipx;
    }
    t->prev_count = b->combatant_count;
}

void sound_tracker_reset(SoundTracker *t, const Battle *b)
{
    *t = (SoundTracker){.hit_tick = -1};
    t->seen_events = b->log_count;
    t->last_tick = b->tick;
    snapshot(t, b);
}

static bool in_range(int index, int count)
{
    return index >= 0 && index < count;
}

static float px(int millipx)
{
    return (float)millipx / MILLIPX_PER_PX;
}

static bool push(SoundCue *out, int *n, int max, const char *id, float x, float y, SoundCategory category)
{
    if (id[0] == '\0' || *n >= max) return false;
    out[(*n)++] = (SoundCue){id, x, y, category};
    return true;
}

static void push_at_combatant(SoundCue *out, int *n, int max, const char *id, const Battle *b, int c, SoundCategory category)
{
    if (!in_range(c, b->combatant_count)) return;
    push(out, n, max, id, px(b->combatants[c].x_millipx), px(b->combatants[c].y_millipx), category);
}

static const Part *slot_part(const Battle *b, const GameData *data, int c, int slot)
{
    if (!in_range(c, b->combatant_count) || !in_range(slot, b->combatants[c].body.slot_count)) return NULL;
    return &data->parts[b->combatants[c].body.slots[slot].part];
}

static const Ability *find_ability(const Part *part, OrderKind kind, int element)
{
    for (int a = 0; part && a < part->ability_count; a++) {
        const Ability *ability = &part->abilities[a];
        bool same_kind = ability->order == kind || (kind == ORDER_STRIKE && ability->order == ORDER_CAST);
        if (same_kind && (element < 0 || (int)ability->element == element)) return ability;
    }
    return NULL;
}

static bool first_hit_of_action(SoundTracker *t, const Event *e)
{
    bool repeat = t->hit_tick == e->tick && t->hit_actor == e->actor && t->hit_actor_part == e->actor_part;
    t->hit_tick = e->tick;
    t->hit_actor = e->actor;
    t->hit_actor_part = e->actor_part;
    return !repeat;
}

static void map_hit(SoundTracker *t, const Battle *b, const GameData *data, const Event *e, SoundCue *out, int *n, int max)
{
    const Ability *ability = find_ability(slot_part(b, data, e->actor, e->actor_part), ORDER_STRIKE, e->element);
    if (ability && first_hit_of_action(t, e)) push_at_combatant(out, n, max, ability->sound, b, e->actor, SOUND_ABILITY);
    if (in_range(e->element, ELEMENT_COUNT)) push_at_combatant(out, n, max, data->elements[e->element].hit_sound, b, e->target, SOUND_ABILITY);
    if (e->detail <= 0) push_at_combatant(out, n, max, data->cues.part_destroyed, b, e->target, SOUND_CUE);
}

static void map_effect(const Battle *b, const GameData *data, const Event *e, SoundCue *out, int *n, int max)
{
    if (e->detail == EFFECT_HEAL) {
        const Ability *ability = find_ability(slot_part(b, data, e->actor, e->actor_part), ORDER_SUPPORT, -1);
        if (ability) push_at_combatant(out, n, max, ability->sound, b, e->actor, SOUND_ABILITY);
    } else if (in_range(e->element, ELEMENT_COUNT)) {
        push_at_combatant(out, n, max, data->elements[e->element].effect_sound, b, e->target, SOUND_EFFECT);
    }
}

static void map_floor_object(const FloorObject *objects, int count, const ArenaObjectDef *defs, int index, SoundCue *out, int *n, int max)
{
    if (!in_range(index, count)) return;
    const FloorObject *object = &objects[index];
    push(out, n, max, defs[object->def].sound, px(object->x_millipx), px(object->y_millipx), SOUND_CUE);
}

static void map_event(SoundTracker *t, const Battle *b, const GameData *data, const Event *e, SoundCue *out, int *n, int max)
{
    switch (e->kind) {
    case EVENT_HIT:
        map_hit(t, b, data, e, out, n, max);
        break;
    case EVENT_EFFECT:
        map_effect(b, data, e, out, n, max);
        break;
    case EVENT_DEATH:
        push_at_combatant(out, n, max, data->cues.death, b, e->target, SOUND_CUE);
        break;
    case EVENT_SPAWN: {
        const Ability *ability = find_ability(slot_part(b, data, e->actor, e->actor_part), ORDER_SPAWN, -1);
        if (ability) push_at_combatant(out, n, max, ability->sound, b, e->actor, SOUND_ABILITY);
        push_at_combatant(out, n, max, data->cues.spawn_appear, b, e->target, SOUND_CUE);
        break;
    }
    case EVENT_PICKUP:
        if (in_range(e->detail, b->pickup_count)) {
            const FloorObject *object = &b->pickups[e->detail];
            push_at_combatant(out, n, max, data->pickups[object->def].sound, b, e->actor, SOUND_CUE);
        }
        break;
    case EVENT_TRAP:
        map_floor_object(b->traps, b->trap_count, data->traps, e->detail, out, n, max);
        break;
    default:
        break;
    }
}

static const char *step_sound_id(const Battle *b, const GameData *data, int c)
{
    const Combatant *who = &b->combatants[c];
    for (int s = 0; s < who->body.slot_count; s++) {
        const Part *part = &data->parts[who->body.slots[s].part];
        if (who->parts[s].integrity > 0 && part->move_speed_px_per_second > 0) return part->step_sound;
    }
    return "";
}

static void collect_steps(SoundTracker *t, const Battle *b, const GameData *data, int elapsed, SoundCue *out, int *n, int max)
{
    if (elapsed <= 0) return;
    for (int c = 0; c < b->combatant_count && c < t->prev_count; c++) {
        const Combatant *who = &b->combatants[c];
        bool moved = who->alive && (who->x_millipx != t->prev_x[c] || who->y_millipx != t->prev_y[c]);
        if (!moved) {
            t->step_ticks[c] = 0;
            continue;
        }
        t->step_ticks[c] += elapsed;
        if (t->step_ticks[c] < SOUND_STEP_TICKS) continue;
        t->step_ticks[c] %= SOUND_STEP_TICKS;
        push_at_combatant(out, n, max, step_sound_id(b, data, c), b, c, SOUND_STEP);
    }
}

int sound_collect(SoundTracker *t, const Battle *b, const GameData *data, SoundCue *out, int max)
{
    int n = 0;
    for (; t->seen_events < b->log_count; t->seen_events++) map_event(t, b, data, &b->log[t->seen_events], out, &n, max);
    collect_steps(t, b, data, b->tick - t->last_tick, out, &n, max);
    t->last_tick = b->tick;
    snapshot(t, b);
    return n;
}
