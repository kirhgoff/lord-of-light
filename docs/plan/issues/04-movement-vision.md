# Movement, collision, line of sight, Vision bands

Type: task
Status: claimed
Blocked by: 03

## Question

In `src/sim/`:

- Movement toward a point at the Body's speed (from leg/wing Parts; zero speed if all are destroyed), circle collision between Combatants, against obstacles and the Arena walls; simple steering around obstacles (grid A* on the 48×32 tiles is fine).
- Line of sight over tiles (obstacles block).
- Vision: for a Combatant, list what it perceives in each Band (Close/Medium/Far) by range from its Parts and LOS, with the detail level per Band as in the glossary.
- Tests: wall blocks, obstacle blocks LOS, band assignment, crippled legs stop movement.

## Comments

**Review 1 (opus) of 8c88f90: CHANGES.** Criteria met, `make test`/`make asan` green, mid-path resume deterministic. To fix (send to an executor on the existing branch):
1. LOS (`vision.c:9-19`) samples every 2 px and sees through corner joints of diagonal solid tiles and clipped corners (e.g. solid (10,10)+(11,11), segment (146000,206000)→(206000,146000) mpx returns true). Replace with Amanatides–Woo grid traversal; block on a corner crossing if either adjacent tile is solid (matches the no-corner-cut walking rule). Tests for both cases.
2. Build is `-O0` (`Makefile:5`): add `-O2` to STRICT (3–5× faster; 64 movers to an unreachable target: 41 ms/Tick at -O0 vs 12 ms at -O2). Reject unreachable targets with a reachability flood fill before A* (unreachable search 420 µs → ~12 µs). Binary heap optional; no cached paths in JSON.
3. Solid/unreachable target freezes the Combatant (`movement.c:197`): A* should fall back to the closed node with the lowest heuristic. Add a test (the solid-target early return currently survives mutation).
4. Perception never reports Combatant kind (`vision.c:66`, `def = UNKNOWN`): carry kind (Deity name or Body Plan id) at Close and Medium; add a position to each Perceived so Far needs no index lookup.
5. Untested branches (mutations stay green): charm_side in perceive (`vision.c:63`), wall sliding fallback (`movement.c:185`), respawning Pickups hidden (`vision.c:74`), move_to Orders on destroyed Parts ignored (`combatant_move_target`).
Noted for ticket 06: opposing groups deadlock at a gap (steering aims at tile centres, no sidestep).
Design questions for the user (see STATUS): collision radius (5 px now; reviewer suggests a 10–12 px feet footprint with clearance-aware A*), and whether a Combatant charmed to your side stops counting as an enemy to you.
Reviewer probes (volatile): scratchpad `perf/`.
