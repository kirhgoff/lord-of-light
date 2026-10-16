#include <string.h>
#include "check.h"
#include "../src/sim/decide.h"

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

static void test_decisions_are_scheduled_by_initiative(void)
{
    load_data();
    fresh();
    battle_setup(&battle, &data, 3);
    run(1, &reflex);
    CHECK(battle.log_count == battle.combatant_count);
    int previous_initiative = 1000, previous_actor = -1;
    for (int e = 0; e < battle.log_count; e++) {
        const Event *event = &battle.log[e];
        const Combatant *who = &battle.combatants[event->actor];
        int initiative = combatant_initiative(who, &data);
        CHECK(event->kind == EVENT_DECISION && event->tick == 0 && event->decider == DECIDER_REFLEX);
        CHECK(initiative < previous_initiative || (initiative == previous_initiative && event->actor > previous_actor));
        CHECK(who->next_decision_tick == interval_for(who));
        CHECK(event->amount == (int)who->decision.stance && event->detail == (int)who->decision.hurt_level &&
              event->element == who->decision.vulnerable_element);
        previous_initiative = initiative;
        previous_actor = event->actor;
    }
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
    Decider pending = {pending_decide, DECIDER_LAYA};
    run(1, &pending);
    for (int c = 0; c < battle.combatant_count; c++) {
        CHECK(battle.combatants[c].decision.stance != STANCE_NONE);
        CHECK(battle.combatants[c].next_decision_tick == 0);
    }
    CHECK(battle.log_count == battle.combatant_count);
    for (int e = 0; e < battle.log_count; e++) CHECK(battle.log[e].decider == DECIDER_REFLEX);
    run(1, &pending);
    CHECK(battle.log_count == battle.combatant_count);
    Decider answers = {retreat_decide, DECIDER_LAYA};
    run(1, &answers);
    CHECK(battle.log_count == 2 * battle.combatant_count);
    for (int e = battle.combatant_count; e < battle.log_count; e++) {
        CHECK(battle.log[e].decider == DECIDER_LAYA && battle.log[e].amount == STANCE_RETREAT);
        CHECK(battle.combatants[battle.log[e].actor].next_decision_tick > 2);
    }
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
    battle_free(&battle);
}
