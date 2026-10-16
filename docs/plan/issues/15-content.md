# Content: full Part library, Body Plans, five Pantheons

Type: task
Status: resolved
Blocked by: 02

## Question

Fill the data files to the full scope, schema unchanged:

- `parts.json`: ~40 Parts covering all 8 Elements and all Order kinds: hands with sword/shield/spear/bow, claws, mandibles, an electric tentacle, a venom stinger, a mind lobe, a spawning sac, legs/wings/serpent coils, eyes and heads with varied Vision, Hearts of different sizes, resistance glands per Element, armour plates, initiative ganglia. Deity-only: wings of light, third eye, radiant crown, Thor-like hammer arm, etc.
- `bodyplans.json`: biped, quadruped, serpent, blob, spider, floater, plus tiny minion plans for Spawning.
- `pantheons/`: greek, norse, celtic, hindu, egyptian; 4 Deities each, mostly humanoid, Parts chosen to symbolise the god (e.g. Agni fire hands + fire-proof gland; Thor lightning hammer; Ra radiance crown; Set void; Morrígan mind + crows via Spawning). Each Deity has a one-line `epithet` in the spirit of Zelazny's *Lord of Light*.
- All loads validated by the ticket 02 loader; a test loads every file.

## Answer

Merged `15-content`: 66 Parts (17 deity-only), 9 Body Plans (biped, quadruped, serpent, blob, spider, floater + minion_imp/crow/wisp), 5 Pantheons × 4 Deities with epithets. Pantheons load in sorted file order (Celtic first). Balance per Pantheon is within ~30% on damage, offsets in healing/speed/resistance; Indra's damage is low (12) after the vajra swap, left for ticket 07's balance run. Random slot filling is not viable yet: ticket 17.
