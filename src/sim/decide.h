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
typedef struct {
    Decider base;
    const Event *log;
    int count;
    int next[MAX_COMBATANTS];
} ReplayDecider;

void replay_decider_init(ReplayDecider *replay, const Event *log, int count);

typedef struct {
    int laya;
    int reflex;
    int fallback[FALLBACK_COUNT];
    int mean_confidence_permille;
    int mean_latency_ms;
} DecisionStats;

DecisionStats battle_decision_stats(const Battle *battle);
void battle_decide(Battle *battle, const GameData *data, Decider *decider);

#endif
