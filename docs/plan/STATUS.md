# Status — read this first when picking up the work

Updated after every step by the orchestrating session. Map: `map.md`. Tickets: `issues/`.

## How to continue (orchestrator loop)

1. Pick frontier tickets: `Status: open`, every `Blocked by` ticket `resolved`. Set `Status: claimed` on main and commit.
2. Spawn one executor agent per ticket (model: sonnet) with `docs/plan/executor-brief.md` + the ticket path + branch/worktree name `NN-slug` at `~/Projects/lord-of-light-NN-slug`. Research tickets: an opus agent using the `mattpocock-skills:research` skill on branch `research/<name>`.
3. When it hands back, spawn a separate reviewer (model: opus) with `docs/plan/reviewer-brief.md`. CHANGES → send findings back to the executor, re-review if non-trivial.
4. The user pre-approved merging (2026-10-16) once review passes and these checks are green; ask only on design decisions. Verify yourself (`make test` and `make asan` in the worktree, `git log --format='%h %G? %s' main..<branch>` all `G`), then on main: `git merge --no-ff <branch>` (signed, trailer), set the ticket `Status: resolved` with an `## Answer`, add a line to map.md "Decisions so far", update this file, commit.
5. Parallel branches each register tests in `tests/test_main.c`; merge conflicts there are expected — keep both sides.
6. Remove the merged worktree: `remove-worktree --force lord-of-light-NN-slug` (user approved --force for worktrees whose branch is merged into main; it refuses otherwise because there is no remote).

## In flight

| Ticket | Branch / worktree | State |
|---|---|---|
| 23 Quality gates + Linux CI | `23-quality-gates` at `~/Projects/lord-of-light-23-quality-gates` | executor running |

## Done

- 01 Skeleton — merged.
- 13 Laya behaviour — merged; led to ADR 0003 (Stance + Appraisals).
- 02 Data schema — merged after 2 review rounds.
- 15 Content — merged.
- 03 State + headless — merged after 2 review rounds.
- 09 Placeholder sprites — merged after design re-review.
- 14 Clay+raylib research — merged (`docs/research/clay-raylib.md`).
- 17 Slot roles — merged after 2 code + 2 design review rounds.
- 21 Parallel Laya requests — closed unmerged: the server serialises inference (laya.md), no speedup measured.
- 12 Clay UI — merged after a code + design review and a design re-check. The map's destination is reached.
- 11 Laya decider — merged after one review round and three user decisions.
- 20 Sound playback — merged after one review round.
- 16 Save/load keys — merged after one review round.
- 10 raylib view — merged after a code + design review and a design re-check.
- 07 Power + Horde — merged after one review round.
- 06 Tactics + Reflex — merged after 2 review rounds.
- 08 Pickups + traps — merged after one review round.
- 05 Combat — merged after one review round (charm-lock winner rule by user decision).
- 19 Sound assets — merged after one review round.
- 18 Sprite proportions — merged after a design review.
- 04 Movement + vision — merged after 3 review rounds (incl. user decisions on footprint and charm).

## Next up (once unblocked)

- Nothing else open on the map.
- 20 Sound playback after 05, 10.
- Later: 16 Save/load keys (after 10).

## Open questions for the user

None. Answered 2026-10-16: collision is an ~11 px feet footprint with clearance-aware A*; a monster charmed to the Pantheon also stops counting as an enemy to the Pantheon (both views flip). Both folded into ticket 04 as findings 6–7.
