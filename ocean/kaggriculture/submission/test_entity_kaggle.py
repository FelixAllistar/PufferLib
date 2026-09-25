"""Local official-package games; not competition-container certification.

Run against an unpacked archive. Does not upload a submission or change it.
"""
import argparse
import hashlib
import importlib.metadata
import importlib.util
import json
from pathlib import Path
import statistics
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("package", type=Path)
    parser.add_argument("--runner", action="store_true",
        help="Use the official Agent file-loading loop, rather than direct calls")
    args = parser.parse_args()
    from kaggle_environments import make
    from kaggle_environments.agent import get_last_callable

    module = importlib.util.find_spec("kaggle_environments.envs.kaggriculture.kaggriculture")
    print(json.dumps(dict(kaggle_environments=importlib.metadata.version("kaggle-environments"),
        environment_sha256=hashlib.sha256(Path(module.origin).read_bytes()).hexdigest())), flush=True)
    source = args.package.resolve() / "main.py"
    metadata = json.loads(source.with_name("policy_metadata.json").read_text())
    for name in ["main.py", "model.bin", "entity_bridge.so"]:
        digest = hashlib.sha256(source.with_name(name).read_bytes()).hexdigest()
        assert digest == metadata["files_sha256"][name], f"package hash mismatch: {name}"
    print(json.dumps(dict(sampling="deterministic" if metadata["deterministic"] else "stochastic",
        files_sha256=metadata["files_sha256"])), flush=True)
    for seed in (7, 42):
        for seat in (0, 1):
            env = make("kaggriculture", configuration={"seed": seed, "episodeSteps": 720},
                debug=True)
            env.reset()
            times = []
            if args.runner:
                agents = ["pass", "pass"]
                agents[seat] = str(source)
                env.run(agents)
            else:
                play = get_last_callable(source.read_text(), path=str(source))
                try:
                    for step in range(719):
                        actions = []
                        for player in (0, 1):
                            observation = json.loads(json.dumps(env.state[player].observation))
                            if player == seat:
                                begin = time.perf_counter()
                                action = play(observation, env.configuration)
                                times.append(time.perf_counter() - begin)
                            else:
                                action = {"farmer": ["PASS"], "hands": [], "market": []}
                            actions.append(action)
                        env.step(actions)
                        assert all(s.status not in ("ERROR", "INVALID", "TIMEOUT")
                            for s in env.state), f"failed at {seed=} {seat=} {step=}"
                finally:
                    controller = play.__globals__.get("_CONTROLLER")
                    if controller:
                        controller.close()
            statuses = [s.status for s in env.state]
            assert statuses == ["DONE", "DONE"], statuses
            assert len(env.steps) == 720
            print(json.dumps(dict(seed=seed, seat=seat, runner=args.runner,
                frames=len(env.steps), statuses=statuses,
                money=env.state[seat].observation.farms[seat].money,
                mean_ms=1000 * statistics.mean(times) if times else None,
                max_ms=1000 * max(times) if times else None)), flush=True)


if __name__ == "__main__":
    main()
