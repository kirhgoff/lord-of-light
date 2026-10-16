# Generated sounds as action attributes

Type: task
Status: claimed
Blocked by:

## Question

Every action has a predefined sound, generated like the placeholder sprites and named in the data.

- `tools/gen_sounds.py`: Python stdlib only (`wave`, `math`, `random` with a fixed seed), deterministic (re-running reproduces the committed files byte for byte), writes `assets/sounds/<id>.wav` — 22050 Hz, mono, 16-bit, ≤ 0.6 s, peak at −3 dBFS, short fade-out (no clicks).
- Timbre by Element, envelope by Order: Kinetic thud/whoosh, Fire crackle (filtered noise), Frost glassy chime, Lightning zap (noisy square sweep), Radiance bright bell, Venom wet hiss, Mind wobbling sine, Void low reversed swell; strike = sharp attack, cast = rising swell, block = dull clang, support = soft chime, spawn = bubbling pop.
- Data, validated by the loaders in `src/sim/data.c` (sound id must be a file in `assets/sounds/`, error otherwise):
  - every Part ability gets a required `sound`; abilities of the same Element and Order may share one;
  - each Element in `elements.json` gets `hit_sound` (played on the target) and `effect_sound` (DoT pulse, slow, chain hop, charm, strip);
  - Parts with `move_speed > 0` get `step_sound` (feet, wings, slither, levitate);
  - Pickups and Traps get `sound`; a top-level `cues` object in `elements.json` holds `death`, `part_destroyed` and `spawn_appear`.
- The sim only stores the ids; nothing in `src/sim/` plays audio (ADR 0002).
- Test: every sound id referenced by the shipped data has a WAV that parses with the expected format (like the atlas coverage test); a loader error fixture for an unknown sound id.
- Previews: `tools/gen_sounds.py --sheet` writes one WAV with every sound in sequence, so a human can listen once.
