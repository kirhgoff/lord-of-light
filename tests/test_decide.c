#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "check.h"
#include "../src/sim/decide.h"
#include "../src/sim/combat.h"
#include "../src/sim/movement.h"

#define ERR_SIZE 256
#define PX MILLIPX_PER_PX

static GameData data;
static Battle battle;
static Decider reflex;

static void load_data(void)
{
    char err[ERR_SIZE];
    LOAD_OR_EXIT(load_game_data(&data, "data", err, ERR_SIZE), err);
    reflex = reflex_decider();
}

static void fresh(void)
{
    battle_free(&battle);
    memset(&battle, 0, sizeof battle);
    battle.rng = 1;
}

static int add_deity(const char *pantheon, const char *deity, Side side, int x_px, int y_px)
{
    for (int p = 0; p < data.pantheon_count; p++) {
        if (strcmp(data.pantheons[p].name, pantheon) != 0) continue;
        for (int d = 0; d < DEITIES_PER_PANTHEON; d++)
            if (strcmp(data.pantheons[p].deities[d].name, deity) == 0)
                return battle_add_combatant(&battle, &data, deity, side, &data.pantheons[p].deities[d].body, x_px * PX, y_px * PX);
    }
    CHECK(!"deity not found");
    return -1;
}

static void destroy_slot(int c, const char *slot_id)
{
    Combatant *who = &battle.combatants[c];
    who->parts[find_body_slot(&who->body, slot_id)].integrity = 0;
}

static Decision decision(int c)
{
    Decision d = {0};
    CHECK(reflex_decide(&reflex, &battle, &data, c, &d));
    return d;
}

static void run(int ticks, Decider *decider)
{
    for (int t = 0; t < ticks; t++) battle_step(&battle, &data, decider);
}

static Order order(int c, const char *slot_id)
{
    const Combatant *who = &battle.combatants[c];
    return who->parts[find_body_slot(&who->body, slot_id)].order;
}

static void set_decision(int c, Stance stance, HurtLevel hurt, int element)
{
    battle.combatants[c].decision = (Decision){stance, hurt, element};
}

static void tactics(int c)
{
    tactics_apply(&battle, &data, c);
}

static void check_order(Order actual, OrderKind kind, int target, int x_px, int y_px)
{
    CHECK(actual.kind == kind);
    CHECK(actual.target == target);
    CHECK(actual.x_millipx == x_px * PX && actual.y_millipx == y_px * PX);
}

static int decision_events(int c)
{
    int count = 0;
    for (int e = 0; e < battle.log_count; e++) count += battle.log[e].kind == EVENT_DECISION && battle.log[e].actor == c;
    return count;
}

static int interval_for(const Combatant *c)
{
    int interval = DECISION_INTERVAL_K / combatant_initiative(c, &data);
    return interval > 1 ? interval : 1;
}

static void test_initiative_sums_working_parts_floor_one(void)
{
    load_data();
    fresh();
    int zeus = add_deity("Greek", "Zeus", SIDE_PANTHEON, 100, 200);
    int shiva = add_deity("Hindu", "Shiva", SIDE_PANTHEON, 100, 300);
    CHECK(combatant_initiative(&battle.combatants[zeus], &data) == 2);
    CHECK(combatant_initiative(&battle.combatants[shiva], &data) == 6);
    destroy_slot(zeus, "head");
    CHECK(combatant_initiative(&battle.combatants[zeus], &data) == 1);
}

static void test_hurt_level_thresholds(void)
{
    load_data();
    fresh();
    int zeus = add_deity("Greek", "Zeus", SIDE_PANTHEON, 100, 200);
    CHECK(battle.combatants[zeus].vitality_max == 60);
    const int vitality[] = {60, 46, 45, 31, 30, 16, 15, 1};
    const HurtLevel expected[] = {HURT_UNHURT, HURT_UNHURT, HURT_WOUNDED, HURT_WOUNDED,
                                  HURT_BADLY_WOUNDED, HURT_BADLY_WOUNDED, HURT_NEAR_DEATH, HURT_NEAR_DEATH};
    for (int i = 0; i < 8; i++) {
        battle.combatants[zeus].vitality = vitality[i];
        CHECK(decision(zeus).hurt_level == expected[i]);
    }
}

static void test_reflex_fights_and_appraises_the_element(void)
{
    load_data();
    fresh();
    int zeus = add_deity("Greek", "Zeus", SIDE_PANTHEON, 100, 200);
    CHECK(decision(zeus).stance == STANCE_FIGHT);
    CHECK(decision(zeus).vulnerable_element == -1);
    add_deity("Celtic", "Dagda", SIDE_HORDE, 300, 200);
    CHECK(decision(zeus).stance == STANCE_FIGHT);
    CHECK(decision(zeus).vulnerable_element == ELEMENT_LIGHTNING);
    fresh();
    zeus = add_deity("Greek", "Zeus", SIDE_PANTHEON, 100, 200);
    add_deity("Greek", "Zeus", SIDE_HORDE, 300, 200);
    CHECK(decision(zeus).vulnerable_element == ELEMENT_KINETIC);
}

static void test_reflex_retreats_only_toward_a_healer(void)
{
    load_data();
    fresh();
    int zeus = add_deity("Greek", "Zeus", SIDE_PANTHEON, 100, 200);
    add_deity("Celtic", "Dagda", SIDE_HORDE, 300, 200);
    battle.combatants[zeus].vitality = 10;
    CHECK(decision(zeus).stance == STANCE_FIGHT);
    add_deity("Greek", "Apollo", SIDE_PANTHEON, 100, 300);
    CHECK(decision(zeus).stance == STANCE_RETREAT);
}

static void test_reflex_supports_a_near_death_ally(void)
{
    load_data();
    fresh();
    int apollo = add_deity("Greek", "Apollo", SIDE_PANTHEON, 100, 200);
    int zeus = add_deity("Greek", "Zeus", SIDE_PANTHEON, 200, 200);
    battle.combatants[zeus].vitality = 10;
    CHECK(decision(apollo).stance == STANCE_SUPPORT);
    battle.combatants[zeus].vitality = 20;
    CHECK(decision(apollo).stance == STANCE_FIGHT);
    battle.combatants[zeus].vitality = 10;
    destroy_slot(apollo, "hand_left");
    CHECK(decision(apollo).stance == STANCE_FIGHT);
}

static void test_reflex_collects_when_wounded_and_pickup_is_nearer(void)
{
    load_data();
    fresh();
    int zeus = add_deity("Greek", "Zeus", SIDE_PANTHEON, 300, 200);
    add_deity("Celtic", "Dagda", SIDE_HORDE, 450, 200);
    battle.combatants[zeus].vitality = 40;
    battle.pickups[0] = (FloorObject){0, 200 * PX, 200 * PX, 0};
    battle.pickup_count = 1;
    CHECK(decision(zeus).stance == STANCE_COLLECT);
    battle.pickups[0].x_millipx = 100 * PX;
    CHECK(decision(zeus).stance == STANCE_FIGHT);
    battle.pickups[0].x_millipx = 200 * PX;
    battle.combatants[zeus].vitality = 60;
    CHECK(decision(zeus).stance == STANCE_FIGHT);
    battle.combatants[zeus].vitality = 40;
    battle.pickups[0].respawn_in = 1;
    CHECK(decision(zeus).stance == STANCE_FIGHT);
}

static int decisions_logged(int combatants)
{
    int count = 0;
    for (int e = 0; e < battle.log_count; e++) count += battle.log[e].kind == EVENT_DECISION && battle.log[e].actor < combatants;
    return count;
}

static void test_decisions_are_scheduled_by_initiative(void)
{
    load_data();
    fresh();
    battle_setup(&battle, &data, 3);
    int count = battle.combatant_count;
    run(1, &reflex);
    CHECK(decisions_logged(count) == count);
    int previous_initiative = 1000, previous_actor = -1, seen = 0;
    for (int e = 0; e < battle.log_count; e++) {
        const Event *event = &battle.log[e];
        if (event->kind != EVENT_DECISION) continue;
        const Combatant *who = &battle.combatants[event->actor];
        int initiative = combatant_initiative(who, &data);
        CHECK(event->tick == 0 && event->decider == DECIDER_REFLEX);
        CHECK(initiative < previous_initiative || (initiative == previous_initiative && event->actor > previous_actor));
        CHECK(who->next_decision_tick == interval_for(who));
        CHECK(event->amount == (int)who->decision.stance && event->detail == (int)who->decision.hurt_level &&
              event->element == who->decision.vulnerable_element);
        previous_initiative = initiative;
        previous_actor = event->actor;
        seen++;
    }
    CHECK(seen == count);
}

static void test_cadence_follows_initiative(void)
{
    load_data();
    fresh();
    int zeus = add_deity("Greek", "Zeus", SIDE_PANTHEON, 100, 100);
    int dagda = add_deity("Celtic", "Dagda", SIDE_HORDE, 1400, 500);
    CHECK(combatant_initiative(&battle.combatants[zeus], &data) == 2);
    CHECK(combatant_initiative(&battle.combatants[dagda], &data) == 1);
    run(DECISION_INTERVAL_K + 1, &reflex);
    CHECK(decision_events(zeus) == 3);
    CHECK(decision_events(dagda) == 2);
}

static bool pending_decide(Decider *self, const Battle *b, const GameData *d, int c, Decision *out)
{
    (void)self, (void)b, (void)d, (void)c, (void)out;
    return false;
}

static bool retreat_decide(Decider *self, const Battle *b, const GameData *d, int c, Decision *out)
{
    (void)self, (void)b, (void)d, (void)c;
    *out = (Decision){STANCE_RETREAT, HURT_UNHURT, -1};
    return true;
}

static void test_pending_decider_keeps_combatant_due_and_uses_reflex_once(void)
{
    load_data();
    fresh();
    battle_setup(&battle, &data, 3);
    int count = battle.combatant_count;
    Decider pending = {pending_decide, DECIDER_LAYA};
    run(2, &pending);
    CHECK(decisions_logged(count) == count);
    for (int c = 0; c < count; c++) {
        CHECK(battle.combatants[c].decision.stance != STANCE_NONE);
        CHECK(battle.combatants[c].next_decision_tick == 0);
    }
    for (int e = 0; e < battle.log_count; e++) CHECK(battle.log[e].kind != EVENT_DECISION || battle.log[e].decider == DECIDER_REFLEX);
    Decider answers = {retreat_decide, DECIDER_LAYA};
    run(1, &answers);
    CHECK(decisions_logged(count) == 2 * count);
    for (int c = 0; c < count; c++) {
        CHECK(battle.combatants[c].decision.stance == STANCE_RETREAT);
        CHECK(battle.combatants[c].next_decision_tick > 2);
    }
    int laya = 0;
    for (int e = 0; e < battle.log_count; e++) laya += battle.log[e].kind == EVENT_DECISION && battle.log[e].actor < count && battle.log[e].decider == DECIDER_LAYA;
    CHECK(laya == count);
}

static void test_null_decider_changes_nothing(void)
{
    load_data();
    fresh();
    battle_setup(&battle, &data, 3);
    run(5, NULL);
    CHECK(battle.log_count == 0);
    for (int c = 0; c < battle.combatant_count; c++) {
        CHECK(battle.combatants[c].decision.stance == STANCE_NONE);
        for (int s = 0; s < battle.combatants[c].body.slot_count; s++) CHECK(battle.combatants[c].parts[s].order.kind == ORDER_IDLE);
    }
}

static void test_fight_closes_to_the_preferred_range_and_attacks(void)
{
    load_data();
    fresh();
    int zeus = add_deity("Greek", "Zeus", SIDE_PANTHEON, 100, 200);
    int apollo = add_deity("Greek", "Apollo", SIDE_HORDE, 250, 200);
    set_decision(zeus, STANCE_FIGHT, HURT_UNHURT, ELEMENT_KINETIC);
    tactics(zeus);
    check_order(order(zeus, "legs"), ORDER_MOVE_TO, -1, 250, 200);
    check_order(order(zeus, "hand_left"), ORDER_STRIKE, apollo, 0, 0);
    check_order(order(zeus, "hand_right"), ORDER_CAST, apollo, 0, 0);
    set_decision(zeus, STANCE_FIGHT, HURT_UNHURT, ELEMENT_LIGHTNING);
    tactics(zeus);
    CHECK(order(zeus, "legs").kind == ORDER_IDLE);
    battle.combatants[zeus].x_millipx = 230 * PX;
    set_decision(zeus, STANCE_FIGHT, HURT_UNHURT, ELEMENT_KINETIC);
    tactics(zeus);
    CHECK(order(zeus, "legs").kind == ORDER_IDLE);
    CHECK(order(zeus, "hand_left").kind == ORDER_STRIKE);
}

static void test_support_block_and_spawn_orders(void)
{
    load_data();
    fresh();
    int isis = add_deity("Egyptian", "Isis", SIDE_PANTHEON, 100, 200);
    int zeus = add_deity("Greek", "Zeus", SIDE_PANTHEON, 150, 200);
    add_deity("Greek", "Apollo", SIDE_HORDE, 400, 200);
    battle.combatants[zeus].vitality = 1;
    set_decision(isis, STANCE_FIGHT, HURT_UNHURT, -1);
    tactics(isis);
    CHECK(order(isis, "hand_left").kind == ORDER_BLOCK);
    check_order(order(isis, "hand_right"), ORDER_SUPPORT, zeus, 0, 0);
    check_order(order(isis, "legs"), ORDER_SUPPORT, zeus, 0, 0);
    battle.combatants[zeus].vitality = 60;
    tactics(isis);
    check_order(order(isis, "hand_right"), ORDER_SUPPORT, isis, 0, 0);
    fresh();
    int morrigan = add_deity("Celtic", "Morrigan", SIDE_PANTHEON, 100, 200);
    set_decision(morrigan, STANCE_FIGHT, HURT_UNHURT, -1);
    tactics(morrigan);
    CHECK(order(morrigan, "ravens").kind == ORDER_SPAWN);
}

static void test_mover_with_ability_moves_before_it_supports(void)
{
    load_data();
    fresh();
    int isis = add_deity("Egyptian", "Isis", SIDE_PANTHEON, 100, 200);
    int zeus = add_deity("Greek", "Zeus", SIDE_PANTHEON, 250, 200);
    battle.combatants[zeus].vitality = 1;
    set_decision(isis, STANCE_SUPPORT, HURT_UNHURT, -1);
    tactics(isis);
    check_order(order(isis, "legs"), ORDER_MOVE_TO, -1, 250, 200);
    battle.combatants[zeus].x_millipx = 150 * PX;
    tactics(isis);
    check_order(order(isis, "legs"), ORDER_SUPPORT, zeus, 0, 0);
}

static void test_support_stance_sends_legs_to_the_hurt_ally(void)
{
    load_data();
    fresh();
    int apollo = add_deity("Greek", "Apollo", SIDE_PANTHEON, 100, 200);
    int zeus = add_deity("Greek", "Zeus", SIDE_PANTHEON, 300, 200);
    battle.combatants[zeus].vitality = 1;
    set_decision(apollo, STANCE_SUPPORT, HURT_UNHURT, -1);
    tactics(apollo);
    check_order(order(apollo, "legs"), ORDER_MOVE_TO, -1, 300, 200);
    battle.combatants[zeus].x_millipx = 150 * PX;
    tactics(apollo);
    CHECK(order(apollo, "legs").kind == ORDER_IDLE);
}

static void test_retreat_flees_or_runs_to_a_healer(void)
{
    load_data();
    fresh();
    int zeus = add_deity("Greek", "Zeus", SIDE_PANTHEON, 100, 200);
    add_deity("Celtic", "Dagda", SIDE_HORDE, 300, 200);
    set_decision(zeus, STANCE_RETREAT, HURT_NEAR_DEATH, -1);
    tactics(zeus);
    check_order(order(zeus, "legs"), ORDER_MOVE_TO, -1, 36, 200);
    int apollo = add_deity("Greek", "Apollo", SIDE_PANTHEON, 100, 400);
    tactics(zeus);
    check_order(order(zeus, "legs"), ORDER_MOVE_TO, -1, 100, 400);
    battle.combatants[1].alive = battle.combatants[apollo].alive = false;
    tactics(zeus);
    CHECK(order(zeus, "legs").kind == ORDER_IDLE);
}

static void test_collect_walks_to_the_pickup_without_a_pick_up_order(void)
{
    load_data();
    fresh();
    int zeus = add_deity("Greek", "Zeus", SIDE_PANTHEON, 100, 200);
    battle.pickups[0] = (FloorObject){0, 300 * PX, 200 * PX, 0};
    battle.pickup_count = 1;
    set_decision(zeus, STANCE_COLLECT, HURT_WOUNDED, -1);
    tactics(zeus);
    check_order(order(zeus, "legs"), ORDER_MOVE_TO, -1, 300, 200);
    CHECK(order(zeus, "torso").kind == ORDER_IDLE);
    battle.combatants[zeus].x_millipx = 300 * PX;
    tactics(zeus);
    check_order(order(zeus, "legs"), ORDER_MOVE_TO, -1, 300, 200);
    CHECK(order(zeus, "torso").kind == ORDER_IDLE);
}

static void test_seek_when_nothing_is_perceived(void)
{
    load_data();
    fresh();
    int zeus = add_deity("Greek", "Zeus", SIDE_PANTHEON, 100, 200);
    add_deity("Greek", "Apollo", SIDE_HORDE, 700, 200);
    set_decision(zeus, STANCE_FIGHT, HURT_UNHURT, -1);
    tactics(zeus);
    check_order(order(zeus, "legs"), ORDER_MOVE_TO, -1, 700, 200);
    CHECK(order(zeus, "hand_left").kind == ORDER_IDLE);
    CHECK(order(zeus, "hand_right").kind == ORDER_IDLE);
}

static void build_trap_duel(int trap_count)
{
    fresh();
    add_deity("Greek", "Zeus", SIDE_PANTHEON, 88, 168);
    int apollo = add_deity("Greek", "Apollo", SIDE_HORDE, 248, 168);
    Combatant *dummy = &battle.combatants[apollo];
    for (int s = 0; s < dummy->body.slot_count; s++)
        if (strcmp(dummy->body.slots[s].id, "torso") != 0 && strcmp(dummy->body.slots[s].id, "heart") != 0) dummy->parts[s].integrity = 0;
    int snare = -1;
    for (int t = 0; t < data.trap_count; t++)
        if (strcmp(data.traps[t].id, "frost_snare") == 0) snare = t;
    CHECK(snare >= 0 && data.traps[snare].radius == 20);
    battle.traps[0] = (FloorObject){snare, 168 * PX, 168 * PX, 0};
    battle.trap_count = trap_count;
}

static int64_t closest_approach_to_trap(bool *reaches_apollo)
{
    int64_t closest = INT64_MAX;
    *reaches_apollo = false;
    for (int t = 0; t < 200; t++) {
        battle_step(&battle, &data, &reflex);
        const Combatant *zeus = &battle.combatants[0];
        int64_t dx = zeus->x_millipx - battle.traps[0].x_millipx, dy = zeus->y_millipx - battle.traps[0].y_millipx;
        int64_t distance = isqrt(dx * dx + dy * dy);
        if (distance < closest) closest = distance;
        if (combatant_in_reach(zeus, &battle.combatants[1], 24)) *reaches_apollo = true;
    }
    return closest;
}

static void test_seen_trap_is_walked_around(void)
{
    load_data();
    bool reaches_apollo;
    build_trap_duel(1);
    CHECK(closest_approach_to_trap(&reaches_apollo) > 20 * PX);
    CHECK(reaches_apollo);
    build_trap_duel(0);
    CHECK(closest_approach_to_trap(&reaches_apollo) < 11 * PX);
}

static void test_tactics_are_deterministic(void)
{
    load_data();
    char *first[2];
    for (int round = 0; round < 2; round++) {
        fresh();
        battle_setup(&battle, &data, 1);
        run(200, &reflex);
        first[round] = battle_save_json(&battle, &data);
    }
    CHECK(strcmp(first[0], first[1]) == 0);
    free(first[0]);
    free(first[1]);
}

void run_decide_tests(void)
{
    test_initiative_sums_working_parts_floor_one();
    test_hurt_level_thresholds();
    test_reflex_fights_and_appraises_the_element();
    test_reflex_retreats_only_toward_a_healer();
    test_reflex_supports_a_near_death_ally();
    test_reflex_collects_when_wounded_and_pickup_is_nearer();
    test_decisions_are_scheduled_by_initiative();
    test_cadence_follows_initiative();
    test_pending_decider_keeps_combatant_due_and_uses_reflex_once();
    test_null_decider_changes_nothing();
    test_fight_closes_to_the_preferred_range_and_attacks();
    test_support_block_and_spawn_orders();
    test_mover_with_ability_moves_before_it_supports();
    test_support_stance_sends_legs_to_the_hurt_ally();
    test_retreat_flees_or_runs_to_a_healer();
    test_collect_walks_to_the_pickup_without_a_pick_up_order();
    test_seek_when_nothing_is_perceived();
    test_seen_trap_is_walked_around();
    test_tactics_are_deterministic();
    battle_free(&battle);
}
