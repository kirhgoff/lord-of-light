#include "report.h"

#include <stdio.h>
#include <string.h>

#include "../sim/movement.h"

void combatant_label(const Battle *b, int c, char *out, size_t size)
{
    if (c < 0 || c >= b->combatant_count) {
        snprintf(out, size, "?");
        return;
    }
    const char *name = b->combatants[c].name;
    int number = 1;
    bool shared = false;
    for (int d = 0; d < b->combatant_count; d++) {
        if (d == c || strcmp(b->combatants[d].name, name) != 0) continue;
        shared = true;
        number += d < c;
    }
    if (shared) snprintf(out, size, "%s %d", name, number);
    else snprintf(out, size, "%s", name);
}

static const char *part_name(const Battle *b, const GameData *data, int c, int slot)
{
    if (c < 0 || c >= b->combatant_count) return "?";
    const Body *body = &b->combatants[c].body;
    if (slot < 0 || slot >= body->slot_count) return "?";
    return data->parts[body->slots[slot].part].name;
}

static const char *element_name(int element)
{
    return element >= 0 && element < ELEMENT_COUNT ? element_id((Element)element) : "?";
}

static const char *pickup_name(const Battle *b, const GameData *data, int index)
{
    return index >= 0 && index < b->pickup_count ? data->pickups[b->pickups[index].def].name : "?";
}

static const char *trap_name(const Battle *b, const GameData *data, int index)
{
    return index >= 0 && index < b->trap_count ? data->traps[b->traps[index].def].name : "?";
}

static bool hit_text(const Battle *b, const GameData *data, const Event *e, char *out, size_t size)
{
    char actor[LABEL_SIZE], target[LABEL_SIZE];
    combatant_label(b, e->actor, actor, sizeof actor);
    combatant_label(b, e->target, target, sizeof target);
    const char *tail = e->detail == 0 ? " - destroyed" : "";
    if (e->actor >= 0)
        snprintf(out, size, "%s's %s hits %s's %s for %d %s%s", actor, part_name(b, data, e->actor, e->actor_part), target,
                 part_name(b, data, e->target, e->target_part), e->amount, element_name(e->element), tail);
    else
        snprintf(out, size, "A Trap hits %s's %s for %d %s%s", target, part_name(b, data, e->target, e->target_part), e->amount,
                 element_name(e->element), tail);
    return true;
}

static bool effect_text(const Battle *b, const Event *e, char *out, size_t size)
{
    char actor[LABEL_SIZE], target[LABEL_SIZE];
    combatant_label(b, e->actor, actor, sizeof actor);
    combatant_label(b, e->target, target, sizeof target);
    switch ((EffectKind)e->detail) {
    case EFFECT_DOT: snprintf(out, size, "%s takes %d %s", target, e->amount, element_name(e->element)); return true;
    case EFFECT_HEAL: snprintf(out, size, "%s heals %s for %d", actor, target, e->amount); return true;
    case EFFECT_SLOW: snprintf(out, size, "%s is slowed by %s", target, actor); return true;
    case EFFECT_CHARM: snprintf(out, size, "%s is charmed by %s", target, actor); return true;
    case EFFECT_STRIP: snprintf(out, size, "%s loses %d buffs", target, e->amount); return true;
    }
    return false;
}

static bool death_text(const Battle *b, const GameData *data, const Event *e, char *out, size_t size)
{
    char actor[LABEL_SIZE], target[LABEL_SIZE];
    combatant_label(b, e->actor, actor, sizeof actor);
    combatant_label(b, e->target, target, sizeof target);
    if (e->actor >= 0 && e->actor_part >= 0)
        snprintf(out, size, "%s dies to %s's %s (%s)", target, actor, part_name(b, data, e->actor, e->actor_part), element_name(e->element));
    else if (e->actor >= 0)
        snprintf(out, size, "%s dies to %s (%s)", target, actor, element_name(e->element));
    else
        snprintf(out, size, "%s dies (%s)", target, element_name(e->element));
    return true;
}

static bool decision_text(const Battle *b, const Event *e, char *out, size_t size)
{
    char actor[LABEL_SIZE];
    combatant_label(b, e->actor, actor, sizeof actor);
    if (e->decider == DECIDER_LAYA || e->decider == DECIDER_JEV) {
        snprintf(out, size, "%s: %s will %s (%d.%03d)", decider_name(e->decider), actor, stance_id((Stance)e->amount), e->confidence_permille / 1000,
                 e->confidence_permille % 1000);
        return true;
    }
    if (e->fallback == FALLBACK_NONE || e->fallback == FALLBACK_WAITING) return false;
    snprintf(out, size, "Reflex for %s: %s", actor, fallback_id(e->fallback));
    return true;
}

static bool simple_text(const Battle *b, const GameData *data, const Event *e, char *out, size_t size)
{
    char actor[LABEL_SIZE], target[LABEL_SIZE];
    combatant_label(b, e->actor, actor, sizeof actor);
    combatant_label(b, e->target, target, sizeof target);
    switch (e->kind) {
    case EVENT_PICKUP: snprintf(out, size, "%s takes %s", actor, pickup_name(b, data, e->detail)); return true;
    case EVENT_TRAP: snprintf(out, size, "%s triggers %s", target, trap_name(b, data, e->detail)); return true;
    case EVENT_SPAWN: snprintf(out, size, "%s spawns %s", actor, target); return true;
    default: return false;
    }
}

static bool event_text(const Battle *b, const GameData *data, const Event *e, char *out, size_t size)
{
    switch (e->kind) {
    case EVENT_HIT: return hit_text(b, data, e, out, size);
    case EVENT_EFFECT: return effect_text(b, e, out, size);
    case EVENT_DEATH: return death_text(b, data, e, out, size);
    case EVENT_DECISION: return decision_text(b, e, out, size);
    case EVENT_PICKUP:
    case EVENT_TRAP:
    case EVENT_SPAWN: return simple_text(b, data, e, out, size);
    case EVENT_KIND_COUNT: break;
    }
    return false;
}

int event_line(const Battle *b, const GameData *data, const Event *e, char *out, size_t size)
{
    char text[EVENT_LINE_SIZE];
    if (!event_text(b, data, e, text, sizeof text)) return 0;
    snprintf(out, size, "%d.%ds %s", e->tick / TICKS_PER_SECOND, e->tick % TICKS_PER_SECOND / 2, text);
    return (int)strlen(out);
}

int battle_last_decision(const Battle *b, int c)
{
    for (int e = b->log_count - 1; e >= 0; e--)
        if (b->log[e].kind == EVENT_DECISION && b->log[e].actor == c) return e;
    return -1;
}

static int argmax_first(const int *values, int count)
{
    int best = -1;
    for (int i = 0; i < count; i++)
        if (values[i] > 0 && (best < 0 || values[i] > values[best])) best = i;
    return best;
}

typedef struct {
    int damage_by[MAX_COMBATANTS];
    int destroyed_by[MAX_PARTS];
    int damage_by_element[ELEMENT_COUNT];
} Tallies;

static void tally_hit(const Battle *b, const Event *e, Tallies *t)
{
    if (e->actor >= 0 && e->actor < b->combatant_count) t->damage_by[e->actor] += e->amount;
    if (e->element >= 0 && e->element < ELEMENT_COUNT) t->damage_by_element[e->element] += e->amount;
    bool part_known = e->target >= 0 && e->target < b->combatant_count && e->target_part >= 0 &&
                      e->target_part < b->combatants[e->target].body.slot_count;
    if (e->detail == 0 && part_known) t->destroyed_by[b->combatants[e->target].body.slots[e->target_part].part]++;
}

static void note_fall(BattleReport *r, const Event *e)
{
    bool later = e->tick > r->last_fall_tick || (e->tick == r->last_fall_tick && e->target < r->last_to_fall);
    if (later) r->last_to_fall = e->target, r->last_fall_tick = e->tick;
}

BattleReport report_build(const Battle *b)
{
    BattleReport r = {.winner = battle_winner(b), .last_to_fall = -1, .last_fall_tick = -1};
    Tallies t = {{0}, {0}, {0}};
    for (int i = 0; i < b->log_count; i++) {
        const Event *e = &b->log[i];
        if (e->kind == EVENT_HIT) tally_hit(b, e, &t);
        else if (e->kind == EVENT_PICKUP) r.pickups++;
        else if (e->kind == EVENT_DEATH && e->target >= 0 && e->target < b->combatant_count) note_fall(&r, e);
    }
    r.top_dealer = argmax_first(t.damage_by, b->combatant_count);
    r.top_dealer_damage = r.top_dealer >= 0 ? t.damage_by[r.top_dealer] : 0;
    r.destroyed_part = argmax_first(t.destroyed_by, MAX_PARTS);
    r.destroyed_count = r.destroyed_part >= 0 ? t.destroyed_by[r.destroyed_part] : 0;
    r.top_element = argmax_first(t.damage_by_element, ELEMENT_COUNT);
    r.top_element_damage = r.top_element >= 0 ? t.damage_by_element[r.top_element] : 0;
    r.decisions = battle_decision_stats(b);
    return r;
}
