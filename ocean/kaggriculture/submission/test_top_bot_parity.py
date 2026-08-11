"""End-to-end parity check for the pure-Python export and native top bot."""

import ctypes
import importlib.util
import json
import sys
from pathlib import Path

from kaggle_environments import make

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
from parity import CAction, CConfig, c_action, canonical_official, c_snapshot
from parity import first_difference, load_core, official_configuration

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
LIB = ROOT / "build" / "libtop_bot_oracle.so"

spec = importlib.util.spec_from_file_location("top_bot", HERE / "top_bot" / "main.py")
top_bot = importlib.util.module_from_spec(spec)
spec.loader.exec_module(top_bot)

learned_spec = importlib.util.spec_from_file_location("learned_submission", HERE / "main.py")
learned_submission = importlib.util.module_from_spec(learned_spec)
learned_spec.loader.exec_module(learned_submission)


def normalized(action):
    return c_action(action)


def action_difference(a, b):
    def unit_tuple(value):
        return value.op, value.arg, value.n if value.op in (5, 14) else 0

    def market_tuple(value):
        return value.op, value.item, value.n if value.op in (0, 1, 2, 3) else 0

    fields = (("farmer", a.farmer, b.farmer),)
    for name, left, right in fields:
        if unit_tuple(left) != unit_tuple(right):
            return name, unit_tuple(left), unit_tuple(right)
    if a.hand_count != b.hand_count:
        return "hand_count", a.hand_count, b.hand_count
    for i in range(a.hand_count):
        left, right = a.hands[i], b.hands[i]
        if unit_tuple(left) != unit_tuple(right):
            return f"hands[{i}]", unit_tuple(left), unit_tuple(right)
    if a.market_count != b.market_count:
        return "market_count", a.market_count, b.market_count
    for i in range(a.market_count):
        left, right = a.market[i], b.market[i]
        if market_tuple(left) != market_tuple(right):
            return f"market[{i}]", market_tuple(left), market_tuple(right)
    return None


def animal_count(snapshot, player):
    return sum(
        isinstance(tile, dict)
        and tile.get("kind") in ("COOP", "PASTURE")
        and tile.get("animal") is not None
        for row in snapshot["farms"][player]["tiles"] for tile in row)


def run(seed, seat, opponent):
    lib = load_core(LIB)
    lib.kg_top_bot_action.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.POINTER(CAction)]
    lib.kg_top_policy_input.argtypes = [ctypes.c_void_p, ctypes.c_int,
        ctypes.POINTER(ctypes.c_uint8), ctypes.POINTER(ctypes.c_uint8)]
    cfg = CConfig()
    lib.kg_config_default(ctypes.byref(cfg))
    cfg.seed = seed
    state = lib.kg_create(ctypes.byref(cfg))
    env = make("kaggriculture", configuration=official_configuration(cfg), debug=True)
    env.reset()
    passed = {"farmer": ["PASS"], "hands": [], "market": []}
    try:
        # Kaggle's episodeSteps includes the initial observation, hence 719
        # action frames for the default value of 720.
        gates = {}
        for step in range(cfg.episode_steps - 1):
            obs = json.loads(json.dumps(env.state[seat]["observation"]))
            native_obs = (ctypes.c_uint8 * learned_submission.OBS_SIZE)()
            native_mask = (ctypes.c_uint8 * learned_submission.MASK_SIZE)()
            lib.kg_top_policy_input(state, seat, native_obs, native_mask)
            py_obs = learned_submission.encode_observation(obs)
            py_mask = learned_submission.action_mask(obs)
            if bytes(native_obs) != py_obs.tobytes():
                mismatch = next(i for i, (a, b) in enumerate(
                    zip(bytes(native_obs), py_obs.tobytes())) if a != b)
                raise AssertionError(f"seed={seed} seat={seat} step={step} "
                    f"observation mismatch at {mismatch}: "
                    f"native={native_obs[mismatch]} python={py_obs[mismatch]}")
            if bytes(native_mask) != py_mask.astype("uint8").tobytes():
                mismatch = next(i for i, (a, b) in enumerate(
                    zip(bytes(native_mask), py_mask.astype("uint8").tobytes()))
                    if a != b)
                raise AssertionError(f"seed={seed} seat={seat} step={step} "
                    f"mask mismatch at {mismatch}: "
                    f"native={native_mask[mismatch]} python={int(py_mask[mismatch])}")
            py_action = top_bot.agent(obs)
            native_action = CAction()
            lib.kg_top_bot_action(state, seat, ctypes.byref(native_action))
            difference = action_difference(normalized(py_action), native_action)
            if difference:
                raise AssertionError(f"seed={seed} seat={seat} step={step} "
                                     f"action mismatch: {difference}\nPython={py_action}")
            actions = [passed, passed]
            native_actions = [c_action(passed), c_action(passed)]
            actions[seat] = py_action
            native_actions[seat] = native_action
            if opponent == "mirror":
                other = 1 - seat
                other_obs = json.loads(json.dumps(env.state[other]["observation"]))
                actions[other] = top_bot.agent(other_obs)
                other_native = CAction()
                lib.kg_top_bot_action(state, other, ctypes.byref(other_native))
                difference = action_difference(
                    normalized(actions[other]), other_native)
                if difference:
                    raise AssertionError(
                        f"mirror seed={seed} seat={other} step={step} "
                        f"action mismatch: {difference}")
                native_actions[other] = other_native
            env.step(actions)
            pair = (CAction * 2)(*native_actions)
            lib.kg_step(state, pair)
            differences = first_difference(canonical_official(env), c_snapshot(lib, state))
            if differences:
                raise AssertionError(f"seed={seed} seat={seat} step={step} "
                                     f"state mismatch: {differences}")
            if step + 1 in (26, 72):
                gates[step + 1] = animal_count(canonical_official(env), seat)
        snapshot = canonical_official(env)
        money = snapshot["farms"][seat]["money"]
        if gates[26] < 3:
            raise AssertionError(f"opening gate failed: only {gates[26]} animals")
        if gates[72] < 3:
            raise AssertionError(f"maintenance gate failed: only {gates[72]} animals")
        print(f"top bot parity seed={seed} seat={seat} opponent={opponent}: "
              f"PASS, animals=(t26:{gates[26]},t72:{gates[72]}), "
              f"final_money={money}")
    finally:
        lib.kg_destroy(state)


if __name__ == "__main__":
    for parity_seed in (7, 42):
        for parity_seat in (0, 1):
            for parity_opponent in ("pass", "mirror"):
                run(parity_seed, parity_seat, parity_opponent)
