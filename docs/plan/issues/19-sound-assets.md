# Generated sounds as action attributes

Type: task
Status: resolved
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

## Answer

Merged `19-sound-assets` after one review round. `tools/gen_sounds.py` (stdlib, deterministic, `--sheet` local preview, gitignored) writes 41 WAVs to `assets/sounds/` (588 KB): ability sounds `<element>_<order>` shared per pair, `<element>_hit`/`_effect`, `step_feet|levitate|slither|wings`, pickups, traps, cues `cue_death|cue_part_destroyed|cue_spawn_appear`. Data: `Ability.sound`, `ElementDef.hit_sound/effect_sound`, `Part.step_sound` (required when move_speed > 0), `ArenaObjectDef.sound`, `GameData.cues`. The loader checks only the id format `[a-z0-9_]+` (no filesystem access); `test_every_sound_id_has_a_wav` checks the files. Tests exit cleanly (`LOAD_OR_EXIT`) when data can't load.

## Comments

**Review 1 (opus): CHANGES** — silent test-runner segfault on a missing WAV, path traversal in ids, loader depended on CWD, start clicks, Void lacked a reversed swell, 1 MB sheet committed. Fixed in 2d30a1e, 4064792. Loudness spread moved to ticket 20.
