# Lord of Light

An arena where a Pantheon of self-styled gods fights a Power-matched Horde of monsters. Every Combatant's body is a graph of Parts, and its Orders come from Laya (System 1 classifier) or the built-in Reflex. Written in C11 with raylib and Clay. Terms: [GLOSSARY.md](GLOSSARY.md).

## Requirements

- macOS with `clang` and `make` (the Makefile links Cocoa/OpenGL frameworks), or Linux (below)
- Linux: `clang`, `make`, and `sudo apt-get install libcurl4-openssl-dev libx11-dev libgl1-mesa-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev`
- libcurl (ships with macOS)
- Optional: [`uv`](https://docs.astral.sh/uv/) to run a local Laya server; required for `make format`, `format-check` and `tidy`, which run pinned clang-format and clang-tidy through it
- `make cppcheck` needs `cppcheck` (`brew install cppcheck` or `apt-get install cppcheck`)

raylib 5.5, Clay v0.14 and cJSON are vendored in `vendor/`, so nothing else needs installing.

## Run

```sh
make run                                   # build and open the arena (seed 1, Reflex decider)
build/game --seed 42                       # another Battle
build/game --decider laya                  # Laya picks Stance + Appraisals
```

The game decides with the Reflex unless you pass `--decider laya`. Laya servers are listed in `data/laya.json`: first `127.0.0.1:8765`, then the LAN host. To run one locally:

```sh
make laya-local                            # laya-serve on 127.0.0.1:8765, CPU
```

If Laya is unreachable or slow, the Reflex takes over.

### Controls

| Key | Action |
|---|---|
| Enter / Space | Lineup screen: begin the Battle |
| Space | Pause / resume |
| Arrows | Pan |
| `-` / `=` | Zoom out / in |
| Mouse hover / click | Show a Combatant's Briefing and Decision |
| T | Tech view: overlays and the Battle Log panel |
| M | Mute |
| F5 / F9 | Save to `saves/` / load the newest save |
| R | After a Battle ends: start a new one (seed + 1) |
| Esc | Quit (confirm with Enter/Y, cancel with N) |

### Other game flags

```
build/game [--seed N] [--ticks N --screenshot out.png] [--xray] [--select N]
           [--decider laya|reflex|jev] [--lineup] [--replay dump.json]
```

`--replay` plays a dumped Battle back from its log of Decisions.

## Headless

```sh
make headless
build/headless --seed 1 --ticks 300 --dump mid.json   # run and save state
build/headless --load mid.json --ticks 300             # continue from a state
build/headless --replay mid.json                       # replay its Decisions
build/headless --seed 1 --ticks 300 --dump-requests build/requests-seed1.jsonl   # Decision request bodies + Reflex answers
uv run --python 3.12 python tools/bench_jev_vs_laya.py --corpus build/requests-seed1.jsonl --size 60   # benchmark Jev vs Laya
```

## Development

```sh
make test          # unit tests
make asan          # tests + headless runs under ASan/UBSan
make screenshots   # PNGs of a few seeds in build/shots/
make balance       # Power balance report
```

### Checks

```sh
make test asan format-check tidy cppcheck   # what CI runs on every PR and push to main
make format                                  # apply clang-format to src/ and tests/
git config core.hooksPath .githooks          # pre-commit: format-check on staged .c/.h files
```

- Game data (Parts, Body Plans, Pantheons, Elements, Power weights) is JSON in `data/`
- `src/sim/` is the simulation and never includes raylib ([ADR 0002](docs/adr/0002-simulation-never-sees-raylib.md)); `src/view/` draws it; `src/laya/` is the Laya decider
- Decisions are in `docs/adr/`, research in `docs/research/`, and the plan and tickets in `docs/plan/`
