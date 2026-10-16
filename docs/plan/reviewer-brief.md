# Reviewer brief (lord-of-light)

You review one ticket's branch, in its existing worktree. You did not write it.

1. Read `GLOSSARY.md`, `docs/adr/*`, `docs/plan/map.md` Notes, the ticket file, then `git diff main...<branch>` (skip vendored files under `vendor/` except to check what was vendored and its versions).
2. Attack the change: build and run every check yourself (`make clean && make test`, plus the builds/runs the ticket names). Every finding must name a concrete input or step and the wrong result it produces. Check: the ticket's acceptance criteria are all met; `src/sim/` never includes raylib; no code comments beyond single short lines on tricky logic; no speculative abstractions; terms match the glossary; tests actually test the logic (for a key test, break the code it covers, confirm it goes red, restore).
3. Don't fix anything and don't commit. Don't merge. Write probe files and harnesses only in your scratchpad, never in the main repo or the worktree.
4. Hand back: verdict PASS or CHANGES, then the findings ranked by severity (file:line, failing input, wrong result, suggested fix), and every command you ran with its exit code. PASS means "attacked and it held".
