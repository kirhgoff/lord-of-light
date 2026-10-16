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
    bool working[MAX_SLOTS];
    body_working_parts(&who->body, who->parts, working);
    for (int s = 0; s < who->body.slot_count; s++) {
        const BodySlot *slot = &who->body.slots[s];
        if (slot->internal) continue;
        const Atlas *atlas = &atlases->parts[slot->part];
        int frame = part_frame(atlas, who, s, &data->parts[slot->part], moving, scene->act_since[c][s],
                               scene->hit_since[c][s], c, tick);
        draw_at_pivot(sheets->parts[slot->part], atlas, frame, origin_x + (float)(flip ? -slot->anchor_x : slot->anchor_x),
                      origin_y + (float)slot->anchor_y, flip, working[s] ? WHITE : GRAY);
    }
}

static float flash_strength(const Scene *scene, int c, int slot, int tick)
{
    int duration = scene->hit_element[c][slot] == ELEMENT_RADIANCE ? FLASH_TICKS_RADIANCE : FLASH_TICKS;
    int age = tick - scene->hit_since[c][slot];
    return age >= 0 && age < duration ? 1.0f - (float)age / (float)duration : 0;
}

static void draw_internal_parts(const Battle *b, const Scene *scene, const Atlases *atlases, const Sheets *sheets, int c,
                                float origin_x, float origin_y, int tick)
{
    const Body *body = &b->combatants[c].body;
    bool flip = scene->facing_left[c];
    for (int s = 0; s < body->slot_count; s++) {
        const BodySlot *slot = &body->slots[s];
        if (!slot->internal) continue;
        const Atlas *atlas = &atlases->parts[slot->part];
        float x = origin_x + (float)(flip ? -slot->anchor_x : slot->anchor_x);
        float y = origin_y + (float)slot->anchor_y;
        Texture2D sheet = sheets->parts[slot->part];
        if (scene->xray) draw_at_pivot(sheet, atlas, atlas_frame(atlas, FRAMES_IDLE, 0), x, y, flip, Fade(WHITE, 0.5f));
        float strength = flash_strength(scene, c, s, tick);
        if (strength <= 0) continue;
        int frame = atlas_frame(atlas, FRAMES_HURT, 0);
        bool radiant = scene->hit_element[c][s] == ELEMENT_RADIANCE;
        if (radiant) BeginBlendMode(BLEND_ADDITIVE);
        draw_at_pivot(sheet, atlas, frame, x, y, flip, Fade(WHITE, strength));
        if (radiant) {
            draw_at_pivot(sheet, atlas, frame, x, y - 1, flip, Fade(WHITE, strength / 2));
            draw_at_pivot(sheet, atlas, frame, x, y + 1, flip, Fade(WHITE, strength / 2));
            EndBlendMode();
        }
    }
}

static void draw_remains(const Drawable *drawable, const Battle *b, const Atlases *atlases, const Sheets *sheets)
{
    const Remains *remains = &b->remains[drawable->index];
    for (int i = 0; i < remains->part_count; i++) {
        int part = remains->parts[i];
        int dx, dy;
        remains_offset(drawable->index, i, &dx, &dy);
        const Atlas *atlas = &atlases->parts[part];
        draw_at_pivot(sheets->parts[part], atlas, atlas_frame(atlas, FRAMES_BROKEN, 0), floorf(drawable->x) + (float)dx,
                      floorf(drawable->y) + (float)dy, false, WHITE);
    }
}

static void draw_combatant(const Drawable *drawable, const Battle *b, const GameData *data, const Scene *scene,
                           const Atlases *atlases, const Sheets *sheets, int tick)
{
    const Body *body = &b->combatants[drawable->index].body;
    float origin_y = floorf(drawable->y - (float)body_feet_offset(body, atlases));
    float origin_x = floorf(drawable->x);
    draw_external_parts(b, data, scene, atlases, sheets, drawable->index, origin_x, origin_y, tick);
    draw_internal_parts(b, scene, atlases, sheets, drawable->index, origin_x, origin_y, tick);
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
        case DRAW_REMAINS: draw_remains(drawable, b, atlases, sheets); break;
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
