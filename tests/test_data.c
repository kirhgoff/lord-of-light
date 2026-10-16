#include <string.h>
#include "check.h"
#include "../src/sim/data.h"

#define FIXTURE(name) "tests/fixtures/" name
#define ERR_SIZE 256

static GameData data;
static char err[ERR_SIZE];

static void load_real_data(void)
{
    LOAD_OR_EXIT(load_game_data(&data, "data", err, ERR_SIZE), err);
}

static void check_error(bool loaded, const char *expected)
{
    CHECK(!loaded);
    if (strstr(err, expected) == NULL) printf("  got: %s\n  want: %s\n", err, expected);
    CHECK(strstr(err, expected) != NULL);
}

static const Pantheon *find_pantheon(const char *name)
{
    for (int p = 0; p < data.pantheon_count; p++)
        if (strcmp(data.pantheons[p].name, name) == 0) return &data.pantheons[p];
    return NULL;
}

static bool part_acts_with(const Part *part, Element element)
{
    for (int a = 0; a < part->ability_count; a++)
        if (part->abilities[a].element == element && part->abilities[a].order != ORDER_BLOCK) return true;
    return false;
}

static void test_content_coverage(void)
{
    load_real_data();
    for (int e = 0; e < ELEMENT_COUNT; e++) {
        bool on_monster_part = false;
        for (int i = 0; i < data.part_count; i++)
            on_monster_part |= !data.parts[i].deity_only && part_acts_with(&data.parts[i], (Element)e);
        CHECK(on_monster_part);
    }
    for (int p = 0; p < data.pantheon_count; p++) {
        int covered = 0;
        for (int e = 0; e < ELEMENT_COUNT; e++) {
            bool found = false;
            for (int d = 0; d < DEITIES_PER_PANTHEON; d++) {
                const Body *body = &data.pantheons[p].deities[d].body;
                for (int s = 0; s < body->slot_count; s++)
                    found |= part_acts_with(&data.parts[body->slots[s].part], (Element)e);
            }
            covered += found;
        }
        CHECK(covered >= 4);
    }
}

static void test_valid_load(void)
{
    load_real_data();
    CHECK(data.part_count >= 40);
    CHECK(data.body_plan_count == 9);
    CHECK(data.pantheon_count == 5);
    CHECK(find_pantheon("Egyptian") && find_pantheon("Norse") && find_pantheon("Celtic"));
    CHECK(data.pickup_count == 6 && data.trap_count == 5);
    CHECK(data.elements[ELEMENT_FIRE].dot_damage > 0);
    CHECK(data.elements[ELEMENT_LIGHTNING].chain_targets > 0);
    CHECK(data.elements[ELEMENT_VENOM].internal_first);
    CHECK(data.power.damage > 0);
    CHECK(data.laya.url_count == 2 && strncmp(data.laya.urls[0], "http://", 7) == 0);
    CHECK(data.laya.timeout_ms == 8000 && data.laya.min_stance_confidence_permille == 200);
}

static void test_part_fields_resolved(void)
{
    load_real_data();
    const Part *gland = &data.parts[find_part(&data, "fire_gland")];
    CHECK(gland->internal);
    CHECK(gland->resistance_percent[ELEMENT_FIRE] == 75);
    const Part *sac = &data.parts[find_part(&data, "spawning_sac")];
    CHECK(sac->spawn_plan == find_body_plan(&data, "minion_imp"));
    CHECK(sac->spawn_cap == 3);
    const Part *hand = &data.parts[find_part(&data, "fire_hand")];
    CHECK(hand->deity_only && hand->abilities[0].order == ORDER_CAST);
    CHECK(hand->abilities[0].element == ELEMENT_FIRE && hand->abilities[0].cooldown_ticks == 20);
    CHECK(data.parts[find_part(&data, "legs")].spawn_plan == -1);
}

static void test_deity_bodies_resolved(void)
{
    load_real_data();
    for (int p = 0; p < data.pantheon_count; p++) {
        for (int d = 0; d < DEITIES_PER_PANTHEON; d++) {
            const Body *body = &data.pantheons[p].deities[d].body;
            CHECK(body->slot_count >= 7);
            CHECK(strcmp(body->slots[body->core].id, "torso") == 0);
            CHECK(data.pantheons[p].deities[d].epithet[0] != '\0');
            for (int s = 0; s < body->slot_count; s++) CHECK(body->slots[s].part >= 0);
        }
    }
    const Body *agni = &find_pantheon("Hindu")->deities[0].body;
    CHECK(strcmp(data.parts[agni->slots[find_body_slot(agni, "heart")].part].id, "large_heart") == 0);
    CHECK(agni->slots[find_body_slot(agni, "gland")].internal);
}

static void test_body_plans_resolved(void)
{
    load_real_data();
    const BodyPlan *biped = &data.body_plans[find_body_plan(&data, "biped")];
    CHECK(biped->body.slot_count == 7 && biped->body.edge_count == 6);
    CHECK(biped->body.slots[find_body_slot(&biped->body, "heart")].internal);
    CHECK(biped->body.slots[find_body_slot(&biped->body, "head")].anchor_y == -10);
}

typedef struct {
    bool (*loader)(GameData *, const char *, char *, size_t);
    const char *fixture;
    const char *expected;
} ErrorCase;

static void test_pantheon_errors(void)
{
    load_real_data();
    check_error(load_pantheon(&data, FIXTURE("pantheon_unknown_part.json"), err, ERR_SIZE),
                "tests/fixtures/pantheon_unknown_part.json: deities[0].body.parts[2].part: unknown part \"fire_hnad\"");
    check_error(load_pantheon(&data, FIXTURE("pantheon_edge_missing_slot.json"), err, ERR_SIZE),
                "deities[1].body.edges[6][1]: unknown slot \"tail\"");
    check_error(load_pantheon(&data, FIXTURE("pantheon_no_core.json"), err, ERR_SIZE),
                "deities[2].body.core: missing core");
    check_error(load_pantheon(&data, FIXTURE("pantheon_unknown_core.json"), err, ERR_SIZE),
                "deities[2].body.core: unknown slot \"spine\"");
    check_error(load_pantheon(&data, FIXTURE("pantheon_wrong_deity_count.json"), err, ERR_SIZE),
                "deities: expected 4 deities, found 3");
    CHECK(data.pantheon_count == 5);
}

static void test_body_plan_errors(void)
{
    check_error(load_body_plans(&data, FIXTURE("bodyplans_edge_missing_slot.json"), err, ERR_SIZE),
                "bodyplans[1].edges[3][1]: unknown slot \"wing\"");
    check_error(load_body_plans(&data, FIXTURE("bodyplans_no_core.json"), err, ERR_SIZE),
                "bodyplans[0].core: missing core");
    check_error(load_body_plans(&data, FIXTURE("bodyplans_duplicate_slot.json"), err, ERR_SIZE),
                "bodyplans[0].slots[7].id: duplicate slot \"head\"");
}

static void test_slot_role_errors(void)
{
    static const ErrorCase cases[] = {
        { load_body_plans, FIXTURE("bodyplans_unknown_role.json"), "bodyplans[0].slots[0].role: unknown role \"flyer\"" },
        { load_body_plans, FIXTURE("bodyplans_heart_role_external.json"), "bodyplans[0].slots[0].role: role \"heart\" needs an internal slot" },
        { load_body_plans, FIXTURE("bodyplans_mover_role_internal.json"), "bodyplans[0].slots[6].role: role \"mover\" needs an external slot" },
        { load_body_plans, FIXTURE("bodyplans_unknown_fixed_part.json"), "bodyplans[0].slots[2].part: unknown part \"torzo\"" },
        { load_body_plans, FIXTURE("bodyplans_fixed_part_wrong_kind.json"), "bodyplans[0].slots[2].part: part \"heart\" is internal" },
        { load_body_plans, FIXTURE("bodyplans_fixed_part_misses_role.json"), "bodyplans[0].slots[3].part: part \"torso\" does not fit role \"sense\"" },
        { load_body_plans, FIXTURE("bodyplans_no_heart_slot.json"), "bodyplans[0]: body plan \"biped\" has no heart slot" },
        { load_body_plans, FIXTURE("bodyplans_no_mover_slot.json"), "bodyplans[0]: body plan \"biped\" has no mover slot" },
        { load_body_plans, FIXTURE("bodyplans_fixed_part_minion_only.json"), "bodyplans[0].slots[2].part: part \"imp_body\" is minion only" },
        { load_body_plans, FIXTURE("bodyplans_fixed_part_deity_only.json"), "bodyplans[0].slots[2].part: part \"fire_hand\" is deity only" },
        { load_body_plans, FIXTURE("bodyplans_spawn_slot_unfilled.json"), "bodyplans[6].slots[1]: spawn plan slot \"claw\" has no part" },
        { load_parts, FIXTURE("parts_horde_heart_unfillable.json"), "bodyplans[0].slots[5]: no part fills slot \"heart\"" },
    };
    load_real_data();
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++)
        check_error(cases[i].loader(&data, cases[i].fixture, err, ERR_SIZE), cases[i].expected);
}

static void test_horde_needs_a_non_spawn_plan(void)
{
    static GameData fresh;
    memset(&fresh, 0, sizeof fresh);
    CHECK(load_body_plans(&fresh, FIXTURE("bodyplans_only_spawn_plans.json"), err, ERR_SIZE));
    check_error(load_parts(&fresh, "data/parts.json", err, ERR_SIZE), "bodyplans: no body plan for the Horde");
}

static void test_core_parts_stay_out_of_horde_slots(void)
{
    load_real_data();
    const BodyPlan *quadruped = &data.body_plans[find_body_plan(&data, "quadruped")];
    CHECK(quadruped->body.slots[find_body_slot(&quadruped->body, "tail")].role == SLOT_ROLE_ATTACK);
    const BodyPlan *serpent = &data.body_plans[find_body_plan(&data, "serpent")];
    const BodySlot *tail = &serpent->body.slots[find_body_slot(&serpent->body, "tail")];
    CHECK(!part_fits_role(&data.parts[find_part(&data, "fanged_head")], SLOT_ROLE_ATTACK));
    CHECK(!fills_horde_slot(&data, find_part(&data, "serpent_coil"), tail));
    CHECK(fills_horde_slot(&data, find_part(&data, "serpent_tail"), tail));
}

static void test_minion_plans_are_fixed(void)
{
    load_real_data();
    for (int p = 0; p < data.part_count; p++) {
        if (data.parts[p].spawn_plan < 0) continue;
        const Body *body = &data.body_plans[data.parts[p].spawn_plan].body;
        for (int s = 0; s < body->slot_count; s++) CHECK(body->slots[s].part >= 0);
    }
    CHECK(data.parts[find_part(&data, "imp_body")].minion_only);
    CHECK(data.parts[find_part(&data, "crow_body")].vision_far > 0);
    CHECK(data.parts[find_part(&data, "wisp_core")].vision_far > 0);
    CHECK(!data.parts[find_part(&data, "heart")].minion_only);
}

static void check_same_stats(const Part *minion, const Part *original)
{
    CHECK(minion->integrity == original->integrity);
    CHECK(minion->ability_count == original->ability_count);
    for (int i = 0; i < original->ability_count; i++) {
        CHECK(minion->abilities[i].order == original->abilities[i].order);
        CHECK(minion->abilities[i].element == original->abilities[i].element);
        CHECK(minion->abilities[i].damage == original->abilities[i].damage);
        CHECK(minion->abilities[i].range == original->abilities[i].range);
        CHECK(minion->abilities[i].cooldown_ticks == original->abilities[i].cooldown_ticks);
        CHECK(minion->abilities[i].area == original->abilities[i].area);
    }
}

static void test_minion_attack_parts_match_the_parts_they_replace(void)
{
    load_real_data();
    int claw = find_part(&data, "claw");
    int minion_claw = find_part(&data, "minion_claw");
    int glow_lantern = find_part(&data, "glow_lantern");
    int wisp_spark = find_part(&data, "wisp_spark");
    CHECK(data.parts[minion_claw].minion_only);
    CHECK(data.parts[wisp_spark].minion_only);
    check_same_stats(&data.parts[minion_claw], &data.parts[claw]);
    check_same_stats(&data.parts[wisp_spark], &data.parts[glow_lantern]);
}

static void test_part_errors(void)
{
    load_real_data();
    check_error(load_parts(&data, FIXTURE("parts_unknown_element.json"), err, ERR_SIZE),
                "parts[6].abilities[0].element: unknown element \"firee\"");
    check_error(load_parts(&data, FIXTURE("parts_unknown_spawn_plan.json"), err, ERR_SIZE),
                "parts[16].spawning.plan: unknown body plan \"minion_ogre\"");
    check_error(load_parts(&data, FIXTURE("parts_duplicate_id.json"), err, ERR_SIZE),
                "parts[22].id: duplicate part \"torso\"");
    check_error(load_parts(&data, FIXTURE("parts_missing_integrity.json"), err, ERR_SIZE),
                "parts[3].integrity: missing");
}

static void test_other_file_errors(void)
{
    check_error(load_elements(&data, FIXTURE("elements_missing_element.json"), err, ERR_SIZE),
                "elements: missing element \"void\"");
    check_error(load_elements(&data, FIXTURE("elements_negative_dot_ticks.json"), err, ERR_SIZE),
                "elements[1].dot_ticks: must be between 0 and 1000000");
    check_error(load_elements(&data, FIXTURE("elements_slow_percent_over_100.json"), err, ERR_SIZE),
                "elements[2].slow_percent: must be between 0 and 100");
    check_error(load_elements(&data, FIXTURE("elements_huge_chain_range.json"), err, ERR_SIZE),
                "elements[3].chain_range: must be between 0 and 1000000");
    check_error(load_power(&data, FIXTURE("power_missing_weight.json"), err, ERR_SIZE), "vision: missing");
    check_error(load_arena_objects(&data, FIXTURE("arena_objects_unknown_element.json"), err, ERR_SIZE),
                "traps[0].element: unknown element \"steam\"");
    check_error(load_laya(&data, FIXTURE("laya_threshold_out_of_range.json"), err, ERR_SIZE),
                "min_stance_confidence_permille: must be between 0 and 1000");
    check_error(load_laya(&data, FIXTURE("laya_missing_urls.json"), err, ERR_SIZE), "urls: missing");
    check_error(load_laya(&data, FIXTURE("laya_empty_url.json"), err, ERR_SIZE), "urls[1]: must be 1 to 127 characters");
}

static void test_sound_errors(void)
{
    load_real_data();
    check_error(load_parts(&data, FIXTURE("parts_invalid_sound.json"), err, ERR_SIZE),
                "parts[13].abilities[0].sound: invalid sound id \"../sounds_sheet\"");
    check_error(load_parts(&data, FIXTURE("parts_missing_step_sound.json"), err, ERR_SIZE),
                "parts[3].step_sound: missing");
    check_error(load_elements(&data, FIXTURE("elements_invalid_sound.json"), err, ERR_SIZE),
                "elements[1].hit_sound: invalid sound id \"../sounds_sheet\"");
    check_error(load_elements(&data, FIXTURE("elements_missing_cue.json"), err, ERR_SIZE),
                "cues.death: missing");
    check_error(load_arena_objects(&data, FIXTURE("arena_objects_invalid_sound.json"), err, ERR_SIZE),
                "traps[1].sound: invalid sound id \"../sounds_sheet\"");
}

static void test_file_errors(void)
{
    check_error(load_parts(&data, FIXTURE("invalid.json"), err, ERR_SIZE), "invalid JSON");
    check_error(load_parts(&data, FIXTURE("missing.json"), err, ERR_SIZE), "missing.json: cannot read file");
}

static void test_body_graph_errors(void)
{
    check_error(load_body_plans(&data, FIXTURE("bodyplans_self_loop.json"), err, ERR_SIZE),
                "bodyplans[0].edges[6]: edge joins a slot to itself");
    check_error(load_body_plans(&data, FIXTURE("bodyplans_duplicate_edge.json"), err, ERR_SIZE),
                "bodyplans[0].edges[6]: duplicate edge");
    check_error(load_body_plans(&data, FIXTURE("bodyplans_unreachable_slot.json"), err, ERR_SIZE),
                "bodyplans[0].slots[6]: slot \"gland\" is not connected to the core");
}

static void test_number_and_range_errors(void)
{
    load_real_data();
    check_error(load_parts(&data, FIXTURE("parts_missing_ability_element.json"), err, ERR_SIZE),
                "parts[6].abilities[0].element: missing");
    check_error(load_parts(&data, FIXTURE("parts_fractional_integrity.json"), err, ERR_SIZE),
                "parts[0].integrity: expected an integer");
    check_error(load_parts(&data, FIXTURE("parts_huge_integrity.json"), err, ERR_SIZE),
                "parts[0].integrity: expected an integer");
    check_error(load_parts(&data, FIXTURE("parts_zero_integrity.json"), err, ERR_SIZE),
                "parts[0].integrity: must be positive");
    check_error(load_parts(&data, FIXTURE("parts_zero_spawn_cap.json"), err, ERR_SIZE),
                "parts[16].spawning.cap: must be at least 1");
    check_error(load_parts(&data, FIXTURE("parts_resistance_out_of_range.json"), err, ERR_SIZE),
                "parts[17].resistances.fire: resistance must be between -100 and 100");
    check_error(load_parts(&data, FIXTURE("parts_vision_out_of_order.json"), err, ERR_SIZE),
                "parts[1].vision: bands must satisfy close <= medium <= far");
    check_error(load_parts(&data, FIXTURE("parts_negative_damage.json"), err, ERR_SIZE),
                "parts[6].abilities[0].damage: must not be negative");
    check_error(load_parts(&data, FIXTURE("parts_huge_damage.json"), err, ERR_SIZE),
                "parts[6].abilities[0].damage: must be between 0 and 1000000");
}

static void test_duplicate_and_empty_errors(void)
{
    load_real_data();
    check_error(load_arena_objects(&data, FIXTURE("arena_objects_duplicate_id.json"), err, ERR_SIZE),
                "traps[2].id: duplicate id \"fire_vent\"");
    check_error(load_pantheon(&data, "data/pantheons/hindu.json", err, ERR_SIZE),
                "name: duplicate pantheon \"Hindu\"");
    check_error(load_pantheons(&data, FIXTURE("pantheons_empty"), err, ERR_SIZE), "no pantheons found");
    check_error(load_pantheons(&data, FIXTURE("pantheons_long_name"), err, ERR_SIZE), "file name too long");
}

static void test_heal_is_separate_from_damage(void)
{
    load_real_data();
    const Ability *heal = &data.parts[find_part(&data, "healing_hand")].abilities[0];
    CHECK(heal->order == ORDER_SUPPORT && heal->heal == 10 && heal->damage == 0);
}

static void test_negative_values_and_empty_parts(void)
{
    static const ErrorCase cases[] = {
        { load_parts, FIXTURE("parts_negative_range.json"), "parts[6].abilities[0].range: must not be negative" },
        { load_parts, FIXTURE("parts_negative_cooldown.json"), "parts[6].abilities[0].cooldown: must not be negative" },
        { load_parts, FIXTURE("parts_negative_area.json"), "parts[6].abilities[0].area: must not be negative" },
        { load_parts, FIXTURE("parts_negative_vitality.json"), "parts[3].vitality: must not be negative" },
        { load_parts, FIXTURE("parts_negative_initiative.json"), "parts[1].initiative: must not be negative" },
        { load_parts, FIXTURE("parts_huge_initiative.json"), "parts[1].initiative: must be between 0 and 100" },
        { load_parts, FIXTURE("parts_negative_move_speed.json"), "parts[5].move_speed: must not be negative" },
        { load_parts, FIXTURE("parts_negative_vision.json"), "parts[1].vision.close: must not be negative" },
        { load_parts, FIXTURE("parts_empty.json"), "parts: no parts" },
        { load_arena_objects, FIXTURE("arena_objects_negative_heal.json"), "pickups[0].heal: must not be negative" },
        { load_arena_objects, FIXTURE("arena_objects_negative_radius.json"), "traps[0].radius: must not be negative" },
        { load_arena_objects, FIXTURE("arena_objects_negative_respawn.json"), "traps[1].respawn_ticks: must not be negative" },
        { load_arena_objects, FIXTURE("arena_objects_huge_percent.json"), "pickups[5].ward_percent: must be between 0 and 1000" },
    };
    load_real_data();
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++)
        check_error(cases[i].loader(&data, cases[i].fixture, err, ERR_SIZE), cases[i].expected);
}

static GameData snapshot;

static void check_unchanged(bool loaded)
{
    CHECK(!loaded);
    CHECK(memcmp(&data, &snapshot, sizeof data) == 0);
}

static void test_failed_load_keeps_previous_data(void)
{
    load_real_data();
    snapshot = data;
    check_unchanged(load_parts(&data, FIXTURE("parts_duplicate_id.json"), err, ERR_SIZE));
    check_unchanged(load_arena_objects(&data, FIXTURE("arena_objects_unknown_element.json"), err, ERR_SIZE));
    check_unchanged(load_power(&data, FIXTURE("power_missing_weight.json"), err, ERR_SIZE));
    check_unchanged(load_laya(&data, FIXTURE("laya_missing_urls.json"), err, ERR_SIZE));
    check_unchanged(load_pantheon(&data, FIXTURE("pantheon_unknown_part.json"), err, ERR_SIZE));
    check_unchanged(load_pantheons(&data, FIXTURE("pantheons_second_file_bad"), err, ERR_SIZE));
    check_unchanged(load_game_data(&data, "tests/fixtures/no_such_dir", err, ERR_SIZE));
}

void run_data_tests(void)
{
    test_valid_load();
    test_part_fields_resolved();
    test_deity_bodies_resolved();
    test_content_coverage();
    test_body_plans_resolved();
    test_pantheon_errors();
    test_body_plan_errors();
    test_slot_role_errors();
    test_core_parts_stay_out_of_horde_slots();
    test_horde_needs_a_non_spawn_plan();
    test_minion_plans_are_fixed();
    test_minion_attack_parts_match_the_parts_they_replace();
    test_part_errors();
    test_other_file_errors();
    test_sound_errors();
    test_file_errors();
    test_failed_load_keeps_previous_data();
    test_body_graph_errors();
    test_number_and_range_errors();
    test_duplicate_and_empty_errors();
    test_heal_is_separate_from_damage();
    test_negative_values_and_empty_parts();
}
