# Power score, Horde generator, balance run

Type: task
Status: resolved
Blocked by: 06, 15, 17

## Question

- Power of a Body: Σ over Parts of weighted stats from `data/power.json`.
- Horde generator: pick Body Plans, fill slots with random Parts of the right kind (no `deity_only`), add monsters until Horde Power is within ±10% of the Pantheon's (1–8 monsters).
- `make balance`: 200 seeded headless Battles per Pantheon, prints win rate, mean length, Power of each side.
- Tests: Power of a known Body, generator stays within ±10% for 50 seeds.

## Answer

Merged `07-power-and-horde` after one review round. `src/sim/power.c` `body_power`: Σ over a Body's Parts of `power.json` weights × integrity, vitality, initiative, vision far, move_speed, Σ resistances, per ability `(damage + heal) × 1000 / cooldown` (0 for cooldown 0) and range, and for spawners `spawn_cap × body_power(spawn plan) × spawning / 100` (`spawning` = 100). Weights bounded 0..1000. `src/sim/horde.c` `generate_horde`: per attempt draw non-spawn plans (role-aware fill), the first always accepted, then add while ≤ 110 % of the Pantheon's Power, stop at 8 or ≥ 90 %; up to 32 attempts, else keep the closest. Horde rows spread over the Arena by Horde size. `make balance` (not in `make test`): 200 Battles per Pantheon. Win rates are uneven by design (user: Pantheon winning more is fine).

## Comments

**Review 1 (opus): CHANGES** — minions barely counted, empty Horde possible, heal uncounted, unbounded weights, untested branches, small Hordes bunched at the top. Fixed in 2774625.
