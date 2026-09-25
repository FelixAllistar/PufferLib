"""Build a local Kaggle archive from an explicit canonical checkpoint/config.

Does not upload, select a champion, or modify training settings. Build on a
Linux x86-64 host compatible with the intended Kaggle runtime.
"""
import argparse
import configparser
import hashlib
import json
import os
from pathlib import Path
import platform
import subprocess
import tarfile
import tempfile

from entity_agent import EntityModel

HERE = Path(__file__).resolve().parent


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--checkpoint", required=True, type=Path)
    parser.add_argument("--config", required=True, type=Path,
        help="Saved canonical run config, not an inferred architecture")
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--sampling", required=True, choices=["deterministic", "stochastic"])
    parser.add_argument("--cc", default="cc")
    args = parser.parse_args()
    assert platform.system() == "Linux" and platform.machine() == "x86_64"
    assert not args.output.exists(), "refusing to overwrite a submission"
    config_text = args.config.read_text()
    config = configparser.ConfigParser(interpolation=None)
    config.read_string(config_text)
    assert config.get("base", "env_name").strip("'\"") == "kaggriculture"
    # The public adapter currently has this fixed controller contract.
    controller = dict(market_slots=10, max_hands=16, land_buy_min_days=0)
    for key, value in controller.items():
        assert config.getint("env", key) == value, f"unsupported controller setting: {key}"
    for key in ["macro_mode", "macro_executor_version", "observation_version"]:
        assert not config.has_option("env", key), "use the canonical run config, not legacy settings"
    hidden = config.getint("policy", "hidden_size")
    layers = config.getint("policy", "num_layers")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="kag-package-", dir=args.output.parent) as directory:
        stage = Path(directory)
        model = stage / "model.bin"
        model.write_bytes(args.checkpoint.read_bytes())
        EntityModel(model, hidden, layers, 8)  # Exact layout/finite-weight validation.
        (stage / "main.py").write_bytes((HERE / "entity_agent.py").read_bytes())
        command = [args.cc, "-O2", "-shared", "-fPIC", str(HERE / "entity_bridge.c"),
            "-lm", "-o", str(stage / "entity_bridge.so")]
        subprocess.run(command, check=True)
        sources = [HERE / "entity_agent.py", HERE / "entity_bridge.c",
            HERE.parent / "policy.h", HERE.parent / "core.h"]
        metadata = dict(policy_version=5, observation_version=3, macro_mode=2,
            macro_executor_version=2, hidden_size=hidden, num_layers=layers,
            param_alignment=8, deterministic=args.sampling == "deterministic",
            controller=controller, parameters=model.stat().st_size // 4,
            config_sha256=hashlib.sha256(config_text.encode()).hexdigest(),
            source_sha256={path.name: hashlib.sha256(path.read_bytes()).hexdigest()
                for path in sources},
            files_sha256={name: hashlib.sha256((stage / name).read_bytes()).hexdigest()
                for name in ["main.py", "model.bin", "entity_bridge.so"]},
            build_platform=platform.platform(), compiler=args.cc,
            official_runtime_verified=False)
        (stage / "policy_metadata.json").write_text(json.dumps(metadata, indent=2) + "\n")
        archive = stage / "submission.tar.gz"
        with tarfile.open(archive, "w:gz") as bundle:
            for name in ["main.py", "model.bin", "entity_bridge.so", "policy_metadata.json"]:
                bundle.add(stage / name, arcname=name)
        # Same-filesystem link publishes the complete archive without replacing a file.
        os.link(archive, args.output)
    print(f"Packaged {args.output} ({args.sampling}); official runtime validation still required")


if __name__ == "__main__":
    main()
