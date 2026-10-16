# Clay UI: T panel, event log, end screen

Type: task
Status: claimed
Blocked by: 10, 11, 14

## Question

Using Clay with the raylib renderer (see ticket 14):

- T toggles: per-Combatant overlay (name, Parts with Integrity, key properties) and a right-side panel with a scrolling Battle Log; selecting/hovering a Combatant shows its last Briefing and Decision.
- End screen when a side wins: winner, death log (who killed whom with which Part/Element, when), top damage dealer, most-destroyed Part kind, longest survivor, Pickups taken, top Element by damage, Laya vs Reflex share and mean confidence.

## Comments

**Plan (fable): `docs/plan/plan-12-clay-ui.md`.** User accepted all 11 defaults 2026-10-16.

**Review 1 (fable design + opus code): CHANGES.** Held: font legibility, labels, log colours/wrap/tail, detail section, end-screen stats, Clay limits at 64 Combatants, hostile Event indices under asan, picking at every zoom, per-glyph letterSpacing (verified against raylib and clay.h). To fix: screenshots exported translucent alpha (misleading colours), overlays over the panel, panel hiding the Arena's right edge (camera inset in T view), fixed-height death log, frame scratch overflow reads out of bounds, Laya offline/briefing state survives R and F9, per-frame label waste, F9 keeps the old selection. **User decision 2026-10-17:** compact overlays (label, Vitality, Stance) for everyone; Parts only for the hovered/selected Combatant — replaces plan decision 8.
