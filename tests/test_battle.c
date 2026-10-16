#include <stdlib.h>
#include <string.h>
#include "check.h"
#include "../src/sim/battle.h"

#define ERR_SIZE 256

static GameData data;
static char err[ERR_SIZE];
static Battle battle, loaded;

static void load_data(void)
{
    CHECK(load_game_data(&data, "data", err, ERR_SIZE));
}

static void run(Battle *b, int ticks)
{
    for (int t = 0; t < ticks; t++) battle_step(b, &data);
}

static void test_setup_is_seeded(void)
{
    load_data();
    static Battle a, b, c;
    battle_setup(&a, &data, 7);
    battle_setup(&b, &data, 7);
    battle_setup(&c, &data, 8);
    char *ja = battle_save_json(&a, &data), *jb = battle_save_json(&b, &data), *jc = battle_save_json(&c, &data);
    CHECK(strcmp(ja, jb) == 0);
    CHECK(strcmp(ja, jc) != 0);
    free(ja);
    free(jb);
    free(jc);
}

static void test_setup_places_both_sides(void)
{
    load_data();
    battle_setup(&battle, &data, 3);
    CHECK(battle.combatant_count == DEITIES_PER_PANTHEON + HORDE_SIZE);
    int left = 0, right = 0;
    for (int c = 0; c < battle.combatant_count; c++) {
        const Combatant *who = &battle.combatants[c];
        CHECK(who->alive && who->vitality > 0 && who->vitality == who->vitality_max);
        CHECK(who->decision.stance == STANCE_NONE && who->parts[0].order.kind == ORDER_IDLE);
        bool on_left = who->x < ARENA_WIDTH_TILES * TILE_SIZE / 2;
        CHECK(on_left == (who->side == SIDE_PANTHEON));
        if (on_left) left++;
        else right++;
    }
    CHECK(left == DEITIES_PER_PANTHEON && right == HORDE_SIZE);
    CHECK(battle.pickup_count == data.pickup_count && battle.trap_count == data.trap_count);
    CHECK(battle_winner(&battle) == -1);
}

static void test_step_advances_tick_and_moves(void)
{
    load_data();
    battle_setup(&battle, &data, 3);
    int x = battle.combatants[0].x;
    run(&battle, 10);
    CHECK(battle.tick == 10);
    CHECK(battle.combatants[0].x > x);
}

static void test_round_trip_is_identical(void)
{
    load_data();
    battle_setup(&battle, &data, 11);
    run(&battle, 30);
    battle_log_event(&battle, EVENT_HIT, 1, 5, 12);
    battle.combatants[2].parts[1].order = (Order){ORDER_STRIKE, 5, 10, 20};
    battle.combatants[2].decision = (Decision){STANCE_FIGHT, 40, ELEMENT_FIRE};
    battle.combatants[5].charm_ticks = 9;
    battle.combatants[5].charm_side = SIDE_PANTHEON;
    battle.remains[0] = (Remains){5, 6, 2, {1, 2}};
    battle.remains_count = 1;
    char *json = battle_save_json(&battle, &data);
    CHECK(battle_load_json(&loaded, &data, json, err, ERR_SIZE));
    char *again = battle_save_json(&loaded, &data);
    CHECK(strcmp(json, again) == 0);
    CHECK(memcmp(loaded.obstacle, battle.obstacle, sizeof battle.obstacle) == 0);
    CHECK(loaded.log_count == 1 && loaded.log[0].value == 12 && loaded.rng == battle.rng);
    free(json);
    free(again);
}

static void test_resume_matches_uninterrupted_run(void)
{
    load_data();
    static Battle straight, resumed;
    battle_setup(&straight, &data, 21);
    run(&straight, 50);
    char *midway = battle_save_json(&straight, &data);
    run(&straight, 120);
    CHECK(battle_load_json(&resumed, &data, midway, err, ERR_SIZE));
    run(&resumed, 120);
    char *expected = battle_save_json(&straight, &data), *actual = battle_save_json(&resumed, &data);
    CHECK(strcmp(expected, actual) == 0);
    CHECK(resumed.tick == 170);
    free(midway);
    free(expected);
    free(actual);
}

static void test_rng_is_deterministic_and_never_zero(void)
{
    uint32_t a = 5, b = 5;
    for (int i = 0; i < 1000; i++) CHECK(rng_next(&a) == rng_next(&b) && a != 0);
    CHECK(rng_below(&a, 7) < 7);
}

static void test_load_rejects_bad_input(void)
{
    load_data();
    battle_setup(&battle, &data, 3);
    char *json = battle_save_json(&battle, &data);
    char *broken = strdup(json);
    char *at = strstr(broken, "\"side\":\"pantheon\"");
    memcpy(at + 8, "nowhere ", 8);
    loaded.tick = 99;
    CHECK(!battle_load_json(&loaded, &data, broken, err, ERR_SIZE));
    CHECK(loaded.tick == 99);
    CHECK(!battle_load_json(&loaded, &data, "{", err, ERR_SIZE));
    CHECK(!battle_load_json(&loaded, &data, "{}", err, ERR_SIZE));
    free(json);
    free(broken);
}

static void test_core_connectivity_two_paths(void)
{
    Body body = {.slot_count = 4, .core = 0, .edge_count = 4,
                 .edges = {{0, 1}, {0, 2}, {1, 3}, {2, 3}}};
    PartState parts[MAX_SLOTS] = {0};
    for (int s = 0; s < 4; s++) parts[s].integrity = 10;
    bool working[MAX_SLOTS];
    body_working_parts(&body, parts, working);
    CHECK(working[0] && working[1] && working[2] && working[3]);
    parts[1].integrity = 0;
    body_working_parts(&body, parts, working);
    CHECK(working[0] && !working[1] && working[2] && working[3]);
    parts[2].integrity = 0;
    body_working_parts(&body, parts, working);
    CHECK(working[0] && !working[1] && !working[2] && !working[3]);
    parts[0].integrity = 0;
    body_working_parts(&body, parts, working);
    CHECK(!working[0] && !working[1] && !working[2] && !working[3]);
}

static void test_winner_when_a_side_is_dead(void)
{
    load_data();
    battle_setup(&battle, &data, 3);
    for (int c = DEITIES_PER_PANTHEON; c < battle.combatant_count; c++) battle.combatants[c].alive = false;
    CHECK(battle_winner(&battle) == SIDE_PANTHEON);
    for (int c = 0; c < DEITIES_PER_PANTHEON; c++) battle.combatants[c].alive = false;
    CHECK(battle_winner(&battle) == -1);
}

void run_battle_tests(void)
{
    test_setup_is_seeded();
    test_setup_places_both_sides();
    test_step_advances_tick_and_moves();
    test_round_trip_is_identical();
    test_resume_matches_uninterrupted_run();
    test_rng_is_deterministic_and_never_zero();
    test_load_rejects_bad_input();
    test_core_connectivity_two_paths();
    test_winner_when_a_side_is_dead();
}
