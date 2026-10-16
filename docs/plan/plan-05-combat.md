# Plan: ticket 05 — Orders, combat, Elements, death

Repo: /Users/kirill.lastovirya/Projects/lord-of-light (main @ 2538ed0). Branch `05-combat-elements`, worktree `~/Projects/lord-of-light-05-combat-elements`.
Spec: `docs/plan/issues/05-combat-elements.md`. Vocabulary: `GLOSSARY.md`. Rules: `docs/plan/executor-brief.md` (no comments, small signed commits with pathspecs, `make test` + `make asan` green).

Everything below is integer arithmetic; the only RNG use is `rng_below(&battle->rng, n)` for the target Part pick. No new files beyond `src/sim/combat.{h,c}`, `tests/test_combat.c`, `tests/fixtures/battle_elements.json`.

## 0. What already exists (reuse, do not rebuild)

- `body_working_parts(body, parts, working)` in `src/sim/battle.c` — BFS from the Core over Parts with integrity > 0. **This is the "cut-off Parts" rule already**: a destroyed Part's dependants are simply not in `working`. The Body is a graph with `edges` + `core`; no parent/anchor relation is needed or added.
- `Combatant.slow_ticks`, `dot_ticks`, `charm_ticks`, `charm_side` already exist and are saved/loaded. `perceive` already uses `charm_side` for both views.
- `PartState {integrity, cooldown, order}` and `Order {kind, target, x_millipx, y_millipx}` exist. `ORDER_MOVE_TO` is already executed by `battle_move_combatants` (`src/sim/movement.c`).
- `Event {tick, kind, actor, actor_part, target, target_part, element, amount, detail}`; `battle_log_event` stamps `tick`.
- `Remains {x, y, part_count, parts[]}`; `MAX_REMAINS == MAX_COMBATANTS == 64`.
- Loader guarantees: spawn plans have every slot fixed (`check_spawn_slots_fixed`), `spawn_cap >= 1`, `spawn_plan` resolved to a Body Plan index, resistances in [-100, 100], abilities have `order`, `element`, `damage`, `heal`, `range` (px), `cooldown_ticks`, `area`.
- `ElementDef` in `data.h` already carries `dot_damage, dot_ticks, slow_percent, slow_ticks, chain_targets, chain_range, pierce_targets, internal_first, charm_ticks, buff_strip_count` from `data/elements.json`.
- `COMBATANT_RADIUS_MILLIPX` (11 px), `line_of_sight` (`vision.h`), `TICKS_PER_SECOND` (`movement.h`).

## 1. Settled design

### 1.1 Tick order (`battle_step`)

```c
void battle_step(Battle *battle, const GameData *data)
{
    battle_act(battle, data);           /* Orders, in Combatant index order, then slot order */
    battle_tick_effects(battle, data);  /* DoT, slow, charm, damage bonus timers */
    battle_move_combatants(battle, data);
    battle->tick++;
}
```

Initiative is NOT used here (it schedules Decisions in ticket 06). Execution order is Combatant index 0..count-1, within a Combatant slot index 0..slot_count-1. A Combatant appended by a spawn this Tick is also visited this Tick (its Orders are idle, so nothing happens); this is fine and deterministic.

### 1.2 Cooldowns and abilities

- At the start of `battle_act`, for every alive Combatant and every slot (working or not): `if (cooldown > 0) cooldown--`.
- A Part acts only if alive, `working[s]`, `cooldown == 0`, `order.kind != ORDER_IDLE`, and the Part has an ability with `ability.order == order.kind` (first match in `abilities[]`). Otherwise the Order is left in place and nothing happens.
- A Part that successfully acts sets `cooldown = ability.cooldown_ticks`. So a Part with cooldown 12 acts on Ticks 0, 12, 24… A failed attempt (out of reach, no LOS, bad target) sets no cooldown.
- Reach: `in_reach(a, b, range_px)` ⇔ `dx*dx + dy*dy <= (range_px * MILLIPX_PER_PX + 2 * COMBATANT_RADIUS_MILLIPX)^2` using `int64_t` (centre-to-centre distance minus both feet radii; needed because touching Combatants sit 22 px apart and claws reach 20 px).

### 1.3 The eight Order kinds (what each does in 05)

| Kind | Effect now |
|---|---|
| `strike` | Needs `order.target` in `[0, combatant_count)`, alive, `target.charm_side != me.charm_side`, in reach of the ability's range. Then `combatant_hit(...)` with the ability's Element and damage. No LOS check (melee). |
| `cast` | Same as strike plus `line_of_sight(battle, me.x, me.y, target.x, target.y)` must be true. `area` is ignored in 05 (not in the ticket; add when Tactics want AoE). |
| `block` | No action, no cooldown. While a working Part with a `block` Order and a `block` ability exists, incoming strike/cast damage to this Combatant is halved (see 1.4). |
| `move_to` | Already done by movement.c. Only change: Frost slow (1.6). |
| `pick_up` | No-op. Ticket 08 owns pickups. |
| `support` | Needs target alive, `target.charm_side == me.charm_side` (self allowed), in reach. `target.vitality = min(vitality + ability.heal, vitality_max)`; Event `effect`/`EFFECT_HEAL` with `amount` = actual gain; cooldown. Skip entirely (no cooldown) when the gain would be 0. |
| `spawn` | See 1.8. |
| `idle` | Nothing. |

### 1.4 Hit resolution — `combatant_hit`

```c
void combatant_hit(Battle *battle, const GameData *data, int actor, int actor_part, int target, Element element, int damage);
```
Public in `combat.h` because Lightning chain and ticket 08's Traps (actor = -1, actor_part = -1) reuse it.

Steps, in order:
1. `working` = `body_working_parts(target)`. If target not alive, return.
2. **Resistance** `r = combatant_resistance_percent(target, data, element, /*internal_only=*/ element == ELEMENT_RADIANCE)`: sum of `resistance_percent[element]` over working Parts (all Parts, or only `data->parts[...].internal` ones for Radiance), clamped to [-100, 100]. Radiance thus ignores armour Parts (external Parts with resistance: `armour_plate`, `carapace`, `gel_mass`, `wisp_core`) but internal glands still protect.
3. **Attacker bonus**: if `actor >= 0 && attacker.damage_bonus_ticks > 0`: `damage = damage * (100 + attacker.damage_bonus_percent) / 100`.
4. `damage = damage * (100 - r) / 100`.
5. **Block**: if the target has a working slot whose `order.kind == ORDER_BLOCK` and whose Part has a `block` ability: `damage = damage * BLOCK_PERCENT / 100` (`BLOCK_PERCENT 50`). Not applied to DoT.
6. **Target Part**: candidates = working slots filtered by kind: if `data->elements[element].internal_first` (Venom) prefer `slot.internal`, else prefer `!slot.internal`; if the preferred set is empty use the other set. Pick `candidates[rng_below(&battle->rng, count)]`. (The Core is external, so it is always a candidate for non-Venom hits; internal Parts are reached only by Venom or by destroying the Core.)
7. `parts[p].integrity = max(0, integrity - damage)`; `vitality -= damage * VITALITY_SHARE_PERCENT / 100` (`VITALITY_SHARE_PERCENT 50`, floor; may go below 0 — leave it, loader accepts 0 only, so clamp at 0: `if (vitality < 0) vitality = 0`).
8. Event `hit`: `{kind=EVENT_HIT, actor, actor_part, target, target_part=p, element, amount=damage (after 3–5), detail=parts[p].integrity after the hit}` (detail 0 ⇔ Part destroyed; Vitality loss is `amount * 50 / 100`, derivable). Log the hit even when damage is 0 (Mind casts).
9. **Element effect** (1.6) — only when called from an Order (pass a `bool primary`; chain hops and Trap calls pass false. Simplest: a static `hit_once(...)` does 1–8 + death check, and the public `combatant_hit` = `hit_once` + element effect).
10. **Death check** (1.7).

### 1.5 Element table (numbers from `data/elements.json`, all already loaded)

| Element | Effect after the hit (primary hits only) | Resistance scaling |
|---|---|---|
| Kinetic | none | damage |
| Fire | `dot_element = FIRE, dot_damage = 2, dot_ticks = scaled(60), dot_source = actor` (latest DoT replaces any earlier one) | damage, dot_ticks |
| Frost | `slow_percent = max(slow_percent, 40)`, `slow_ticks = max(slow_ticks, scaled(60))`; Event `effect`/`EFFECT_SLOW`, amount = ticks applied | damage, slow_ticks |
| Lightning | chain: up to `chain_targets` (2) further Combatants, each alive, `charm_side != attacker.charm_side`, not yet hit in this chain, within `chain_range` (96 px, reach formula) of the **previous** hit Combatant; nearest first, ties by lower index; hop damage = previous hop's input damage `/ 2`, stop when 0. Each hop is `hit_once` (no further chaining, no effect). Skipped when `actor < 0`. | damage per hop |
| Radiance | none beyond `internal_only` resistance in step 2. `pierce_targets` stays loaded and unused. | internal glands only |
| Venom | internal-first targeting (step 6) + DoT `dot_damage = 1, dot_ticks = scaled(200)` | damage, dot_ticks |
| Mind | `target.charm_side = attacker.charm_side; target.charm_ticks = scaled(100)`; Event `effect`/`EFFECT_CHARM`, amount = ticks. Skipped if scaled ticks == 0. | charm_ticks |
| Void | if `buff_strip_count > 0 && target.damage_bonus_ticks > 0`: `damage_bonus_ticks = 0; damage_bonus_percent = 0`; Event `effect`/`EFFECT_STRIP`, amount = 1 (one buff kind exists). | damage |

`scaled(ticks) = ticks * (100 - r) / 100` with the same `r` as the damage (so a Mind-proof gland cuts Charm to 25 Ticks). Effects whose scaled value is 0 are not applied and log nothing.

**"Buff" in 05** = the damage bonus pair `damage_bonus_percent/damage_bonus_ticks` on a Combatant. Nothing in 05 grants it (ticket 08's Rage Draught will: `damage_bonus_percent 50, effect_ticks 200` are already in `arena_objects.json`); Void strips it, the hit formula uses it, tests set it from the fixture. No other buff kinds.

### 1.6 Effects stage — `battle_tick_effects`

For every alive Combatant, in index order:
```
if (damage_bonus_ticks > 0 && --damage_bonus_ticks == 0) damage_bonus_percent = 0;
if (slow_ticks > 0 && --slow_ticks == 0) slow_percent = 0;
if (charm_ticks > 0 && --charm_ticks == 0) charm_side = side;
if (dot_ticks > 0) {
    if (dot_ticks % DOT_INTERVAL_TICKS == 0) { vitality -= dot_damage (clamp 0); Event effect/EFFECT_DOT {actor=dot_source, actor_part=-1, element=dot_element, amount=dot_damage}; death check with actor=dot_source, actor_part=-1, element=dot_element; }
    dot_ticks--;
}
```
`DOT_INTERVAL_TICKS 20` (once per second). Fire 60 ticks → 3 pulses of 2 (at the hit Tick, +20, +40); Venom 200 → 10 pulses of 1. DoT damage goes to Vitality only, no Part, no block, no resistance (duration was already scaled). A Combatant that dies in this stage stops at that point (check `alive` before each line is unnecessary: the DoT line is last).

Frost in movement: in `battle_move_combatants` (`movement.c`), after computing `step`: `if (me->slow_ticks > 0) step = step * (100 - me->slow_percent) / 100;`. `slow_percent 100` ⇒ step 0 ⇒ rooted (data only ships 40; the fixture test sets 100 by hand).

### 1.7 Death, Remains, battle end

`death_check(battle, data, target, actor, actor_part, element)` after every Vitality or integrity change:
- dead ⇔ `vitality <= 0` **or** no working slot whose Part has `vitality > 0` (a Heart that is destroyed or cut off from the Core no longer counts; destroying the Core therefore kills).
- On death: `alive = false`; if `remains_count < MAX_REMAINS` append `Remains {x, y, slot_count, every slot's part index}` (the whole heap, destroyed Parts included); Event `death` `{actor, actor_part, target, target_part=-1, element, amount=0, detail=remains index or -1}`.
- Dead Combatants keep their Parts/Orders but are skipped by `battle_act`, effects, movement, perception (already) and spawn counting.
- Battle end: `battle_winner` unchanged (real `side`); the headless loop already stops on it.

### 1.8 Spawning

A working Part with a `spawn` Order, a `spawn` ability, `spawn_plan >= 0`, cooldown 0:
- `alive_minions = count of alive Combatants with spawner == me`. If `alive_minions >= part.spawn_cap` or `combatant_count == MAX_COMBATANTS`: nothing, no cooldown (two sacs on one Body share the per-Combatant count; acceptable).
- Else `init_combatant(&combatants[combatant_count], data, plan->id, spawner.side, &plan->body, spawner.x, spawner.y)` (make `init_combatant` non-static or add a thin `battle_add_combatant`; plan slots are all fixed so `plan->body` is usable as-is), then `minion.charm_side = spawner.charm_side; minion.charm_ticks = spawner.charm_ticks; minion.spawner = me;` `combatant_count++`; Event `spawn` `{actor=me, actor_part=s, target=new index, target_part=-1, element=-1, amount=0, detail=0}`; cooldown.
- Placement is the spawner's exact position; `separate_combatants` pushes them apart next movement stage (already tested in ticket 04). Dead minions free their cap slot. Minions are named by plan id (as `add_monster` does).

### 1.9 Charm

Applied inside the Mind hit (1.5), immediately: the target's `perceive` flips that Tick; its stale strike/cast Orders at its new allies do nothing (same-side check in 1.3), its allies stop targeting it once Tactics re-decide (06). Expiry in 1.6 restores `charm_side = side`. A second Mind hit refreshes/overwrites. Minions inherit the spawner's current charm (1.8).

### 1.10 State additions (`battle.h`) and JSON

`Combatant` gains: `int dot_damage; int dot_element; int dot_source; int slow_percent; int spawner; int damage_bonus_percent; int damage_bonus_ticks;` — `dot_element`, `dot_source`, `spawner` are -1 when unset (`init_combatant` sets them).

`EventKind` gains `EVENT_EFFECT, EVENT_SPAWN` appended **after** `EVENT_DEATH` (keeps old ids stable); `EVENT_IDS` in `battle_json.c` gains `"effect", "spawn"`.

New enum in `battle.h` for `Event.detail` of effect Events: `typedef enum { EFFECT_DOT, EFFECT_SLOW, EFFECT_CHARM, EFFECT_STRIP, EFFECT_HEAL } EffectKind;`

Event field meanings (write these into the ticket Answer later, not as code comments):
- `hit`: actor, actor_part, target, target_part, element, amount = Part damage, detail = Part integrity left.
- `effect`: actor (source; -1 for Traps), actor_part (-1 for DoT pulses), target, target_part -1, element, amount (DoT damage / ticks / 1 stripped / Vitality healed), detail = EffectKind.
- `death`: actor, actor_part, target, -1, element, 0, remains index.
- `spawn`: spawner, sac slot, minion index, -1, -1, 0, 0.

`battle_json.c`: `save_combatant` adds the 7 numbers (`dot_source`, `spawner` as plain ints, `dot_element` as int; keep it numeric like `vulnerable_element`). `load_combatant` adds: `dot_damage` [0, 1000000], `dot_element` [-1, ELEMENT_COUNT-1], `dot_source` via `read_combatant_ref`, `slow_percent` [0, 100], `spawner` via `read_combatant_ref`, `damage_bonus_percent` [0, 1000], `damage_bonus_ticks` [0, 1000000]. All required (same hostile-safe style; old saves from 03/04 stop loading — accepted, as in ticket 17).

### 1.11 Files

- `src/sim/combat.h` (new):
  ```c
  #ifndef SIM_COMBAT_H
  #define SIM_COMBAT_H
  #include "battle.h"
  #define VITALITY_SHARE_PERCENT 50
  #define BLOCK_PERCENT 50
  #define DOT_INTERVAL_TICKS 20
  int combatant_resistance_percent(const Combatant *c, const GameData *data, Element element, bool internal_only);
  bool combatant_in_reach(const Combatant *from, const Combatant *to, int range_px);
  bool combatant_has_working_heart(const Combatant *c, const GameData *data);
  void combatant_hit(Battle *battle, const GameData *data, int actor, int actor_part, int target, Element element, int damage);
  void battle_act(Battle *battle, const GameData *data);
  void battle_tick_effects(Battle *battle, const GameData *data);
  #endif
  ```
- `src/sim/combat.c` (new, ~250 lines): the above plus static helpers `find_ability(part, kind)`, `pick_target_part`, `is_blocking`, `kill`, `apply_element`, `chain_lightning`, `act_strike_or_cast`, `act_support`, `act_spawn`.
- `src/sim/battle.c`: `battle_step` as in 1.1; `init_combatant` sets the new -1 fields; expose `init_combatant` as `void battle_init_combatant(...)` in `battle.h` (or keep static and add `int battle_add_combatant(Battle*, const GameData*, const char *name, Side, const Body*, int x, int y)` returning the index — pick this one; `add_monster` and `battle_setup` use it too).
- `src/sim/battle.h`: fields, enums, prototype.
- `src/sim/battle_json.c`: save/load fields, `EVENT_IDS`.
- `src/sim/movement.c`: the one slow line.
- `tests/test_combat.c` (new) + `run_combat_tests` in `tests/test_main.c`; `tests/fixtures/battle_elements.json` (new).
- `tests/test_battle.c`: `test_round_trip_is_identical` also sets the new fields; `test_load_rejects_bad_input` adds 3 rejections (`"dot_element":-1`→`8`, `"spawner":-1`→`99`, `"slow_percent":0`→`101`).
- Makefile: nothing (wildcards).

## 2. The fixture `tests/fixtures/battle_elements.json`

Hand-written, pretty-printed, in the exact shape `battle_save_json` emits (every key required by `battle_load_json`). Root: `seed 5, rng 12345, tick 0, pantheon "Hindu", arena = 32 strings of 48 '.'`, `pickups []`, `traps []`, `remains []`, `log []`. All Combatants: `alive true`, `slow_ticks 0, dot_ticks 0, charm_ticks 0, dot_damage 0, dot_element -1, dot_source -1, slow_percent 0, spawner -1, damage_bonus_percent 0, damage_bonus_ticks 0`, `charm_side` = side, `decision {stance "none", hurt_level 0, vulnerable_element -1}`, every slot `x 0, y 0`, every part `cooldown 0`, order `{kind "idle", target -1, x_millipx 0, y_millipx 0}`, integrity = the Part's `integrity` in `data/parts.json`. Edges are index pairs from the Core (slot 0) to every other slot. All `y_millipx 176000` (= 11 tiles exactly, an A* anchor row, so a move_to along the row is a straight line; copied from `test_moves_at_part_speed`).

| idx | name | side | x_millipx | slots (id: part) | vitality/max |
|---|---|---|---|---|---|
| 0 | Striker | pantheon | 168000 | torso: torso (core), heart: large_heart (internal), sword: sword_hand, maw: ember_maw, fang: frost_fang, tentacle: electric_tentacle, lantern: glow_lantern, stinger: venom_stinger, lobe: mind_lobe, tendril: void_tendril, sac: spawning_sac (internal), healer: healing_hand, shield: shield_hand | 60/60 |
| 1 | Dummy | horde | 200000 (32 px right) | torso: torso (core), heart: heart (internal), gland: ganglion (internal), plate: armour_plate, legs: legs, shield: shield_hand | 40/40 |
| 2 | Second | horde | 264000 (64 px right of Dummy) | torso, heart: heart (internal) | 40/40 |
| 3 | Far | horde | 360000 (160 px right of Dummy) | torso, heart: heart (internal) | 40/40 |
| 4 | Hurt | pantheon | 120000 (48 px left of Striker) | torso, heart: heart (internal) | 10/40 |

Reach checks that make the tests deterministic: Striker→Dummy 32 px ≤ 20 + 22 (every melee Part reaches); Striker→Second 96 px > 36 + 22 (no melee) but ≤ 96 + 22 (ember_maw reaches); Dummy→Second 64 ≤ 96 chain; Dummy→Far 160 > 96; Striker→Hurt 48 ≤ 96 + 22.

Kinetic resistance of Dummy = 40 (armour_plate, external, working); every other Element 0.

Test helper in `test_combat.c`: `static void load_fixture(void)` reads the file (copy `read_text_file` from `src/main_headless.c` — 12 lines; keep it static in the test), `battle_load_json`; `static int slot(int c, const char *id)` = `find_body_slot`; `static void order(int c, const char *slot_id, OrderKind kind, int target)`; `static int sum_integrity(int c, bool internal)`; `static int count_events(EventKind kind)`; `static const Event *last_event(EventKind kind)`.

## 3. Tests (`tests/test_combat.c`) — every one loads the fixture, mutates in C, steps

Orders and hits:
1. `test_kinetic_hit_splits_part_and_vitality`: sword→Dummy, 1 step: Σ external integrity loss 7 (12·60/100), vitality 37, one hit Event {actor 0, actor_part slot(sword), target 1, element kinetic, amount 7, detail = that Part's integrity}; cooldown == 12 after the step.
2. `test_cooldown_spaces_hits`: keep the sword Order: after 13 steps exactly 2 hit Events, after 25 steps 3 (hits on Ticks 0, 12, 24).
3. `test_out_of_reach_does_nothing`: sword→Second, 1 step: no Events, cooldown 0.
4. `test_cast_needs_line_of_sight`: maw→Second with `battle.obstacle[11][14] = 1` (row 11, column 14: x 224000–240000, clear of both feet): no Event; clear the tile: hit Event.
5. `test_order_at_ally_does_nothing`: sword→Hurt: no Event.
6. `test_destroyed_part_does_not_act`: `parts[slot(sword)].integrity = 0`, sword→Dummy: no Event.
7. `test_block_halves_damage`: Dummy shield Order block (target -1); sword→Dummy: Σ loss 3, vitality 39.
8. `test_damage_bonus_and_void_strip`: Striker `damage_bonus_percent 50, ticks 10`: sword→Dummy loses 10 (12·150/100=18, ·60/100=10). Then Dummy `damage_bonus_ticks 50`; tendril→Dummy: ticks 0, effect Event EFFECT_STRIP amount 1.
9. `test_support_heals_up_to_max`: healer→Hurt: vitality 20, effect Event EFFECT_HEAL amount 10, cooldown 40; set Hurt vitality 35, cooldown 0, step: 40, amount 5; at 40 no Event and cooldown stays 0.
10. `test_spawn_creates_minions_up_to_cap`: sac Order spawn; step: combatant_count 6, [5] name "minion_imp", side pantheon, spawner 0, position == Striker's, spawn Event {0, slot(sac), 5}; set sac cooldown 0 and step twice more: count 8; once more: count 8, no new Event; `combatants[5].alive = false`, step: count 9.
11. `test_core_destruction_cuts_off_parts`: Dummy torso integrity 1, plate/legs/shield integrity 0 (so torso is the only external candidate), vitality 40; sword→Dummy: torso 0 → no working Heart → dead. (This is also death rule 2 via cut-off.)

Elements (one each):
12. `test_fire_burns_over_time`: maw→Dummy; after step 1 vitality 35 (3 hit + 2 pulse), dot_ticks 59; after 21 steps 33; after 61 steps 31 and dot_ticks 0; 3 EFFECT_DOT Events with actor 0.
13. `test_frost_slows_then_roots`: fang→Dummy, Dummy legs Order move_to (600000, 176000): after 20 steps Dummy moved 20 · 1800 = 36000 (the hit lands in the act stage before the first movement, so every step is slowed) (legs 60 px/s = 3000 mpx/Tick · 60/100); `slow_percent` 40, effect Event EFFECT_SLOW amount 60. Then set `slow_percent 100, slow_ticks 20`: 10 steps move 0. After slow_ticks hit 0, slow_percent is 0.
14. `test_lightning_chains_within_range`: tentacle→Dummy: Dummy loses 7/vitality 37; Second loses 3/vitality 39; Far untouched; 2 hit Events.
15. `test_radiance_ignores_armour_but_not_glands`: lantern→Dummy: Σ loss 6 (armour_plate ignored); then `body.slots[slot(gland)].part = find_part("radiance_gland")`, cooldown 0: loss 1.
16. `test_venom_hits_internal_first`: stinger→Dummy: Σ internal integrity loss 5 (heart or ganglion, random), externals untouched, vitality 38; dot_ticks 200; after 201 steps vitality 28 (10 pulses).
17. `test_mind_charms_for_a_while`: lobe→Dummy: hit Event amount 0, `charm_side` pantheon, `charm_ticks 100`, EFFECT_CHARM Event amount 100; sword→Dummy next step: no hit (same charm_side); after 99 steps still pantheon, after step 100 horde. Also `test_mind_resisted_by_gland`: Dummy gland slot part → mind_gland: charm_ticks 25.
18. `test_void_is_plain_without_buffs`: tendril→Dummy with no bonus: Σ loss 6, no effect Event.

Death and end:
19. `test_death_by_vitality_leaves_remains`: Dummy vitality 3; sword→Dummy: alive false, remains_count 1, remains[0] at Dummy's position with 6 Parts, death Event {0, slot(sword), 1, kinetic, detail 0}.
20. `test_death_when_all_hearts_destroyed`: Dummy heart integrity 0 (already destroyed), vitality 40; sword→Dummy (any external Part): the death check after the hit finds no working Heart: dead.
21. `test_death_by_dot_names_the_source`: maw→Dummy with Dummy vitality 4: dies on the first pulse (3 + 2 > 4), death Event actor 0, actor_part -1, element fire.
22. `test_battle_ends_when_a_side_is_dead`: Second, Far alive=false; Dummy vitality 3; sword→Dummy: `battle_winner == SIDE_PANTHEON`.
23. `test_dead_combatants_do_nothing`: Dummy alive=false, Dummy shield block + Striker sword→Dummy: no Event.

Persistence:
24. `test_effects_round_trip`: after fire+frost+mind+void state (tests 12/13/17 setup, 1 step), save → load → save equal; and resume: save at step 5, run 60 more both ways, equal JSON (same pattern as `test_resume_matches_uninterrupted_run`).
25. In `test_battle.c`: the three new rejections in `test_load_rejects_bad_input` (section 1.11).

Also keep `make asan` green: the seed sweep still runs 300 Ticks with idle Orders.

## 4. Commits (in order; each leaves `make test` green)

1. **State and JSON for combat** — `battle.h` fields/enums (1.10), `init_combatant` defaults, `battle_json.c` save/load + `EVENT_IDS`, `test_battle.c` round-trip fields + 3 rejections. `Add combat state fields and effect/spawn Events to the Battle state`.
2. **Hits, destruction, death** — `combat.{h,c}` with `combatant_resistance_percent`, `combatant_in_reach`, `combatant_has_working_heart`, `hit_once`/`combatant_hit` (Kinetic only: steps 1–8, 10 of 1.4; element effects as an empty switch), `battle_add_combatant` refactor in `battle.c`; the fixture; tests 1, 7 (block needs the Order check only — do it here), 11, 19, 20, 22. `Resolve hits against a target Part with resistance, block and Vitality share; death leaves Remains`.
3. **Orders each Tick** — `battle_act` (cooldowns, strike/cast/support/spawn), `battle_tick_effects` with only the damage-bonus line, `battle_step` wiring; tests 2–6, 8 (bonus half only), 9, 10, 23. `Execute Orders per Part each Tick with ability ranges and cooldowns; spawn minions up to the cap`.
4. **Element effects** — `apply_element` + `chain_lightning`, full `battle_tick_effects`, movement slow line; tests 8 (strip half), 12–18, 21, 24. `Apply Element effects: Fire and Venom DoT, Frost slow, Lightning chain, Radiance through armour, Mind Charm, Void strip`.
5. Hand-back lists the Event field meanings (1.10) and the ambiguities resolved (area ignored, pierce unused, two sacs share a cap, old saves need the new fields).

## 5. Seams left for 06 / 07 / 08 (nothing built for them)

- 06 Tactics writes `parts[s].order` and reads `Perception`; it needs `combatant_in_reach` (exposed) to send legs toward range. Decision Events keep `EVENT_DECISION`.
- 07 Power reads `Part` only. Horde size/`HORDE_SIZE` untouched.
- 08 Traps call `combatant_hit(battle, data, -1, -1, who, def->element, def->damage)` and set `slow_percent/slow_ticks` for Frost Rune themselves; Rage Draught sets `damage_bonus_percent/ticks`; Pickups implement `ORDER_PICK_UP` in `battle_act` (currently a no-op branch).
