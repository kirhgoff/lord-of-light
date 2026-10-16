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
