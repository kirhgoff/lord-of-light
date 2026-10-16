# Status — read this first when picking up the work

Updated after every step by the orchestrating session. Map: `map.md`. Tickets: `issues/`.

## How to continue (orchestrator loop)

1. Pick frontier tickets: `Status: open`, every `Blocked by` ticket `resolved`. Set `Status: claimed` on main and commit.
2. Spawn one executor agent per ticket (model: sonnet) with `docs/plan/executor-brief.md` + the ticket path + branch/worktree name `NN-slug` at `~/Projects/lord-of-light-NN-slug`. Research tickets: an opus agent using the `mattpocock-skills:research` skill on branch `research/<name>`.
3. When it hands back, spawn a separate reviewer (model: opus) with `docs/plan/reviewer-brief.md`. CHANGES → send findings back to the executor, re-review if non-trivial.
4. Verify yourself (`make test` and `make asan` in the worktree, `git log --format='%h %G? %s' main..<branch>` all `G`), then on main: `git merge --no-ff <branch>` (signed, trailer), set the ticket `Status: resolved` with an `## Answer`, add a line to map.md "Decisions so far", update this file, commit.
5. Parallel branches each register tests in `tests/test_main.c`; merge conflicts there are expected — keep both sides.
6. Remove the merged worktree: `remove-worktree --force lord-of-light-NN-slug` (user approved --force for worktrees whose branch is merged into main; it refuses otherwise because there is no remote).

## In flight

Handover 2026-10-16: the user asked to stop starting new tasks. The agents below were left to finish; their results are recorded here. A new session continues with step 3/4 of the loop for each row, then the frontier.

| Ticket | Branch / worktree | State |
|---|---|---|
| 04 Movement + vision | `04-movement-vision` / `~/Projects/lord-of-light-04-movement-vision` | review CHANGES — findings in the ticket's `## Comments`; next: send them to a sonnet executor on this branch, then re-review |
| 17 Slot roles | `17-slot-roles` / `~/Projects/lord-of-light-17-slot-roles` | executor running |

If a session died mid-ticket: inspect the worktree (`git -C <wt> log main..HEAD`, `git status`), then either spawn a new executor to finish from there or review what exists.

## Done

- 01 Skeleton — merged.
- 13 Laya behaviour — merged; led to ADR 0003 (Stance + Appraisals).
- 02 Data schema — merged after 2 review rounds.
- 15 Content — merged.
- 03 State + headless — merged after 2 review rounds.
- 09 Placeholder sprites — merged after design re-review.
- 14 Clay+raylib research — merged (`docs/research/clay-raylib.md`).

## Next up (once unblocked)

- After 04 + 17: 05 Combat.
- Later: 16 Save/load keys (after 10).

## Open questions for the user

- Collision radius (ticket 04): keep 5 px, or a ~10–12 px feet footprint with clearance-aware A* (Combatants overlap less, can't squeeze through 1-tile gaps)? Reviewer recommends the latter.
- Charm (ticket 04/06): should a monster charmed to the Pantheon stop counting as an enemy to the Pantheon? Currently only the charmed Combatant's own view changes.
