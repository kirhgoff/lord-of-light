# Plan: ticket 08 — Pickups and Traps

Repo: /Users/kirill.lastovirya/Projects/lord-of-light (main @ 662d551 + 81335ab claim). Branch `08-pickups-traps`, worktree `~/Projects/lord-of-light-08-pickups-traps`.
Spec: `docs/plan/issues/08-pickups-traps.md`. Vocabulary: `GLOSSARY.md`. Rules: `docs/plan/executor-brief.md` (no comments, small signed commits with pathspecs, `make test` + `make asan` green).

Ticket 06 (Tactics) is built in parallel on another branch. Do NOT touch Orders, Stances, Deciders or `ORDER_PICK_UP`. Pickups are collected by walking over them (section 1.3), so Tactics only need `move_to`.

## 0. What already exists (reuse, do not rebuild)

- `Battle.pickups[] / traps[]` of `FloorObject {def, x_millipx, y_millipx, respawn_in}` with `pickup_count / trap_count`, saved/loaded by id in `src/sim/battle_json.c` (`save_floor`, `load_floor`). Unchanged.
- `ArenaObjectDef` in `src/sim/data.h` + loader `read_arena_object` / `read_arena_object_list` in `src/sim/data.c` (lines 849–895). Unknown JSON keys are ignored by the loader, so old fixtures with `slow_percent` keep loading.
- `perceive` in `src/sim/vision.c` already: hides objects with `respawn_in > 0`, shows Traps only in the Close Band, shows a Pickup in the Far Band only if `def->heal > 0`. **Nothing to do for "Trap visibility only in the Close Band"**; `tests/test_movement.c:test_pickups_and_traps_by_band` already covers it.
- `combatant_hit(battle, data, actor, actor_part, target, element, damage)` in `src/sim/combat.c` is public and already accepts `actor = -1` (DoT source −1, hit Event actor −1, `kill` with actor −1). Traps call it. Element effects (Fire/Venom DoT, Frost slow, Lightning chain, Void strip) come from `data/elements.json` via `apply_element`.
- `EVENT_PICKUP` exists in `EventKind` and `EVENT_IDS` ("pickup") but nothing logs it yet.
- Buff machinery: `Combatant.damage_bonus_percent / damage_bonus_ticks`, ticked down in `battle_tick_effects`, used in `hit_once`, stripped by Void in `apply_strip`.
- `scatter_objects` + `free_tile_column` in `src/sim/battle.c` place one object per def at setup. Replaced by section 1.2.
- `tile_is_solid` (`movement.h`), `rng_below`, `TILE_MILLIPX`, `MILLIPX_PER_PX`, `COMBATANT_RADIUS_MILLIPX`.
- Generators: `tools/gen_sprites.py` (`make_pickup`, `make_trap`, `vial`, `snare`, `generic_trap`, `trap_base`, `Palette`, `ell`, `px_rect`, `flame`, `add_outline`, `WORLD = 24`), `tools/gen_sounds.py` (`strike`, `cast`, `partials`, `oscillator`, `contour`, `noise`, `lowpass`, `highpass`, `envelope`, `swell`, `mix`, `scale`, `apply`, `count`).
- Asset tests `tests/test_assets.c` already check every Pickup/Trap `sprite` atlas and `sound` WAV.

## 1. Settled design

### 1.1 The eleven objects (`data/arena_objects.json`, replaces the 4 placeholders)

Pickups (all instant; `radius` 12 px = centre-to-centre reach; `respawn_ticks` = time until it lies there again):

| id | name | fields | effect |
|---|---|---|---|
| `soma` | Soma | `heal 30`, `respawn_ticks 600` | Vitality += min(30, max − vitality) |
| `menders_moss` | Mender's Moss | `mend 15`, `respawn_ticks 600` | every Part with `0 < integrity < max` gets +min(15, max − integrity); destroyed Parts (0) stay destroyed |
| `ember_seed` | Ember Seed | `damage_bonus_percent 50`, `effect_ticks 200`, `respawn_ticks 900` | `damage_bonus_percent = 50`, `damage_bonus_ticks = 200` (replace, not max) |
| `quickening_leaf` | Quickening Leaf | `speed_bonus_percent 50`, `effect_ticks 200`, `respawn_ticks 900` | `haste_percent = 50`, `haste_ticks = 200`; movement step ×1.5 |
| `eye_of_far_seeing` | Eye of Far Seeing | `vision_bonus_percent 50`, `effect_ticks 400`, `respawn_ticks 900` | `sight_percent = 50`, `sight_ticks = 400`; all three Band ranges ×1.5 |
| `ward_stone` | Ward Stone | `ward_percent 40`, `effect_ticks 200`, `respawn_ticks 900` | `ward_percent = 40`, `ward_ticks = 200`; +40 added to body-wide resistance against every Element (before the ±100 clamp) |

Traps (`radius` 14 px; `respawn_ticks` = re-arm time; the hit is `combatant_hit(battle, data, -1, -1, victim, element, damage)` so the Element effect is whatever `elements.json` says):

| id | name | element | damage | respawn_ticks | effect via 05 code |
|---|---|---|---|---|---|
| `spike_pit` | Spike Pit | kinetic | 15 | 100 | plain hit |
| `frost_rune` | Frost Rune | frost | 8 | 120 | slow 40 % for 60 Ticks |
| `spore_pod` | Spore Pod | venom | 5 | 160 | Venom DoT 1 × 200 Ticks, Internal Part first |
| `lightning_pylon` | Lightning Pylon | lightning | 12 | 200 | chains to the 2 nearest other Combatants within 96 px, any side (section 1.6) |
| `void_rift` | Void Rift | void | 10 | 200 | strips up to 2 buffs (section 1.5) |

Sound ids: `pickup_<id>` and `trap_<id>` (e.g. `pickup_eye_of_far_seeing`, `trap_lightning_pylon`; all ≤ 31 chars, `[a-z0-9_]+`). Sprite ids: `<id>`.

Exact file (commit 1):

```json
{
  "pickups": [
    { "id": "soma", "name": "Soma", "sprite": "soma", "sound": "pickup_soma",
      "heal": 30, "radius": 12, "respawn_ticks": 600 },
    { "id": "menders_moss", "name": "Mender's Moss", "sprite": "menders_moss", "sound": "pickup_menders_moss",
      "mend": 15, "radius": 12, "respawn_ticks": 600 },
    { "id": "ember_seed", "name": "Ember Seed", "sprite": "ember_seed", "sound": "pickup_ember_seed",
      "damage_bonus_percent": 50, "effect_ticks": 200, "radius": 12, "respawn_ticks": 900 },
    { "id": "quickening_leaf", "name": "Quickening Leaf", "sprite": "quickening_leaf", "sound": "pickup_quickening_leaf",
      "speed_bonus_percent": 50, "effect_ticks": 200, "radius": 12, "respawn_ticks": 900 },
    { "id": "eye_of_far_seeing", "name": "Eye of Far Seeing", "sprite": "eye_of_far_seeing", "sound": "pickup_eye_of_far_seeing",
      "vision_bonus_percent": 50, "effect_ticks": 400, "radius": 12, "respawn_ticks": 900 },
    { "id": "ward_stone", "name": "Ward Stone", "sprite": "ward_stone", "sound": "pickup_ward_stone",
      "ward_percent": 40, "effect_ticks": 200, "radius": 12, "respawn_ticks": 900 }
  ],
  "traps": [
    { "id": "spike_pit", "name": "Spike Pit", "sprite": "spike_pit", "sound": "trap_spike_pit",
      "element": "kinetic", "damage": 15, "radius": 14, "respawn_ticks": 100 },
    { "id": "frost_rune", "name": "Frost Rune", "sprite": "frost_rune", "sound": "trap_frost_rune",
      "element": "frost", "damage": 8, "radius": 14, "respawn_ticks": 120 },
    { "id": "spore_pod", "name": "Spore Pod", "sprite": "spore_pod", "sound": "trap_spore_pod",
      "element": "venom", "damage": 5, "radius": 14, "respawn_ticks": 160 },
    { "id": "lightning_pylon", "name": "Lightning Pylon", "sprite": "lightning_pylon", "sound": "trap_lightning_pylon",
      "element": "lightning", "damage": 12, "radius": 14, "respawn_ticks": 200 },
    { "id": "void_rift", "name": "Void Rift", "sprite": "void_rift", "sound": "trap_void_rift",
      "element": "void", "damage": 10, "radius": 14, "respawn_ticks": 200 }
  ]
}
```
(Pretty-print with 2-space indent, one key per line, like the current file.)

### 1.2 Placement at setup (`battle_place_floor_objects`, new `src/sim/floor.c`)

One object per def (6 Pickups, 5 Traps), in def order, Pickups first. For each:
- Draw `row = rng_below(&battle->rng, ARENA_HEIGHT_TILES)`, `column = FLOOR_MARGIN_TILES + rng_below(&battle->rng, ARENA_WIDTH_TILES - 2 * FLOOR_MARGIN_TILES)` with `FLOOR_MARGIN_TILES 8` (columns 8..39; Deities spawn at column 2, Horde at column 46, obstacles at 6..41). Redraw until the 3×3 tile block around it has no obstacle and is inside the Arena (`tile_is_solid` is true out of bounds, so edge rows are rejected by the same test).
- Up to `FLOOR_PLACEMENT_TRIES 64` such draws, stop at the first one at Chebyshev tile distance ≥ `FLOOR_SPACING_TILES 3` from every object already placed (both lists); if none qualifies, keep the last clear draw.
- Position = tile centre: `column * TILE_MILLIPX + TILE_MILLIPX / 2`, same for y. `respawn_in = 0`.

RNG draws happen only through `rng_below`, so a seed still gives one Battle (`test_setup_is_seeded` stays true). The existing 12 obstacles are placed before this, as now.

### 1.3 Pick-up rule: walk over, consumed only if it does something

Decision: no `pick_up` Order. Every Tick, after movement, for each Pickup lying there (`respawn_in == 0`), the first alive Combatant (lowest index) whose centre is within `def->radius` px of the Pickup takes it — if the effect amount is non-zero. Soma on a full-Vitality Combatant and Moss on an undamaged Body do nothing and the Pickup stays (amount 0, no Event, no respawn). Buffs always apply (amount = the percent). Charm and side are irrelevant: anyone can take anything.

Event: `{kind=EVENT_PICKUP, actor=combatant, actor_part=-1, target=-1, target_part=-1, element=-1, amount=effect amount, detail=pickup index into battle->pickups}`. Then `respawn_in = def->respawn_ticks`.

Effect amount = `heal gain + mend total + damage_bonus_percent + speed_bonus_percent + vision_bonus_percent + ward_percent` (sum; for the shipped data exactly one term is non-zero per Pickup).

### 1.4 Trap trigger rule

Every Tick, after Pickups, for each armed Trap (`respawn_in == 0`): the first alive Combatant whose centre is within `def->radius` px springs it. Log `{kind=EVENT_TRAP, actor=-1, actor_part=-1, target=victim, target_part=-1, element=def->element, amount=def->damage, detail=trap index}`, set `respawn_in = def->respawn_ticks`, then `combatant_hit(battle, data, -1, -1, victim, def->element, def->damage)` (which logs the `hit`, `effect` and `death` Events as in 05, with actor −1). One victim per arming; the Lightning chain is how the Pylon reaches a crowd.

`EVENT_TRAP` is appended to `EventKind` right before `EVENT_KIND_COUNT`; `EVENT_IDS` in `battle_json.c` gets `"trap"` appended. (`check_rejected(json, "\"kind\":\"hit\"", "\"kind\":7")` in test_battle.c still holds: ids are 0..6.)

### 1.5 New buffs and the Void strip rule

`Combatant` gets, right after `damage_bonus_ticks`:
```c
    int haste_percent;
    int haste_ticks;
    int sight_percent;
    int sight_ticks;
    int ward_percent;
    int ward_ticks;
```
Timers tick down in `battle_tick_effects` exactly like `damage_bonus_ticks` (percent reset to 0 when the timer reaches 0). Void strips buffs in the fixed order damage bonus → haste → sight → ward, at most `buff_strip_count` (2) of those active, Event amount = number stripped (existing Void test with only the damage bonus active still sees amount 1).

Hooks (one or two lines each):
- `movement.c` `battle_move_combatants`: after the slow line, `if (me->haste_ticks > 0) step = (int)((int64_t)step * (100 + me->haste_percent) / 100);`
- `vision.c` `combatant_vision_ranges`: after the Part loop, if `sight_ticks > 0` scale all three ranges by `(100 + sight_percent) / 100` in `int64_t`.
- `combat.c` `combatant_resistance_percent`: `if (c->ward_ticks > 0) total += c->ward_percent;` before the clamp. (Applies to Radiance too; the ward is not a Part.)

### 1.6 Lightning from a Trap chains regardless of side

`chain_lightning` currently needs `actor >= 0` for the side filter. Change `nearest_chain_target`'s `Side side` parameter to `int side`, compare `(int)candidate->charm_side == side`, and in `chain_lightning` compute `int side = actor >= 0 ? (int)battle->combatants[actor].charm_side : -1;`. In `apply_element` drop `&& actor >= 0` from the chain condition. Part/Order lightning is unchanged (actor ≥ 0).

### 1.7 Tick order

```c
void battle_step(Battle *battle, const GameData *data)
{
    battle_act(battle, data);
    battle_tick_effects(battle, data);
    battle_move_combatants(battle, data);
    battle_tick_floor(battle, data);
    battle->tick++;
}
```
`battle_tick_floor` first decrements every `respawn_in > 0` (both lists), then takes Pickups, then springs Traps. So an object consumed in Tick t has `respawn_in == respawn_ticks` right after that step and `respawn_ticks - 1` after the next; it is back (and visible) after `respawn_ticks` further steps.

### 1.8 Data schema changes

`ArenaObjectDef`: remove `slow_percent` (Trap effects now come from the Element); add `int mend; int speed_bonus_percent; int vision_bonus_percent; int ward_percent;`. Loader: read the four with `get_count(..., false)` and remove the `slow_percent` line; add a `check_arena_object_ranges` after reading (via `check_range`):
- `damage, heal, mend, radius, effect_ticks, respawn_ticks` in `[0, ABILITY_AMOUNT_LIMIT]`
- `damage_bonus_percent, speed_bonus_percent, vision_bonus_percent, ward_percent` in `[0, 1000]` (matches the save-file range for `damage_bonus_percent`).

Add to `data.h`/`data.c` (next to `find_part`):
```c
int find_pickup(const GameData *data, const char *id);
int find_trap(const GameData *data, const char *id);
```
(linear search by id, −1 if absent). `battle_json.c` keeps its own static `find_object`; don't refactor it.

### 1.9 Save/load

`battle_json.c`: save and load the six new Combatant fields next to `damage_bonus_*` (`*_percent` in `[0, 1000]`, `*_ticks` in `[0, 1000000]`); `EVENT_IDS` += `"trap"`. Old saves no longer load (missing keys); that is the 05 precedent.

## 2. Commits (in order; each leaves `make test` green)

### Commit 1 — "Replace the placeholder Pickups and Traps with the eleven ticket objects"

Files: `data/arena_objects.json`, `src/sim/data.h`, `src/sim/data.c`, `tools/gen_sprites.py`, `tools/gen_sounds.py`, `assets/pickups/*`, `assets/traps/*`, `assets/sounds/*`, `assets/contact_sheet.png`, `tests/test_data.c`, `tests/test_battle.c`, `tests/fixtures/arena_objects_huge_percent.json`.

1. `data/arena_objects.json` = section 1.1.
2. `src/sim/data.h`: `ArenaObjectDef` per 1.8; declare `find_pickup`, `find_trap`.
3. `src/sim/data.c`:
   - `read_arena_object`: replace the `slow_percent` line with `mend`, `speed_bonus_percent`, `vision_bonus_percent`, `ward_percent` (`get_count`, not required); append `&& check_arena_object_ranges(ctx, path, def)`:
     ```c
     static bool check_arena_object_ranges(Context *ctx, const char *path, const ArenaObjectDef *def)
     {
         return check_range(ctx, path, "damage", def->damage, 0, ABILITY_AMOUNT_LIMIT)
             && check_range(ctx, path, "heal", def->heal, 0, ABILITY_AMOUNT_LIMIT)
             && check_range(ctx, path, "mend", def->mend, 0, ABILITY_AMOUNT_LIMIT)
             && check_range(ctx, path, "radius", def->radius, 0, ABILITY_AMOUNT_LIMIT)
             && check_range(ctx, path, "effect_ticks", def->effect_ticks, 0, ABILITY_AMOUNT_LIMIT)
             && check_range(ctx, path, "respawn_ticks", def->respawn_ticks, 0, ABILITY_AMOUNT_LIMIT)
             && check_range(ctx, path, "damage_bonus_percent", def->damage_bonus_percent, 0, 1000)
             && check_range(ctx, path, "speed_bonus_percent", def->speed_bonus_percent, 0, 1000)
             && check_range(ctx, path, "vision_bonus_percent", def->vision_bonus_percent, 0, 1000)
             && check_range(ctx, path, "ward_percent", def->ward_percent, 0, 1000);
     }
     ```
   - `find_pickup` / `find_trap` (loop + `strcmp`, return −1).
4. `tools/gen_sprites.py` (keep `vial`, `trap_base`, `snare`, `generic_trap`; delete `vent` and `PICKUP_COLOURS`):
   ```python
   def blank():
       img = Image.new("RGBA", (WORLD, WORLD), (0, 0, 0, 0))
       return img, ImageDraw.Draw(img)

   def soma(bob):
       return vial((214, 160, 60), bob)

   def ember_seed(bob):
       return vial((190, 98, 52), bob, rage=True)

   def menders_moss(bob):
       img, g = blank()
       y = 2 + bob
       g.ellipse([4, y + 9, 19, y + 18], fill=(70, 110, 50, 255))
       g.ellipse([7, y + 6, 16, y + 14], fill=(112, 152, 82, 255))
       g.ellipse([9, y + 4, 14, y + 9], fill=(150, 190, 100, 255))
       return add_outline(img)

   def quickening_leaf(bob):
       img, g = blank()
       y = 2 + bob
       g.polygon([(12, y + 2), (19, y + 8), (12, y + 18), (5, y + 8)], fill=(96, 170, 70, 255))
       g.line([(12, y + 3), (12, y + 17)], fill=(60, 120, 40, 255))
       return add_outline(img)

   def eye_of_far_seeing(bob):
       img, g = blank()
       y = 2 + bob
       g.ellipse([3, y + 6, 20, y + 15], fill=(240, 236, 220, 255))
       g.ellipse([8, y + 6, 15, y + 15], fill=(152, 112, 164, 255))
       g.ellipse([10, y + 8, 13, y + 12], fill=OUTLINE)
       return add_outline(img)

   def ward_stone(bob):
       img, g = blank()
       y = 2 + bob
       g.rounded_rectangle([5, y + 4, 18, y + 18], radius=4, fill=(120, 124, 132, 255))
       g.line([(12, y + 7), (12, y + 15)], fill=(238, 226, 206, 255))
       g.line([(9, y + 11), (15, y + 11)], fill=(238, 226, 206, 255))
       return add_outline(img)

   PICKUP_DRAWERS = {
       "soma": soma, "menders_moss": menders_moss, "ember_seed": ember_seed,
       "quickening_leaf": quickening_leaf, "eye_of_far_seeing": eye_of_far_seeing, "ward_stone": ward_stone,
   }

   def spike_pit(element, armed):
       p = Palette(element)
       img, g = blank()
       trap_base(g, p)
       tips = [(7, 13), (12, 11), (17, 13)] if armed else [(7, 5), (12, 3), (17, 5)]
       for x, y in tips:
           g.polygon([(x - 2, 16), (x + 2, 16), (x, y)], fill=STEEL + (255,))
       return add_outline(img)

   def spore_pod(element, armed):
       p = Palette(element)
       img, g = blank()
       trap_base(g, p)
       if armed:
           g.ellipse([7, 6, 17, 16], fill=p.base)
           g.ellipse([10, 8, 14, 12], fill=p.glow)
       else:
           for cx, cy in [(6, 6), (12, 4), (18, 6), (9, 10), (15, 10)]:
               ell(g, cx, cy, 2, 2, p.glow)
       return add_outline(img)

   def lightning_pylon(element, armed):
       p = Palette(element)
       img, g = blank()
       trap_base(g, p)
       px_rect(g, 10, 4, 4, 11, STEEL + (255,))
       if armed:
           ell(g, 12, 4, 2, 2, p.glow)
       else:
           g.line([(12, 4), (6, 9), (14, 11), (8, 16)], fill=p.glow)
           g.line([(12, 4), (18, 9), (10, 11), (16, 16)], fill=p.glow)
       return add_outline(img)

   def void_rift(element, armed):
       p = Palette(element)
       img, g = blank()
       g.ellipse([2, 10, 21, 20], fill=p.dark)
       g.ellipse([5, 12, 18, 18], fill=p.deep)
       g.ellipse([8, 13, 15, 17], fill=p.glow if not armed else p.base)
       return add_outline(img)

   TRAP_DRAWERS = {"kinetic": spike_pit, "frost": snare, "venom": spore_pod, "lightning": lightning_pylon, "void": void_rift}
   ```
   `make_pickup`: `drawer = PICKUP_DRAWERS[obj["id"]]; images = [drawer(0), drawer(1)]` (unknown id = loud KeyError, intended). `make_trap` unchanged (`TRAP_DRAWERS.get(obj["element"], generic_trap)`). `ell(g, cx, cy, rx, ry, fill)` and `px_rect(g, x, y, w, h, fill)` exist. Keep all art within x,y ∈ [2, 21] so `check_clear_of_edges` passes.
5. `tools/gen_sounds.py`: rename `pickup_health` → `pickup_soma`, `pickup_rage` → `pickup_ember_seed`, `trap_frost_snare` → `trap_frost_rune` (bodies unchanged); delete `trap_fire_vent`; add:
   ```python
   def pickup_menders_moss(rng):
       n = count(0.3)
       return apply(partials(contour(n, 523, 659), [(1, 1.0), (2, 0.4), (3, 0.15)], 8), envelope(n, 0.01, 0.12))

   def pickup_quickening_leaf(rng):
       n = count(0.2)
       return apply(oscillator(contour(n, 600, 1800)), envelope(n, 0.005, 0.06))

   def pickup_eye_of_far_seeing(rng):
       n = count(0.4)
       wobble = oscillator([880 * (1 + 0.05 * math.sin(TAU * 6 * i / RATE)) for i in range(n)])
       return apply(wobble, swell(n))

   def pickup_ward_stone(rng):
       n = count(0.45)
       return apply(partials(contour(n, 220, 220), [(1, 1.0), (2.76, 0.5), (5.4, 0.25)], 6), envelope(n, 0.005, 0.15))

   def trap_spike_pit(rng):
       n = count(0.25)
       snap = apply(lowpass(noise(rng, n), 900), envelope(n, 0.001, 0.03))
       return mix(snap, strike("kinetic", rng, 1.0, 0.25))

   def trap_spore_pod(rng):
       n = count(0.4)
       puff = apply(lowpass(noise(rng, n), 1200), envelope(n, 0.02, 0.12))
       return mix(puff, scale(strike("venom", rng, 1.0, 0.4), 0.6))

   def trap_lightning_pylon(rng):
       return mix(strike("lightning", rng, 1.0, 0.3), scale(cast("lightning", rng), 0.5))

   def trap_void_rift(rng):
       n = count(0.5)
       drone = apply(oscillator(contour(n, 70, 35)), swell(n))
       return mix(strike("void", rng, 1.0, 0.5), scale(drone, 0.7))
   ```
   Register all eleven in `build_sounds()` under their `pickup_<id>` / `trap_<id>` keys, removing the four old keys. All are ≤ 0.5 s (asset test caps at 0.6 s).
6. Regenerate: `git rm assets/pickups/health_vial.* assets/pickups/rage_draught.* assets/traps/fire_vent.* assets/traps/frost_snare.* assets/sounds/pickup_health.wav assets/sounds/pickup_rage.wav assets/sounds/trap_fire_vent.wav assets/sounds/trap_frost_snare.wav`, then `uv run --with pillow==12.3.0 python tools/gen_sprites.py` and `python3 tools/gen_sounds.py`. Commit the new `assets/pickups/*`, `assets/traps/*`, the eleven new WAVs and `assets/contact_sheet.png`. `git status` must show no other asset changed (generators are deterministic); if a Part sprite changed, stop and report.
7. Tests touched by the id change:
   - `tests/test_data.c:68`: `CHECK(data.pickup_count == 6 && data.trap_count == 5);`
   - `tests/test_battle.c:221`: `"\"def\":\"health_vial\""` → `"\"def\":\"soma\""`; lines 255–256: `"soma"` and `"spike_pit"`.
   - New fixture `tests/fixtures/arena_objects_huge_percent.json`: copy of `data/arena_objects.json` with `ward_percent: 1001`; add to the error table in `test_negative_values_and_empty_parts` (or a sibling check): expected `"pickups[5].ward_percent: must be between 0 and 1000"`.
8. `make test` green (asset tests now cover the new files).

### Commit 2 — "Place, take and spring Pickups and Traps each Tick"

Files: `src/sim/floor.h` (new), `src/sim/floor.c` (new), `src/sim/battle.h`, `src/sim/battle.c`, `src/sim/combat.c`, `src/sim/movement.c`, `src/sim/vision.c`, `src/sim/battle_json.c`.

1. `battle.h`: `EVENT_TRAP` before `EVENT_KIND_COUNT`; six Combatant fields (1.5).
2. `floor.h`:
   ```c
   #ifndef SIM_FLOOR_H
   #define SIM_FLOOR_H

   #include "battle.h"

   #define FLOOR_MARGIN_TILES 8
   #define FLOOR_SPACING_TILES 3
   #define FLOOR_PLACEMENT_TRIES 64

   void battle_place_floor_objects(Battle *battle, const GameData *data);
   void battle_tick_floor(Battle *battle, const GameData *data);

   #endif
   ```
3. `floor.c` (the Makefile wildcard picks it up):
   ```c
   #include "floor.h"

   #include <stdlib.h>

   #include "combat.h"
   #include "movement.h"

   static int64_t squared(int64_t value)
   {
       return value * value;
   }

   static bool tile_area_is_clear(const Battle *battle, int column, int row)
   {
       for (int dr = -1; dr <= 1; dr++)
           for (int dc = -1; dc <= 1; dc++)
               if (tile_is_solid(battle, column + dc, row + dr)) return false;
       return true;
   }

   static bool far_from(const FloorObject *objects, int count, int column, int row)
   {
       for (int i = 0; i < count; i++) {
           int dc = abs(objects[i].x_millipx / TILE_MILLIPX - column), dr = abs(objects[i].y_millipx / TILE_MILLIPX - row);
           if (dc < FLOOR_SPACING_TILES && dr < FLOOR_SPACING_TILES) return false;
       }
       return true;
   }

   static void random_clear_tile(Battle *battle, int *column, int *row)
   {
       do {
           *row = rng_below(&battle->rng, ARENA_HEIGHT_TILES);
           *column = FLOOR_MARGIN_TILES + rng_below(&battle->rng, ARENA_WIDTH_TILES - 2 * FLOOR_MARGIN_TILES);
       } while (!tile_area_is_clear(battle, *column, *row));
   }

   static FloorObject place_one(Battle *battle, int def)
   {
       int column = 0, row = 0;
       for (int attempt = 0; attempt < FLOOR_PLACEMENT_TRIES; attempt++) {
           random_clear_tile(battle, &column, &row);
           if (far_from(battle->pickups, battle->pickup_count, column, row) &&
               far_from(battle->traps, battle->trap_count, column, row))
               break;
       }
       return (FloorObject){def, column * TILE_MILLIPX + TILE_MILLIPX / 2, row * TILE_MILLIPX + TILE_MILLIPX / 2, 0};
   }

   void battle_place_floor_objects(Battle *battle, const GameData *data)
   {
       for (int d = 0; d < data->pickup_count; d++) {
           FloorObject placed = place_one(battle, d);
           battle->pickups[battle->pickup_count++] = placed;
       }
       for (int d = 0; d < data->trap_count; d++) {
           FloorObject placed = place_one(battle, d);
           battle->traps[battle->trap_count++] = placed;
       }
   }
   ```
   (Two statements on purpose: `battle->pickups[battle->pickup_count++] = place_one(...)` has unspecified evaluation order against `place_one` reading `pickup_count`.)
   ```c
   static bool touching(const Combatant *who, const FloorObject *object, int radius_px)
   {
       int64_t reach = (int64_t)radius_px * MILLIPX_PER_PX;
       return squared(who->x_millipx - object->x_millipx) + squared(who->y_millipx - object->y_millipx) <= squared(reach);
   }

   static int heal_vitality(Combatant *who, int heal)
   {
       int gain = who->vitality_max - who->vitality;
       if (gain > heal) gain = heal;
       who->vitality += gain;
       return gain;
   }

   static int mend_parts(Combatant *who, const GameData *data, int mend)
   {
       int total = 0;
       for (int s = 0; s < who->body.slot_count; s++) {
           int max = data->parts[who->body.slots[s].part].integrity;
           int *integrity = &who->parts[s].integrity;
           if (*integrity <= 0 || *integrity >= max) continue;
           int gain = max - *integrity < mend ? max - *integrity : mend;
           *integrity += gain;
           total += gain;
       }
       return total;
   }

   static int buff(int *percent, int *ticks, int value, int duration)
   {
       if (value <= 0) return 0;
       *percent = value;
       *ticks = duration;
       return value;
   }

   static int apply_pickup(Combatant *who, const GameData *data, const ArenaObjectDef *def)
   {
       return heal_vitality(who, def->heal) + mend_parts(who, data, def->mend) +
              buff(&who->damage_bonus_percent, &who->damage_bonus_ticks, def->damage_bonus_percent, def->effect_ticks) +
              buff(&who->haste_percent, &who->haste_ticks, def->speed_bonus_percent, def->effect_ticks) +
              buff(&who->sight_percent, &who->sight_ticks, def->vision_bonus_percent, def->effect_ticks) +
              buff(&who->ward_percent, &who->ward_ticks, def->ward_percent, def->effect_ticks);
   }

   static void take_pickups(Battle *battle, const GameData *data)
   {
       for (int p = 0; p < battle->pickup_count; p++) {
           FloorObject *object = &battle->pickups[p];
           const ArenaObjectDef *def = &data->pickups[object->def];
           if (object->respawn_in > 0) continue;
           for (int c = 0; c < battle->combatant_count; c++) {
               Combatant *who = &battle->combatants[c];
               if (!who->alive || !touching(who, object, def->radius)) continue;
               int amount = apply_pickup(who, data, def);
               if (amount == 0) continue;
               battle_log_event(battle, (Event){0, EVENT_PICKUP, c, -1, -1, -1, -1, amount, p});
               object->respawn_in = def->respawn_ticks;
               break;
           }
       }
   }

   static void spring_traps(Battle *battle, const GameData *data)
   {
       for (int t = 0; t < battle->trap_count; t++) {
           FloorObject *object = &battle->traps[t];
           const ArenaObjectDef *def = &data->traps[object->def];
           if (object->respawn_in > 0) continue;
           for (int c = 0; c < battle->combatant_count; c++) {
               if (!battle->combatants[c].alive || !touching(&battle->combatants[c], object, def->radius)) continue;
               battle_log_event(battle, (Event){0, EVENT_TRAP, -1, -1, c, -1, def->element, def->damage, t});
               object->respawn_in = def->respawn_ticks;
               combatant_hit(battle, data, -1, -1, c, def->element, def->damage);
               break;
           }
       }
   }

   static void count_down(FloorObject *objects, int count)
   {
       for (int i = 0; i < count; i++)
           if (objects[i].respawn_in > 0) objects[i].respawn_in--;
   }

   void battle_tick_floor(Battle *battle, const GameData *data)
   {
       count_down(battle->pickups, battle->pickup_count);
       count_down(battle->traps, battle->trap_count);
       take_pickups(battle, data);
       spring_traps(battle, data);
   }
   ```
4. `battle.c`: `#include "floor.h"`; delete `free_tile_column` and `scatter_objects`; in `battle_setup` replace the two `scatter_objects` calls with `battle_place_floor_objects(battle, data);`; `battle_step` per 1.7.
5. `combat.c`:
   - `combatant_resistance_percent`: ward line (1.5).
   - Replace `apply_strip` with `strip_buffs` + `apply_strip`:
     ```c
     static int strip_buffs(Combatant *victim, int count)
     {
         int *ticks[] = {&victim->damage_bonus_ticks, &victim->haste_ticks, &victim->sight_ticks, &victim->ward_ticks};
         int *percent[] = {&victim->damage_bonus_percent, &victim->haste_percent, &victim->sight_percent, &victim->ward_percent};
         int stripped = 0;
         for (int b = 0; b < 4 && stripped < count; b++) {
             if (*ticks[b] == 0) continue;
             *ticks[b] = 0;
             *percent[b] = 0;
             stripped++;
         }
         return stripped;
     }

     static void apply_strip(Battle *battle, Combatant *victim, const ElementDef *def, int actor, int actor_part, int target, Element element)
     {
         int stripped = strip_buffs(victim, def->buff_strip_count);
         if (stripped == 0) return;
         log_effect(battle, actor, actor_part, target, element, stripped, EFFECT_STRIP);
     }
     ```
   - Chain side per 1.6.
   - `battle_tick_effects`: after the damage-bonus line add the same line for `haste`, `sight`, `ward`.
6. `movement.c`, `vision.c`: hooks per 1.5.
7. `battle_json.c`: `EVENT_IDS` += `"trap"`; `save_combatant` adds the six numbers after `damage_bonus_ticks`; `load_combatant` adds six `read_int` calls (`*_percent` 0..1000, `*_ticks` 0..1000000).
8. `make test`, `make asan` green (asan runs seeds 0–9 through setup + 600 Ticks; placement must terminate — it does, the clear-tile loop has ~700 candidates against 12 obstacles).

### Commit 3 — "Test every Pickup and Trap, placement, respawn and saves"

Files: `tests/test_floor.c` (new), `tests/test_main.c` (declare + call `run_floor_tests` after `run_combat_tests`), `tests/test_battle.c`.

Use the `tests/fixtures/battle_elements.json` fixture (no Pickups/Traps; Combatants: 0 Striker pantheon at (168000,176000), 1 Dummy horde at (200000,176000) with parts torso/heart/ganglion/armour_plate/legs/shield_hand, 2 Second horde at (264000,176000) torso+heart 40/40, 3 Far horde at (360000,176000), 4 Hurt pantheon at (120000,176000) 10/40). Copy `read_text_file`, `load_fixture`, `count_events`, `last_event`, `run` from `tests/test_combat.c` (static, file-local; no shared header). Helpers:
```c
static int put_pickup(const char *id, int at)   /* places the Pickup on Combatant `at`, returns its index */
{
    const Combatant *who = &battle.combatants[at];
    battle.pickups[battle.pickup_count] = (FloorObject){find_pickup(&data, id), who->x_millipx, who->y_millipx, 0};
    return battle.pickup_count++;
}
static int put_trap(const char *id, int at)     /* same for Traps */
```
Tests (one per object, then the rules):
1. `test_soma_heals_vitality_and_respawns`: `put_pickup("soma", HURT)`; `run(1)`; Hurt vitality 40; last `EVENT_PICKUP` has actor HURT, amount 30, detail 0; `respawn_in == 600`. Also: `put_pickup("soma", DUMMY)` (full Vitality), `run(1)`: `respawn_in` of that Pickup still 0 and `count_events(EVENT_PICKUP) == 1`.
2. `test_menders_moss_mends_damaged_parts_not_destroyed_ones`: Dummy `armour_plate` integrity 30 (max 50), `shield_hand` 0; put Moss on Dummy; `run(1)`; armour 45, shield 0; Event amount 15.
3. `test_ember_seed_grants_damage_bonus`: on Dummy; `run(1)`; `damage_bonus_percent == 50 && damage_bonus_ticks == 200`; `run(1)`; ticks 199.
4. `test_quickening_leaf_speeds_movement`: Dummy's `legs` give 60 px/s = 3000 millipx per Tick. `put_pickup("quickening_leaf", DUMMY)`; `run(1)` (takes the Leaf: `haste_percent == 50 && haste_ticks == 200`); then Dummy core Order `move_to` (200000, 480000) and `run(10)`: `y_millipx == 176000 + 45000` (4500 per Tick; nothing lies below the Dummy). Control in the same test after a fresh `load_fixture()`: same Order without the Leaf, `run(10)` → `176000 + 30000`.
5. `test_eye_of_far_seeing_extends_vision`: no fixture Combatant has a vision Part, so use real setup: `battle_setup(&battle, &data, 3)`; Combatant 0 is a Deity with a head. `int r0[3]; combatant_vision_ranges(&battle.combatants[0], &data, r0);` (`r0[BAND_CLOSE] > 0`); `put_pickup("eye_of_far_seeing", 0)`; `run(1)`; `combatant_vision_ranges` again → each `r[b] == r0[b] * 150 / 100`; `sight_ticks == 400`. `battle_free(&battle)` at the end (setup allocates the log).
6. `test_ward_stone_raises_resistance`: `combatant_resistance_percent(&Second, &data, ELEMENT_KINETIC, false)` is 0 before; put Ward Stone on Second; `run(1)`; now 40; `run(200)`; back to 0 (`ward_ticks` expired).
7. `test_spike_pit_hits_the_first_combatant_on_it`: `put_trap("spike_pit", SECOND)`; `run(1)`; `EVENT_TRAP` with target SECOND, element kinetic, amount 15, detail 0; last `EVENT_HIT` actor −1, actor_part −1, amount 15; Second vitality 33 (40 − 15·50 %); trap `respawn_in == 100`; `run(1)` → no second hit, `respawn_in == 99`.
8. `test_frost_rune_slows`: on Second; `run(1)`; `slow_percent == 40 && slow_ticks == 60`; an `EVENT_EFFECT` with detail `EFFECT_SLOW` and actor −1.
9. `test_spore_pod_poisons_internal_parts_first`: on Second; `run(1)`; `dot_element == ELEMENT_VENOM && dot_ticks == 200 && dot_source == -1`; last hit `target_part == find_body_slot(&Second.body, "heart")`.
10. `test_lightning_pylon_chains_to_anyone_nearby`: on Second; `run(1)`; `count_events(EVENT_HIT) == 3` with targets SECOND (12), DUMMY (nearest, 64 px), STRIKER (nearest to Dummy, 32 px) — sides differ, so the chain ignored sides; amounts 12, then 6 and 3 scaled by each target's lightning resistance (assert targets only, not amounts).
11. `test_void_rift_strips_two_buffs_in_order`: Second gets `damage_bonus_* = 50/100`, `haste_* = 50/100`, `sight_* = 50/100`; put Void Rift on Second; `run(1)`; damage and haste are 0/0, sight still 50/99 (ticked once by `battle_tick_effects` before the floor phase); last `EVENT_EFFECT` is `EFFECT_STRIP` with amount 2.
12. `test_setup_scatters_objects_apart_from_obstacles_and_each_other`: `battle_setup(&battle, &data, 3)` (real data): `pickup_count == 6 && trap_count == 5`; for every object: column in [8, 40), 3×3 tile block obstacle-free (`battle.obstacle`), and every pair (both lists) at Chebyshev tile distance ≥ 3. Repeat for seeds 0..9 in a loop.
13. `test_taken_pickup_returns_after_respawn_ticks`: Soma on Hurt, `run(1)`; set Hurt vitality back to 10; `run(599)` → `respawn_in == 1` and vitality still 10; `run(1)` → `respawn_in == 0`, and `run(1)` → vitality 40 again, `count_events(EVENT_PICKUP) == 2`.
14. `test_buffs_and_trap_events_survive_save_and_load` (in `tests/test_battle.c` or here): set the six fields on a Combatant, log an `EVENT_TRAP`, `battle_save_json` → `battle_load_json` → fields equal, `log[n].kind == EVENT_TRAP`, and `check_rejected(json, "\"haste_percent\":0", "\"haste_percent\":1001")` in `test_load_rejects_bad_input`.

`make test` and `make asan` green; the hand-back lists both exit codes.

## 3. Out of scope / left as is

- `ORDER_PICK_UP` stays an unused Order kind (ticket 06 maps `collect` to `move_to`; removing the enum value is a one-line follow-up if 06 agrees).
- Far-Band visibility still keys on `heal > 0` only (Soma). Mender's Moss is not a "healing Pickup" for the Reflex's `collect` rule (Vitality-based).
- No AoE for Traps beyond Lightning's own chain; no Trap damage to Remains; no per-side Traps.
- `slow_percent` on ArenaObjectDef is deleted; the old `effect_ticks` on Traps is no longer read (unknown keys are ignored).
- View: nothing (ticket 10 draws `respawn_in == 0` objects and plays `sound` on `EVENT_PICKUP` / `EVENT_TRAP`, detail = object index).

### Critical files for implementation
- /Users/kirill.lastovirya/Projects/lord-of-light/src/sim/floor.c (new; placement, take, spring)
- /Users/kirill.lastovirya/Projects/lord-of-light/src/sim/combat.c (ward resistance, strip order, side-less chain, timers)
- /Users/kirill.lastovirya/Projects/lord-of-light/src/sim/battle.h (Combatant buff fields, EVENT_TRAP)
- /Users/kirill.lastovirya/Projects/lord-of-light/data/arena_objects.json + src/sim/data.c (eleven objects, new def fields, ranges, find_pickup/find_trap)
- /Users/kirill.lastovirya/Projects/lord-of-light/tools/gen_sprites.py + tools/gen_sounds.py (drawers and sound recipes for every object)
