# Tactics, Reflex, Decision scheduling

Type: task
Status: claimed
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

## Comments

**Plan (fable): `docs/plan/plan-06-tactics.md`.** User accepted all its defaults 2026-10-16, plus walk-over Pickups for both 06 and 08 (`ORDER_PICK_UP` removed by 08).
