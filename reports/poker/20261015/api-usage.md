# Slumbot API usage check — 2026-10-15

The official Slumbot site's API panel explicitly invites researchers and
hobbyists to test their bots. It links the official sample client at
https://slumbot.com/sample_api.py and documents the same HTTPS endpoints used
by our evaluator. The panel is rendered from the site's public page bundle.
No numerical request quota was found in that panel or sample client.

The 2,000-hand style-1 evaluation began with one sequential client and a
one-second pause before each request. This was our conservative pacing choice,
not a published Slumbot quota. At the user's request, normal delay was removed
after 70 completed hands, retaining all results and starting a fresh session.
There is still only one client. Rate-limit responses honor `Retry-After`;
transient server/transport errors use exponential backoff. Ambiguous actions
are not replayed; unresolved hands and worst-case score bounds remain visible.
Six consecutive failed hand attempts stop the run.

The user subsequently requested 20–50 simultaneous games and a GitHub contact
header. The run resumed from 649 completed hands with 20 independent games in
one process, each with its own token, connection and bot RNG. Outgoing requests
identify the client as:

`PufferTank-PokerEval/1.0 (+https://github.com/FelixAllistar)`

The GitHub profile was verified against both the authenticated GitHub account
and the repository's origin remote. The restart conservatively records a
potentially unresolved in-flight hand. A progress file shows the aggregate
score and ETA; subsequent SIGINT stops finish current hands before exiting.

Library SHA-256:
`f76875ef0a369a41e6bed23f23a64a27785742f6239301fa3b436b79be8bb51a`.
Main results: `slumbot_style1_seed20261016_paced.jsonl`.
The earlier two-hand probe was stopped to check this guidance and add pacing;
it is excluded from the main evaluation.
