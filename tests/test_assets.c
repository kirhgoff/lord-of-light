#include <stdio.h>
#include "check.h"
#include "../src/sim/data.h"

#define ERR_SIZE 256

static bool atlas_exists(const char *folder, const char *id)
{
    char path[256];
    snprintf(path, sizeof path, "assets/%s/%s.json", folder, id);
    FILE *file = fopen(path, "r");
    if (file == NULL) printf("  missing atlas: %s\n", path);
    if (file != NULL) fclose(file);
    return file != NULL;
}

static void test_every_sprite_id_has_an_atlas(void)
{
    static GameData data;
    char err[ERR_SIZE];
    CHECK(load_game_data(&data, "data", err, ERR_SIZE));
    for (int i = 0; i < data.part_count; i++) CHECK(atlas_exists("parts", data.parts[i].sprite));
    for (int i = 0; i < data.pickup_count; i++) CHECK(atlas_exists("pickups", data.pickups[i].sprite));
    for (int i = 0; i < data.trap_count; i++) CHECK(atlas_exists("traps", data.traps[i].sprite));
    for (int e = 0; e < ELEMENT_COUNT; e++) CHECK(atlas_exists("effects", element_id((Element)e)));
    CHECK(atlas_exists("tiles", "floor"));
    CHECK(atlas_exists("tiles", "obstacle"));
}

void run_asset_tests(void)
{
    test_every_sprite_id_has_an_atlas();
}
