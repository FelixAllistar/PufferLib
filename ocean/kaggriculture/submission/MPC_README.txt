This archive is a hierarchical Kaggriculture export.

Default runtime:
  main.py uses the parity-tested deterministic top-bot executor. This is the
  safe submission path and is intentionally not controlled by the experimental
  public-state ridge proposal layer.

Local opt-in:
  PUFFERLIB_MACRO_OVERLAY=1 enables macro_overlay.py, which proposes only
  strategic PLANT/BUILD_ANIMAL/BUY_LAND plans and keeps the top-bot routine
  maintenance, harvest, market, and liquidation actions. Exact native MPC
  remains an offline evaluator because Kaggle callbacks do not expose the
  opponent's hidden state or the native serialized simulator state.
