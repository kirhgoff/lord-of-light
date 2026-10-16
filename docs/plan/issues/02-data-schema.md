# Data schema and loaders

Type: task
Status: resolved
Blocked by: 01

## Question

Define the JSON schemas and the `src/sim/` loaders (cJSON) for:

- `data/elements.json`: the 8 Elements (Kinetic, Fire, Frost, Lightning, Radiance, Venom, Mind, Void) with their effect parameters (DoT, slow, chain, pierce, internal-first, charm duration, buff strip).
- `data/parts.json`: the Part library. A Part: id, name, external/internal, `deity_only`, Integrity, abilities (each: Order kind it enables, Element, damage, range, cooldown in Ticks, area), resistances per Element, Vitality, Initiative, Vision band ranges, move speed, Spawning (minion Body Plan id, cap), sprite id.
- `data/bodyplans.json`: Body Plans as graphs: slots (id, external/internal, anchor offset in px for drawing) and edges; one slot is the Core.
- `data/pantheons/<name>.json`: name, 4 Deities, each a full Body (Parts placed in a graph with Core + edges + anchors).
- `data/arena_objects.json`: Pickups and Traps with effect parameters and respawn timers.
- `data/power.json`: Power weights.

Ship a small but valid sample of each (≥10 Parts, 3 Body Plans, the Hindu Pantheon, 2 Pickups, 2 Traps). Loader validates references (unknown part id, edge to missing slot, no Core) and reports file+path on error. Tests cover a valid load and each validation error.

## Answer

Merged `02-data-schema`. Structs + loaders in `src/sim/data.{h,c}` (fixed arrays, ids resolved to indices; MAX_PARTS 128, MAX_SLOTS 24, MAX_PANTHEONS 8). Every load is all-or-nothing (scratch copy). Shapes: Body Plan `{id, core, slots:[{id,kind,x,y}], edges:[[a,b]]}`; Deity `{name, epithet?, body:{core, parts:[{id,part,x,y}], edges}}`; resistances are percent -100..100 keyed by Element id; Vision `{close,medium,far}` ascending; support abilities heal via non-negative `heal`. Validation rejects unknown refs, duplicates, self-loop/duplicate edges, slots unreachable from the Core, non-integers, out-of-range numbers, zero Pantheons. Sample data: 22 Parts, 3 Body Plans, Hindu Pantheon.
