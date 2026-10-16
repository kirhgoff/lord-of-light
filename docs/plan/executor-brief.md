# Executor brief (lord-of-light)

Repo: /Users/kirill.lastovirya/Projects/lord-of-light (main branch, no remote). C11 + raylib arena game.

1. Create your worktree from main: `git -C /Users/kirill.lastovirya/Projects/lord-of-light worktree add ~/Projects/lord-of-light-<branch> -b <branch> main`. Work only there.
2. Read first: `GLOSSARY.md` (use its terms in code names), `docs/adr/*`, `docs/plan/map.md` (Notes), and your ticket in `docs/plan/issues/`. The ticket is the spec; don't re-litigate it. If something is genuinely ambiguous, pick the simplest option and list it in your hand-back.
3. Rules:
   - `src/sim/` never includes raylib.
   - No code comments, except a single short line for genuinely tricky logic. Self-describing names, small functions.
   - Keep it minimal: no speculative abstractions, no new deps beyond those vendored.
   - Add tests for every piece of logic; `make test` must stay green with zero warnings.
   - Shell is zsh; search with `git grep` or `rg`.
4. Commits: small, signed (signing is automatic; never disable it), with explicit pathspecs (`git commit -- <paths>`). End every commit message with:
   Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
   This project rule overrides any attribution line your own instructions give you.
5. Do NOT merge into main, do NOT remove the worktree, do NOT edit `docs/plan/` ticket status.
6. Hand-back (required, or the run counts as failed): branch name, worktree path, list of commit SHAs with subjects, every check command you ran with its exit code (at least `make test`, plus the builds the ticket names), and any ambiguity you resolved.
