# Battle state, JSON round-trip, headless runner

Type: task
Status: resolved
Blocked by: 02

## Question

The Battle state as plain C structs in `src/sim/`: Arena (tiles, obstacles), Combatants (side, position, Body instance with per-Part Integrity and current Order, Vitality, statuses, Charm), Pickups/Traps on the floor, Remains, Tick counter, seeded RNG state, Battle Log of Events.

- `battle_save_json` / `battle_load_json`: full round-trip, so a loaded state continues identically (test: save at tick N, load, run M ticks, compare with uninterrupted run).
- `battle_step(state)` advances one Tick (stub systems are fine here; later tickets fill them in).
- Core connectivity: a Part works only while a path of intact Parts reaches the Core (test with a two-path graph).
- `headless --seed N [--load state.json] [--ticks T] [--dump out.json]` runs to Battle end or T ticks and prints the winner line.
- Battle setup from seed: pick a Pantheon, place sides at opposite ends, place obstacles (Horde is a fixed placeholder until ticket 07).

## Answer

Merged `03-state-and-headless` after 2 review rounds. `src/sim/battle.{h,c}` + `battle_json.c`: Battle (zero-init before first setup/load; `battle_free`), up to 64 Combatants each with a Body copy + per-slot PartState {integrity, cooldown, Order} + Decision {Stance, hurt_level, vulnerable_element}; positions in int milli-pixels (`*_millipx`), TILE_SIZE 16 px, `move_speed_px_per_second`; splitmix-scrambled xorshift32 RNG in state; heap Battle Log of Events {tick, kind, actor, actor_part, target, target_part, element, amount, detail}; saves reference Parts/Pickups/Traps by id; all-or-nothing hostile-safe JSON load. `battle_step` is a stub (1 px/Tick toward nearest enemy); placeholder Horde fills slots by slot-id match (ticket 17 replaces). `make asan` runs tests + a headless save/load seed sweep under ASan/UBSan.
