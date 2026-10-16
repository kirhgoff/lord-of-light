# Parallel Laya requests

Type: task
Status: claimed
Blocked by: 12

## Question

The libcurl transport (`src/laya/laya_curl.c`) runs one worker thread, so requests queue: on seed 1 the mean answer time (queued → applied) is ~2.6 s and Laya makes only ~12 % of Decisions. Run a small fixed pool of workers (e.g. 4, a constant) sharing the existing queue, keeping: one request per Combatant at most, URL fallback and backoff, the abort-on-close callback, TSan-clean shared state, and replay byte-identical. Measure the seed-1 LAN smoke check before and after (answer time, Laya share). No test touches the network; the fake transport tests stay as they are plus one for concurrent completion order.
