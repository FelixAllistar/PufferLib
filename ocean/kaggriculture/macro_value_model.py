"""Load and apply the offline Kaggriculture macro-value models.

The model is intentionally a small, side-effect-free adapter.  It accepts the
same public snapshot and :class:`macro_actions.MacroAction` objects used by the
counterfactual writer and returns one score per candidate.  The adapter does
not call the simulator and it never reads the opponent's private inventory.

Two on-disk backends are supported:

* ``fit_macro_value.py --backend ridge`` writes a compressed NumPy ``.npz``;
* ``--backend lightgbm`` writes a LightGBM text booster and a sidecar metadata
  JSON containing the feature order.

The score is whatever target was fitted (normally same-state delta cash), so a
planner must record the target and opponent-continuation definition alongside
its decisions rather than treating a score as an absolute money forecast.
"""

from __future__ import annotations

import json
import pathlib
from dataclasses import dataclass
from typing import Any, Sequence

from macro_actions import MacroAction, public_features


def feature_row(
    snapshot: dict[str, Any], player: int, candidate: MacroAction,
    *, episode_steps: int = 720, turns_per_day: int = 24,
    shed_capacity: int = 100, feature_names: Sequence[str] | None = None,
) -> dict[str, float]:
    """Build a model row using the exact feature names emitted by the fitter."""

    values = public_features(
        snapshot, player, episode_steps=episode_steps,
        turns_per_day=turns_per_day, shed_capacity=shed_capacity,
    )
    columns = candidate.columns()
    names = tuple(feature_names or (*(
        f"feature_{name}" for name in values
    ), "candidate_kind_code", "candidate_item_code", "candidate_quantity",
    "candidate_executable"))
    row: dict[str, float] = {}
    for name in names:
        if name.startswith("feature_"):
            row[name] = float(values.get(name[8:], 0.0))
        else:
            value = columns.get(name, 0.0)
            try:
                row[name] = float(value)
            except (TypeError, ValueError):
                row[name] = 0.0
    return row


@dataclass
class MacroValueModel:
    """Backend-neutral candidate scorer."""

    backend: str
    feature_names: tuple[str, ...]
    target: str
    metadata: dict[str, Any]
    _weights: Any = None
    _mean: Any = None
    _scale: Any = None
    _booster: Any = None

    @classmethod
    def load(cls, path: str | pathlib.Path) -> "MacroValueModel":
        path = pathlib.Path(path)
        metadata_path = pathlib.Path(f"{path}.meta.json")
        metadata: dict[str, Any] = {}
        if metadata_path.exists():
            metadata = json.loads(metadata_path.read_text(encoding="utf-8"))
        backend = str(metadata.get("backend", ""))
        if path.suffix == ".npz" or backend == "ridge":
            import numpy as np

            with np.load(path, allow_pickle=False) as values:
                names = tuple(str(item) for item in values["feature_names"].tolist())
                target_array = values.get("target")
                target = str(target_array[0]) if target_array is not None else str(metadata.get("target", "delta_money"))
                weights = values["weights"].astype(np.float64, copy=False)
                mean = values["mean"].astype(np.float64, copy=False)
                scale = values["scale"].astype(np.float64, copy=False)
            if weights.ndim != 1 or weights.shape[0] != len(names) + 1:
                raise ValueError(f"invalid ridge coefficient shape in {path}")
            if mean.shape != (len(names),) or scale.shape != (len(names),):
                raise ValueError(f"invalid ridge normalization shape in {path}")
            return cls("ridge", names, target, metadata, weights, mean, scale, None)
        if backend != "lightgbm":
            # A LightGBM model without metadata is still unambiguous by its
            # conventional text suffix only when the sidecar was preserved.
            raise ValueError(
                f"cannot identify macro-value backend for {path}; expected {path}.meta.json"
            )
        try:
            import lightgbm as lgb
        except ImportError as error:
            raise RuntimeError("LightGBM is required to load this macro model") from error
        names_raw = metadata.get("feature_names")
        if not isinstance(names_raw, list) or not names_raw:
            raise ValueError(f"LightGBM metadata has no feature_names: {metadata_path}")
        booster = lgb.Booster(model_file=str(path))
        return cls(
            "lightgbm", tuple(str(name) for name in names_raw),
            str(metadata.get("target", "delta_money")), metadata,
            _booster=booster,
        )

    def predict_rows(self, rows: Sequence[dict[str, float]]) -> list[float]:
        if not rows:
            return []
        if self.backend == "ridge":
            import numpy as np

            matrix = np.asarray(
                [[float(row.get(name, 0.0)) for name in self.feature_names] for row in rows],
                dtype=np.float64,
            )
            normalized = (matrix - self._mean) / self._scale
            design = np.column_stack((np.ones(len(matrix)), normalized))
            return [float(value) for value in design @ self._weights]
        matrix = [[float(row.get(name, 0.0)) for name in self.feature_names] for row in rows]
        return [float(value) for value in self._booster.predict(matrix)]

    def predict_candidates(
        self, snapshot: dict[str, Any], player: int,
        candidates: Sequence[MacroAction], *, episode_steps: int = 720,
        turns_per_day: int = 24, shed_capacity: int = 100,
    ) -> list[float]:
        rows = [feature_row(
            snapshot, player, candidate, episode_steps=episode_steps,
            turns_per_day=turns_per_day, shed_capacity=shed_capacity,
            feature_names=self.feature_names,
        ) for candidate in candidates]
        return self.predict_rows(rows)


def choose_best(
    candidates: Sequence[MacroAction], scores: Sequence[float],
) -> tuple[int, MacroAction, float]:
    """Return the highest-scoring feasible candidate with deterministic ties."""

    if len(candidates) != len(scores) or not candidates:
        raise ValueError("candidate/score arrays must have the same nonzero length")
    best = max(
        range(len(candidates)),
        key=lambda index: (float(scores[index]), -index),
    )
    return best, candidates[best], float(scores[best])

