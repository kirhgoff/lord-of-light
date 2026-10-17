#ifndef SIM_BRIEFING_H
#define SIM_BRIEFING_H

#include "battle.h"

#define BRIEFING_MAX 1200
#define LAYA_REPLY_MAX ((size_t)1024 * 1024)

typedef struct {
    bool stance_offered[STANCE_COUNT];
    int elements[ELEMENT_COUNT];
    int element_count;
    int fixed_element;
} LayaQuestions;

int briefing_write(const Battle *battle, const GameData *data, int combatant, char *out, size_t size);

char *laya_request_json(const Battle *battle, const GameData *data, int combatant, const char *briefing, const char *model,
                        LayaQuestions *asked);
char *laya_request_record_json(const Battle *battle, const GameData *data, int combatant, const Event *reflex);
Event laya_decision(const Battle *battle, const GameData *data, int combatant, const LayaQuestions *asked, const char *reply_text,
                    DeciderKind source, Fallback failure, int answer_ms, bool *truncated);

#endif
