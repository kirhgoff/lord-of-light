# Save and load the Battle from the keyboard

Type: task
Status: resolved
Blocked by: 03, 10

## Question

In the raylib view, let the user snapshot and restore the running Battle with `battle_save_json` / `battle_load_json` (ticket 03).

- F5 saves the current state to `saves/<YYYY-MM-DD_HH-MM-SS>.json` (create the folder; `saves/` is gitignored) and shows a short "Saved …" toast.
- F9 loads the most recent file in `saves/`; the Battle continues from it (paused if the game was paused). A failed load shows the error in a toast and leaves the running Battle untouched.
- Works while paused. The loaded file is also usable with `headless --load`.
- Test (sim side, no window): save → load → continue gives the same state as an uninterrupted run (reuse ticket 03's determinism check if it already covers this).

## Answer

Merged `16-save-load-keys` after one review round. `src/view/saves.{h,c}` (raylib-free, tested): F5 writes `saves/YYYY-MM-DD_HH-MM-SS.json` (folder created, gitignored); F9 loads the greatest name of exactly that form among regular files. `battle_load_json` is atomic (scratch Battle, swap on success), so a failed load leaves the running Battle untouched; `scene_reset` follows a successful load. A wall-clock toast (2 s) shows while paused. Works while paused, ignored while the quit dialog is open; saves load with `headless --load`. Same-second saves overwrite (accepted). The toast and keys were not seen headlessly — check in the window.

## Comments

**Review 1 (opus): CHANGES** — newest save picked by `.json` suffix (a `mid.json` always won), failed-load test only covered a parse error. Fixed in be21d77; orchestrator confirmed the test goes red with a non-atomic load.
