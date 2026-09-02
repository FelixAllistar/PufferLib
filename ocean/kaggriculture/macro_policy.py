"""ABI-neutral boundary for a future macro PPO policy.

This module is intentionally a protocol, not a second executor.  A macro
policy receives public strategic features, candidate descriptions, and (when
available) scorer outputs.  It returns a candidate index; the existing
deterministic executor then expands that candidate into primitive actions.
Keeping this JSON-serializable boundary separate means a PPO experiment can be
added without changing the current 1,058-head policy ABI or accidentally
feeding hidden opponent inventory into the model.
"""

from __future__ import annotations

from typing import Any, Sequence

from macro_actions import MacroAction, public_features


MACRO_POLICY_FORMAT_VERSION = 1


def build_observation(
    snapshot: dict[str, Any], player: int, candidates: Sequence[MacroAction],
    scores: Sequence[float] | None = None, *, episode_steps: int = 720,
    turns_per_day: int = 24, shed_capacity: int = 100,
) -> dict[str, Any]:
    """Build the complete public input for a macro controller."""

    if scores is not None and len(scores) != len(candidates):
        raise ValueError("scores must have one value per candidate")
    return {
        "format": "kaggriculture_macro_observation_v1",
        "version": MACRO_POLICY_FORMAT_VERSION,
        "player": int(player),
        "features": public_features(
            snapshot, player, episode_steps=episode_steps,
            turns_per_day=turns_per_day, shed_capacity=shed_capacity,
        ),
        "candidates": [candidate.columns() for candidate in candidates],
        "scores": [float(value) for value in scores] if scores is not None else None,
    }


def validate_decision(
    decision: Any, candidates: Sequence[MacroAction],
) -> tuple[int, MacroAction]:
    """Validate a model response and return its selected candidate.

    Accept either an integer or ``{"candidate_index": N}``; reject malformed,
    out-of-range, and non-executable choices instead of silently turning them
    into PASS.  A runtime adapter can call this at every strategic decision
    point and then submit ``candidate.action_sequence()`` to the executor.
    """

    if isinstance(decision, dict):
        value = decision.get("candidate_index")
    else:
        value = decision
    try:
        index = int(value)
    except (TypeError, ValueError) as error:
        raise ValueError("macro decision must contain an integer candidate_index") from error
    if index < 0 or index >= len(candidates):
        raise ValueError(f"macro candidate index {index} is out of range")
    candidate = candidates[index]
    if not candidate.executable:
        raise ValueError(f"macro candidate {candidate.action_id} is not executable")
    return index, candidate


def decision_payload(index: int, candidate: MacroAction) -> dict[str, Any]:
    """Return an auditable decision record for logs or replay."""

    return {
        "format": "kaggriculture_macro_decision_v1",
        "version": MACRO_POLICY_FORMAT_VERSION,
        "candidate_index": int(index),
        "candidate_id": candidate.action_id,
        "plan": list(candidate.action_sequence()),
    }

