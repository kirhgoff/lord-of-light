# Plan: ticket 12 — Clay UI: T panel, Battle Log, end screen

Repo: /Users/kirill.lastovirya/Projects/lord-of-light (main @ 07b7aac). Branch/worktree `12-clay-ui`.
Spec: `docs/plan/issues/12-clay-ui.md`. Follow `docs/research/clay-raylib.md` to the letter (v0.14, own UTF-8 measure, panel declared before variable content, `Clay_GetScrollContainerData` only before `Clay_BeginLayout`, dynamic strings `isStaticallyAllocated = false`).
Rules: `src/sim/` never includes raylib (ADR 0002); no comments; GLOSSARY names; `make test` + `make asan` green; signed commits with the executor-brief trailer.

## Facts the design rests on (verified)

- `vendor/clay/clay.h` is `// VERSION: 0.14`; `vendor/clay/clay_renderer_raylib.c` is vendored verbatim (includes `"raylib.h"`, `"raymath.h"`); no bundled raylib headers. **Nothing includes or compiles Clay yet.** `vendor/VERSIONS` already says `clay v0.14`.
- Makefile: `VIEW_OBJECTS` = wildcard `src/view/*.c` (a new `ui.c` is picked up automatically); `VIEW_PURE_OBJECTS` lists the raylib-free view objects linked into `tests`; non-pure view objects get `STRICT += -I$(RAYLIB_DIR)`. `make asan` builds `test headless` only.
- raylib 5.5 config has `SUPPORT_DEFAULT_FONT`, `SUPPORT_FILEFORMAT_TTF`; `GetFontDefault()` (baseSize 10) is what `DrawText` in `draw.c` already uses. `viewport_screen_x/y` in `src/view/scene.c` is the exact formula of the `Camera2D` built in `draw_frame_contents`, so overlay positions come from the pure Viewport.
- Clay seam: `draw.c` `draw_frame_contents`: world (`BeginMode2D`) → `draw_hud` → `draw_toast` → `draw_confirm_dialog`. Clay goes after `draw_toast`, before the confirm dialog (dialog stays on top).
- T today: `main_game.c` `if (IsKeyPressed(KEY_T)) scene->xray = !scene->xray;` → `Scene.xray` draws internal Parts at 50 %. `--xray` sets it for screenshots; `make screenshots` renders `s1_t60_xray.png` and `s{1,2,3}_t100000.png` (runs until a winner).
- Esc dialog: raylib, `Hud.confirm_quit`, Enter/Y quit, Esc/N stay.
- Events (`battle.h`, producers in `combat.c`, `floor.c`, `decide.c`):
  - HIT `{actor, actor_part, target, target_part, element, amount=Part damage, detail=remaining integrity}`; `actor == -1` for Traps; `detail == 0` ⇒ Part destroyed (logged exactly once: `pick_target_part` only picks working Parts).
  - EFFECT `{actor, actor_part (−1 for DOT pulses), target, element, amount, detail=EffectKind}`; DOT amount = Vitality damage, HEAL = Vitality healed, SLOW/CHARM = ticks, STRIP = buffs stripped.
  - PICKUP `{actor, amount, detail=pickup index}` → `data->pickups[b->pickups[detail].def].name`.
  - TRAP `{target, element, amount, detail=trap index}` (followed by a HIT with `actor == -1`).
  - DEATH `{actor, actor_part, target, element, detail=remains index}`; actor −1 when killed by a Trap/DOT with no source.
  - SPAWN `{actor, actor_part, target=minion}`.
  - DECISION `{actor, amount=Stance, detail=HurtLevel, element, decider, fallback, confidence_permille, answer_ms, stance_permille[]}`. The Reflex re-decides every `60/Initiative` Ticks, so plain Reflex Decisions are noise for a log.
- `Combatant.decision` holds the current Decision; Horde `Combatant.name` is the Body Plan id (duplicates like "floater", "floater"); minions are named after their spawn plan.
- Name tables: `battle_json.c` has static `STANCE_IDS`, `FALLBACK_IDS`, `EVENT_IDS`; `data.c` exports `element_id`, `order_kind_id`.
- Laya: `LayaDecider.briefing[c]` holds the last Briefing sent for c; `DeciderSetup` exposes `laya` + `laya_decider`. With the Reflex there is no stored Briefing; `briefing_write(battle, data, c, out, size)` is pure and cheap.
- `DecisionStats battle_decision_stats(const Battle *)` gives Laya/Reflex counts, fallback counts, mean confidence (permille), mean answer ms.
- `battle_winner(b)` ≥ 0 stops the sim loop; `draw_status_label` draws "Pantheon wins"/"Horde wins" in raylib.
- Window is 1280×720, not resizable, no HIGHDPI → Clay layout dims are constant.

## Decisions (user-accepted 2026-10-16)

1. **T = one "tech view" flag.** `Scene.xray` now shows x-ray internal Parts + per-Combatant overlays + the right panel. `--xray` renders the whole T view in screenshots. Key help: `T tech view`.
2. **Font: raylib default** (`GetFontDefault()`, slot 0), sizes 10 and 20 (title).
3. **Esc dialog stays raylib**, drawn after Clay.
4. **Combatant labels (view only):** `combatant_label()` = name, plus ` N` (1-based among same-named Combatants) only when shared: "floater 1", "floater 2"; Deity names untouched. Used by HUD bars, overlays, log lines, end screen. Sim names and Briefings unchanged.
5. **Battle Log:** one line per Event; DECISION lines only for Laya answers and real fallbacks (`decider == DECIDER_LAYA`, or `fallback` ∉ {NONE, WAITING}). Formatted every frame from the tail of `b->log` (newest 200 displayable Events) into frame scratch — stateless, nothing to reset. Colour by kind.
6. **Autoscroll:** the research's `StickLogToBottom`.
7. **Hover/select:** hover = Combatant under the mouse; left click (not over the panel) selects; click on empty floor clears. Detail shows hovered else selected. Briefing: `laya_decider.briefing[c]` when running Laya and non-empty, else a live `briefing_write`.
8. **Overlay:** label + Vitality; one line per Part (`name cur/max`, destroyed red, cut-off grey); Stance + active statuses. Overlap in melee piles accepted.
9. **End screen stats (pure, from `b->log` + state):**
   - winner: `battle_winner`.
   - death log: every EVENT_DEATH via `event_line`.
   - top damage dealer: argmax over actors ≥ 0 of Σ HIT.amount (DOT and Trap damage excluded). None → "nobody".
   - most-destroyed Part kind: HIT with `detail == 0` → `b->combatants[target].body.slots[target_part].part`; argmax; `data->parts[p].name`.
   - longest survivor = **last to fall**: dead Combatant with the greatest DEATH tick (ties → lowest index); "nobody fell" if none.
   - Pickups taken: count of EVENT_PICKUP.
   - top Element by damage: argmax over `element` of Σ HIT.amount (Trap hits included).
   - Laya vs Reflex share and mean confidence from `battle_decision_stats`; also mean answer ms.
   Ties → lowest index/enum.
10. **End-screen keys:** `R` → new Battle with `seed + 1` (toast "New Battle, seed N"); Esc → quit dialog. R ignored under `--replay`. Known: an in-flight Laya reply from the old Battle may land in the new one (same as F9).
11. **Screenshot flags:** `--xray` (full T view) and new `--select N`. End screen needs no flag (`--ticks 100000`).
12. **Scissor in screenshots:** cosmetic if the log isn't clipped in PNGs; don't chase it.

## Module split

| File | raylib? | Owns |
|---|---|---|
| `src/view/report.{h,c}` (new, pure, in `VIEW_PURE_OBJECTS`) | no | `combatant_label`, `event_line`, `battle_last_decision`, `report_build` |
| `src/view/scene.{h,c}` | no | add `viewport_world_x/y`, `scene_pick` |
| `src/view/ui.{h,c}` (new) | yes + Clay | the only TU with `CLAY_IMPLEMENTATION` + `#include "clay_renderer_raylib.c"`; arena, font, measure fn, frame scratch, pointer/scroll, overlays, panel, end screen |
| `src/view/draw.{h,c}` | yes | `ui_draw` at the seam; HUD uses labels; winner label removed |
| `src/main_game.c` | yes | `ui_init/ui_update/ui_close`, click/R handling, `--select` |
| `src/sim/battle.h` + `battle_json.c` | no | export `stance_id`, `fallback_id` |
| `tests/test_report.c` (new), `tests/test_view.c` | no | tests below |

Makefile: `VIEW_PURE_OBJECTS += $(BUILD)/obj/src/view/report.o`; `$(BUILD)/obj/src/view/ui.o: STRICT += -isystem vendor/clay`; `screenshots` adds `$(BUILD)/game --seed 1 --ticks 60 --xray --select 0 --screenshot $(BUILD)/shots/s1_t60_xray_select0.png`.

## report.h

```c
#include "../sim/battle.h"
#include "../sim/decide.h"
#define LABEL_SIZE (NAME_LEN + 8)
#define EVENT_LINE_SIZE 160

void combatant_label(const Battle *b, int c, char *out, size_t size);
int event_line(const Battle *b, const GameData *data, const Event *e, char *out, size_t size);
int battle_last_decision(const Battle *b, int c);

typedef struct {
    int winner;
    int top_dealer, top_dealer_damage;
    int destroyed_part, destroyed_count;
    int last_to_fall, last_fall_tick;
    int pickups;
    int top_element, top_element_damage;
    DecisionStats decisions;
} BattleReport;
BattleReport report_build(const Battle *b, const GameData *data);
```

- `combatant_label`: shared = any d != c with the same name; n = 1 + count of d < c with the same name; `shared ? "%s %d" : "%s"`.
- `event_line` returns bytes written, 0 = not shown. Prefix `"%d.%ds "` from `tick / 20`, `(tick % 20) / 2`. `"?"` for out-of-range indices. Lines:
  - HIT actor ≥ 0 `"%s's %s hits %s's %s for %d %s"`; actor −1 `"A Trap hits %s's %s for %d %s"`; `" - destroyed"` when `detail == 0`.
  - EFFECT: DOT `"%s takes %d %s"`; HEAL `"%s heals %s for %d"`; SLOW `"%s is slowed by %s"`; CHARM `"%s is charmed by %s"`; STRIP `"%s loses %d buffs"`.
  - PICKUP `"%s takes %s"`; TRAP `"%s triggers %s"` (guard index ranges).
  - DEATH actor ≥ 0 `"%s dies to %s's %s (%s)"`; else `"%s dies (%s)"`.
  - SPAWN `"%s spawns %s"`.
  - DECISION Laya `"Laya: %s will %s (%d.%03d)"`; fallback ∉ {NONE, WAITING} `"Reflex for %s: Laya %s"`; else 0.
- `battle_last_decision`: scan back for `kind == EVENT_DECISION && actor == c`.
- `report_build`: one pass with `damage_by[MAX_COMBATANTS]`, `destroyed_by[MAX_PARTS]`, `damage_by_element[ELEMENT_COUNT]`; strict `>` argmax (first wins).

## scene.h additions

```c
float viewport_world_x(const Viewport *v, float screen_x);
float viewport_world_y(const Viewport *v, float screen_y);
#define PICK_HALF_WIDTH_PX 16
int scene_pick(const Scene *s, const Battle *b, const Atlases *atlases, float world_x, float world_y);
```

`scene_pick`: for each living c: `scene_position` → (x, y); h = `body_feet_offset`; hit if `|world_x - x| <= 16 && y - h <= world_y <= y`; keep the largest y (front-most). −1 if none.

## ui.h / ui.c

```c
typedef struct { int selected; int hovered; bool follow_log; } Ui;
bool ui_init(int width, int height);
void ui_close(void);
void ui_update(Ui *ui, const Battle *b, const Scene *scene, const Atlases *atlases, const Viewport *view, bool input_blocked);
void ui_draw(const Ui *ui, const Battle *b, const GameData *data, const Scene *scene, const Atlases *atlases, const Viewport *view,
             const char (*laya_briefings)[BRIEFING_MAX + 1]);
```

- File scope: font, arena, `frame_text[64 * 1024]`, `MeasureTextUtf8`, `HandleClayError` (stderr + abort), `FrameString` helpers from the research. `PANEL_WIDTH 360`, `LOG_LINES 200`, `FONT_SMALL 10`, `FONT_BIG 20`.
- `ui_update` (once per frame before drawing, also once in screenshot mode): reset scratch; `Clay_SetLayoutDimensions`; `Clay_SetPointerState`; wheel → `StickLogToBottom` on `CLAY_ID("BattleLog")`; `Clay_UpdateScrollContainers`; `over_panel`; picking when `scene->xray && !input_blocked && !over_panel && no winner`; left press selects.
- `ui_draw`: `Clay_BeginLayout`; winner → `end_screen`; else if xray → root aligned right with `panel`, then an overlay per living Combatant; `Clay_Raylib_Render`.
- `panel` (`CLAY_ID("Panel")`, fixed width, top padding HUD_HEIGHT): first the `BattleLog` scroll container (vertical clip, `Clay_GetScrollOffset`), then the detail section (label; Decision line with Stance, hurt word, element, decider, fallback, confidence, answer ms; the Briefing as wrapped text). Log before detail (#606 rule).
- Log colours: DEATH red, PICKUP green, TRAP yellow, SPAWN cyan, EFFECT heal green / other orange, DECISION dim, HIT text.
- `overlay(c)`: floating, attached to root at `viewport_screen_x/y` of the head, pointer passthrough, zIndex 1.
- `end_screen` (`CLAY_ID("EndScreen")`): title (FONT_BIG); stat lines; "Deaths" + `DeathLog` scroll container of DEATH lines; footer `"R: new Battle (seed %u)   Esc: quit"`.

## draw.c / main_game.c

- `draw_frame_contents(..., const Hud *hud, const Ui *ui, laya_briefings)`: after `draw_toast` → `ui_draw` → confirm dialog.
- `draw_segment` uses `combatant_label`; `draw_status_label` only "PAUSED"; key help `"Space pause  Arrows pan  -/= zoom  T tech view  F5 save  F9 load  Esc quit"`.
- `main_game.c`: `--select N`; `Ui ui = {-1, -1, true}`; `ui_init` after `SetTargetFPS` (fail → exit 1); `ui_close` before `CloseWindow`; `ui_update` before `BeginDrawing`; R on the end screen (not in the dialog, not under `--replay`) → `battle_setup(++seed)`, `scene_reset`, `sound_reset`, clear selection, toast; screenshot path calls `ui_update` once.

## Tests

1. `test_labels_number_shared_names`: 3 "floater" + 1 "Agni" → "floater 1/2/3", "Agni".
2. `test_event_lines_per_kind`: each kind's verb/fields; Trap HIT starts "A Trap"; Reflex NONE/WAITING → 0; Laya conf 420 → "Laya:" and "0.420"; Reflex/UNREACHABLE → "unreachable"; tick 250 → "12.5s".
3. `test_report_build`: hand-built log → top dealer 1 with 10, top Element frost, destroyed count 1, last to fall 6 at tick 80, pickups 2, Laya 1; empty log → −1/0.
4. `test_last_decision_finds_newest`.
5. `test_viewport_world_inverts_screen` (zoom 2, panned).
6. `test_pick_hits_the_front_combatant`.

Reviewer screenshots: `s1_t60_xray.png`, `s1_t60_xray_select0.png`, `s1_t100000.png` (end screen).

## Commits

1. `build: compile Clay v0.14 with its raylib renderer in src/view/ui.c` — Makefile rule, `ui.{h,c}` with init/close/measure/scratch and an empty layout at the seam; `stance_id`/`fallback_id` exported.
2. `view: Combatant labels and one line per Event` — `report.{h,c}` (labels, event lines, last decision), tests 1, 2, 4; HUD labels; key help.
3. `view: T view shows overlays and the Battle Log panel`.
4. `view: hover or click a Combatant for its Briefing and Decision` — picking (tests 5, 6), detail section, `--select N`, `_select0` shot.
5. `view: end screen with the Battle report, R starts a new Battle` — `report_build` (test 3), end screen, R restart, raylib winner label removed.

## Pitfalls (from the research; do not deviate)

- `CLAY_IMPLEMENTATION` and the renderer include only in `ui.c`. Never `return`/`break` inside a `CLAY({...}) { }` body.
- `Clay_SetPointerState` once per frame; `Clay_GetScrollContainerData` only before `Clay_BeginLayout`.
- Dynamic `Clay_String`s: `isStaticallyAllocated = false`, bytes in `frame_text`, reset only at the top of `ui_update`.
- `CLAY_STRING()` only for literals. Don't use the renderer's `Raylib_MeasureText`.
- `BattleLog` is declared before anything whose element count varies.
