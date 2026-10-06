# Gen 9 training completion contract

The native-conversion goal below is paused by the user. The current route uses
the original pinned Showdown rules in batched Node CPU workers and the existing
PufferLib CUDA PPO learner. Its first 1,024-step/eight-update smoke passed with
finite changing weights and four evaluation games. README documents the
working commands. Its initial wide one-hot MLP/MinGRU encoder and current-policy
self-play are a training baseline; shared entity encoders, stronger opponents,
scaling and playing-strength evaluation remain. This evidence does not fulfill
the original native-conversion contract.

Complete the pinned `gen9randombattle` simulator and PufferLib integration
before treating a rollout as training-ready. Completion requires all reachable
move/item/ability/condition handlers, complete independent games against
Showdown, exact player requests/public state, reproducible RNG, CPU batching,
and a real CUDA/PPO training smoke. Diagnostic primitives are intermediate
deliverables.

The policy uses shared semantic MLPs and the existing MinGRU path. It uses no
transformer or search, during training or evaluation. The Gen 1 implementation
in `ocean/pokemon/pokemon_encoder.cu` supplies reusable MLP/decoder conventions;
Gen 9 receives its own catalog, contract and checkpoint version.

The [Jaxcalibur write-up](https://jaxcalibur.github.io/), inspected October 5,
2026, suggests useful feature groups: battlefield, active Pokémon, move
candidates including Struggle, both teams, recent events and matchups. It also
describes opponent set/action prediction as auxiliary training tasks and
search-free PPO self-play. Its architecture and playing-strength claims are
not validation of our implementation.

Use the feature groups with shared MLP encoders, masked entity features,
fixed-layout fusion and optional MinGRU memory. Learn categorical embeddings
or use semantic catalog features; dense integer IDs must not become ordinal
numeric features. Candidate move/switch scoring should share weights across
slots. Preserve source-specific forced switch, wait, recharge, revival and Tera
choices before fixing action dimensions.

Own-team data is exact. Opponent bench starts unknown; observed identity, HP
percentage, moves, item, ability and Tera information enter features only at
source-authorized revelation. Keep persistent revealed knowledge and recent
public events separate from private simulator state. An Illusion disguise
must not identify the hidden Pokémon, level or party position.

Any matchup feature uses only own sets, public opponent data and a separately
identified estimate of unknown properties. It must not call native damage
against the true hidden opponent set and expose that result to the actor.
Auxiliary opponent labels may use private state only as training targets;
they are never actor inputs. Opponent action labels refer to a simultaneous
decision after commitment, with observations captured before either choice is
revealed.

Start with undiscounted terminal win/loss rewards and fresh independent team,
battle and policy RNG streams. Keep true ties, training truncations and engine
errors distinct. Training readiness includes both seats, frozen policy
opponents, ported scripted smoke/evaluation opponents and independent
Showdown evaluation. Broader performance qualification measures complete
rollouts and CPU/GPU overlap. Busy-machine timing does not establish speed.
