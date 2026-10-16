#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cJSON.h"
#include "check.h"
#include "../src/laya/laya.h"

#define ERR_SIZE 256
#define MAX_SENT 256
#define TICK_MS 50

typedef struct {
    LayaTransport base;
    const char *fixed_text;
    Fallback failure;
    const char *only_reachable_url;
    double stance_confidence;
    bool truncated;
    bool hold_replies;
    int sent;
    int polled;
    int sent_id[MAX_SENT];
    char sent_url[MAX_SENT][LAYA_URL_LEN];
    char *sent_body[MAX_SENT];
} FakeTransport;

static GameData data;
static Battle battle, again;
static FakeTransport fake;
static LayaDecider laya;
static int fake_now_ms;

static int fake_clock(void)
{
    return fake_now_ms;
}

static char *reply_choosing_first_options(const char *body, double stance_confidence, bool truncated)
{
    cJSON *request = cJSON_Parse(body);
    cJSON *reply = cJSON_CreateObject();
    cJSON *answers = cJSON_AddObjectToObject(reply, "answers");
    const cJSON *question;
    cJSON_ArrayForEach(question, cJSON_GetObjectItemCaseSensitive(request, "questions"))
    {
        const char *first = cJSON_GetObjectItemCaseSensitive(question, "criteria")->child->string;
        cJSON *answer = cJSON_AddObjectToObject(answers, question->string);
        cJSON_AddStringToObject(answer, "choice", first);
        cJSON_AddNumberToObject(cJSON_AddObjectToObject(answer, "probabilities"), first, 0.75);
        cJSON_AddNumberToObject(answer, "x_jev_confidence", strcmp(question->string, "stance") == 0 ? stance_confidence : 0.9);
    }
    cJSON_AddBoolToObject(cJSON_AddObjectToObject(reply, "usage"), "truncated", truncated);
    char *text = cJSON_PrintUnformatted(reply);
    cJSON_Delete(reply);
    cJSON_Delete(request);
    return text;
}

static bool fake_send(LayaTransport *self, int id, const char *url, const char *body)
{
    FakeTransport *transport = (FakeTransport *)self;
    CHECK(transport->sent < MAX_SENT);
    transport->sent_id[transport->sent] = id;
    snprintf(transport->sent_url[transport->sent], LAYA_URL_LEN, "%s", url);
    transport->sent_body[transport->sent++] = strdup(body);
    return true;
}

static bool fake_poll(LayaTransport *self, LayaReply *out)
{
    FakeTransport *transport = (FakeTransport *)self;
    if (transport->hold_replies || transport->polled >= transport->sent) return false;
    int i = transport->polled++;
    bool reachable = !transport->only_reachable_url || strcmp(transport->only_reachable_url, transport->sent_url[i]) == 0;
    *out = (LayaReply){transport->sent_id[i], NULL, reachable ? transport->failure : FALLBACK_UNREACHABLE};
    if (reachable && transport->failure == FALLBACK_NONE)
        out->text = transport->fixed_text ? strdup(transport->fixed_text)
                                          : reply_choosing_first_options(transport->sent_body[i], transport->stance_confidence, transport->truncated);
    return true;
}

static void fake_close(LayaTransport *self)
{
    FakeTransport *transport = (FakeTransport *)self;
    for (int i = 0; i < transport->sent; i++) free(transport->sent_body[i]);
}

static void start(uint32_t seed, int url_count)
{
    char err[ERR_SIZE];
    LOAD_OR_EXIT(load_game_data(&data, "data", err, ERR_SIZE), err);
    data.laya.url_count = url_count;
    battle_setup(&battle, &data, seed);
    memset(&fake, 0, sizeof fake);
    fake.base = (LayaTransport){fake_send, fake_poll, fake_close};
    fake.stance_confidence = 0.5;
    laya_decider_init(&laya, &fake.base);
    laya.clock_ms = fake_clock;
    fake_now_ms = 0;
}

static void finish(void)
{
    laya_decider_close(&laya);
    battle_free(&battle);
}

static void run(int ticks)
{
    for (int t = 0; t < ticks; t++) {
        battle_step(&battle, &data, &laya.base);
        fake_now_ms += TICK_MS;
    }
}

static int decisions_by(DeciderKind decider, Fallback fallback, int first_combatants)
{
    int count = 0;
    for (int e = 0; e < battle.log_count; e++)
        count += battle.log[e].kind == EVENT_DECISION && battle.log[e].actor < first_combatants && battle.log[e].decider == decider && battle.log[e].fallback == fallback;
    return count;
}

static int sends_for(int combatant)
{
    int count = 0;
    for (int i = 0; i < fake.sent; i++) count += fake.sent_id[i] == combatant;
    return count;
}

static int sends_for_first(int combatants)
{
    int count = 0;
    for (int c = 0; c < combatants; c++) count += sends_for(c);
    return count;
}

static const char *url_sent_for(int combatant, int nth)
{
    for (int i = 0; i < fake.sent; i++)
        if (fake.sent_id[i] == combatant && nth-- == 0) return fake.sent_url[i];
    return "";
}

static const char *body_sent_for(int combatant)
{
    for (int i = 0; i < fake.sent; i++)
        if (fake.sent_id[i] == combatant) return fake.sent_body[i];
    return "";
}

static void test_laya_asks_once_then_decides(void)
{
    start(3, 1);
    int count = battle.combatant_count;
    run(1);
    CHECK(fake.sent == count);
    CHECK(decisions_by(DECIDER_REFLEX, FALLBACK_WAITING, count) == count);
    run(1);
    CHECK(sends_for_first(count) == count);
    CHECK(decisions_by(DECIDER_LAYA, FALLBACK_NONE, count) == count);
    for (int c = 0; c < count; c++) {
        CHECK(battle.combatants[c].next_decision_tick > 1);
        CHECK(laya.briefing[c][0] != '\0');
        cJSON *request = cJSON_Parse(body_sent_for(c));
        CHECK(strcmp(cJSON_GetObjectItemCaseSensitive(request, "state")->valuestring, laya.briefing[c]) == 0);
        cJSON_Delete(request);
    }
    finish();
}

static void test_a_pending_request_is_not_asked_again(void)
{
    start(3, 1);
    int count = battle.combatant_count;
    fake.hold_replies = true;
    run(5);
    CHECK(sends_for_first(count) == count);
    CHECK(decisions_by(DECIDER_LAYA, FALLBACK_NONE, count) == 0);
    fake.hold_replies = false;
    run(1);
    CHECK(decisions_by(DECIDER_LAYA, FALLBACK_NONE, count) == count);
    finish();
}

static int events_of(int combatant, DeciderKind decider, Fallback fallback)
{
    int count = 0;
    for (int e = 0; e < battle.log_count; e++)
        count += battle.log[e].kind == EVENT_DECISION && battle.log[e].actor == combatant && battle.log[e].decider == decider &&
                 battle.log[e].fallback == fallback;
    return count;
}

static int nth_laya_answer_ms(int combatant, int nth)
{
    for (int e = 0; e < battle.log_count; e++)
        if (battle.log[e].kind == EVENT_DECISION && battle.log[e].actor == combatant && battle.log[e].decider == DECIDER_LAYA && nth-- == 0)
            return battle.log[e].answer_ms;
    return -1;
}

static int laya_answer_ms(int combatant)
{
    return nth_laya_answer_ms(combatant, 0);
}

static void test_reflex_keeps_deciding_while_a_request_is_pending(void)
{
    start(3, 1);
    int slowest = 0;
    for (int c = 0; c < battle.combatant_count; c++)
        if (combatant_initiative(&battle.combatants[c], &data) < combatant_initiative(&battle.combatants[slowest], &data)) slowest = c;
    int interval = DECISION_INTERVAL_K / combatant_initiative(&battle.combatants[slowest], &data);
    CHECK(interval > 1);
    fake.hold_replies = true;
    run(2 * interval);
    CHECK(events_of(slowest, DECIDER_REFLEX, FALLBACK_WAITING) == 2);
    CHECK(events_of(slowest, DECIDER_LAYA, FALLBACK_NONE) == 0);
    fake.hold_replies = false;
    run(1);
    CHECK(events_of(slowest, DECIDER_REFLEX, FALLBACK_WAITING) == 2);
    CHECK(events_of(slowest, DECIDER_LAYA, FALLBACK_NONE) == 1);
    CHECK(laya_answer_ms(slowest) == 2 * interval * TICK_MS);
    run(2 * interval);
    CHECK(nth_laya_answer_ms(slowest, 1) == TICK_MS);
    finish();
}

static void test_timeout_falls_back_to_the_reflex(void)
{
    start(3, 1);
    int count = battle.combatant_count;
    fake.failure = FALLBACK_TIMEOUT;
    run(2);
    CHECK(decisions_by(DECIDER_REFLEX, FALLBACK_TIMEOUT, count) == count);
    for (int e = 0; e < battle.log_count; e++)
        CHECK(battle.log[e].kind != EVENT_DECISION || battle.log[e].actor >= count || battle.log[e].fallback != FALLBACK_TIMEOUT || battle.log[e].answer_ms == TICK_MS);
    finish();
}

static void test_unreachable_backs_off_then_asks_again(void)
{
    start(3, 1);
    fake.failure = FALLBACK_UNREACHABLE;
    int count = battle.combatant_count;
    run(2);
    CHECK(decisions_by(DECIDER_REFLEX, FALLBACK_UNREACHABLE, count) == count);
    int logged = battle.log_count;
    int sent_before = fake.sent;
    run(LAYA_OFFLINE_TICKS - 2);
    CHECK(fake.sent == sent_before);
    int backed_off = 0;
    for (int e = logged; e < battle.log_count; e++) {
        if (battle.log[e].kind != EVENT_DECISION) continue;
        backed_off++;
        CHECK(battle.log[e].fallback == FALLBACK_UNREACHABLE && battle.log[e].answer_ms == 0);
    }
    CHECK(backed_off > 0);
    run(LAYA_OFFLINE_TICKS);
    CHECK(fake.sent > sent_before);
    finish();
}

static void test_next_url_answers_when_the_first_is_unreachable(void)
{
    start(3, 2);
    int count = battle.combatant_count;
    fake.only_reachable_url = data.laya.urls[1];
    run(3);
    CHECK(sends_for_first(count) == 2 * count);
    for (int c = 0; c < count; c++) {
        CHECK(strcmp(url_sent_for(c, 0), data.laya.urls[0]) == 0);
        CHECK(strcmp(url_sent_for(c, 1), data.laya.urls[1]) == 0);
    }
    CHECK(decisions_by(DECIDER_LAYA, FALLBACK_NONE, count) == count);
    CHECK(decisions_by(DECIDER_REFLEX, FALLBACK_UNREACHABLE, count) == 0);
    for (int c = 0; c < count; c++) CHECK(laya_answer_ms(c) == TICK_MS);
    finish();
}

static void test_only_unreachable_moves_on_to_the_next_url(void)
{
    start(3, 2);
    int count = battle.combatant_count;
    run(3);
    CHECK(sends_for_first(count) == count);
    finish();
    start(3, 2);
    fake.failure = FALLBACK_TIMEOUT;
    run(3);
    CHECK(sends_for_first(count) == count);
    CHECK(decisions_by(DECIDER_REFLEX, FALLBACK_TIMEOUT, count) == count);
    finish();
}

static void test_every_url_unreachable_backs_off(void)
{
    start(3, 2);
    int count = battle.combatant_count;
    fake.only_reachable_url = "http://nowhere";
    run(3);
    CHECK(sends_for_first(count) == 2 * count);
    CHECK(decisions_by(DECIDER_REFLEX, FALLBACK_UNREACHABLE, count) == count);
    CHECK(decisions_by(DECIDER_LAYA, FALLBACK_NONE, count) == 0);
    int sent_before = fake.sent;
    run(LAYA_OFFLINE_TICKS - 3);
    CHECK(fake.sent == sent_before);
    finish();
}

static void test_malformed_and_low_confidence_replies_fall_back(void)
{
    start(3, 1);
    int count = battle.combatant_count;
    fake.fixed_text = "nope";
    run(2);
    CHECK(decisions_by(DECIDER_REFLEX, FALLBACK_MALFORMED, count) == count);
    finish();
    start(3, 1);
    count = battle.combatant_count;
    fake.stance_confidence = 0.1;
    run(2);
    CHECK(decisions_by(DECIDER_REFLEX, FALLBACK_LOW_CONFIDENCE, count) == count);
    CHECK(decisions_by(DECIDER_LAYA, FALLBACK_NONE, count) == 0);
    finish();
}

static void test_truncated_replies_are_counted(void)
{
    start(3, 1);
    int count = battle.combatant_count;
    fake.truncated = true;
    run(2);
    CHECK(laya.truncated_count == count);
    finish();
}

static void test_a_laya_battle_replays_from_its_log(void)
{
    start(3, 1);
    run(200);
    char *original = battle_save_json(&battle, &data);
    CHECK(battle_decision_stats(&battle).laya > 0);
    memset(&again, 0, sizeof again);
    battle_setup(&again, &data, 3);
    ReplayDecider replay;
    replay_decider_init(&replay, battle.log, battle.log_count);
    for (int t = 0; t < 200; t++) battle_step(&again, &data, &replay.base);
    char *replayed = battle_save_json(&again, &data);
    CHECK(strcmp(original, replayed) == 0);
    free(original);
    free(replayed);
    battle_free(&again);
    finish();
}

static void test_transfer_outcomes_are_classified(void)
{
    CHECK(laya_classify_transfer(&(TransferOutcome){.curl_ok = true, .connected = true, .http_status = 200}) == FALLBACK_NONE);
    CHECK(laya_classify_transfer(&(TransferOutcome){.curl_ok = true, .connected = true, .http_status = 500}) == FALLBACK_MALFORMED);
    CHECK(laya_classify_transfer(&(TransferOutcome){.timed_out = true, .connected = true}) == FALLBACK_TIMEOUT);
    CHECK(laya_classify_transfer(&(TransferOutcome){.timed_out = true, .connected = false}) == FALLBACK_UNREACHABLE);
    CHECK(laya_classify_transfer(&(TransferOutcome){.connected = false}) == FALLBACK_UNREACHABLE);
    CHECK(laya_classify_transfer(&(TransferOutcome){.connected = true}) == FALLBACK_UNREACHABLE);
    CHECK(laya_classify_transfer(&(TransferOutcome){.too_large = true, .connected = true}) == FALLBACK_MALFORMED);
}

static void test_timeout_backs_off_too(void)
{
    start(3, 1);
    int count = battle.combatant_count;
    fake.failure = FALLBACK_TIMEOUT;
    run(2);
    int sent_before = fake.sent;
    int logged = battle.log_count;
    run(LAYA_OFFLINE_TICKS - 2);
    CHECK(fake.sent == sent_before);
    int backed_off = 0;
    for (int e = logged; e < battle.log_count; e++) {
        if (battle.log[e].kind != EVENT_DECISION) continue;
        backed_off++;
        CHECK(battle.log[e].fallback == FALLBACK_TIMEOUT && battle.log[e].decider == DECIDER_REFLEX);
    }
    CHECK(backed_off > 0);
    run(LAYA_OFFLINE_TICKS);
    CHECK(fake.sent > sent_before);
    CHECK(decisions_by(DECIDER_REFLEX, FALLBACK_TIMEOUT, count) >= count);
    finish();
}

static void test_reset_forgets_the_back_off_and_briefings(void)
{
    start(3, 1);
    fake.failure = FALLBACK_UNREACHABLE;
    run(2);
    int sent_before = fake.sent;
    CHECK(laya.briefing[0][0] != '\0' && laya.offline_until_tick > 0);
    laya_decider_reset(&laya);
    CHECK(laya.briefing[0][0] == '\0' && laya.offline_until_tick == 0);
    battle_setup(&battle, &data, 3);
    run(2);
    CHECK(fake.sent > sent_before);
    finish();
}

void run_laya_tests(void)
{
    test_reset_forgets_the_back_off_and_briefings();
    test_laya_asks_once_then_decides();
    test_transfer_outcomes_are_classified();
    test_timeout_backs_off_too();
    test_a_pending_request_is_not_asked_again();
    test_reflex_keeps_deciding_while_a_request_is_pending();
    test_timeout_falls_back_to_the_reflex();
    test_unreachable_backs_off_then_asks_again();
    test_next_url_answers_when_the_first_is_unreachable();
    test_only_unreachable_moves_on_to_the_next_url();
    test_every_url_unreachable_backs_off();
    test_malformed_and_low_confidence_replies_fall_back();
    test_truncated_replies_are_counted();
    test_a_laya_battle_replays_from_its_log();
}
