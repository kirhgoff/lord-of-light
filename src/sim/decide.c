#include "decide.h"

#include "combat.h"
#include "vision.h"

static int64_t squared(int64_t value)
{
    return value * value;
}

static int64_t distance_squared(const Combatant *from, int x_millipx, int y_millipx)
{
    return squared(from->x_millipx - x_millipx) + squared(from->y_millipx - y_millipx);
}

static bool is_attack(const Ability *ability)
{
    return ability->order == ORDER_STRIKE || ability->order == ORDER_CAST;
}

static bool has_working_support(const Combatant *c, const GameData *data)
{
    bool working[MAX_SLOTS];
    body_working_parts(&c->body, c->parts, working);
    for (int s = 0; s < c->body.slot_count; s++) {
        const Part *part = &data->parts[c->body.slots[s].part];
        for (int a = 0; working[s] && a < part->ability_count; a++)
            if (part->abilities[a].order == ORDER_SUPPORT) return true;
    }
    return false;
}

int combatant_initiative(const Combatant *c, const GameData *data)
{
    bool working[MAX_SLOTS];
    int total = 0;
    body_working_parts(&c->body, c->parts, working);
    for (int s = 0; s < c->body.slot_count; s++)
        if (working[s]) total += data->parts[c->body.slots[s].part].initiative;
    return total > 0 ? total : 1;
}

static HurtLevel hurt_level_of(const Combatant *c)
{
    int quarters = combatant_health_quarters(c);
    return quarters >= 4 ? HURT_UNHURT : quarters == 3 ? HURT_WOUNDED : quarters == 2 ? HURT_BADLY_WOUNDED : HURT_NEAR_DEATH;
}

static bool is_enemy_of(const Combatant *me, const Perceived *seen)
{
    return seen->kind == PERCEIVED_COMBATANT && (seen->band == BAND_FAR || seen->side != (int)me->charm_side);
}

static bool is_ally_of(const Combatant *me, const Perceived *seen)
{
    return seen->kind == PERCEIVED_COMBATANT && seen->band != BAND_FAR && seen->side == (int)me->charm_side;
}

static const Perceived *nearest_enemy(const Combatant *me, const Perception *perception)
{
    const Perceived *best = NULL;
    int64_t best_distance = 0;
    for (int i = 0; i < perception->seen_count; i++) {
        const Perceived *seen = &perception->seen[i];
        int64_t distance = distance_squared(me, seen->x_millipx, seen->y_millipx);
        if (is_enemy_of(me, seen) && (!best || distance < best_distance)) best = seen, best_distance = distance;
    }
    return best;
}

static const Perceived *nearest_healing_pickup(const Combatant *me, const GameData *data, const Perception *perception)
{
    const Perceived *best = NULL;
    int64_t best_distance = 0;
    for (int i = 0; i < perception->seen_count; i++) {
        const Perceived *seen = &perception->seen[i];
        if (seen->kind != PERCEIVED_PICKUP || (seen->band != BAND_FAR && data->pickups[seen->def].heal <= 0)) continue;
        int64_t distance = distance_squared(me, seen->x_millipx, seen->y_millipx);
        if (!best || distance < best_distance) best = seen, best_distance = distance;
    }
    return best;
}

static int most_vulnerable_element(const Combatant *me, const Combatant *focus, const GameData *data)
{
    bool working[MAX_SLOTS];
    int best = -1, best_resistance = 0;
    body_working_parts(&me->body, me->parts, working);
    for (int s = 0; s < me->body.slot_count; s++) {
        const Part *part = &data->parts[me->body.slots[s].part];
        for (int a = 0; working[s] && a < part->ability_count; a++) {
            if (!is_attack(&part->abilities[a])) continue;
            Element element = part->abilities[a].element;
            int resistance = combatant_resistance_percent(focus, data, element, element == ELEMENT_RADIANCE);
            if (best < 0 || resistance < best_resistance) best = element, best_resistance = resistance;
        }
    }
    return best;
}

static bool some_ally(const Battle *battle, const Combatant *me, const GameData *data, const Perception *perception, bool needs_support, bool needs_help)
{
    for (int i = 0; i < perception->seen_count; i++) {
        const Perceived *seen = &perception->seen[i];
        if (!is_ally_of(me, seen)) continue;
        if (needs_support && !has_working_support(&battle->combatants[seen->index], data)) continue;
        if (needs_help && seen->health_quarters != 1) continue;
        return true;
    }
    return false;
}

bool reflex_decide(Decider *self, const Battle *battle, const GameData *data, int combatant, Decision *out)
{
    (void)self;
    const Combatant *me = &battle->combatants[combatant];
    Perception perception;
    perceive(battle, data, combatant, &perception);
    const Perceived *focus = nearest_enemy(me, &perception);
    const Perceived *healing = nearest_healing_pickup(me, data, &perception);
    HurtLevel hurt = hurt_level_of(me);
    bool healing_is_nearer = healing && (!focus || distance_squared(me, healing->x_millipx, healing->y_millipx) <
                                                       distance_squared(me, focus->x_millipx, focus->y_millipx));
    Stance stance = STANCE_FIGHT;
    if (hurt >= HURT_WOUNDED && healing_is_nearer) stance = STANCE_COLLECT;
    else if (hurt == HURT_NEAR_DEATH && some_ally(battle, me, data, &perception, true, false)) stance = STANCE_RETREAT;
    else if (has_working_support(me, data) && some_ally(battle, me, data, &perception, false, true)) stance = STANCE_SUPPORT;
    *out = (Decision){stance, hurt, focus ? most_vulnerable_element(me, &battle->combatants[focus->index], data) : -1};
    return true;
}

Decider reflex_decider(void)
{
    return (Decider){reflex_decide, DECIDER_REFLEX};
}

static void store_decision(Battle *battle, int combatant, Decision decision, DeciderKind kind)
{
    battle->combatants[combatant].decision = decision;
    battle_log_event(battle, (Event){0, EVENT_DECISION, combatant, -1, -1, -1, decision.vulnerable_element, decision.stance,
                                     decision.hurt_level, kind});
}

static int collect_due(const Battle *battle, const GameData *data, int due[MAX_COMBATANTS])
{
    int count = 0;
    for (int c = 0; c < battle->combatant_count; c++) {
        const Combatant *who = &battle->combatants[c];
        if (!who->alive || who->next_decision_tick > battle->tick) continue;
        int at = count++;
        while (at > 0 && combatant_initiative(&battle->combatants[due[at - 1]], data) < combatant_initiative(who, data)) {
            due[at] = due[at - 1];
            at--;
        }
        due[at] = c;
    }
    return count;
}

static void ask(Battle *battle, const GameData *data, Decider *decider, int combatant)
{
    Combatant *who = &battle->combatants[combatant];
    Decision decision;
    if (decider->decide(decider, battle, data, combatant, &decision)) {
        store_decision(battle, combatant, decision, decider->kind);
        int interval = DECISION_INTERVAL_K / combatant_initiative(who, data);
        who->next_decision_tick = battle->tick + (interval > 1 ? interval : 1);
    } else if (who->decision.stance == STANCE_NONE) {
        reflex_decide(NULL, battle, data, combatant, &decision);
        store_decision(battle, combatant, decision, DECIDER_REFLEX);
    }
}

void battle_decide(Battle *battle, const GameData *data, Decider *decider)
{
    int due[MAX_COMBATANTS];
    int count = collect_due(battle, data, due);
    for (int i = 0; i < count; i++) ask(battle, data, decider, due[i]);
}
