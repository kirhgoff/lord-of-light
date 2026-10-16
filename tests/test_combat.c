#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "check.h"
#include "../src/sim/combat.h"

#define ERR_SIZE 256
#define STRIKER 0
#define DUMMY 1
#define SECOND 2
#define FAR 3
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
    CHECK(load_game_data(&data, "data", err, ERR_SIZE));
    char *json = read_text_file("tests/fixtures/battle_elements.json");
    CHECK(json != NULL);
    battle_free(&battle);
    CHECK(json && battle_load_json(&battle, &data, json, err, ERR_SIZE));
    if (!json) printf("  %s\n", err);
    free(json);
}

static int slot(int c, const char *id)
{
    return find_body_slot(&battle.combatants[c].body, id);
}

static int sum_integrity(int c, bool internal)
{
    const Combatant *who = &battle.combatants[c];
    int total = 0;
    for (int s = 0; s < who->body.slot_count; s++)
        if (who->body.slots[s].internal == internal) total += who->parts[s].integrity;
    return total;
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

static void sword_hits(int target)
{
    combatant_hit(&battle, &data, STRIKER, slot(STRIKER, "sword"), target, ELEMENT_KINETIC, 12);
}

static void test_kinetic_hit_splits_part_and_vitality(void)
{
    load_fixture();
    int before = sum_integrity(DUMMY, false);
    sword_hits(DUMMY);
    CHECK(before - sum_integrity(DUMMY, false) == 7);
    CHECK(battle.combatants[DUMMY].vitality == 37);
    const Event *hit = last_event(EVENT_HIT);
    CHECK(hit && count_events(EVENT_HIT) == 1);
    if (!hit) return;
    CHECK(hit->actor == STRIKER && hit->actor_part == slot(STRIKER, "sword") && hit->target == DUMMY);
    CHECK(hit->element == ELEMENT_KINETIC && hit->amount == 7);
    CHECK(hit->detail == battle.combatants[DUMMY].parts[hit->target_part].integrity);
}

static void test_block_halves_damage(void)
{
    load_fixture();
    battle.combatants[DUMMY].parts[slot(DUMMY, "shield")].order = (Order){ORDER_BLOCK, -1, 0, 0};
    int before = sum_integrity(DUMMY, false);
    sword_hits(DUMMY);
    CHECK(before - sum_integrity(DUMMY, false) == 3);
    CHECK(battle.combatants[DUMMY].vitality == 39);
}

static void test_destroyed_shield_does_not_block(void)
{
    load_fixture();
    Combatant *dummy = &battle.combatants[DUMMY];
    dummy->parts[slot(DUMMY, "shield")].order = (Order){ORDER_BLOCK, -1, 0, 0};
    dummy->parts[slot(DUMMY, "shield")].integrity = 0;
    int before = sum_integrity(DUMMY, false);
    sword_hits(DUMMY);
    CHECK(before - sum_integrity(DUMMY, false) == 7);
}

static void test_core_destruction_cuts_off_parts(void)
{
    load_fixture();
    Combatant *dummy = &battle.combatants[DUMMY];
    dummy->parts[slot(DUMMY, "torso")].integrity = 1;
    dummy->parts[slot(DUMMY, "plate")].integrity = 0;
    dummy->parts[slot(DUMMY, "legs")].integrity = 0;
    dummy->parts[slot(DUMMY, "shield")].integrity = 0;
    sword_hits(DUMMY);
    CHECK(dummy->parts[slot(DUMMY, "torso")].integrity == 0);
    CHECK(!dummy->alive);
}

static void test_hit_on_a_body_without_working_parts_kills(void)
{
    load_fixture();
    battle.combatants[DUMMY].parts[slot(DUMMY, "torso")].integrity = 0;
    sword_hits(DUMMY);
    CHECK(!battle.combatants[DUMMY].alive && battle.log_count == 1);
}

static void test_death_by_vitality_leaves_remains(void)
{
    load_fixture();
    battle.combatants[DUMMY].vitality = 3;
    sword_hits(DUMMY);
    CHECK(!battle.combatants[DUMMY].alive);
    CHECK(battle.remains_count == 1);
    CHECK(battle.remains[0].x_millipx == battle.combatants[DUMMY].x_millipx && battle.remains[0].part_count == 6);
    const Event *death = last_event(EVENT_DEATH);
    CHECK(death && death->actor == STRIKER && death->actor_part == slot(STRIKER, "sword") && death->target == DUMMY);
    CHECK(death && death->element == ELEMENT_KINETIC && death->detail == 0);
}

static void test_death_when_all_hearts_destroyed(void)
{
    load_fixture();
    battle.combatants[DUMMY].parts[slot(DUMMY, "heart")].integrity = 0;
    sword_hits(DUMMY);
    CHECK(battle.combatants[DUMMY].vitality > 0);
    CHECK(!battle.combatants[DUMMY].alive);
}

static void test_battle_ends_when_a_side_is_dead(void)
{
    load_fixture();
    battle.combatants[SECOND].alive = false;
    battle.combatants[FAR].alive = false;
    battle.combatants[DUMMY].vitality = 3;
    CHECK(battle_winner(&battle) == -1);
    sword_hits(DUMMY);
    CHECK(battle_winner(&battle) == SIDE_PANTHEON);
}

void run_combat_tests(void)
{
    test_kinetic_hit_splits_part_and_vitality();
    test_block_halves_damage();
    test_destroyed_shield_does_not_block();
    test_core_destruction_cuts_off_parts();
    test_hit_on_a_body_without_working_parts_kills();
    test_death_by_vitality_leaves_remains();
    test_death_when_all_hearts_destroyed();
    test_battle_ends_when_a_side_is_dead();
    battle_free(&battle);
}
