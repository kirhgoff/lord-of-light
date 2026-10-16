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

static void order(int c, const char *slot_id, OrderKind kind, int target)
{
    battle.combatants[c].parts[slot(c, slot_id)].order = (Order){kind, target, 0, 0};
}

static void run(int ticks)
{
    for (int t = 0; t < ticks; t++) battle_step(&battle, &data);
}

static int cooldown(int c, const char *slot_id)
{
    return battle.combatants[c].parts[slot(c, slot_id)].cooldown;
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

static void test_strike_order_hits_and_sets_cooldown(void)
{
    load_fixture();
    order(STRIKER, "sword", ORDER_STRIKE, DUMMY);
    int before = sum_integrity(DUMMY, false);
    run(1);
    CHECK(before - sum_integrity(DUMMY, false) == 7);
    CHECK(battle.combatants[DUMMY].vitality == 37);
    CHECK(count_events(EVENT_HIT) == 1);
    CHECK(cooldown(STRIKER, "sword") == 12);
}

static void test_cooldown_spaces_hits(void)
{
    load_fixture();
    order(STRIKER, "sword", ORDER_STRIKE, DUMMY);
    run(13);
    CHECK(count_events(EVENT_HIT) == 2);
    run(12);
    CHECK(count_events(EVENT_HIT) == 3);
}

static void test_out_of_reach_does_nothing(void)
{
    load_fixture();
    order(STRIKER, "sword", ORDER_STRIKE, SECOND);
    run(1);
    CHECK(battle.log_count == 0 && cooldown(STRIKER, "sword") == 0);
}

static void test_cast_needs_line_of_sight(void)
{
    load_fixture();
    order(STRIKER, "maw", ORDER_CAST, SECOND);
    battle.obstacle[11][14] = 1;
    run(1);
    CHECK(count_events(EVENT_HIT) == 0);
    battle.obstacle[11][14] = 0;
    run(1);
    CHECK(count_events(EVENT_HIT) == 1);
}

static void test_order_at_ally_does_nothing(void)
{
    load_fixture();
    order(STRIKER, "sword", ORDER_STRIKE, HURT);
    run(1);
    order(STRIKER, "sword", ORDER_STRIKE, STRIKER);
    run(1);
    CHECK(battle.log_count == 0);
}

static void test_destroyed_part_does_not_act(void)
{
    load_fixture();
    order(STRIKER, "sword", ORDER_STRIKE, DUMMY);
    battle.combatants[STRIKER].parts[slot(STRIKER, "sword")].integrity = 0;
    run(1);
    CHECK(battle.log_count == 0);
}

static void test_part_cut_off_from_the_core_does_not_act(void)
{
    load_fixture();
    order(STRIKER, "sword", ORDER_STRIKE, DUMMY);
    battle.combatants[STRIKER].parts[slot(STRIKER, "torso")].integrity = 0;
    run(1);
    CHECK(count_events(EVENT_HIT) == 0);
}

static void test_damage_bonus_scales_hits_and_expires(void)
{
    load_fixture();
    battle.combatants[STRIKER].damage_bonus_percent = 50;
    battle.combatants[STRIKER].damage_bonus_ticks = 10;
    order(STRIKER, "sword", ORDER_STRIKE, DUMMY);
    int before = sum_integrity(DUMMY, false);
    run(1);
    CHECK(before - sum_integrity(DUMMY, false) == 10);
    run(9);
    CHECK(battle.combatants[STRIKER].damage_bonus_ticks == 0 && battle.combatants[STRIKER].damage_bonus_percent == 0);
}

static void test_support_heals_up_to_max(void)
{
    load_fixture();
    order(STRIKER, "healer", ORDER_SUPPORT, HURT);
    run(1);
    CHECK(battle.combatants[HURT].vitality == 20);
    const Event *heal = last_event(EVENT_EFFECT);
    CHECK(heal && heal->detail == EFFECT_HEAL && heal->amount == 10 && heal->actor == STRIKER && heal->target == HURT);
    CHECK(cooldown(STRIKER, "healer") == 40);
    battle.combatants[HURT].vitality = 35;
    battle.combatants[STRIKER].parts[slot(STRIKER, "healer")].cooldown = 0;
    run(1);
    CHECK(battle.combatants[HURT].vitality == 40);
    CHECK(last_event(EVENT_EFFECT)->amount == 5);
    battle.combatants[STRIKER].parts[slot(STRIKER, "healer")].cooldown = 0;
    int events = battle.log_count;
    run(1);
    CHECK(battle.log_count == events && cooldown(STRIKER, "healer") == 0);
}

static void test_support_refuses_enemies(void)
{
    load_fixture();
    battle.combatants[DUMMY].vitality = 20;
    order(STRIKER, "healer", ORDER_SUPPORT, DUMMY);
    run(1);
    CHECK(battle.log_count == 0);
    battle.combatants[HURT].alive = false;
    order(STRIKER, "healer", ORDER_SUPPORT, HURT);
    run(1);
    CHECK(battle.log_count == 0);
}

static void test_spawn_creates_minions_up_to_cap(void)
{
    load_fixture();
    order(STRIKER, "sac", ORDER_SPAWN, -1);
    battle_act(&battle, &data);
    const Combatant *minion = &battle.combatants[5];
    CHECK(battle.combatant_count == 6);
    CHECK(strcmp(minion->name, "minion_imp") == 0 && minion->side == SIDE_PANTHEON && minion->spawner == STRIKER);
    CHECK(minion->x_millipx == battle.combatants[STRIKER].x_millipx && minion->y_millipx == battle.combatants[STRIKER].y_millipx);
    const Event *spawn = last_event(EVENT_SPAWN);
    CHECK(spawn && spawn->actor == STRIKER && spawn->actor_part == slot(STRIKER, "sac") && spawn->target == 5);
    for (int extra = 0; extra < 2; extra++) {
        battle.combatants[STRIKER].parts[slot(STRIKER, "sac")].cooldown = 0;
        run(1);
    }
    CHECK(battle.combatant_count == 8);
    battle.combatants[STRIKER].parts[slot(STRIKER, "sac")].cooldown = 0;
    run(1);
    CHECK(battle.combatant_count == 8 && count_events(EVENT_SPAWN) == 3);
    battle.combatants[5].alive = false;
    battle.combatants[STRIKER].parts[slot(STRIKER, "sac")].cooldown = 0;
    run(1);
    CHECK(battle.combatant_count == 9);
}

static void test_dead_combatants_do_nothing(void)
{
    load_fixture();
    battle.combatants[DUMMY].alive = false;
    order(DUMMY, "shield", ORDER_BLOCK, -1);
    order(STRIKER, "sword", ORDER_STRIKE, DUMMY);
    run(1);
    CHECK(battle.log_count == 0);
    load_fixture();
    battle.combatants[STRIKER].alive = false;
    order(STRIKER, "sword", ORDER_STRIKE, DUMMY);
    run(1);
    CHECK(battle.log_count == 0);
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
    test_strike_order_hits_and_sets_cooldown();
    test_cooldown_spaces_hits();
    test_out_of_reach_does_nothing();
    test_cast_needs_line_of_sight();
    test_order_at_ally_does_nothing();
    test_destroyed_part_does_not_act();
    test_part_cut_off_from_the_core_does_not_act();
    test_damage_bonus_scales_hits_and_expires();
    test_support_heals_up_to_max();
    test_support_refuses_enemies();
    test_spawn_creates_minions_up_to_cap();
    test_dead_combatants_do_nothing();
    battle_free(&battle);
}
