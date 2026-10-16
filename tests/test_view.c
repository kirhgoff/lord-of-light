#include <string.h>
#include "check.h"
#include "../src/view/atlas.h"

#define ERR_SIZE 256

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
    static GameData data;
    static Atlases atlases;
    char err[ERR_SIZE];
    LOAD_OR_EXIT(load_game_data(&data, "data", err, ERR_SIZE), err);
    CHECK(atlases_load(&atlases, &data, err, ERR_SIZE));
    if (err[0]) printf("  %s\n", err);
}

void run_view_tests(void)
{
    test_atlas_reads_sidecar();
    test_atlas_missing_names_the_path();
    test_atlas_frame_wraps_within_the_kind();
    test_all_atlases_load_for_the_real_data();
}
