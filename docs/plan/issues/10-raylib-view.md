# raylib view

Type: task
Status: resolved
Blocked by: 06, 09

## Question

`src/view/` draws the sim state (read-only) at 2× in a 1280×720 window, Drova-like: floor from above, Combatants from the side, y-sorted.

- Scale (user decision): 32-px Part frames, 16-px floor tiles, 32-px rocks / 32×64 pillars, 24-px Pickups and Traps; Combatants stand ~4 tiles tall.
- Internal Parts (user decision): hidden on living Combatants; flash through the body when that Part is hit (x-ray look, strongest for Radiance); always shown semi-transparent in the T view; spill out in Remains.
- Each Combatant drawn as its External Parts at Body anchors; idle bob, act animation when a Part's Order fires, hurt flash, broken frame for destroyed Parts; dead → Remains (Parts scattered in a heap).
- Element effect sprites/particles on hits; Pickups, Traps (shown when any Combatant perceives them — simple: always), obstacles.
- Team bars at top: Pantheon and Horde, each a row of per-Combatant segments.
- Keys: Space pause, Esc → confirm dialog, arrows pan, `-`/`=` zoom (work while paused), camera clamped to the Arena.
- Sim runs at 20 Ticks/s with interpolation for drawing.

## Answer

Merged `10-raylib-view` after one code + design review round and a design re-check, following `docs/plan/plan-10-view.md`. `src/view/atlas` (sidecar parsing, strictly validated) and `src/view/scene` (Viewport, prev/current position snapshot, Event→animation, effects ring, y-sort) build without raylib and are unit-tested; `src/view/draw` does all raylib drawing; `src/main_game.c` runs a fixed 20 Hz loop with interpolation and `--seed/--ticks/--screenshot/--xray`; `make screenshots` renders review PNGs. Camera: integer zoom 1–4, **starts at 1×** (user decision), HUD-aware clamp. Internal Parts flash on hit, semi-transparent in the T view; Remains heaps; team bars without minions; Esc confirm dialog in raylib. Missing sprite sheets or bad sidecars exit 1 naming the file. Ticket 20 reads `Viewport` from `scene.h`; ticket 16 must call `scene_reset` after a load; ticket 12 draws Clay after `draw_hud`.

## Comments

**Plan (fable): `docs/plan/plan-10-view.md`.** User accepted all defaults 2026-10-16: integer zoom 1–4 (default 2), Esc dialog in plain raylib now, team bars without spawned minions, animation clock on sim Ticks, grey tint for cut-off Parts, `--xray` with `--screenshot`.

**Review 1 (opus code + fable design): CHANGES.** Held: ADR 0002, Event cursor across realloc, deterministic screenshots, accumulator cap, clamp at every zoom, x-ray flash, Esc dialog, y-sort in melee piles (pile accepted for placeholder art). To fix: start zoom (**user decision 2026-10-16: start at 1×**, integer 1–4 kept), HUD-aware camera, effects at body height, OOB effect element from a loaded save (+ sim loader validation), missing PNG fails silently, lax sidecar validation, two untested contracts, effect position from stale alpha, seam comment, strict `--ticks`. Ticket 12 note: Horde bar labels repeat plan ids ("floater floater"), indistinguishable.

**Fixes** in 1f90d65..acd7a57; **design re-check (fable): APPROVE**.
