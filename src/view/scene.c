#include "scene.h"

#include <math.h>

#include <stdint.h>
#include <stdlib.h>

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
    v->centre_x = clamp_axis(v->centre_x, (float)(v->width - v->inset_right), v->zoom, ARENA_WIDTH_PX);
    v->centre_y = clamp_axis(v->centre_y, (float)(v->height - HUD_HEIGHT), v->zoom, ARENA_HEIGHT_PX);
}

void viewport_init(Viewport *v, int width, int height)
{
    *v = (Viewport){ARENA_WIDTH_PX / 2.0f, ARENA_HEIGHT_PX / 2.0f, ZOOM_DEFAULT, width, height, 0};
    viewport_clamp(v);
}

void viewport_set_inset(Viewport *v, int inset_right)
{
    v->inset_right = inset_right;
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
    v->zoom = zoom < ZOOM_MIN ? ZOOM_MIN : zoom > ZOOM_MAX ? ZOOM_MAX
                                                           : zoom;
    viewport_clamp(v);
}

float viewport_screen_x(const Viewport *v, float world_x)
{
    return (float)(v->width - v->inset_right) / 2 + (world_x - v->centre_x) * (float)v->zoom;
}

float viewport_screen_y(const Viewport *v, float world_y)
{
    return (float)HUD_HEIGHT + (float)(v->height - HUD_HEIGHT) / 2 + (world_y - v->centre_y) * (float)v->zoom;
}

float viewport_world_x(const Viewport *v, float screen_x)
{
    return v->centre_x + (screen_x - (float)(v->width - v->inset_right) / 2) / (float)v->zoom;
}

float viewport_world_y(const Viewport *v, float screen_y)
{
    return v->centre_y + (screen_y - (float)HUD_HEIGHT - (float)(v->height - HUD_HEIGHT) / 2) / (float)v->zoom;
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
    if (element < 0 || element >= ELEMENT_COUNT) return;
    s->effects[s->effect_next++ % MAX_EFFECTS] = (Effect){x, y, element, tick};
}

static void push_effect_at_combatant(Scene *s, const Battle *b, int c, int element, int tick)
{
    if (c < 0 || c >= b->combatant_count) return;
    const Combatant *who = &b->combatants[c];
    push_effect(s, (float)who->x_millipx / MILLIPX_PER_PX, (float)who->y_millipx / MILLIPX_PER_PX - EFFECT_LIFT_PX, element, tick);
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

static int compare_drawables(const void *left, const void *right)
{
    const Drawable *a = left, *b = right;
    if (a->y != b->y) return a->y < b->y ? -1 : 1;
    if (a->kind != b->kind) return a->kind < b->kind ? -1 : 1;
    return a->index - b->index;
}

static void collect_floor_objects(const FloorObject *objects, int count, DrawableKind kind, Drawable *out, int *n)
{
    for (int i = 0; i < count; i++) {
        if (objects[i].respawn_in > 0) continue;
        out[(*n)++] = (Drawable){kind, i, (float)objects[i].x_millipx / MILLIPX_PER_PX, (float)objects[i].y_millipx / MILLIPX_PER_PX};
    }
}

int scene_collect(const Scene *s, const Battle *b, Drawable *out)
{
    int n = 0;
    for (int row = 0; row < ARENA_HEIGHT_TILES; row++)
        for (int column = 0; column < ARENA_WIDTH_TILES; column++)
            if (b->obstacle[row][column])
                out[n++] = (Drawable){DRAW_OBSTACLE, row * ARENA_WIDTH_TILES + column, (float)(column * TILE_SIZE + TILE_SIZE / 2),
                                      (float)(row * TILE_SIZE + TILE_SIZE)};
    collect_floor_objects(b->pickups, b->pickup_count, DRAW_PICKUP, out, &n);
    for (int t = 0; t < b->trap_count; t++)
        out[n++] = (Drawable){DRAW_TRAP, t, (float)b->traps[t].x_millipx / MILLIPX_PER_PX, (float)b->traps[t].y_millipx / MILLIPX_PER_PX};
    for (int r = 0; r < b->remains_count; r++)
        out[n++] = (Drawable){DRAW_REMAINS, r, (float)b->remains[r].x_millipx / MILLIPX_PER_PX, (float)b->remains[r].y_millipx / MILLIPX_PER_PX};
    for (int c = 0; c < b->combatant_count; c++) {
        if (!b->combatants[c].alive) continue;
        out[n] = (Drawable){DRAW_COMBATANT, c, 0, 0};
        scene_position(s, b, c, &out[n].x, &out[n].y);
        n++;
    }
    qsort(out, (size_t)n, sizeof *out, compare_drawables);
    return n;
}

int tile_hash(int column, int row)
{
    return (column * 7 + row * 13) & 0xff;
}

int body_feet_offset(const Body *body, const Atlases *atlases)
{
    int feet = 0;
    for (int s = 0; s < body->slot_count; s++) {
        const BodySlot *slot = &body->slots[s];
        if (slot->internal) continue;
        const Atlas *atlas = &atlases->parts[slot->part];
        int bottom = slot->anchor_y - atlas->pivot_y + atlas->frame_h;
        if (bottom > feet) feet = bottom;
    }
    return feet ? feet : 32;
}

int scene_pick(const Scene *s, const Battle *b, const Atlases *atlases, float world_x, float world_y)
{
    int picked = -1;
    float picked_y = 0;
    for (int c = 0; c < b->combatant_count; c++) {
        if (!b->combatants[c].alive) continue;
        float x, y;
        scene_position(s, b, c, &x, &y);
        float top = y - (float)body_feet_offset(&b->combatants[c].body, atlases);
        bool inside = fabsf(world_x - x) <= PICK_HALF_WIDTH_PX && world_y >= top && world_y <= y;
        if (inside && (picked < 0 || y > picked_y)) picked = c, picked_y = y;
    }
    return picked;
}

void remains_offset(int remains_index, int part_index, int *dx, int *dy)
{
    uint32_t h = ((uint32_t)remains_index * 2654435761u) ^ ((uint32_t)part_index * 40503u + 12345u);
    h ^= h >> 15;
    h *= 2246822519u;
    h ^= h >> 13;
    *dx = (int)(h % (2 * REMAINS_SPREAD_PX + 1)) - REMAINS_SPREAD_PX;
    *dy = (int)((h >> 8) % 11) - 4;
}
