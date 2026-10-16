# raylib view

Type: task
Status: claimed
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
