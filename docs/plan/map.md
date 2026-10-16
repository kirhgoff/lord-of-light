# Map: Lord of Light arena

## Destination

A playable raylib arena: a random Pantheon (from JSON) fights a Power-matched Horde, every Combatant's Orders picked by Laya (Reflex as fallback), with the T panel, event log and end screen; the same Battle runs headless from a seed or a JSON state, under `make test`.

## Notes

- Vocabulary: `GLOSSARY.md`. Decisions: `docs/adr/`. Use the glossary's terms in code names.
- **Execution is carried into this map**: most tickets are `task` tickets that build a slice. Each is done by one Sonnet agent in a worktree at `~/Projects/lord-of-light-<NN-slug>` on branch `<NN-slug>`, reviewed by a separate Opus agent, then merged into `main` locally with `git merge --no-ff` (signed) and the worktree removed with `remove-worktree`.
- Every task leaves `make test` green and adds tests for its logic; `src/sim/` never includes raylib (ADR 0002).
- No comments in code unless the logic is genuinely tricky (user's global rule).
- Laya: `POST http://192.168.0.3:8000/v1/systemone`, classifier with choice questions; LAN only. ADR 0001 (async, Reflex fallback) + ADR 0003 (Laya sets Stance + Appraisals, Tactics pick Orders). Facts: `docs/research/laya.md`.
- Tracker: this folder. Ticket files carry `Type:`, `Status:` (open/claimed/resolved) and `Blocked by:` lines.

## Decisions so far

- [How Laya behaves](issues/13-laya-behaviour.md): great at Appraisals, weak at picking Orders, ~230 ms per question → ADR 0003 hybrid (Stance + Appraisals, game Tactics).
- [Skeleton](issues/01-skeleton.md): Makefile + vendored raylib 5.5 / Clay v0.14 / cJSON v1.7.19, header-tracked builds, tiny test runner.

## Not yet specified

- Power weight values and Horde size limits: tuned once `make balance` exists and real Battles can be watched.
- Tuning the Stance confidence threshold and Briefing wording once Laya drives real Battles.
- What Spawn minions look like as Bodies (fixed tiny Body Plans per spawning Part?), and a cap on their number.
- Replacing placeholder sprites with real art; whether Parts need per-direction frames.

## Out of scope

- Sound and music.
- Player control of Combatants; multiplayer.
- Linear tracking for this project.
