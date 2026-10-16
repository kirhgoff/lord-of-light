# Plan: ticket 10 — raylib view

Repo: /Users/kirill.lastovirya/Projects/lord-of-light (main @ df95aa6). Branch/worktree `10-raylib-view`.
Spec: `docs/plan/issues/10-raylib-view.md` (scale and internal-Part rules are user decisions, don't re-litigate).
Rules: `src/sim/` untouched (ADR 0002); no comments; GLOSSARY names; `make test` + `make asan` green; signed commits with the executor-brief trailer.

## Facts the design rests on (verified in the code)

- Sim: `battle_step` at 20 Ticks/s (`TICKS_PER_SECOND` in `src/sim/movement.h`). Positions are `int x_millipx, y_millipx` = centre of the feet footprint (top-down, 11 px radius). Arena 48×32 tiles of 16 px = 768×512 world px.
- Battle Log is append-only (`battle->log[0..log_count)`), each Event carries `tick`. The view learns everything from it:
  - `EVENT_HIT {actor, actor_part, target, target_part, element, amount, detail=remaining integrity}` (`src/sim/combat.c:hit_once`). `actor_part >= 0` means that Part's Order fired. `detail == 0` = Part destroyed. Trap hits have `actor == -1`.
  - `EVENT_EFFECT {actor, actor_part (−1 for DOT pulses, slot for heals), target, element, amount, detail=EffectKind}`.
  - `EVENT_SPAWN {actor, actor_part, target=minion}`; `EVENT_DEATH {target, detail=remains index}`; `EVENT_PICKUP {actor, detail=pickup index}`; `EVENT_TRAP {target, element, detail=trap index}`; `EVENT_DECISION` (ignored by the view).
  - `ORDER_BLOCK` fires no Event and sets no cooldown (`perform` returns false) — it is a held state, read from `parts[s].order.kind`.
  - `ORDER_MOVE_TO` is visible as position change between Ticks.
- Cooldown is set only when a Part acts (`state->cooldown = ability->cooldown_ticks`), but detecting it needs a per-Tick copy of every PartState; Events already carry `actor_part`. Decision: Events, not cooldowns.
- Atlases (`tools/gen_sprites.py`): every sheet is ONE ROW, frame i at x = i*frame_w. Sidecar `assets/<folder>/<id>.json` = `{frame_w, frame_h, pivot{x,y}, frames{<state>: [indices]}}`. States: parts `idle[2] act[3] hurt[1] broken[1]`; effects `play[5]`; pickups `idle[2]`; traps `idle[1] act[1]`; floor `variants[4]`; rock/pillar `idle[1]`. External Parts 32×32, internal 16×16 (pivot in 16-px space), effects 32×32 pivot (16,16), pickups/traps 24×24 pivot (12,12), floor 16×16, rock 32×32 pivot (16,28), pillar 32×64 pivot (16,60).
- A Part frame is placed at `origin + anchor − pivot` (`tools/compose_body.py:render_view`). Anchors are `BodySlot.anchor_x/anchor_y` (side view, all sprites face right). Slot order in data = far Parts first (ticket 17), so iterating `body.slots` in order IS the far-first draw order.
- Biped extents: legs anchor y=10, pivot y=9, frame_h 32 → feet at origin+33; head top at origin−38. "~4 tiles tall" holds at 1×.
- `GameData` gives sprite ids: `parts[i].sprite`, `pickups[i].sprite`, `traps[i].sprite`, `element_id(e)` for effects; `BodySlot.internal` / `Part.internal` flag internal Parts.
- raylib 5.5 vendored with `SUPPORT_FILEFORMAT_PNG`, `SUPPORT_IMAGE_EXPORT`, `FLAG_WINDOW_HIDDEN`, `LoadRenderTexture`, `LoadImageFromTexture`, `ImageFlipVertical`, `ExportImage`, `DrawTexturePro` (negative `source.width` flips), `BeginMode2D(Camera2D)`, `BLEND_ADDITIVE`, `SetExitKey`.
- Makefile: `VIEW_OBJECTS` = every `src/view/*.c`, all get `-I$(RAYLIB_DIR)`; `tests` links only sim + cJSON. `make asan` builds `test headless` (never `game`).
- Clay (ticket 12) renders after the world pass and before `EndDrawing` (`docs/research/clay-raylib.md` "Frame loop wiring"). Leave that slot in `draw_frame_contents` (see below).

## Module split (smallest set)

Delete `src/view/arena_view.{h,c}`. Add:

| File | Links raylib? | Owns |
|---|---|---|
| `src/view/atlas.{h,c}` | no (cJSON only) | `Atlas` metadata parsed from sidecars; `Atlases` table indexed like `GameData` |
| `src/view/scene.{h,c}` | no | `Viewport` (camera), snapshot + interpolation, animation state, Event → animation, effects ring, drawable y-sort, frame picking, tile hash. Pure; unit-tested |
| `src/view/draw.{h,c}` | yes | `Sheets` (Texture2D table), `draw_frame_contents(...)`: floor, sorted drawables, effects, HUD, confirm dialog |
| `src/main_game.c` | yes | args, window, fixed-step loop, input, screenshot mode |
| `tests/test_view.c` | no | tests for atlas + scene |

Makefile: `VIEW_PURE_OBJECTS := $(BUILD)/obj/src/view/atlas.o $(BUILD)/obj/src/view/scene.o`; tests link them (`$(BUILD)/tests: $(TEST_OBJECTS) $(SIM_OBJECTS) $(VIEW_PURE_OBJECTS) $(CJSON_OBJECT)`); change the raylib include rule to `$(BUILD)/obj/src/main_game.o $(filter-out $(VIEW_PURE_OBJECTS),$(VIEW_OBJECTS)): STRICT += -I$(RAYLIB_DIR)` so the pure files cannot include raylib by accident. Add target `screenshots` (below).

Ticket 20 reads the camera as `Viewport` from `src/view/scene.h`. Ticket 12 inserts `Clay_Raylib_Render` at the marked point in `draw_frame_contents`. Ticket 16 must call `scene_reset(&scene, &battle)` after `battle_load_json` (re-syncs `seen_events` and the snapshot).

## atlas.h

```c
typedef enum { FRAMES_IDLE, FRAMES_ACT, FRAMES_HURT, FRAMES_BROKEN, FRAMES_PLAY, FRAMES_VARIANTS, FRAMES_KIND_COUNT } FramesKind;
typedef struct { int frame_w, frame_h, pivot_x, pivot_y; int first[FRAMES_KIND_COUNT], count[FRAMES_KIND_COUNT]; } Atlas;
typedef struct { Atlas parts[MAX_PARTS], pickups[MAX_ARENA_OBJECTS], traps[MAX_ARENA_OBJECTS], effects[ELEMENT_COUNT], floor, rock, pillar; } Atlases;
bool atlas_load(Atlas *atlas, const char *folder, const char *id, char *err, size_t err_size);
bool atlases_load(Atlases *atlases, const GameData *data, char *err, size_t err_size);
int atlas_frame(const Atlas *atlas, FramesKind kind, int n);   /* first[kind] + n % count[kind]; 0 if count is 0 */
```
`atlas_load` reads `assets/<folder>/<id>.json` with cJSON (reuse the `read_text_file` pattern from `src/main_headless.c`; copy it, it is 12 lines), takes `frame_w`, `frame_h`, `pivot.x/y`, and for each name in a static table `{"idle","act","hurt","broken","play","variants"}` the first index and array length if present. Fail with a message naming the path on any missing/invalid number. `atlases_load` loops `data->part_count`, `pickup_count`, `trap_count`, `ELEMENT_COUNT` (`element_id`), then `tiles/floor`, `tiles/rock`, `tiles/pillar`.

## scene.h

Constants: `TICK_SECONDS (1.0f / TICKS_PER_SECOND)`, `ZOOM_MIN 1`, `ZOOM_MAX 4`, `ZOOM_DEFAULT 2`, `PAN_SCREEN_PX_PER_SECOND 600`, `IDLE_FRAME_TICKS 10`, `WALK_FRAME_TICKS 3`, `ACT_TICKS 6`, `HURT_TICKS 4`, `FLASH_TICKS 8`, `FLASH_TICKS_RADIANCE 16`, `EFFECT_TICKS 10`, `TRAP_ACT_TICKS 10`, `MAX_EFFECTS 64`, `NEVER (-1000000)`, `HUD_HEIGHT 48`, `REMAINS_SPREAD_PX 12`.

```c
typedef struct { float centre_x, centre_y; int zoom; int width, height; } Viewport;   /* centre in world px; zoom = screen px per world px */
void viewport_init(Viewport *v, int width, int height);                                /* zoom 2, centred on the Arena, clamped */
void viewport_clamp(Viewport *v);        /* per axis: visible = size / zoom; if visible >= arena → centre = arena / 2, else centre ∈ [visible/2, arena − visible/2] */
void viewport_pan(Viewport *v, float dx_screen_px, float dy_screen_px);               /* centre += d / zoom, then clamp */
void viewport_zoom(Viewport *v, int step);                                             /* zoom = clamp(zoom + step, ZOOM_MIN, ZOOM_MAX), then clamp */
float viewport_screen_x(const Viewport *v, float world_x);  float viewport_screen_y(const Viewport *v, float world_y);

typedef enum { DRAW_OBSTACLE, DRAW_PICKUP, DRAW_TRAP, DRAW_REMAINS, DRAW_COMBATANT } DrawableKind;
typedef struct { DrawableKind kind; int index; float x, y; } Drawable;                /* x,y = world px of the feet / base */
#define MAX_DRAWABLES (ARENA_WIDTH_TILES * ARENA_HEIGHT_TILES + MAX_COMBATANTS + MAX_REMAINS + 2 * MAX_ARENA_OBJECTS)

typedef struct { float x, y; int element; int since_tick; } Effect;

typedef struct {
    int prev_x[MAX_COMBATANTS], prev_y[MAX_COMBATANTS], prev_count;
    float alpha;                                     /* 0..1 fraction of a Tick since the last step */
    bool facing_left[MAX_COMBATANTS];
    int act_since[MAX_COMBATANTS][MAX_SLOTS];
    int hit_since[MAX_COMBATANTS][MAX_SLOTS]; int hit_element[MAX_COMBATANTS][MAX_SLOTS];
    int trap_since[MAX_ARENA_OBJECTS];
    Effect effects[MAX_EFFECTS]; int effect_next;
    int seen_events;
    bool xray;
} Scene;

void scene_reset(Scene *s, const Battle *b);                 /* NEVER everywhere, seen_events = log_count, snapshot = current, facing by side (Pantheon right, Horde left) */
void scene_before_step(Scene *s, const Battle *b);            /* copy positions + count */
void scene_after_step(Scene *s, const Battle *b);             /* facing from dx; consume log[seen_events..log_count) via scene_apply_event */
void scene_apply_event(Scene *s, const Battle *b, const Event *e);   /* pure mapping, see table */
float scene_time(const Scene *s, const Battle *b);           /* b->tick + alpha, the only animation clock */
void scene_position(const Scene *s, const Battle *b, int c, float *x_px, float *y_px);  /* lerp prev→cur by alpha; c >= prev_count → current */
int scene_collect(const Scene *s, const Battle *b, Drawable *out);   /* obstacles, visible pickups (respawn_in == 0), traps, remains, living Combatants; qsort by y then kind then index */
int part_frame(const Atlas *a, const Combatant *c, int slot, const Part *part, bool working, bool moving, int act_since, int hit_since, int idle_phase, int tick);
int effect_frame(const Effect *e, int tick);                 /* -1 when expired */
int tile_hash(int column, int row);                          /* (column * 7 + row * 13) & 0xff; floor variant = hash % 4, pillar when hash % 3 == 0 */
int body_feet_offset(const Body *body, const GameData *data, const Atlases *atlases);  /* max over external slots of anchor_y − pivot_y + frame_h; falls back to 32 if no external slot */
void remains_offset(int remains_index, int part_index, int *dx, int *dy);  /* deterministic scatter from a tiny hash: dx ∈ [−12, 12], dy ∈ [−4, 6] */
```

Event → animation (`scene_apply_event`), tick = `e->tick`:

| Event | Effect on Scene |
|---|---|
| HIT | `actor >= 0 && actor_part >= 0` → `act_since[actor][actor_part] = tick`. `target_part >= 0` → `hit_since[target][target_part] = tick`, `hit_element = element`. Push Effect at the target's body centre (position − feet_offset/2) with `element`. |
| EFFECT | `actor_part >= 0` → `act_since[actor][actor_part] = tick` (heal). Push Effect at target with `element` (DOT pulse, heal, slow, charm, strip all show the Element sheet; no per-kind art exists). |
| SPAWN | `act_since[actor][actor_part] = tick`. Push kinetic Effect at the minion. |
| TRAP | `trap_since[detail] = tick`; push Effect at the trap with `element`. |
| PICKUP, DEATH, DECISION | nothing (Remains and `respawn_in` come from sim state). |

Effect ring: `effects[effect_next++ % MAX_EFFECTS] = ...` (oldest overwritten).

`part_frame` priority: integrity ≤ 0 → BROKEN; `tick − hit_since < HURT_TICKS` → HURT; `tick − act_since < ACT_TICKS` → ACT[(tick − act_since) * 3 / ACT_TICKS]; `order.kind == ORDER_BLOCK` → ACT[1] held; `moving && part->move_speed_px_per_second > 0` → ACT[(tick / WALK_FRAME_TICKS) % 3]; else IDLE[(tick / IDLE_FRAME_TICKS + idle_phase) % 2] with `idle_phase = combatant index`. `moving` = prev position != current position. Non-working intact Parts: frame as above, draw.c tints them GRAY (one line).

Animation clock is `scene_time` (sim ticks + alpha), so everything freezes while paused and screenshots are deterministic.

## draw.h

```c
typedef struct { Texture2D parts[MAX_PARTS], pickups[MAX_ARENA_OBJECTS], traps[MAX_ARENA_OBJECTS], effects[ELEMENT_COUNT], floor, rock, pillar; } Sheets;
void sheets_load(Sheets *sheets, const GameData *data);      /* LoadTexture("assets/<folder>/<id>.png") for the same ids as atlases_load; after InitWindow */
void sheets_unload(Sheets *sheets);
typedef struct { bool paused; bool confirm_quit; } Hud;
void draw_frame_contents(const Battle *b, const GameData *data, const Scene *scene, const Atlases *atlases, const Sheets *sheets, const Viewport *view, const Hud *hud);
```

`draw_frame_contents` (called between `BeginDrawing/EndDrawing` or inside `BeginTextureMode`):
1. `ClearBackground`.
2. `BeginMode2D((Camera2D){.offset = {width/2, height/2}, .target = {centre_x, centre_y}, .zoom = zoom})`.
3. Floor: every tile, `floor` sheet frame `tile_hash % 4` at `(column*16, row*16)`.
4. `scene_collect` → for each Drawable in order:
   - OBSTACLE: pillar if `tile_hash % 3 == 0` else rock, pivot at tile centre bottom `(column*16+8, row*16+16)`.
   - PICKUP: `idle[(tick / IDLE_FRAME_TICKS) % 2]`. TRAP: `act[0]` if `tick − trap_since < TRAP_ACT_TICKS` else `idle[0]`.
   - REMAINS: for each `parts[i]` (internal too — they spill out) draw BROKEN frame at `pos + remains_offset(index, i)` in list order.
   - COMBATANT: origin = `(x, y − body_feet_offset)`, snapped with `floorf`. Loop `body.slots` in order (far first): skip internal; `draw_part(frame, anchor, flip, tint)`. Then internal Parts: if `scene->xray` → draw idle frame with `Fade(WHITE, 0.5f)`; else if `tick − hit_since < flash_ticks` (16 for Radiance, 8 otherwise) → alpha `1 − age/flash_ticks`, Radiance drawn under `BeginBlendMode(BLEND_ADDITIVE)` and a second time at alpha/2 one px up and down (the "x-ray glow"); others normal blend.
   - `draw_part`: `src = {frame*fw, 0, flip ? -fw : fw, fh}`, `dest.x = origin.x + (flip ? -anchor_x - (fw - pivot_x) : anchor_x - pivot_x)`, `dest.y = origin.y + anchor_y − pivot_y`, `DrawTexturePro(sheet, src, {dest.x, dest.y, fw, fh}, {0,0}, 0, tint)`.
5. Effects: `BeginBlendMode(BLEND_ADDITIVE)`; for each live Effect draw `play[effect_frame]` at its position; `EndBlendMode`.
6. `EndMode2D`.
7. HUD (screen space, top `HUD_HEIGHT` strip, semi-transparent black): Pantheon row on the left half, Horde row on the right half; segments for Combatants with `spawner < 0`, width = min(120, half/count − 4): name in 10-px `DrawText`, 8-px bar filled `vitality / vitality_max` (green > 50 %, yellow > 25 %, red), dead → dark grey outline only, charmed (`charm_side != side`) → purple fill. Centre text: "PAUSED" when paused; "<Side> wins" when `battle_winner >= 0`. Bottom-left one-line key help: "Space pause  Arrows pan  -/= zoom  T x-ray  Esc quit".
8. `/* ticket 12: Clay_Raylib_Render goes here */` — leave this single comment; it is the seam.
9. Confirm dialog when `hud->confirm_quit`: dim overlay `Fade(BLACK, 0.5f)`, centred 320×120 panel, "Quit the Battle?" and "Enter / Y: quit   Esc / N: stay".

## main_game.c

Args (same parser shape as `main_headless.c`): `--seed N` (default 1), `--ticks N` (screenshot only), `--screenshot path.png`, `--xray`. Usage on anything else.

```
SetExitKey(KEY_NULL); if (screenshot) SetConfigFlags(FLAG_WINDOW_HIDDEN);
InitWindow(1280, 720, "Lord of Light " GAME_VERSION); SetTargetFPS(60);
load_game_data → atlases_load → sheets_load; battle_setup(seed); Decider reflex = reflex_decider();
viewport_init; scene_reset; scene.xray = xray_flag;
if (screenshot) { for N ticks: before_step, battle_step, after_step; render_to_png(path); CloseWindow; return 0; }
loop while !WindowShouldClose():
  input: Esc → confirm_quit = !confirm_quit (when open, Esc/N closes, Enter/Y → break); Space → paused toggle; T → xray toggle;
         arrows → viewport_pan(±PAN_SCREEN_PX_PER_SECOND * dt); '-'/'=' → viewport_zoom(∓1)   (all independent of paused)
  time: if (!paused && battle_winner < 0) { acc += min(GetFrameTime(), 0.25f); while (acc >= TICK_SECONDS) { before_step; battle_step(&battle,&data,&reflex); after_step; acc -= TICK_SECONDS; } }
        scene.alpha = acc / TICK_SECONDS;
  BeginDrawing(); draw_frame_contents(...); EndDrawing();
sheets_unload; battle_free; CloseWindow.
```
`render_to_png`: `RenderTexture2D rt = LoadRenderTexture(1280, 720); BeginTextureMode(rt); draw_frame_contents(...); EndTextureMode(); Image img = LoadImageFromTexture(rt.texture); ImageFlipVertical(&img); ExportImage(img, path); UnloadImage; UnloadRenderTexture`. Alpha is 0 in screenshot mode, so PNGs for a seed/tick are reproducible up to GPU rounding. Needs a logged-in GUI session (hidden window still needs a window server); fine on the dev Mac.

Makefile target:
```
screenshots: game
	for s in 1 2 3; do for t in 0 200 600; do $(BUILD)/game --seed $$s --ticks $$t --screenshot $(BUILD)/shots/s$${s}_t$$t.png; done; done
	$(BUILD)/game --seed 1 --ticks 200 --xray --screenshot $(BUILD)/shots/s1_t200_xray.png
```
(`ExportImage` does not create folders: `mkdir -p $(BUILD)/shots` first.)

## Tests (`tests/test_view.c`, registered in `tests/test_main.c` as `run_view_tests`)

1. `atlas_load("parts","sword_hand")` → frame_w 32, pivot (5,24), ACT first 2 count 3, BROKEN first 6; `atlas_load("effects","fire")` → PLAY count 5; `atlas_load("tiles","floor")` → VARIANTS count 4; missing id → false with err naming the path. `atlases_load` with real data succeeds.
2. Viewport: zoom 2, 1280×720: clamp gives centre_x ∈ [320, 448], centre_y ∈ [180, 332]; zoom 1 → centred (384, 256); `viewport_zoom` clamps to [1,4]; pan by +10000 ends at the max edge; `viewport_screen_x` of centre = width/2.
3. Interpolation: after `battle_setup`, set a Combatant to x=1000, `before_step`, x=3000, alpha 0.5 → 2 px; index ≥ prev_count uses current; facing flips left when dx < 0, keeps when dx == 0.
4. Events: HIT with actor_part 1, target_part 2 → `act_since[actor][1] == tick`, `hit_since[target][2] == tick`, one Effect with the element; TRAP → `trap_since[detail]`; DECISION/DEATH change nothing; `scene_after_step` consumes only new Events (`seen_events` advances, applying twice does not double-push). Ring wraps after MAX_EFFECTS pushes.
5. `part_frame`: integrity 0 → broken index; hit 1 tick ago → hurt; act 2 ticks ago → act[1]; BLOCK order → act[1]; moving mover → walk frame; otherwise idle alternates every IDLE_FRAME_TICKS. `effect_frame` returns −1 after EFFECT_TICKS.
6. `scene_collect` on a hand-built Battle: a Combatant at y=100 px sorts after a Remains at y=90 and before a pickup at y=110; dead Combatants and respawning pickups are absent; obstacles count equals the obstacle cells.
7. `body_feet_offset` for Agni (biped: legs anchor 10, pivot 9, frame 32) == 33. `tile_hash` in [0,256) and both derived choices cover all variants over the Arena. `remains_offset` stays in its bounds and is deterministic.

Not testable headlessly (reviewer looks at `make screenshots` PNGs): sprite placement/flip, flash look, HUD layout, dialog.

## Commits (in order; each leaves `make test` and `make game` green)

1. `Parse sprite atlases in the view` — `src/view/atlas.{h,c}`, Makefile `VIEW_PURE_OBJECTS` + filter-out rule + tests link, `tests/test_view.c` with test 1, `run_view_tests` in `test_main.c`.
2. `Add the Viewport with clamp, pan and zoom` — `scene.{h,c}` Viewport part, test 2.
3. `Snapshot positions between Ticks for interpolation` — Scene struct, `scene_reset/before_step/after_step` (facing only, Events next), `scene_position`, `scene_time`, test 3.
4. `Map Battle Log Events to Part animations and Element effects` — `scene_apply_event`, effects ring, `part_frame`, `effect_frame`, tests 4–5.
5. `Collect and y-sort drawables` — `scene_collect`, `tile_hash`, `body_feet_offset`, `remains_offset`, tests 6–7.
6. `Draw the Battle with raylib` — delete `arena_view.{h,c}`; `draw.{h,c}` steps 1–6 (external Parts only, no flash/xray yet) ; `main_game.c` loop with fixed step, Space, arrows, `-`/`=`, T (stored but unused yet). `make game` runs seed 1 at 2×.
7. `Add --screenshot for off-screen review renders` — args, hidden window, `render_to_png`, `screenshots` Make target. Executor attaches `build/shots/*.png` paths in the hand-back.
8. `Flash hit internal Parts, x-ray toggle, Remains heaps` — draw.c internal-Part branch (flash, Radiance additive glow, T view), grey tint for cut-off Parts, Remains scatter; regenerate screenshots.
9. `Team bars, pause and winner labels, Esc confirm dialog` — HUD + dialog + `SetExitKey(KEY_NULL)` + Clay seam comment; regenerate screenshots.
10. Hand-back per `docs/plan/executor-brief.md` (branch, SHAs, `make test`, `make asan`, `make game`, `make screenshots` exit codes, PNG paths, ambiguities).

## Decisions made here (defaults the executor follows unless the user overrides)

- Zoom is integer 1–4 (pixel-crisp), default 2; `-`/`=` step by 1.
- Esc confirm dialog is drawn with raylib in this ticket; ticket 12 may restyle it with Clay.
- Team bars show Combatants with `spawner < 0` only (minions skipped; ticket 12's panel lists everything).
- Animation clock is sim time (`tick + alpha`): idle bob and effects freeze while paused; screenshots deterministic.
- Facing: Pantheon faces right, Horde left by default; flips with horizontal movement.
- Obstacle tiles alternate rock/pillar by `tile_hash`; traps always drawn (ticket's "simple: always").
- Cut-off (intact but not working) Parts are tinted grey; destroyed Parts use the broken frame.
- Window fixed 1280×720, not resizable; `FLAG_WINDOW_HIGHDPI` off (per the Clay note).
- `--xray` is accepted alongside `--screenshot` so reviewers can see the T view without a window.
