# Laya Decider

Type: task
Status: open
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

## Comments

From the ticket 06 review: `most_vulnerable_element` reads the focus's true resistances even at Far Band. Fine for the Reflex, but the Laya Briefing must only expose what the Combatant perceives at that Band.
