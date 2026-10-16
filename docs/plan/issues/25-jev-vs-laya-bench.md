# Benchmark Jev vs Laya (local and tomhat)

Type: task
Status: claimed
Blocked by: none

## Question

Compare TypeSafe's hosted Jev with Laya running locally (`make laya-local`) and Laya on the LAN server tomhat (`192.168.0.3:8000`), using the exact request bodies the game sends. Measure speed (cold/warm latency, p50/p95/max, failures) and Decision behaviour (agreement between backends and with the Reflex, Stance margin, share clearing `min_stance_confidence_permille`). Corpus from a new `--dump-requests` flag on the Reflex path; a stdlib Python script in `tools/` replays it and writes JSONL to `build/` and an HTML report to `~/Downloads/_reports/`. The API key lives in the gitignored `.jev_key` and never appears in output.

Out of scope: making the game's decider talk to Jev (follow-up: `laya_decision` reads `x_jev_confidence`, which Jev doesn't send).
