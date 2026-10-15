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


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=("prepare", "verify", "run"))
    parser.add_argument("--state", type=Path, default=ROOT / "leagues/pokemon/ou_leads_rr50m/state.json")
    parser.add_argument("--output", type=Path, default=EXPORT)
    parser.add_argument("--profile", type=Path, default=PROFILE)
    parser.add_argument("--binary", type=Path, default=ROOT / "build/pokemon/puffer_native")
    parser.add_argument("--mode", choices=("sweep", "train"), default="sweep")
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
    steps = learner_steps(config)
    if args.mode == "sweep" and steps < 25_000_000:
        raise ValueError("sweep trials must train at least 25 million learner steps")
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
