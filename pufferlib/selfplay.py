"""Selfplay-pool training: a fraction of envs play primary vs a frozen historical
snapshot, the rest are pure selfplay. Used by `_train` in pufferl.py — gated on
`selfplay.enabled` (config section).

Pool grows on two triggers:
  - snapshot_interval: every N global steps, save primary weights as a new
    pool entry regardless of winrate. Provides a steady cadence.
  - winrate-driven swap: when primary beats the current opponent at >=
    swap_winrate over >= min_games, also save primary as a pool entry, then
    swap to a new opponent. Marks progress checkpoints in the curriculum.

Swap (without a snapshot) also fires when opp_timeout_steps have elapsed
since the current opponent was finalized. Timeout prevents stalemates from
pinning the curriculum to a single opponent indefinitely.

Pool storage is disk-only (paths held in memory; weights only on GPU when
loaded as the frozen bank). Stride-eviction preserves temporal coverage when
the pool exceeds its cap.
"""
import glob
import json
import os
import re

import numpy as np

from pufferlib import _C


def sample_opponent(pool, rng):
    candidates = pool if len(pool) < 6 else pool[:-5]
    weights = np.array([(i + 1) ** 0.5 for i in range(len(candidates))], dtype=np.float64)
    weights /= weights.sum()
    idx = int(rng.choice(len(candidates), p=weights))
    return candidates[idx]


def update_elo(primary_elo, opp_elo, score_rate, k):
    expected = 1.0 / (1.0 + 10.0 ** ((opp_elo - primary_elo) / 400.0))
    delta = k * (score_rate - expected)
    return primary_elo + delta, opp_elo - delta


def evict(pool, max_size):
    '''Drop every other entry from the oldest half once the pool exceeds max_size.
    Newest half is preserved intact.'''
    if len(pool) <= max_size:
        return pool
    half = len(pool) // 2
    return pool[:half:2] + pool[half:]


def split_seed_tokens(value):
    if value is None:
        return []
    if isinstance(value, (list, tuple)):
        return [str(v).strip() for v in value if str(v).strip()]
    return [token for token in re.split(r'[\s,;]+', str(value).strip()) if token]


def boolish(value, default=False):
    if value is None:
        return default
    if isinstance(value, bool):
        return value
    if isinstance(value, (int, float)):
        return bool(value)
    return str(value).strip().lower() in ('1', 'true', 'yes', 'on')


def native_policy_matches(entry, args, explicit=False):
    '''Reject obvious checkpoint/profile mismatches before loading frozen banks.

    Direct paths and globs often do not have metadata, so explicit=True lets them
    through and relies on the native loader's byte-size guard. Registry entries
    are filtered strictly because we do have their declared layout.
    '''
    if explicit:
        return True
    policy = args.get('policy', {})
    checks = (
        ('model_type', int),
        ('hidden_size', int),
        ('num_layers', int),
        ('expansion_factor', int),
    )
    for key, caster in checks:
        if key not in entry:
            continue
        if key not in policy:
            continue
        try:
            if caster(entry[key]) != caster(policy[key]):
                return False
        except (TypeError, ValueError):
            return False
    return True


def existing_seed_path(path):
    path = os.path.expanduser(str(path))
    if os.path.isfile(path) and os.path.getsize(path) > 0:
        return path
    return None


def load_registry(path):
    if not path:
        return {}
    path = os.path.expanduser(str(path))
    if not os.path.isfile(path):
        return {}
    with open(path, 'r', encoding='utf-8') as f:
        return json.load(f)


def resolve_initial_opponents(sp, args):
    registry = load_registry(sp.get('registry_file') or sp.get('registry_path'))
    learned = registry.get('learned_opponents', []) if isinstance(registry, dict) else []
    by_name = {str(entry.get('name', '')).lower(): entry for entry in learned
               if entry.get('name')}
    player_deck = int(args.get('env', {}).get('player_deck', -1))
    same_deck_only = boolish(sp.get('initial_same_deck_only'), True)
    names = split_seed_tokens(sp.get('initial_opponent_names'))

    entries = []

    def add_entry(name, path, entry=None, explicit=False):
        path = existing_seed_path(path)
        if path is None:
            return
        if entry is not None and not native_policy_matches(entry, args, explicit=explicit):
            return
        entries.append({
            'path': path,
            'elo': float(sp.get('elo_init', 0.0)),
            'name': name or os.path.basename(path),
            'seeded': True,
        })

    def add_learned(entry, explicit=False):
        if entry is None:
            return
        add_entry(entry.get('name'), entry.get('path'), entry=entry, explicit=explicit)
        for path in entry.get('paths', []) or []:
            add_entry(entry.get('name'), path, entry=entry, explicit=explicit)
        for pattern in split_seed_tokens(entry.get('glob')):
            for path in sorted(glob.glob(os.path.expanduser(pattern), recursive=True)):
                add_entry(entry.get('name'), path, entry=entry, explicit=explicit)

    if learned and (not names or any(name.lower() == 'same_deck' for name in names)):
        for entry in learned:
            if same_deck_only and int(entry.get('deck_id', -999)) != player_deck:
                continue
            add_learned(entry)

    if any(name.lower() == 'all' for name in names):
        for entry in learned:
            add_learned(entry)
    else:
        for name in names:
            lowered = name.lower()
            if lowered in ('same_deck', ''):
                continue
            entry = by_name.get(lowered)
            if entry is not None:
                add_learned(entry, explicit=True)
            else:
                add_entry(name, name, explicit=True)

    for token in split_seed_tokens(sp.get('initial_opponents')):
        entry = by_name.get(token.lower())
        if entry is not None:
            add_learned(entry, explicit=True)
        else:
            add_entry(token, token, explicit=True)

    for pattern in split_seed_tokens(sp.get('initial_opponent_glob')):
        for path in sorted(glob.glob(os.path.expanduser(pattern), recursive=True)):
            add_entry(os.path.basename(path), path, explicit=True)

    deduped = []
    seen = set()
    for entry in entries:
        real = os.path.realpath(entry['path'])
        if real in seen:
            continue
        seen.add(real)
        deduped.append(entry)
    return deduped


def build_perm_tags(num_buffers, agents_per_buffer, agents_per_env, frozen_sizes, num_envs):
    '''Build env-slot -> rollout-row routing and per-env bank tag.

    Multi-bank generalization. `frozen_sizes` is a list of per-bank agent counts
    (per buffer). With one bank this reduces to the legacy single-bank layout.

    Per-buffer physical-row layout (apb = agents_per_buffer, F = sum(frozen_sizes)):
        [0,           apb - 2F)                       primary — selfplay envs (all slots)
        [apb - 2F,    apb - F)                        primary — historical envs' team A
        [apb - F,     apb - F + frozen_sizes[0])      bank 0  — historical envs' team B
        [apb - F + frozen_sizes[0], ... + ...[1])     bank 1  — ... etc.

    Env order within a buffer: selfplay envs first (tag=0), then historical
    envs assigned to banks in block order — the first `frozen_sizes[0]/team_size`
    historical envs play bank 0 (tag=1), next block plays bank 1 (tag=2), etc.

    The C-side bank_layout (pufferlib.cu:1798-1806) lays banks out sequentially
    after primary, so our routing matches: bank b's slice is
    [apb - F + sum(frozen_sizes[:b]),  apb - F + sum(frozen_sizes[:b+1])).

    Returns (perm, tags, num_hist_envs_per_bank) — last is a list of per-bank
    historical-env counts across all buffers, used by selfplay.step to know how
    many env alignments to wait for per bank during swaps.'''
    team_size = agents_per_env // 2
    envs_per_buffer = agents_per_buffer // agents_per_env
    num_banks = len(frozen_sizes)
    total_frozen = sum(frozen_sizes)
    hist_envs_per_bank_per_buffer = [fs // team_size for fs in frozen_sizes]
    total_hist_envs_per_buffer = sum(hist_envs_per_bank_per_buffer)
    selfplay_envs = envs_per_buffer - total_hist_envs_per_buffer
    perm = np.empty(num_buffers * agents_per_buffer, dtype=np.int32)
    tags = np.zeros(num_envs, dtype=np.int32)
    env_idx = 0
    for b_buf in range(num_buffers):
        buf_start          = b_buf * agents_per_buffer
        hist_primary_start = buf_start + agents_per_buffer - 2 * total_frozen
        bank_starts = []
        offset = buf_start + agents_per_buffer - total_frozen
        for bank in range(num_banks):
            bank_starts.append(offset)
            offset += frozen_sizes[bank]
        h_within_buffer = 0
        for e in range(envs_per_buffer):
            slot_base = buf_start + e * agents_per_env
            if e < selfplay_envs:
                for s in range(agents_per_env):
                    perm[slot_base + s] = slot_base + s
                tags[env_idx] = 0
            else:
                # Block assignment: walk cumulative bank capacity to find which
                # bank this historical env belongs to.
                bank_idx = 0
                cum = hist_envs_per_bank_per_buffer[0]
                while h_within_buffer >= cum and bank_idx < num_banks - 1:
                    bank_idx += 1
                    cum += hist_envs_per_bank_per_buffer[bank_idx]
                h_in_bank = h_within_buffer - (cum - hist_envs_per_bank_per_buffer[bank_idx])
                team_a_offset = hist_primary_start + h_within_buffer * team_size
                team_b_offset = bank_starts[bank_idx] + h_in_bank * team_size
                for s in range(team_size):
                    perm[slot_base + s] = team_a_offset + s
                    perm[slot_base + team_size + s] = team_b_offset + s
                tags[env_idx] = bank_idx + 1
                h_within_buffer += 1
            env_idx += 1
    num_hist_envs_per_bank = [n * num_buffers for n in hist_envs_per_bank_per_buffer]
    return perm, tags, num_hist_envs_per_bank


def setup(pufferl, backend, args, run_id):
    '''Wire up agent_perm/tags and bootstrap the frozen bank with the current
    weights so historical envs have an opponent from rollout 1. Returns a
    pool_state dict (or None if disabled).'''
    sp = args.get('selfplay', {})
    if not sp.get('enabled', 0):
        return None
    if backend is not _C:
        raise RuntimeError('selfplay_pool requires the native CUDA backend')

    total_agents = int(args['vec']['total_agents'])
    num_buffers = int(args['vec']['num_buffers'])
    if total_agents % num_buffers != 0:
        raise RuntimeError(f'total_agents ({total_agents}) must be divisible by '
                           f'num_buffers ({num_buffers})')
    agents_per_buffer = total_agents // num_buffers

    num_envs = backend.num_envs(pufferl)
    agents_per_env = total_agents // num_envs
    if agents_per_env % 2 != 0:
        raise RuntimeError(f'agents_per_env ({agents_per_env}) must be even (two equal teams)')
    if agents_per_buffer % agents_per_env != 0:
        raise RuntimeError(f'agents_per_buffer ({agents_per_buffer}) must be divisible by '
                           f'agents_per_env ({agents_per_env})')
    team_size = agents_per_env // 2

    num_banks = int(args['vec'].get('num_frozen_banks', 1))
    if num_banks <= 0:
        raise RuntimeError('selfplay.enabled requires num_frozen_banks >= 1')
    if num_banks > 8:
        raise RuntimeError(f'num_frozen_banks {num_banks} exceeds chess.h CHESS_MAX_BANKS=8')

    # frozen_bank_pct is per-bank (matches C-side: pufferlib.cu:2069). Each bank
    # gets floor(apb * pct) agents, total historical = num_banks * frozen_size.
    frozen_size = int(agents_per_buffer * float(args['vec']['frozen_bank_pct']))
    frozen_size -= frozen_size % team_size
    if frozen_size <= 0:
        raise RuntimeError('selfplay.enabled but frozen_bank_pct rounds to 0 slots '
                           f'after team-size ({team_size}) alignment')
    total_frozen = frozen_size * num_banks
    if total_frozen >= agents_per_buffer // 2:
        raise RuntimeError(f'total_frozen {total_frozen} (= num_banks {num_banks} '
                           f'* per_bank {frozen_size}) >= apb/2 {agents_per_buffer//2}')

    frozen_sizes = [frozen_size] * num_banks
    perm, tags, num_hist_envs_per_bank = build_perm_tags(
        num_buffers, agents_per_buffer, agents_per_env, frozen_sizes, num_envs)
    backend.set_agent_perm(pufferl, perm)
    backend.set_env_tags(pufferl, tags)

    pool_dir = os.path.join(args['checkpoint_dir'], args['env_name'], run_id, 'pool')
    os.makedirs(pool_dir, exist_ok=True)
    bootstrap_path = os.path.join(pool_dir, f'{pufferl.global_step:016d}.bin')
    backend.save_weights(pufferl, bootstrap_path)

    elo_init = float(sp.get('elo_init', 0.0))
    elo_k    = float(sp.get('elo_k',    16.0))
    rng = np.random.default_rng(int(sp.get('seed', 0)))
    bootstrap_entry = {
        'path': bootstrap_path,
        'elo': elo_init,
        'name': 'bootstrap_current',
        'seeded': False,
    }
    initial_entries = resolve_initial_opponents(sp, args)
    pool = [bootstrap_entry] + initial_entries
    bank_entries = initial_entries if initial_entries else [bootstrap_entry]
    if initial_entries:
        names = ', '.join(entry['name'] for entry in initial_entries[:8])
        suffix = '' if len(initial_entries) <= 8 else f', +{len(initial_entries) - 8} more'
        print(f'selfplay seeded {len(initial_entries)} initial opponent(s): {names}{suffix}', flush=True)
    else:
        print('selfplay seeded 0 initial opponents; frozen banks start from current bootstrap', flush=True)

    banks_state = []
    for b in range(num_banks):
        bank_entry = bank_entries[b % len(bank_entries)]
        backend.load_frozen_bank(pufferl, b, bank_entry['path'])
        banks_state.append({
            'cur_opp_path': bank_entry['path'],
            'cur_opp_elo': bank_entry['elo'],
            'cur_opp_name': bank_entry['name'],
            'hist_score': 0.0,
            'hist_n': 0.0,
            'pending_opp_path': None,
            'pending_opp_elo': None,
            'pending_opp_name': None,
            'epoch_armed': 0,
            'opp_started_step': int(pufferl.global_step),
            'num_hist_envs': num_hist_envs_per_bank[b],
            'last_winrate_at_swap': 0.0,
            'last_epochs_to_align': 0,
        })

    return {
        'pool_dir': pool_dir,
        'pool': pool,
        'seeded_opponents': len(initial_entries),
        'rng': rng,
        'max_size': int(sp['max_size']),
        'min_games': int(sp['min_games']),
        'swap_winrate': float(sp['swap_winrate']),
        'snapshot_interval': int(sp.get('snapshot_interval', 1_000_000_000)),
        'opp_timeout_steps': int(sp.get('opp_timeout_steps', 500_000_000)),
        'num_banks': num_banks,
        'banks': banks_state,
        'primary_elo': elo_init,
        'elo_k': elo_k,
        'last_snapshot_step': int(pufferl.global_step),
    }


def step(pufferl, backend, pool_state, flat_logs, epoch):
    if pool_state is None:
        return

    n_window = float(flat_logs.get('env/n', 0.0))
    num_banks = pool_state['num_banks']

    # 1. Per-bank Elo update from the most recent rollout window.
    for b in range(num_banks):
        bank = pool_state['banks'][b]
        hist_score_w = float(flat_logs.get(f'env/hist_score_bank_{b}', 0.0)) * n_window
        hist_n_w     = float(flat_logs.get(f'env/hist_n_bank_{b}',     0.0)) * n_window
        if hist_n_w > 0.0:
            bank['hist_score'] += hist_score_w
            bank['hist_n']     += hist_n_w
            score_rate = hist_score_w / hist_n_w
            new_p, new_o = update_elo(pool_state['primary_elo'],
                bank['cur_opp_elo'], score_rate, pool_state['elo_k'])
            # All banks update the shared primary Elo. Multiple banks updating
            # primary in one step is fine — Elo is symmetric, just a few more
            # tiny adjustments per rollout window.
            pool_state['primary_elo'] = new_p
            bank['cur_opp_elo'] = new_o
            for entry in pool_state['pool']:
                if entry['path'] == bank['cur_opp_path']:
                    entry['elo'] = new_o
                    break

    # 2. Global snapshot cadence (shared across banks).
    if (pool_state['snapshot_interval'] > 0
            and pufferl.global_step - pool_state['last_snapshot_step']
                >= pool_state['snapshot_interval']):
        snap_path = os.path.join(pool_state['pool_dir'],
            f'{pufferl.global_step:016d}.bin')
        backend.save_weights(pufferl, snap_path)
        pool_state['pool'].append({
            'path': snap_path,
            'elo': pool_state['primary_elo'],
            'name': f'snapshot_{pufferl.global_step:016d}',
            'seeded': False,
        })
        pool_state['pool'] = evict(pool_state['pool'], pool_state['max_size'])
        pool_state['last_snapshot_step'] = int(pufferl.global_step)

    # 3. Per-bank swap logic. Each bank decides independently based on its own
    # winrate. Tags 1..num_banks correspond to bank 0..num_banks-1.
    for b in range(num_banks):
        bank = pool_state['banks'][b]
        winrate = (bank['hist_score'] / bank['hist_n']
                       if bank['hist_n'] > 0 else None)
        winrate_met = (winrate is not None
            and bank['hist_n'] >= pool_state['min_games']
            and winrate >= pool_state['swap_winrate'])
        timed_out = (pool_state['opp_timeout_steps'] > 0
            and pufferl.global_step - bank['opp_started_step']
                >= pool_state['opp_timeout_steps'])
        tag_value = b + 1

        if bank['pending_opp_path'] is not None:
            if backend.count_aligned(pufferl, tag_value, 0) >= bank['num_hist_envs']:
                backend.load_frozen_bank(pufferl, b, bank['pending_opp_path'])
                backend.count_aligned(pufferl, tag_value, 1)
                bank['cur_opp_path'] = bank['pending_opp_path']
                bank['cur_opp_elo'] = bank['pending_opp_elo']
                bank['cur_opp_name'] = bank['pending_opp_name']
                bank['pending_opp_path'] = None
                bank['pending_opp_elo'] = None
                bank['pending_opp_name'] = None
                bank['hist_score'] = 0.0
                bank['hist_n'] = 0.0
                bank['opp_started_step'] = int(pufferl.global_step)
                bank['last_epochs_to_align'] = epoch - bank['epoch_armed']
        elif winrate_met or timed_out:
            # Winrate-driven snapshot kept while pool is small (< 10). After
            # that, only the global interval cadence grows the pool. Timeout
            # swaps never snapshot (stalemate, not progress).
            if winrate_met and len(pool_state['pool']) < 10:
                snap_path = os.path.join(pool_state['pool_dir'],
                    f'{pufferl.global_step:016d}.bin')
                backend.save_weights(pufferl, snap_path)
                pool_state['pool'].append({
                    'path': snap_path,
                    'elo': pool_state['primary_elo'],
                    'name': f'snapshot_{pufferl.global_step:016d}',
                    'seeded': False,
                })
                pool_state['pool'] = evict(pool_state['pool'], pool_state['max_size'])
                pool_state['last_snapshot_step'] = int(pufferl.global_step)
            opp_entry = sample_opponent(pool_state['pool'], pool_state['rng'])
            bank['pending_opp_path'] = opp_entry['path']
            bank['pending_opp_elo'] = opp_entry['elo']
            bank['pending_opp_name'] = opp_entry.get('name', os.path.basename(opp_entry['path']))
            bank['epoch_armed'] = epoch
            bank['last_winrate_at_swap'] = winrate if winrate is not None else 0.0

    # 4. Emit logs — per-bank and aggregate.
    flat_logs['pool/size']     = len(pool_state['pool'])
    flat_logs['env/elo']       = pool_state['primary_elo']
    flat_logs['pool/num_banks'] = num_banks
    flat_logs['pool/seeded_opponents'] = pool_state.get('seeded_opponents', 0)
    total_score = 0.0
    total_n     = 0.0
    for b in range(num_banks):
        bank = pool_state['banks'][b]
        wr = (bank['hist_score'] / bank['hist_n']
              if bank['hist_n'] > 0 else None)
        flat_logs[f'pool/winrate_at_swap_bank_{b}'] = bank['last_winrate_at_swap']
        flat_logs[f'pool/epochs_to_align_bank_{b}'] = bank['last_epochs_to_align']
        flat_logs[f'pool/winrate_bank_{b}'] = wr if wr is not None else 0.0
        flat_logs[f'env/historical_winrate_bank_{b}'] = wr if wr is not None else 0.0
        total_score += bank['hist_score']
        total_n     += bank['hist_n']
    # Aggregate winrate across all banks (legacy compat with old dashboards).
    if total_n > 0:
        agg = total_score / total_n
        flat_logs['pool/winrate']           = agg
        flat_logs['env/historical_winrate'] = agg
