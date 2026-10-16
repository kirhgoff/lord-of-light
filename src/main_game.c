#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "raylib.h"
#include "sim/battle.h"
#include "sim/decide.h"
#include "version.h"
#include "view/atlas.h"
#include "view/draw.h"
#include "view/scene.h"

#define WINDOW_WIDTH 1280
#define WINDOW_HEIGHT 720
#define ERR_SIZE 256
#define MAX_FRAME_SECONDS 0.25f

static bool parse_seed(const char *text, uint32_t *seed)
{
    char *end;
    errno = 0;
    unsigned long long value = strtoull(text, &end, 10);
    if (errno || end == text || *end || text[0] == '-' || value > UINT32_MAX) return false;
    *seed = (uint32_t)value;
    return true;
}

static int usage(void)
{
    fprintf(stderr, "usage: game [--seed N]\n");
    return 2;
}

static void handle_camera_input(Viewport *view)
{
    float step = PAN_SCREEN_PX_PER_SECOND * GetFrameTime();
    if (IsKeyDown(KEY_LEFT)) viewport_pan(view, -step, 0);
    if (IsKeyDown(KEY_RIGHT)) viewport_pan(view, step, 0);
    if (IsKeyDown(KEY_UP)) viewport_pan(view, 0, -step);
    if (IsKeyDown(KEY_DOWN)) viewport_pan(view, 0, step);
    if (IsKeyPressed(KEY_MINUS)) viewport_zoom(view, -1);
    if (IsKeyPressed(KEY_EQUAL)) viewport_zoom(view, 1);
}

static void advance_ticks(Battle *battle, const GameData *data, Decider *decider, Scene *scene, float *accumulated)
{
    *accumulated += GetFrameTime() < MAX_FRAME_SECONDS ? GetFrameTime() : MAX_FRAME_SECONDS;
    while (*accumulated >= TICK_SECONDS) {
        scene_before_step(scene, battle);
        battle_step(battle, data, decider);
        scene_after_step(scene, battle);
        *accumulated -= TICK_SECONDS;
    }
}

int main(int argc, char **argv)
{
    uint32_t seed = 1;
    for (int i = 1; i < argc; i++) {
        if (i + 1 < argc && strcmp(argv[i], "--seed") == 0 && parse_seed(argv[i + 1], &seed)) i++;
        else return usage();
    }

    static GameData data;
    static Atlases atlases;
    static Sheets sheets;
    static Battle battle;
    char err[ERR_SIZE];
    if (!load_game_data(&data, "data", err, ERR_SIZE) || !atlases_load(&atlases, &data, err, ERR_SIZE)) {
        fprintf(stderr, "%s\n", err);
        return 1;
    }

    InitWindow(WINDOW_WIDTH, WINDOW_HEIGHT, "Lord of Light " GAME_VERSION);
    SetTargetFPS(60);
    sheets_load(&sheets, &data);
    battle_setup(&battle, &data, seed);
    Decider reflex = reflex_decider();
    Viewport view;
    viewport_init(&view, WINDOW_WIDTH, WINDOW_HEIGHT);
    Scene scene = {0};
    scene_reset(&scene, &battle);
    Hud hud = {0};
    float accumulated = 0;

    while (!WindowShouldClose()) {
        handle_camera_input(&view);
        if (IsKeyPressed(KEY_SPACE)) hud.paused = !hud.paused;
        if (IsKeyPressed(KEY_T)) scene.xray = !scene.xray;
        if (!hud.paused && battle_winner(&battle) < 0) advance_ticks(&battle, &data, &reflex, &scene, &accumulated);
        scene.alpha = accumulated / TICK_SECONDS;
        BeginDrawing();
        draw_frame_contents(&battle, &data, &scene, &atlases, &sheets, &view, &hud);
        EndDrawing();
    }
    sheets_unload(&sheets);
    battle_free(&battle);
    CloseWindow();
    return 0;
}
