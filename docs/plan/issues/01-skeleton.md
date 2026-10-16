# Skeleton: build, vendored deps, test runner

Type: task
Status: resolved
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

## Answer

Merged `01-skeleton`. Makefile builds vendored raylib 5.5 as a static lib; Clay v0.14 (+ raylib renderer, not yet compiled) and cJSON v1.7.19 vendored; per-object builds with `-MMD -MP`; `GAME_VERSION` in `src/version.h`; assert-based runner in `tests/`. Floor drawn at 22 px/tile until ticket 10's camera.
