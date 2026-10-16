# Slot roles and fixed minion bodies

Type: task
Status: claimed
Blocked by: 15, 03, 09

## Question

Review of ticket 15 showed random slot filling can't build viable monsters: a Body Plan slot is only `external`/`internal`, so a one-heart-slot plan gets no Heart ~71% of the time (dies at spawn), ~20% of bipeds and ~50% of imps can't move, minion bodies (`imp_body`, `crow_body`, `wisp_core`) leak into big monsters, and a spawning sac can land in a minion.

Extend the schema (loader + validation + tests + data):

- Optional slot `role`: `heart` | `mover` | `attack` | `sense` | `any` (default `any`). The generator fills a slot only with Parts that fit the role (heart: has Vitality; mover: move_speed > 0; attack: has a strike/cast ability; sense: has Vision). Validation: every Body Plan has ≥1 `heart` slot and ≥1 `mover` slot (or a Core that moves).
- Optional fixed `part` on a slot: minion plans name their exact Parts, so a spawned minion is always the same body.
- Part flag `minion_only`: excluded from Horde filling (like `deity_only`).
- Update `data/bodyplans.json` and `data/parts.json` accordingly; ticket 03's placeholder Horde uses roles.
- Side-view anchors (from the ticket 09 design review): Body Plans were authored partly top-down. Re-author them for a side view where every sprite faces right: rear Parts (tails, stingers) at −x, paired Parts (spider mandibles, blob lashes, floater tentacles) at near/far depth (small x/y offset, far one drawn first) instead of mirrored ±x, and `compound_eyes`-sized heads not larger than the Core. Check by composing bodies with the reviewer's script (`tools/compose_body.py`, added by ticket 09).
- Test: for 200 seeds, every generated placeholder monster has a Heart and can move.
