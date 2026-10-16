#include <string.h>
#include "check.h"
#include "../src/sim/data.h"

#define FIXTURE(name) "tests/fixtures/" name
#define ERR_SIZE 256

static GameData data;
static char err[ERR_SIZE];

static void load_real_data(void)
{
    CHECK(load_game_data(&data, "data", err, ERR_SIZE));
}

static void check_error(bool loaded, const char *expected)
{
    CHECK(!loaded);
    if (strstr(err, expected) == NULL) printf("  got: %s\n  want: %s\n", err, expected);
    CHECK(strstr(err, expected) != NULL);
}

static void test_valid_load(void)
{
    load_real_data();
    CHECK(data.part_count >= 10);
    CHECK(data.body_plan_count == 3);
    CHECK(data.pantheon_count == 1);
    CHECK(strcmp(data.pantheons[0].name, "Hindu") == 0);
    CHECK(data.pickup_count == 2 && data.trap_count == 2);
    CHECK(data.elements[ELEMENT_FIRE].dot_damage > 0);
    CHECK(data.elements[ELEMENT_LIGHTNING].chain_targets > 0);
    CHECK(data.elements[ELEMENT_VENOM].internal_first);
    CHECK(data.power.damage > 0);
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
    for (int d = 0; d < DEITIES_PER_PANTHEON; d++) {
        const Body *body = &data.pantheons[0].deities[d].body;
        CHECK(body->slot_count >= 7);
        CHECK(strcmp(body->slots[body->core].id, "torso") == 0);
        for (int s = 0; s < body->slot_count; s++) CHECK(body->slots[s].part >= 0);
    }
    const Body *agni = &data.pantheons[0].deities[0].body;
    CHECK(strcmp(data.parts[agni->slots[find_body_slot(agni, "heart")].part].id, "heart") == 0);
    CHECK(agni->slots[find_body_slot(agni, "gland")].internal);
}

static void test_body_plans_resolved(void)
{
    load_real_data();
    const BodyPlan *biped = &data.body_plans[find_body_plan(&data, "biped")];
    CHECK(biped->body.slot_count == 7 && biped->body.edge_count == 6);
    CHECK(biped->body.slots[find_body_slot(&biped->body, "heart")].internal);
    CHECK(biped->body.slots[find_body_slot(&biped->body, "head")].anchor_y == -14);
}

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
    CHECK(data.pantheon_count == 1);
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
    check_error(load_power(&data, FIXTURE("power_missing_weight.json"), err, ERR_SIZE), "vision: missing");
    check_error(load_arena_objects(&data, FIXTURE("arena_objects_unknown_element.json"), err, ERR_SIZE),
                "traps[0].element: unknown element \"steam\"");
}

static void test_file_errors(void)
{
    check_error(load_parts(&data, FIXTURE("invalid.json"), err, ERR_SIZE), "invalid JSON");
    check_error(load_parts(&data, FIXTURE("missing.json"), err, ERR_SIZE), "missing.json: cannot read file");
}

static void test_failed_load_keeps_previous_data(void)
{
    load_real_data();
    int before = data.part_count;
    CHECK(!load_parts(&data, FIXTURE("parts_duplicate_id.json"), err, ERR_SIZE));
    CHECK(data.part_count == before);
}

void run_data_tests(void)
{
    test_valid_load();
    test_part_fields_resolved();
    test_deity_bodies_resolved();
    test_body_plans_resolved();
    test_pantheon_errors();
    test_body_plan_errors();
    test_part_errors();
    test_other_file_errors();
    test_file_errors();
    test_failed_load_keeps_previous_data();
}
