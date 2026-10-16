#include "decide.h"

#include "combat.h"
#include "movement.h"
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

static const Ability *find_working_ability(const Combatant *c, const GameData *data, OrderKind kind)
{
    bool working[MAX_SLOTS];
    body_working_parts(&c->body, c->parts, working);
    for (int s = 0; s < c->body.slot_count; s++) {
        const Part *part = &data->parts[c->body.slots[s].part];
        for (int a = 0; working[s] && a < part->ability_count; a++)
            if (part->abilities[a].order == kind) return &part->abilities[a];
    }
    return NULL;
}

bool combatant_has_working_support(const Combatant *c, const GameData *data)
{
    return find_working_ability(c, data, ORDER_SUPPORT) != NULL;
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

Event decision_event(int combatant, Stance stance, HurtLevel hurt, int element, DeciderKind decider)
{
    return (Event){.tick = 0, .kind = EVENT_DECISION, .actor = combatant, .actor_part = -1, .target = -1, .target_part = -1, .element = element, .amount = stance, .detail = hurt, .decider = decider};
}

Decision event_decision(const Event *event)
{
    return (Decision){(Stance)event->amount, (HurtLevel)event->detail, event->element};
}

HurtLevel hurt_level_from_quarters(int quarters)
{
    return quarters >= 4 ? HURT_UNHURT : quarters == 3 ? HURT_WOUNDED
                                     : quarters == 2   ? HURT_BADLY_WOUNDED
                                                       : HURT_NEAR_DEATH;
}

bool perceived_is_enemy(const Combatant *me, const Perceived *seen)
{
    return seen->kind == PERCEIVED_COMBATANT && (seen->band == BAND_FAR || seen->side != (int)me->charm_side);
}

bool perceived_is_ally(const Combatant *me, const Perceived *seen)
{
    return seen->kind == PERCEIVED_COMBATANT && seen->band != BAND_FAR && seen->side == (int)me->charm_side;
}

const Perceived *perception_nearest_enemy(const Combatant *me, const Perception *perception)
{
    const Perceived *best = NULL;
    int64_t best_distance = 0;
    for (int i = 0; i < perception->seen_count; i++) {
        const Perceived *seen = &perception->seen[i];
        int64_t distance = distance_squared(me, seen->x_millipx, seen->y_millipx);
        if (perceived_is_enemy(me, seen) && (!best || distance < best_distance)) best = seen, best_distance = distance;
    }
    return best;
}

static const Perceived *nearest_pickup(const Combatant *me, const GameData *data, const Perception *perception, bool healing_only)
{
    const Perceived *best = NULL;
    int64_t best_distance = 0;
    for (int i = 0; i < perception->seen_count; i++) {
        const Perceived *seen = &perception->seen[i];
        if (seen->kind != PERCEIVED_PICKUP) continue;
        if (healing_only && seen->band != BAND_FAR && data->pickups[seen->def].heal <= 0) continue;
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
        if (!perceived_is_ally(me, seen)) continue;
        if (needs_support && !combatant_has_working_support(&battle->combatants[seen->index], data)) continue;
        if (needs_help && seen->health_quarters != 1) continue;
        return true;
    }
    return false;
}

bool reflex_decide(Decider *self, const Battle *battle, const GameData *data, int combatant, Event *out)
{
    (void)self;
    const Combatant *me = &battle->combatants[combatant];
    Perception perception;
    perceive(battle, data, combatant, &perception);
    const Perceived *focus = perception_nearest_enemy(me, &perception);
    const Perceived *healing = nearest_pickup(me, data, &perception, true);
    HurtLevel hurt = hurt_level_from_quarters(combatant_health_quarters(me));
    bool healing_is_nearer = healing && (!focus || distance_squared(me, healing->x_millipx, healing->y_millipx) <
                                                       distance_squared(me, focus->x_millipx, focus->y_millipx));
    Stance stance = STANCE_FIGHT;
    if (hurt >= HURT_WOUNDED && healing_is_nearer) stance = STANCE_COLLECT;
    else if (hurt == HURT_NEAR_DEATH && some_ally(battle, me, data, &perception, true, false)) stance = STANCE_RETREAT;
    else if (combatant_has_working_support(me, data) && some_ally(battle, me, data, &perception, false, true)) stance = STANCE_SUPPORT;
    *out = decision_event(combatant, stance, hurt, focus ? most_vulnerable_element(me, &battle->combatants[focus->index], data) : -1, DECIDER_REFLEX);
    return true;
}

Decider reflex_decider(void)
{
    return (Decider){reflex_decide};
}

static bool replay_decide(Decider *self, const Battle *battle, const GameData *data, int combatant, Event *out)
{
    (void)data;
    ReplayDecider *replay = (ReplayDecider *)self;
    for (int i = replay->next[combatant]; i < replay->count; i++) {
        const Event *logged = &replay->log[i];
        if (logged->kind != EVENT_DECISION || logged->actor != combatant) continue;
        if (logged->tick != battle->tick) return false;
        replay->next[combatant] = i + 1;
        if (logged->fallback == FALLBACK_WAITING) return false;
        *out = *logged;
        return true;
    }
    return false;
}

void replay_decider_init(ReplayDecider *replay, const Event *log, int count)
{
    *replay = (ReplayDecider){.base = {replay_decide}, .log = log, .count = count};
}

DecisionStats battle_decision_stats(const Battle *battle)
{
    DecisionStats stats = {0};
    long confidence_sum = 0, answer_sum = 0;
    int answer_count = 0;
    for (int e = 0; e < battle->log_count; e++) {
        const Event *event = &battle->log[e];
        if (event->kind != EVENT_DECISION) continue;
        stats.fallback[event->fallback]++;
        if (event->decider == DECIDER_LAYA) stats.laya++, confidence_sum += event->confidence_permille;
        else stats.reflex++;
        if (event->answer_ms > 0) answer_sum += event->answer_ms, answer_count++;
    }
    stats.mean_confidence_permille = stats.laya ? (int)(confidence_sum / stats.laya) : 0;
    stats.mean_answer_ms = answer_count ? (int)(answer_sum / answer_count) : 0;
    return stats;
}

typedef struct {
    const Battle *battle;
    const GameData *data;
    const Combatant *me;
    bool working[MAX_SLOTS];
    Perception perception;
    const Perceived *focus;
    const Ability *preferred_attack;
    int most_hurt;
} Situation;

typedef struct {
    bool has_goal;
    int x_millipx;
    int y_millipx;
    bool hold;
} Movement;

static const Ability *pick_attack(const Situation *situation)
{
    const Ability *longest = NULL;
    for (int s = 0; s < situation->me->body.slot_count; s++) {
        const Part *part = &situation->data->parts[situation->me->body.slots[s].part];
        for (int a = 0; situation->working[s] && a < part->ability_count; a++) {
            const Ability *ability = &part->abilities[a];
            if (!is_attack(ability)) continue;
            if ((int)ability->element == situation->me->decision.vulnerable_element) return ability;
            if (!longest || ability->range > longest->range) longest = ability;
        }
    }
    return longest;
}

static int pick_most_hurt(const Situation *situation, int self)
{
    int best = self, best_quarters = combatant_health_quarters(situation->me);
    for (int i = 0; i < situation->perception.seen_count; i++) {
        const Perceived *seen = &situation->perception.seen[i];
        if (perceived_is_ally(situation->me, seen) && seen->health_quarters < best_quarters) best = seen->index, best_quarters = seen->health_quarters;
    }
    return best;
}

static const Perceived *nearest_healer(const Situation *situation)
{
    const Perceived *best = NULL;
    int64_t best_distance = 0;
    for (int i = 0; i < situation->perception.seen_count; i++) {
        const Perceived *seen = &situation->perception.seen[i];
        int64_t distance = distance_squared(situation->me, seen->x_millipx, seen->y_millipx);
        if (perceived_is_ally(situation->me, seen) && combatant_has_working_support(&situation->battle->combatants[seen->index], situation->data) &&
            (!best || distance < best_distance))
            best = seen, best_distance = distance;
    }
    return best;
}

static const Perceived *most_hurt_ally(const Situation *situation)
{
    const Perceived *best = NULL;
    for (int i = 0; i < situation->perception.seen_count; i++) {
        const Perceived *seen = &situation->perception.seen[i];
        if (perceived_is_ally(situation->me, seen) && seen->health_quarters < 4 && (!best || seen->health_quarters < best->health_quarters)) best = seen;
    }
    return best;
}

static int nearest_opponent(const Situation *situation)
{
    int best = -1;
    int64_t best_distance = 0;
    for (int c = 0; c < situation->battle->combatant_count; c++) {
        const Combatant *other = &situation->battle->combatants[c];
        int64_t distance = distance_squared(situation->me, other->x_millipx, other->y_millipx);
        if (other->alive && other->charm_side != situation->me->charm_side && (best < 0 || distance < best_distance))
            best = c, best_distance = distance;
    }
    return best;
}

static Movement go_to(int x_millipx, int y_millipx, bool hold)
{
    return (Movement){true, x_millipx, y_millipx, hold};
}

static Movement fight_movement(const Situation *situation)
{
    const Battle *battle = situation->battle;
    if (situation->focus && situation->preferred_attack) {
        const Combatant *focus = &battle->combatants[situation->focus->index];
        return go_to(focus->x_millipx, focus->y_millipx, combatant_in_reach(situation->me, focus, situation->preferred_attack->range));
    }
    if (situation->focus) return (Movement){0};
    int opponent = nearest_opponent(situation);
    if (opponent < 0) return (Movement){0};
    return go_to(battle->combatants[opponent].x_millipx, battle->combatants[opponent].y_millipx, false);
}

static int clamp_to_arena(int millipx, int limit)
{
    return millipx < 0 ? 0 : millipx >= limit ? limit - 1
                                              : millipx;
}

static Movement flee_movement(const Situation *situation)
{
    const Combatant *me = situation->me;
    const Perceived *healer = nearest_healer(situation);
    if (healer) {
        const Combatant *ally = &situation->battle->combatants[healer->index];
        const Ability *support = find_working_ability(ally, situation->data, ORDER_SUPPORT);
        return go_to(ally->x_millipx, ally->y_millipx, combatant_in_reach(me, ally, support->range));
    }
    if (!situation->focus) return (Movement){0};
    int64_t dx = me->x_millipx - situation->focus->x_millipx, dy = me->y_millipx - situation->focus->y_millipx;
    int64_t distance = isqrt(squared(dx) + squared(dy));
    if (distance == 0) return (Movement){0};
    int64_t flee = (int64_t)FLEE_PX * MILLIPX_PER_PX;
    return go_to(clamp_to_arena((int)(me->x_millipx + dx * flee / distance), ARENA_WIDTH_MILLIPX),
                 clamp_to_arena((int)(me->y_millipx + dy * flee / distance), ARENA_HEIGHT_MILLIPX), false);
}

static Movement stance_movement(const Situation *situation)
{
    const Combatant *me = situation->me;
    switch (me->decision.stance) {
    case STANCE_SUPPORT: {
        const Perceived *ally = most_hurt_ally(situation);
        const Ability *support = find_working_ability(me, situation->data, ORDER_SUPPORT);
        if (ally && support) {
            const Combatant *hurt = &situation->battle->combatants[ally->index];
            return go_to(hurt->x_millipx, hurt->y_millipx, combatant_in_reach(me, hurt, support->range));
        }
        break;
    }
    case STANCE_RETREAT: return flee_movement(situation);
    case STANCE_COLLECT: {
        const Perceived *pickup = nearest_pickup(me, situation->data, &situation->perception, true);
        if (!pickup) pickup = nearest_pickup(me, situation->data, &situation->perception, false);
        if (pickup) return go_to(pickup->x_millipx, pickup->y_millipx, false);
        break;
    }
    default: break;
    }
    return fight_movement(situation);
}

static bool waypoint_around(const Combatant *me, int goal_x, int goal_y, int trap_x, int trap_y, int64_t avoid, int *x_millipx, int *y_millipx)
{
    int64_t dx = goal_x - me->x_millipx, dy = goal_y - me->y_millipx;
    int64_t vx = trap_x - me->x_millipx, vy = trap_y - me->y_millipx;
    int64_t along = vx * dx + vy * dy, length_squared = dx * dx + dy * dy;
    if (length_squared == 0 || along < 0 || along > length_squared) return false;
    int64_t px = me->x_millipx + dx * along / length_squared, py = me->y_millipx + dy * along / length_squared;
    int64_t off_x = px - trap_x, off_y = py - trap_y, off_squared = off_x * off_x + off_y * off_y;
    if (off_squared >= avoid * avoid) return false;
    if (off_squared == 0) {
        int64_t length = isqrt(length_squared);
        off_x = -dy * avoid / length, off_y = dx * avoid / length;
    } else {
        int64_t off = isqrt(off_squared);
        off_x = off_x * avoid / off, off_y = off_y * avoid / off;
    }
    *x_millipx = clamp_to_arena((int)(trap_x + off_x), ARENA_WIDTH_MILLIPX);
    *y_millipx = clamp_to_arena((int)(trap_y + off_y), ARENA_HEIGHT_MILLIPX);
    return true;
}

static void avoid_traps(const Situation *situation, Movement *movement)
{
    for (int i = 0; i < situation->perception.seen_count; i++) {
        const Perceived *seen = &situation->perception.seen[i];
        if (seen->kind != PERCEIVED_TRAP) continue;
        int64_t avoid = (int64_t)situation->data->traps[seen->def].radius * MILLIPX_PER_PX + 2 * COMBATANT_RADIUS_MILLIPX;
        if (waypoint_around(situation->me, movement->x_millipx, movement->y_millipx, seen->x_millipx, seen->y_millipx, avoid,
                            &movement->x_millipx, &movement->y_millipx))
            return;
    }
}

static Order attack_order(const Situation *situation, const Part *part)
{
    const Ability *first = NULL;
    const Combatant *focus = &situation->battle->combatants[situation->focus->index];
    for (int a = 0; a < part->ability_count; a++) {
        const Ability *ability = &part->abilities[a];
        if (!is_attack(ability)) continue;
        if (combatant_in_reach(situation->me, focus, ability->range)) return (Order){ability->order, situation->focus->index, 0, 0};
        if (!first) first = ability;
    }
    return (Order){first->order, situation->focus->index, 0, 0};
}

static bool part_has(const Part *part, bool (*matches)(const Ability *))
{
    for (int a = 0; a < part->ability_count; a++)
        if (matches(&part->abilities[a])) return true;
    return false;
}

static bool is_support(const Ability *ability)
{
    return ability->order == ORDER_SUPPORT;
}
static bool is_block(const Ability *ability)
{
    return ability->order == ORDER_BLOCK;
}
static bool is_spawn(const Ability *ability)
{
    return ability->order == ORDER_SPAWN;
}

static Order order_for_part(const Situation *situation, const Part *part, const Movement *movement)
{
    if (part->move_speed_px_per_second > 0 && movement->has_goal && !movement->hold)
        return (Order){ORDER_MOVE_TO, -1, movement->x_millipx, movement->y_millipx};
    if (situation->focus && part_has(part, is_attack)) return attack_order(situation, part);
    if (part_has(part, is_support)) return (Order){ORDER_SUPPORT, situation->most_hurt, 0, 0};
    if (part_has(part, is_block)) return (Order){ORDER_BLOCK, -1, 0, 0};
    if (part_has(part, is_spawn)) return (Order){ORDER_SPAWN, -1, 0, 0};
    return (Order){ORDER_IDLE, -1, 0, 0};
}

void tactics_apply(Battle *battle, const GameData *data, int combatant)
{
    Combatant *me = &battle->combatants[combatant];
    Situation situation = {.battle = battle, .data = data, .me = me};
    body_working_parts(&me->body, me->parts, situation.working);
    perceive(battle, data, combatant, &situation.perception);
    situation.focus = perception_nearest_enemy(me, &situation.perception);
    situation.preferred_attack = pick_attack(&situation);
    situation.most_hurt = pick_most_hurt(&situation, combatant);
    Movement movement = stance_movement(&situation);
    if (movement.has_goal && !movement.hold) avoid_traps(&situation, &movement);
    for (int s = 0; s < me->body.slot_count; s++)
        if (situation.working[s]) me->parts[s].order = order_for_part(&situation, &data->parts[me->body.slots[s].part], &movement);
}

static void store_decision(Battle *battle, Event event)
{
    battle->combatants[event.actor].decision = event_decision(&event);
    battle_log_event(battle, event);
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

static int decision_interval(const Combatant *who, const GameData *data)
{
    int interval = DECISION_INTERVAL_K / combatant_initiative(who, data);
    return interval > 1 ? interval : 1;
}

static void ask(Battle *battle, const GameData *data, Decider *decider, int combatant)
{
    Combatant *who = &battle->combatants[combatant];
    Event event;
    if (decider->decide(decider, battle, data, combatant, &event)) {
        event.actor = combatant;
        store_decision(battle, event);
        who->next_decision_tick = battle->tick + decision_interval(who, data);
        who->next_waiting_tick = who->next_decision_tick;
    } else if (battle->tick >= who->next_waiting_tick) {
        reflex_decide(NULL, battle, data, combatant, &event);
        event.fallback = FALLBACK_WAITING;
        store_decision(battle, event);
        who->next_waiting_tick = battle->tick + decision_interval(who, data);
    }
}

void battle_decide(Battle *battle, const GameData *data, Decider *decider)
{
    int due[MAX_COMBATANTS];
    int count = collect_due(battle, data, due);
    for (int i = 0; i < count; i++) ask(battle, data, decider, due[i]);
    for (int c = 0; c < battle->combatant_count; c++)
        if (battle->combatants[c].alive && battle->combatants[c].decision.stance != STANCE_NONE) tactics_apply(battle, data, c);
}
