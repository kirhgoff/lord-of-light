---
status: per-Part questions superseded by ADR 0003
---
# Laya picks Orders from options the game builds, asynchronously, with Reflex as the floor

Laya (System 1) is a classifier, not a generator, and answers in 0.5–6 s. So the game, not Laya, enumerates each acting Part's legal Orders (≤5 options) and sends one question per Part in a single request built from the Combatant's Briefing. Requests run on a worker thread in Initiative order; the Battle never waits for them, and a Combatant keeps its current Orders (or the Reflex's) until an answer arrives. The Reflex is a deterministic Decider that also drives every headless test, so the simulation stays reproducible from a seed plus the Battle Log.

## Considered Options

- Blocking turn-based calls in Initiative order: simple, but freezes the view for seconds per Combatant.
- Pausing the Battle clock while any request is in flight: deterministic, but the Battle crawls.
