# Skeleton: build, vendored deps, test runner

Type: task
Status: claimed
Blocked by:

## Question

Set up the repo so that everything after it can just add code.

- `Makefile` (clang, `-std=c11 -Wall -Wextra -Werror`): targets `game` (raylib view), `headless`, `test`, `run`, `clean`.
- Vendor raylib 5.5 source under `vendor/raylib/` and build it as a static lib via the Makefile (macOS frameworks: Cocoa, IOKit, CoreVideo, OpenGL). Vendor `clay.h` (latest tagged release) and cJSON (`cJSON.c/.h`, tagged release) under `vendor/`. Record versions in `vendor/VERSIONS`.
- Layout: `src/sim/` (no raylib), `src/view/`, `src/main_game.c`, `src/main_headless.c`, `tests/`, `data/`, `assets/`.
- Minimal assert-based test runner (`tests/test_main.c`, one macro, no framework); one smoke test.
- `game` opens a 1280×720 window showing the empty 48×32 arena floor; `headless` prints a version line.
- `.gitignore` for build outputs.

Done when `make test`, `make headless` and `make game` all build cleanly from a fresh clone.
