#ifndef SIM_COMBAT_H
#define SIM_COMBAT_H

#include "battle.h"

#define VITALITY_SHARE_PERCENT 50
#define BLOCK_PERCENT 50
#define DOT_INTERVAL_TICKS 20

int combatant_resistance_percent(const Combatant *c, const GameData *data, Element element, bool internal_only);
bool combatant_in_reach(const Combatant *from, const Combatant *to, int range_px);
bool combatant_has_working_heart(const Combatant *c, const GameData *data);
void combatant_hit(Battle *battle, const GameData *data, int actor, int actor_part, int target, Element element, int damage);
void battle_act(Battle *battle, const GameData *data);
void battle_tick_effects(Battle *battle, const GameData *data);

#endif
