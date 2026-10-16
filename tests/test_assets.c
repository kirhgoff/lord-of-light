#include <stdio.h>
#include <string.h>
#include "check.h"
#include "../src/sim/data.h"

#define ERR_SIZE 256

static bool file_exists(const char *folder, const char *id, const char *extension)
{
    char path[256];
    snprintf(path, sizeof path, "assets/%s/%s.%s", folder, id, extension);
    FILE *file = fopen(path, "r");
    if (file == NULL) printf("  missing file: %s\n", path);
    if (file != NULL) fclose(file);
    return file != NULL;
}

static bool atlas_exists(const char *folder, const char *id)
{
    bool json = file_exists(folder, id, "json");
    bool png = file_exists(folder, id, "png");
    return json && png;
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
    CHECK(atlas_exists("tiles", "rock"));
    CHECK(atlas_exists("tiles", "pillar"));
}

static int le32(const unsigned char *p)
{
    return p[0] | p[1] << 8 | p[2] << 16 | p[3] << 24;
}

static int le16(const unsigned char *p)
{
    return p[0] | p[1] << 8;
}

static bool sound_has_expected_format(const char *id)
{
    char path[256];
    unsigned char header[44];
    snprintf(path, sizeof path, "assets/sounds/%s.wav", id);
    FILE *file = fopen(path, "rb");
    if (file == NULL) return false;
    size_t read = fread(header, 1, sizeof header, file);
    fclose(file);
    int data_bytes = le32(header + 40);
    bool ok = read == sizeof header && memcmp(header, "RIFF", 4) == 0 && memcmp(header + 8, "WAVEfmt ", 8) == 0
        && le16(header + 20) == 1 && le16(header + 22) == 1 && le32(header + 24) == 22050
        && le16(header + 34) == 16 && memcmp(header + 36, "data", 4) == 0
        && data_bytes > 0 && data_bytes <= 22050 * 2 * 6 / 10;
    if (!ok) printf("  bad sound: %s\n", path);
    return ok;
}

static void test_every_sound_id_has_a_wav(void)
{
    static GameData data;
    char err[ERR_SIZE];
    CHECK(load_game_data(&data, "data", err, ERR_SIZE));
    for (int p = 0; p < data.part_count; p++) {
        const Part *part = &data.parts[p];
        for (int a = 0; a < part->ability_count; a++) CHECK(sound_has_expected_format(part->abilities[a].sound));
        if (part->move_speed_px_per_second > 0) CHECK(sound_has_expected_format(part->step_sound));
    }
    for (int i = 0; i < data.pickup_count; i++) CHECK(sound_has_expected_format(data.pickups[i].sound));
    for (int i = 0; i < data.trap_count; i++) CHECK(sound_has_expected_format(data.traps[i].sound));
    for (int e = 0; e < ELEMENT_COUNT; e++) {
        CHECK(sound_has_expected_format(data.elements[e].hit_sound));
        CHECK(sound_has_expected_format(data.elements[e].effect_sound));
    }
    CHECK(sound_has_expected_format(data.cues.death));
    CHECK(sound_has_expected_format(data.cues.part_destroyed));
    CHECK(sound_has_expected_format(data.cues.spawn_appear));
}

void run_asset_tests(void)
{
    test_every_sprite_id_has_an_atlas();
    test_every_sound_id_has_a_wav();
}
