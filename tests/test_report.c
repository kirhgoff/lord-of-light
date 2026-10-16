#include <stdio.h>
#include <string.h>
#include "check.h"
#include "../src/view/report.h"

#define ERR_SIZE 256
#define PX MILLIPX_PER_PX

static GameData data;
static Battle battle;

static int add(const char *name, Side side)
{
    return battle_add_combatant(&battle, &data, name, side, &data.pantheons[0].deities[0].body, 100 * PX, 100 * PX);
}

static void fresh(void)
{
    char err[ERR_SIZE];
    if (data.part_count == 0) LOAD_OR_EXIT(load_game_data(&data, "data", err, ERR_SIZE), err);
    battle_free(&battle);
    memset(&battle, 0, sizeof battle);
    battle.rng = 1;
}

static void log_at(int tick, Event event)
{
    battle.tick = tick;
    battle_log_event(&battle, event);
}

static bool has(const char *line, const char *text)
{
    return strstr(line, text) != NULL;
}

static void test_labels_number_shared_names(void)
{
    fresh();
    for (int i = 0; i < 3; i++) add("floater", SIDE_HORDE);
    add("Agni", SIDE_PANTHEON);
    char label[LABEL_SIZE];
    for (int c = 0; c < 3; c++) {
        char expected[LABEL_SIZE];
        snprintf(expected, sizeof expected, "floater %d", c + 1);
        combatant_label(&battle, c, label, sizeof label);
        CHECK(strcmp(label, expected) == 0);
    }
    combatant_label(&battle, 3, label, sizeof label);
    CHECK(strcmp(label, "Agni") == 0);
    combatant_label(&battle, 9, label, sizeof label);
    CHECK(strcmp(label, "?") == 0);
}

static void test_event_lines_per_kind(void)
{
    fresh();
    int agni = add("Agni", SIDE_PANTHEON);
    int imp = add("imp", SIDE_HORDE);
    battle.pickup_count = battle.trap_count = 1;
    char line[EVENT_LINE_SIZE];

    Event hit = {.kind = EVENT_HIT, .actor = agni, .actor_part = 0, .target = imp, .target_part = 0, .element = ELEMENT_FIRE, .amount = 7, .detail = 3};
    log_at(250, hit);
    CHECK(event_line(&battle, &data, &battle.log[0], line, sizeof line) > 0);
    CHECK(has(line, "12.5s") && has(line, "Agni's") && has(line, "hits imp's") && has(line, "for 7 fire") && !has(line, "destroyed"));

    hit.detail = 0;
    log_at(250, hit);
    event_line(&battle, &data, &battle.log[1], line, sizeof line);
    CHECK(has(line, " - destroyed"));

    hit.actor = -1;
    log_at(250, hit);
    event_line(&battle, &data, &battle.log[2], line, sizeof line);
    CHECK(strstr(line, "A Trap hits imp's") != NULL);

    log_at(0, (Event){.kind = EVENT_EFFECT, .actor = agni, .actor_part = -1, .target = imp, .element = ELEMENT_VENOM, .amount = 4, .detail = EFFECT_DOT});
    event_line(&battle, &data, &battle.log[3], line, sizeof line);
    CHECK(has(line, "imp takes 4 venom"));

    log_at(0, (Event){.kind = EVENT_EFFECT, .actor = agni, .target = imp, .amount = 9, .detail = EFFECT_HEAL});
    event_line(&battle, &data, &battle.log[4], line, sizeof line);
    CHECK(has(line, "Agni heals imp for 9"));

    log_at(0, (Event){.kind = EVENT_EFFECT, .actor = agni, .target = imp, .detail = EFFECT_SLOW});
    event_line(&battle, &data, &battle.log[5], line, sizeof line);
    CHECK(has(line, "imp is slowed by Agni"));

    log_at(0, (Event){.kind = EVENT_EFFECT, .actor = agni, .target = imp, .detail = EFFECT_CHARM});
    event_line(&battle, &data, &battle.log[6], line, sizeof line);
    CHECK(has(line, "imp is charmed by Agni"));

    log_at(0, (Event){.kind = EVENT_EFFECT, .actor = agni, .target = imp, .amount = 2, .detail = EFFECT_STRIP});
    event_line(&battle, &data, &battle.log[7], line, sizeof line);
    CHECK(has(line, "imp loses 2 buffs"));

    log_at(0, (Event){.kind = EVENT_PICKUP, .actor = agni, .detail = 0});
    event_line(&battle, &data, &battle.log[8], line, sizeof line);
    CHECK(has(line, "Agni takes ") && !has(line, "?"));

    log_at(0, (Event){.kind = EVENT_TRAP, .target = imp, .detail = 0});
    event_line(&battle, &data, &battle.log[9], line, sizeof line);
    CHECK(has(line, "imp triggers ") && !has(line, "?"));

    log_at(0, (Event){.kind = EVENT_DEATH, .actor = agni, .actor_part = 0, .target = imp, .element = ELEMENT_FIRE});
    event_line(&battle, &data, &battle.log[10], line, sizeof line);
    CHECK(has(line, "imp dies to Agni's") && has(line, "(fire)"));

    log_at(0, (Event){.kind = EVENT_DEATH, .actor = -1, .target = imp, .element = ELEMENT_VOID});
    event_line(&battle, &data, &battle.log[11], line, sizeof line);
    CHECK(has(line, "imp dies (void)"));

    log_at(0, (Event){.kind = EVENT_SPAWN, .actor = agni, .target = imp});
    event_line(&battle, &data, &battle.log[12], line, sizeof line);
    CHECK(has(line, "Agni spawns imp"));
}

static void test_decision_lines_show_laya_and_fallbacks_only(void)
{
    fresh();
    int agni = add("Agni", SIDE_PANTHEON);
    char line[EVENT_LINE_SIZE];
    Event reflex = {.kind = EVENT_DECISION, .actor = agni, .amount = STANCE_FIGHT, .decider = DECIDER_REFLEX, .fallback = FALLBACK_NONE};
    log_at(0, reflex);
    CHECK(event_line(&battle, &data, &battle.log[0], line, sizeof line) == 0);
    reflex.fallback = FALLBACK_WAITING;
    log_at(0, reflex);
    CHECK(event_line(&battle, &data, &battle.log[1], line, sizeof line) == 0);
    reflex.fallback = FALLBACK_UNREACHABLE;
    log_at(0, reflex);
    CHECK(event_line(&battle, &data, &battle.log[2], line, sizeof line) > 0 && has(line, "unreachable"));
    log_at(0, (Event){.kind = EVENT_DECISION, .actor = agni, .amount = STANCE_RETREAT, .decider = DECIDER_LAYA, .confidence_permille = 420});
    CHECK(event_line(&battle, &data, &battle.log[3], line, sizeof line) > 0);
    CHECK(has(line, "Laya:") && has(line, "retreat") && has(line, "0.420"));
}

static void test_last_decision_finds_newest(void)
{
    fresh();
    int a = add("Agni", SIDE_PANTHEON), b = add("imp", SIDE_HORDE);
    CHECK(battle_last_decision(&battle, a) == -1);
    log_at(0, (Event){.kind = EVENT_DECISION, .actor = a});
    log_at(1, (Event){.kind = EVENT_DECISION, .actor = b});
    log_at(2, (Event){.kind = EVENT_HIT, .actor = a, .target = b});
    log_at(3, (Event){.kind = EVENT_DECISION, .actor = a});
    CHECK(battle_last_decision(&battle, a) == 3);
    CHECK(battle_last_decision(&battle, b) == 1);
}

void run_report_tests(void)
{
    test_labels_number_shared_names();
    test_event_lines_per_kind();
    test_decision_lines_show_laya_and_fallbacks_only();
    test_last_decision_finds_newest();
}
