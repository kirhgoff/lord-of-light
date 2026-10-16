#include "horde.h"

#include <stdlib.h>

#include "battle.h"
#include "power.h"

#define MAX_ATTEMPTS 32
#define MAX_DRAWS 24
#define LOWER_PERCENT 90
#define UPPER_PERCENT 110

static int random_part(uint32_t *rng, const GameData *data, const BodySlot *slot)
{
    if (slot->part >= 0) return slot->part;
    int eligible[MAX_PARTS], count = 0;
    for (int p = 0; p < data->part_count; p++)
        if (fills_horde_slot(data, p, slot)) eligible[count++] = p;
    return eligible[rng_below(rng, count)];
}

static Monster random_monster(const GameData *data, uint32_t *rng)
{
    int plans[MAX_BODY_PLANS], count = 0;
    for (int p = 0; p < data->body_plan_count; p++)
        if (!is_spawn_plan(data, p)) plans[count++] = p;
    Monster monster = {.plan = plans[rng_below(rng, count)]};
    monster.body = data->body_plans[monster.plan].body;
    for (int s = 0; s < monster.body.slot_count; s++) monster.body.slots[s].part = random_part(rng, data, &monster.body.slots[s]);
    return monster;
}

static Horde attempt_horde(const GameData *data, long target_power, uint32_t *rng)
{
    Horde horde = {0};
    for (int draw = 0; draw < MAX_DRAWS && horde.count < HORDE_MAX_SIZE && horde.power * 100 < target_power * LOWER_PERCENT; draw++) {
        Monster monster = random_monster(data, rng);
        long power = body_power(data, &monster.body);
        if ((horde.power + power) * 100 > target_power * UPPER_PERCENT) continue;
        horde.monsters[horde.count++] = monster;
        horde.power += power;
    }
    return horde;
}

static long distance_from(const Horde *horde, long target_power)
{
    return horde->count ? labs(horde->power - target_power) : target_power;
}

static bool within_band(const Horde *horde, long target_power)
{
    return horde->count && horde->power * 100 >= target_power * LOWER_PERCENT && horde->power * 100 <= target_power * UPPER_PERCENT;
}

Horde generate_horde(const GameData *data, long target_power, uint32_t *rng)
{
    Horde best = attempt_horde(data, target_power, rng);
    for (int attempt = 1; attempt < MAX_ATTEMPTS && !within_band(&best, target_power); attempt++) {
        Horde next = attempt_horde(data, target_power, rng);
        if (distance_from(&next, target_power) < distance_from(&best, target_power)) best = next;
    }
    return best;
}
