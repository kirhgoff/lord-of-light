# Sprite proportion fixes

Type: task
Status: claimed
Blocked by: 17

## Question

From the ticket 17 design review; pixel edits in `assets/parts/` plus the data entries that use them. Check with `tools/compose_body.py`.

- `compound_eyes` is 25×21 px, wider than every Core: shrink to ≤18 px wide.
- `serpent_coil` already draws a head at its top-right: erase it so the head slot is the only head.
- `claw` (27×22) is as big as `imp_body` and the crow body (used as its beak): add a ≤12 px minion claw/beak Part and use it in the imp and crow plans.
- `glow_lantern` (19×26) is bigger than `wisp_core`: add a ≤10 px spark Part for the wisp.
- `wings_of_light` and `bat_wings` are drawn as right-pointing limbs and vanish as rear movers (Isis, Freya: 7–27 % visible): redraw as up-swept wings behind the shoulder.
- Still low-visibility after ticket 17's anchor pass (sprite shape, not anchors): Morrigan `mind_lobe` at the hip 27 %, Kali `venom_stinger` 30 %, Ra/Apollo `healing_hand` 22 % under the bow.
