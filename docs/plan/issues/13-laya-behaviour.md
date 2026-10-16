# How Laya behaves with game-style questions

Type: research
Status: open
Blocked by:

## Question

Against the live server (`http://192.168.0.3:8000`), find the facts ticket 11 needs: exact request/response shape of `/v1/systemone` and `/v1/systemone/batch`; warm vs cold latency for 1, 3 and 5 questions and for a batch of 8 Combatants; state length limit before truncation (`state_tokens_dropped`); how much criteria wording vs state text moves the choice (e.g. does it retreat when Vitality is low, pick a resisted Element less); whether option keys or descriptions matter. Write findings to `docs/research/laya.md` with sample requests.
