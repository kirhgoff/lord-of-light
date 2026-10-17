#ifndef LAYA_LAYA_H
#define LAYA_LAYA_H

#include "../sim/briefing.h"
#include "../sim/decide.h"

#define LAYA_OFFLINE_TICKS 100

typedef struct {
    int id;
    char *text;
    Fallback failure;
} LayaReply;

typedef struct LayaTransport LayaTransport;

struct LayaTransport {
    bool (*send)(LayaTransport *self, int id, const char *url, const char *body);
    bool (*poll)(LayaTransport *self, LayaReply *out);
    void (*close)(LayaTransport *self);
};

typedef struct {
    bool curl_ok;
    bool timed_out;
    bool connected;
    bool too_large;
    long http_status;
} TransferOutcome;

Fallback laya_classify_transfer(const TransferOutcome *outcome);

LayaTransport *laya_curl_transport(int timeout_ms, const char *bearer_key);

#define LAYA_KEY_LEN 256

typedef enum { LAYA_KEY_OK,
               LAYA_KEY_EMPTY,
               LAYA_KEY_TOO_LONG,
               LAYA_KEY_INVALID } LayaKeyStatus;

LayaKeyStatus laya_parse_key(const char *text, char *key, size_t size);

typedef struct {
    Decider base;
    LayaTransport *transport;
    DeciderKind source;
    char urls[LAYA_MAX_URLS][LAYA_URL_LEN];
    int url_count;
    char model[LAYA_MODEL_LEN];
    bool in_flight[MAX_COMBATANTS];
    int stale_replies[MAX_COMBATANTS];
    LayaQuestions asked[MAX_COMBATANTS];
    bool has_reply[MAX_COMBATANTS];
    LayaReply reply[MAX_COMBATANTS];
    int url_index[MAX_COMBATANTS];
    char *body[MAX_COMBATANTS];
    char briefing[MAX_COMBATANTS][BRIEFING_MAX + 1];
    int (*clock_ms)(void);
    int queued_ms[MAX_COMBATANTS];
    int offline_until_tick;
    Fallback offline_reason;
    int truncated_count;
} LayaDecider;

typedef struct {
    DeciderKind source;
    const char *const *urls;
    int url_count;
    const char *model;
} LayaEndpoint;

void laya_decider_init(LayaDecider *laya, LayaTransport *transport, const LayaEndpoint *endpoint);
void laya_decider_close(LayaDecider *laya);
void laya_decider_reset(LayaDecider *laya);

#endif
