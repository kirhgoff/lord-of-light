#include <ctype.h>
#include <stdlib.h>
#include <string.h>
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
