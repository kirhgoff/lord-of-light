# Laya Decider

Type: task
Status: open
Blocked by: 06, 13

## Question

- Briefing builder: short English text from Vision bands + own condition (see ticket 13's findings for wording and limits).
- Request: one `/v1/systemone` call per Combatant, one choice question per acting Part with its options as criteria; use `/batch` when several are due.
- libcurl on a worker thread; the sim never blocks; results applied at the next Tick; timeouts and unreachable server fall back to Reflex (logged).
- Record each Decision with source and probabilities in the Battle Log; replay from the log reproduces the Battle.
- `--decider laya|reflex` flag for both binaries; tests use a fake transport (canned responses), never the network.
- Stats for the end screen: share of Decisions from Laya vs Reflex, mean confidence, mean latency.
