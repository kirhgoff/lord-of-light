#ifndef VIEW_SCENE_H
#define VIEW_SCENE_H

#include <stdbool.h>

#include "../sim/battle.h"
#include "../sim/movement.h"
#include "atlas.h"

#define HUD_HEIGHT 48
#define PANEL_WIDTH 360
#define ZOOM_MIN 1
#define ZOOM_MAX 4
#define ZOOM_DEFAULT 1
#define PAN_SCREEN_PX_PER_SECOND 600
#define TICK_SECONDS (1.0f / TICKS_PER_SECOND)
#define IDLE_FRAME_TICKS 10
#define WALK_FRAME_TICKS 3
#define ACT_TICKS 6
#define HURT_TICKS 4
#define FLASH_TICKS 8
#define FLASH_TICKS_RADIANCE 16
#define EFFECT_TICKS 10
#define TRAP_ACT_TICKS 10
#define MAX_EFFECTS 64
#define NEVER (-1000000)
#define REMAINS_SPREAD_PX 12
#define EFFECT_LIFT_PX 32
#define PICK_HALF_WIDTH_PX 16

typedef struct {
    float centre_x;
    float centre_y;
    int zoom;
    int width;
    int height;
    int inset_right;
} Viewport;

void viewport_init(Viewport *v, int width, int height);
void viewport_clamp(Viewport *v);
void viewport_set_inset(Viewport *v, int inset_right);
void viewport_pan(Viewport *v, float dx_screen_px, float dy_screen_px);
void viewport_zoom(Viewport *v, int step);
float viewport_screen_x(const Viewport *v, float world_x);
float viewport_screen_y(const Viewport *v, float world_y);
float viewport_world_x(const Viewport *v, float screen_x);
float viewport_world_y(const Viewport *v, float screen_y);

typedef enum { DRAW_OBSTACLE, DRAW_PICKUP, DRAW_TRAP, DRAW_REMAINS, DRAW_COMBATANT } DrawableKind;

typedef struct {
    DrawableKind kind;
    int index;
    float x;
    float y;
} Drawable;

#define MAX_DRAWABLES (ARENA_WIDTH_TILES * ARENA_HEIGHT_TILES + MAX_COMBATANTS + MAX_REMAINS + 2 * MAX_ARENA_OBJECTS)

typedef struct {
    float x;
    float y;
    int element;
    int since_tick;
} Effect;

typedef struct {
    int prev_x[MAX_COMBATANTS];
    int prev_y[MAX_COMBATANTS];
    int prev_count;
    float alpha;
    bool facing_left[MAX_COMBATANTS];
    int act_since[MAX_COMBATANTS][MAX_SLOTS];
    int hit_since[MAX_COMBATANTS][MAX_SLOTS];
    int hit_element[MAX_COMBATANTS][MAX_SLOTS];
    int trap_since[MAX_ARENA_OBJECTS];
    Effect effects[MAX_EFFECTS];
    int effect_next;
    int seen_events;
    bool xray;
} Scene;

void scene_reset(Scene *s, const Battle *b);
void scene_before_step(Scene *s, const Battle *b);
void scene_after_step(Scene *s, const Battle *b);
float scene_time(const Scene *s, const Battle *b);
void scene_apply_event(Scene *s, const Battle *b, const Event *e);
void scene_position(const Scene *s, const Battle *b, int c, float *x_px, float *y_px);
int part_frame(const Atlas *atlas, const Combatant *who, int slot, const Part *part, bool moving, int act_since,
               int hit_since, int idle_phase, int tick);
int effect_frame(const Effect *effect, int tick);
int scene_collect(const Scene *s, const Battle *b, Drawable *out);
int tile_hash(int column, int row);
int body_feet_offset(const Body *body, const Atlases *atlases);
int scene_pick(const Scene *s, const Battle *b, const Atlases *atlases, float world_x, float world_y);
void remains_offset(int remains_index, int part_index, int *dx, int *dy);

#endif
