#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "decider_setup.h"
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

static bool parse_ticks(const char *text, long *ticks)
{
    char *end;
    errno = 0;
    long value = strtol(text, &end, 10);
    if (errno || end == text || *end || value < 0) return false;
    *ticks = value;
    return true;
}

static int usage(void)
{
    fprintf(stderr, "usage: game [--seed N] [--ticks N --screenshot out.png] [--xray] [--decider laya|reflex] [--replay dump.json]\n");
    return 2;
}

typedef struct {
    uint32_t seed;
    long ticks;
    const char *screenshot;
    bool ticks_given;
    bool xray;
} Args;

static bool parse_args(int argc, char **argv, Args *args, DeciderSetup *setup)
{
    *args = (Args){.seed = 1};
    for (int i = 1; i < argc; i++) {
        bool has_value = i + 1 < argc;
        if (strcmp(argv[i], "--xray") == 0) args->xray = true;
        else if (has_value && strcmp(argv[i], "--seed") == 0 && parse_seed(argv[i + 1], &args->seed)) i++;
        else if (has_value && strcmp(argv[i], "--ticks") == 0 && parse_ticks(argv[i + 1], &args->ticks)) i++, args->ticks_given = true;
        else if (has_value && strcmp(argv[i], "--screenshot") == 0) args->screenshot = argv[++i];
        else if (!decider_setup_parse(setup, argc, argv, &i)) return false;
    }
    return args->screenshot || !args->ticks_given;
}

static void render_to_png(const char *path, const Battle *b, const GameData *data, const Scene *scene,
                          const Atlases *atlases, const Sheets *sheets, const Viewport *view, const Hud *hud)
{
    RenderTexture2D target = LoadRenderTexture(view->width, view->height);
    BeginTextureMode(target);
    draw_frame_contents(b, data, scene, atlases, sheets, view, hud);
    EndTextureMode();
    Image image = LoadImageFromTexture(target.texture);
    ImageFlipVertical(&image);
    ExportImage(image, path);
    UnloadImage(image);
    UnloadRenderTexture(target);
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

static void run_window(Battle *battle, const GameData *data, Decider *decider, Scene *scene, const Atlases *atlases,
                       const Sheets *sheets, Viewport *view, Hud *hud)
{
    float accumulated = 0;
    while (!WindowShouldClose()) {
        if (IsKeyPressed(KEY_ESCAPE)) hud->confirm_quit = !hud->confirm_quit;
        if (hud->confirm_quit && (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_Y))) break;
        if (hud->confirm_quit && IsKeyPressed(KEY_N)) hud->confirm_quit = false;
        handle_camera_input(view);
        if (IsKeyPressed(KEY_SPACE)) hud->paused = !hud->paused;
        if (IsKeyPressed(KEY_T)) scene->xray = !scene->xray;
        if (!hud->paused && !hud->confirm_quit && battle_winner(battle) < 0) advance_ticks(battle, data, decider, scene, &accumulated);
        scene->alpha = accumulated / TICK_SECONDS;
        BeginDrawing();
        draw_frame_contents(battle, data, scene, atlases, sheets, view, hud);
        EndDrawing();
    }
}

int main(int argc, char **argv)
{
    Args args;
    static DeciderSetup setup;
    if (!parse_args(argc, argv, &args, &setup)) return usage();

    static GameData data;
    static Atlases atlases;
    static Sheets sheets;
    static Battle battle;
    char err[ERR_SIZE];
    if (!load_game_data(&data, "data", err, ERR_SIZE) || !atlases_load(&atlases, &data, err, ERR_SIZE)) {
        fprintf(stderr, "%s\n", err);
        return 1;
    }

    if (args.screenshot) SetConfigFlags(FLAG_WINDOW_HIDDEN);
    InitWindow(WINDOW_WIDTH, WINDOW_HEIGHT, "Lord of Light " GAME_VERSION);
    SetExitKey(KEY_NULL);
    SetTargetFPS(60);
    if (!sheets_load(&sheets, &data, &atlases, err, ERR_SIZE)) {
        fprintf(stderr, "%s\n", err);
        sheets_unload(&sheets);
        CloseWindow();
        return 1;
    }
    if (!decider_setup_open(&setup, &data, &battle, err, ERR_SIZE)) {
        fprintf(stderr, "decider: %s\n", err);
        sheets_unload(&sheets);
        CloseWindow();
        return 1;
    }
    if (!setup.replay_path) battle_setup(&battle, &data, args.seed);
    Viewport view;
    viewport_init(&view, WINDOW_WIDTH, WINDOW_HEIGHT);
    Scene scene = {.xray = args.xray};
    scene_reset(&scene, &battle);
    Hud hud = {0};

    if (args.screenshot) {
        for (long t = 0; t < args.ticks && battle_winner(&battle) < 0; t++) {
            scene_before_step(&scene, &battle);
            battle_step(&battle, &data, setup.decider);
            scene_after_step(&scene, &battle);
        }
        render_to_png(args.screenshot, &battle, &data, &scene, &atlases, &sheets, &view, &hud);
    } else {
        run_window(&battle, &data, setup.decider, &scene, &atlases, &sheets, &view, &hud);
    }
    decider_setup_close(&setup);
    sheets_unload(&sheets);
    battle_free(&battle);
    CloseWindow();
    return 0;
}
