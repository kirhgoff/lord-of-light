#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "check.h"
#include "../src/sim/battle.h"
#include "../src/view/saves.h"

#define ERR_SIZE 256

static GameData data;
static char err[ERR_SIZE];
static char dir[SAVES_PATH_SIZE];

static void load_data(void)
{
    LOAD_OR_EXIT(load_game_data(&data, "data", err, ERR_SIZE), err);
}

static void make_dir(void)
{
    snprintf(dir, sizeof dir, "build/saves_test_XXXXXX");
    mkdir("build", 0777);
    CHECK(mkdtemp(dir) != NULL);
}

static void remove_dir(void)
{
    char command[SAVES_PATH_SIZE + 16];
    snprintf(command, sizeof command, "rm -rf %s", dir);
    CHECK(system(command) == 0);
}

static void touch_with(const char *name, const char *text)
{
    char path[SAVES_PATH_SIZE];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    FILE *file = fopen(path, "w");
    CHECK(file != NULL);
    if (file) fputs(text, file), fclose(file);
}

static time_t local_time(int year, int month, int day, int hour, int minute, int second)
{
    struct tm parts = {.tm_year = year - 1900, .tm_mon = month - 1, .tm_mday = day, .tm_hour = hour, .tm_min = minute, .tm_sec = second, .tm_isdst = -1};
    return mktime(&parts);
}

static void test_name_is_a_sortable_timestamp(void)
{
    char path[SAVES_PATH_SIZE];
    saves_path_for("saves", local_time(2026, 3, 7, 9, 5, 2), path, sizeof path);
    CHECK(strcmp(path, "saves/2026-03-07_09-05-02.json") == 0);
}

static void test_newest_is_the_greatest_json_name(void)
{
    make_dir();
    touch_with("2026-01-01_00-00-00.json", "{}");
    touch_with("2026-10-16_09-00-00.json", "{}");
    touch_with("2026-02-01_00-00-00.json", "{}");
    touch_with("2099-notes.txt", "x");
    touch_with("mid.json", "{}");
    touch_with("notes.json", "{}");
    char folder[SAVES_PATH_SIZE + 32];
    snprintf(folder, sizeof folder, "%s/zz.json", dir);
    CHECK(mkdir(folder, 0777) == 0);
    snprintf(folder, sizeof folder, "%s/2099-12-31_23-59-59.json", dir);
    CHECK(mkdir(folder, 0777) == 0);
    char path[SAVES_PATH_SIZE];
    CHECK(saves_newest(dir, path, sizeof path, err, ERR_SIZE));
    CHECK(strstr(path, "2026-10-16_09-00-00.json") != NULL);
    remove_dir();
}

static void test_missing_or_empty_folder_names_the_problem(void)
{
    char path[SAVES_PATH_SIZE];
    CHECK(!saves_newest("build/no_such_saves_dir", path, sizeof path, err, ERR_SIZE));
    CHECK(strstr(err, "no_such_saves_dir") != NULL);
    make_dir();
    CHECK(!saves_newest(dir, path, sizeof path, err, ERR_SIZE));
    CHECK(strstr(err, "no saves") != NULL);
    remove_dir();
}

static void test_write_creates_the_folder_and_load_restores_the_battle(void)
{
    load_data();
    static Battle battle, restored;
    make_dir();
    char nested[SAVES_PATH_SIZE + 8], path[SAVES_PATH_SIZE];
    snprintf(nested, sizeof nested, "%s/saves", dir);
    battle_setup(&battle, &data, 9);
    for (int t = 0; t < 40; t++) battle_step(&battle, &data, NULL);
    CHECK(saves_write(nested, local_time(2026, 10, 16, 12, 0, 0), &battle, &data, path, sizeof path, err, ERR_SIZE));
    CHECK(strstr(path, "2026-10-16_12-00-00.json") != NULL);
    CHECK(saves_load_newest(nested, &restored, &data, path, sizeof path, err, ERR_SIZE));
    char *expected = battle_save_json(&battle, &data), *actual = battle_save_json(&restored, &data);
    CHECK(strcmp(expected, actual) == 0 && restored.tick == 40);
    free(expected);
    free(actual);
    battle_free(&battle);
    battle_free(&restored);
    remove_dir();
}

static char *last_decider_field(char *json)
{
    char *last = NULL;
    for (char *at = json; (at = strstr(at, "\"decider\":")); at += 1) last = at;
    return last;
}

static void test_failed_load_leaves_the_running_battle_untouched(void)
{
    load_data();
    static Battle battle;
    make_dir();
    battle_setup(&battle, &data, 4);
    for (int t = 0; t < 25; t++) battle_step(&battle, &data, NULL);
    battle_log_event(&battle, (Event){.tick = 25, .kind = EVENT_HIT, .actor = 0, .actor_part = 0, .target = 1, .target_part = 0, .element = ELEMENT_FIRE, .amount = 5, .detail = 0, .decider = DECIDER_NONE});
    char *before = battle_save_json(&battle, &data);
    char *valid = battle_save_json(&battle, &data);
    char *last_decider = last_decider_field(valid);
    CHECK(last_decider != NULL);
    if (last_decider) last_decider[strlen("\"decider\":")] = '9';
    touch_with("2026-01-01_00-00-00.json", valid);
    free(valid);
    char path[SAVES_PATH_SIZE];
    CHECK(!saves_load_newest(dir, &battle, &data, path, sizeof path, err, ERR_SIZE));
    CHECK(err[0] != '\0');
    char *after = battle_save_json(&battle, &data);
    CHECK(strcmp(before, after) == 0 && battle.tick == 25);
    free(before);
    free(after);
    battle_free(&battle);
    remove_dir();
}

void run_saves_tests(void)
{
    test_name_is_a_sortable_timestamp();
    test_newest_is_the_greatest_json_name();
    test_missing_or_empty_folder_names_the_problem();
    test_write_creates_the_folder_and_load_restores_the_battle();
    test_failed_load_leaves_the_running_battle_untouched();
}
