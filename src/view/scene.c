#include "scene.h"

#define ARENA_WIDTH_PX (ARENA_WIDTH_TILES * TILE_SIZE)
#define ARENA_HEIGHT_PX (ARENA_HEIGHT_TILES * TILE_SIZE)

static float clamp_axis(float centre, float screen_size, int zoom, float arena_size)
{
    float visible = screen_size / (float)zoom;
    if (visible >= arena_size) return arena_size / 2;
    if (centre < visible / 2) return visible / 2;
    if (centre > arena_size - visible / 2) return arena_size - visible / 2;
    return centre;
}

void viewport_clamp(Viewport *v)
{
    v->centre_x = clamp_axis(v->centre_x, (float)v->width, v->zoom, ARENA_WIDTH_PX);
    v->centre_y = clamp_axis(v->centre_y, (float)v->height, v->zoom, ARENA_HEIGHT_PX);
}

void viewport_init(Viewport *v, int width, int height)
{
    *v = (Viewport){ARENA_WIDTH_PX / 2.0f, ARENA_HEIGHT_PX / 2.0f, ZOOM_DEFAULT, width, height};
    viewport_clamp(v);
}

void viewport_pan(Viewport *v, float dx_screen_px, float dy_screen_px)
{
    v->centre_x += dx_screen_px / (float)v->zoom;
    v->centre_y += dy_screen_px / (float)v->zoom;
    viewport_clamp(v);
}

void viewport_zoom(Viewport *v, int step)
{
    int zoom = v->zoom + step;
    v->zoom = zoom < ZOOM_MIN ? ZOOM_MIN : zoom > ZOOM_MAX ? ZOOM_MAX : zoom;
    viewport_clamp(v);
}

float viewport_screen_x(const Viewport *v, float world_x)
{
    return (float)v->width / 2 + (world_x - v->centre_x) * (float)v->zoom;
}

float viewport_screen_y(const Viewport *v, float world_y)
{
    return (float)v->height / 2 + (world_y - v->centre_y) * (float)v->zoom;
}

static void snapshot_positions(Scene *s, const Battle *b)
{
    for (int c = 0; c < b->combatant_count; c++) {
        s->prev_x[c] = b->combatants[c].x_millipx;
        s->prev_y[c] = b->combatants[c].y_millipx;
    }
    s->prev_count = b->combatant_count;
}

void scene_reset(Scene *s, const Battle *b)
{
    bool xray = s->xray;
    *s = (Scene){0};
    s->xray = xray;
    for (int c = 0; c < MAX_COMBATANTS; c++) {
        for (int p = 0; p < MAX_SLOTS; p++) s->act_since[c][p] = s->hit_since[c][p] = NEVER;
        s->facing_left[c] = c < b->combatant_count && b->combatants[c].side == SIDE_HORDE;
    }
    for (int t = 0; t < MAX_ARENA_OBJECTS; t++) s->trap_since[t] = NEVER;
    for (int e = 0; e < MAX_EFFECTS; e++) s->effects[e].since_tick = NEVER;
    s->seen_events = b->log_count;
    snapshot_positions(s, b);
}

void scene_before_step(Scene *s, const Battle *b)
{
    snapshot_positions(s, b);
}

void scene_after_step(Scene *s, const Battle *b)
{
    for (int c = 0; c < b->combatant_count; c++) {
        const Combatant *who = &b->combatants[c];
        if (c >= s->prev_count) s->facing_left[c] = who->side == SIDE_HORDE;
        else if (who->x_millipx < s->prev_x[c]) s->facing_left[c] = true;
        else if (who->x_millipx > s->prev_x[c]) s->facing_left[c] = false;
    }
    for (; s->seen_events < b->log_count; s->seen_events++) scene_apply_event(s, b, &b->log[s->seen_events]);
}

float scene_time(const Scene *s, const Battle *b)
{
    return (float)b->tick + s->alpha;
}

void scene_position(const Scene *s, const Battle *b, int c, float *x_px, float *y_px)
{
    float x = (float)b->combatants[c].x_millipx;
    float y = (float)b->combatants[c].y_millipx;
    if (c < s->prev_count) {
        x = (float)s->prev_x[c] + (x - (float)s->prev_x[c]) * s->alpha;
        y = (float)s->prev_y[c] + (y - (float)s->prev_y[c]) * s->alpha;
    }
    *x_px = x / MILLIPX_PER_PX;
    *y_px = y / MILLIPX_PER_PX;
}

static void push_effect(Scene *s, float x, float y, int element, int tick)
{
    s->effects[s->effect_next++ % MAX_EFFECTS] = (Effect){x, y, element, tick};
}

static void push_effect_at_combatant(Scene *s, const Battle *b, int c, int element, int tick)
{
    if (c < 0 || c >= b->combatant_count) return;
    float x, y;
    scene_position(s, b, c, &x, &y);
    push_effect(s, x, y - EFFECT_LIFT_PX, element, tick);
}

static void mark_act(Scene *s, const Battle *b, int actor, int part, int tick)
{
    if (actor >= 0 && actor < b->combatant_count && part >= 0 && part < MAX_SLOTS) s->act_since[actor][part] = tick;
}

static void apply_hit(Scene *s, const Battle *b, const Event *e)
{
    mark_act(s, b, e->actor, e->actor_part, e->tick);
    if (e->target >= 0 && e->target < b->combatant_count && e->target_part >= 0 && e->target_part < MAX_SLOTS) {
        s->hit_since[e->target][e->target_part] = e->tick;
        s->hit_element[e->target][e->target_part] = e->element;
    }
    push_effect_at_combatant(s, b, e->target, e->element, e->tick);
}

void scene_apply_event(Scene *s, const Battle *b, const Event *e)
{
    switch (e->kind) {
    case EVENT_HIT:
        apply_hit(s, b, e);
        break;
    case EVENT_EFFECT:
        mark_act(s, b, e->actor, e->actor_part, e->tick);
        push_effect_at_combatant(s, b, e->target, e->element, e->tick);
        break;
    case EVENT_SPAWN:
        mark_act(s, b, e->actor, e->actor_part, e->tick);
        push_effect_at_combatant(s, b, e->target, ELEMENT_KINETIC, e->tick);
        break;
    case EVENT_TRAP:
        if (e->detail >= 0 && e->detail < b->trap_count) {
            const FloorObject *trap = &b->traps[e->detail];
            s->trap_since[e->detail] = e->tick;
            push_effect(s, (float)trap->x_millipx / MILLIPX_PER_PX, (float)trap->y_millipx / MILLIPX_PER_PX, e->element, e->tick);
        }
        break;
    default:
        break;
    }
}

int part_frame(const Atlas *atlas, const Combatant *who, int slot, const Part *part, bool moving, int act_since,
               int hit_since, int idle_phase, int tick)
{
    if (who->parts[slot].integrity <= 0) return atlas_frame(atlas, FRAMES_BROKEN, 0);
    if (tick - hit_since < HURT_TICKS) return atlas_frame(atlas, FRAMES_HURT, 0);
    if (tick - act_since < ACT_TICKS) return atlas_frame(atlas, FRAMES_ACT, (tick - act_since) * 3 / ACT_TICKS);
    if (who->parts[slot].order.kind == ORDER_BLOCK) return atlas_frame(atlas, FRAMES_ACT, 1);
    if (moving && part->move_speed_px_per_second > 0) return atlas_frame(atlas, FRAMES_ACT, (tick / WALK_FRAME_TICKS) % 3);
    return atlas_frame(atlas, FRAMES_IDLE, tick / IDLE_FRAME_TICKS + idle_phase);
}

int effect_frame(const Effect *effect, int tick)
{
    int age = tick - effect->since_tick;
    if (age < 0 || age >= EFFECT_TICKS) return -1;
    return age * 5 / EFFECT_TICKS;
}
