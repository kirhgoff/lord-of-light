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
    LOAD_OR_EXIT(load_game_data(&data, "data", err, ERR_SIZE), err);
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
    for (int t = 0; t < ticks; t++) battle_step(&battle, &data, NULL);
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

static void test_huge_damage_with_a_huge_bonus_does_not_overflow(void)
{
    load_fixture();
    battle.combatants[STRIKER].damage_bonus_percent = 1000;
    battle.combatants[STRIKER].damage_bonus_ticks = 10;
    combatant_hit(&battle, &data, STRIKER, slot(STRIKER, "sword"), DUMMY, ELEMENT_KINETIC, 2000000);
    CHECK(last_event(EVENT_HIT)->amount == 13200000);
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
    for (int s = 0; s < 6; s++) CHECK(battle.remains[0].parts[s] == battle.combatants[DUMMY].body.slots[s].part);
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

static void test_a_side_charmed_to_the_other_has_lost(void)
{
    load_fixture();
    battle.combatants[SECOND].alive = false;
    battle.combatants[FAR].alive = false;
    CHECK(battle_winner(&battle) == -1);
    battle.combatants[DUMMY].charm_side = SIDE_PANTHEON;
    battle.combatants[DUMMY].charm_ticks = 50;
    CHECK(battle_winner(&battle) == SIDE_PANTHEON);
}

static void test_repeated_charm_wins_a_one_on_one(void)
{
    load_fixture();
    int lobe = slot(STRIKER, "lobe");
    for (int c = SECOND; c <= HURT; c++) battle.combatants[c].alive = false;
    battle.combatants[DUMMY].parts[slot(DUMMY, "legs")].order = (Order){ORDER_MOVE_TO, -1, 600000, 176000};
    order(STRIKER, "lobe", ORDER_CAST, DUMMY);
    for (int t = 0; t < 400 && battle_winner(&battle) < 0; t++) {
        battle.combatants[STRIKER].parts[lobe].order = (Order){ORDER_CAST, DUMMY, 0, 0};
        battle_step(&battle, &data, NULL);
    }
    CHECK(battle_winner(&battle) == SIDE_PANTHEON && battle.tick < 400);
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
    battle.combatants[STRIKER].charm_side = SIDE_HORDE;
    battle.combatants[STRIKER].charm_ticks = 50;
    battle_act(&battle, &data);
    const Combatant *minion = &battle.combatants[5];
    CHECK(minion->charm_side == SIDE_HORDE && minion->charm_ticks == 50);
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

static int count_effects(EffectKind kind)
{
    int count = 0;
    for (int e = 0; e < battle.log_count; e++) count += battle.log[e].kind == EVENT_EFFECT && battle.log[e].detail == (int)kind;
    return count;
}

static void clear_order(int c, const char *slot_id)
{
    order(c, slot_id, ORDER_IDLE, -1);
}

static void test_fire_burns_over_time(void)
{
    load_fixture();
    order(STRIKER, "maw", ORDER_CAST, DUMMY);
    run(1);
    clear_order(STRIKER, "maw");
    CHECK(battle.combatants[DUMMY].vitality == 35 && battle.combatants[DUMMY].dot_ticks == 59);
    run(20);
    CHECK(battle.combatants[DUMMY].vitality == 33);
    run(40);
    CHECK(battle.combatants[DUMMY].vitality == 31 && battle.combatants[DUMMY].dot_ticks == 0);
    CHECK(count_effects(EFFECT_DOT) == 3);
    const Event *pulse = last_event(EVENT_EFFECT);
    CHECK(pulse && pulse->actor == STRIKER && pulse->actor_part == -1 && pulse->element == ELEMENT_FIRE && pulse->amount == 2);
}

static void test_fire_gland_shortens_the_burn(void)
{
    load_fixture();
    battle.combatants[DUMMY].body.slots[slot(DUMMY, "gland")].part = find_part(&data, "fire_gland");
    order(STRIKER, "maw", ORDER_CAST, DUMMY);
    run(1);
    CHECK(battle.combatants[DUMMY].dot_ticks == 14);
    CHECK(count_effects(EFFECT_DOT) == 0);
}

static void test_frost_slows_then_roots(void)
{
    load_fixture();
    Combatant *dummy = &battle.combatants[DUMMY];
    order(STRIKER, "fang", ORDER_STRIKE, DUMMY);
    dummy->parts[slot(DUMMY, "legs")].order = (Order){ORDER_MOVE_TO, -1, 600000, 176000};
    int start = dummy->x_millipx;
    run(20);
    CHECK(dummy->x_millipx - start == 20 * 1800);
    CHECK(dummy->slow_percent == 40);
    const Event *slow = last_event(EVENT_EFFECT);
    CHECK(slow && slow->detail == EFFECT_SLOW && slow->amount == 60 && slow->actor_part == slot(STRIKER, "fang"));
    clear_order(STRIKER, "fang");
    dummy->slow_percent = 100;
    dummy->slow_ticks = 20;
    start = dummy->x_millipx;
    run(10);
    CHECK(dummy->x_millipx == start);
    run(10);
    CHECK(dummy->slow_ticks == 0 && dummy->slow_percent == 0);
}

static void test_lightning_chains_within_range(void)
{
    load_fixture();
    battle.combatants[FAR].x_millipx = 400000;
    order(STRIKER, "tentacle", ORDER_STRIKE, DUMMY);
    int second_before = sum_integrity(SECOND, false), far_before = sum_integrity(FAR, false);
    run(1);
    CHECK(battle.combatants[DUMMY].vitality == 37);
    CHECK(battle.combatants[SECOND].vitality == 39 && second_before - sum_integrity(SECOND, false) == 3);
    CHECK(battle.combatants[FAR].vitality == 40 && far_before == sum_integrity(FAR, false));
    CHECK(count_events(EVENT_HIT) == 2);
}

static void test_lightning_hops_from_the_previous_target_and_stops_at_the_cap(void)
{
    load_fixture();
    battle.combatants[HURT].side = battle.combatants[HURT].charm_side = SIDE_HORDE;
    battle.combatants[HURT].x_millipx = 392000;
    combatant_hit(&battle, &data, STRIKER, slot(STRIKER, "tentacle"), DUMMY, ELEMENT_LIGHTNING, 40);
    CHECK(count_events(EVENT_HIT) == 3);
    CHECK(battle.log[2].target == FAR && battle.log[2].amount == 10);
    CHECK(sum_integrity(HURT, false) == data.parts[find_part(&data, "torso")].integrity);
}

static void test_lightning_stops_when_the_hop_does_no_damage(void)
{
    load_fixture();
    combatant_hit(&battle, &data, STRIKER, slot(STRIKER, "tentacle"), DUMMY, ELEMENT_LIGHTNING, 1);
    CHECK(count_events(EVENT_HIT) == 1);
}

static void test_radiance_ignores_armour_but_not_glands(void)
{
    load_fixture();
    data.parts[find_part(&data, "armour_plate")].resistance_percent[ELEMENT_RADIANCE] = 50;
    order(STRIKER, "lantern", ORDER_CAST, DUMMY);
    int before = sum_integrity(DUMMY, false);
    run(1);
    CHECK(before - sum_integrity(DUMMY, false) == 6);
    battle.combatants[DUMMY].body.slots[slot(DUMMY, "gland")].part = find_part(&data, "radiance_gland");
    battle.combatants[STRIKER].parts[slot(STRIKER, "lantern")].cooldown = 0;
    before = sum_integrity(DUMMY, false);
    run(1);
    CHECK(before - sum_integrity(DUMMY, false) == 1);
}

static void test_venom_hits_internal_first(void)
{
    load_fixture();
    order(STRIKER, "stinger", ORDER_STRIKE, DUMMY);
    int external = sum_integrity(DUMMY, false), internal = sum_integrity(DUMMY, true);
    run(1);
    clear_order(STRIKER, "stinger");
    CHECK(internal - sum_integrity(DUMMY, true) == 5 && external == sum_integrity(DUMMY, false));
    CHECK(battle.combatants[DUMMY].vitality == 37 && battle.combatants[DUMMY].dot_ticks == 199);
    run(200);
    CHECK(battle.combatants[DUMMY].vitality == 28);
    CHECK(count_effects(EFFECT_DOT) == 10);
}

static void test_mind_charms_for_a_while(void)
{
    load_fixture();
    order(STRIKER, "lobe", ORDER_CAST, DUMMY);
    run(1);
    clear_order(STRIKER, "lobe");
    const Combatant *dummy = &battle.combatants[DUMMY];
    CHECK(last_event(EVENT_HIT)->amount == 0);
    CHECK(dummy->charm_side == SIDE_PANTHEON && dummy->charm_ticks == 99);
    const Event *charm = last_event(EVENT_EFFECT);
    CHECK(charm && charm->detail == EFFECT_CHARM && charm->amount == 100 && charm->actor_part == slot(STRIKER, "lobe"));
    order(STRIKER, "sword", ORDER_STRIKE, DUMMY);
    run(98);
    CHECK(count_events(EVENT_HIT) == 1 && dummy->charm_side == SIDE_PANTHEON);
    run(1);
    CHECK(dummy->charm_side == SIDE_HORDE && dummy->charm_ticks == 0);
}

static void test_mind_resisted_by_gland(void)
{
    load_fixture();
    battle.combatants[DUMMY].body.slots[slot(DUMMY, "gland")].part = find_part(&data, "mind_gland");
    order(STRIKER, "lobe", ORDER_CAST, DUMMY);
    run(1);
    CHECK(battle.combatants[DUMMY].charm_ticks == 24);
}

static void test_void_strips_buffs(void)
{
    load_fixture();
    battle.combatants[DUMMY].damage_bonus_percent = 50;
    battle.combatants[DUMMY].damage_bonus_ticks = 50;
    order(STRIKER, "tendril", ORDER_STRIKE, DUMMY);
    run(1);
    CHECK(battle.combatants[DUMMY].damage_bonus_ticks == 0 && battle.combatants[DUMMY].damage_bonus_percent == 0);
    const Event *strip = last_event(EVENT_EFFECT);
    CHECK(strip && strip->detail == EFFECT_STRIP && strip->amount == 1 && strip->actor_part == slot(STRIKER, "tendril"));
}

static void test_void_is_plain_without_buffs(void)
{
    load_fixture();
    order(STRIKER, "tendril", ORDER_STRIKE, DUMMY);
    int before = sum_integrity(DUMMY, false);
    run(1);
    CHECK(before - sum_integrity(DUMMY, false) == 6);
    CHECK(count_events(EVENT_EFFECT) == 0);
}

static void test_death_by_dot_names_the_source(void)
{
    load_fixture();
    battle.combatants[DUMMY].vitality = 4;
    order(STRIKER, "maw", ORDER_CAST, DUMMY);
    run(1);
    const Event *death = last_event(EVENT_DEATH);
    CHECK(!battle.combatants[DUMMY].alive && death);
    CHECK(death && death->actor == STRIKER && death->actor_part == -1 && death->element == ELEMENT_FIRE);
}

static void test_a_killing_hit_applies_no_effect(void)
{
    load_fixture();
    battle.combatants[DUMMY].vitality = 1;
    order(STRIKER, "fang", ORDER_STRIKE, DUMMY);
    run(1);
    CHECK(!battle.combatants[DUMMY].alive && count_events(EVENT_EFFECT) == 0 && battle.combatants[DUMMY].slow_ticks == 0);
}

static void test_death_by_lightning_chain_hop(void)
{
    load_fixture();
    battle.combatants[SECOND].vitality = 1;
    order(STRIKER, "tentacle", ORDER_STRIKE, DUMMY);
    run(1);
    CHECK(!battle.combatants[SECOND].alive && last_event(EVENT_DEATH)->target == SECOND);
}

static void test_effects_round_trip_and_resume(void)
{
    load_fixture();
    battle.combatants[DUMMY].damage_bonus_percent = 50;
    battle.combatants[DUMMY].damage_bonus_ticks = 50;
    const char *casters[] = {"maw", "fang", "stinger", "lobe", "tendril", "sac", "tentacle"};
    for (int i = 0; i < 7; i++) order(STRIKER, casters[i], i == 5 ? ORDER_SPAWN : i == 0 || i == 3 ? ORDER_CAST
                                                                                                   : ORDER_STRIKE,
                                      DUMMY);
    run(5);
    char *midway = battle_save_json(&battle, &data);
    CHECK(battle.combatants[DUMMY].dot_ticks > 0 && battle.combatants[DUMMY].slow_ticks > 0 && battle.combatants[DUMMY].charm_ticks > 0);
    static Battle resumed;
    CHECK(battle_load_json(&resumed, &data, midway, err, ERR_SIZE));
    char *again = battle_save_json(&resumed, &data);
    CHECK(strcmp(midway, again) == 0);
    run(60);
    for (int t = 0; t < 60; t++) battle_step(&resumed, &data, NULL);
    char *expected = battle_save_json(&battle, &data), *actual = battle_save_json(&resumed, &data);
    CHECK(strcmp(expected, actual) == 0);
    battle_free(&resumed);
    free(midway);
    free(again);
    free(expected);
    free(actual);
}

void run_combat_tests(void)
{
    test_kinetic_hit_splits_part_and_vitality();
    test_block_halves_damage();
    test_huge_damage_with_a_huge_bonus_does_not_overflow();
    test_destroyed_shield_does_not_block();
    test_core_destruction_cuts_off_parts();
    test_hit_on_a_body_without_working_parts_kills();
    test_death_by_vitality_leaves_remains();
    test_death_when_all_hearts_destroyed();
    test_battle_ends_when_a_side_is_dead();
    test_a_side_charmed_to_the_other_has_lost();
    test_repeated_charm_wins_a_one_on_one();
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
    test_fire_burns_over_time();
    test_fire_gland_shortens_the_burn();
    test_frost_slows_then_roots();
    test_lightning_chains_within_range();
    test_lightning_hops_from_the_previous_target_and_stops_at_the_cap();
    test_lightning_stops_when_the_hop_does_no_damage();
    test_radiance_ignores_armour_but_not_glands();
    test_venom_hits_internal_first();
    test_mind_charms_for_a_while();
    test_mind_resisted_by_gland();
    test_void_strips_buffs();
    test_void_is_plain_without_buffs();
    test_death_by_dot_names_the_source();
    test_a_killing_hit_applies_no_effect();
    test_death_by_lightning_chain_hop();
    test_effects_round_trip_and_resume();
    battle_free(&battle);
}
