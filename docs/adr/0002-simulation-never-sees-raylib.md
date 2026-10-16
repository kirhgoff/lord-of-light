# The simulation never includes raylib

Everything under `src/sim/` is plain C over JSON-serialisable state, advanced in fixed 20 Hz Ticks with one seeded RNG; only the view links raylib (vendored) and Clay. This is what lets a Battle run headless, be dumped and loaded mid-fight as JSON, and be tested from hand-written states. A reader will be tempted to put a `Vector2` in a sim struct; don't.
