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
    if (e->actor >= 0)
        snprintf(out, size, "%s dies to %s's %s (%s)", target, actor, part_name(b, data, e->actor, e->actor_part), element_name(e->element));
    else
        snprintf(out, size, "%s dies (%s)", target, element_name(e->element));
    return true;
}

static bool decision_text(const Battle *b, const Event *e, char *out, size_t size)
{
    char actor[LABEL_SIZE];
    combatant_label(b, e->actor, actor, sizeof actor);
    if (e->decider == DECIDER_LAYA) {
        snprintf(out, size, "Laya: %s will %s (%d.%03d)", actor, stance_id((Stance)e->amount), e->confidence_permille / 1000,
                 e->confidence_permille % 1000);
        return true;
    }
    if (e->fallback == FALLBACK_NONE || e->fallback == FALLBACK_WAITING) return false;
    snprintf(out, size, "Reflex for %s: Laya %s", actor, fallback_id(e->fallback));
    return true;
}

static bool event_text(const Battle *b, const GameData *data, const Event *e, char *out, size_t size)
{
    char actor[LABEL_SIZE], target[LABEL_SIZE];
    combatant_label(b, e->actor, actor, sizeof actor);
    combatant_label(b, e->target, target, sizeof target);
    switch (e->kind) {
    case EVENT_HIT: return hit_text(b, data, e, out, size);
    case EVENT_EFFECT: return effect_text(b, e, out, size);
    case EVENT_DEATH: return death_text(b, data, e, out, size);
    case EVENT_DECISION: return decision_text(b, e, out, size);
    case EVENT_PICKUP: snprintf(out, size, "%s takes %s", actor, pickup_name(b, data, e->detail)); return true;
    case EVENT_TRAP: snprintf(out, size, "%s triggers %s", target, trap_name(b, data, e->detail)); return true;
    case EVENT_SPAWN: snprintf(out, size, "%s spawns %s", actor, target); return true;
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
