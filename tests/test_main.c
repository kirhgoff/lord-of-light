#include <stdio.h>
#include "check.h"
#include "../src/sim/arena.h"

int test_failures;

void run_data_tests(void);
void run_battle_tests(void);
void run_asset_tests(void);
void run_movement_tests(void);
void run_combat_tests(void);
void run_decide_tests(void);
void run_floor_tests(void);
void run_view_tests(void);
void run_power_tests(void);
void run_sound_tests(void);

static void test_arena_is_48_by_32(void)
{
    CHECK(arena_tile_count() == 48 * 32);
}

int main(void)
{
    test_arena_is_48_by_32();
    run_data_tests();
    run_battle_tests();
    run_asset_tests();
    run_movement_tests();
    run_combat_tests();
    run_decide_tests();
    run_floor_tests();
    run_view_tests();
    run_power_tests();
    run_sound_tests();
    if (test_failures) return 1;
    printf("all tests passed\n");
    return 0;
}
