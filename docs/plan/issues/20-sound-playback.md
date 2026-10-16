# Sound playback relative to the viewport

Type: task
Status: claimed
Blocked by: 05, 10, 19

## Question

`src/view/sound.{h,c}` plays the ticket 19 sounds for what happens in the Battle, placed by where it happens relative to the camera.

- Source: each frame, play the sounds for Battle Log Events appended since the last frame (hit → the actor ability's `sound` at the actor, then the Element's `hit_sound` at the target; effect → `effect_sound`; Part destroyed → `part_destroyed`; death → `death`; spawn → the ability `sound` + `spawn_appear`; pickup/trap → their `sound`) and step sounds for moving Combatants (one per ~8 Ticks of movement per Combatant).
- Placement is a pure function, unit-tested without an audio device: `sound_place(world position, camera) → {volume, pan}`. Pan follows the horizontal offset from the viewport centre (−1 at the left edge, +1 at the right, clamped). Volume is 1 inside the viewport and falls off linearly to 0 at one viewport half-width outside it; zooming out widens the viewport, so more is heard and quieter.
- Voices: load each WAV once, a few `LoadSoundAlias` voices per sound (raylib 5.5); a new play steals the oldest voice; skip sounds below volume 0.05; at most ~12 sounds started per frame.
- Pause: no new sounds while paused. `M` mutes and unmutes; master volume constant in one place.
- Headless runs and `make test` never open the audio device.
- Loudness (from the ticket 19 review): every WAV peaks at −3 dBFS but RMS ranges −30 dB (`fire_hit`) to −10 dB (`lightning_cast`), and steps peak as loud as `cue_death`. Add per-category gain (steps quieter, cues louder), or switch the generator to loudness normalisation.

## Comments

**Review 1 (opus): CHANGES.** Pan conversion verified against raudio.c (`left = pan`): right of viewport → right speaker. Accepted: heal plays the support ability sound, ability chosen by Element, part_destroyed on detail ≤ 0, pickups/traps as cues, cap drops later cues. To fix: chain-lightning hops stack the cast sound (+9.5 dB), no audio device → exit 1 naming a WAV, cast branch untested, `sound_reset` not called after F9. Known limitation: a Part with both a strike and a cast of the same Element (Thor's `lightning_hammer`) always plays the strike sound, because Events don't carry the ability.
