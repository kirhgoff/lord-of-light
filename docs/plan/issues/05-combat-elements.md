# Orders, combat, Elements, death

Type: task
Status: resolved
Blocked by: 04, 17

## Question

Execute the 8 Order kinds per Part each Tick (strike, cast, block, move_to, pick_up, support, spawn, idle) with ranges and cooldowns from Part abilities.

- Damage hits one target Part (chosen from what the attacker's reach can touch, external first unless the Element says otherwise), scaled by the target's resistances; also takes a share off Vitality. Destroyed Parts and their cut-off Parts stop working.
- Element effects: Fire DoT, Frost slow/root, Lightning chain, Radiance ignores armour Parts, Venom hits Internal Parts first, Mind applies Charm, Void strips buffs, Kinetic plain.
- Spawning creates minion Combatants on the spawner's side up to the cap.
- Death when Vitality ≤ 0 or all Hearts destroyed → Remains. Battle ends when a side has no living Combatants.
- Every hit, effect, death and spawn appends an Event (with source Combatant, Part, Element, amount, Tick).
- Each Element and each death rule has a test from a hand-written JSON state.

## Answer

Merged `05-combat-elements` after one review round, following `docs/plan/plan-05-combat.md`. `src/sim/combat.{h,c}`: each Tick runs act (Orders per working Part with ability range, LOS and cooldown) → effects → movement. Hits pick a target Part from reach (external first; Venom internal first), scale by body-wide resistance (clamped ±100; Radiance ignores external Parts), block halves, Vitality takes 50 %; all in 64-bit. Elements are data-driven from ElementDef: Fire/Venom DoT every 20 Ticks, Frost slow, Lightning chain (nearest within 96 px of the previous hit, half damage, cap), Mind charm, Void strips the damage bonus. No effects on a killing hit. Death = Vitality ≤ 0 or no working Heart → Remains with every Part. Spawning up to a per-Combatant cap; minions copy the spawner's charm. Winner: a side whose living members are all charmed to the other side has lost. Events hit/effect/death/spawn carry source Combatant and Part (−1 only for DoT pulses). The loader bounds ability amounts and ElementDef fields to the save ranges. Old saves no longer load.

## Comments

**Plan (fable): `docs/plan/plan-05-combat.md`.** User accepted all defaults 2026-10-16: Radiance ignores resistance from external (armour) Parts, `pierce_targets` unused; "buff" = `damage_bonus_percent/ticks` (Void strips it, nothing grants it yet); ability `area` deferred to 06; body-wide resistance clamped ±100 scales hit damage and effect durations, Vitality share 50 %, block 50 %, DoT every 20 Ticks, lightning hop half damage within 96 px; death = Vitality ≤ 0 or no working Heart, Remains keep every Part; minions spawn at the spawner, copy its charm, one cap per Combatant; old saves stop loading.

**Review 1 (opus): CHANGES.** All executor deviations accepted (chain test geometry, cap test damage 40, injected radiance resistance, direct `combatant_hit` tests, timers one lower after a step, data-driven `apply_element`, Void amount 1). Determinism, save/load resume, chain targeting, spawn cap and death rules held.
1. Mind charm-lock: the last Horde Combatant is re-charmed every expiry, allies won't hit it, Mind does 0 damage → no winner (seeds 0, 1, 7 with a nearest-enemy probe Tactic). **User decision 2026-10-16:** a side whose living members are all charmed to the other side loses.
2. Slow/Charm/Strip Events drop the source Part (`log_effect` hardcodes actor_part −1).
3. Unbounded ability damage and ElementDef fields → signed overflow and saves that fail to load; bound in data.c, int64 arithmetic.
4. Surviving mutations: minion charm copy, Remains Part list.

**Fixes for review 1** in 1516f51, b5dfd91, d7dff73 (verified by the orchestrator; each new test red when reverted).
