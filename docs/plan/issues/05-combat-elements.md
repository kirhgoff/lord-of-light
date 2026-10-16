# Orders, combat, Elements, death

Type: task
Status: open
Blocked by: 04

## Question

Execute the 8 Order kinds per Part each Tick (strike, cast, block, move_to, pick_up, support, spawn, idle) with ranges and cooldowns from Part abilities.

- Damage hits one target Part (chosen from what the attacker's reach can touch, external first unless the Element says otherwise), scaled by the target's resistances; also takes a share off Vitality. Destroyed Parts and their cut-off Parts stop working.
- Element effects: Fire DoT, Frost slow/root, Lightning chain, Radiance ignores armour Parts, Venom hits Internal Parts first, Mind applies Charm, Void strips buffs, Kinetic plain.
- Spawning creates minion Combatants on the spawner's side up to the cap.
- Death when Vitality ≤ 0 or all Hearts destroyed → Remains. Battle ends when a side has no living Combatants.
- Every hit, effect, death and spawn appends an Event (with source Combatant, Part, Element, amount, Tick).
- Each Element and each death rule has a test from a hand-written JSON state.
