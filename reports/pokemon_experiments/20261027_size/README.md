
## Queue change (28 Oct)
- 1024x2 skipped: already known from the giant lineage (user).
- 2048x3 skipped unless the others show promise: run it only if depth 3 helps (512x3 beats 512x2, or 1024x3 looks
  better than 1024 did) or 2048x2 beats 1024. To run it, delete h2048x3/checkpoints/pokemon/SKIPPED and start the
  queue again. Order now: 512x2 (done) -> 512x3 -> 1024x3 -> 2048x2.

## Results so far (28 Oct)
| Size | Params | Training speed | vs Kakuna, held-out A (2B, from scratch) |
|---|---|---|---|
| 512x2 | 2.75M | ~260K steps/s (2 h 25 m) | 49.2 +/- 2.5 |
| 512x3 | 3.54M | ~243K steps/s (2 h 21 m) | 46.2 +/- 2.5 |
| 1024x2 (giant lineage, not from scratch) | 9.60M | ~140K steps/s | 57.5 (4B on top of ~5B) |
| 1024x3 | - | cancelled: depth 3 does not help | - |
| 2048x2 | 35.9M | ~46K steps/s (~12 h) | running |

Depth 3 is dropped for good (user): keep x2; x1/x4/x6/x8 are open questions. Cross-size head-to-head is not
possible with ./puffer match (it forces the opponent seat to the learner architecture), so sizes are compared
against Kakuna instead.
