#include "combat.h"

#include "movement.h"
#include "vision.h"

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

static bool is_living_target(const Battle *battle, int target)
{
    return target >= 0 && target < battle->combatant_count && battle->combatants[target].alive;
}

static bool act_attack(Battle *battle, const GameData *data, int me, int s, const Ability *ability)
{
    const Combatant *self = &battle->combatants[me];
    int target = self->parts[s].order.target;
    if (!is_living_target(battle, target)) return false;
    const Combatant *victim = &battle->combatants[target];
    if (victim->charm_side == self->charm_side || !combatant_in_reach(self, victim, ability->range)) return false;
    if (ability->order == ORDER_CAST && !line_of_sight(battle, self->x_millipx, self->y_millipx, victim->x_millipx, victim->y_millipx))
        return false;
    combatant_hit(battle, data, me, s, target, ability->element, ability->damage);
    return true;
}

static bool act_support(Battle *battle, int me, int s, const Ability *ability)
{
    const Combatant *self = &battle->combatants[me];
    int target = self->parts[s].order.target;
    if (!is_living_target(battle, target)) return false;
    Combatant *ally = &battle->combatants[target];
    if (ally->charm_side != self->charm_side || !combatant_in_reach(self, ally, ability->range)) return false;
    int gain = ally->vitality_max - ally->vitality;
    if (gain > ability->heal) gain = ability->heal;
    if (gain <= 0) return false;
    ally->vitality += gain;
    battle_log_event(battle, (Event){0, EVENT_EFFECT, me, s, target, -1, ability->element, gain, EFFECT_HEAL});
    return true;
}

static int living_minions(const Battle *battle, int spawner)
{
    int count = 0;
    for (int c = 0; c < battle->combatant_count; c++) count += battle->combatants[c].alive && battle->combatants[c].spawner == spawner;
    return count;
}

static bool act_spawn(Battle *battle, const GameData *data, int me, int s)
{
    const Part *sac = &data->parts[battle->combatants[me].body.slots[s].part];
    if (sac->spawn_plan < 0 || battle->combatant_count == MAX_COMBATANTS || living_minions(battle, me) >= sac->spawn_cap) return false;
    const BodyPlan *plan = &data->body_plans[sac->spawn_plan];
    const Combatant *spawner = &battle->combatants[me];
    int minion = battle_add_combatant(battle, data, plan->id, spawner->side, &plan->body, spawner->x_millipx, spawner->y_millipx);
    battle->combatants[minion].charm_side = spawner->charm_side;
    battle->combatants[minion].charm_ticks = spawner->charm_ticks;
    battle->combatants[minion].spawner = me;
    battle_log_event(battle, (Event){0, EVENT_SPAWN, me, s, minion, -1, -1, 0, 0});
    return true;
}

static bool perform(Battle *battle, const GameData *data, int me, int s, const Ability *ability)
{
    switch (ability->order) {
    case ORDER_STRIKE:
    case ORDER_CAST: return act_attack(battle, data, me, s, ability);
    case ORDER_SUPPORT: return act_support(battle, me, s, ability);
    case ORDER_SPAWN: return act_spawn(battle, data, me, s);
    default: return false;
    }
}

static void act_combatant(Battle *battle, const GameData *data, int me)
{
    bool working[MAX_SLOTS];
    body_working_parts(&battle->combatants[me].body, battle->combatants[me].parts, working);
    for (int s = 0; s < battle->combatants[me].body.slot_count; s++) {
        PartState *state = &battle->combatants[me].parts[s];
        const Ability *ability = find_ability(&data->parts[battle->combatants[me].body.slots[s].part], state->order.kind);
        if (!working[s] || state->cooldown > 0 || !ability) continue;
        if (perform(battle, data, me, s, ability)) state->cooldown = ability->cooldown_ticks;
    }
}

void battle_act(Battle *battle, const GameData *data)
{
    for (int c = 0; c < battle->combatant_count; c++)
        for (int s = 0; s < battle->combatants[c].body.slot_count; s++)
            if (battle->combatants[c].alive && battle->combatants[c].parts[s].cooldown > 0) battle->combatants[c].parts[s].cooldown--;
    for (int c = 0; c < battle->combatant_count; c++)
        if (battle->combatants[c].alive) act_combatant(battle, data, c);
}

void battle_tick_effects(Battle *battle, const GameData *data)
{
    (void)data;
    for (int c = 0; c < battle->combatant_count; c++) {
        Combatant *who = &battle->combatants[c];
        if (!who->alive) continue;
        if (who->damage_bonus_ticks > 0 && --who->damage_bonus_ticks == 0) who->damage_bonus_percent = 0;
    }
}
