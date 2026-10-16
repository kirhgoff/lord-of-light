# Laya Decider

Type: task
Status: resolved
Blocked by: 06, 13

## Question

(Shape set by ADR 0003; follow every recommendation in `docs/research/laya.md`.)
- Briefing builder: plain-string English prose, third person with the name, one fact per sentence, words not numbers, most important first, ≤1,200 chars.
- Request: one `/v1/systemone` call per Combatant with the two Appraisal questions and the Stance question (rule in `instructions`, keys are the verb phrases); `state` as a string. No batch.
- If the Stance `x_jev_confidence` is below a threshold (in a data file), use the Reflex's Stance and log why.
- libcurl on a worker thread; the sim never blocks; results applied at the next Tick; timeouts and unreachable server fall back to Reflex (logged).
- Record each Decision with source and probabilities in the Battle Log; replay from the log reproduces the Battle.
- `--decider laya|reflex` flag for both binaries; tests use a fake transport (canned responses), never the network.
- Stats for the end screen: share of Decisions from Laya vs Reflex, mean confidence, mean latency.

## Answer

Merged `11-laya-decider` after one review round plus user decisions, following `docs/plan/plan-11-laya.md`. `src/sim/briefing.{h,c}` (pure): Briefing prose (third person, words not numbers, only what is perceived per Band, ≤ 1,200 chars), the `/v1/systemone` request (two Appraisals + Stance) and reply parsing. `src/laya/`: the Laya Decider over a `LayaTransport` seam, with a libcurl worker thread (`laya_curl.c`, linked into headless and game only) and a fake for tests. `data/laya.json`: `urls` tried in order (LAN box, then `make laya-local` on 127.0.0.1:8765), 8 s timeout, Stance confidence threshold 200‰. Unreachable or timed-out requests fall back to the Reflex with a 100-Tick backoff; low confidence keeps Laya's Appraisals with the Reflex Stance. While a request is pending the Reflex keeps re-deciding on schedule. Decision Events carry decider, fallback reason, Stance probabilities, confidence and answer time (queued → applied). `--decider laya|reflex` and `--replay <dump>` for headless and game; replay re-saves byte-identical. Known: one worker thread serialises requests, so answer times (~2.6 s on seed 1) include queue wait.

## Comments

From the ticket 06 review: `most_vulnerable_element` reads the focus's true resistances even at Far Band. Fine for the Reflex, but the Laya Briefing must only expose what the Combatant perceives at that Band.

**Plan (fable): `docs/plan/plan-11-laya.md`.** User accepted all nine defaults 2026-10-16, plus the addendum: URL list with a local `make laya-local` fallback.

**Review 1 (opus): CHANGES.** Confidence parsing verified against the live server (x_jev read correctly; Laya is near-uniform on Stance, as laya.md predicts); TSan clean; queue bounded; replay identical. Fixed in c26ba4d..a19ee9d: connect timeout counted as unreachable (local fallback now reachable), Decision Event bounds on load, double clamp before int cast, backoff after timeouts, quit no longer waits for a request, named Event initialisers instead of a global warning flag, 1 MiB reply cap, game replay pauses at the dump's end, ASCII-only names in the data.
**User decisions 2026-10-16:** (A) the Reflex keeps re-deciding on its schedule while a Laya request is in flight; (B) keep Laya choosing the Stance with the 0.2 threshold, tune later while watching Battles; (C) latency = request queued → answer applied.

**Decisions A and C** in 7aadae9, db0cee8 (orchestrator verified: tests, asan, replay identical).
