#ifndef SIM_DECIDE_H
#define SIM_DECIDE_H

#include "battle.h"

#define DECISION_INTERVAL_K 60

struct Decider {
    bool (*decide)(Decider *self, const Battle *battle, const GameData *data, int combatant, Decision *out);
    DeciderKind kind;
};

Decider reflex_decider(void);
bool reflex_decide(Decider *self, const Battle *battle, const GameData *data, int combatant, Decision *out);
int combatant_initiative(const Combatant *c, const GameData *data);
void battle_decide(Battle *battle, const GameData *data, Decider *decider);

#endif
