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

static long power_of_parts(const char **ids, int count)
{
    Body body;
    memset(&body, 0, sizeof body);
    body.slot_count = count;
    for (int s = 0; s < count; s++) body.slots[s].part = find_part(&data, ids[s]);
    return body_power(&data, &body);
}

static void test_power_of_a_hand_built_body(void)
{
    load_data();
    const char *ids[] = {"torso", "carapace", "fanged_head", "serpent_coil", "heart"};
    long torso = 60;
    long carapace = 70 + 25;
    long fanged_head = 28 + 3 * 1 + 256 + 5 * (9 * 1000 / 14) + 20;
    long serpent_coil = 65 + 30;
    long heart = 20 + 2 * 40;
    CHECK(power_of_parts(ids, 5) == torso + carapace + fanged_head + serpent_coil + heart);
}

static void test_healing_counts_like_damage(void)
{
    load_data();
    const char *ids[] = {"healing_hand"};
    CHECK(power_of_parts(ids, 1) == 25 + 5 * (10 * 1000 / 40) + 96);
}

static void test_spawner_counts_its_minions(void)
{
    load_data();
    const char *ids[] = {"spawning_sac"};
    long imp = (14 + 160 + 50) + (20 + 5 * (8 * 1000 / 10) + 20) + (10 + 2 * 15);
    CHECK(power_of_parts(ids, 1) == 20 + 3 * imp);
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

static long distance(long power, long target)
{
    return power > target ? power - target : target - power;
}

static void test_horde_always_has_a_monster_and_stops_at_eight(void)
{
    load_data();
    uint32_t rng = scramble_seed(7);
    CHECK(generate_horde(&data, 1, &rng).count == 1);
    CHECK(generate_horde(&data, 0, &rng).count == 1);
    CHECK(generate_horde(&data, 1L << 40, &rng).count == HORDE_MAX_SIZE);
}

static long closest_attempt_distance(long target, uint32_t seed)
{
    uint32_t rng = scramble_seed(seed);
    long best = -1;
    for (int attempt = 0; attempt < HORDE_MAX_ATTEMPTS; attempt++) {
        long d = distance(horde_attempt(&data, target, &rng).power, target);
        if (best < 0 || d < best) best = d;
    }
    return best;
}

static void test_unreachable_target_keeps_the_closest_attempt(void)
{
    load_data();
    long target = 1L << 40;
    for (uint32_t seed = 0; seed < 5; seed++) {
        uint32_t rng = scramble_seed(seed);
        CHECK(distance(generate_horde(&data, target, &rng).power, target) == closest_attempt_distance(target, seed));
    }
}

static void test_later_attempts_rescue_a_missed_band(void)
{
    load_data();
    int rescued = 0;
    for (uint32_t seed = 0; seed < 200; seed++) {
        long target = pantheon_power(&data, (int)(seed % (uint32_t)data.pantheon_count));
        uint32_t first_rng = scramble_seed(seed), rng = first_rng;
        Horde first = horde_attempt(&data, target, &first_rng);
        Horde chosen = generate_horde(&data, target, &rng);
        bool first_missed = first.power * 100 < target * 90 || first.power * 100 > target * 110;
        rescued += first_missed && chosen.power * 100 >= target * 90 && chosen.power * 100 <= target * 110;
    }
    CHECK(rescued > 0);
}

static void test_forced_pantheon_is_used(void)
{
    load_data();
    static Battle battle;
    for (int p = 0; p < data.pantheon_count; p++) {
        battle_setup_pantheon(&battle, &data, 11, p);
        CHECK(battle.pantheon == p);
    }
    battle_free(&battle);
}

void run_power_tests(void)
{
    test_power_of_a_hand_built_body();
    test_healing_counts_like_damage();
    test_spawner_counts_its_minions();
    test_horde_always_has_a_monster_and_stops_at_eight();
    test_unreachable_target_keeps_the_closest_attempt();
    test_later_attempts_rescue_a_missed_band();
    test_forced_pantheon_is_used();
    test_horde_matches_pantheon_power_within_ten_percent();
    test_same_seed_makes_the_same_horde();
}
