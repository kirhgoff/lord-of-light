#include <stdio.h>
#include <string.h>
#include "../src/sim/arena.h"

static int failures;

#define CHECK(cond) \
    do { if (!(cond)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

static void test_arena_is_48_by_32(void)
{
    CHECK(arena_tile_count() == 48 * 32);
}

int main(void)
{
    test_arena_is_48_by_32();
    if (failures) return 1;
    printf("all tests passed\n");
    return 0;
}
