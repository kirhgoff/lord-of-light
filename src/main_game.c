#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "decider_setup.h"
#include "raylib.h"
#include "sim/battle.h"
#include "sim/decide.h"
#include "version.h"
#include "view/atlas.h"
#include "view/draw.h"
#include "view/saves.h"
#include "view/scene.h"
#include "view/sound.h"
#include "view/ui.h"

#define WINDOW_WIDTH 1280
#define WINDOW_HEIGHT 720
#define ERR_SIZE 256
#define MAX_FRAME_SECONDS 0.25f
#define SCREENSHOT_UI_PASSES 2

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
    fprintf(stderr, "usage: game [--seed N] [--ticks N --screenshot out.png] [--xray] [--select N] [--decider laya|reflex] [--replay dump.json]\n");
    return 2;
}

typedef struct {
    uint32_t seed;
    long ticks;
    const char *screenshot;
    bool ticks_given;
    bool xray;
    int select;
} Args;

static bool parse_args(int argc, char **argv, Args *args, DeciderSetup *setup)
{
    *args = (Args){.seed = 1, .select = -1};
    long selected;
    for (int i = 1; i < argc; i++) {
        bool has_value = i + 1 < argc;
        if (strcmp(argv[i], "--xray") == 0) args->xray = true;
        else if (has_value && strcmp(argv[i], "--seed") == 0 && parse_seed(argv[i + 1], &args->seed)) i++;
        else if (has_value && strcmp(argv[i], "--ticks") == 0 && parse_ticks(argv[i + 1], &args->ticks)) i++, args->ticks_given = true;
        else if (has_value && strcmp(argv[i], "--select") == 0 && parse_ticks(argv[i + 1], &selected)) i++, args->select = (int)selected;
        else if (has_value && strcmp(argv[i], "--screenshot") == 0) args->screenshot = argv[++i];
        else if (!decider_setup_parse(setup, argc, argv, &i)) return false;
    }
    return args->screenshot || !args->ticks_given;
}

static void render_to_png(const char *path, const Battle *b, const GameData *data, const Scene *scene,
                          const Atlases *atlases, const Sheets *sheets, const Viewport *view, const Hud *hud, Ui *ui,
                          const char (*laya_briefings)[BRIEFING_MAX + 1])
{
    RenderTexture2D target = LoadRenderTexture(view->width, view->height);
    for (int pass = 0; pass < SCREENSHOT_UI_PASSES; pass++) {
        ui_update(ui, b, scene, atlases, view, false);
        BeginTextureMode(target);
        draw_frame_contents(b, data, scene, atlases, sheets, view, hud, ui, laya_briefings);
        EndTextureMode();
    }
    Image image = LoadImageFromTexture(target.texture);
    ImageFlipVertical(&image);
    ImageFormat(&image, PIXELFORMAT_UNCOMPRESSED_R8G8B8);
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

static void advance_ticks(Battle *battle, const GameData *data, Decider *decider, Scene *scene, float *accumulated, int stop_tick)
{
    *accumulated += GetFrameTime() < MAX_FRAME_SECONDS ? GetFrameTime() : MAX_FRAME_SECONDS;
    while (*accumulated >= TICK_SECONDS && battle->tick < stop_tick) {
        scene_before_step(scene, battle);
        battle_step(battle, data, decider);
        scene_after_step(scene, battle);
        *accumulated -= TICK_SECONDS;
    }
    if (battle->tick >= stop_tick) *accumulated = 0;
}

static void handle_save_keys(Battle *battle, const GameData *data, Scene *scene, Hud *hud)
{
    char path[SAVES_PATH_SIZE], err[ERR_SIZE], message[TOAST_SIZE];
    if (IsKeyPressed(KEY_F5)) {
        bool ok = saves_write(SAVES_DIR, time(NULL), battle, data, path, sizeof path, err, sizeof err);
        snprintf(message, sizeof message, ok ? "Saved %s" : "Save failed: %s", ok ? path : err);
        hud_show_toast(hud, message);
    }
    if (IsKeyPressed(KEY_F9)) {
        bool ok = saves_load_newest(SAVES_DIR, battle, data, path, sizeof path, err, sizeof err);
        if (ok) {
            scene_reset(scene, battle);
            sound_reset(battle);
        }
        snprintf(message, sizeof message, ok ? "Loaded %s" : "Load failed: %s", ok ? path : err);
        hud_show_toast(hud, message);
    }
}

static void restart_battle(Battle *battle, const GameData *data, Scene *scene, Ui *ui, Hud *hud, float *accumulated)
{
    char message[TOAST_SIZE];
    battle_setup(battle, data, battle->seed + 1);
    scene_reset(scene, battle);
    sound_reset(battle);
    ui->selected = ui->hovered = -1;
    ui->follow_log = true;
    *accumulated = 0;
    snprintf(message, sizeof message, "New Battle, seed %u", battle->seed);
    hud_show_toast(hud, message);
}

static void run_window(Battle *battle, const GameData *data, Decider *decider, Scene *scene, const Atlases *atlases,
                       const Sheets *sheets, Viewport *view, Hud *hud, Ui *ui,
                       const char (*laya_briefings)[BRIEFING_MAX + 1], int stop_tick)
{
    float accumulated = 0;
    while (!WindowShouldClose()) {
        if (IsKeyPressed(KEY_ESCAPE)) hud->confirm_quit = !hud->confirm_quit;
        if (hud->confirm_quit && (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_Y))) break;
        if (hud->confirm_quit && IsKeyPressed(KEY_N)) hud->confirm_quit = false;
        handle_camera_input(view);
        if (!hud->confirm_quit) handle_save_keys(battle, data, scene, hud);
        if (ui->restart_allowed && !hud->confirm_quit && battle_winner(battle) >= 0 && IsKeyPressed(KEY_R))
            restart_battle(battle, data, scene, ui, hud, &accumulated);
        if (IsKeyPressed(KEY_SPACE)) hud->paused = !hud->paused;
        if (IsKeyPressed(KEY_T)) scene->xray = !scene->xray;
        if (IsKeyPressed(KEY_M)) sound_toggle_mute();
        if (!hud->paused && !hud->confirm_quit && battle_winner(battle) < 0) advance_ticks(battle, data, decider, scene, &accumulated, stop_tick);
        if (battle->tick >= stop_tick) hud->paused = true;
        sound_update(battle, data, view, hud->paused || hud->confirm_quit);
        scene->alpha = accumulated / TICK_SECONDS;
        ui_update(ui, battle, scene, atlases, view, hud->confirm_quit);
        BeginDrawing();
        draw_frame_contents(battle, data, scene, atlases, sheets, view, hud, ui, laya_briefings);
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
    if (!ui_init(WINDOW_WIDTH, WINDOW_HEIGHT)) {
        fprintf(stderr, "cannot initialise the UI\n");
        CloseWindow();
        return 1;
    }
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
    Ui ui = {args.select, -1, true, !setup.replay_path};
    const char (*laya_briefings)[BRIEFING_MAX + 1] = setup.decider == &setup.laya_decider.base ? setup.laya_decider.briefing : NULL;
    int exit_code = 0;

    if (args.screenshot) {
        for (long t = 0; t < args.ticks && battle_winner(&battle) < 0; t++) {
            scene_before_step(&scene, &battle);
            battle_step(&battle, &data, setup.decider);
            scene_after_step(&scene, &battle);
        }
        render_to_png(args.screenshot, &battle, &data, &scene, &atlases, &sheets, &view, &hud, &ui, laya_briefings);
    } else if (sound_init(&data, &battle, err, ERR_SIZE)) {
        run_window(&battle, &data, setup.decider, &scene, &atlases, &sheets, &view, &hud, &ui, laya_briefings, setup.replay_path ? setup.source.tick : INT_MAX);
    } else {
        fprintf(stderr, "%s\n", err);
        exit_code = 1;
    }
    decider_setup_close(&setup);
    sound_close();
    sheets_unload(&sheets);
    battle_free(&battle);
    ui_close();
    CloseWindow();
    return exit_code;
}
