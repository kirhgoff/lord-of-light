#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "check.h"
#include "../src/sim/combat.h"
#include "../src/sim/floor.h"
#include "../src/sim/movement.h"
#include "../src/sim/vision.h"

#define ERR_SIZE 256
#define STRIKER 0
#define DUMMY 1
#define SECOND 2
#define HURT 4

static GameData data;
static Battle battle;
static char err[ERR_SIZE];

static char *read_text_file(const char *path)
{
    FILE *file = fopen(path, "rb");
    if (!file) return NULL;
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    rewind(file);
    char *text = size < 0 ? NULL : malloc((size_t)size + 1);
    if (text && fread(text, 1, (size_t)size, file) == (size_t)size) text[size] = '\0';
    else free(text), text = NULL;
    fclose(file);
    return text;
}

static void load_fixture(void)
{
    LOAD_OR_EXIT(load_game_data(&data, "data", err, ERR_SIZE), err);
    char *json = read_text_file("tests/fixtures/battle_elements.json");
    CHECK(json != NULL);
    battle_free(&battle);
    CHECK(json && battle_load_json(&battle, &data, json, err, ERR_SIZE));
    if (!json) printf("  %s\n", err);
    free(json);
}

static void run(int ticks)
{
    for (int t = 0; t < ticks; t++) battle_step(&battle, &data);
}

static int count_events(EventKind kind)
{
    int count = 0;
    for (int e = 0; e < battle.log_count; e++) count += battle.log[e].kind == kind;
    return count;
}

static const Event *last_event(EventKind kind)
{
    for (int e = battle.log_count - 1; e >= 0; e--)
        if (battle.log[e].kind == kind) return &battle.log[e];
    return NULL;
}

static int put_pickup(const char *id, int at)
{
    const Combatant *who = &battle.combatants[at];
    battle.pickups[battle.pickup_count] = (FloorObject){find_pickup(&data, id), who->x_millipx, who->y_millipx, 0};
    return battle.pickup_count++;
}

static int put_trap(const char *id, int at)
{
    const Combatant *who = &battle.combatants[at];
    battle.traps[battle.trap_count] = (FloorObject){find_trap(&data, id), who->x_millipx, who->y_millipx, 0};
    return battle.trap_count++;
}

static int slot(int c, const char *id)
{
    return find_body_slot(&battle.combatants[c].body, id);
}

static void test_soma_heals_vitality_and_respawns(void)
{
    load_fixture();
    int soma = put_pickup("soma", HURT);
    run(1);
    const Event *taken = last_event(EVENT_PICKUP);
    CHECK(battle.combatants[HURT].vitality == 40);
    CHECK(taken && taken->actor == HURT && taken->amount == 30 && taken->detail == soma);
    CHECK(battle.pickups[soma].respawn_in == 600);
    int wasted = put_pickup("soma", DUMMY);
    run(1);
    CHECK(battle.pickups[wasted].respawn_in == 0 && count_events(EVENT_PICKUP) == 1);
}

static void test_menders_moss_mends_damaged_parts_not_destroyed_ones(void)
{
    load_fixture();
    PartState *parts = battle.combatants[DUMMY].parts;
    parts[slot(DUMMY, "plate")].integrity = 30;
    parts[slot(DUMMY, "shield")].integrity = 0;
    put_pickup("menders_moss", DUMMY);
    run(1);
    CHECK(parts[slot(DUMMY, "plate")].integrity == 45 && parts[slot(DUMMY, "shield")].integrity == 0);
    CHECK(last_event(EVENT_PICKUP)->amount == 15);
}

static void test_ember_seed_grants_damage_bonus(void)
{
    load_fixture();
    const Combatant *dummy = &battle.combatants[DUMMY];
    put_pickup("ember_seed", DUMMY);
    run(1);
    CHECK(dummy->damage_bonus_percent == 50 && dummy->damage_bonus_ticks == 200);
    run(1);
    CHECK(dummy->damage_bonus_ticks == 199);
}

static int ten_ticks_of_walking_east(void)
{
    Combatant *dummy = &battle.combatants[DUMMY];
    dummy->x_millipx = 100000;
    dummy->y_millipx = 400000;
    dummy->parts[slot(DUMMY, "legs")].order = (Order){ORDER_MOVE_TO, -1, 600000, 400000};
    run(10);
    return dummy->x_millipx - 100000;
}

static void test_quickening_leaf_speeds_movement(void)
{
    load_fixture();
    put_pickup("quickening_leaf", DUMMY);
    run(1);
    CHECK(battle.combatants[DUMMY].haste_percent == 50 && battle.combatants[DUMMY].haste_ticks == 200);
    CHECK(ten_ticks_of_walking_east() == 45000);
    load_fixture();
    CHECK(ten_ticks_of_walking_east() == 30000);
}

static void test_eye_of_far_seeing_extends_vision(void)
{
    LOAD_OR_EXIT(load_game_data(&data, "data", err, ERR_SIZE), err);
    battle_setup(&battle, &data, 3);
    int before[3], after[3];
    combatant_vision_ranges(&battle.combatants[0], &data, before);
    CHECK(before[BAND_CLOSE] > 0);
    put_pickup("eye_of_far_seeing", 0);
    run(1);
    combatant_vision_ranges(&battle.combatants[0], &data, after);
    for (int b = 0; b < 3; b++) CHECK(after[b] == before[b] * 150 / 100);
    CHECK(battle.combatants[0].vision_ticks == 400);
    battle_free(&battle);
}

static void test_ward_stone_raises_resistance(void)
{
    load_fixture();
    const Combatant *second = &battle.combatants[SECOND];
    CHECK(combatant_resistance_percent(second, &data, ELEMENT_KINETIC, false) == 0);
    put_pickup("ward_stone", SECOND);
    run(1);
    CHECK(combatant_resistance_percent(second, &data, ELEMENT_KINETIC, false) == 40);
    run(200);
    CHECK(combatant_resistance_percent(second, &data, ELEMENT_KINETIC, false) == 0);
}

static void test_spike_pit_hits_the_first_combatant_on_it(void)
{
    load_fixture();
    int pit = put_trap("spike_pit", SECOND);
    run(1);
    const Event *sprung = last_event(EVENT_TRAP);
    CHECK(sprung && sprung->target == SECOND && sprung->element == ELEMENT_KINETIC && sprung->amount == 15 && sprung->detail == pit);
    const Event *hit = last_event(EVENT_HIT);
    CHECK(hit && hit->actor == -1 && hit->actor_part == -1 && hit->amount == 15);
    CHECK(battle.combatants[SECOND].vitality == 33 && battle.traps[pit].respawn_in == 100);
    run(1);
    CHECK(count_events(EVENT_HIT) == 1 && battle.traps[pit].respawn_in == 99);
}

static void test_frost_rune_slows(void)
{
    load_fixture();
    put_trap("frost_rune", SECOND);
    run(1);
    CHECK(battle.combatants[SECOND].slow_percent == 40 && battle.combatants[SECOND].slow_ticks == 60);
    const Event *slow = last_event(EVENT_EFFECT);
    CHECK(slow && slow->detail == EFFECT_SLOW && slow->actor == -1);
}

static void test_spore_pod_poisons_internal_parts_first(void)
{
    load_fixture();
    const Combatant *second = &battle.combatants[SECOND];
    put_trap("spore_pod", SECOND);
    run(1);
    CHECK(second->dot_element == ELEMENT_VENOM && second->dot_ticks == 200 && second->dot_source == -1);
    CHECK(last_event(EVENT_HIT)->target_part == slot(SECOND, "heart"));
}

static void test_lightning_pylon_chains_to_anyone_nearby(void)
{
    load_fixture();
    put_trap("lightning_pylon", SECOND);
    run(1);
    CHECK(count_events(EVENT_HIT) == 3);
    CHECK(battle.log[1].target == SECOND && battle.log[1].amount == 12);
    CHECK(battle.log[2].target == DUMMY && battle.log[3].target == STRIKER);
}

static void test_void_rift_strips_two_buffs_in_order(void)
{
    load_fixture();
    Combatant *second = &battle.combatants[SECOND];
    second->damage_bonus_percent = second->haste_percent = second->vision_percent = 50;
    second->damage_bonus_ticks = second->haste_ticks = second->vision_ticks = 100;
    put_trap("void_rift", SECOND);
    run(1);
    CHECK(second->damage_bonus_ticks == 0 && second->damage_bonus_percent == 0);
    CHECK(second->haste_ticks == 0 && second->haste_percent == 0);
    CHECK(second->vision_ticks == 99 && second->vision_percent == 50);
    const Event *strip = last_event(EVENT_EFFECT);
    CHECK(strip && strip->detail == EFFECT_STRIP && strip->amount == 2);
}

static bool apart_from_each_other(const FloorObject *a, const FloorObject *b)
{
    return abs(a->x_millipx / TILE_MILLIPX - b->x_millipx / TILE_MILLIPX) >= FLOOR_SPACING_TILES ||
           abs(a->y_millipx / TILE_MILLIPX - b->y_millipx / TILE_MILLIPX) >= FLOOR_SPACING_TILES;
}

static void check_placement(const FloorObject *object)
{
    int column = object->x_millipx / TILE_MILLIPX, row = object->y_millipx / TILE_MILLIPX;
    CHECK(column >= FLOOR_MARGIN_TILES && column < ARENA_WIDTH_TILES - FLOOR_MARGIN_TILES);
    for (int dr = -1; dr <= 1; dr++)
        for (int dc = -1; dc <= 1; dc++) CHECK(!tile_is_solid(&battle, column + dc, row + dr));
}

static void test_setup_scatters_objects_apart_from_obstacles_and_each_other(void)
{
    LOAD_OR_EXIT(load_game_data(&data, "data", err, ERR_SIZE), err);
    for (unsigned seed = 0; seed < 10; seed++) {
        battle_setup(&battle, &data, seed);
        CHECK(battle.pickup_count == 6 && battle.trap_count == 5);
        FloorObject all[11];
        memcpy(all, battle.pickups, sizeof(FloorObject) * 6);
        memcpy(all + 6, battle.traps, sizeof(FloorObject) * 5);
        for (int i = 0; i < 11; i++) {
            check_placement(&all[i]);
            for (int j = i + 1; j < 11; j++) CHECK(apart_from_each_other(&all[i], &all[j]));
        }
    }
    battle_free(&battle);
}

static void test_taken_pickup_returns_after_respawn_ticks(void)
{
    load_fixture();
    int soma = put_pickup("soma", HURT);
    run(1);
    battle.combatants[HURT].vitality = 10;
    run(599);
    CHECK(battle.pickups[soma].respawn_in == 1 && battle.combatants[HURT].vitality == 10);
    run(1);
    CHECK(battle.combatants[HURT].vitality == 40 && count_events(EVENT_PICKUP) == 2);
}

static void test_buffs_and_trap_events_survive_save_and_load(void)
{
    load_fixture();
    Combatant *dummy = &battle.combatants[DUMMY];
    dummy->haste_percent = 50, dummy->haste_ticks = 7;
    dummy->vision_percent = 60, dummy->vision_ticks = 8;
    dummy->ward_percent = 40, dummy->ward_ticks = 9;
    battle_log_event(&battle, (Event){0, EVENT_TRAP, -1, -1, DUMMY, -1, ELEMENT_FROST, 8, 1});
    char *json = battle_save_json(&battle, &data);
    static Battle loaded;
    CHECK(battle_load_json(&loaded, &data, json, err, ERR_SIZE));
    const Combatant *back = &loaded.combatants[DUMMY];
    CHECK(back->haste_percent == 50 && back->haste_ticks == 7 && back->vision_percent == 60);
    CHECK(back->vision_ticks == 8 && back->ward_percent == 40 && back->ward_ticks == 9);
    CHECK(loaded.log[loaded.log_count - 1].kind == EVENT_TRAP);
    battle_free(&loaded);
    free(json);
}

void run_floor_tests(void)
{
    test_soma_heals_vitality_and_respawns();
    test_menders_moss_mends_damaged_parts_not_destroyed_ones();
    test_ember_seed_grants_damage_bonus();
    test_quickening_leaf_speeds_movement();
    test_eye_of_far_seeing_extends_vision();
    test_ward_stone_raises_resistance();
    test_spike_pit_hits_the_first_combatant_on_it();
    test_frost_rune_slows();
    test_spore_pod_poisons_internal_parts_first();
    test_lightning_pylon_chains_to_anyone_nearby();
    test_void_rift_strips_two_buffs_in_order();
    test_setup_scatters_objects_apart_from_obstacles_and_each_other();
    test_taken_pickup_returns_after_respawn_ticks();
    test_buffs_and_trap_events_survive_save_and_load();
    battle_free(&battle);
}
