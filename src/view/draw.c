#include "draw.h"

#include <math.h>
#include <stdio.h>

#define PATH_SIZE 256
#define FLOOR_COLOUR ((Color){20, 18, 16, 255})

static Texture2D load_sheet(const char *folder, const char *id)
{
    char path[PATH_SIZE];
    snprintf(path, sizeof path, "assets/%s/%s.png", folder, id);
    return LoadTexture(path);
}

void sheets_load(Sheets *sheets, const GameData *data)
{
    for (int p = 0; p < data->part_count; p++) sheets->parts[p] = load_sheet("parts", data->parts[p].sprite);
    for (int i = 0; i < data->pickup_count; i++) sheets->pickups[i] = load_sheet("pickups", data->pickups[i].sprite);
    for (int i = 0; i < data->trap_count; i++) sheets->traps[i] = load_sheet("traps", data->traps[i].sprite);
    for (int e = 0; e < ELEMENT_COUNT; e++) sheets->effects[e] = load_sheet("effects", element_id((Element)e));
    sheets->floor = load_sheet("tiles", "floor");
    sheets->rock = load_sheet("tiles", "rock");
    sheets->pillar = load_sheet("tiles", "pillar");
}

static void unload_all(Texture2D *textures, int count)
{
    for (int i = 0; i < count; i++) UnloadTexture(textures[i]);
}

void sheets_unload(Sheets *sheets)
{
    unload_all(sheets->parts, MAX_PARTS);
    unload_all(sheets->pickups, MAX_ARENA_OBJECTS);
    unload_all(sheets->traps, MAX_ARENA_OBJECTS);
    unload_all(sheets->effects, ELEMENT_COUNT);
    UnloadTexture(sheets->floor);
    UnloadTexture(sheets->rock);
    UnloadTexture(sheets->pillar);
}

static void draw_at_pivot(Texture2D sheet, const Atlas *atlas, int frame, float x, float y, bool flip, Color tint)
{
    float width = (float)atlas->frame_w;
    Rectangle source = {(float)(frame * atlas->frame_w), 0, flip ? -width : width, (float)atlas->frame_h};
    float left = flip ? x - (width - (float)atlas->pivot_x) : x - (float)atlas->pivot_x;
    Rectangle dest = {left, y - (float)atlas->pivot_y, width, (float)atlas->frame_h};
    DrawTexturePro(sheet, source, dest, (Vector2){0, 0}, 0, tint);
}

static void draw_floor(const Atlases *atlases, const Sheets *sheets)
{
    float size = (float)atlases->floor.frame_w;
    for (int row = 0; row < ARENA_HEIGHT_TILES; row++)
        for (int column = 0; column < ARENA_WIDTH_TILES; column++) {
            int frame = atlas_frame(&atlases->floor, FRAMES_VARIANTS, tile_hash(column, row) % 4);
            Rectangle source = {size * (float)frame, 0, size, size};
            DrawTextureRec(sheets->floor, source, (Vector2){(float)(column * TILE_SIZE), (float)(row * TILE_SIZE)}, WHITE);
        }
}

static void draw_obstacle(const Drawable *drawable, const Atlases *atlases, const Sheets *sheets)
{
    int column = drawable->index % ARENA_WIDTH_TILES, row = drawable->index / ARENA_WIDTH_TILES;
    bool pillar = tile_hash(column, row) % 3 == 0;
    const Atlas *atlas = pillar ? &atlases->pillar : &atlases->rock;
    draw_at_pivot(pillar ? sheets->pillar : sheets->rock, atlas, 0, drawable->x, drawable->y, false, WHITE);
}

static void draw_trap(const Drawable *drawable, const Battle *b, const Scene *scene, const Atlases *atlases,
                      const Sheets *sheets, int tick)
{
    int def = b->traps[drawable->index].def;
    bool acting = tick - scene->trap_since[drawable->index] < TRAP_ACT_TICKS;
    int frame = atlas_frame(&atlases->traps[def], acting ? FRAMES_ACT : FRAMES_IDLE, 0);
    draw_at_pivot(sheets->traps[def], &atlases->traps[def], frame, drawable->x, drawable->y, false, WHITE);
}

static void draw_pickup(const Drawable *drawable, const Battle *b, const Atlases *atlases, const Sheets *sheets, int tick)
{
    int def = b->pickups[drawable->index].def;
    int frame = atlas_frame(&atlases->pickups[def], FRAMES_IDLE, tick / IDLE_FRAME_TICKS);
    draw_at_pivot(sheets->pickups[def], &atlases->pickups[def], frame, drawable->x, drawable->y, false, WHITE);
}

static void draw_external_parts(const Battle *b, const GameData *data, const Scene *scene, const Atlases *atlases,
                                const Sheets *sheets, int c, float origin_x, float origin_y, int tick)
{
    const Combatant *who = &b->combatants[c];
    bool flip = scene->facing_left[c];
    bool moving = c < scene->prev_count && (who->x_millipx != scene->prev_x[c] || who->y_millipx != scene->prev_y[c]);
    for (int s = 0; s < who->body.slot_count; s++) {
        const BodySlot *slot = &who->body.slots[s];
        if (slot->internal) continue;
        const Atlas *atlas = &atlases->parts[slot->part];
        int frame = part_frame(atlas, who, s, &data->parts[slot->part], moving, scene->act_since[c][s],
                               scene->hit_since[c][s], c, tick);
        draw_at_pivot(sheets->parts[slot->part], atlas, frame, origin_x + (float)(flip ? -slot->anchor_x : slot->anchor_x),
                      origin_y + (float)slot->anchor_y, flip, WHITE);
    }
}

static void draw_combatant(const Drawable *drawable, const Battle *b, const GameData *data, const Scene *scene,
                           const Atlases *atlases, const Sheets *sheets, int tick)
{
    const Body *body = &b->combatants[drawable->index].body;
    float origin_y = floorf(drawable->y - (float)body_feet_offset(body, atlases));
    draw_external_parts(b, data, scene, atlases, sheets, drawable->index, floorf(drawable->x), origin_y, tick);
}

static void draw_drawables(const Battle *b, const GameData *data, const Scene *scene, const Atlases *atlases,
                           const Sheets *sheets, int tick)
{
    static Drawable drawables[MAX_DRAWABLES];
    int count = scene_collect(scene, b, drawables);
    for (int i = 0; i < count; i++) {
        const Drawable *drawable = &drawables[i];
        switch (drawable->kind) {
        case DRAW_OBSTACLE: draw_obstacle(drawable, atlases, sheets); break;
        case DRAW_PICKUP: draw_pickup(drawable, b, atlases, sheets, tick); break;
        case DRAW_TRAP: draw_trap(drawable, b, scene, atlases, sheets, tick); break;
        case DRAW_REMAINS: break;
        case DRAW_COMBATANT: draw_combatant(drawable, b, data, scene, atlases, sheets, tick); break;
        }
    }
}

static void draw_effects(const Scene *scene, const Atlases *atlases, const Sheets *sheets, int tick)
{
    BeginBlendMode(BLEND_ADDITIVE);
    for (int e = 0; e < MAX_EFFECTS; e++) {
        const Effect *effect = &scene->effects[e];
        int n = effect_frame(effect, tick);
        if (n < 0) continue;
        const Atlas *atlas = &atlases->effects[effect->element];
        draw_at_pivot(sheets->effects[effect->element], atlas, atlas_frame(atlas, FRAMES_PLAY, n), floorf(effect->x),
                      floorf(effect->y), false, WHITE);
    }
    EndBlendMode();
}

void draw_frame_contents(const Battle *b, const GameData *data, const Scene *scene, const Atlases *atlases,
                         const Sheets *sheets, const Viewport *view, const Hud *hud)
{
    (void)hud;
    int tick = (int)scene_time(scene, b);
    ClearBackground(FLOOR_COLOUR);
    Camera2D camera = {{(float)view->width / 2, (float)view->height / 2}, {view->centre_x, view->centre_y}, 0, (float)view->zoom};
    BeginMode2D(camera);
    draw_floor(atlases, sheets);
    draw_drawables(b, data, scene, atlases, sheets, tick);
    draw_effects(scene, atlases, sheets, tick);
    EndMode2D();
}
