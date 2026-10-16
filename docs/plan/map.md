# Map: Lord of Light arena

## Destination

A playable raylib arena: a random Pantheon (from JSON) fights a Power-matched Horde, every Combatant's Orders picked by Laya (Reflex as fallback), with the T panel, event log and end screen; the same Battle runs headless from a seed or a JSON state, under `make test`.

## Notes

- Vocabulary: `GLOSSARY.md`. Decisions: `docs/adr/`. Use the glossary's terms in code names.
- **Execution is carried into this map**: most tickets are `task` tickets that build a slice. Each is done by one Sonnet agent in a worktree at `~/Projects/lord-of-light-<NN-slug>` on branch `<NN-slug>`, reviewed by a separate Opus agent, then merged into `main` locally with `git merge --no-ff` (signed) and the worktree removed with `remove-worktree`.
- Every task leaves `make test` green and adds tests for its logic; `src/sim/` never includes raylib (ADR 0002).
- No comments in code unless the logic is genuinely tricky (user's global rule).
- Laya: `POST http://192.168.0.3:8000/v1/systemone`, classifier with choice questions; LAN only. ADR 0001 (async, Reflex fallback) + ADR 0003 (Laya sets Stance + Appraisals, Tactics pick Orders). Facts: `docs/research/laya.md`.
- Orchestration state and how to resume: `STATUS.md`. Agent briefs: `executor-brief.md`, `reviewer-brief.md`.
- Tracker: this folder. Ticket files carry `Type:`, `Status:` (open/claimed/resolved) and `Blocked by:` lines.

## Decisions so far

- [Sound playback](issues/20-sound-playback.md): pure placement/mapping module + thin raylib player; per-category gains; silent without an audio device.
- [Save/load keys](issues/16-save-load-keys.md): F5/F9 to `saves/` by timestamp name; loads are atomic.
- [raylib view](issues/10-raylib-view.md): pure scene/atlas modules + one raylib draw module; integer zoom starting at 1×; review via `--screenshot` PNGs.
- [Power and Horde](issues/07-power-and-horde.md): Power is mostly damage/heal per time; spawners count their minions; Hordes land within ±10 % of the Pantheon.
- [Tactics and Reflex](issues/06-reflex-and-scheduling.md): Tactics every Tick from a stored Decision; the scheduler and Laya own only the Decision; seek by true position, attack only what is perceived.
- [Pickups and Traps](issues/08-pickups-traps.md): walk-over Pickups, Traps reuse the combat hit path, buffs are sibling fields that refresh, one of each object per Battle.
- [Combat](issues/05-combat-elements.md): data-driven Element effects, 50 % Vitality share, death by Vitality or no working Heart, a side all charmed to the enemy loses.
- [Sound assets](issues/19-sound-assets.md): generated WAVs named per action in the data; the sim validates id format only, asset tests check the files.
- [Sprite proportions](issues/18-sprite-proportions.md): minion bodies use their own small Parts (`minion_claw`, `wisp_spark`); shared sprites are tuned by anchors, not per-Deity pivots.
- [Movement + vision](issues/04-movement-vision.md): 11 px feet footprint, A* on 2x2 footprint anchors (gaps ≥2 tiles), grid LOS blocks diagonal corner crossings, charm flips enemy/ally in both views.
- [Slot roles](issues/17-slot-roles.md): slots carry roles and optional fixed Parts; Cores and minion/Deity Parts never fill Horde slots; side-view anchors with far Parts drawn first.
- [Clay with raylib](issues/14-clay-raylib.md): pin v0.14, custom text measure, scroll/overlay pitfalls in `docs/research/clay-raylib.md`.
- [How Laya behaves](issues/13-laya-behaviour.md): great at Appraisals, weak at picking Orders, ~230 ms per question → ADR 0003 hybrid (Stance + Appraisals, game Tactics).
- [Placeholder sprites](issues/09-placeholder-sprites.md): generated pixel art; 32-px Parts, scaled-up world objects, internal Parts hidden except on hit / T view.
- [State and headless](issues/03-state-and-headless.md): Battle structs, milli-pixel positions, heap Battle Log, id-based saves, `make asan`.
- [Content](issues/15-content.md): 66 Parts, 9 Body Plans, 5 Pantheons; slot filling needs roles (ticket 17).
- [Data schema](issues/02-data-schema.md): all-or-nothing validating loaders in `src/sim/data.c`; shapes listed in the ticket's Answer.
- [Skeleton](issues/01-skeleton.md): Makefile + vendored raylib 5.5 / Clay v0.14 / cJSON v1.7.19, header-tracked builds, tiny test runner.

## Sound

User request 2026-10-16: generated sounds for every action, stored as data attributes (ticket 19), played relative to the viewport (ticket 20).

## Not yet specified

- Power weight values and Horde size limits: tuned once real Battles can be watched. User 2026-10-16: the Pantheon winning more often is fine; don't tune to even out win rates.
- Tuning the Stance confidence threshold and Briefing wording once Laya drives real Battles.
- Replacing placeholder sprites with real art; whether Parts need per-direction frames.

## Out of scope

- Music.
- Player control of Combatants; multiplayer.
- Linear tracking for this project.
