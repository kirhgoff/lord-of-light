# Plan: ticket 06 — Tactics, Reflex, Decision scheduling

Repo: /Users/kirill.lastovirya/Projects/lord-of-light (main @ 81335ab, ticket claimed). Branch `06-reflex-and-scheduling`, worktree `~/Projects/lord-of-light-06-reflex-and-scheduling`.
Spec: `docs/plan/issues/06-reflex-and-scheduling.md`, ADR 0001 + 0003, `GLOSSARY.md`. Rules: `docs/plan/executor-brief.md` (no comments, small signed commits with explicit pathspecs, `make test` + `make asan` green, trailer `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`).

Everything is integer arithmetic (`int64_t` for squared distances). No RNG use in this ticket. New files: `src/sim/decide.{h,c}`, `tests/test_decide.c`. Touched: `battle.{h,c}`, `battle_json.c`, `movement.{h,c}`, `vision.{h,c}`, `main_headless.c`, `tests/test_main.c`, `tests/test_battle.c`, `tests/test_combat.c`, `tests/test_movement.c`, `tests/fixtures/battle_elements.json`, `data/parts.json` (commit 5 only).

## 0. What exists (reuse, do not rebuild)

- `Decision {Stance stance; int hurt_level; int vulnerable_element;}` on `Combatant`, saved under `"decision"` (`battle_json.c`: `save_combatant`, `load_decision`; `hurt_level` bounded 0..100 today). `Stance` enum `STANCE_NONE..STANCE_COLLECT`. `init_combatant` sets `vulnerable_element = -1`.
- `EVENT_DECISION` already exists in `EventKind` / `EVENT_IDS` ("decision"); nobody logs it yet.
- `perceive(battle, data, c, &out)` (`vision.h`): `Perceived {kind, index, band, def, side, health_quarters, name, x, y}`. Far lists only enemies (side `UNKNOWN`) and healing Pickups; Traps only at Close; `health_quarters` is 1..4 (ceil) at Close/Medium, `UNKNOWN` at Far. Perception requires LOS from centre to centre — the same check `act_attack` makes for casts, so a perceived enemy in range is always castable.
- `static int health_quarters(const Combatant *)` in `vision.c` — make public (commit 3).
- `combatant_in_reach(from, to, range_px)` (`combat.h`): centre distance ≤ range + both feet radii (22 px). `combatant_resistance_percent(c, data, element, internal_only)`. `body_working_parts`. `find_ability` is static in `combat.c` — write a 3-line local in `decide.c`.
- Movers = working Parts with `move_speed_px_per_second > 0`; `combatant_move_target` takes the first working Part with a `move_to` Order; `combatant_speed_px_per_second` is the max over working Parts.
- `Part.initiative` is loaded (`data.c:800`). Data: head 1, keen_head 2, eye_stalk 1, compound_eyes 1, dim_eye 0, fanged_head 1, ganglion 3, third_eye 2, radiant_crown 2, judging_eye 2; all else 0 (minion bodies sum to 0).
- `ArenaObjectDef.heal`, `.radius` (px); `Battle.pickups/traps` are `FloorObject {def, x, y, respawn_in}`; `pick_up` is a no-op in `combat.c` (no Part has the ability) — ticket 08 owns it.
- `isqrt` is static in `movement.c` — make public (commit 4).
- Headless: `battle_setup(seed)` then `battle_step` until a winner or `--ticks` (default 2000). Today nobody sets Orders, so every seed prints "Winner: none".
- Spawn positions: Pantheon x = 2 tiles, Horde x = 46 tiles → 704 px apart; the longest Far vision is 512 px, so at Tick 0 nobody perceives anybody (hence the seek rule, 1.6).
- Deities usable in tests (`data/pantheons/*.json`, look up by Pantheon + Deity name): Greek Zeus = sword_hand (strike kinetic 24 px), legs, torso, keen_head (vision 128/256/448, initiative 2), thunderbolt_hand (cast lightning 170 px), large_heart (60), lightning_gland (lightning 75). Greek Apollo = healing_hand (support 10, range 96), legs, torso, radiant_crown (vision 128/256/480, initiative 2), sun_bow (cast radiance 220), large_heart, radiance_gland. Celtic Dagda = healing_hand, legs, torso, head, war_club (strike kinetic 28), large_heart, armour_plate (kinetic 40). Egyptian Isis = shield_hand (block), wings_of_light (support 6 range 96, move_speed 90), torso, head, healing_hand, large_heart, radiance_gland — no legs. Celtic Morrigan has raven_sac (spawn). Hindu Shiva = head + third_eye + ganglion → initiative 6.

## 1. Settled design

### 1.1 Shapes (`battle.h`)

```c
typedef enum { HURT_UNHURT, HURT_WOUNDED, HURT_BADLY_WOUNDED, HURT_NEAR_DEATH, HURT_COUNT } HurtLevel;
typedef enum { DECIDER_NONE, DECIDER_REFLEX, DECIDER_LAYA, DECIDER_COUNT } DeciderKind;

typedef struct {
    Stance stance;
    HurtLevel hurt_level;
    int vulnerable_element;      /* Element or -1 */
} Decision;

/* Combatant gains (after damage_bonus_ticks): */ int next_decision_tick;
/* Event gains as its LAST field so the existing 9-field initialisers stay valid (0 == DECIDER_NONE): */ int decider;

typedef struct Decider Decider;   /* defined in decide.h */
/* decider NULL: no Decisions are made and Orders stay as set (tests). */
void battle_step(Battle *battle, const GameData *data, Decider *decider);
```

No `focus`/`ally` in Decision: Tactics re-run every Tick (1.4) and re-pick the nearest enemy / most hurt ally from current perception, so the Decision is exactly ADR 0003's "Stance + Appraisals" — the three answers Laya will give in ticket 11. Save shape unchanged apart from the bound.

HurtLevel from Vitality, same ceil-quarters as `vision.c`: `q = (vitality * 4 + max - 1) / max`; `q >= 4 → UNHURT, 3 → WOUNDED, 2 → BADLY_WOUNDED, else NEAR_DEATH`. Thresholds: > 75 % unhurt, (50, 75] wounded, (25, 50] badly wounded, ≤ 25 % near death. For max 60: 46+ / 31–45 / 16–30 / 1–15.

Save/load (`battle_json.c`):
- `"decision"."hurt_level"` bound `0..HURT_COUNT-1`.
- Combatant `"next_decision_tick"` after `"damage_bonus_ticks"`, loaded with bound `0..MAX_TICK`.
- Event `"decider"` after `"detail"`, bound `0..DECIDER_COUNT-1`.
- Old saves and the fixture stop loading until `next_decision_tick` is present (fixture edit in commit 2).

### 1.2 Decision Event

`(Event){0, EVENT_DECISION, c, -1, -1, -1, d.vulnerable_element, d.stance, d.hurt_level, kind}` — `amount` = Stance, `detail` = HurtLevel, `element` = appraised Element, `decider` = `DECIDER_REFLEX` now, `DECIDER_LAYA` later. Probabilities: the Reflex has none; ticket 11 adds a `confidence_permille` Event field when it has one (not now).

### 1.3 Decider interface (`decide.h`) — sync now, async-ready by polling

```c
#define DECISION_INTERVAL_K 60
#define FLEE_PX 64

struct Decider {
    bool (*decide)(Decider *self, const Battle *battle, const GameData *data, int combatant, Decision *out);
    DeciderKind kind;
};

Decider reflex_decider(void);                                   /* (Decider){reflex_decide, DECIDER_REFLEX} */
bool reflex_decide(Decider *self, const Battle *battle, const GameData *data, int combatant, Decision *out);  /* always true */
int combatant_initiative(const Combatant *c, const GameData *data);   /* sum of working Parts' initiative, floor 1 */
void tactics_apply(Battle *battle, const GameData *data, int combatant);
void battle_decide(Battle *battle, const GameData *data, Decider *decider);
```

`decide` returning `false` means "not yet": the Combatant stays due and is polled again next Tick. That is the entire async seam — ticket 11's Laya Decider enqueues on the first call and returns true once its worker has the answer; the sim never blocks and never sees a thread. No queues, callbacks or futures here.

### 1.4 Scheduling (`battle_decide`, first thing in `battle_step` when `decider != NULL`)

```
due = every alive Combatant with next_decision_tick <= battle->tick
sort due by combatant_initiative desc, then index asc (insertion sort, n <= 64)
for c in due:
    if decider->decide(decider, battle, data, c, &d):
        combatants[c].decision = d; log the Decision Event with decider->kind
        combatants[c].next_decision_tick = tick + max(1, DECISION_INTERVAL_K / combatant_initiative(c))
    else if combatants[c].decision.stance == STANCE_NONE:
        reflex_decide(...); store + log with DECIDER_REFLEX; next_decision_tick unchanged (still due)
for every alive Combatant with decision.stance != STANCE_NONE, index order: tactics_apply(battle, data, c)
```

- `k = 60`: Initiative 1 → every 60 Ticks (3 s), 2 → 30, 3 → 20, 6 → 10. Laya answers in 0.5–6 s (ADR 0001).
- Everyone starts with `next_decision_tick = 0`, so all decide on Tick 0 in Initiative order; a spawned minion decides on its first Tick.
- `battle_step`: `battle_decide` → `battle_act` → `battle_tick_effects` → `battle_move_combatants` → `tick++`. Orders decided at Tick t act at Tick t.
- Tactics run every Tick from the stored Decision (one `perceive` per Combatant, trivial cost). ADR 0001's "keeps its current Orders until an answer arrives" becomes "keeps its current Decision": same behaviour, and `move_to` goals never go stale.
- `decider == NULL` → nothing here runs; every existing test that sets Orders by hand keeps working.

### 1.5 Reflex (`reflex_decide`) — rules in order, from `perceive(self)`

```
enemies = perceived Combatants with band == BAND_FAR or side != my charm_side
allies  = perceived Combatants with side == my charm_side        (Close/Medium only)
focus   = nearest enemy by squared distance, tie → lower index
healing = perceived Pickup with band == BAND_FAR or data->pickups[def].heal > 0; nearest
hurt    = HurtLevel of self (1.1)
1. COLLECT  if hurt >= HURT_WOUNDED and healing exists and (no focus or dist²(healing) < dist²(focus))
2. RETREAT  if hurt == HURT_NEAR_DEATH and some ally has a working Part with a support ability
3. SUPPORT  if self has a working Part with a support ability and some ally has health_quarters == 1
4. FIGHT    otherwise
vulnerable_element: over my working Parts' strike/cast abilities (slot order, then ability order) the Element with the lowest
    combatant_resistance_percent(focus, data, e, e == ELEMENT_RADIANCE); ties keep the first; -1 without focus or attack ability
```

Rule 2's guard is the termination guard: a near-death Combatant with no healer in sight fights (its attack Parts fire anyway, 1.6). Without it two near-death opponents flee each other until the Tick bound. Laya may still choose RETREAT freely; Tactics then flee.

### 1.6 Tactics (`tactics_apply`) — one Order per working Part, every Tick

Perception and candidates as in 1.5, plus:
- `pref` = preferred attack ability: the first working strike/cast ability whose Element == `decision.vulnerable_element`; else the working strike/cast ability with the greatest range (tie → first). NULL if none.
- `most_hurt` = among self + allies, the lowest health_quarters (self via `combatant_health_quarters`); tie → self, then lower index.
- `healer` = nearest ally with a working support Part; `healer_range` = that Part's first support ability's range.
- `my_support` = self's first working support ability.
- `pickup` = nearest perceived healing Pickup, else nearest perceived Pickup of any kind.
- `seek` = nearest living Combatant with `charm_side != mine` by true position (tie → lower index).

Legs goal and `hold`, by Stance; a Stance whose own target is missing falls through to the FIGHT rows:
| Stance | goal | hold when |
|---|---|---|
| FIGHT, focus and pref exist | focus position | `combatant_in_reach(self, focus, pref->range)` |
| FIGHT, focus exists, no pref (unarmed) | — | always |
| FIGHT, no focus, seek exists | seek position | never |
| FIGHT, nothing | — | always |
| SUPPORT, my_support and a hurt ally (quarters < 4) exist | that ally's position | `combatant_in_reach(self, ally, my_support->range)` |
| RETREAT, healer exists | healer position | `combatant_in_reach(self, healer, healer_range)` |
| RETREAT, no healer, focus exists | `self + (self − focus) * FLEE_PX / dist` (int64, clamp to the Arena) | never |
| RETREAT, nothing perceived | — | always |
| COLLECT, pickup exists | pickup position | `dist²(self, pickup) <= (8 px)²` |

Then `goal = avoid_traps(goal)` (1.7) when not holding.

Per working slot `s`, first matching branch wins:
1. `move_speed > 0` and not hold and goal exists → `{ORDER_MOVE_TO, -1, goal.x, goal.y}`.
2. Part has a strike/cast ability and focus exists → the first of its strike/cast abilities with `combatant_in_reach(self, focus, range)`, else its first strike/cast ability → `{that kind, focus, 0, 0}`.
3. Part has a support ability → `{ORDER_SUPPORT, most_hurt, 0, 0}` (`act_support` refuses gain 0 without cooldown).
4. Part has a block ability → `{ORDER_BLOCK, -1, 0, 0}` (always).
5. Part has a spawn ability → `{ORDER_SPAWN, -1, 0, 0}` (cap in combat.c).
6. `s == body.core` and Stance COLLECT and pickup exists and hold → `{ORDER_PICK_UP, -1, pickup.x, pickup.y}` — the seam for ticket 08 (`target` stays -1 because `Order.target` loads as a Combatant ref; 08 matches by position or contact).
7. else `{ORDER_IDLE, -1, 0, 0}`.

Branch 1 before 2–3 makes wings_of_light (mover + support) move when the legs must move and support when standing; same for moving cores (serpent_coil, gas_bladder, minion bodies). Non-working Parts keep their old Order (combat.c ignores them).

"Strike/cast the focus with the appraised Element" means: every attack Part fires whenever it can; the appraised Element decides which Part's range the legs close to (Zeus walks to sword range against a Lightning-resistant target, holds at thunderbolt range against a Kinetic-resistant one).

Ability `area`: deferred, stays unused. Nothing in Tactics needs AoE.

### 1.7 Trap avoidance (`avoid_traps`, static in decide.c)

For each perceived Trap (Close band only): `avoid = data->traps[def].radius * MILLIPX_PER_PX + 2 * COMBATANT_RADIUS_MILLIPX`. With `d = goal − self`, `v = trap − self` (int64): `t_num = v·d`, `t_den = d·d`; skip if `t_den == 0`, `t_num < 0` or `t_num > t_den` (trap behind or beyond the goal). `P = self + d * t_num / t_den`. If `dist²(trap, P) < avoid²`: waypoint `W = trap + (P − trap) * avoid / isqrt(dist²(trap, P))`; if `P == trap`, use the left perpendicular `(-d.y, d.x) * avoid / isqrt(t_den)`. Return the first waypoint found (perception order), clamped to the Arena. Re-evaluated every Tick, so once the trap is behind, the real goal returns.

### 1.8 Deadlock at a gap (`movement.c`)

Root cause: when the full step is blocked by a Combatant, the axis-slide fallbacks either do nothing (`try_move(0, 0)` "succeeds") or creep back onto the blocked line, so a Combatant behind an ally, or facing one in a gap, never gets round. Fix: slide along the blocker's tangent instead of the axes.

```c
static int blocking_combatant(const Battle *battle, int self, int x_millipx, int y_millipx);  /* hits_combatant, returning the index or -1 */

static void slide_around(Battle *battle, int self, int blocker, int dx, int dy, int step)
{
    const Combatant *me = &battle->combatants[self], *other = &battle->combatants[blocker];
    int64_t bx = other->x_millipx - me->x_millipx, by = other->y_millipx - me->y_millipx;
    int64_t len = isqrt(squared(bx) + squared(by));
    if (len == 0) return;
    int tx = (int)(-by * step / len), ty = (int)(bx * step / len);
    if ((int64_t)tx * dx + (int64_t)ty * dy < 0) tx = -tx, ty = -ty;
    if (!try_move(battle, self, tx, ty)) try_move(battle, self, -tx, -ty);
}
```
In `move_towards`, after the full `try_move` fails: `blocker = blocking_combatant(battle, self, me->x_millipx + dx, me->y_millipx + dy)`; if `blocker >= 0` → `slide_around`, return; else the existing axis slides (terrain). Tangent sign: positive dot product with the wanted direction; exact zero keeps the left tangent. Each step rotates ~8° around the blocker, so a Combatant clears an ally in ≤ 23 Ticks, and head-on pairs in a 2-tile gap end up touching and in melee reach (≥ 18 px + 22 px) instead of freezing.

### 1.9 Initiative

`combatant_initiative` = sum of `data->parts[slot.part].initiative` over working Parts, floored to 1. Derived, not saved. Deities 1–6, monsters 1–5, minions 1.

### 1.10 Headless

`main_headless.c`: `Decider reflex = reflex_decider();` and `battle_step(&battle, &data, &reflex)`; `DEFAULT_TICKS` 4000. No new flags (`--decider` is ticket 11).

## 2. Commits (each leaves `make test` green; `make asan` at commits 4 and 6)

### Commit 1 — movement: slide around a blocking Combatant
Files: `src/sim/movement.c`, `tests/test_movement.c`.
- `hits_combatant` → `blocking_combatant` (index or -1; `try_move` tests `>= 0`); add `slide_around`; wire into `move_towards` (1.8).
- Test `test_blocked_by_ally_slides_around`: `open_field()`; `alive = false` for Combatants 2..count-1; `place(0, 5, 10)`, `place(1, 8, 10)`, `set_side` both `SIDE_PANTHEON`; `send_to(0, 12 * TILE + TILE / 2, 10 * TILE + TILE / 2)`; `run(300)`; CHECK Combatant 0 is exactly at the goal. Confirm it is red before the fix (0 stops touching 1).

### Commit 2 — battle state: HurtLevel, next_decision_tick, Event.decider, Decider parameter
Files: `src/sim/battle.h`, `src/sim/battle.c`, `src/sim/battle_json.c`, `src/main_headless.c`, `tests/test_battle.c`, `tests/test_combat.c`, `tests/test_movement.c`, `tests/fixtures/battle_elements.json`.
- Enums/fields per 1.1. `battle_step` accepts `Decider *decider` and ignores it in this commit (`(void)decider;`); `typedef struct Decider Decider;` forward declaration in `battle.h`.
- Save/load per 1.1. `Event` initialisers in `combat.c` untouched.
- All callers pass `NULL`: the `run` helpers in the three test files, `test_combat.c:211` and `:588`, headless.
- Fixture: add `"next_decision_tick": 0` after `"damage_bonus_ticks"` in each of the 5 combatants (keep the file's 1-space indentation).
- `test_round_trip_is_identical`: `(Decision){STANCE_FIGHT, HURT_WOUNDED, ELEMENT_FIRE}`; `battle.combatants[3].next_decision_tick = 77` and CHECK it after loading; log `(Event){0, EVENT_DECISION, 2, -1, -1, -1, ELEMENT_FIRE, STANCE_FIGHT, HURT_WOUNDED, DECIDER_REFLEX}` and CHECK `loaded.log[1].decider == DECIDER_REFLEX` (adjust the `log_count == 1` check to 2).
- `test_load_rejects_bad_input`: add `check_rejected(json, "\"hurt_level\":0", "\"hurt_level\":4")`, `check_rejected(json, "\"decider\":0", "\"decider\":3")`, `check_rejected(json, "\"next_decision_tick\":0", "\"next_decision_tick\":-1")`.

### Commit 3 — decide: Decider interface, Initiative, Reflex, scheduling, Decision Events
Files: `src/sim/decide.h`, `src/sim/decide.c`, `src/sim/vision.h`, `src/sim/vision.c`, `src/sim/battle.c`, `src/main_headless.c`, `tests/test_decide.c`, `tests/test_main.c`.
- `vision.c`: `health_quarters` → public `int combatant_health_quarters(const Combatant *)` (declared in `vision.h`; update the call in `perceive`).
- `decide.c`: 1.3, 1.4 (without the Tactics loop), 1.5, 1.9. `tactics_apply` arrives in commit 4.
- `battle.c`: `#include "decide.h"`; `if (decider) battle_decide(battle, data, decider);` first in `battle_step`.
- Headless uses `reflex_decider()`.
- `tests/test_decide.c` (register `run_decide_tests` in `test_main.c`). Harness: `static GameData data; static Battle battle; static Decider reflex;` `load_data()` as in test_battle.c; `fresh()` = `battle_free(&battle); memset(&battle, 0, sizeof battle); battle.rng = 1;`; `add_deity(const char *pantheon, const char *deity, Side side, int x_px, int y_px)` finds the Pantheon by `name` and the Deity by `name` in `data.pantheons[]` and returns `battle_add_combatant(&battle, &data, deity, side, &body, x_px * MILLIPX_PER_PX, y_px * MILLIPX_PER_PX)` (CHECK found); `kill_movers(c)` sets integrity 0 on every slot whose Part has `move_speed_px_per_second > 0`; `decision(c)` runs `reflex_decide(&reflex, &battle, &data, c, &d)` and returns `d`; `order(c, slot_id)` returns `battle.combatants[c].parts[find_body_slot(&body, slot_id)].order`; `run(ticks, decider)`.
  1. `test_initiative_sums_working_parts_floor_one`: Zeus → 2; Shiva → 6; Zeus with its `head` slot integrity 0 → 1.
  2. `test_hurt_level_thresholds`: Zeus (max 60) alone; vitality 60, 46, 45, 31, 30, 16, 15, 1 → UNHURT, UNHURT, WOUNDED, WOUNDED, BADLY, BADLY, NEAR_DEATH, NEAR_DEATH via `decision(zeus).hurt_level`.
  3. `test_reflex_fights_and_appraises_the_element`: Zeus PANTHEON (100,200), Dagda HORDE (300,200) → FIGHT, `vulnerable_element == ELEMENT_LIGHTNING` (kinetic 40 % behind armour_plate). Replace Dagda by a second Zeus as HORDE → `ELEMENT_KINETIC` (lightning 75 %). Zeus alone → FIGHT, element -1.
  4. `test_reflex_retreats_only_toward_a_healer`: Zeus vitality 10, Dagda HORDE at (300,200) → FIGHT; add Apollo PANTHEON at (100,300) → RETREAT.
  5. `test_reflex_supports_a_near_death_ally`: Apollo PANTHEON (100,200), Zeus PANTHEON (200,200) vitality 10 (quarters 1) → Apollo SUPPORT; Zeus vitality 20 (quarters 2) → FIGHT; Zeus vitality 10 but Apollo's `hand_left` integrity 0 → FIGHT.
  6. `test_reflex_collects_when_wounded_and_pickup_is_nearer`: Zeus (300,200) vitality 40; `battle.pickups[0] = (FloorObject){0, 200 * PX, 200 * PX, 0}`, `pickup_count = 1` (def 0 = health_vial); Dagda HORDE (450,200) → COLLECT; pickup at (100,200) → FIGHT; pickup back at (200,200), vitality 60 → FIGHT; pickup `respawn_in = 1` → FIGHT.
  7. `test_decisions_are_scheduled_by_initiative`: `battle_setup(&battle, &data, 3)`, `battle_step(&battle, &data, &reflex)` once → exactly `combatant_count` Decision Events at tick 0; their `actor` sequence has non-increasing `combatant_initiative` with ascending index on ties; every `next_decision_tick == max(1, 60 / initiative)`; `decider == DECIDER_REFLEX`; `amount/detail/element` equal the Combatant's stored Decision. Step to tick 60 → a Combatant with Initiative 1 has exactly 2 Decision Events, one with Initiative 2 has 3.
  8. `test_pending_decider_keeps_combatant_due_and_uses_reflex_once`: `Decider pending = {pending_decide, DECIDER_LAYA}` (returns false) on `battle_setup(3)`: one step → every stance != NONE, all events `DECIDER_REFLEX`, every `next_decision_tick == 0`; second step → no new Decision Events; then `Decider answers = {retreat_decide, DECIDER_LAYA}` (returns true with `{STANCE_RETREAT, HURT_UNHURT, -1}`) → one step logs `combatant_count` events with `decider == DECIDER_LAYA`, `amount == STANCE_RETREAT`, and bumps every `next_decision_tick`.
  9. `test_null_decider_changes_nothing`: `battle_setup(3)`, five `battle_step(NULL)` → no Decision Events, stances NONE, Orders still IDLE.

### Commit 4 — tactics: Orders per Part, seek, trap avoidance
Files: `src/sim/decide.h`, `src/sim/decide.c`, `src/sim/movement.h`, `src/sim/movement.c`, `tests/test_decide.c`.
- `movement.c`: drop `static` from `isqrt`; declare `int64_t isqrt(int64_t value);` in `movement.h`.
- `tactics_apply` per 1.6–1.7; add the final loop to `battle_decide`.
- Tests (set `battle.combatants[c].decision` by hand, call `tactics_apply(&battle, &data, c)`, inspect `order(c, slot_id)`). Slot ids: Zeus `hand_left` sword_hand, `hand_right` thunderbolt_hand, `legs`, `torso` (core), `head` keen_head; Apollo `hand_left` healing_hand, `hand_right` sun_bow, `head` radiant_crown (also casts radiance 160 px), `legs`; Isis `hand_left` shield_hand, `legs` wings_of_light, `hand_right` healing_hand; Morrigan `ravens` raven_sac; Dagda `hand_left` healing_hand, `gland` armour_plate. Vision: Zeus 128/256/448, Apollo 128/256/480, Isis and Dagda (head) 96/192/320 — allies are only perceived at Close/Medium, so keep allies within those ranges:
  1. `test_fight_closes_to_the_preferred_range_and_attacks`: Zeus PANTHEON (100,200), Apollo HORDE (250,200) (150 px). Zeus `{FIGHT, UNHURT, ELEMENT_KINETIC}` (pref sword 24 + 22 < 150) → `legs` MOVE_TO (250000,200000); `hand_left` STRIKE target apollo; `hand_right` CAST target apollo (170 + 22 ≥ 150). Element LIGHTNING (pref thunderbolt) → `legs` IDLE. Zeus at (230,200), element KINETIC → `legs` IDLE, `hand_left` STRIKE.
  2. `test_support_block_and_spawn_orders`: Isis PANTHEON (100,200), Zeus PANTHEON (150,200) vitality 1, Apollo HORDE (400,200); Isis FIGHT → shield slot BLOCK, healing_hand slot SUPPORT target zeus, wings slot SUPPORT target zeus (Isis is unarmed → hold). Zeus vitality 60 → healing_hand target isis (self). Morrigan PANTHEON FIGHT → raven_sac slot SPAWN.
  3. `test_mover_with_ability_moves_before_it_supports`: Isis PANTHEON (100,200) `{SUPPORT, UNHURT, -1}`, Zeus PANTHEON vitality 1 at (250,200) (Medium band, 150 > 96 + 22) → `legs` (wings) MOVE_TO (250000,200000); Zeus at (150,200) → `legs` SUPPORT target zeus.
  4. `test_support_stance_sends_legs_to_the_hurt_ally`: Apollo PANTHEON (100,200) `{SUPPORT, UNHURT, -1}`, Zeus PANTHEON vitality 1 at (300,200) (Medium) → `legs` MOVE_TO (300000,200000); Zeus at (150,200) → `legs` IDLE (96 + 22 ≥ 50).
  5. `test_retreat_flees_or_runs_to_a_healer`: Zeus `{RETREAT, NEAR_DEATH, -1}` at (100,200), Dagda HORDE (300,200) → `legs` MOVE_TO (36000, 200000); add Apollo PANTHEON (100,400) → `legs` MOVE_TO (100000,400000); all others dead → `legs` IDLE.
  6. `test_collect_walks_to_the_pickup_then_marks_pick_up`: Zeus `{COLLECT, WOUNDED, -1}` at (100,200), `pickups[0] = {0, 300000, 200000, 0}`, count 1 → `legs` MOVE_TO (300000,200000), `torso` IDLE; Zeus at (300,200) → `legs` IDLE, `torso` `{ORDER_PICK_UP, -1, 300000, 200000}`.
  7. `test_seek_when_nothing_is_perceived`: Zeus FIGHT at (100,200), Apollo HORDE at (700,200) (600 px > 448) → `legs` MOVE_TO (700000,200000); `hand_left`/`hand_right` IDLE.
  8. `test_seen_trap_is_walked_around`: Zeus PANTHEON (88,168), Apollo HORDE (248,168) with integrity 0 on every slot except `torso` and `heart` (unarmed, blind, immobile dummy); `battle.traps[0] = (FloorObject){frost_snare index (find by id in `data.traps`, radius 20), 168000, 168000, 0}`, `trap_count = 1`; 200 × `battle_step(&reflex)` tracking the minimum centre distance Zeus–trap (int64 isqrt or compare squared) → `> 31 * PX` (radius 20 + feet 11; the waypoint sits 42 px off and anchors deviate ≤ 8 px), and at some Tick `combatant_in_reach(&zeus, &apollo, 24)` is true. Control: rebuild the same start with `trap_count = 0` → minimum distance `< 11 * PX` (the anchor path runs 8 px below the trap centre).
  9. `test_tactics_are_deterministic`: `battle_setup(1)`, 200 steps with the Reflex, twice from scratch → identical `battle_save_json`.

### Commit 5 — data: minion bodies can see
Files: `data/parts.json`, `tests/test_data.c`.
- Give `crow_body` and `wisp_core` `"vision": {"close": 48, "medium": 96, "far": 160}` like `imp_body`. Blind minions never attack (attack Orders need a perceived focus), and a Battle reduced to crows vs wisps never ends. They are `minion_only`, so `fills_horde_slot` still excludes them from Horde slots.
- Test: `find_part("crow_body")` and `find_part("wisp_core")` have `vision_far > 0`.

### Commit 6 — full Battles: seed 1 winner, determinism, sweep, resume; asan
Files: `tests/test_decide.c`, `src/main_headless.c` (DEFAULT_TICKS 4000).
- `test_seed_one_runs_to_a_deterministic_winner`: `battle_setup(1)`, step with the Reflex until `battle_winner >= 0` or tick 4000 → CHECK winner ≥ 0 and tick < 4000; repeat from scratch → identical `battle_save_json`.
- `test_seed_sweep_ends_within_bound`: seeds 0..9, each a winner within 4000 Ticks. If a seed does not end, do not change any rule: print the seed, tick, and each living Combatant's name, vitality and working attack Parts, and list it in the hand-back (the orchestrator decides). Known residual stalemate: both survivors with every attack Part destroyed and no DoT ticking.
- `test_resume_mid_battle_matches_straight_run_with_reflex`: like `test_resume_matches_uninterrupted_run` but with the Reflex and no hand-set Orders (save at 300, resume 300 vs straight 600, identical JSON).
- `make asan` (its loop runs seeds with `--ticks 300` + resume). Run `build/headless --seed 1`, record the printed winner/tick in the hand-back.

## 3. Hand-back must include
Branch, worktree, commit SHAs, outputs + exit codes of `make test`, `make asan`, `build/headless --seed 1`, any sweep seed without a winner, and any deviation from 1.5–1.7.

## Orchestrator override (user-accepted 2026-10-16)

Pickups are taken by walking over them (ticket 08 plan, decision 1). In COLLECT, Tactics only `move_to` the Pickup's position; do not emit `ORDER_PICK_UP` and do not add the Core pick_up marker. Ticket 08 removes `ORDER_PICK_UP`; if 06 merges first, leave the enum value alone.
