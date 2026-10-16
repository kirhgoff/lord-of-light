#include <string.h>
#include "check.h"
#include "../src/view/scene.h"

#define WIDTH 1280
#define HEIGHT 720
#define ERR_SIZE 256

static GameData data;
static Battle battle;

static void setup_battle(void)
{
    char err[ERR_SIZE];
    LOAD_OR_EXIT(load_game_data(&data, "data", err, ERR_SIZE), err);
    battle_setup(&battle, &data, 1);
}

static void test_atlas_reads_sidecar(void)
{
    Atlas atlas;
    char err[ERR_SIZE];
    CHECK(atlas_load(&atlas, "parts", "sword_hand", err, ERR_SIZE));
    CHECK(atlas.frame_w == 32 && atlas.frame_h == 32);
    CHECK(atlas.pivot_x == 5 && atlas.pivot_y == 24);
    CHECK(atlas.first[FRAMES_ACT] == 2 && atlas.count[FRAMES_ACT] == 3);
    CHECK(atlas.first[FRAMES_BROKEN] == 6);
    CHECK(atlas_load(&atlas, "effects", "fire", err, ERR_SIZE));
    CHECK(atlas.count[FRAMES_PLAY] == 5);
    CHECK(atlas_load(&atlas, "tiles", "floor", err, ERR_SIZE));
    CHECK(atlas.count[FRAMES_VARIANTS] == 4);
}

static void test_atlas_missing_names_the_path(void)
{
    Atlas atlas;
    char err[ERR_SIZE];
    CHECK(!atlas_load(&atlas, "parts", "no_such_part", err, ERR_SIZE));
    CHECK(strstr(err, "assets/parts/no_such_part.json") != NULL);
}

static void test_atlas_frame_wraps_within_the_kind(void)
{
    Atlas atlas = {.first = {[FRAMES_ACT] = 2}, .count = {[FRAMES_ACT] = 3}};
    CHECK(atlas_frame(&atlas, FRAMES_ACT, 0) == 2);
    CHECK(atlas_frame(&atlas, FRAMES_ACT, 4) == 3);
    CHECK(atlas_frame(&atlas, FRAMES_HURT, 7) == 0);
}

static void test_all_atlases_load_for_the_real_data(void)
{
    static Atlases atlases;
    char err[ERR_SIZE];
    LOAD_OR_EXIT(load_game_data(&data, "data", err, ERR_SIZE), err);
    CHECK(atlases_load(&atlases, &data, err, ERR_SIZE));
    if (err[0]) printf("  %s\n", err);
}

static void test_viewport_starts_centred_and_clamped(void)
{
    Viewport v;
    viewport_init(&v, WIDTH, HEIGHT);
    CHECK(v.zoom == 2);
    CHECK(v.centre_x >= 320 && v.centre_x <= 448);
    CHECK(v.centre_y >= 180 && v.centre_y <= 332);
    viewport_zoom(&v, -1);
    CHECK(v.centre_x == 384 && v.centre_y == 256);
}

static void test_viewport_zoom_stays_within_limits(void)
{
    Viewport v;
    viewport_init(&v, WIDTH, HEIGHT);
    for (int i = 0; i < 10; i++) viewport_zoom(&v, 1);
    CHECK(v.zoom == ZOOM_MAX);
    for (int i = 0; i < 10; i++) viewport_zoom(&v, -1);
    CHECK(v.zoom == ZOOM_MIN);
}

static void test_viewport_pan_stops_at_the_arena_edge(void)
{
    Viewport v;
    viewport_init(&v, WIDTH, HEIGHT);
    viewport_pan(&v, 10000, 10000);
    CHECK(v.centre_x == 448 && v.centre_y == 332);
    viewport_pan(&v, -10000, -10000);
    CHECK(v.centre_x == 320 && v.centre_y == 180);
}

static void test_viewport_centre_maps_to_screen_centre(void)
{
    Viewport v;
    viewport_init(&v, WIDTH, HEIGHT);
    viewport_pan(&v, 40, -30);
    CHECK(viewport_screen_x(&v, v.centre_x) == WIDTH / 2);
    CHECK(viewport_screen_y(&v, v.centre_y) == HEIGHT / 2);
    CHECK(viewport_screen_x(&v, v.centre_x + 10) == WIDTH / 2 + 10 * v.zoom);
}

static void test_position_interpolates_between_ticks(void)
{
    setup_battle();
    Scene scene = {0};
    scene_reset(&scene, &battle);
    battle.combatants[0].x_millipx = 1000;
    battle.combatants[0].y_millipx = 4000;
    scene_before_step(&scene, &battle);
    battle.combatants[0].x_millipx = 3000;
    scene.alpha = 0.5f;
    float x, y;
    scene_position(&scene, &battle, 0, &x, &y);
    CHECK(x == 2.0f && y == 4.0f);
    scene.prev_count = 0;
    scene_position(&scene, &battle, 0, &x, &y);
    CHECK(x == 3.0f);
    CHECK(scene_time(&scene, &battle) == (float)battle.tick + 0.5f);
}

static void test_facing_follows_horizontal_movement(void)
{
    setup_battle();
    Scene scene = {0};
    scene_reset(&scene, &battle);
    int pantheon = 0, horde = battle.combatant_count - 1;
    CHECK(!scene.facing_left[pantheon] && scene.facing_left[horde]);
    scene_before_step(&scene, &battle);
    battle.combatants[pantheon].x_millipx -= 500;
    battle.combatants[horde].x_millipx += 500;
    scene_after_step(&scene, &battle);
    CHECK(scene.facing_left[pantheon] && !scene.facing_left[horde]);
    scene_before_step(&scene, &battle);
    scene_after_step(&scene, &battle);
    CHECK(scene.facing_left[pantheon] && !scene.facing_left[horde]);
}

void run_view_tests(void)
{
    test_position_interpolates_between_ticks();
    test_facing_follows_horizontal_movement();
    test_viewport_starts_centred_and_clamped();
    test_viewport_zoom_stays_within_limits();
    test_viewport_pan_stops_at_the_arena_edge();
    test_viewport_centre_maps_to_screen_centre();
    test_atlas_reads_sidecar();
    test_atlas_missing_names_the_path();
    test_atlas_frame_wraps_within_the_kind();
    test_all_atlases_load_for_the_real_data();
}
