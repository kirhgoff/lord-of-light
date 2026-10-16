---
status: accepted, supersedes the per-Part questions in ADR 0001
---
# Laya chooses a Stance from Appraisals; the game turns it into Orders

Research (`docs/research/laya.md`) showed Laya reads situations well (0.82–0.95 on "how hurt is X", "which Element hurts the ogre") but picks Orders badly (legs "advance" at every Vitality, Fire cast at a Fire-resistant ogre), and every question re-reads the Briefing (~230 ms each). So per Combatant Laya answers two Appraisal questions and one Stance choice with the rule stated in `instructions`; deterministic Tactics map Stance + Appraisals to per-Part Orders. The Reflex produces the same Stance + Appraisals from game state, so both Deciders share one Tactics path. ADR 0001's async scheduling and Reflex fallback still hold.

## Considered Options

- One Order question per Part (ADR 0001): purest test of Laya choosing, but weak margins and senseless charges.
- Appraisals only, game decides everything: most reliable, but Laya would never choose anything.
