# Clay UI: T panel, event log, end screen

Type: task
Status: resolved
Blocked by: 10, 11, 14

## Question

Using Clay with the raylib renderer (see ticket 14):

- T toggles: per-Combatant overlay (name, Parts with Integrity, key properties) and a right-side panel with a scrolling Battle Log; selecting/hovering a Combatant shows its last Briefing and Decision.
- End screen when a side wins: winner, death log (who killed whom with which Part/Element, when), top damage dealer, most-destroyed Part kind, longest survivor, Pickups taken, top Element by damage, Laya vs Reflex share and mean confidence.

## Answer

Merged `12-clay-ui` after one code + design review round and a design re-check, following `docs/plan/plan-12-clay-ui.md` (decision 8 replaced by the user: compact overlays). Clay v0.14 compiles only in `src/view/ui.c` (`-isystem vendor/clay`), drawn after the HUD and below the raylib Esc dialog. T = tech view: x-ray, compact overlays (label, Vitality, Stance/statuses; Parts for the hovered/selected Combatant), a floating right panel with the stateless Battle Log (newest 200 lines, follows the tail) and the detail section (Decision line + Briefing: Laya's last or a live one); the camera insets left of the panel. Pure, tested: `report.c` (numbered labels "floater 1/2", one line per Event, Battle report), `frame_text.c`, `viewport_world_x/y`, `scene_pick`. End screen: winner, top damage dealer, most destroyed Part, last to fall, Pickups, top Element, Laya/Reflex share, death log; R = new Battle with seed + 1 (not under `--replay`). R and F9 reset the Laya back-off and Briefings. Screenshots export opaque, so they match the live window. Hover, click, scroll and R were not exercised interactively.

## Comments

**Plan (fable): `docs/plan/plan-12-clay-ui.md`.** User accepted all 11 defaults 2026-10-16.

**Review 1 (fable design + opus code): CHANGES.** Held: font legibility, labels, log colours/wrap/tail, detail section, end-screen stats, Clay limits at 64 Combatants, hostile Event indices under asan, picking at every zoom, per-glyph letterSpacing (verified against raylib and clay.h). To fix: screenshots exported translucent alpha (misleading colours), overlays over the panel, panel hiding the Arena's right edge (camera inset in T view), fixed-height death log, frame scratch overflow reads out of bounds, Laya offline/briefing state survives R and F9, per-frame label waste, F9 keeps the old selection. **User decision 2026-10-17:** compact overlays (label, Vitality, Stance) for everyone; Parts only for the hovered/selected Combatant — replaces plan decision 8.

**Fixes** in 74b1dc1..9bc4957; **design re-check (fable): APPROVE**.
