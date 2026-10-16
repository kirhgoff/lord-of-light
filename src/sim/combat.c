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

static int percent_of(int value, int percent)
{
    return (int)((int64_t)value * percent / 100);
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
        damage = percent_of(damage, 100 + battle->combatants[actor].damage_bonus_percent);
    damage = percent_of(damage, 100 - resistance);
    if (is_blocking(victim, data, working)) damage = percent_of(damage, BLOCK_PERCENT);
    int part = pick_target_part(battle, data, victim, working, element);
    if (part < 0) return death_check(battle, data, target, actor, actor_part, element);
    victim->parts[part].integrity = victim->parts[part].integrity > damage ? victim->parts[part].integrity - damage : 0;
    victim->vitality -= percent_of(damage, VITALITY_SHARE_PERCENT);
    if (victim->vitality < 0) victim->vitality = 0;
    battle_log_event(battle, (Event){0, EVENT_HIT, actor, actor_part, target, part, element, damage, victim->parts[part].integrity});
    death_check(battle, data, target, actor, actor_part, element);
}

static int scaled(int ticks, int resistance)
{
    return percent_of(ticks, 100 - resistance);
}

static void log_effect(Battle *battle, int actor, int actor_part, int target, Element element, int amount, EffectKind kind)
{
    battle_log_event(battle, (Event){0, EVENT_EFFECT, actor, actor_part, target, -1, element, amount, kind});
}

static void apply_burning(Combatant *victim, const ElementDef *def, int actor, Element element, int resistance)
{
    int ticks = scaled(def->dot_ticks, resistance);
    if (def->dot_ticks == 0 || ticks == 0) return;
    victim->dot_damage = def->dot_damage;
    victim->dot_element = element;
    victim->dot_source = actor;
    victim->dot_ticks = ticks;
}

static void apply_slow(Battle *battle, Combatant *victim, const ElementDef *def, int actor, int actor_part, int target, Element element, int resistance)
{
    int ticks = scaled(def->slow_ticks, resistance);
    if (def->slow_ticks == 0 || ticks == 0) return;
    if (def->slow_percent > victim->slow_percent) victim->slow_percent = def->slow_percent;
    if (ticks > victim->slow_ticks) victim->slow_ticks = ticks;
    log_effect(battle, actor, actor_part, target, element, ticks, EFFECT_SLOW);
}

static void apply_charm(Battle *battle, Combatant *victim, const ElementDef *def, int actor, int actor_part, int target, Element element, int resistance)
{
    int ticks = scaled(def->charm_ticks, resistance);
    if (def->charm_ticks == 0 || ticks == 0 || actor < 0) return;
    victim->charm_side = battle->combatants[actor].charm_side;
    victim->charm_ticks = ticks;
    log_effect(battle, actor, actor_part, target, element, ticks, EFFECT_CHARM);
}

static void apply_strip(Battle *battle, Combatant *victim, const ElementDef *def, int actor, int actor_part, int target, Element element)
{
    if (def->buff_strip_count == 0 || victim->damage_bonus_ticks == 0) return;
    victim->damage_bonus_ticks = 0;
    victim->damage_bonus_percent = 0;
    log_effect(battle, actor, actor_part, target, element, 1, EFFECT_STRIP);
}

static int nearest_chain_target(const Battle *battle, int from, Side side, const bool struck[MAX_COMBATANTS], int range_px)
{
    int best = -1;
    int64_t best_distance = 0;
    const Combatant *origin = &battle->combatants[from];
    for (int c = 0; c < battle->combatant_count; c++) {
        const Combatant *candidate = &battle->combatants[c];
        if (struck[c] || !candidate->alive || candidate->charm_side == side || !combatant_in_reach(origin, candidate, range_px)) continue;
        int64_t distance = squared(origin->x_millipx - candidate->x_millipx) + squared(origin->y_millipx - candidate->y_millipx);
        if (best < 0 || distance < best_distance) best = c, best_distance = distance;
    }
    return best;
}

static void chain_lightning(Battle *battle, const GameData *data, int actor, int actor_part, int first, int damage)
{
    bool struck[MAX_COMBATANTS] = {false};
    const ElementDef *def = &data->elements[ELEMENT_LIGHTNING];
    int previous = first;
    struck[first] = true;
    for (int hop = 0; hop < def->chain_targets && damage / 2 > 0; hop++) {
        damage /= 2;
        int next = nearest_chain_target(battle, previous, battle->combatants[actor].charm_side, struck, def->chain_range);
        if (next < 0) return;
        struck[next] = true;
        int resistance = combatant_resistance_percent(&battle->combatants[next], data, ELEMENT_LIGHTNING, false);
        hit_once(battle, data, actor, actor_part, next, ELEMENT_LIGHTNING, damage, resistance);
        previous = next;
    }
}

static void apply_element(Battle *battle, const GameData *data, int actor, int actor_part, int target, Element element, int damage, int resistance)
{
    const ElementDef *def = &data->elements[element];
    Combatant *victim = &battle->combatants[target];
    if (def->chain_targets > 0 && actor >= 0) chain_lightning(battle, data, actor, actor_part, target, damage);
    if (!victim->alive) return;
    apply_burning(victim, def, actor, element, resistance);
    apply_slow(battle, victim, def, actor, actor_part, target, element, resistance);
    apply_charm(battle, victim, def, actor, actor_part, target, element, resistance);
    apply_strip(battle, victim, def, actor, actor_part, target, element);
}

void combatant_hit(Battle *battle, const GameData *data, int actor, int actor_part, int target, Element element, int damage)
{
    const Combatant *victim = &battle->combatants[target];
    if (!victim->alive) return;
    int resistance = combatant_resistance_percent(victim, data, element, element == ELEMENT_RADIANCE);
    hit_once(battle, data, actor, actor_part, target, element, damage, resistance);
    apply_element(battle, data, actor, actor_part, target, element, damage, resistance);
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

static void pulse_dot(Battle *battle, const GameData *data, int target)
{
    Combatant *who = &battle->combatants[target];
    who->vitality = who->vitality > who->dot_damage ? who->vitality - who->dot_damage : 0;
    log_effect(battle, who->dot_source, -1, target, (Element)who->dot_element, who->dot_damage, EFFECT_DOT);
    death_check(battle, data, target, who->dot_source, -1, (Element)who->dot_element);
}

void battle_tick_effects(Battle *battle, const GameData *data)
{
    for (int c = 0; c < battle->combatant_count; c++) {
        Combatant *who = &battle->combatants[c];
        if (!who->alive) continue;
        if (who->damage_bonus_ticks > 0 && --who->damage_bonus_ticks == 0) who->damage_bonus_percent = 0;
        if (who->slow_ticks > 0 && --who->slow_ticks == 0) who->slow_percent = 0;
        if (who->charm_ticks > 0 && --who->charm_ticks == 0) who->charm_side = who->side;
        if (who->dot_ticks == 0) continue;
        if (who->dot_ticks % DOT_INTERVAL_TICKS == 0) pulse_dot(battle, data, c);
        who->dot_ticks--;
    }
}
