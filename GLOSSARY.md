# Lord of Light

An arena where a Pantheon of self-styled gods fights a Horde of monsters. Each Combatant's body is a graph of Parts that determines everything it can do. Inspired by Zelazny's *Lord of Light*, where mutants with engineered powers took the names of gods.

## Language

### Sides

**Pantheon**:
The god side of a Battle: a fixed set of four Deities drawn from one mythology (Greek, Norse, Celtic, Hindu or Egyptian), loaded from an editable file.
_Avoid_: Gods team, deity squad

**Deity**:
A mostly humanoid Combatant of a Pantheon whose Parts symbolically represent a god, such as a fire-wielder calling themself Agni.
_Avoid_: God, hero

**Horde**:
The monster side of a Battle, generated at random to match the Pantheon's Power.
_Avoid_: Monster group, enemies

**Combatant**:
Any character in the Arena, whether Deity or monster.
_Avoid_: Character, unit, creature, actor

### Bodies

**Body**:
A Combatant's graph of Parts, in which some vertices hold External Parts and others hold Internal Parts.
_Avoid_: Skeleton, rig

**Part**:
A single node of a Body, such as a sword hand, an electric tentacle, a fire-proof gland or a heart, that grants abilities or properties and takes its own damage.
_Avoid_: Limb, organ, component (except to say whether a Part is external or internal)

**External Part**:
A Part on the outside of the Body that acts or perceives, such as a hand, leg, head, tentacle or wing.
_Avoid_: Limb

**Internal Part**:
A Part inside the Body that gives passive properties, such as a heart for health or an organ that resists an Element.
_Avoid_: Organ

**Core**:
The root Part of every Body. A Part cut off from the Core by destroyed Parts stops working.
_Avoid_: Torso, root

**Heart**:
An Internal Part that adds to the Combatant's Vitality.

**Integrity**:
How much damage a single Part can take before it is destroyed.
_Avoid_: Part HP

**Vitality**:
A Combatant's life pool, fed by its Hearts; the Combatant dies when it reaches zero or every Heart is destroyed.
_Avoid_: HP, health

**Body Plan**:
The shape of a monster Body: a graph of External and Internal slots. The Horde generator fills each empty slot with a random Part that fits its role; slots with a fixed Part (e.g. the Core, minion bodies) keep it.
_Avoid_: Template, archetype

**Remains**:
The heap of Parts left in the Arena by a dead Combatant.
_Avoid_: Corpse

### Elements

**Element**:
A kind of damage and resistance with its own look on screen: Kinetic, Fire, Frost, Lightning, Radiance, Venom, Mind or Void.
_Avoid_: Damage type, school

**Spawning**:
An ability of some Parts to summon minor Combatants (Spawn) onto their own side. It is an ability, not an Element.
_Avoid_: Summoning element

### Battle

**Battle**:
One match, from release into the Arena until one side has no living Combatants.
_Avoid_: Match, round, game

**Arena**:
The walled field where a Battle takes place.
_Avoid_: Map, level

**Pickup**:
An object lying in the Arena that a Combatant can take, such as a healing item or a power-up.
_Avoid_: Item, loot

**Trap**:
An object in the Arena that harms or hinders whoever triggers it.

**Power**:
A score computed from a Body's Parts that measures how strong a Combatant is, used to balance the Horde against the Pantheon.
_Avoid_: Strength, level, CR

**Initiative**:
A property granted by Parts that sets the order in which Combatants decide their next actions.

**Vision**:
What a Combatant perceives within line of sight, in three Bands: Close (everything), Medium (kind, side, rough health) and Far (only that an enemy or a healing Pickup is there), with ranges set by its Parts.
_Avoid_: Sight, perception

**Charm**:
The effect of a Mind hit: for a while the charmed Combatant counts the charmer's side as its own.
_Avoid_: Mind control, conversion

**Tick**:
One fixed step of Battle time; there are 20 per second.
_Avoid_: Frame, turn

**Battle Log**:
The ordered record of every Event in a Battle, including every Decision made, enough to replay it.
_Avoid_: History

**Event**:
One thing that happened in a Battle, such as a hit, a pickup, a Decision or a death.

### Deciding

**Decision**:
A Decider's answer for one Combatant: its Stance plus its Appraisals, which Tactics turn into Orders.
_Avoid_: Action, turn

**Stance**:
A Combatant's overall intent until its next Decision: fight, retreat, support or collect.
_Avoid_: Mode, goal, plan

**Appraisal**:
A Decider's judgement of one fact about the situation, such as how hurt a Combatant is or which Element hurts a target most.
_Avoid_: Assessment, observation

**Tactics**:
The game's fixed rules that turn a Stance and Appraisals into an Order for each acting Part.
_Avoid_: Policy, behaviour tree

**Order**:
What one Part is told to do until the next Decision: strike, cast, block, move to, pick up, support, spawn or idle, with a target where needed.
_Avoid_: Command, action

**Decider**:
Whatever produces Decisions: either Laya or the Reflex.
_Avoid_: AI, brain, mind (Mind is an Element)

**Briefing**:
The short English text describing what a Combatant perceives and its own condition, sent to Laya and shown in the technical panel.
_Avoid_: Prompt, state, observation

**Laya**:
The external decision model, reached through its System 1 interface, that answers Appraisals and chooses a Stance from a Combatant's Briefing.
_Avoid_: LLM

**Reflex**:
The built-in, deterministic Decider that derives Stance and Appraisals straight from the Battle state; used in tests, when Laya is unreachable or unsure, and while a Combatant waits for its next Decision.
_Avoid_: Fallback AI, heuristic
