# How Laya behaves with game-style questions

Research for ticket 13 (`docs/plan/issues/13-laya-behaviour.md`), feeding ticket 11.
Measured 2026-10-16 against `http://192.168.0.3:8000` running `laya[serve]==0.4.7`, checkpoint
`english` (revision `7b928d82`), CPU, 8 threads (`~/Projects/localbrains/apps/laya/run.ps1`).
About 25 requests in total. Every claim about the protocol cites the package source
(`laya/serve.py`, `laya/common.py`, `laya/agent.py`, `laya/confidence.py` in the 0.4.7 wheel);
every behavioural claim cites a measured call below.

## TL;DR

- **Latency is linear in input tokens: about 1.5 s per 1,000 tokens, roughly 650 tokens/s.** Each
  question is a separate model row of `head + state` tokens. A 100-token Briefing costs about 230 ms
  per question, and a 25-token one about 90 ms. Batching does not make anything faster, and the
  server runs one inference at a time.
- **The state window is about 445–480 tokens (about 1,500 characters) per question.** Anything past
  that is cut from the end without warning.
- **Laya is a strong *situation classifier* and a weak *Order chooser*.** "How hurt is Agni?" and
  "Which Element hurts the ogre most?" score 0.82–0.95 on the right answer. "Choose the Order for
  the legs" ignores Vitality completely and keeps a fixed prior ("advance", "cast_fire", "attack").
- **It does not read numbers.** "Vitality 5 of 100" and "Vitality 95 of 100" give the same answers.
  "near death" and "unhurt" work.
- **Comparisons and attribution across Combatants are unreliable.** Ask about one Combatant at a
  time.

## Protocol

### Single: `POST /v1/systemone`

The body is an object with a required `questions` field and an optional `state` field
(`serve.py:_systemone_inner`). The state can be a string or a JSON value. **A non-string state is
serialised with `json.dumps` before tokenising** (`common.py:serialize_state`). So
`{"state":{"body":"…"}}` sends the literal `{"body": "…"}` to the model, which adds about 5 tokens
and moved the probabilities measurably: 0.338 against 0.485 for the same question (calls 01 and 02).
**Send `state` as a plain string.**

Optional body controls (`serve.py:BODY_CONTROLS`): `model`, `max_len`, `head_max_len`, `task`,
`lang`, `lang_guess` and `min_confidence`.

```json
{
  "state": "Agni is near death. Ally Indra is 6 m south and unhurt. An ogre is 4 m north.",
  "questions": {
    "hurt_agni": {"type": "choice", "instructions": "How hurt is Agni?",
                  "criteria": {"healthy": "unhurt", "wounded": "wounded", "near_death": "near death"}}
  }
}
```

Response (from call 12, per state):

```json
{
  "model": "laya-rl-agent",
  "answers": {
    "hurt_agni": {
      "type": "choice", "choice": "near_death",
      "probabilities": {"healthy": 0.0886, "wounded": 0.0934, "near_death": 0.818},
      "confidence": 0.4535, "answer_confidence": 0.818, "x_jev_confidence": 0.727,
      "action": {"act_probability": 1.0}
    }
  },
  "usage": {"input_tokens": 169, "output_tokens": 0, "state_tokens": 26,
            "state_tokens_dropped": 0, "truncated": false, "truncated_questions": []},
  "routing": {"model": "english", "repo": "convaiinnovations/laya", "reason": "English Latin text", "detection": {"…": "…"}}
}
```

- `choice` is the argmax key, and `probabilities` holds every key in criteria order
  (`agent.py` around line 1365).
- `usage.state_tokens_dropped` is the **maximum** over the request's questions, and
  `truncated_questions` names the questions whose state was cut (`agent.py` around line 1543).
- The server also returns the headers `X-Inference-Time-Ms` and `Server-Timing`.
- `action.act_probability` was 1.0 on every answer we saw. Ignore it.
- `routing.reason` would change if a Briefing were not plain English. Keep it ASCII English.

### Batch: `POST /v1/systemone/batch`

Takes `{"states": [s1, s2, …], "questions": {…}}`. **The same `questions` apply to every state**
(`serve.py:_systemone_batch_inner`). The response is
`{"results": [<single response per state>], "total_usage": {"input_tokens", "output_tokens"}}`.
Because the questions are shared, a batch only fits Combatants whose acting Parts have identical
option lists. For different Bodies it is the wrong tool. Limits: 64 states, 64 questions, 50,000
characters per state, a 2 MB body, and 100 options per choice (`serve.py` module constants).

### Concurrency

All inference runs behind one `asyncio.Lock` (`gate`) on a single-worker pool. Parallel requests
queue rather than speed up. More than 16 requests in flight at once get a 503 (`LAYA_MAX_CONCURRENT`).

### `confidence` vs `answer_confidence`

| field | formula | use |
|---|---|---|
| `answer_confidence` | `max(p)`, the probability of the chosen option, after the checkpoint's temperature/binning map if one is fitted (`common.py:answer_confidence`, `agent.py`) | **the number to threshold.** `min_confidence` gates on it (`confidence.py:answer_confidence_value`) |
| `confidence` | `1 − H(p)/log k`, normalised entropy, "not calibrated" (`common.py:confidence_from_probs`) | how peaked the whole distribution is. It depends on the option count, so it does not compare across Parts with 2 and 5 options |
| `x_jev_confidence` | `(max p − 1/k)/(1 − 1/k)`, Jev's formula (`confidence.py:jev_confidence`) | margin over uniform. The only one of the three that is comparable across option counts |

Example (call 01): `p = {0.34, 0.23, 0.21, 0.21}` gives `answer_confidence` 0.34, `confidence` 0.014
and `x_jev` 0.12. The package itself warns that the shipped checkpoints are over-confident and
that `answer_confidence` is calibrated only after per-checkpoint fitting (`common.py:answer_confidence`
docstring).

## Latency

All numbers are warm. `infer` is the server-side `X-Inference-Time-Ms`. LAN overhead was about 20 ms.

| call | rows (states × questions) | input tokens | infer ms | wall ms | ms/row | ms per 1k tokens |
|---|---|---|---|---|---|---|
| 02 1 question, 100-token Briefing | 1 | 166 | 267 | 287 | 267 | 1611 |
| 04 3 questions | 3 | 456 | 694 | 712 | 231 | 1523 |
| 05 5 questions | 5 | 734 | 1138 | 1154 | 228 | 1551 |
| 07 **batch of 8 × 3 questions** | 24 | 3648 | 5571 | 5607 | 232 | 1527 |
| 12 batch, 25-token Briefings | 12 | 730 | 1061 | 1076 | 88 | 1454 |
| 14 1 question, state truncated to 512 | 1 | 512 | 800 | 828 | 800 | 1563 |
| 15 3 questions, truncated | 3 | 1536 | 2265 | 2281 | 755 | 1474 |

- **Cost per round is about 1.5 ms × (head tokens + Briefing tokens) × questions.** Each question
  re-reads the whole Briefing (`build_sequence` builds one sequence per question), so a long Briefing
  is paid for once per Part.
- **Batching gives no throughput gain:** 232 ms/row batched against 228 ms/row single. It only saves
  HTTP round trips.
- **Cold start:** the first call we made (01) took 402 ms against 267 ms warm. That is not the
  reported 6.3 s. `LAYA_PRELOAD` defaults to 1 and `run.ps1` sets no `LAYA_IDLE_UNLOAD_SECONDS`
  (default 0, never unload), so the checkpoint stays resident. A 6 s first call most likely means the
  process had just started (boot task, torch init). Treat about 6 s as a once-per-server-restart cost,
  not a once-per-Battle cost.
- **Answers are deterministic.** Identical requests return identical probabilities (calls 02 and 03).
  A question's answer does not depend on which other questions share the request (call 02 against
  call 04).

## Truncation

The english checkpoint's `max_len` is 512 tokens per row. The sequence is
`[CLS] <type> question: instructions [SEP] [MASK] key: description … [SEP] state [SEP]`
(`common.py:build_sequence`), and the state gets whatever is left:
`room = max_len − head − 1`. The state is **cut from the right**, so the end of a string Briefing
is lost first. Only a list state is cut from the left.

Measured with a 3,842-character state (1,146 tokens, about 3.35 characters per token):

- Small head (call 14, 3 short options): 483 state tokens used, 663 dropped. "Agni is near death" sat
  at the end and was cut, and the answer flipped to `healthy`.
- Big head (call 15, 3–4 options with 6–8-word descriptions): 445 tokens used.

Other per-question limits: each rendered option is capped at 48 tokens, and the whole head at
`head_max_len` (default 192). Beyond that, options are squeezed (`common.py:build_head`).
**Practical limit: keep the Briefing under about 1,200 characters (about 350 tokens) and put the
facts that matter first.**

## Behaviour with game situations

Full requests and responses are in the "Sample calls" section.

### Order questions do not react to the situation

The legs question (`advance` / `retreat` toward the healing Pickup / `hold`) got the same answer
across these cases:

| Briefing | p(advance) | p(retreat) |
|---|---|---|
| 470-character Briefing, Vitality 95 … 5 (8 states, call 07) | 0.47–0.50 | 0.23–0.25 |
| short, "Vitality 5 of 100, near death" | 0.60 | 0.19 |
| short, "will die from one more hit" | 0.59 | 0.17 |
| short, "Vitality 100 of 100, unhurt" | 0.46 | 0.23 |

The same held when we reordered the options (retreat first), used opaque keys, used key-only
options, or added "best when badly hurt" to the retreat option (calls 08 and 09). The attack
question ("strike with Kinetic sword" vs "cast Fire") chose `cast_fire` 0.58–0.89 whether the ogre
resisted Fire or Kinetic (call 10). "Support hand" chose `attack` unless an ally was near death.
Laya brings a strong prior per option wording, and the Briefing barely moves it.

### Situation questions react strongly

When the Briefing states each fact in words, single-fact classification questions are accurate
(calls 09, 10 and 12):

| question | Briefing says | answer |
|---|---|---|
| How hurt is Agni? | "Agni … near death" | near_death 0.84 |
| How hurt is Agni? | "Agni … unhurt" | healthy 0.91 |
| Which Element hurts the ogre most? | "resists Fire and is weak to Kinetic" | kinetic 0.95 |
| Which Element hurts the ogre most? | "resists Kinetic and is weak to Fire" | fire 0.93 |
| How hurt is Indra? (two Combatants in the Briefing) | Indra near death / Agni near death | near_death 0.84 / healthy 0.86 |
| How hurt is Agni? (two Combatants in the Briefing) | Agni near death / Indra near death | near_death 0.82 / healthy 0.89 |

When the Briefing holds no evidence, the probabilities drop to near uniform: Element question about
an unrelated Briefing, 0.50–0.52 (call 11). That makes `answer_confidence` a usable "Laya doesn't
know" signal.

### What fails

- **Numbers.** "Vitality 5 of 100." and "Vitality 95 of 100." both gave `healthy` (0.47 and 0.67) for
  "How hurt is Agni?" (call 10). Inside the long Briefing, both gave `wounded` (about 0.40).
- **Comparisons between Combatants.** "Who most needs protection?" answered Indra 0.85 whether Agni
  or Indra was the one near death (call 12). The same failure appeared with "you" phrasing (call 11).
- **Second person.** "You are near death…" did worse than the named third person.
- **Line or record format.** "Agni (self): near death.\nIndra (ally …): unhurt." confused attribution
  (call 12: Agni rated near_death 0.58 when only Indra was). Plain prose sentences worked.
- **Conditional option text.** "Kinetic sword, when the ogre is weak to Kinetic" inverted the choice
  (call 13).

### Partial: put the rule in the instructions

Moving the decision rule into `instructions` makes Order questions point the right way, but with
thin margins (call 13, 4 states):

- `"Agni attacks with the Element the ogre is weak to. Choose Agni's attack."` was right 4 of 4 times,
  with p 0.60–0.68.
- Legs options `"charge the ogre, when Agni is unhurt"` / `"flee to heal, when Agni is near death"`
  were right 4 of 4 times, with p 0.51–0.64.

### Keys against descriptions

Each option is rendered as `"key: description"`, or as just `key` when the description is empty
(`common.py:render_options`). **The model reads both.** From call 11 ("Which Element hurts the ogre
most?"):

- Opaque keys with descriptions (`x: Kinetic`): right 0.80 / 0.87.
- Plain-word keys with no description (`Kinetic`): right 0.94 / 0.96.
- Lower-case key plus a capitalised duplicate (`fire: Fire`): one case right 0.96, the other near
  chance (0.51).
- Key and description that contradict each other (`fire: Kinetic`): about 0.5–0.76, which follows
  neither cleanly.

Redundant or conflicting key/description pairs add noise. A key that *is* the option wording, or a
neutral key plus one description, works best. Option order and casing visibly affect results (the
`el_swapped` row). We did not try to separate these two effects.

## Recommendations for ticket 11

**Briefing format**

1. Send `state` as a **plain string**, not `{"body": …}`.
2. Write **short English prose, one sentence per fact, third person, using the Combatant's name**.
   Use no line or record format and no "you".
3. **Put conditions into words chosen by the game, not numbers:** Vitality as
   `unhurt / wounded / badly wounded / near death`, distance as Bands (`close / near / far`),
   resistances as `"<X> resists Fire and is weak to Kinetic."`. Each Combatant's sentence carries
   its own state: `"Indra, an ally, is near and near death."`
4. **Most important facts first** (self condition, then the closest threat, then allies), and stay
   under about 1,200 characters. Watch `usage.truncated` and log it in the technical panel.
5. **Every token costs latency once per Part.** Budget about 1.5 ms × (Briefing + head tokens) per
   question. A 60-token Briefing with 3 Parts is about 0.4 s. With 8 Combatants that is about 3–4 s
   per full round, serialised on the server.

**Option wording**

6. Keep **2–3 options per Part**. Use **verb-phrase keys that are the wording itself**
   (`"strike the ogre"`, `"flee to the healing pickup"`) or a neutral id with a single description.
   Never repeat the key in the description, and never let them disagree.
7. **Put the decision rule in `instructions`**, for example
   `"Agni attacks with the Element the ogre is weak to. Choose Agni's attack."` Do not put
   "when …" clauses in options.
8. **Do not ask comparison questions** ("who needs help most"). Ask one Combatant at a time, or let
   the game pick the target and ask only whether to support.

**Using the answer**

9. Threshold on **`x_jev_confidence`** (comparable across option counts) or on `answer_confidence`
   per option count. Never threshold on `confidence`. Order questions came back at only 0.51–0.68 on
   2 options, so when the margin is thin, keep the Reflex's Order.
10. **Design consequence for ADR 0001:** Laya is reliable at reading the *situation* and poor at
    turning it into an *Order*. Ticket 11 should consider a fallback design: ask Laya 1–3 situation
    questions per Combatant ("how hurt is X", "which Element hurts the target", "is an ally near
    death"), and let the game map those answers to Orders deterministically. This keeps Laya on the
    task it does well, cuts the questions per Combatant from one per Part to a shared few, and keeps
    the Orders explainable in the Battle Log. If ADR 0001's one-question-per-Part design stays, apply
    points 6–7 and expect weak margins.
11. Use `/v1/systemone/batch` only when several Combatants share identical questions. With situation
    questions phrased per Combatant name, they will not, so send one `/v1/systemone` request per
    Combatant from the worker thread, in Initiative order. Parallel requests gain nothing.

## Sample calls

These are the calls the findings rest on, trimmed; the numbers are the server's
probabilities rounded to two places.

**Call 07: 8 Combatants in one batch** (Vitality 95…5 in a 470-character Briefing; legs probabilities
barely move):

```json
{"states": ["You are Agni, a Deity of the Pantheon. Vitality 95 of 100. Parts: right hand holds a sword (Kinetic), left hand casts Fire, legs, head. Close: a Horde ogre, 4 m north, Vitality high, resists Fire. Medium: a Horde wolf, 15 m east, badly hurt. Far: a healing Pickup to the west. Ally Indra is 6 m south, Vitality 70 of 100.", "… Vitality 5 of 100 …"],
 "questions": {"legs": {"type": "choice", "instructions": "Choose the Order for the legs.",
   "criteria": {"advance": "move toward the nearest enemy",
                "retreat": "move away from enemies toward the healing Pickup",
                "hold": "stay in place"}}}}
```
→ Vitality 95: `advance 0.50 / retreat 0.24 / hold 0.26`. Vitality 5: `advance 0.50 / retreat 0.24 / hold 0.26`.
This took 5,571 ms of inference for 24 rows.

**Call 10: Element resistance, as a situation question and as an Order question:**

```json
{"states": ["Agni is unhurt. An ogre is 4 m north. The ogre resists Fire and is weak to Kinetic.",
            "Agni is unhurt. An ogre is 4 m north. The ogre resists Kinetic and is weak to Fire."],
 "questions": {
  "element": {"type": "choice", "instructions": "Which Element hurts the ogre most?",
              "criteria": {"kinetic": "Kinetic, the ogre is weak to it", "fire": "Fire, the ogre is weak to it"}},
  "attack": {"type": "choice", "instructions": "Choose the Order for Agni's attack.",
             "criteria": {"strike_sword": "strike the ogre with the Kinetic sword", "cast_fire": "cast Fire at the ogre"}}}}
```
→ element: `kinetic 0.95` / `fire 0.93` (both right). attack: `cast_fire 0.72` / `cast_fire 0.89`
(the first is wrong).

**Call 12: attribution across two Combatants:**

```json
{"states": ["Agni is near death. Ally Indra is 6 m south and unhurt. An ogre is 4 m north.",
            "Agni is unhurt. Ally Indra is 6 m south and near death. An ogre is 4 m north."],
 "questions": {
  "hurt_indra": {"type": "choice", "instructions": "How hurt is Indra?",
                 "criteria": {"healthy": "unhurt", "wounded": "wounded", "near_death": "near death"}},
  "needy": {"type": "choice", "instructions": "Who most needs protection?",
            "criteria": {"agni": "Agni", "indra": "Indra", "nobody": "nobody, both are unhurt"}}}}
```
→ hurt_indra: `healthy 0.86` / `near_death 0.84` (both right). needy: `indra 0.85` / `indra 0.85`
(the first is wrong).

**Call 13: the rule in the instructions:**

```json
{"questions": {"attack_rule": {"type": "choice",
   "instructions": "Agni attacks with the Element the ogre is weak to. Choose Agni's attack.",
   "criteria": {"strike_sword": "strike with the Kinetic sword", "cast_fire": "cast Fire"}}}}
```
→ ogre resists Fire: `strike_sword 0.63–0.68`. Ogre resists Kinetic: `cast_fire 0.59–0.60`.

**Call 14: truncation:** a 3,842-character state that ends with "Agni is near death." returns
`usage = {"input_tokens": 512, "state_tokens": 1146, "state_tokens_dropped": 663, "truncated": true,
"truncated_questions": ["cond"]}` and `healthy 0.53`.
