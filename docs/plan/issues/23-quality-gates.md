# Quality gates and Linux CI

Type: task
Status: claimed
Blocked by: none

## Question

Tighten the compiler and add static checks, then run all of it on GitHub Actions on Linux for every PR and every push to `main`. One commit per item below, on one branch with one PR.

1. **Warnings.** Add `-Wpedantic -Wshadow -Wcast-align -Wconversion -Wsign-conversion` to `STRICT` (`-Wunused` is already in `-Wall`). Fix all ~22 sites the flags hit (enum-as-index sign conversions, `combat.c:108` void return, `ui.c:91` int→uint16_t, the string-literal-to-bool test asserts). Fix the code itself; use a cast only where the value is known to be in range. No `#pragma` or per-file opt-outs for our own code. Vendored code keeps `VENDOR_FLAGS`.
2. **Linux build.** The Makefile picks link flags by `uname -s`: Darwin keeps the frameworks and builds `rglfw.c` as Objective-C; Linux links `-lGL -lm -lpthread -ldl -lrt -lX11` and builds `rglfw.c` as C. Add only the feature macros glibc needs (e.g. `-D_POSIX_C_SOURCE=200809L`/`_DEFAULT_SOURCE`). `make game headless test asan` all pass on Linux with clang. LeakSanitizer is on by default on Linux: fix any real leak it finds; don't suppress it.
3. **Format.** A `.clang-format` matching the current style closely enough that the one-time reformat stays small (pick `ColumnLimit` etc. to keep the diff small, not to reflow the code). The reformat goes in its own commit. `make format` applies it, and `make format-check` fails on a diff. Both cover `src/` and `tests/` only, never `vendor/`. Pin one clang-format version for local and CI (e.g. `uv tool run clang-format==<ver>`, since uv is already used in the Makefile) so the two never disagree.
4. **clang-tidy.** A `.clang-tidy` with `bugprone-*,clang-analyzer-*,cert-*`, disabling only checks that are noise for this codebase (list each with its reason in the hand-back). `make tidy` runs it over `src/` with the real flags. Fix the real findings. Pin its version the same way as clang-format.
5. **cppcheck.** `make cppcheck` over `src/` (`--enable=warning,performance,portability --error-exitcode=1`, vendor excluded or suppressed). Fix the real findings.
6. **CI.** `.github/workflows/ci.yml` on `pull_request` and `push` to `main`, `runs-on: ubuntu-24.04`, clang from apt plus the X11/GL/curl dev packages. Jobs: `build` (`make game headless test`), `asan` (`make asan`), `lint` (`make format-check tidy cppcheck`). Actions pinned to exact versions.
7. **Pre-commit hook.** `.githooks/pre-commit` runs format-check on staged `.c`/`.h` files. Enable it with `git config core.hooksPath .githooks`, documented in the README.
8. **README.** Add the Linux requirements (apt packages) and a short "Checks" section: `make test asan format tidy cppcheck` and the hook.

Out of scope: Valgrind (ASan covers it), Flawfinder/RATS (no banned functions in use; `cert-*` covers the rest), a new test framework, coverage.

## Acceptance

- `make clean && make game headless test asan` passes with zero warnings on macOS, and also in an `ubuntu:24.04` container (podman) with the same apt packages CI installs.
- `make format-check tidy cppcheck` pass.
- The CI workflow runs green on the PR.
