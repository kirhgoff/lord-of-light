#ifndef SIM_DECIDE_H
#define SIM_DECIDE_H

#include "battle.h"
#include "vision.h"

#define DECISION_INTERVAL_K 60
#define FLEE_PX 64

struct Decider {
    bool (*decide)(Decider *self, const Battle *battle, const GameData *data, int combatant, Event *out);
};

Event decision_event(int combatant, Stance stance, HurtLevel hurt, int element, DeciderKind decider);
Decision event_decision(const Event *event);

Decider reflex_decider(void);
bool reflex_decide(Decider *self, const Battle *battle, const GameData *data, int combatant, Event *out);

HurtLevel hurt_level_from_quarters(int quarters);
bool perceived_is_enemy(const Combatant *me, const Perceived *seen);
bool perceived_is_ally(const Combatant *me, const Perceived *seen);
const Perceived *perception_nearest_enemy(const Combatant *me, const Perception *perception);
int combatant_initiative(const Combatant *c, const GameData *data);
void tactics_apply(Battle *battle, const GameData *data, int combatant);
void battle_decide(Battle *battle, const GameData *data, Decider *decider);

#endif
