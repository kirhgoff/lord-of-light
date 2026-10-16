# Save and load the Battle from the keyboard

Type: task
Status: claimed
Blocked by: 03, 10

## Question

In the raylib view, let the user snapshot and restore the running Battle with `battle_save_json` / `battle_load_json` (ticket 03).

- F5 saves the current state to `saves/<YYYY-MM-DD_HH-MM-SS>.json` (create the folder; `saves/` is gitignored) and shows a short "Saved …" toast.
- F9 loads the most recent file in `saves/`; the Battle continues from it (paused if the game was paused). A failed load shows the error in a toast and leaves the running Battle untouched.
- Works while paused. The loaded file is also usable with `headless --load`.
- Test (sim side, no window): save → load → continue gives the same state as an uninterrupted run (reuse ticket 03's determinism check if it already covers this).
