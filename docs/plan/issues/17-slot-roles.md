# Slot roles and fixed minion bodies

Type: task
Status: resolved
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

## Answer

Merged `17-slot-roles` after 2 code and 2 design review rounds. Slot `role` (heart/mover/attack/sense/any; attack excludes Parts with Vision), fixed slot `part`, Part `minion_only`. Horde filling skips Core Parts, `deity_only` and `minion_only`. The loader rejects unfillable Horde slots, flagged fixed Parts, spawn plans with non-fixed slots and data without a Horde plan. Body Plans and Deities re-anchored for a right-facing side view: far Parts listed first, far hands at (6, 4), signature weapons in the near hand. Saves embed whole Bodies, so old saves still load.

## Comments

**Executor hand-back (363786d), not yet reviewed.** `make test` and `make asan` green. Slot `role` (any/heart/mover/attack/sense) + fixed `part` + Part `minion_only`; `part_fits_role` in data.c; Horde fill uses roles (slot-id rule removed); all Body Plans and 20 Deities re-anchored for side view (far Parts listed first; floater tentacles renamed `tentacle_far/mid/near`; Odin's Gungnir now in the near hand). Things the reviewer should check:
- The 200-seed test was not shown to go red with roles reverted.
- `random_part` has no guard for an empty eligible pool.
- Role `any` still allows odd fills (e.g. `serpent_coil` in a legs slot).
- Visual leftovers: imp/crow claws and wisp lantern oversized; `compound_eyes` head wider than a biped torso; serpent_coil sprite already has a head, so the separate head Part overlaps. Regenerate previews with `tools/compose_body.py` (a Fable reviewer should look at them).
- Old saves with old slot ids no longer load (accepted).

**Review 1 (opus code) of 363786d: CHANGES.** Role filter verified (200-seed test goes red with it reverted); `make test`/`make asan` green.
1. Empty eligible pool: `random_part` (`battle.c:103-109`) calls `rng_below(…, 0)` — e.g. Horde Hearts all `deity_only` leaves heart slots empty; loads fine, then UB (div by zero under asan, garbage index otherwise). Fix: move `fills_horde_slot` into data.c and fail in `resolve_body_plan` for any non-fixed slot of a non-spawn plan no Part fills; error fixture.
2. Core Parts with move_speed (`serpent_coil`, `gas_bladder`) fill mover slots (seed 1 floater levitator = serpent_coil; seed 3 serpent tail = serpent_coil); quadruped `tail` is `mover` so legs/wings land at the rear; attack pool admits `fanged_head`. Fix: exclude Parts used as fixed Cores from Horde filling; quadruped tail → `attack`.
3. `resolve_fixed_part` ignores flags: reject `minion_only` fixed Parts in non-spawn plans and `deity_only` fixed Parts in all Body Plans.
4. Info: old saves still load (Bodies are embedded whole) — better than the hand-back said.
5. GLOSSARY "Body Plan" still says all slots are filled randomly; mention fixed Parts.

**Review 1 (fable design): CHANGES.** Facing and rear Parts fine; far Parts are buried (12 of 20 Deity far-hand weapons ≤10 % visible). Data-only fixes:
6. Biped and all 20 Deities: `arm_far` → (6, 4); signature weapon moves to the near slot (3, −5), support item to far: Dagda war_club/healing_hand; Lugh lugh_spear/shield_hand; Ra, Apollo sun_bow/healing_hand; Set, Hades, Kali void_maw/(serpent_hand, void_tendril, venom_stinger); Zeus thunderbolt_hand/sword_hand; Indra thunderbolt_hand/shield_hand; Poseidon frost_trident/frost_fang; Thor lightning_hammer/shield_hand; Brigid, Loki hearth_hand/(healing_hand, serpent_hand); Isis healing_hand/shield_hand. Freya, Odin, Morrigan, Agni, Anubis keep their near Part.
7. Anubis `mind_lobe`: list first, (−6, −14). Shiva: `mind_lobe` to far (−6, −14) listed first, healing_hand near (3, −5).
8. Blob `lash_far` → (−2, −6), `lash_near` → (4, 4). Floater `tentacle_far` → (−3, 2), `tentacle_near` → (3, 13). Spider `mandible_far` → (10, −4), `mandible_near` → (11, 3). minion_crow `wing` → (−6, −14).
Sprite redraws split to ticket 18. Previews: scratchpad `bodies/` (volatile).

**Review 2 (opus code): CHANGES (minor)** — fanged_head in arm slots, spawn plans not checked fixed, no-Horde-plan div by zero; fixed in 3a3d14e. **Design re-check (fable): APPROVE** — 19/20 signature weapons 100 % visible, Dagda 66 %.
