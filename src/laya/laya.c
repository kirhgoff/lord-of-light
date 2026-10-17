#include "laya.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int monotonic_ms(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (int)(now.tv_sec * 1000 + now.tv_nsec / 1000000);
}

Fallback laya_classify_transfer(const TransferOutcome *outcome)
{
    if (outcome->too_large) return FALLBACK_MALFORMED;
    if (outcome->curl_ok) return outcome->http_status == 200 ? FALLBACK_NONE : FALLBACK_MALFORMED;
    if (outcome->timed_out && outcome->connected) return FALLBACK_TIMEOUT;
    return FALLBACK_UNREACHABLE;
}

static bool resend_to_next_url(LayaDecider *laya, int combatant, Fallback failure)
{
    int next = laya->url_index[combatant] + 1;
    if (failure != FALLBACK_UNREACHABLE || next >= laya->url_count) return false;
    if (!laya->transport->send(laya->transport, combatant, laya->urls[next], laya->body[combatant])) return false;
    laya->url_index[combatant] = next;
    return true;
}

static void drain_replies(LayaDecider *laya)
{
    LayaReply reply;
    while (laya->transport->poll(laya->transport, &reply)) {
        int c = reply.id;
        if (c < 0 || c >= MAX_COMBATANTS || resend_to_next_url(laya, c, reply.failure)) {
            free(reply.text);
            continue;
        }
        free(laya->reply[c].text);
        laya->reply[c] = reply;
        laya->has_reply[c] = true;
        laya->in_flight[c] = false;
    }
}

static Event take_reply(LayaDecider *laya, const Battle *battle, const GameData *data, int c)
{
    const LayaReply *reply = &laya->reply[c];
    bool truncated;
    Event event = laya_decision(battle, data, c, &laya->asked[c], reply->text, laya->source, reply->failure, laya->clock_ms() - laya->queued_ms[c], &truncated);
    laya->truncated_count += truncated;
    if (reply->failure == FALLBACK_UNREACHABLE || reply->failure == FALLBACK_TIMEOUT) {
        laya->offline_until_tick = battle->tick + LAYA_OFFLINE_TICKS;
        laya->offline_reason = reply->failure;
    }
    free(laya->reply[c].text);
    laya->reply[c].text = NULL;
    free(laya->body[c]);
    laya->body[c] = NULL;
    laya->has_reply[c] = false;
    return event;
}

static void ask_laya(LayaDecider *laya, const Battle *battle, const GameData *data, int c)
{
    briefing_write(battle, data, c, laya->briefing[c], sizeof laya->briefing[c]);
    free(laya->body[c]);
    laya->body[c] = laya_request_json(battle, data, c, laya->briefing[c], laya->model[0] ? laya->model : NULL, &laya->asked[c]);
    laya->url_index[c] = 0;
    laya->queued_ms[c] = laya->clock_ms();
    laya->in_flight[c] = laya->transport->send(laya->transport, c, laya->urls[0], laya->body[c]);
}

static bool laya_decide(Decider *self, const Battle *battle, const GameData *data, int combatant, Event *out)
{
    LayaDecider *laya = (LayaDecider *)self;
    drain_replies(laya);
    if (laya->has_reply[combatant]) {
        *out = take_reply(laya, battle, data, combatant);
        return true;
    }
    if (laya->in_flight[combatant]) return false;
    if (battle->tick < laya->offline_until_tick) {
        reflex_decide(NULL, battle, data, combatant, out);
        out->fallback = laya->offline_reason;
        return true;
    }
    ask_laya(laya, battle, data, combatant);
    return false;
}

bool laya_parse_key(const char *text, char *key, size_t size)
{
    while (isspace((unsigned char)*text)) text++;
    size_t length = strlen(text);
    while (length > 0 && isspace((unsigned char)text[length - 1])) length--;
    if (length == 0 || length >= size) return false;
    memcpy(key, text, length);
    key[length] = '\0';
    return true;
}

void laya_decider_init(LayaDecider *laya, LayaTransport *transport, const LayaEndpoint *endpoint)
{
    memset(laya, 0, sizeof *laya);
    laya->source = endpoint->source;
    laya->url_count = endpoint->url_count;
    for (int i = 0; i < endpoint->url_count; i++) snprintf(laya->urls[i], sizeof laya->urls[i], "%s", endpoint->urls[i]);
    if (endpoint->model) snprintf(laya->model, sizeof laya->model, "%s", endpoint->model);
    laya->base.decide = laya_decide;
    laya->clock_ms = monotonic_ms;
    laya->transport = transport;
}

void laya_decider_reset(LayaDecider *laya)
{
    laya->offline_until_tick = 0;
    laya->offline_reason = FALLBACK_NONE;
    memset(laya->briefing, 0, sizeof laya->briefing);
}

void laya_decider_close(LayaDecider *laya)
{
    laya->transport->close(laya->transport);
    for (int c = 0; c < MAX_COMBATANTS; c++) free(laya->reply[c].text), free(laya->body[c]);
    memset(laya, 0, sizeof *laya);
}
