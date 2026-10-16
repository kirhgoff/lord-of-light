# Plan: ticket 11 — Laya Decider

Repo: /Users/kirill.lastovirya/Projects/lord-of-light (main @ df95aa6, ticket claimed). Branch `11-laya-decider`, worktree `~/Projects/lord-of-light-11-laya-decider`.
Spec: `docs/plan/issues/11-laya-decider.md` (+ its Comment), ADR 0001/0002/0003, `docs/research/laya.md` (every recommendation), `GLOSSARY.md`. Rules: `docs/plan/executor-brief.md` (no comments, small signed commits with explicit pathspecs, `make test` + `make asan` green, trailer `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`).

Facts checked on this machine: libcurl 8.7.1 is in the macOS SDK (`curl-config --libs` → `-lcurl`, headers under `$(xcrun --show-sdk-path)/usr/include/curl`), so nothing is vendored. pthreads need no extra flag on macOS. `tests/fixtures/battle_elements.json` has an empty `"log": []`, so no fixture edit for the new Event fields. Ticket 10 is rewriting `src/main_game.c` in parallel (today it has no Battle loop).

No test touches the network. The LAN server (`http://192.168.0.3:8000`) is only used by hand via `build/headless --decider laya`.

## 0. What exists (reuse, do not rebuild)

- `src/sim/decide.{h,c}`: `Decider {decide fn; DeciderKind kind}`; `decide` returning `false` = "not yet" (Combatant stays due, polled next Tick; Reflex fills in once while `stance == STANCE_NONE`). `battle_decide` runs first in `battle_step`, in Initiative order. `reflex_decide(NULL, …)` is callable directly. Statics worth exporting: `hurt_level_of`, `is_enemy_of`, `is_ally_of`, `nearest_enemy`.
- `src/sim/vision.{h,c}`: `perceive()` → `Perceived {kind, index, band, def, side, health_quarters, name, x, y}`. Far: only enemies (`side`/`health_quarters` = `UNKNOWN`, `name` NULL) and healing Pickups (`def` UNKNOWN). Traps only at Close. Close/Medium: name, side, health quarters. Resistances are never in `Perceived` — at Close the Combatant "sees everything", so the Briefing may state them at Close only (the ticket Comment).
- `Event {tick, kind, actor, actor_part, target, target_part, element, amount, detail, decider}`; Decision Events use `element` = appraised Element, `amount` = Stance, `detail` = HurtLevel. Saved/loaded in `battle_json.c` (`save_json` log loop, `load_event`, strict `read_*` helpers, `read_id` with `*_IDS` tables).
- `src/sim/data.c`: `load_file(data, path, err, err_size, root_loader)` + `get_string/get_int/get_object` + `fail(ctx, path, fmt…)`; `power_from_root` is the model for a tiny settings file; `load_game_data` chains loaders with `LOAD(...)`. Range errors read `"<key>: must be between A and B"`.
- `combatant_resistance_percent(c, data, element, internal_only)` (`combat.h`), `combatant_health_quarters` (`vision.h`), `body_working_parts`, `element_id()` (lower-case ids), `Part.name`, `ArenaObjectDef.name/heal`.
- Tests: `tests/check.h` (`CHECK`, `LOAD_OR_EXIT`), `tests/test_decide.c` helpers (`load_data`, `fresh`, `add_deity(pantheon, deity, side, x_px, y_px)`, `run`, `decision_events`), `tests/test_main.c` registers `run_*_tests`.
- Fixture Deities (`data/pantheons/*.json`): Greek **Zeus** = sword_hand (strike Kinetic 24), legs, torso, keen_head (vision 128/256/448, initiative 2), thunderbolt_hand (cast Lightning 170), large_heart (60), lightning_gland. Greek **Apollo** = healing_hand (support), legs, torso, radiant_crown, sun_bow (cast Radiance), large_heart, radiance_gland. Celtic **Dagda** = healing_hand, legs, torso, head, war_club (strike Kinetic), large_heart, armour_plate (Kinetic 40). Pickup `soma` heals 30. Monster names are Body Plan ids (`biped`, `minion_imp`, …).
- Makefile: `SIM_OBJECTS` = `src/sim/*.c`, `VIEW_OBJECTS`, `TEST_OBJECTS`; headless links sim + cJSON only; `make asan` runs tests + headless seeds.

## 1. Settled design

### 1.1 Where things live

| File | Links | Holds |
|---|---|---|
| `src/sim/battle.h`, `battle_json.c` | all | `Fallback` enum, 4 new `Event` fields, save/load |
| `src/sim/decide.{h,c}` | all | Decider interface now fills an `Event`; `ReplayDecider`; `DecisionStats`; exported helpers |
| `src/sim/briefing.{h,c}` (new) | all | Briefing text, request JSON, reply parsing → complete Decision `Event` (pure C + cJSON, like `battle_json.c`; no threads, no sockets) |
| `src/sim/data.{h,c}` + `data/laya.json` (new) | all | `LayaSettings {url, timeout_ms, min_stance_confidence_permille}` |
| `src/laya/laya.{h,c}` (new) | tests, headless, game | `LayaTransport` seam + `LayaDecider` (in-flight bookkeeping, poll/send, offline backoff, last Briefing per Combatant) |
| `src/laya/laya_curl.c` (new) | headless, game | the real transport: worker thread + libcurl |
| `src/decider_setup.{h,c}` (new) | headless, game | `--decider laya|reflex`, `--replay <dump.json>`; builds the Decider; `read_text_file` moves here |
| `tests/test_briefing.c`, `tests/test_laya.c` (new) | tests | canned-string tests; `FakeTransport` |

`src/sim/` stays free of raylib, libcurl and pthreads. The only seam is `LayaTransport` (real: libcurl worker thread; fake: in-memory). Everything else is plain functions.

Makefile:
```make
LAYA_OBJECTS := $(patsubst %.c,$(BUILD)/obj/%.o,$(wildcard src/laya/*.c))
SETUP_OBJECT := $(BUILD)/obj/src/decider_setup.o
GAME_OBJECTS := $(BUILD)/obj/src/main_game.o $(VIEW_OBJECTS) $(SIM_OBJECTS) $(LAYA_OBJECTS) $(SETUP_OBJECT) $(CJSON_OBJECT)
HEADLESS_OBJECTS := $(BUILD)/obj/src/main_headless.o $(SIM_OBJECTS) $(LAYA_OBJECTS) $(SETUP_OBJECT) $(CJSON_OBJECT)
$(BUILD)/game: ... $(CC) $^ $(FRAMEWORKS) -lcurl -o $@
$(BUILD)/headless: ... $(CC) $^ $(SAN) -lcurl -o $@
$(BUILD)/tests: $(TEST_OBJECTS) $(SIM_OBJECTS) $(BUILD)/obj/src/laya/laya.o $(CJSON_OBJECT)   # no curl, no laya_curl.o
```
`-Isrc` is not needed: includes use relative paths like the existing code (`#include "sim/decide.h"` from `src/laya/laya.h` → use `"../sim/decide.h"`).

### 1.2 Sim changes: Event provenance, Decider fills an Event

`battle.h`:
```c
typedef enum { FALLBACK_NONE, FALLBACK_WAITING, FALLBACK_UNREACHABLE, FALLBACK_TIMEOUT, FALLBACK_MALFORMED,
               FALLBACK_LOW_CONFIDENCE, FALLBACK_COUNT } Fallback;

typedef struct {
    /* existing 10 fields unchanged, then: */
    int confidence_permille;            /* Stance x_jev_confidence * 1000, 0..1000; 0 for Reflex */
    int latency_ms;                     /* send→reply wall time measured by the transport; 0 for Reflex */
    Fallback fallback;                  /* why the Reflex decided although Laya was asked */
    int stance_permille[STANCE_COUNT];  /* Laya's Stance probabilities * 1000 by Stance; index STANCE_NONE unused */
} Event;
```
New fields go last so every existing `(Event){0, EVENT_HIT, …}` initialiser still compiles (trailing zeros = none).

`battle_json.c`: `FALLBACK_IDS = {"none","waiting","unreachable","timeout","malformed","low_confidence"}`. In the save loop, **only when `ev->kind == EVENT_DECISION`**, add `"confidence_permille"`, `"latency_ms"`, `"fallback"` (string id), `"stance_permille"` (`cJSON_CreateIntArray(ev->stance_permille, STANCE_COUNT)`). In `load_event`, when kind is decision these four are required: `confidence_permille` 0..1000, `latency_ms` 0..EVENT_AMOUNT_LIMIT, `fallback` via `read_id`, `stance_permille` an array of exactly `STANCE_COUNT` ints each 0..1000 (else `fail(ctx, "stance_permille: array of 5 permille")`). Hit events keep their current shape and size.

`decide.h`:
```c
struct Decider {
    bool (*decide)(Decider *self, const Battle *battle, const GameData *data, int combatant, Event *out);
};
/* out: a complete Decision Event except tick (set by the log) — kind, actor, parts -1, element, amount=Stance, detail=HurtLevel, decider, provenance. */

Event decision_event(int combatant, Stance stance, HurtLevel hurt, int element, DeciderKind decider);
    /* (Event){0, EVENT_DECISION, combatant, -1, -1, -1, element, stance, hurt, decider} */
Decision event_decision(const Event *event);   /* {amount, detail, element} */

Decider reflex_decider(void);                  /* (Decider){reflex_decide} */
bool reflex_decide(Decider *self, const Battle *b, const GameData *d, int c, Event *out);   /* always true, decider REFLEX */

HurtLevel hurt_level_from_quarters(int quarters);         /* 4→UNHURT, 3→WOUNDED, 2→BADLY, else NEAR_DEATH */
bool perceived_is_enemy(const Combatant *me, const Perceived *seen);
bool perceived_is_ally(const Combatant *me, const Perceived *seen);
const Perceived *perception_nearest_enemy(const Combatant *me, const Perception *perception);

typedef struct { Decider base; const Event *log; int count; int next[MAX_COMBATANTS]; } ReplayDecider;
void replay_decider_init(ReplayDecider *replay, const Event *log, int count);

typedef struct { int laya; int reflex; int fallback[FALLBACK_COUNT]; int mean_confidence_permille; int mean_latency_ms; } DecisionStats;
DecisionStats battle_decision_stats(const Battle *battle);
```
`DeciderKind kind` leaves `struct Decider` (the Event carries it; the replay Decider has no single kind). Pattern for Decider implementations with state: `Decider base` as the **first** member, `decide(Decider *self…)` casts `(ReplayDecider *)self`.

`decide.c` changes:
- `hurt_level_of(c)` becomes `hurt_level_from_quarters(combatant_health_quarters(c))`; `is_enemy_of`/`is_ally_of`/`nearest_enemy` renamed to the exported names (all call sites in `decide.c` updated).
- `reflex_decide`: `*out = decision_event(combatant, stance, hurt, element, DECIDER_REFLEX)`.
- `store_decision(Battle *b, Event event)`: `b->combatants[event.actor].decision = event_decision(&event); battle_log_event(b, event);`
- `ask()`:
```c
Event event;
if (decider->decide(decider, battle, data, combatant, &event)) {
    event.actor = combatant;
    store_decision(battle, event);
    /* schedule next_decision_tick exactly as today */
} else if (who->decision.stance == STANCE_NONE) {
    reflex_decide(NULL, battle, data, combatant, &event);
    event.fallback = FALLBACK_WAITING;
    store_decision(battle, event);
}
```
`FALLBACK_WAITING` marks the stop-gap Reflex Decision logged while a Decider has not answered yet; replay needs it (1.8).
- `replay_decide(self, battle, data, c, out)`:
```
r = (ReplayDecider *)self
for (i = r->next[c]; i < r->count; i++) {
    if (r->log[i].kind != EVENT_DECISION || r->log[i].actor != c) continue;
    if (r->log[i].tick != battle->tick) return false;          /* not yet (or diverged: stays false forever) */
    r->next[c] = i + 1;
    if (r->log[i].fallback == FALLBACK_WAITING) return false;  /* the sim regenerates this one itself */
    *out = r->log[i];
    return true;
}
return false;
```
- `battle_decision_stats`: one pass over the log: `laya` = Decision Events with `decider == DECIDER_LAYA`, `reflex` = the rest, `fallback[ev.fallback]++`, mean confidence over the Laya ones, mean latency over events with `latency_ms > 0` (integer division, 0 when empty).

Test updates in `tests/test_decide.c`: `decision(c)` helper → `Event e; CHECK(reflex_decide(&reflex, …, &e)); return event_decision(&e);`; `pending_decide`/`retreat_decide` take `Event *out` (`*out = decision_event(c, STANCE_RETREAT, HURT_UNHURT, -1, DECIDER_LAYA)`); `Decider pending = {pending_decide}`, `Decider answers = {retreat_decide}`. All assertions there stay valid (the stop-gap events are still `DECIDER_REFLEX`).

### 1.3 Briefing (`src/sim/briefing.h`, `briefing_write`)

```c
#define BRIEFING_MAX 1200
int briefing_write(const Battle *battle, const GameData *data, int combatant, char *out, size_t size);  /* returns strlen(out); size >= BRIEFING_MAX + 1 */
```
Rules (laya.md recs 1–5 + the ticket Comment):
1. ASCII English prose. One fact per sentence, each ending in `.`, joined by a single space. No digits anywhere, no newlines, no second person, no `key: value` records.
2. Third person with the Combatant's name. `subject(name)`: if `name[0]` is lower-case (monsters: Body Plan ids) → `"the " + name` with `_` → space (`"the minion imp"`); else the name as is. At a sentence start capitalise the first letter (`"The biped, an enemy, …"`).
3. Only what the Combatant perceives: numbers become words — Vitality → `HURT_WORDS[HurtLevel] = {"unhurt","wounded","badly wounded","near death"}`; distance → `BAND_WORDS[Band] = {"close","near","far"}`; position → compass: `|dx| >= |dy| ? (dx > 0 ? "east" : "west") : (dy > 0 ? "south" : "north")` (y grows south). Far: no name, no health, no side beyond "enemy". Medium: name + health, **no resistances**. Close: everything, including the focus's resistances. Allies are never Far (vision drops them).
4. Order = importance: self, nearest enemy (the focus) and its resistances, other enemies nearest first, allies most hurt first (tie: nearer first), Pickups nearest first, Traps nearest first. Append a sentence only if `len + (len ? 1 : 0) + strlen(sentence) <= BRIEFING_MAX`; the first sentence that does not fit ends the Briefing (nothing after it is added).

Sentences, in order (printf shapes; `%S` = subject with capital at sentence start):
- `"%S is %s."` self, `HURT_WORDS[hurt_level_from_quarters(combatant_health_quarters(me))]`.
- `"%S can %s."` where the list is the working attack abilities in slot/ability order, deduplicated by (order kind, Element): strike → `"strike with <Element>"`, cast → `"cast <Element>"`, joined `A` / `A and B` / `A, B and C`. With no working attack: `"%S cannot attack."`. `ELEMENT_WORDS[ELEMENT_COUNT] = {"Kinetic","Fire","Frost","Lightning","Radiance","Venom","Mind","Void"}` (used for options too).
- `"%S can heal an ally."` if the self has a working support ability.
- Enemies (perceived, `perceived_is_enemy`, sorted by squared distance): Far → first one `"An enemy is far to the %s."`, later ones `"Another enemy is far to the %s."`; Close/Medium → `"%S, an enemy, is %s to the %s and %s."` (subject, band word, direction, hurt word from `health_quarters`).
- Right after the focus (the first enemy), only if the focus is Close and the self has at least 2 distinct attack Elements: `high` = the self's attack Element with the highest `combatant_resistance_percent(focus, data, e, e == ELEMENT_RADIANCE)`, `low` = the lowest (ties: first in slot order). If `high% > low%`: `"%S resists %s and is weak to %s."` when `high% > 0`, else `"%S is weak to %s."`. If `high% == low%`: no sentence (no evidence → Laya's near-uniform answer is honest).
- Allies (`perceived_is_ally`, sorted by `health_quarters` asc then distance): `"%S, an ally, is %s to the %s and %s."` then, if that ally has a working support ability, `"%S can heal."`.
- Pickups (sorted by distance): Far or `data->pickups[def].heal > 0` → `"A healing pickup is %s to the %s."`; otherwise `"A %s pickup is %s to the %s."` with `ArenaObjectDef.name` lower-cased (`"ember seed"`).
- Traps (Close only): `"A %s trap is close to the %s."` with the name lower-cased.

Worked example (test fixture, `tests/test_briefing.c`): fresh Battle, no obstacles; Zeus (Greek, PANTHEON) at (500, 200) px; Dagda (Celtic, as HORDE, the trick `test_decide.c` already uses) at (580, 200) → 80 px east = Close (≤128); Apollo (Greek, PANTHEON) at (500, 400) with `vitality = 40` of 60 → quarters 3 = wounded, 200 px south = Medium (≤256); soma Pickup at (100, 200) → 400 px west = Far (≤448); `pickup_count = 1`. Zeus's attack Elements: Kinetic (sword_hand), Lightning (thunderbolt_hand); Dagda's armour_plate gives Kinetic 40, Lightning 0. Expected, byte for byte:

```
Zeus is unhurt. Zeus can strike with Kinetic and cast Lightning. Dagda, an enemy, is close to the east and unhurt. Dagda resists Kinetic and is weak to Lightning. Apollo, an ally, is near to the south and wounded. Apollo can heal. A healing pickup is far to the west.
```
(Executor: confirm `armour_plate` in Dagda's body in `data/pantheons/celtic.json` before writing the string; it is there per plan-06.) Move Dagda to (900, 200) (400 px → Far): the enemy sentences become `"An enemy is far to the east."` with no name, no hurt word and no resistances sentence; at (700, 200) (Medium): `"Dagda, an enemy, is near to the east and unhurt."` and no resistances sentence.

### 1.4 Request JSON (`laya_request_json`)

```c
typedef struct {
    bool stance_offered[STANCE_COUNT];
    int elements[ELEMENT_COUNT];   /* Elements offered in the element question, in option order */
    int element_count;             /* 0 → no element question; 1 never happens (see below) */
} LayaQuestions;
char *laya_request_json(const Battle *battle, const GameData *data, int combatant, const char *briefing, LayaQuestions *asked);  /* malloc'd (cJSON_PrintUnformatted) */
```
Body, exactly per laya.md (state is a **string**; keys are the wording; descriptions empty; rule in `instructions`):
```json
{"state":"<Briefing>",
 "questions":{
  "hurt":{"type":"choice","instructions":"How hurt is Zeus?",
          "criteria":{"unhurt":"","wounded":"","badly wounded":"","near death":""}},
  "element":{"type":"choice","instructions":"Which Element hurts Dagda most?",
             "criteria":{"Kinetic":"","Lightning":""}},
  "stance":{"type":"choice",
            "instructions":"Zeus fights unless near death, then retreats to a healer. Zeus collects a healing pickup when wounded and the pickup is nearer than any enemy. Zeus supports an ally who is near death when Zeus can heal. Choose what Zeus does now.",
            "criteria":{"fight the nearest enemy":"","retreat from the enemy":"","collect the pickup":""}}}}
```
- Question keys are fixed: `"hurt"`, `"element"`, `"stance"`. Key order inside `criteria` is fixed (as listed; Elements in slot order).
- `hurt`: always asked. Name = `subject(me)` (capitalised).
- `element`: asked only when a focus exists **and** the self has ≥ 2 distinct working attack Elements (a 1-option choice is degenerate: x_jev divides by zero). Options = `ELEMENT_WORDS` of those Elements. The question names the focus: `subject(focus name)` at Close/Medium, `"the enemy"` at Far. With one attack Element and a focus, `asked->element_count = 0` and the Decision's `vulnerable_element` is that single Element; without a focus it is -1.
- `stance`: `STANCE_KEYS[STANCE_COUNT] = {NULL, "fight the nearest enemy", "retreat from the enemy", "support the hurt ally", "collect the pickup"}`. Offered: FIGHT and RETREAT always; SUPPORT iff the self has a working support ability and some perceived ally has `health_quarters < 4`; COLLECT iff any Pickup is perceived. The instructions string is the fixed Reflex rule with the name substituted (as above). 2–4 options, never fewer than 2 (rec 6).

### 1.5 Reply parsing and the Decision (`laya_decision`)

```c
Event laya_decision(const Battle *battle, const GameData *data, int combatant, const LayaQuestions *asked,
                    const char *reply_text, Fallback failure, int latency_ms, bool *truncated);
```
Always returns a complete Decision Event (`actor = combatant`, `latency_ms` copied). Steps:
1. `failure != FALLBACK_NONE` (transport reported unreachable/timeout/bad HTTP status, `reply_text` NULL) → `reflex_decide` Event with `fallback = failure`, `latency_ms` set. Return.
2. `cJSON_Parse(reply_text)`; need object `answers`; for every question in `asked` (hurt always; element iff `element_count > 0`; stance always) need an object with a string `choice` that **exactly equals one offered key**. Any miss → Reflex Event with `FALLBACK_MALFORMED`, `latency_ms`. (Appraisals and Stance are one answer: half-parsable replies are treated as malformed; deterministic and simple.)
3. `*truncated = usage.truncated == true` (optional field; default false). The caller counts it.
4. From the stance answer: `x = x_jev_confidence` if a number else 0; `confidence_permille = (int)(x * 1000 + 0.5)` clamped 0..1000. `probabilities` (optional object): for each offered Stance key present as a number p, `stance_permille[stance] = (int)(p * 1000 + 0.5)` clamped 0..1000.
5. Appraisals: `hurt` → index in `HURT_WORDS`; `element` → `asked->elements[i]` for the matching `ELEMENT_WORDS[i]` (or the single/absent Element rule from 1.4).
6. If `confidence_permille < data->laya.min_stance_confidence_permille`: Stance = the Reflex's (`reflex_decide` → `event.amount`), `decider = DECIDER_REFLEX`, `fallback = FALLBACK_LOW_CONFIDENCE`; Appraisals, `confidence_permille`, `stance_permille`, `latency_ms` stay Laya's (so the log shows what Laya thought). Else `decider = DECIDER_LAYA`, `fallback = FALLBACK_NONE`.

Canned reply for the worked example (shape from laya.md call 12, our keys):
```json
{"model":"laya-rl-agent","answers":{
 "hurt":{"type":"choice","choice":"unhurt","probabilities":{"unhurt":0.91,"wounded":0.05,"badly wounded":0.02,"near death":0.02},"confidence":0.7,"answer_confidence":0.91,"x_jev_confidence":0.88,"action":{"act_probability":1.0}},
 "element":{"type":"choice","choice":"Lightning","probabilities":{"Kinetic":0.07,"Lightning":0.93},"x_jev_confidence":0.86},
 "stance":{"type":"choice","choice":"fight the nearest enemy","probabilities":{"fight the nearest enemy":0.61,"retreat from the enemy":0.2,"collect the pickup":0.19},"x_jev_confidence":0.415}},
 "usage":{"input_tokens":400,"output_tokens":0,"state_tokens":90,"state_tokens_dropped":0,"truncated":false,"truncated_questions":[]}}
```
→ `decider LAYA, amount STANCE_FIGHT, detail HURT_UNHURT, element ELEMENT_LIGHTNING, confidence_permille 415, stance_permille {0, 610, 200, 0, 190}, fallback NONE`. Same reply with `"x_jev_confidence":0.1` on stance → `decider REFLEX, fallback LOW_CONFIDENCE, amount = Reflex's (FIGHT here), confidence_permille 100`, probabilities kept.

### 1.6 Transport seam and the libcurl worker (`src/laya/laya.h`, `laya_curl.c`)

```c
typedef struct { int id; char *text; int latency_ms; Fallback failure; } LayaReply;   /* text NULL ⇔ failure != NONE; caller frees text */
typedef struct LayaTransport LayaTransport;
struct LayaTransport {
    bool (*send)(LayaTransport *self, int id, char *body);   /* takes ownership of body (malloc'd) in every case; false = queue full, try next Tick */
    bool (*poll)(LayaTransport *self, LayaReply *out);       /* oldest finished reply; false = none */
    void (*close)(LayaTransport *self);                      /* stops the worker, frees everything incl. self */
};
LayaTransport *laya_curl_transport(const char *url, int timeout_ms);   /* NULL if curl init fails */
```
`laya_curl.c` (`CurlTransport {LayaTransport base; char url[256]; int timeout_ms; pthread_t worker; pthread_mutex_t lock; pthread_cond_t wake; struct {int id; char *body;} pending[MAX_COMBATANTS]; int pending_head, pending_count; LayaReply done[MAX_COMBATANTS]; int done_head, done_count; bool closing;}`):
- Rings of `MAX_COMBATANTS` entries. Bound: `LayaDecider` sends at most one request per Combatant at a time, so neither ring can fill; `send` still returns false when `pending_count == MAX_COMBATANTS` (and frees the body), and the worker drops a reply when `done` is full. No other backpressure.
- `send`: lock, push, signal, unlock. `poll`: lock, pop or false, unlock. Both are only ever called from the sim thread inside `laya_decide` (which `battle_decide` calls at the start of a Tick). The worker never touches `Battle` or `GameData`.
- Worker loop: lock; `while (!closing && pending_count == 0) cond_wait`; if closing → unlock, return; pop; unlock; perform (no lock held); lock; push `done`; unlock. One `CURL *` easy handle created in the worker and reused: `CURLOPT_URL`, `CURLOPT_POSTFIELDS` = body, header `Content-Type: application/json` (`curl_slist`), `CURLOPT_TIMEOUT_MS` = timeout_ms, `CURLOPT_CONNECTTIMEOUT_MS` = `LAYA_CONNECT_TIMEOUT_MS` (1000; LAN), `CURLOPT_NOSIGNAL 1`, `CURLOPT_WRITEFUNCTION` appending to a growable `char *` (realloc). Latency = `clock_gettime(CLOCK_MONOTONIC)` around `curl_easy_perform`, in ms.
- Classification: `CURLE_OK` and HTTP 200 → `text`; `CURLE_OPERATION_TIMEDOUT` → `FALLBACK_TIMEOUT`; any other CURLcode → `FALLBACK_UNREACHABLE`; HTTP ≠ 200 → `FALLBACK_MALFORMED` (text freed).
- `close`: lock, `closing = true`, signal, unlock, `pthread_join`, `curl_easy_cleanup` (in the worker before it returns), free queued bodies and replies, `curl_global_cleanup`, free self. `laya_curl_transport` calls `curl_global_init(CURL_GLOBAL_DEFAULT)` and `pthread_create`.

Requests are sent in the order `battle_decide` asks (Initiative order) and performed one at a time, as laya.md rec 11 wants.

### 1.7 LayaDecider (`src/laya/laya.{h,c}`)

```c
#define LAYA_OFFLINE_TICKS 100   /* 5 s of Reflex after an unreachable reply before asking again */
typedef struct {
    Decider base;
    LayaTransport *transport;
    bool in_flight[MAX_COMBATANTS];
    LayaQuestions asked[MAX_COMBATANTS];
    bool has_reply[MAX_COMBATANTS];
    LayaReply reply[MAX_COMBATANTS];
    char briefing[MAX_COMBATANTS][BRIEFING_MAX + 1];   /* last Briefing per Combatant, for the T panel (ticket 12) */
    int offline_until_tick;
    int truncated_count;                                /* replies with usage.truncated, for the technical panel */
} LayaDecider;
void laya_decider_init(LayaDecider *laya, LayaTransport *transport);   /* zeroes, base.decide = laya_decide */
void laya_decider_close(LayaDecider *laya);                            /* transport->close, frees held replies */
```
`laya_decide(self, battle, data, c, out)`:
```
drain: while (transport->poll(transport, &r)): if 0 <= r.id < MAX_COMBATANTS → free(reply[r.id].text); reply[r.id] = r; has_reply[r.id] = true; in_flight[r.id] = false; else free(r.text)
if (has_reply[c]):
    *out = laya_decision(battle, data, c, &asked[c], reply[c].text, reply[c].failure, reply[c].latency_ms, &truncated)
    if (truncated) truncated_count++
    if (reply[c].failure == FALLBACK_UNREACHABLE) offline_until_tick = battle->tick + LAYA_OFFLINE_TICKS
    free(reply[c].text); has_reply[c] = false; return true
if (in_flight[c]) return false
if (battle->tick < offline_until_tick): reflex_decide(NULL, …, out); out->fallback = FALLBACK_UNREACHABLE; return true
briefing_write(battle, data, c, briefing[c], sizeof briefing[c])
body = laya_request_json(battle, data, c, briefing[c], &asked[c])
if (transport->send(transport, c, body)) in_flight[c] = true
return false
```
Replies for Combatants that died in flight sit in `reply[c]` until `laya_decider_close` frees them (bounded by `MAX_COMBATANTS`). Combatant indices are never reused within a Battle (`battle_add_combatant` only appends). A `--load`ed Battle starts with nothing in flight.

### 1.8 Determinism and replay

A Laya Battle is not reproducible from the seed alone: Decisions land on whatever Tick the reply arrives. It **is** reproducible from the Battle Log: each Decision Event records the Tick it was applied (`battle_log_event` stamps `battle->tick`) and the full Decision (Stance, Appraisals, provenance). Replay = `battle_setup(seed)` + `ReplayDecider` over that log: at Tick t for Combatant c it returns the logged Decision whose tick is t, otherwise "not yet". The stop-gap Reflex Decisions (`FALLBACK_WAITING`) are skipped by the replay Decider and regenerated by `ask()` itself — same Tick, same Reflex, same bytes — which also keeps `next_decision_tick` unchanged exactly as in the original run (a WAITING event never reschedules). Everything else in the sim is seeded, so `battle_save_json` after the same number of Ticks is byte-identical. `headless --replay dump.json` proves it (1.10). Replay works the same for pure-Reflex dumps.

### 1.9 Stats for ticket 12

`DecisionStats battle_decision_stats(const Battle *)` (1.2) from the log — share Laya vs Reflex, fallback counts, mean Stance confidence (permille), mean latency (ms) — plus `LayaDecider.truncated_count` and `LayaDecider.briefing[c]` for the per-Combatant panel. Headless prints the same numbers.

### 1.10 Settings file and CLI

`data/laya.json`:
```json
{"url": "http://192.168.0.3:8000/v1/systemone", "timeout_ms": 8000, "min_stance_confidence_permille": 200}
```
`data.h`: `typedef struct { char url[128]; int timeout_ms; int min_stance_confidence_permille; } LayaSettings;` as `GameData.laya`; `load_laya(data, path, err, err_size)` via `load_file` + `laya_from_root`: `url` required non-empty, `timeout_ms` 1..60000, `min_stance_confidence_permille` 0..1000 (range errors in the existing `"<key>: must be between A and B"` style); wired into `load_game_data` after `power.json`. Threshold 200 permille of x_jev: laya.md saw 0.51–0.68 top probabilities on 2-option Stance questions (x_jev 0.02–0.36), so 0.2 keeps the Reflex in charge when Laya is near uniform; map.md already lists tuning it as "not yet specified".

`src/decider_setup.h`:
```c
typedef struct {
    bool laya;
    const char *replay_path;
    Decider reflex;
    LayaDecider laya_decider;
    ReplayDecider replay;
    Battle source;          /* the loaded dump when replaying (seed + log) */
    Decider *decider;       /* pass this to battle_step */
} DeciderSetup;
bool decider_setup_parse(DeciderSetup *setup, int argc, char **argv, int *i);   /* consumes "--decider laya|reflex" or "--replay <path>" at argv[*i]; false otherwise */
bool decider_setup_open(DeciderSetup *setup, const GameData *data, Battle *battle, char *err, size_t err_size);
void decider_setup_close(DeciderSetup *setup);
char *read_text_file(const char *path);   /* moved from main_headless.c */
```
`open`: replay → read + `battle_load_json(&setup->source)`, `battle_setup(battle, data, setup->source.seed)`, `replay_decider_init(&setup->replay, setup->source.log, setup->source.log_count)`, `decider = &setup->replay.base`; laya → `laya_curl_transport(data->laya.url, data->laya.timeout_ms)` (NULL → err "cannot start libcurl"), `laya_decider_init`, `decider = &setup->laya_decider.base`; else `reflex = reflex_decider(); decider = &reflex`. `close`: `laya_decider_close` if laya; `battle_free(&source)` if replaying.

`main_headless.c`: usage `headless [--seed N] [--load state.json] [--ticks T] [--dump out.json] [--decider laya|reflex] [--replay dump.json]`. Arg loop gains `else if (decider_setup_parse(&setup, argc, argv, &i)) {}` before the `else return usage()`. After `load_game_data`: `decider_setup_open` (error → stderr, exit 1); when replaying it already set the Battle up and `ticks = setup.source.tick`, `--load`/`--seed` are ignored; otherwise load/setup as today. Loop: `battle_step(&battle, &data, setup.decider)`. After the loop, when replaying: compare `battle_save_json(&battle)` with `battle_save_json(&setup.source)`; print `Replay: identical` or `Replay: diverged` (exit 1). When `--decider laya`: print `Decisions: %d Laya, %d Reflex (unreachable %d, timeout %d, malformed %d, low confidence %d, waiting %d); mean confidence %d.%03d; mean latency %d ms; truncated %d`. Then `--dump` as today, `decider_setup_close`.

`main_game.c`: the same two calls around whatever loop ticket 10 leaves there; `battle_step(..., setup.decider)`. See commit 8 for the merge rule.

## 2. Commits (in order; each leaves `make test` green)

Each commit: `git add` + `git commit -- <paths>` with the trailer. Run `make test` before every commit; `make asan` at commits 2, 6, 8.

**Commit 1 — `sim: Decision Events carry provenance; Deciders fill an Event`**
Files: `src/sim/battle.h`, `src/sim/battle_json.c`, `src/sim/decide.h`, `src/sim/decide.c`, `tests/test_decide.c`, `tests/test_battle.c`.
- 1.2 in full except `ReplayDecider`/`DecisionStats`.
- `tests/test_battle.c` `test_round_trip_is_identical`: log one more Decision Event with `confidence_permille 415, latency_ms 612, fallback FALLBACK_LOW_CONFIDENCE, stance_permille {0,610,200,0,190}` and CHECK those fields after load; add to `test_load_rejects_bad_input` a decision entry with `"stance_permille":[1,2]` (rejected) and a hit entry **without** the four keys (accepted).
- `tests/test_decide.c`: signature updates (1.2); new `test_waiting_reflex_is_marked`: with the `pending` Decider, every logged Decision has `fallback == FALLBACK_WAITING`; with `reflex`, `FALLBACK_NONE`.

**Commit 2 — `sim: replay Decider and Decision stats`**
Files: `src/sim/decide.h`, `src/sim/decide.c`, `tests/test_decide.c`.
- `ReplayDecider`, `replay_decider_init`, `replay_decide`, `battle_decision_stats`.
- Tests: `test_replay_reproduces_a_reflex_battle`: `battle_setup(seed 1)`, run 300 Ticks with `reflex`, `json = battle_save_json`; `static Battle again; battle_setup(&again, seed 1)`, `replay_decider_init(&r, battle.log, battle.log_count)`, run 300 Ticks with `&r.base`, CHECK save JSON equal. `test_replay_reproduces_a_delayed_decider`: a scripted Decider that returns false for the first 3 calls per Combatant then `decision_event(c, STANCE_RETREAT, …, DECIDER_LAYA)` (so WAITING events and late Decisions both occur); run 100 Ticks; replay as above; JSON equal and `decision_events` counts equal. `test_decision_stats`: hand-build 4 Decision Events (2 Laya conf 400/600 latency 100/300, 1 Reflex low-confidence latency 200, 1 Reflex waiting) → `laya 2, reflex 2, fallback[LOW_CONFIDENCE] 1, fallback[WAITING] 1, mean_confidence 500, mean_latency 200`.

**Commit 3 — `data: Laya settings file`**
Files: `src/sim/data.h`, `src/sim/data.c`, `data/laya.json`, `tests/fixtures/laya_threshold_out_of_range.json` (`min_stance_confidence_permille: 1001`), `tests/fixtures/laya_missing_url.json`, `tests/test_data.c`.
- Tests: real data loads with `data.laya.timeout_ms == 8000`, `min_stance_confidence_permille == 200`, url starts with `http://`; the two fixtures fail with `"min_stance_confidence_permille: must be between 0 and 1000"` and `"url: missing"` (match the exact phrasing `get_string` produces — copy it from an existing `check_error` line).

**Commit 4 — `sim: Briefing builder`**
Files: `src/sim/briefing.h`, `src/sim/briefing.c`, `tests/test_briefing.c`, `tests/test_main.c` (`run_briefing_tests`), Makefile untouched (wildcards).
- 1.3 in full. Internals: a sentence buffer `char sentence[256]` built with `snprintf`, appended by `append(out, &len, sentence)` which enforces the cap and capitalises the first letter; sorting with small insertion sorts over index arrays (n ≤ MAX_PERCEIVED).
- Tests: the worked example (exact string) at Close, the Medium and Far variants (`strstr` checks for absent name/hurt/resists); `test_briefing_has_no_digits` (busy Battle: `battle_setup(seed 1)`, move every Combatant to within 100 px of Combatant 0, `isdigit` never true, every char ASCII); `test_briefing_is_capped` (64 Combatants placed around one: `strlen <= BRIEFING_MAX`, last char `'.'`, starts with the self sentence); `test_monster_subject` (a `biped` Horde Combatant close to Zeus → `"The biped, an enemy, is close …"`); `test_unarmed_combatant_cannot_attack` (destroy Zeus's hand slots → `"Zeus cannot attack."`).

**Commit 5 — `sim: Laya request and reply`**
Files: `src/sim/briefing.h`, `src/sim/briefing.c`, `tests/test_briefing.c`.
- 1.4 and 1.5. `laya_decision` calls `reflex_decide(NULL, …)` for the fallback paths.
- Tests (parse the request with cJSON, never string-compare it): `state` is a string equal to the Briefing; questions = `hurt`, `element`, `stance` with the criteria keys in order for the worked example; `stance` has only fight+retreat when Zeus stands alone; `support the hurt ally` appears for Apollo (healer) with a wounded ally; no `element` question for Agni (Hindu, Fire only) and `asked.element_count == 0`; the canned reply above → expected Event; low x_jev → LOW_CONFIDENCE path; `"<html>"`, `{"answers":5}`, a reply missing `stance`, a `choice` not offered (`"charge"`) → `FALLBACK_MALFORMED` with Reflex values and `latency_ms` preserved; `failure = FALLBACK_TIMEOUT` with NULL text → Reflex + TIMEOUT; `usage.truncated: true` sets `*truncated`; probabilities missing → zeros, x_jev missing → 0 → low confidence.

**Commit 6 — `laya: Decider over a transport seam`**
Files: `src/laya/laya.h`, `src/laya/laya.c`, `Makefile` (add `$(BUILD)/obj/src/laya/laya.o` to the tests link; `LAYA_OBJECTS`/`-lcurl` wait for commit 7), `tests/test_laya.c`, `tests/test_main.c` (`run_laya_tests`).
- 1.6 types and 1.7. `FakeTransport` in the test file: `{LayaTransport base; const char *script[8]; Fallback failure[8]; int count; int sent; int polled; int ids[8]; char *bodies[8];}`; `send` stores id+body (keeps bodies for assertions), `poll` returns `strdup(script[polled])` (or NULL + `failure[polled]`) with `latency_ms = 100 + polled` while `polled < sent`, `close` frees bodies. Scripts past `count` repeat the last entry.
- Tests: `test_laya_asks_once_then_decides`: `battle_setup(seed 3)`, fake with the canned reply; Tick 0: `fake.sent == combatant_count`, every Decision logged is REFLEX+WAITING; Tick 1: each Combatant has a LAYA Decision (fight), `fake.sent` unchanged, `next_decision_tick > 1`, `laya.briefing[c]` non-empty and equals the `state` string inside `fake.bodies[c]` (cJSON). `test_laya_timeout_falls_back`: script = NULL/TIMEOUT → Tick 1 Decisions are REFLEX with `fallback TIMEOUT`, `latency_ms == 100`. `test_laya_unreachable_backs_off`: script = NULL/UNREACHABLE → after Tick 1, run 99 more Ticks: `fake.sent` still `combatant_count`, Decisions logged meanwhile carry `FALLBACK_UNREACHABLE` with `latency 0`; at Tick ≥ 101 a new send happens. `test_laya_malformed_and_low_confidence` (scripts `"nope"` and the low-x_jev reply) → the matching fallbacks in the log. `test_laya_battle_replays`: fake with the canned reply, run 200 Ticks with `&laya.base`, save JSON; fresh `battle_setup(seed)`, replay from the log 200 Ticks, JSON equal; `battle_decision_stats(...).laya > 0`. `test_laya_truncated_is_counted`: reply with `"truncated":true` → `truncated_count == combatant_count`.

**Commit 7 — `laya: libcurl worker transport`**
Files: `src/laya/laya_curl.c`, `Makefile` (`LAYA_OBJECTS`, `-lcurl` on game and headless per 1.1).
- 1.6 in full. No unit test (network). Build check: `make headless` links; `make asan` still green (tests do not link curl).

**Commit 8 — `cli: --decider laya|reflex and --replay for headless and game`**
Files: `src/decider_setup.h`, `src/decider_setup.c`, `src/main_headless.c`, `src/main_game.c`, `Makefile` (`SETUP_OBJECT`).
- Before touching `src/main_game.c`: `git merge main` (ticket 10 may have landed). If `main_game.c` now runs `battle_step`, add the parse/open/close calls and pass `setup.decider`. If it still has no Battle loop, add only the argv loop (`int main(int argc, char **argv)`) with `decider_setup_parse`, the data load, `decider_setup_open` before the window loop and `decider_setup_close` after it, leaving the loop untouched; say so in the hand-back. Either way the reviewer accepts.
- Checks: `make test`, `make asan`, `make headless && make game`; `build/headless --seed 1 --decider reflex --ticks 300 --dump build/d.json && build/headless --replay build/d.json` prints `Replay: identical` (exit 0); `build/headless --seed 1 --decider laya --ticks 200` with the server off ends within ~10 s and prints a Decisions line dominated by `unreachable`; the same with the LAN server up (if reachable) prints Laya > 0 and mean latency in the hundreds of ms; then `--dump build/l.json` and `--replay build/l.json` → identical.

Hand-back additions: the three check commands above with exit codes; whether `main_game.c` got the full wiring or the loop-less variant.

## 3. Pitfalls for the executor

- `Decider base` must be the first member of `ReplayDecider`, `LayaDecider`, `CurlTransport`-style structs; `decide`/`send` cast `self`.
- `battle_log_event` overwrites `event.tick`; never set it yourself.
- `laya_decision` must set `out->actor = combatant` and `-1` in the three part/target fields (`decision_event` does).
- `event_decision` is the only place that maps Event ↔ Decision fields; do not hand-roll `{amount, detail, element}` elsewhere.
- Lower-case `name` check uses `islower((unsigned char)name[0])`; `ctype` casts everywhere.
- Briefing cap check happens before appending; the self sentence always fits.
- The stance `instructions` and all keys are string literals in `briefing.c`; the mapping back from `choice` is `strcmp` against `STANCE_KEYS`, `HURT_WORDS`, `ELEMENT_WORDS` — never `strstr`.
- Under ASan, `pthread` + `curl` in headless is fine; tests never create threads.
- No comments in code; one short line is allowed above the replay WAITING skip and the queue-bound reasoning (`send` cannot fill because one request per Combatant).

### Critical Files for Implementation
- /Users/kirill.lastovirya/Projects/lord-of-light/src/sim/decide.c
- /Users/kirill.lastovirya/Projects/lord-of-light/src/sim/battle.h
- /Users/kirill.lastovirya/Projects/lord-of-light/src/sim/battle_json.c
- /Users/kirill.lastovirya/Projects/lord-of-light/src/sim/briefing.c (new)
- /Users/kirill.lastovirya/Projects/lord-of-light/src/laya/laya.c (new)

## Orchestrator addendum (user request 2026-10-16): local Laya when the LAN box is down

- `data/laya.json` takes `"urls": ["http://192.168.0.3:8000", "http://127.0.0.1:8765"]` (tried in order) instead of a single `url`. A request that fails as unreachable on one URL moves to the next; the 100-Tick offline backoff applies only once every URL is unreachable. The fake-transport tests cover: first URL unreachable → second answers; both unreachable → Reflex with `FALLBACK_UNREACHABLE` + backoff.
- `make laya-local` runs the same pinned server on this Mac, bound to localhost, in the foreground:
  `LAYA_HOST=127.0.0.1 LAYA_PORT=8765 LAYA_DEVICE=cpu LAYA_THREADS=8 LAYA_MODELS=english LAYA_DEFAULT_MODEL=english uv tool run --exclude-newer 2026-10-15 --python 3.12 --from 'laya[serve]==0.4.7' laya-serve`
  (same pins as `~/Projects/localbrains/apps/laya/run.ps1`). Not part of `make test`; no test starts it.
