# Pick the Decider on the lineup screen: Reflex, Laya or Jev

Type: task
Status: claimed
Blocked by: none

## Question

User request 2026-10-17: before each Battle, let the player choose who drives every Combatant: the Reflex, local Laya or TypeSafe's hosted Jev.

1. **Lineup picker.** The lineup (intro) screen shows a line `Decider: [1] Reflex  [2] Laya  [3] Jev`, with the current choice highlighted. Keys 1/2/3 select it, and Enter/Space starts the Battle with that Decider as before. The initial choice comes from `--decider` (default `reflex`). The choice persists across R (new Battle), where the lineup shows again and can be changed. The picker is not shown under `--replay`. When `.jev_key` can't be read, Jev is shown dimmed with `(no .jev_key)` and can't be selected.
2. **Laya = local first.** Reorder `data/laya.json` `urls` to `127.0.0.1:8765` (`make laya-local`) first and tomhat second, so "Laya" means local with the existing unreachable → next-URL fallback.
3. **Jev backend.** Add `jev_url` (`https://api.typesafe.ai`) and `jev_model` (`jev-latest`) to `data/laya.json` and validate them in the loader, like `urls`. Jev uses the same `/v1/systemone` request and the same Briefing, plus `"model": <jev_model>` in the body and an `Authorization: Bearer <key>` header. The key comes from `.jev_key` in the working directory (gitignored; trim whitespace) and never appears in logs, saves or error messages. The libcurl transport takes an optional bearer key at creation. The Laya Decider takes its URL list and optional model at init instead of reading `data->laya.urls` directly. Give Jev its own transport and Decider instance, created lazily on first use; Laya's stays as it is.
4. **Confidence.** Jev does not send `x_jev_confidence`. When it is absent, compute the Stance confidence from the probabilities as `(k·max p − 1)/(k − 1)` (the same formula; see `docs/research/laya.md` "confidence vs answer_confidence"), so the threshold applies to both backends. Laya replies keep using the field.
5. **Name the source.** Add `DECIDER_JEV`, so the Battle Log lines, the T panel detail line, the end-screen share ("Laya / Jev / Reflex") and saves/replay name Jev separately. Save/load round-trips it, and replay of a Jev Battle reproduces it byte-identically.
6. **CLI.** `--decider reflex|laya|jev` for both `game` and `headless`. `--dump-requests` stays Reflex-only.

Tests (no network, fake transport): the margin fallback with and without `x_jev_confidence`; the Jev request body has `model` and the Laya one doesn't; the loader rejects a bad `jev_url`/`jev_model`; `DECIDER_JEV` save/load round-trip; the picker's selection logic as a pure function if it isn't trivial. Screenshot (`--screenshot`) of the lineup with the picker at the default and a small window size.

Out of scope: tomhat as a separate option, parallel requests (ticket 21), tuning the threshold.
