# lord-of-light

C/raylib arena game whose units are driven by Laya deciders. Terms come from `GLOSSARY.md`; decisions live in `docs/adr/`.

## Planning and merging

- The plan is a map at `docs/plan/map.md`, with tickets in `docs/plan/issues/NN-*.md`. `docs/plan/executor-brief.md` and `reviewer-brief.md` brief the agents.
- Each ticket gets a branch and a worktree at `~/Projects/lord-of-light-<NN-slug>`, an executor, and a separate reviewer.
- Each branch gets a PR. Once CI is green, merge it into `main` locally with a signed `git merge --no-ff` whose subject ends with `(#N)`, then push `main`.
- Only the orchestrator edits ticket Status lines, on `main`.
