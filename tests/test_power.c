#include <string.h>
#include "check.h"
#include "../src/sim/battle.h"
#include "../src/sim/horde.h"
#include "../src/sim/power.h"

#define ERR_SIZE 256
#define SEEDS 50

static GameData data;

static void load_data(void)
{
    char err[ERR_SIZE];
    LOAD_OR_EXIT(load_game_data(&data, "data", err, ERR_SIZE), err);
}

static void test_power_of_a_hand_built_body(void)
{
    load_data();
    const char *ids[] = {"torso", "carapace", "fanged_head"};
    Body body;
    memset(&body, 0, sizeof body);
    body.slot_count = 3;
    for (int s = 0; s < 3; s++) body.slots[s].part = find_part(&data, ids[s]);
    long torso = 60;
    long carapace = 70 + 25;
    long fanged_head = 28 + 3 * 1 + 256 + 5 * (9 * 1000 / 14) + 20;
    CHECK(body_power(&data, &body) == torso + carapace + fanged_head);
}

static Horde horde_for_seed(uint32_t seed, long *target)
{
    uint32_t rng = scramble_seed(seed);
    *target = pantheon_power(&data, (int)(seed % (uint32_t)data.pantheon_count));
    return generate_horde(&data, *target, &rng);
}

static void test_horde_matches_pantheon_power_within_ten_percent(void)
{
    load_data();
    for (uint32_t seed = 0; seed < SEEDS; seed++) {
        long target;
        Horde horde = horde_for_seed(seed, &target);
        CHECK(horde.count >= 1 && horde.count <= HORDE_MAX_SIZE);
        CHECK(horde.power * 100 >= target * 90 && horde.power * 100 <= target * 110);
    }
}

static void test_same_seed_makes_the_same_horde(void)
{
    load_data();
    for (uint32_t seed = 0; seed < SEEDS; seed++) {
        long target;
        Horde first = horde_for_seed(seed, &target);
        Horde second = horde_for_seed(seed, &target);
        CHECK(first.count == second.count && first.power == second.power);
        for (int m = 0; m < first.count; m++) {
            CHECK(first.monsters[m].plan == second.monsters[m].plan);
            CHECK(memcmp(&first.monsters[m].body, &second.monsters[m].body, sizeof(Body)) == 0);
        }
    }
}

void run_power_tests(void)
{
    test_power_of_a_hand_built_body();
    test_horde_matches_pantheon_power_within_ten_percent();
    test_same_seed_makes_the_same_horde();
}
