#ifndef SIM_BRIEFING_H
#define SIM_BRIEFING_H

#include "battle.h"

#define BRIEFING_MAX 1200
#define LAYA_REPLY_MAX (1024 * 1024)

typedef struct {
    bool stance_offered[STANCE_COUNT];
    int elements[ELEMENT_COUNT];
    int element_count;
    int fixed_element;
} LayaQuestions;

int briefing_write(const Battle *battle, const GameData *data, int combatant, char *out, size_t size);

char *laya_request_json(const Battle *battle, const GameData *data, int combatant, const char *briefing, LayaQuestions *asked);
Event laya_decision(const Battle *battle, const GameData *data, int combatant, const LayaQuestions *asked, const char *reply_text,
                    Fallback failure, int latency_ms, bool *truncated);

#endif
