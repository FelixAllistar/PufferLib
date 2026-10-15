#!/usr/bin/env python3
"""Prepare immutable old opponents, then exec the native single-policy sweep."""
import argparse
import configparser
import hashlib
import json
import math
import os
from pathlib import Path
import shutil

ROOT = Path(__file__).resolve().parents[2]
PROFILE = ROOT / "ocean/pokemon/profiles/starmie_oak.ini"
EXPORT = ROOT / "saved/pokemon/starmie_sweep"


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def read_config(profile=PROFILE, overrides=()):
    config = configparser.ConfigParser(interpolation=None)
    config.read([ROOT / "config/default.ini", ROOT / "config/pokemon.ini"])
    overlay = configparser.ConfigParser(interpolation=None)
    with Path(profile).open() as stream:
        overlay.read_file(stream)
    config.read_dict({section: dict(overlay[section]) for section in overlay.sections()})
    for arg in overrides:
        if not arg.startswith("--") or "=" not in arg:
            raise ValueError(f"expected --section.key=value, got {arg}")
        key, value = arg[2:].split("=", 1)
        section, key = key.rsplit(".", 1)
        if section not in config or key not in config[section]:
            raise ValueError(f"unknown override {section}.{key}")
        config[section][key] = value
    # A native sweep inherits many generic ranges. Freeze every range that this
    # profile did not explicitly choose, including architecture and trial size.
    for section in config.sections():
        if section.startswith("sweep.") and section not in overlay:
            parent, key = section[6:].rsplit(".", 1)
            config[section]["min"] = config[section]["max"] = config[parent][key]
    return config


def learner_steps(config):
    agents = config.getint("vec", "total_agents")
    buffers = config.getint("vec", "num_buffers")
    horizon = config.getint("train", "horizon")
    if agents < 2 or buffers < 1 or agents % (2 * buffers):
        raise ValueError("agents must be divisible by twice the buffer count")
    frozen_envs = int(config.getfloat("env", "expert_fraction") * agents / (2 * buffers)) * buffers
    learner_agents = agents - frozen_envs
    minibatch = config.getint("train", "minibatch_size")
    if horizon <= 0 or minibatch <= 0 or minibatch % horizon or learner_agents % (minibatch // horizon):
        raise ValueError("minibatch rows must divide learner agents")
    rollouts = config.getint("train", "total_timesteps") // (agents * horizon)
    return rollouts * horizon * learner_agents


def prepare(state_path, output):
    state = json.loads(state_path.read_text())
    members = state["members"]
    if state["architecture"] != {"hidden": 64, "layers": 2} or len(members) != 14:
        raise ValueError("expected the 14-member H64/L2 OU roster")
    checked = []
    for member in members:
        name = member["name"]
        if not name or Path(name).name != name or name in (".", ".."):
            raise ValueError("unsafe member name")
        current = member["current"]
        weights = Path(current["path"])
        metadata = weights.with_name("config.ini")
        if sha(weights) != current["sha256"] or sha(metadata) != current["config_sha256"]:
            raise ValueError(f"changed or missing checkpoint: {name}")
        saved = configparser.ConfigParser(interpolation=None)
        saved.read(metadata)
        for key, expected in (("abi_version", "3"), ("policy_version", "3"),
                              ("rules_sha", state["rules_sha"]),
                              ("learner_team", member["team"]), ("learner_lead", member["lead"])):
            if saved["env"][key].strip("'\"") != expected:
                raise ValueError(f"incompatible metadata {name}: {key}")
        checked.append((member, weights, metadata))
    output = output.resolve()
    relative = output.relative_to(ROOT)
    output.mkdir(parents=True, exist_ok=False)
    manifest = configparser.ConfigParser(interpolation=None)
    manifest["native"] = {"version": "3", "banks": str(len(members)), "hidden_size": "64",
                          "num_layers": "2", "rules_sha": state["rules_sha"],
                          "opponents": str(relative / "opponents.txt")}
    paths, inventory = [], []
    for i, (member, weights, metadata) in enumerate(checked):
        directory = output / member["name"]
        directory.mkdir()
        shutil.copy2(weights, directory / "weights.bin")
        shutil.copy2(metadata, directory / "config.ini")
        path = str(relative / member["name"] / "weights.bin")
        paths.append(path)
        manifest[f"bank.{i}"] = {"name": member["name"], "path": path,
                                    "team": member["team"], "lead": member["lead"]}
        inventory.append({"name": member["name"], "path": path,
                          "sha256": sha(directory / "weights.bin"),
                          "config_sha256": sha(directory / "config.ini")})
    (output / "opponents.txt").write_text("\n".join(paths) + "\n")
    with (output / "native.ini").open("x") as stream:
        manifest.write(stream)
    (output / "inventory.json").write_text(json.dumps(inventory, indent=2) + "\n")
    print(f"Exported and SHA-256 verified {len(inventory)} frozen policies, including OU master: {output}")


def verify(output):
    inventory = json.loads((output / "inventory.json").read_text())
    for row in inventory:
        path = ROOT / row["path"]
        if sha(path) != row["sha256"] or sha(path.with_name("config.ini")) != row["config_sha256"]:
            raise ValueError(f"frozen opponent changed: {row['name']}")
    print(f"Verified {len(inventory)} immutable opponent checkpoints and metadata.", flush=True)


def search_parameters(config):
    return [section[6:] for section in config.sections() if section.startswith("sweep.")
            and float(config[section]["min"]) != float(config[section]["max"])]


def history_context(config):
    varying = set(search_parameters(config))
    ignored = {"base.run_id", "base.log_dir", "base.checkpoint_dir", "base.result_fd",
               "base.gpu_offset", "oak.python", "oak.workers"}
    return {section: {key: value.strip("'\"") for key, value in config[section].items()
                      if f"{section}.{key}" not in varying | ignored}
            for section in config.sections() if not section.startswith("sweep")}


def validate_history_context(config, path):
    context_path = Path(str(path) + ".context.json")
    context = history_context(config)
    if context_path.exists():
        if json.loads(context_path.read_text()) != context:
            raise ValueError("sweep history belongs to different fixed training/evaluation settings")
    else:
        if path.exists():
            raise ValueError("existing sweep history is missing its experiment context")
        with context_path.open("x") as stream:
            json.dump(context, stream, indent=2)


def import_history(config, logs, history, checkpoint_root):
    parameters = search_parameters(config)
    rows, provenance = [], []
    seen = set()
    for summary_path in sorted(logs.glob("sweep_*.ini.oak/summary.json")):
        summary = json.loads(summary_path.read_text())
        saved = configparser.ConfigParser(interpolation=None)
        log_path = summary_path.parent.with_suffix("")
        with log_path.open() as stream:
            saved.read_file(stream)
        run = int(saved["base"]["run_id"].rsplit("_", 1)[1])
        if run in seen:
            raise ValueError(f"duplicate trial index {run}; select logs from one sweep")
        seen.add(run)
        fixed = [("policy", key) for key in config["policy"]]
        fixed += [("train", key) for key in config["train"] if "train." + key not in parameters]
        fixed += [("env", key) for key in config["env"]
                  if key not in ("rules_sha", "reward_gamma", "core_next_assigned", "core_next_drafted")]
        fixed += [("vec", key) for key in ("total_agents", "num_buffers", "num_threads", "action_mask_size")]
        fixed += [("base", key) for key in ("seed", "load_model_path", "async", "reset_every_horizon")]
        fixed += [("oak", key) for key in ("games", "budget", "seed")]
        for section, key in fixed:
            expected = config[section][key].strip("'\"")
            if section == "env" and key in ("opponent_team", "opponent_lead"):
                expected = config["env"][key.replace("opponent_", "learner_")]
            actual = saved[section][key].strip("'\"")
            try:
                matches = math.isclose(float(expected), float(actual), rel_tol=2e-6, abs_tol=1e-10)
            except ValueError:
                matches = expected == actual
            if not matches:
                raise ValueError(f"incompatible {log_path}: {section}.{key}={actual}, expected {expected}")
        if saved.getint("selfplay", "opp_timeout_steps") != 0:
            raise ValueError("import requires immutable opponents")
        checkpoint = Path(summary["checkpoint"])
        if not checkpoint.exists():
            checkpoint = checkpoint_root / checkpoint.parent.name / checkpoint.name
        if sha(checkpoint) != summary["sha256"]:
            raise ValueError(f"checkpoint changed: {checkpoint}")
        score = summary["scores"]["argmax"]["score"]
        if not math.isclose(float(saved["metrics"]["env/oak_score"]), score, abs_tol=1e-7):
            raise ValueError("native objective and Oak summary disagree")
        if int(float(saved["metrics"]["agent_steps"])) != config.getint("train", "total_timesteps"):
            raise ValueError("cannot import an incomplete training stint")
        if any(summary["scores"][mode]["games"] != 3 * config.getint("oak", "games")
               for mode in ("argmax", "sample")):
            raise ValueError("incomplete evaluation panel")
        cost = float(saved["metrics"]["uptime"]) + summary["elapsed_seconds"]
        values = [float(saved[section][key]) for section, key in (p.rsplit(".", 1) for p in parameters)]
        if not all(map(math.isfinite, [score, cost, *values])) or cost <= 0:
            raise ValueError("nonfinite history observation")
        rows.append((run, score, cost, values))
        provenance.append({"run": run, "log": str(log_path), "summary": str(summary_path),
                           "checkpoint_sha256": summary["sha256"]})
    if not rows:
        raise ValueError("no completed, Oak-scored trials to import")
    if sorted(seen) != list(range(len(rows))):
        raise ValueError("import expects one contiguous sweep starting at trial zero")
    if history.exists():
        raise FileExistsError(history)
    validate_history_context(config, history)
    with history.open("x") as stream:
        stream.write(f"PUFFER_SWEEP_HISTORY_V1 {len(parameters)}\n")
        for key in parameters:
            bounds = config["sweep." + key]
            stream.write(f"{key} {bounds['min']} {bounds['max']} {bounds['distribution']}\n")
        for run, score, cost, values in sorted(rows):
            stream.write(f"{run} {score:.17g} {cost:.17g} 0 " + " ".join(f"{v:.17g}" for v in values) + "\n")
    Path(str(history) + ".import.json").write_text(json.dumps(provenance, indent=2) + "\n")
    print(f"Imported {len(rows)} completed trials; next trial index={len(rows)}, total target={config['sweep']['max_runs']}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=("prepare", "verify", "import-history", "run"))
    parser.add_argument("--state", type=Path, default=ROOT / "leagues/pokemon/ou_leads_rr50m/state.json")
    parser.add_argument("--output", type=Path, default=EXPORT)
    parser.add_argument("--profile", type=Path, default=PROFILE)
    parser.add_argument("--binary", type=Path, default=ROOT / "build/pokemon/puffer_native")
    parser.add_argument("--mode", choices=("sweep", "train"), default="sweep")
    parser.add_argument("--logs", type=Path, default=ROOT / "logs/pokemon")
    parser.add_argument("--checkpoint-root", type=Path, default=ROOT / "checkpoints/pokemon")
    args, overrides = parser.parse_known_args()
    os.chdir(ROOT)
    if args.command == "prepare":
        if overrides:
            parser.error("prepare accepts no native overrides")
        prepare(args.state, args.output)
        return
    verify(args.output)
    if args.command == "verify":
        return
    config = read_config(args.profile, overrides)
    history = Path(config["sweep"]["history_path"])
    if args.command == "import-history":
        import_history(config, args.logs, history, args.checkpoint_root)
        return
    steps = learner_steps(config)
    if args.mode == "sweep" and steps < 25_000_000:
        raise ValueError("sweep trials must train at least 25 million learner steps")
    if args.mode == "sweep" and str(history) != "None":
        validate_history_context(config, history)
    for section in config.sections():
        if section.startswith("sweep."):
            parent, key = section[6:].rsplit(".", 1)
            lo, hi = (float(config[section][k]) for k in ("min", "max"))
            value = float(config[parent][key])
            if not all(map(math.isfinite, (lo, hi, value))) or not lo <= value <= hi:
                raise ValueError(f"baseline outside range: {section}")
    command = [str(args.binary.resolve(strict=True)), args.mode]
    command.extend("--" + section + "." + key + "=" + value.strip("'\"")
                   for section in config.sections() for key, value in config[section].items())
    print(f"Native {args.mode}: {steps:,} learner steps per trial; fresh learner; frozen opponents + current self-play.", flush=True)
    # Replace this process. Native sweep and its live trainer inherit the tmux
    # terminal directly; there is no log capture, polling UI, or Python worker.
    os.execv(command[0], command)


if __name__ == "__main__":
    main()
