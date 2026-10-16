# raylib view

Type: task
Status: open
Blocked by: 06, 09

## Question

`src/view/` draws the sim state (read-only) at 2× in a 1280×720 window, Drova-like: floor from above, Combatants from the side, y-sorted.

- Each Combatant drawn as its Parts at Body anchors; idle bob, act animation when a Part's Order fires, hurt flash, broken frame for destroyed Parts; dead → Remains (Parts scattered in a heap).
- Element effect sprites/particles on hits; Pickups, Traps (shown when any Combatant perceives them — simple: always), obstacles.
- Team bars at top: Pantheon and Horde, each a row of per-Combatant segments.
- Keys: Space pause, Esc → confirm dialog, arrows pan, `-`/`=` zoom (work while paused), camera clamped to the Arena.
- Sim runs at 20 Ticks/s with interpolation for drawing.
