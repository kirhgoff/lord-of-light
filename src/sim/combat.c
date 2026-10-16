#include "combat.h"

#include "movement.h"

static int64_t squared(int64_t value)
{
    return value * value;
}

static int clamp_percent(int value)
{
    return value < -100 ? -100 : value > 100 ? 100 : value;
}

static const Ability *find_ability(const Part *part, OrderKind kind)
{
    for (int a = 0; a < part->ability_count; a++)
        if (part->abilities[a].order == kind) return &part->abilities[a];
    return NULL;
}

int combatant_resistance_percent(const Combatant *c, const GameData *data, Element element, bool internal_only)
{
    bool working[MAX_SLOTS];
    int total = 0;
    body_working_parts(&c->body, c->parts, working);
    for (int s = 0; s < c->body.slot_count; s++) {
        const Part *part = &data->parts[c->body.slots[s].part];
        if (working[s] && (!internal_only || part->internal)) total += part->resistance_percent[element];
    }
    return clamp_percent(total);
}

bool combatant_in_reach(const Combatant *from, const Combatant *to, int range_px)
{
    int64_t reach = (int64_t)range_px * MILLIPX_PER_PX + 2 * COMBATANT_RADIUS_MILLIPX;
    return squared(from->x_millipx - to->x_millipx) + squared(from->y_millipx - to->y_millipx) <= squared(reach);
}

bool combatant_has_working_heart(const Combatant *c, const GameData *data)
{
    bool working[MAX_SLOTS];
    body_working_parts(&c->body, c->parts, working);
    for (int s = 0; s < c->body.slot_count; s++)
        if (working[s] && data->parts[c->body.slots[s].part].vitality > 0) return true;
    return false;
}

static bool is_blocking(const Combatant *c, const GameData *data, const bool working[MAX_SLOTS])
{
    for (int s = 0; s < c->body.slot_count; s++)
        if (working[s] && c->parts[s].order.kind == ORDER_BLOCK && find_ability(&data->parts[c->body.slots[s].part], ORDER_BLOCK))
            return true;
    return false;
}

static int pick_target_part(Battle *battle, const GameData *data, const Combatant *c, const bool working[MAX_SLOTS], Element element)
{
    int preferred[MAX_SLOTS], others[MAX_SLOTS], preferred_count = 0, other_count = 0;
    bool want_internal = data->elements[element].internal_first;
    for (int s = 0; s < c->body.slot_count; s++) {
        if (!working[s]) continue;
        if (c->body.slots[s].internal == want_internal) preferred[preferred_count++] = s;
        else others[other_count++] = s;
    }
    if (preferred_count > 0) return preferred[rng_below(&battle->rng, preferred_count)];
    if (other_count == 0) return -1;
    return others[rng_below(&battle->rng, other_count)];
}

static void kill(Battle *battle, int target, int actor, int actor_part, Element element)
{
    Combatant *c = &battle->combatants[target];
    int remains_index = -1;
    c->alive = false;
    if (battle->remains_count < MAX_REMAINS) {
        Remains *remains = &battle->remains[battle->remains_count];
        *remains = (Remains){c->x_millipx, c->y_millipx, c->body.slot_count, {0}};
        for (int s = 0; s < c->body.slot_count; s++) remains->parts[s] = c->body.slots[s].part;
        remains_index = battle->remains_count++;
    }
    battle_log_event(battle, (Event){0, EVENT_DEATH, actor, actor_part, target, -1, element, 0, remains_index});
}

static void death_check(Battle *battle, const GameData *data, int target, int actor, int actor_part, Element element)
{
    const Combatant *c = &battle->combatants[target];
    if (c->alive && (c->vitality <= 0 || !combatant_has_working_heart(c, data))) kill(battle, target, actor, actor_part, element);
}

static void hit_once(Battle *battle, const GameData *data, int actor, int actor_part, int target, Element element, int damage, int resistance)
{
    Combatant *victim = &battle->combatants[target];
    bool working[MAX_SLOTS];
    body_working_parts(&victim->body, victim->parts, working);
    if (actor >= 0 && battle->combatants[actor].damage_bonus_ticks > 0)
        damage = damage * (100 + battle->combatants[actor].damage_bonus_percent) / 100;
    damage = damage * (100 - resistance) / 100;
    if (is_blocking(victim, data, working)) damage = damage * BLOCK_PERCENT / 100;
    int part = pick_target_part(battle, data, victim, working, element);
    if (part < 0) return death_check(battle, data, target, actor, actor_part, element);
    victim->parts[part].integrity = victim->parts[part].integrity > damage ? victim->parts[part].integrity - damage : 0;
    victim->vitality -= damage * VITALITY_SHARE_PERCENT / 100;
    if (victim->vitality < 0) victim->vitality = 0;
    battle_log_event(battle, (Event){0, EVENT_HIT, actor, actor_part, target, part, element, damage, victim->parts[part].integrity});
    death_check(battle, data, target, actor, actor_part, element);
}

void combatant_hit(Battle *battle, const GameData *data, int actor, int actor_part, int target, Element element, int damage)
{
    const Combatant *victim = &battle->combatants[target];
    if (!victim->alive) return;
    int resistance = combatant_resistance_percent(victim, data, element, element == ELEMENT_RADIANCE);
    hit_once(battle, data, actor, actor_part, target, element, damage, resistance);
}
