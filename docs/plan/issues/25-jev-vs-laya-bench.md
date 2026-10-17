# Benchmark Jev vs Laya (local and tomhat)

Type: task
Status: resolved
Blocked by: none

## Question

Compare TypeSafe's hosted Jev with Laya running locally (`make laya-local`) and Laya on the LAN server tomhat (`192.168.0.3:8000`), using the exact request bodies the game sends. Measure speed (cold/warm latency, p50/p95/max, failures) and Decision behaviour (agreement between backends and with the Reflex, Stance margin, share clearing `min_stance_confidence_permille`). Corpus from a new `--dump-requests` flag on the Reflex path; a stdlib Python script in `tools/` replays it and writes JSONL to `build/` and an HTML report to `~/Downloads/_reports/`. The API key lives in the gitignored `.jev_key` and never appears in output.

Out of scope: making the game's decider talk to Jev (follow-up: `laya_decision` reads `x_jev_confidence`, which Jev doesn't send).

## Answer

Merged (PR #3) after one review round. Seeds 1+2, 60 records: Jev reused-connection p50 229 ms, 97 % of Stances clear 0.2, 78 % agree with the Reflex; Laya local p50 250 ms, tomhat 564 ms, both 32 % clear, 55 % Reflex agreement; Jev and Laya agree on 52 % of Stances. Report: ~/Downloads/_reports/2026-10-17-jev-vs-laya.html. Follow-up: the game reads `x_jev_confidence`, which Jev does not send.
