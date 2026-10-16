# Order options, Reflex, Decision scheduling

Type: task
Status: open
Blocked by: 05

## Question

- For each acting Part, build its options: ≤5 concrete Orders (most relevant targets + `idle`), from what the Combatant perceives.
- A Decider interface: given a Combatant and its options, return a Decision (one choice per acting Part) — possibly later (async).
- Reflex Decider: deterministic scoring (attack the weakest reachable enemy with the best Element vs its resistances, retreat to healing when Vitality < 30%, support hurt allies, avoid seen Traps).
- Scheduling: each Combatant asks again every `k / Initiative` Ticks; due Combatants are queued by Initiative; until a Decision arrives they keep their current Orders (or the Reflex's if they have none).
- Decisions are Events in the Battle Log (with source: reflex/laya, and probabilities if any).
- Test: `headless --seed 1` runs a full Battle to a winner deterministically (same seed → identical Battle Log).
