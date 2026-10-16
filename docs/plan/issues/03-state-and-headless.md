# Battle state, JSON round-trip, headless runner

Type: task
Status: claimed
Blocked by: 02

## Question

The Battle state as plain C structs in `src/sim/`: Arena (tiles, obstacles), Combatants (side, position, Body instance with per-Part Integrity and current Order, Vitality, statuses, Charm), Pickups/Traps on the floor, Remains, Tick counter, seeded RNG state, Battle Log of Events.

- `battle_save_json` / `battle_load_json`: full round-trip, so a loaded state continues identically (test: save at tick N, load, run M ticks, compare with uninterrupted run).
- `battle_step(state)` advances one Tick (stub systems are fine here; later tickets fill them in).
- Core connectivity: a Part works only while a path of intact Parts reaches the Core (test with a two-path graph).
- `headless --seed N [--load state.json] [--ticks T] [--dump out.json]` runs to Battle end or T ticks and prints the winner line.
- Battle setup from seed: pick a Pantheon, place sides at opposite ends, place obstacles (Horde is a fixed placeholder until ticket 07).
