# Gen 3: what went wrong and the plan (2026-11-07)

## What went wrong
Gen 3 was built by porting `ocean/pokemon9` (a quick Gen 9 env with a flat observation) instead of Gen 1's network.
Every Gen 3 run used PufferLib's default encoder:
- one linear layer from 5,175 (later 5,343) flat floats into the hidden state;
- a linear head with one logit per action slot.

Gen 1 took weeks to get a good network, and its runs used the structured network in `ocean/pokemon/pokemon_encoder.cu` +
`semantic_contract.h`:
- **Moves:** move identity + 152 mechanics features (static table) -> shared move MLP -> move embedding.
- **Pokemon:** species identity + 176 species features + the mean of its move embeddings + 96 dynamic features
  (HP, status, boosts, active, PP...) -> shared Pokemon MLP. Twenty entities share the weights.
- **Fusion:** 20 Pokemon embeddings + 160 global features -> MLP -> MinGRU core.
- **Pointer decoder:** a move action is scored as query . (that move's embedding) plus per-action features (STAB,
  type effectiveness vs the foe's types from the type chart, PP). A switch is scored as query . (that Pokemon's
  embedding) plus its features. The network knows *which* move or Pokemon an action means. With a flat head, "action
  2" means "whatever is in slot 2", and it has to learn that routing for every slot.

None of that was carried over. The gap was never measured: no scripted baseline ran from step 0, and self-play and
vs-random scores look fine for weak models. The result: 4 hours and a 1024x2 run to reach 55% vs a max-damage bot.

## Numbers so far (in-engine, held-out OU teams unless noted)
| model | vs max-damage | vs Kakuna (30 games, Showdown) |
|---|---|---|
| omni 4B, flat obs | 55% (policy; magnet not scored) | 3 / 240 over the run |
| matchup 1024x2 2.36B, hand features (scored by another session) | 78.7% policy / **80.3% magnet** | 8 / 30 (2.1B) |
| poke-engine MCTS, 200 iterations (7 ms/turn, full info, randbat teams) | **86%** | - |
| poke-engine MCTS, 2,000 iterations (21 ms/turn) | 87% | - |
| max-damage vs random | 99.3% | - |

A cheap search with no learning beats our best network. Under the Gen 1 recipe, the network should beat it.

## Plan, in order
1. **Structured encoder + pointer decoder for Gen 3** (`ocean/pokemon3`, env-local; trainer core only through the
   existing encoder/decoder hooks).
   - Works on the current observation layout (12 Pokemon x 404 + matchup block), so the env and bridge barely change.
   - Moves: shared MLP over the 70 move features + its 3 matchup values -> move embedding (48 slots).
   - Pokemon: shared MLP over the Pokemon features (no move block) + mean of its move embeddings + its 2 defensive
     matchup values + side flag -> Pokemon embedding (12).
   - Fusion: 12 embeddings + both sides' 158 side features + 11 field features -> MLP -> hidden.
   - Decoder: battle actions 0-3 = query_move . (active Pokemon's move embedding i) + bias; 4-9 = query_switch .
     (own Pokemon embedding j) + bias. The draft keeps a linear head (species 0-127, sets 0-5).
   - CPU forward with a parity test against the CUDA path (Gen 1 did this: `semantic_cpu.h`), so the Showdown policy
     server can run it.
2. **Species identity and set priors** (next obs change, after 1 is measured).
   - Gen 3 shows species only through stats and types. Unrevealed opponent moves and items are blank.
   - Add a species id and, per Pokemon, the usage prior over its common moves and items from the 172K-team pool (what a
     player knows: "Skarmory usually has Spikes and Whirlwind").
3. **Eval ladder from step 0, policy and magnet both:**
   - random, max-damage, MCTS-200 and MCTS-2000, in-engine (`env.bot_policy` 2 now; 3 = MCTS to add);
   - Kakuna every 1B.
   - Early-stop rule: a run that is not above max-damage by 500M steps is broken.
4. **Search as a teacher (optional, after 1).**
   - MCTS-200 costs 7 ms per decision.
   - Distill it: KL toward MCTS visit counts on sampled states, or a warm start by imitation.
   - Or add search at play time (AlphaZero style) for the ladder bot.
5. **Drafting:** measure separately. Both runs collapsed to one team that beats pool teams 94%. That is a
   specialist effect, not strength; judge drafts with the Gen 1 "team tests vs own policy" rule.

## Test order (one training run per box, 256x2 first)
- A/B at 256x2, 500M steps each, same seed, judged on max-damage and MCTS-200, policy and magnet: structured
  encoder vs the matchup flat net (53% at 500M).
- Then the full 1024x2 recipe with the winner.
- Gen 1 reference: from-scratch giant recipe 1024x2 beat Kakuna ~50-57% at piloting.

## Implementation status (2026-11-07, local only; nothing committed)
Step 1 and the `bot_policy 3` part of step 3 are built and tested locally. No 500M A/B yet.

**Built** (opt-in with `--policy.pokemon3_net=1`; the default 0 keeps the linear net, so old checkpoints and binaries
are unchanged):
- **Encoder:**
  - shared move MLP, 73 -> `pokemon_mid` -> `pokemon_embed`, over all 48 move slots;
  - shared Pokemon MLP, 124 + E + 3 -> mid -> E, over the 12 Pokemon. Its input is the base features, the mean of its
    known moves' embeddings, 2 defensive matchup values and an own flag;
  - fusion MLP: 12E + 316 + 11 -> H -> H.
- **Decoder:**
  - a linear head (128 logits + value) scores everything, the draft included;
  - in battle (drafting flag 0), a pointer term is added: q_move . (own active's move j embedding) / sqrt(E) on actions
    0-3, and q_switch . (own party slot m embedding) / sqrt(E) on actions 4-9;
  - q comes from a query MLP H -> H -> 2E (gain 0.01);
  - the decoder has its own move and Pokemon MLPs, over the 24 own move slots and 6 own Pokemon.
- Defaults are E 32 and mid 64. At 256x2 that is 800.5K params, against 1.8M for the flat net (whose encoder alone is
  5343 x 256).

**Files:**

| file | what |
|---|---|
| `ocean/pokemon3/pk3_net.h` | obs layout, `pk3_net_on` / `pk3_embed` / `pk3_mid`, CUDA/C feature helpers |
| `ocean/pokemon3/pk3_mlp.cuh` | copy of Gen 1's `semantic_mlp.cuh` with P3 names |
| `ocean/pokemon3/pokemon3_encoder.cu` | the encoder and decoder |
| `ocean/pokemon3/pk3_net_cpu.h` | CPU forward |
| `ocean/pokemon3/tests/test_net.cu` | parity and gradient test |
| `ocean/pokemon3/tests/run_test_net.sh` | builds and runs the test |
| `ocean/pokemon3/tests/bench_net.cu`, `run_bench_net.sh` | per-stage speed check in bf16 (`run_bench_net.sh ROWS`) |
| `ocean/pokemon3/pokemon3.h` | `PUF_CONFIGURE` -> `pk3_configure`; `bot_policy 3` |
| `config/pokemon3.ini` | `policy.pokemon3_net`, `pokemon_embed`, `pokemon_mid`; `env.mcts_iterations` |
| `src/ocean.cu` | 7 dispatch lines; the trainer-core change is logged in `reports/core_changes_audit.md` |

**Parity test** (FP32, GTX 1060). B=4 observations in the real layout: active slot 0, active 3, no active, drafting.
Pass 0 includes Pokemon with no known moves.
- CUDA vs CPU, max abs error: encoder 8.9e-8, decoder 2.8e-7. These go through the real `arch_forward` and
  `arch_forward_train`.
- The draft row and actions >= 10 are exactly linear-only.
- 285 / 285 central-difference derivatives pass (every parameter tensor plus the decoder's hidden input, 3 batches).
  One probe hit a ReLU kink at step 1e-3 and passed at 2.5e-4.

**Smoke tests** (bf16 `./puffer_pk3s`, 256x2, 512 agents, 4 threads, ou3 held-out pool, draft 30%):
- **Losses finite.** Structured: 2.1M steps before a 15-minute timeout; losses finite, entropy 1.82 -> 1.43, no
  engine errors. Flat: 1M steps; losses finite, entropy 1.64.
- **SPS is env-bound and noisy.** Both runs shared the 3 GB card with another session's Cretaceous renders.
  - Structured: 12-17K SPS when the GPU was free, 2-6K when contended. Train phase 0.8-1.1 s per 32K-step epoch;
    rollout model 0.27-0.48 s.
  - Flat: 13-29K SPS. Train phase 0.23 s; rollout model 0.11-0.13 s.
  - So the structured net costs about 4x the flat net's GPU time. Env stepping still dominates at this agent count.
  - Activations at minibatch 4096 filled the card (VRAM 3.0/3G; flat 1.8/3G), and the first epoch took about 6
    minutes there. That is a local 3 GB issue; a 16 GB box will not hit it. Use minibatch 2048 locally.
- **Eval path.** 100 greedy games each vs max-damage: structured checkpoint at 590K steps **10%**; flat at 983K steps
  9%. Both are untrained.
  - Loading a structured checkpoint without the flag exits on the exact-size check (3,202,048 vs 7,176,192 bytes).
  - **Eval does not read the run's ini, so every eval must pass `--policy.pokemon3_net=1`.** A `.magnet` needs it as
    well.
- **`env.bot_policy=3`** (MCTS, `env.mcts_iterations` default 200, full information, drafts at random): runs. The 1M
  flat checkpoint scored 2.1% (vs 9% against max-damage); 100 games took 50 s wall, including startup.

**Speed (bf16, RTX 5060 Ti, 4,096 rows, `run_bench_net.sh`):**
- Encoder forward 1.09 ms (move rows 0.22, move MLP 0.44, Pokemon rows 0.20, Pokemon MLP 0.10, fusion 0.11).
- Decoder forward 0.50 ms; encoder backward 1.31 ms.
- Training: 360K steps/s at 4,096 agents and 24 threads. 500M steps take ~23 minutes.
- The first A/B box (Vast 59637366) showed 28.9K steps/s. Its GPU was faulty: memory clock stuck at 810 MHz, 6.6
  GB/s device-to-device (a healthy 5060 Ti does 387), and the card 100% busy with nothing running. Destroyed. Check
  bandwidth first on every new box.

**Open issues:**
- **Showdown path wired (2026-11-08, not yet played):** `showdown/policy_server.c` runs `pk3_cpu_encode` -> puffercpu
  MinGRU -> `pk3_cpu_decode` when the checkpoint is a structured one. It tells the nets apart by size: the
  checkpoint holds encoder, decoder, then MinGRU, back to back, since every tensor is a multiple of 8 floats. That is
  800,512 floats at 256x2 / E 32 / M 64. Pass `--embed/--mid` (client.js passes them through) for other widths.
  Detection is tested on both nets. A Showdown game vs max-damage still has to confirm the end-to-end path.
- The pointer decoder has no per-action hand features yet (the matchup eff/dmg/KO only enter through the move
  embeddings). Gen 1 adds STAB/effectiveness scalars per action; add them if the A/B is close.
- Not yet measured: the 256x2 A/B (500M each, max-damage + MCTS-200, policy and magnet) from the test order above.

## Results (2026-11-08, Vast 59640753; evals.txt in ../20261107_gen3_encoder_ab)
Structured arm, 256x2, 500M, matchup recipe; 1,000 held-out teams (policy / magnet):

| steps | vs max-damage | vs MCTS-200 |
|---|---|---|
| 0 | 6.9% | 0.1% |
| 105M | 77.2% / 74.9% | 30.0% / 24.3% |
| 210M | 81.8% / 80.7% | 39.3% / 37.8% |
| 315M | 85.1% / 83.3% | 43.8% / 41.7% |
| 420M | 85.2% / 85.3% | 41.8% / 40.4% |
| 500M | **86.6%** / 86.3% | 43.3% / **47.1%** |

- The flat arm reruns the same recipe for the control: 25.3% / 20.2% vs max-damage at 105M. The earlier flat A/B
  reached 53% at 500M.
- The flat 1024x2 full-recipe run plateaued at ~78.5%.
- Next: ../20261108_gen3_top_teams/README.md (rank and weight top teams, forced states, league).
