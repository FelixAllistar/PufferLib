# Reward/accounting audit and fixes

## Fixed

- `animal_bits` indexes coops/pastures, not livestock. Keep this engine index
  unchanged. `kag_live_tiles` now checks real entities, fixing expansion reward
  peaks and diagnostic animal totals. Empty housing cannot earn animal credit.
- Fuzzy phases require a valid crop/animal ID and matching tile kind before
  health/readiness/occupied-plot credit. Empty pasture no longer counts as
  a non-cow animal or productive plot occupancy.
- Diagnostic productive extra tiles now check actual animal presence too.
  Spending diagnostics expose `occupied_animals`, `empty_animal_housing`, and
  `animal_housing` separately; the 17-double buffer has a regression test.
- Optional maintenance FEED valuation uses the configured season length rather
  than a hard-coded 30-day divisor. Default 720/24 behavior is unchanged.

No observation/action/parameter ABI, market rules, executor, or reward weights
changed. The dashboard `animals_alive` already used actual livestock and was
not affected by the old diagnostic overcount. Existing cash-based evaluation
results remain usable; old total-animal/extra-occupancy diagnostic fields are
not directly comparable with corrected outputs. V2 phase training really did
receive rewards, but its other-animal/occupancy targets included empty housing.

## Reviewed; deliberately preserved semantics

- `reward_money_scale` and `reward_progress_terminal_money_scale` independently
  add normalized signed terminal cash: their coefficients sum.
- `reward_cash_scale` is discounted cash-potential shaping retaining terminal
  cash, NOT zero-terminal PBRS.
- Legacy `reward_potential_scale` retains terminal marked net worth. At gamma
  <= 0 it uses raw dollar differences, unlike the normalized discounted mode.
- Progress potential alone has a zero terminal successor. Its discounted sum
  cancels (up to the fixed initial potential) only with matching gamma and no
  nonlinear reward clipping. It includes cash and daily hired-hand capital
  even if all per-asset component multipliers are zero.
- Expansion rewards episode HIGH-WATER MARKS, not simultaneous surviving
  populations. Maximum root-episode credit is 5 * scale. Its land target means
  TOTAL plots, while the dashboard `land_purchases` excludes the original plot.
- Phases reward current state across four fuzzy windows; cap is 4 * scale.
  They are intentionally NOT policy-invariant potential shaping.
- Maintenance and tagged curriculum are additional non-telescoping objectives.
  Tagged CASH_LOOP deliberately compresses biological time; it is not a normal
  root-start game. Current remote config disables both systems.
- Both legacy and progress economic values guard empty livestock housing.
  Existing market-impact tests cover conservative marks, rounded prices,
  inventory impact, and asset-transfer semantics. No market formula changed.

## Money ledger

`purchase_spend` includes only successful seed/animal/product buys. It does NOT
include hires or land. `sales_revenue` counts successful sales only. For a
root-start game: final cash = starting cash + sales - purchases - hires - land.
For restored states use their initial cash/counter offsets instead; absolute
counter values may include the replay prefix. Failed/partially filled orders
only count actually committed units. Tests cover an unaffordable 1000-seed
request (300 seeds bought with $3000), repeated failed orders, land cost $7000
for all three expansions, and Fibonacci hiring costs. No ledger bug found in
these paths; accounting labels were easy to misinterpret.

## Validation and deployment

Remote evidence: `logs/kaggriculture/reward_audit_20260909/`.
Passed new occupancy/accounting tests, phase CPU and CUDA-helper tests,
spending-buffer tests, existing full CPU adapter suite, and actual CPU/GPU
transition parity (12 modes x 1440 turns, state/obs/mask/reward/reset/log).

GPU trainer compiled to a separate filename then atomically installed after
tests. Old executable backed up as `puffer_before`. Running PID 155915 retains
its original executable mapping (hash c0a52a...); new launches use the fixed
binary (00d8809...). Its config hash c56aea... is unchanged. No training or
submission was launched, no weights or league members were changed.
Diagnostic shared libraries were rebuilt; old main probe backed up in the
same evidence directory. Immutable old experiment runners intentionally reject
changed source/binary fingerprints; use new output/plan generations.

To inspect reward semantics on disk without changing anything:

```sh
/venv/main/bin/python ocean/kaggriculture/audit_rewards.py config/kaggriculture.ini
```

After the audit, the user explicitly requested next-run config preparation.
Remote config now uses fresh ID `fresh_phases_fixed_20260909_v2`, no loaded
model, phase scale 2, terminal cash 4, win 1; legacy/cash/progress shaping and
peak expansion are zero. LR, batching, architecture, EMAG, and step budget
were preserved. Backup: `config_before_next.ini` in the evidence directory.
This edits only the next-launch config, not the current process's objective.
