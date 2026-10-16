#include <stdio.h>

#include "sim/battle.h"
#include "sim/decide.h"
#include "sim/power.h"

#define ERR_SIZE 256
#define BATTLES_PER_PANTHEON 200
#define SEED_STRIDE 1000
#define TICK_BOUND 4000

static long side_power(const Battle *battle, const GameData *data, Side side)
{
    long power = 0;
    for (int c = 0; c < battle->combatant_count; c++)
        if (battle->combatants[c].side == side) power += body_power(data, &battle->combatants[c].body);
    return power;
}

static void balance_pantheon(const GameData *data, int pantheon)
{
    static Battle battle;
    Decider reflex = reflex_decider();
    int pantheon_wins = 0;
    long ticks = 0, pantheon_power_sum = 0, horde_power_sum = 0;
    for (int n = 0; n < BATTLES_PER_PANTHEON; n++) {
        battle_setup_pantheon(&battle, data, (uint32_t)(pantheon * SEED_STRIDE + n), pantheon);
        pantheon_power_sum += side_power(&battle, data, SIDE_PANTHEON);
        horde_power_sum += side_power(&battle, data, SIDE_HORDE);
        while (battle_winner(&battle) < 0 && battle.tick < TICK_BOUND) battle_step(&battle, data, &reflex);
        pantheon_wins += battle_winner(&battle) == SIDE_PANTHEON;
        ticks += battle.tick;
    }
    battle_free(&battle);
    printf("%-16s win rate %5.1f%%  mean length %6ld ticks  mean Power: Pantheon %ld, Horde %ld\n", data->pantheons[pantheon].name,
           100.0 * pantheon_wins / BATTLES_PER_PANTHEON, ticks / BATTLES_PER_PANTHEON,
           pantheon_power_sum / BATTLES_PER_PANTHEON, horde_power_sum / BATTLES_PER_PANTHEON);
}

int main(void)
{
    static GameData data;
    char err[ERR_SIZE];
    if (!load_game_data(&data, "data", err, ERR_SIZE)) return fprintf(stderr, "data: %s\n", err), 1;
    for (int p = 0; p < data.pantheon_count; p++) balance_pantheon(&data, p);
    return 0;
}
