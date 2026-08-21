"""Closed-loop smoke and behavioral gates for a packaged learned policy."""

import argparse
import importlib.util
import json
import os
from pathlib import Path

from kaggle_environments import make


def load_agent(source, model, deterministic=True):
    os.environ["PUFFERLIB_MODEL_PATH"] = str(Path(model).resolve())
    # Keep the historical deterministic default, but make the stochastic
    # export path testable with the exact same packaged source/model.  The
    # environment variable is read when main.py is imported, so it must be set
    # before loading the module.
    os.environ["PUFFERLIB_DETERMINISTIC"] = "1" if deterministic else "0"
    spec = importlib.util.spec_from_file_location("kag_export", source)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def pass_action(obs):
    player = obs["player"]
    return {"farmer": ["PASS"],
            "hands": [["PASS"] for _ in obs["farms"][player]["hands"]],
            "market": []}


def run(module, seed, seat, steps):
    env = make("kaggriculture",
               configuration={"episodeSteps": steps, "seed": seed},
               debug=True)
    env.reset()
    module._MODEL.reset()
    feeds = places = buys = 0
    for _ in range(steps - 1):
        actions = []
        for player in range(2):
            obs = json.loads(json.dumps(env.state[player]["observation"]))
            action = module.agent(obs) if player == seat else pass_action(obs)
            if player == seat:
                units = [action["farmer"]] + action.get("hands", [])
                feeds += sum(command[0] == "FEED" for command in units)
                places += sum(command[0] == "PLACE" for command in units)
                buys += sum(order[0] == "BUY_ANIMAL"
                            for order in action.get("market", []))
            actions.append(action)
        env.step(actions)
    obs = env.state[seat]["observation"]
    farm = obs["farms"][seat]
    animals = sum(bool(isinstance(tile, dict)
                       and tile.get("kind") in ("COOP", "PASTURE")
                       and tile.get("animal"))
                  for row in farm["tiles"] for tile in row)
    statuses = tuple(state.status for state in env.state)
    return animals, feeds, places, buys, float(farm["money"]), statuses


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path)
    parser.add_argument("model", type=Path)
    parser.add_argument("--mode", choices=("opening", "recovery", "smoke"),
                        default="smoke")
    parser.add_argument("--stochastic", action="store_true",
                        help="sample masked actions instead of masked argmax")
    args = parser.parse_args()
    module = load_agent(args.source, args.model,
                        deterministic=not args.stochastic)
    steps = 27 if args.mode == "opening" else 96 if args.mode == "recovery" else 720
    for seed in (7, 42):
        for seat in (0, 1):
            result = run(module, seed, seat, steps)
            animals, feeds, places, buys, money, statuses = result
            policy_mode = "stochastic" if args.stochastic else "deterministic"
            print(f"export policy={policy_mode} mode={args.mode} seed={seed} seat={seat} "
                  f"animals={animals} feeds={feeds} places={places} "
                  f"animal_buys={buys} money={money:.0f} status={statuses}")
            if args.mode == "opening" and not (
                    animals >= 4 and places >= 4 and buys >= 2):
                raise SystemExit("opening export gate failed")
            if args.mode == "recovery" and not (
                    animals >= 3 and feeds >= 8):
                raise SystemExit("recovery export gate failed")
            if args.mode == "smoke" and statuses != ("DONE", "DONE"):
                raise SystemExit("full-game export smoke failed")


if __name__ == "__main__":
    main()
