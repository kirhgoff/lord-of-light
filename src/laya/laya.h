#ifndef LAYA_LAYA_H
#define LAYA_LAYA_H

#include "../sim/briefing.h"
#include "../sim/decide.h"

#define LAYA_OFFLINE_TICKS 100

typedef struct {
    int id;
    char *text;
    int latency_ms;
    Fallback failure;
} LayaReply;

typedef struct LayaTransport LayaTransport;

struct LayaTransport {
    bool (*send)(LayaTransport *self, int id, const char *url, const char *body);
    bool (*poll)(LayaTransport *self, LayaReply *out);
    void (*close)(LayaTransport *self);
};

LayaTransport *laya_curl_transport(int timeout_ms);

typedef struct {
    Decider base;
    LayaTransport *transport;
    bool in_flight[MAX_COMBATANTS];
    LayaQuestions asked[MAX_COMBATANTS];
    bool has_reply[MAX_COMBATANTS];
    LayaReply reply[MAX_COMBATANTS];
    int url_index[MAX_COMBATANTS];
    char *body[MAX_COMBATANTS];
    char briefing[MAX_COMBATANTS][BRIEFING_MAX + 1];
    int offline_until_tick;
    int truncated_count;
} LayaDecider;

void laya_decider_init(LayaDecider *laya, LayaTransport *transport);
void laya_decider_close(LayaDecider *laya);

#endif
