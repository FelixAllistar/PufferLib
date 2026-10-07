"""Single-config direct PufferNet launches and the requested six-model BC grid."""
import argparse
import configparser
import hashlib
import json
import math
import os
from pathlib import Path
import re
import shlex
import struct
import subprocess
import time

ROOT = Path(__file__).resolve().parents[2]
os.sys.path.insert(0, str(Path(__file__).resolve().parent))
from compact_contract import OBS, HEADS, PACKED, STEPS, POLICY_VERSION, OBSERVATION_VERSION
def grid_shapes(ini):
    widths = [int(x) for x in ini["bc_grid"]["hidden_sizes"].split(",")]
    depths = [int(x) for x in ini["bc_grid"]["num_layers"].split(",")]
    assert widths and depths and set(widths) <= {256,512,1024} and set(depths) <= {2,3}
    assert len(widths) == len(set(widths)) and len(depths) == len(set(depths))
    return [(h,l) for h in widths for l in depths]


def digest(path):
    with Path(path).open("rb") as stream:
        return hashlib.file_digest(stream,"sha256").hexdigest()


def settings():
    ini = configparser.ConfigParser(interpolation=None,inline_comment_prefixes=("#",";"))
    ini.read([ROOT/"config/default.ini", ROOT/"config/kaggriculture.ini"])
    assert ini.getint("policy","action_version") == POLICY_VERSION
    return ini


def model_path(ini,h,l,kind="bc"):
    return ROOT/ini["bc_grid"]["output_root"]/f"h{h}_l{l}"/f"{kind}.bin"


def check_dataset(path):
    with path.open("rb") as stream:
        header = struct.unpack("<16IQQd",stream.read(88))
    assert header[0:2] == (0x4b414742,3)
    assert header[3:6] == (OBS,HEADS,PACKED) and header[8:15] == (4,OBSERVATION_VERSION,POLICY_VERSION,0,0,1,0)
    assert header[-1] == 1 and 0 < header[15] < header[6]
    assert header[2] == header[6]*STEPS and path.stat().st_size == 88+header[2]*(OBS*4+HEADS*4+PACKED+4)
    metadata = json.loads(path.with_suffix(".json").read_text())
    assert metadata["sha256"] == digest(path), "dataset digest mismatch"
    return metadata


def completed_checkpoint(output, expected, resume=False):
    """Only skip a complete, matching, checksum-verified checkpoint/receipt pair."""
    receipt_path = output.with_suffix(".json")
    if not output.exists() and not receipt_path.exists():
        return False
    if not resume:
        raise FileExistsError(f"refusing to replace {output}; use --resume to verify and skip completed models")
    if not output.is_file() or not receipt_path.is_file():
        raise ValueError(f"incomplete checkpoint/receipt pair: {output}; refusing to overwrite or silently skip")
    receipt = json.loads(receipt_path.read_text())
    for key, value in expected.items():
        # Receipts written before --resume did not snapshot default.ini. Their
        # main config, exact command, dataset and trainer are still checked.
        if key == "default_config" and key not in receipt:
            continue
        if receipt.get(key) != value:
            raise ValueError(f"{output}: resume receipt mismatch for {key}")
    size = output.stat().st_size
    if not size or size % 4 or receipt.get("parameters") != size // 4:
        raise ValueError(f"{output}: checkpoint size does not match receipt")
    if receipt.get("sha256") != digest(output):
        raise ValueError(f"{output}: checkpoint checksum mismatch")
    if not isinstance(receipt.get("selected_epoch"), int) or receipt["selected_epoch"] < 0:
        raise ValueError(f"{output}: missing held-out selection record")
    loss = receipt.get("heldout_loss")
    if not isinstance(loss, (int, float)) or not math.isfinite(loss) or loss < 0:
        raise ValueError(f"{output}: invalid held-out loss")
    return True


def launch(command,dry_run,log=None,preflight=False):
    print(shlex.join(map(str,command)),flush=True)
    if dry_run:
        return
    if log is None:
        subprocess.run(command,cwd=ROOT,check=True)
        return
    log.parent.mkdir(parents=True,exist_ok=True)
    with log.open("x") as stream:
        commands = [[os.sys.executable, str(ROOT/"ocean/kaggriculture_direct/check_cuda.py")]] if preflight else []
        for invocation in commands + [command]:
            process = subprocess.Popen(invocation,cwd=ROOT,stdout=subprocess.PIPE,
                                       stderr=subprocess.STDOUT,text=True,bufsize=1)
            for line in process.stdout:
                print(line,end="",flush=True); stream.write(line); stream.flush()
            if process.wait():
                raise subprocess.CalledProcessError(process.returncode,invocation)


def run_bc(args, ini, shapes, overrides):
    data = ROOT/ini["bc"]["data"]
    metadata = None if args.dry_run else check_dataset(data)
    common = {} if args.dry_run else dict(
        policy=POLICY_VERSION, observation=OBSERVATION_VERSION, macro=0, executor=0, optimizer="Adam",
        config=(ROOT/"config/kaggriculture.ini").read_text(),
        default_config=(ROOT/"config/default.ini").read_text(),
        dataset_sha256=metadata["sha256"], trainer_sha256=digest(args.bc_binary))
    pending = []
    # Validate the entire queue before launching anything. Existing checkpoints
    # are never overwritten, even if a later shape has a bad/partial receipt.
    for h, l in shapes:
        critic = args.mode == "critic"
        output = model_path(ini,h,l,"critic" if critic else "bc")
        initial = str(model_path(ini,h,l)) if critic else "None"
        command = [str(args.bc_binary),f"--policy.hidden_size={h}",f"--policy.num_layers={l}",
                   f"--bc.mode={'critic' if critic else 'actor'}",f"--bc.data={data}",
                   f"--base.load_model_path={initial}",f"--bc.output={output}"]+overrides
        expected = dict(common, hidden=h, layers=l, mode="critic" if critic else "actor", command=command)
        if not args.dry_run and completed_checkpoint(output, expected, args.resume):
            print(f"BC resume: verified {h}x{l}, keeping {output}",flush=True)
        else:
            pending.append((output, command, expected))
    for output, command, expected in pending:
        started = time.time()
        log = output.with_suffix(f".{time.time_ns()}.log")
        launch(command,args.dry_run,log,preflight=True)
        if not args.dry_run:
            receipt = dict(expected, parameters=output.stat().st_size//4, sha256=digest(output),
                           seconds=time.time()-started, log=str(log))
            selected = re.search(r"BC selected_epoch=(\d+) heldout_loss=(\S+)",log.read_text())
            if selected:
                receipt.update(selected_epoch=int(selected[1]),heldout_loss=float(selected[2]))
            with output.with_suffix(".json").open("x") as stream:
                json.dump(receipt,stream,indent=2)
    if not pending:
        print("BC queue complete: all requested checkpoints verified; nothing to train.",flush=True)


def sweep_results(ini):
    folder = ROOT/ini["base"]["log_dir"]/"kaggriculture"
    rows = []
    for path in folder.glob("sweep_*/result.json"):
        result = json.loads(path.read_text())
        if result.get("status") == "complete":
            rows.append(result)
    print("Fresh-start mean cash | win rate | steps | checkpoint")
    for result in sorted(rows, key=lambda r: r["score"], reverse=True):
        print(f"${result['score']:,.2f} | {result['win_rate']:.1%} | {result['steps']:,} | {result['checkpoint']}")
    if not rows:
        print("No completed sweep results yet.")


def main():
    transformer_modes = {"transformer-ppo": ("transformer_ppo.py", "train"),
                         "transformer-ppo-smoke": ("transformer_ppo.py", "smoke"),
                         "transformer-eval": ("transformer.py", "eval")}
    if len(os.sys.argv) > 1 and os.sys.argv[1] in transformer_modes:
        script, mode = transformer_modes[os.sys.argv[1]]
        return subprocess.run([os.sys.executable, str(ROOT/"ocean/kaggriculture_direct"/script),
                               mode, *os.sys.argv[2:]], cwd=ROOT).returncode
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mode",choices=("train","eval","match","sweep","sweep-results","build-bc","prepare-bc","bc","bc-grid","critic"))
    parser.add_argument("--hidden",type=int,choices=(256,512,1024))
    parser.add_argument("--layers",type=int,choices=(2,3))
    parser.add_argument("--binary",type=Path,default=ROOT/"puffer")
    parser.add_argument("--bc-binary",type=Path,default=ROOT/"build/kaggriculture_direct_bc")
    parser.add_argument("--manifest",type=Path,default=ROOT/"data/kaggriculture/terminal.json")
    parser.add_argument("--tape-root",type=Path,default=ROOT/"data/kaggriculture/tapes")
    parser.add_argument("--teacher",action="append")
    parser.add_argument("--dry-run",action="store_true")
    parser.add_argument("--resume",action="store_true",
                        help="BC only: verify and skip completed models; unfinished models start fresh")
    args, overrides=parser.parse_known_args()
    if args.resume and args.mode not in ("bc","bc-grid","critic"):
        parser.error("--resume is only supported for bc, bc-grid and critic")
    if any(not x.startswith("--") or "." not in x or "=" not in x for x in overrides):
        parser.error("use native --section.key=value overrides; legacy --profile overlays are retired")
    ini=settings()
    args.binary = ROOT/args.binary
    if args.mode == "sweep-results":
        if overrides or args.hidden or args.layers:
            parser.error("sweep-results reads completed results using the shared config")
        sweep_results(ini)
        return 0
    h=args.hidden or ini.getint("policy","hidden_size")
    l=args.layers or ini.getint("policy","num_layers")
    if args.mode=="build-bc":
        if overrides: parser.error("build-bc takes no training overrides")
        launch(["bash","ocean/kaggriculture_direct/build_bc.sh",args.bc_binary],args.dry_run)
    elif args.mode=="prepare-bc":
        if overrides: parser.error("edit the shared config or use --teacher")
        launch(["make","-C","ocean/kaggriculture_direct","replay-bridge"],args.dry_run)
        command=[os.sys.executable,"ocean/kaggriculture_direct/build_bc_dataset.py",
                 "--manifest",args.manifest,"--tape-root",args.tape_root,"--output",ROOT/ini["bc"]["data"]]
        for teacher in args.teacher or [ini["bc_grid"]["teacher"]]: command += ["--teacher",teacher]
        launch(command,args.dry_run)
    elif args.mode in ("bc","bc-grid","critic"):
        if args.mode=="bc-grid" and (args.hidden or args.layers): parser.error("bc-grid builds all six shapes")
        reserved=("--bc.output=","--bc.mode=","--base.load_model_path=","--policy.hidden_size=","--policy.num_layers=","--bc.data=")
        if any(o.startswith(reserved) for o in overrides):
            parser.error("BC architecture, dataset and output come from the shared config/--hidden/--layers")
        shapes=grid_shapes(ini) if args.mode=="bc-grid" else [(h,l)]
        try:
            run_bc(args,ini,shapes,overrides)
        except (OSError, ValueError, subprocess.CalledProcessError) as error:
            print(f"BC queue stopped: {error}\nCompleted checkpoints and logs are kept. "
                  "After resolving the error, rerun the same command with --resume.",file=os.sys.stderr)
            return 1
    else:
        command=[str(args.binary),args.mode]
        if args.mode == "sweep" and (args.hidden or args.layers):
            parser.error("sweep architecture is fixed in the single shared config; edit its fixed bounds too")
        if args.hidden or args.layers:
            command += [f"--policy.hidden_size={h}",f"--policy.num_layers={l}",
                        f"--base.load_model_path={model_path(ini,h,l)}",
                        f"--base.teacher_model_path={model_path(ini,h,l)}"]
        if args.mode in ("eval","match"):
            command += ["--headless","--env.reset_state_prob=0","--base.eval_episodes=64",
                        f"--env.num_agents={2 if args.mode=='match' else 1}"]
            if args.mode == "eval":
                command += ["--vec.num_policies=1", "--vec.hist_policy_percent=0",
                            "--selfplay.enabled=0", "--train.teacher_kl_coefficient=0"]
        if args.mode == "sweep":
            command += [f"--sweep.trainer_path={args.binary}"]
            log = ROOT/"logs/kaggriculture"/f"sweep.{time.time_ns()}.log"
            launch(command+overrides,args.dry_run,log)
        else:
            launch(command+overrides,args.dry_run)
    return 0


if __name__=="__main__":
    raise SystemExit(main())
