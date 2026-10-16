#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cJSON.h"
#include "check.h"
#include "../src/sim/briefing.h"
#include "../src/sim/decide.h"

#define ERR_SIZE 256
#define PX MILLIPX_PER_PX

static GameData data;
static Battle battle;
static char text[BRIEFING_MAX + 1];

static void fresh(void)
{
    char err[ERR_SIZE];
    LOAD_OR_EXIT(load_game_data(&data, "data", err, ERR_SIZE), err);
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

static void move(int c, int x_px, int y_px)
{
    battle.combatants[c].x_millipx = x_px * PX;
    battle.combatants[c].y_millipx = y_px * PX;
}

static void briefing_of(int c)
{
    CHECK(briefing_write(&battle, &data, c, text, sizeof text) == (int)strlen(text));
}

static int zeus_with_company(int *dagda, int *apollo)
{
    fresh();
    int zeus = add_deity("Greek", "Zeus", SIDE_PANTHEON, 500, 200);
    *dagda = add_deity("Celtic", "Dagda", SIDE_HORDE, 580, 200);
    *apollo = add_deity("Greek", "Apollo", SIDE_PANTHEON, 500, 400);
    battle.combatants[*apollo].vitality = 40;
    battle.pickups[0] = (FloorObject){find_pickup(&data, "soma"), 100 * PX, 200 * PX, 0};
    battle.pickup_count = 1;
    return zeus;
}

static void test_close_briefing_is_exact(void)
{
    int dagda, apollo;
    int zeus = zeus_with_company(&dagda, &apollo);
    briefing_of(zeus);
    CHECK(strcmp(text,
                 "Zeus is unhurt. Zeus can strike with Kinetic and cast Lightning. Dagda, an enemy, is close to the east and unhurt. "
                 "Dagda resists Kinetic and is weak to Lightning. Apollo, an ally, is near to the south and wounded. "
                 "Apollo can heal. A healing pickup is far to the west.") == 0);
}

static void test_medium_enemy_shows_no_resistances(void)
{
    int dagda, apollo;
    int zeus = zeus_with_company(&dagda, &apollo);
    move(dagda, 700, 200);
    briefing_of(zeus);
    CHECK(strstr(text, "Dagda, an enemy, is near to the east and unhurt.") != NULL);
    CHECK(strstr(text, "resists") == NULL && strstr(text, "weak") == NULL);
}

static void test_far_enemy_is_anonymous(void)
{
    int dagda, apollo;
    int zeus = zeus_with_company(&dagda, &apollo);
    move(dagda, 100, 250);
    briefing_of(zeus);
    CHECK(strstr(text, "An enemy is far to the west.") != NULL);
    CHECK(strstr(text, "Dagda") == NULL && strstr(text, "resists") == NULL && strstr(text, "weak") == NULL);
}

static void test_briefing_has_no_digits(void)
{
    fresh();
    battle_setup(&battle, &data, 1);
    for (int c = 0; c < battle.combatant_count; c++) move(c, 500 + c * 5, 300 + c * 5);
    for (int c = 0; c < battle.combatant_count; c++) {
        briefing_of(c);
        CHECK(text[0] != '\0');
        for (const char *ch = text; *ch; ch++) CHECK(!isdigit((unsigned char)*ch) && *ch >= ' ' && *ch < 127);
    }
}

static void test_briefing_is_capped(void)
{
    fresh();
    int zeus = add_deity("Greek", "Zeus", SIDE_PANTHEON, 500, 200);
    for (int i = 1; i < MAX_COMBATANTS; i++)
        add_deity(i % 2 ? "Celtic" : "Greek", i % 2 ? "Dagda" : "Apollo", i % 2 ? SIDE_HORDE : SIDE_PANTHEON, 460 + (i % 8) * 10, 160 + (i / 8) * 10);
    briefing_of(zeus);
    CHECK(strlen(text) <= BRIEFING_MAX && strlen(text) > 1100);
    CHECK(text[strlen(text) - 1] == '.' && strncmp(text, "Zeus is unhurt.", 15) == 0);
}

static void test_a_full_briefing_stops_at_the_first_sentence_that_does_not_fit(void)
{
    for (int others = 30; others < MAX_COMBATANTS - 1; others++) {
        fresh();
        int zeus = add_deity("Greek", "Zeus", SIDE_PANTHEON, 500, 200);
        for (int i = 1; i <= others; i++)
            add_deity("Celtic", "Dagda", SIDE_HORDE, 460 + (i % 8) * 10, 160 + (i / 8) * 10);
        add_deity("Greek", "Apollo", SIDE_PANTHEON, 500, 300);
        briefing_of(zeus);
        CHECK(strlen(text) <= BRIEFING_MAX);
        if (strlen(text) > 1100) CHECK(strstr(text, "Apollo") == NULL);
    }
}

static void test_equal_resistances_say_nothing(void)
{
    fresh();
    int zeus = add_deity("Greek", "Zeus", SIDE_PANTHEON, 500, 200);
    add_deity("Greek", "Poseidon", SIDE_HORDE, 580, 200);
    briefing_of(zeus);
    CHECK(strstr(text, "Poseidon, an enemy, is close to the east and unhurt.") != NULL);
    CHECK(strstr(text, "resists") == NULL && strstr(text, "weak") == NULL);
}

static void test_monster_subject(void)
{
    fresh();
    int zeus = add_deity("Greek", "Zeus", SIDE_PANTHEON, 500, 200);
    Body body = data.body_plans[find_body_plan(&data, "biped")].body;
    for (int s = 0; s < body.slot_count; s++)
        for (int p = 0; body.slots[s].part < 0 && p < data.part_count; p++)
            if (fills_horde_slot(&data, p, &body.slots[s])) body.slots[s].part = p;
    battle_add_combatant(&battle, &data, "biped", SIDE_HORDE, &body, 580 * PX, 200 * PX);
    briefing_of(zeus);
    CHECK(strstr(text, "The biped, an enemy, is close to the east and unhurt.") != NULL);
    briefing_of(1);
    CHECK(strncmp(text, "The biped is unhurt.", 20) == 0);
}

static void test_unarmed_combatant_cannot_attack(void)
{
    fresh();
    int zeus = add_deity("Greek", "Zeus", SIDE_PANTHEON, 500, 200);
    Combatant *who = &battle.combatants[zeus];
    who->parts[find_body_slot(&who->body, "hand_left")].integrity = 0;
    who->parts[find_body_slot(&who->body, "hand_right")].integrity = 0;
    briefing_of(zeus);
    CHECK(strcmp(text, "Zeus is unhurt. Zeus cannot attack.") == 0);
}

void run_briefing_tests(void)
{
    test_close_briefing_is_exact();
    test_medium_enemy_shows_no_resistances();
    test_far_enemy_is_anonymous();
    test_briefing_has_no_digits();
    test_briefing_is_capped();
    test_a_full_briefing_stops_at_the_first_sentence_that_does_not_fit();
    test_equal_resistances_say_nothing();
    test_monster_subject();
    test_unarmed_combatant_cannot_attack();
    battle_free(&battle);
}

static const char *const CANNED_REPLY_FORMAT =
    "{\"model\":\"laya-rl-agent\",\"answers\":{"
    "\"hurt\":{\"type\":\"choice\",\"choice\":\"unhurt\",\"probabilities\":{\"unhurt\":0.91,\"wounded\":0.05},\"x_jev_confidence\":0.88},"
    "\"element\":{\"type\":\"choice\",\"choice\":\"Lightning\",\"probabilities\":{\"Kinetic\":0.07,\"Lightning\":0.93},\"x_jev_confidence\":0.86},"
    "\"stance\":{\"type\":\"choice\",\"choice\":\"%s\",\"probabilities\":{\"fight the nearest enemy\":0.61,\"retreat from the enemy\":0.2,"
    "\"collect the pickup\":0.19},\"x_jev_confidence\":%s}},"
    "\"usage\":{\"input_tokens\":400,\"truncated\":%s}}";

static char reply[2048];

static void canned_reply(const char *choice, const char *confidence, const char *truncated)
{
    snprintf(reply, sizeof reply, CANNED_REPLY_FORMAT, choice, confidence, truncated);
}

static char *request_for(int c, LayaQuestions *asked)
{
    briefing_of(c);
    return laya_request_json(&battle, &data, c, text, asked);
}

static const cJSON *question(const cJSON *request, const char *id)
{
    return cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(request, "questions"), id);
}

static void check_criteria(const cJSON *request, const char *id, const char *const *keys, int count)
{
    const cJSON *criteria = cJSON_GetObjectItemCaseSensitive(question(request, id), "criteria");
    CHECK(cJSON_GetArraySize(criteria) == count);
    for (int k = 0; k < count; k++) CHECK(strcmp(cJSON_GetArrayItem(criteria, k)->string, keys[k]) == 0);
}

static void test_request_for_the_worked_example(void)
{
    int dagda, apollo;
    int zeus = zeus_with_company(&dagda, &apollo);
    LayaQuestions asked;
    char *json = request_for(zeus, &asked);
    cJSON *request = cJSON_Parse(json);
    CHECK(request != NULL);
    const cJSON *state = cJSON_GetObjectItemCaseSensitive(request, "state");
    CHECK(cJSON_IsString(state) && strcmp(state->valuestring, text) == 0);
    const cJSON *questions = cJSON_GetObjectItemCaseSensitive(request, "questions");
    CHECK(cJSON_GetArraySize(questions) == 3);
    CHECK(strcmp(cJSON_GetArrayItem(questions, 0)->string, "hurt") == 0 && strcmp(cJSON_GetArrayItem(questions, 1)->string, "element") == 0 &&
          strcmp(cJSON_GetArrayItem(questions, 2)->string, "stance") == 0);
    check_criteria(request, "hurt", (const char *const[]){"unhurt", "wounded", "badly wounded", "near death"}, 4);
    check_criteria(request, "element", (const char *const[]){"Kinetic", "Lightning"}, 2);
    check_criteria(request, "stance", (const char *const[]){"fight the nearest enemy", "retreat from the enemy", "collect the pickup"}, 3);
    CHECK(strcmp(cJSON_GetObjectItemCaseSensitive(question(request, "element"), "instructions")->valuestring, "Which Element hurts Dagda most?") == 0);
    CHECK(strcmp(cJSON_GetObjectItemCaseSensitive(question(request, "hurt"), "instructions")->valuestring, "How hurt is Zeus?") == 0);
    CHECK(strstr(cJSON_GetObjectItemCaseSensitive(question(request, "stance"), "instructions")->valuestring, "Zeus fights unless near death") != NULL);
    CHECK(asked.element_count == 2 && asked.elements[0] == ELEMENT_KINETIC && asked.elements[1] == ELEMENT_LIGHTNING);
    cJSON_Delete(request);
    free(json);
}

static void test_a_lone_combatant_is_offered_only_fight_and_retreat(void)
{
    fresh();
    int zeus = add_deity("Greek", "Zeus", SIDE_PANTHEON, 500, 200);
    LayaQuestions asked;
    char *json = request_for(zeus, &asked);
    cJSON *request = cJSON_Parse(json);
    check_criteria(request, "stance", (const char *const[]){"fight the nearest enemy", "retreat from the enemy"}, 2);
    CHECK(question(request, "element") == NULL && asked.element_count == 0 && asked.fixed_element == -1);
    cJSON_Delete(request);
    free(json);
}

static void test_a_healer_with_a_hurt_ally_may_support(void)
{
    fresh();
    int apollo = add_deity("Greek", "Apollo", SIDE_PANTHEON, 500, 200);
    int zeus = add_deity("Greek", "Zeus", SIDE_PANTHEON, 540, 200);
    add_deity("Celtic", "Dagda", SIDE_HORDE, 580, 200);
    LayaQuestions asked;
    char *json = request_for(apollo, &asked);
    CHECK(!asked.stance_offered[STANCE_SUPPORT]);
    free(json);
    battle.combatants[zeus].vitality = 10;
    json = request_for(apollo, &asked);
    cJSON *request = cJSON_Parse(json);
    check_criteria(request, "stance", (const char *const[]){"fight the nearest enemy", "retreat from the enemy", "support the hurt ally"}, 3);
    CHECK(question(request, "element") == NULL && asked.element_count == 0 && asked.fixed_element == ELEMENT_RADIANCE);
    cJSON_Delete(request);
    free(json);
    json = request_for(zeus, &asked);
    CHECK(!asked.stance_offered[STANCE_SUPPORT]);
    free(json);
}

static void test_a_single_element_needs_no_element_question(void)
{
    fresh();
    int agni = add_deity("Hindu", "Agni", SIDE_PANTHEON, 500, 200);
    add_deity("Celtic", "Dagda", SIDE_HORDE, 580, 200);
    LayaQuestions asked;
    char *json = request_for(agni, &asked);
    cJSON *request = cJSON_Parse(json);
    CHECK(question(request, "element") == NULL && asked.element_count == 0 && asked.fixed_element == ELEMENT_FIRE);
    cJSON_Delete(request);
    free(json);
}

static Event decide_reply(int zeus, const LayaQuestions *asked, const char *text_reply, Fallback failure, int latency_ms, bool *truncated)
{
    return laya_decision(&battle, &data, zeus, asked, text_reply, failure, latency_ms, truncated);
}

static void test_a_good_reply_becomes_a_laya_decision(void)
{
    int dagda, apollo;
    int zeus = zeus_with_company(&dagda, &apollo);
    LayaQuestions asked;
    free(request_for(zeus, &asked));
    canned_reply("fight the nearest enemy", "0.415", "false");
    bool truncated = true;
    Event event = decide_reply(zeus, &asked, reply, FALLBACK_NONE, 612, &truncated);
    CHECK(!truncated);
    CHECK(event.kind == EVENT_DECISION && event.actor == zeus && event.actor_part == -1 && event.target == -1 && event.target_part == -1);
    CHECK(event.decider == DECIDER_LAYA && event.amount == STANCE_FIGHT && event.detail == HURT_UNHURT && event.element == ELEMENT_LIGHTNING);
    CHECK(event.confidence_permille == 415 && event.latency_ms == 612 && event.fallback == FALLBACK_NONE);
    const int expected[STANCE_COUNT] = {0, 610, 200, 0, 190};
    CHECK(memcmp(event.stance_permille, expected, sizeof expected) == 0);
    canned_reply("collect the pickup", "0.415", "false");
    CHECK(decide_reply(zeus, &asked, reply, FALLBACK_NONE, 1, &truncated).amount == STANCE_COLLECT);
}

static void test_low_confidence_keeps_the_reflex_stance(void)
{
    int dagda, apollo;
    int zeus = zeus_with_company(&dagda, &apollo);
    LayaQuestions asked;
    free(request_for(zeus, &asked));
    canned_reply("retreat from the enemy", "0.1", "false");
    bool truncated;
    Event event = decide_reply(zeus, &asked, reply, FALLBACK_NONE, 300, &truncated);
    CHECK(event.decider == DECIDER_REFLEX && event.fallback == FALLBACK_LOW_CONFIDENCE && event.amount == STANCE_FIGHT);
    CHECK(event.confidence_permille == 100 && event.latency_ms == 300 && event.element == ELEMENT_LIGHTNING);
    CHECK(event.stance_permille[STANCE_RETREAT] == 200);
}

static void test_unusable_replies_fall_back_to_the_reflex(void)
{
    int dagda, apollo;
    int zeus = zeus_with_company(&dagda, &apollo);
    LayaQuestions asked;
    free(request_for(zeus, &asked));
    canned_reply("fight the nearest", "0.9", "false");
    char *prefix_only = strdup(reply);
    canned_reply("charge", "0.9", "false");
    char *no_stance = strdup(reply);
    char *cut = strstr(no_stance, ",\"stance\"");
    CHECK(cut != NULL);
    strcpy(cut, "}}");
    const char *bad[] = {"<html>", "{\"answers\":5}", "{}", no_stance, reply, prefix_only};
    for (int i = 0; i < 6; i++) {
        bool truncated;
        Event event = decide_reply(zeus, &asked, bad[i], FALLBACK_NONE, 77, &truncated);
        CHECK(event.decider == DECIDER_REFLEX && event.fallback == FALLBACK_MALFORMED && event.latency_ms == 77);
        CHECK(event.amount == STANCE_FIGHT && event.confidence_permille == 0);
    }
    free(no_stance);
    free(prefix_only);
}

static void test_transport_failures_fall_back_to_the_reflex(void)
{
    int dagda, apollo;
    int zeus = zeus_with_company(&dagda, &apollo);
    LayaQuestions asked;
    free(request_for(zeus, &asked));
    bool truncated;
    Event event = decide_reply(zeus, &asked, NULL, FALLBACK_TIMEOUT, 8000, &truncated);
    CHECK(event.decider == DECIDER_REFLEX && event.fallback == FALLBACK_TIMEOUT && event.latency_ms == 8000 && event.amount == STANCE_FIGHT);
}

static void test_truncation_is_reported(void)
{
    int dagda, apollo;
    int zeus = zeus_with_company(&dagda, &apollo);
    LayaQuestions asked;
    free(request_for(zeus, &asked));
    canned_reply("fight the nearest enemy", "0.5", "true");
    bool truncated = false;
    decide_reply(zeus, &asked, reply, FALLBACK_NONE, 1, &truncated);
    CHECK(truncated);
}

static void test_missing_confidence_and_probabilities_count_as_zero(void)
{
    int dagda, apollo;
    int zeus = zeus_with_company(&dagda, &apollo);
    LayaQuestions asked;
    free(request_for(zeus, &asked));
    const char *bare =
        "{\"answers\":{\"hurt\":{\"choice\":\"unhurt\"},\"element\":{\"choice\":\"Kinetic\"},\"stance\":{\"choice\":\"fight the nearest enemy\"}}}";
    bool truncated;
    Event event = decide_reply(zeus, &asked, bare, FALLBACK_NONE, 5, &truncated);
    CHECK(event.fallback == FALLBACK_LOW_CONFIDENCE && event.confidence_permille == 0 && event.element == ELEMENT_KINETIC);
    for (int s = 0; s < STANCE_COUNT; s++) CHECK(event.stance_permille[s] == 0);
}

static void test_out_of_range_numbers_are_clamped(void)
{
    int dagda, apollo;
    int zeus = zeus_with_company(&dagda, &apollo);
    LayaQuestions asked;
    free(request_for(zeus, &asked));
    const char *wild =
        "{\"answers\":{\"hurt\":{\"choice\":\"unhurt\"},\"element\":{\"choice\":\"Kinetic\"},\"stance\":{\"choice\":\"fight the nearest enemy\","
        "\"probabilities\":{\"fight the nearest enemy\":1e400,\"retreat from the enemy\":-1e400,\"collect the pickup\":5},\"x_jev_confidence\":1e400}}}";
    bool truncated;
    Event event = decide_reply(zeus, &asked, wild, FALLBACK_NONE, 5, &truncated);
    CHECK(event.decider == DECIDER_LAYA && event.confidence_permille == 1000);
    CHECK(event.stance_permille[STANCE_FIGHT] == 1000 && event.stance_permille[STANCE_RETREAT] == 0 && event.stance_permille[STANCE_COLLECT] == 1000);
}

static void test_an_oversized_reply_is_malformed(void)
{
    int dagda, apollo;
    int zeus = zeus_with_company(&dagda, &apollo);
    LayaQuestions asked;
    free(request_for(zeus, &asked));
    canned_reply("fight the nearest enemy", "0.9", "false");
    size_t padding = LAYA_REPLY_MAX + 1;
    char *huge = malloc(strlen(reply) + padding + 1);
    memcpy(huge, reply, strlen(reply));
    memset(huge + strlen(reply), ' ', padding);
    huge[strlen(reply) + padding] = '\0';
    bool truncated;
    CHECK(decide_reply(zeus, &asked, huge, FALLBACK_NONE, 9, &truncated).fallback == FALLBACK_MALFORMED);
    huge[LAYA_REPLY_MAX - 100] = '\0';
    CHECK(decide_reply(zeus, &asked, huge, FALLBACK_NONE, 9, &truncated).fallback == FALLBACK_NONE);
    free(huge);
}

void run_briefing_request_tests(void)
{
    test_request_for_the_worked_example();
    test_a_lone_combatant_is_offered_only_fight_and_retreat();
    test_a_healer_with_a_hurt_ally_may_support();
    test_a_single_element_needs_no_element_question();
    test_a_good_reply_becomes_a_laya_decision();
    test_low_confidence_keeps_the_reflex_stance();
    test_unusable_replies_fall_back_to_the_reflex();
    test_transport_failures_fall_back_to_the_reflex();
    test_truncation_is_reported();
    test_missing_confidence_and_probabilities_count_as_zero();
    test_out_of_range_numbers_are_clamped();
    test_an_oversized_reply_is_malformed();
    battle_free(&battle);
}
