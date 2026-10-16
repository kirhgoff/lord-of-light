#include <stdlib.h>
#include <string.h>
#include "cJSON.h"
#include "check.h"
#include "../src/sim/battle.h"

#define ERR_SIZE 256

static GameData data;
static char err[ERR_SIZE];
static Battle battle, loaded;

static void reset(Battle *b)
{
    battle_free(b);
    memset(b, 0, sizeof *b);
}

static void load_data(void)
{
    LOAD_OR_EXIT(load_game_data(&data, "data", err, ERR_SIZE), err);
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

static void test_placeholder_horde_is_viable(void)
{
    load_data();
    for (uint32_t seed = 0; seed < 200; seed++) {
        battle_setup(&battle, &data, seed);
        for (int c = 0; c < battle.combatant_count; c++) {
            const Combatant *who = &battle.combatants[c];
            if (who->side != SIDE_HORDE) continue;
            bool working[MAX_SLOTS];
            body_working_parts(&who->body, who->parts, working);
            bool has_heart = false, can_move = false;
            for (int s = 0; s < who->body.slot_count; s++) {
                const Part *part = &data.parts[who->body.slots[s].part];
                CHECK(!part->deity_only && !part->minion_only);
                has_heart |= working[s] && part_fits_role(part, SLOT_ROLE_HEART);
                can_move |= working[s] && part_fits_role(part, SLOT_ROLE_MOVER);
            }
            CHECK(has_heart && can_move);
        }
    }
    battle_free(&battle);
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
        bool on_left = who->x_millipx < ARENA_WIDTH_MILLIPX / 2;
        CHECK(on_left == (who->side == SIDE_PANTHEON));
        if (on_left) left++;
        else right++;
    }
    CHECK(left == DEITIES_PER_PANTHEON && right == HORDE_SIZE);
    CHECK(battle.pickup_count == data.pickup_count && battle.trap_count == data.trap_count);
    CHECK(battle_winner(&battle) == -1);
}

static void send_everyone_to_the_middle(Battle *b)
{
    for (int c = 0; c < b->combatant_count; c++) {
        Combatant *who = &b->combatants[c];
        who->parts[who->body.core].order = (Order){ORDER_MOVE_TO, -1, ARENA_WIDTH_MILLIPX / 2, ARENA_HEIGHT_MILLIPX / 2};
    }
}

static void test_step_advances_tick_and_moves_only_with_orders(void)
{
    load_data();
    battle_setup(&battle, &data, 3);
    int x = battle.combatants[0].x_millipx;
    run(&battle, 10);
    CHECK(battle.tick == 10);
    CHECK(battle.combatants[0].x_millipx == x);
    send_everyone_to_the_middle(&battle);
    run(&battle, 10);
    CHECK(battle.combatants[0].x_millipx > x);
}

static void test_round_trip_is_identical(void)
{
    load_data();
    battle_setup(&battle, &data, 11);
    run(&battle, 30);
    CHECK(battle_log_event(&battle, (Event){0, EVENT_HIT, 1, 2, 5, 3, ELEMENT_FIRE, 12, -4}));
    battle.combatants[2].parts[1].order = (Order){ORDER_STRIKE, 5, 10000, 20000};
    battle.combatants[2].decision = (Decision){STANCE_FIGHT, 40, ELEMENT_FIRE};
    battle.combatants[5].charm_ticks = 9;
    battle.combatants[5].charm_side = SIDE_PANTHEON;
    battle.combatants[5].dot_damage = 2;
    battle.combatants[5].dot_element = ELEMENT_FIRE;
    battle.combatants[5].dot_source = 1;
    battle.combatants[5].slow_percent = 40;
    battle.combatants[5].spawner = 2;
    battle.combatants[5].damage_bonus_percent = 50;
    battle.combatants[5].damage_bonus_ticks = 7;
    battle.remains[0] = (Remains){5000, 6000, 2, {1, 2}};
    battle.remains_count = 1;
    char *json = battle_save_json(&battle, &data);
    CHECK(battle_load_json(&loaded, &data, json, err, ERR_SIZE));
    char *again = battle_save_json(&loaded, &data);
    CHECK(strcmp(json, again) == 0);
    CHECK(memcmp(loaded.obstacle, battle.obstacle, sizeof battle.obstacle) == 0);
    CHECK(loaded.log_count == 1 && loaded.log[0].amount == 12 && loaded.log[0].detail == -4 && loaded.log[0].tick == 30 && loaded.rng == battle.rng);
    free(json);
    free(again);
    battle_free(&loaded);
    battle_free(&battle);
}

static void test_resume_matches_uninterrupted_run(void)
{
    load_data();
    static Battle straight, resumed;
    battle_setup(&straight, &data, 21);
    send_everyone_to_the_middle(&straight);
    run(&straight, 50);
    char *midway = battle_save_json(&straight, &data);
    run(&straight, 120);
    CHECK(battle_load_json(&resumed, &data, midway, err, ERR_SIZE));
    run(&resumed, 120);
    char *expected = battle_save_json(&straight, &data), *actual = battle_save_json(&resumed, &data);
    CHECK(strcmp(expected, actual) == 0);
    CHECK(resumed.tick == 170);
    battle_free(&straight);
    battle_free(&resumed);
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

static char *saved_json(uint32_t seed)
{
    battle_setup(&battle, &data, seed);
    battle_log_event(&battle, (Event){0, EVENT_HIT, 0, 0, 1, 0, ELEMENT_FIRE, 5, 0});
    return battle_save_json(&battle, &data);
}

static char *replaced(const char *json, const char *from, const char *to)
{
    const char *at = strstr(json, from);
    CHECK(at != NULL);
    char *out = malloc(strlen(json) + strlen(to) + 1);
    memcpy(out, json, (size_t)(at - json));
    strcpy(out + (at - json), to);
    strcpy(out + (at - json) + strlen(to), at + strlen(from));
    return out;
}

static void check_rejected(const char *json, const char *from, const char *to)
{
    char *mutated = replaced(json, from, to);
    err[0] = '\0';
    loaded.tick = 99;
    bool ok = battle_load_json(&loaded, &data, mutated, err, ERR_SIZE);
    if (ok || err[0] == '\0' || loaded.tick != 99) printf("  not rejected cleanly: %s -> %s (err '%s')\n", from, to, err);
    CHECK(!ok);
    CHECK(err[0] != '\0');
    CHECK(loaded.tick == 99);
    free(mutated);
}

static void test_silent_failure_gets_generic_message(char *json)
{
    cJSON *root = cJSON_Parse(json);
    cJSON *parts = cJSON_GetObjectItem(cJSON_GetArrayItem(cJSON_GetObjectItem(root, "combatants"), 0), "parts");
    cJSON_ReplaceItemInArray(parts, 0, cJSON_CreateNumber(5));
    char *broken = cJSON_PrintUnformatted(root);
    err[0] = '\0';
    CHECK(!battle_load_json(&loaded, &data, broken, err, ERR_SIZE));
    CHECK(strcmp(err, "invalid battle state") == 0);
    free(broken);
    cJSON_Delete(root);
}

static void test_load_rejects_bad_input(void)
{
    load_data();
    char *json = saved_json(3);
    check_rejected(json, "\"side\":\"pantheon\"", "\"side\":\"nowhere\"");
    check_rejected(json, "\"combatants\":[{", "\"combatants\":[5,{");
    check_rejected(json, "\"pickups\":[{", "\"pickups\":[5,{");
    check_rejected(json, "\"log\":[{", "\"log\":[5,{");
    check_rejected(json, "\"decision\":{\"stance\"", "\"decisionx\":{\"stance\"");
    check_rejected(json, "\"slots\":[{", "\"slots\":[5,{");
    check_rejected(json, "\"def\":\"soma\"", "\"def\":\"no_such_pickup\"");
    check_rejected(json, "\"seed\":3", "\"seed\":4294967296");
    check_rejected(json, "\"rng\":", "\"rng\":0,\"old\":");
    check_rejected(json, "\"tick\":0", "\"tick\":1e19");
    check_rejected(json, "\"tick\":0", "\"tick\":2147483647");
    check_rejected(json, "\"x_millipx\":", "\"x_millipx\":1e300,\"z\":");
    check_rejected(json, "\"x_millipx\":", "\"x_millipx\":-1e999,\"z\":");
    check_rejected(json, "\"edges\":[[", "\"edges\":[{\"a\":0},[");
    check_rejected(json, "\"edges\":[[2,3]", "\"edges\":[[2,1e300]");
    check_rejected(json, "\"target\":-1", "\"target\":64");
    check_rejected(json, "\"actor\":0", "\"actor\":64");
    check_rejected(json, "\"actor_part\":0", "\"actor_part\":24");
    check_rejected(json, "\"amount\":5", "\"amount\":2000000000");
    check_rejected(json, "\"kind\":\"hit\"", "\"kind\":7");
    check_rejected(json, "\"dot_element\":-1", "\"dot_element\":8");
    check_rejected(json, "\"spawner\":-1", "\"spawner\":99");
    check_rejected(json, "\"slow_percent\":0", "\"slow_percent\":101");
    check_rejected(json, "\"haste_percent\":0", "\"haste_percent\":1001");
    test_silent_failure_gets_generic_message(json);
    CHECK(!battle_load_json(&loaded, &data, "{", err, ERR_SIZE));
    CHECK(!battle_load_json(&loaded, &data, "{}", err, ERR_SIZE) && err[0] != '\0');
    CHECK(!battle_load_json(&loaded, &data, "[1]", err, ERR_SIZE) && err[0] != '\0');
    free(json);
    battle_free(&battle);
    battle_free(&loaded);
}

static void test_remains_and_floor_objects_are_saved_by_id(void)
{
    load_data();
    battle_setup(&battle, &data, 3);
    battle.remains[0] = (Remains){5000, 6000, 1, {find_part(&data, "heart")}};
    battle.remains_count = 1;
    char *json = battle_save_json(&battle, &data);
    CHECK(strstr(json, "\"parts\":[\"heart\"]") != NULL);
    CHECK(strstr(json, "\"def\":\"soma\"") != NULL);
    CHECK(strstr(json, "\"def\":\"spike_pit\"") != NULL);
    free(json);
    battle_free(&battle);
}

static void test_log_grows_without_dropping(void)
{
    load_data();
    reset(&battle);
    battle_setup(&battle, &data, 3);
    for (int i = 0; i < 5000; i++) CHECK(battle_log_event(&battle, (Event){0, EVENT_HIT, 0, 0, 1, 0, -1, i, 0}));
    CHECK(battle.log_count == 5000 && battle.log[4999].amount == 4999);
    char *json = battle_save_json(&battle, &data);
    CHECK(battle_load_json(&loaded, &data, json, err, ERR_SIZE));
    CHECK(loaded.log_count == 5000 && loaded.log[4999].amount == 4999);
    free(json);
    battle_free(&battle);
    battle_free(&loaded);
}

static void test_seed_zero_differs_from_one(void)
{
    CHECK(scramble_seed(0) != scramble_seed(1));
    CHECK(scramble_seed(0) != 0);
    load_data();
    static Battle zero, one;
    battle_setup(&zero, &data, 0);
    battle_setup(&one, &data, 1);
    CHECK(zero.rng != one.rng);
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
    test_placeholder_horde_is_viable();
    test_setup_places_both_sides();
    test_step_advances_tick_and_moves_only_with_orders();
    test_round_trip_is_identical();
    test_resume_matches_uninterrupted_run();
    test_rng_is_deterministic_and_never_zero();
    test_load_rejects_bad_input();
    test_remains_and_floor_objects_are_saved_by_id();
    test_log_grows_without_dropping();
    test_seed_zero_differs_from_one();
    test_core_connectivity_two_paths();
    test_winner_when_a_side_is_dead();
}
