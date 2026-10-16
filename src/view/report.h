#ifndef VIEW_REPORT_H
#define VIEW_REPORT_H

#include <stddef.h>

#include "../sim/battle.h"
#include "../sim/decide.h"

#define LABEL_SIZE (NAME_LEN + 8)
#define EVENT_LINE_SIZE 160

void combatant_label(const Battle *b, int c, char *out, size_t size);
int event_line(const Battle *b, const GameData *data, const Event *e, char *out, size_t size);
int battle_last_decision(const Battle *b, int c);

typedef struct {
    int winner;
    int top_dealer;
    int top_dealer_damage;
    int destroyed_part;
    int destroyed_count;
    int last_to_fall;
    int last_fall_tick;
    int pickups;
    int top_element;
    int top_element_damage;
    DecisionStats decisions;
} BattleReport;

BattleReport report_build(const Battle *b);

#endif
