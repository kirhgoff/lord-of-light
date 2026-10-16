#include "power.h"

#define TICKS_PER_THOUSAND 1000

static long ability_power(const PowerWeights *w, const Ability *ability)
{
    long damage_per_thousand_ticks = ability->cooldown_ticks > 0 ? (long)ability->damage * TICKS_PER_THOUSAND / ability->cooldown_ticks : 0;
    return w->damage * damage_per_thousand_ticks + (long)w->range * ability->range;
}

static long part_power(const GameData *data, const Part *part)
{
    const PowerWeights *w = &data->power;
    long power = (long)w->integrity * part->integrity + (long)w->vitality * part->vitality
        + (long)w->initiative * part->initiative + (long)w->vision * part->vision_far
        + (long)w->move_speed * part->move_speed_px_per_second;
    for (int a = 0; a < part->ability_count; a++) power += ability_power(w, &part->abilities[a]);
    for (int e = 0; e < ELEMENT_COUNT; e++) power += (long)w->resistance * part->resistance_percent[e];
    if (part->spawn_plan >= 0) power += (long)w->spawning * part->spawn_cap;
    return power;
}

long body_power(const GameData *data, const Body *body)
{
    long power = 0;
    for (int s = 0; s < body->slot_count; s++) power += part_power(data, &data->parts[body->slots[s].part]);
    return power;
}

long pantheon_power(const GameData *data, int pantheon)
{
    long power = 0;
    for (int d = 0; d < DEITIES_PER_PANTHEON; d++) power += body_power(data, &data->pantheons[pantheon].deities[d].body);
    return power;
}
