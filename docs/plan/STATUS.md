# Status — read this first when picking up the work

Updated after every step by the orchestrating session. Map: `map.md`. Tickets: `issues/`.

## How to continue (orchestrator loop)

1. Pick frontier tickets: `Status: open`, every `Blocked by` ticket `resolved`. Set `Status: claimed` on main and commit.
2. Spawn one executor agent per ticket (model: sonnet) with `docs/plan/executor-brief.md` + the ticket path + branch/worktree name `NN-slug` at `~/Projects/lord-of-light-NN-slug`. Research tickets: an opus agent using the `mattpocock-skills:research` skill on branch `research/<name>`.
3. When it hands back, spawn a separate reviewer (model: opus) with `docs/plan/reviewer-brief.md`. CHANGES → send findings back to the executor, re-review if non-trivial.
4. Verify yourself (`make test` in the worktree, `git log --format='%h %G? %s' main..<branch>` all `G`), then on main: `git merge --no-ff <branch>` (signed, trailer), set the ticket `Status: resolved` with an `## Answer`, add a line to map.md "Decisions so far", update this file, commit.
5. Leave merged worktrees in place (see Open questions).

## In flight

| Ticket | Branch / worktree | State |
|---|---|---|
| 02 Data schema | `02-data-schema` / `~/Projects/lord-of-light-02-data-schema` | executor running |
| 14 Clay+raylib research | `research/clay` / `~/Projects/lord-of-light-research-clay` | researcher running |

If a session died mid-ticket: inspect the worktree (`git -C <wt> log main..HEAD`, `git status`), then either spawn a new executor to finish from there or review what exists.

## Done

- 01 Skeleton — merged.
- 13 Laya behaviour — merged; led to ADR 0003 (Stance + Appraisals).

## Next up (once unblocked)

- After 02: 03 State + headless, and 15 Content (parallel).
- After 15: 09 Placeholder sprites.

## Open questions for the user

- May merged worktrees be removed with `remove-worktree --force`? It refuses because there is no remote. Until answered, keep them.
