# Power score, Horde generator, balance run

Type: task
Status: open
Blocked by: 06, 15, 17

## Question

- Power of a Body: Σ over Parts of weighted stats from `data/power.json`.
- Horde generator: pick Body Plans, fill slots with random Parts of the right kind (no `deity_only`), add monsters until Horde Power is within ±10% of the Pantheon's (1–8 monsters).
- `make balance`: 200 seeded headless Battles per Pantheon, prints win rate, mean length, Power of each side.
- Tests: Power of a known Body, generator stays within ±10% for 50 seeds.
