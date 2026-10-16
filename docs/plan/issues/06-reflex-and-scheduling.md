# Tactics, Reflex, Decision scheduling

Type: task
Status: resolved
Blocked by: 05

## Question

(Shape set by ADR 0003.)
- Decision = Stance (fight / retreat / support / collect) + Appraisals (own condition: unhurt/wounded/badly wounded/near death; best Element vs the focus target; is an ally near death).
- Tactics: deterministic mapping from Decision + what the Combatant perceives to one Order per acting Part (fight: strike/cast the focus target with the appraised Element, legs approach to range; retreat: move away / to healing, block; support: support the hurt ally; collect: move to and pick up the nearest useful Pickup; avoid seen Traps).
- A Decider interface: given a Combatant, return a Decision — possibly later (async).
- Reflex Decider: derives the Decision from Battle state (retreat when near death, support when an ally is near death and it can, collect when a healing Pickup is closer than any enemy and it is wounded, else fight; best Element from target resistances).
- Scheduling: each Combatant asks again every `k / Initiative` Ticks; due Combatants are queued by Initiative; until a Decision arrives they keep their current Orders (or the Reflex's if they have none).
- Decisions are Events in the Battle Log (with source: reflex/laya, and probabilities if any).
- Test: `headless --seed 1` runs a full Battle to a winner deterministically (same seed → identical Battle Log).

## Answer

Merged `06-reflex-and-scheduling` after 2 review rounds, following `docs/plan/plan-06-tactics.md`. `src/sim/decide.{h,c}`: Decision = Stance + HurtLevel (4 quarters) + appraised Element; a Decider is a function pointer + kind returning false for "not yet" (async-ready, ADR 0001); the Reflex retreats only near death with a healer in sight, supports a near-death ally, collects a healing Pickup when wounded and it is nearer than any enemy, else fights. Scheduling: every 60 / Initiative Ticks (sum of working Parts' initiative, floor 1, Part cap 100), due Combatants queued by Initiative. Tactics re-run every Tick from the stored Decision: block/support/spawn Parts always do their job, the Stance steers the legs; seek toward the nearest enemy by true position when nothing is perceived; trap avoidance. Decision Events carry `Event.decider`. Movement: tangent slide around a blocking Combatant; in a cramped head-on gap the higher index backs out. Crow and wisp minions got vision. Seeds 0..199 reach a winner within 4000 Ticks.

## Comments

**Plan (fable): `docs/plan/plan-06-tactics.md`.** User accepted all its defaults 2026-10-16, plus walk-over Pickups for both 06 and 08 (`ORDER_PICK_UP` removed by 08).

**Review 1 (opus): CHANGES.** Deviations accepted (DECIDER_NONE in initialisers, COLLECT always move_to, trap test bound); sweep 0..199 no stalls, save/resume identical, charm targeting and the Decider interface held. To fix: merge main (08) and repair floor.c initialisers / removed trap and pickup ids in tests; allies oscillating head-on at a 2-tile gap (higher index steps back); trap test bound from the radius; SUPPORT-legs-to-enemy mutation survives; cap Part initiative at 100.

**Re-review (opus): APPROVE** — gap rule held in corridor and sweep probes; trap test still red without avoidance.
