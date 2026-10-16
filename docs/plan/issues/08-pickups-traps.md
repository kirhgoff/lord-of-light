# Pickups and Traps

Type: task
Status: resolved
Blocked by: 05

## Question

Implement from `data/arena_objects.json`: Pickups Soma, Mender's Moss, Ember Seed, Quickening Leaf, Eye of Far Seeing, Ward Stone; Traps Spike Pit, Frost Rune, Spore Pod, Lightning Pylon, Void Rift. Placement at Battle setup from the seed, respawn timers, Trap visibility only in the Close Band, Events for every pickup/trigger. One test per object.

## Answer

Merged `08-pickups-traps` after one review round, following `docs/plan/plan-08-pickups-traps.md`. `data/arena_objects.json` holds the ticket's 6 Pickups (soma, menders_moss, ember_seed, quickening_leaf, eye_of_far_seeing, ward_stone) and 5 Traps (spike_pit, frost_rune, spore_pod, lightning_pylon, void_rift) with generated sprites and sounds; placeholders removed. `src/sim/floor.{h,c}`: one of each placed from the seed (columns 8..39, obstacle-free 3×3, ≥3 tiles apart); Pickups taken by walking over them (lowest index wins; healing ones left if they would do nothing), Traps spring an Element hit through `combatant_hit(actor = −1)` (Lightning chains regardless of side); respawn countdown. Buffs `damage_bonus`/`haste`/`vision`/`ward` refresh rather than stack; Void strips them in that order. `EVENT_TRAP`; `ORDER_PICK_UP` removed. Old saves no longer load.

## Comments

**Plan (fable): `docs/plan/plan-08-pickups-traps.md`.** User accepted all its defaults 2026-10-16, plus walk-over Pickups for both 06 and 08 (`ORDER_PICK_UP` removed by 08).

**Review 1 (opus): CHANGES** — no functional bug; `sight_*` renamed `vision_*` (glossary), seven untested rule branches covered. Fixed in bacf745, 83612d9.
