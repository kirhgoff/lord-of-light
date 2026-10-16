#include "laya.h"

#include <stdlib.h>
#include <string.h>

static bool resend_to_next_url(LayaDecider *laya, const GameData *data, int combatant, Fallback failure)
{
    int next = laya->url_index[combatant] + 1;
    if (failure != FALLBACK_UNREACHABLE || next >= data->laya.url_count) return false;
    if (!laya->transport->send(laya->transport, combatant, data->laya.urls[next], laya->body[combatant])) return false;
    laya->url_index[combatant] = next;
    return true;
}

static void drain_replies(LayaDecider *laya, const GameData *data)
{
    LayaReply reply;
    while (laya->transport->poll(laya->transport, &reply)) {
        int c = reply.id;
        if (c < 0 || c >= MAX_COMBATANTS || resend_to_next_url(laya, data, c, reply.failure)) {
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
    Event event = laya_decision(battle, data, c, &laya->asked[c], reply->text, reply->failure, reply->latency_ms, &truncated);
    laya->truncated_count += truncated;
    if (reply->failure == FALLBACK_UNREACHABLE) laya->offline_until_tick = battle->tick + LAYA_OFFLINE_TICKS;
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
    laya->body[c] = laya_request_json(battle, data, c, laya->briefing[c], &laya->asked[c]);
    laya->url_index[c] = 0;
    laya->in_flight[c] = laya->transport->send(laya->transport, c, data->laya.urls[0], laya->body[c]);
}

static bool laya_decide(Decider *self, const Battle *battle, const GameData *data, int combatant, Event *out)
{
    LayaDecider *laya = (LayaDecider *)self;
    drain_replies(laya, data);
    if (laya->has_reply[combatant]) {
        *out = take_reply(laya, battle, data, combatant);
        return true;
    }
    if (laya->in_flight[combatant]) return false;
    if (battle->tick < laya->offline_until_tick) {
        reflex_decide(NULL, battle, data, combatant, out);
        out->fallback = FALLBACK_UNREACHABLE;
        return true;
    }
    ask_laya(laya, battle, data, combatant);
    return false;
}

void laya_decider_init(LayaDecider *laya, LayaTransport *transport)
{
    memset(laya, 0, sizeof *laya);
    laya->base.decide = laya_decide;
    laya->transport = transport;
}

void laya_decider_close(LayaDecider *laya)
{
    laya->transport->close(laya->transport);
    for (int c = 0; c < MAX_COMBATANTS; c++) free(laya->reply[c].text), free(laya->body[c]);
    memset(laya, 0, sizeof *laya);
}
