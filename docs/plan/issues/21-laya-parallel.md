# Parallel Laya requests

Type: task
Status: resolved
Blocked by: 12

## Question

The libcurl transport (`src/laya/laya_curl.c`) runs one worker thread, so requests queue: on seed 1 the mean answer time (queued → applied) is ~2.6 s and Laya makes only ~12 % of Decisions. Run a small fixed pool of workers (e.g. 4, a constant) sharing the existing queue, keeping: one request per Combatant at most, URL fallback and backoff, the abort-on-close callback, TSan-clean shared state, and replay byte-identical. Measure the seed-1 LAN smoke check before and after (answer time, Laya share). No test touches the network; the fake transport tests stay as they are plus one for concurrent completion order.

## Answer

Won't do; not merged. Branch `21-laya-parallel` (6a0be77: 4 curl workers, replies applied in Combatant order, TSan clean, replay identical) is kept for reference. Measured seed 1 on the LAN server, 3 runs each: main ~2815 ms mean answer time, 10 % Laya share; branch ~2990 ms, 10 %, within noise. Cause, already recorded in `docs/research/laya.md` (Concurrency): the server runs all inference behind one lock on a single worker, so parallel requests queue. The ticket should not have been filed without checking that. Real levers, per laya.md: shorter Briefings (a 25-token prompt is ~90 ms per question vs ~230 ms), fewer questions per request, or asking less often.
