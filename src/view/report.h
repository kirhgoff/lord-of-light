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

#endif
