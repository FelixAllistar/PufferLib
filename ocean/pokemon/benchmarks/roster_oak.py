"""Resumable all-roster Oak evaluation; writes JSON, CSV, HTML and transcript table."""
import argparse
import concurrent.futures
import csv
import hashlib
import html
import json
import subprocess
import sys
import time
from pathlib import Path

from oak_benchmark import ROOT, PANEL, load_team, timing_summary


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def atomic_json(path, data):
    temporary = path.with_suffix(".tmp")
    temporary.write_text(json.dumps(data, indent=2)+"\n")
    temporary.replace(path)


def aggregate(output, members, games, budget, seed, started):
    rows, details, searches, policies = [], [], [], []
    species_names = {item["id"]:item["name"] for item in json.loads((ROOT/"ocean/pokemon/data/freepick.json").read_text())["species"]}
    for member in members:
        row = {"policy":member["name"], "checkpoint":member["current"]["path"],
            "checkpoint_sha256":member["current"]["sha256"], "team":member["team"], "lead":member["lead"]}
        row["species_names"] = [species_names[int(value)] for value in member["team"].removeprefix("species:").split(",")] if member["team"].startswith("species:") else []
        row["lead_name"] = species_names[int(member["lead"])] if member["lead"] != "None" else "Free choice"
        for mode in ("argmax", "sample"):
            counts = dict(games=0, wins=0, draws=0, losses=0, timeouts=0)
            for team in PANEL:
                path = output / f"ou_leads_rr50m-{member['name']}-{team}-{budget}-{mode}.json"
                if not path.exists():
                    continue
                report = json.loads(path.read_text())
                assert report["checkpoint_sha256"] == member["current"]["sha256"]
                assert report["games"] == games and report["budget"] == budget
                assert report["deterministic"] == (mode == "argmax")
                assert report["wins"]+report["draws"]+report["losses"] == games
                assert sum(record["seat"] == 0 for record in report["records"]) == games//2
                for field in counts:
                    counts[field] += report[field]
                details.append({"policy":member["name"],"mode":mode,"opponent_team":team,
                    **{field:report[field] for field in counts},
                    "score_percent":100*(report["wins"]+.5*report["draws"])/report["games"]})
                searches.extend(report["timing"]["oak_search_seconds"])
                policies.extend(report["timing"]["policy_inference_seconds"])
            counts["score_percent"] = 100*(counts["wins"]+.5*counts["draws"])/counts["games"] if counts["games"] else None
            row[mode] = counts
        rows.append(row)
    rows.sort(key=lambda row: -(row["argmax"]["score_percent"] or 0))
    summary = {"league":"ou_leads_rr50m", "oak_version":"1.1.4", "budget":budget,
        "games_per_team_mode":games,"seed":seed,"panel":PANEL,"rows":rows,"team_details":details,
        "completed_blocks":len(details),"planned_blocks":len(members)*2*len(PANEL),
        "games_completed":sum(d["games"] for d in details),
        "elapsed_seconds":time.monotonic()-started,
        "timing":{"oak_search":timing_summary(searches),"policy_inference":timing_summary(policies)},
        "limitations":["Oak sees the full native state; learned policies see private observations.",
            "Oak searches in its own engine fork, which is not transition-identical to the authoritative native engine.",
            "Timing is wall clock on this machine; three benchmark workers run concurrently.",
            "Specialists have fixed species/lead but learned moves; the master drafts freely.",
            "Latest checkpoint versions are mixed round 2/3; this is not a same-training-budget comparison.",
            "Fixed battle/draft seeds and alternating seats; Oak search RNG is internally seeded.",
            "Score = (wins + half draws) / games; a draw may be a 512-update timeout."]}
    probe_path = output/"speed_probe/ou_leads_rr50m-master-zam_egg_mie-4096-argmax.json"
    if probe_path.exists():
        probe = json.loads(probe_path.read_text())
        summary["pre_grid_speed_probe"] = {"games":probe["games"],
            "oak_search":probe["timing"]["oak_search"],
            "policy_inference":probe["timing"]["policy_inference"],
            "note":"Excluded from matchup totals. One Oak worker, with brief overlap from the CPU parity test."}
    atomic_json(output/"summary.json",summary)
    columns = ["policy"]+[f"{m}_{f}" for m in ("argmax","sample") for f in ("games","wins","draws","losses","timeouts","score_percent")]
    with (output/"results.csv").open("w", newline="") as stream:
        writer=csv.DictWriter(stream,fieldnames=columns); writer.writeheader()
        for row in rows:
            writer.writerow({"policy":row["policy"], **{f"{m}_{f}":row[m][f] for m in ("argmax","sample") for f in ("games","wins","draws","losses","timeouts","score_percent")}})
    with (output/"team_details.csv").open("w", newline="") as stream:
        writer=csv.DictWriter(stream,fieldnames=["policy","mode","opponent_team","games","wins","draws","losses","timeouts","score_percent"])
        writer.writeheader(); writer.writerows(details)
    table = ["| Policy | Highest-probability score | W–D–L | Sampled score | W–D–L |",
        "|:--|--:|:--|--:|:--|"]
    html_rows = []
    for row in rows:
        cells=[row["policy"]]
        for mode in ("argmax","sample"):
            count=row[mode]
            cells.extend([f"{count['score_percent']:.2f}%" if count["games"] else "pending",
                f"{count['wins']}–{count['draws']}–{count['losses']}"])
        table.append("| " + " | ".join(cells) + " |")
        html_rows.append("<tr>"+"".join(f"<td>{html.escape(cell)}</td>" for cell in cells)+"</tr>")
    (output/"results.txt").write_text("\n".join(table)+"\n")
    timing=summary["timing"]
    timing_text="; ".join(f"{name}: mean {value['mean_ms']:.3f} ms, median {value['median_ms']:.3f} ms, p95 {value['p95_ms']:.3f} ms ({value['calls']:,} calls)" for name,value in timing.items() if value["calls"])
    document='<!doctype html><meta charset="utf-8"><title>All 14 policies vs Oak</title><style>body{font:16px system-ui;max-width:1100px;margin:40px auto;padding:0 20px;color:#20242b}table{border-collapse:collapse;width:100%}td,th{text-align:left;padding:10px;border-bottom:1px solid #ddd}th{background:#eef1f6}tbody tr:nth-child(even){background:#f8fafc}td:nth-child(2),td:nth-child(4){font-variant-numeric:tabular-nums;font-weight:600}p{line-height:1.6}</style>'
    document += f'<h1>All 14 OU policies vs Oak</h1><p>{summary["games_completed"]:,} / {len(members)*2*len(PANEL)*games:,} games complete; {games} games per opponent team and mode, alternating seats. Oak: 4,096-iteration fp/UCB search. Score counts draws as half a win.</p>'
    document += '<table><thead><tr><th>Policy</th><th>Highest-probability score</th><th>W–D–L</th><th>Sampled score</th><th>W–D–L</th></tr></thead><tbody>'+"".join(html_rows)+'</tbody></table>'
    document += '<h2>Measured decision speed</h2><p>'+html.escape(timing_text)+'</p><h2>Benchmark qualifications</h2><ul>'+"".join('<li>'+html.escape(note)+'</li>' for note in summary["limitations"] )+'</ul>'
    document += '<p>Short names: zam = Alakazam; egg = Exeggutor; mie = Starmie; gar = Gengar; zap = Zapdos; cloy = Cloyster; don = Rhydon. The labels describe team cores, not necessarily the full six.</p>'
    document += '<details><summary>Opponent teams and per-team results</summary><p>'+"; ".join(f'<a href="{html.escape(url)}">{html.escape(team)}</a>' for team,url in PANEL.items())+'</p><table><thead><tr><th>Policy</th><th>Mode</th><th>Oak team</th><th>Games</th><th>W–D–L</th><th>Score</th></tr></thead><tbody>'
    for detail in details:
        cells=[detail["policy"],detail["mode"],detail["opponent_team"],str(detail["games"]),
            f"{detail['wins']}–{detail['draws']}–{detail['losses']}",f"{detail['score_percent']:.2f}%"]
        document += '<tr>'+"".join('<td>'+html.escape(cell)+'</td>' for cell in cells)+'</tr>'
    document += '</tbody></table></details><details><summary>Checkpoint and full teams</summary><table><thead><tr><th>Policy</th><th>Checkpoint version</th><th>Species</th><th>Lead</th></tr></thead><tbody>'
    for row in rows:
        cells=[row["policy"],Path(row["checkpoint"]).parent.name,", ".join(row["species_names"]) or "Free draft",row["lead_name"]]
        document += '<tr>'+"".join('<td>'+html.escape(cell)+'</td>' for cell in cells)+'</tr>'
    document += '</tbody></table></details>'
    (output/"results.html").write_text(document)
    return summary


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--games",type=int,default=128)
    parser.add_argument("--workers",type=int,default=3)
    parser.add_argument("--budget",type=int,default=4096)
    parser.add_argument("--seed",type=int,default=20261014)
    parser.add_argument("--summarize-only",action="store_true")
    args=parser.parse_args()
    if args.games < 2 or args.games % 2 or args.workers < 1:
        parser.error("Need positive workers and an even game count >= 2")
    args.output=args.output.resolve();args.output.mkdir(parents=True,exist_ok=True)
    state=json.loads((ROOT/"leagues/pokemon/ou_leads_rr50m/state.json").read_text())
    members=state["members"]
    assert len(members)==14, "Roster changed; audit it before proceeding"
    for member in members:
        assert sha(member["current"]["path"]) == member["current"]["sha256"], member["name"]
        assert sha(Path(member["current"]["path"]).with_name("config.ini")) == member["current"]["config_sha256"], member["name"]
    sources={team:load_team(team,args.output)[1] for team in PANEL}
    manifest={"members":members,"games_per_block":args.games,"budget":args.budget,"seed":args.seed,
        "workers":args.workers,"team_sha256":sources,
        "adapter_sha256":sha(ROOT/"build/pokemon/benchmark_adapter_blas.so"),
        "runner_sha256":sha(Path(__file__).with_name("oak_benchmark.py"))}
    manifest_path=args.output/"manifest.json"
    if manifest_path.exists():
        assert json.loads(manifest_path.read_text()) == manifest, "Protocol/input changed; choose a fresh output directory"
    else:
        atomic_json(manifest_path,manifest)
    tasks=[]
    for member in members:
        for mode in ("argmax","sample"):
            for team in PANEL:
                tasks.append((member,mode,team))
    started=time.monotonic()
    if args.summarize_only:
        previous=args.output/"summary.json"
        if previous.exists():
            started -= json.loads(previous.read_text())["elapsed_seconds"]
        summary=aggregate(args.output,members,args.games,args.budget,args.seed,started)
        print((args.output/"results.txt").read_text(),flush=True)
        print(json.dumps(summary["timing"],indent=2),flush=True)
        return

    def run(task):
        member,mode,team=task
        key=f"ou_leads_rr50m-{member['name']}-{team}-{args.budget}-{mode}"
        path=args.output/(key+".json")
        if path.exists():
            result=json.loads(path.read_text())
            assert result["games"]==args.games and result["checkpoint_sha256"]==member["current"]["sha256"] and result["team_sha256"]==sources[team]
            return key,"cached"
        command=[sys.executable,str(Path(__file__).with_name("oak_benchmark.py")),member["current"]["path"],"--team",team,"--games",str(args.games),"--budget",str(args.budget),"--seed",str(args.seed),"--fast","--output",str(args.output)]
        if mode=="argmax":command.append("--deterministic")
        with (args.output/(key+".log")).open("w") as log:
            subprocess.run(command,stdout=log,stderr=subprocess.STDOUT,check=True,cwd=ROOT)
        result=json.loads(path.read_text())
        assert result["adapter_sha256"]==manifest["adapter_sha256"] and result["team_sha256"]==sources[team]
        return key,"finished"

    with concurrent.futures.ThreadPoolExecutor(max_workers=args.workers) as pool:
        futures=[pool.submit(run,task) for task in tasks]
        pending=set(futures)
        while pending:
            done,pending=concurrent.futures.wait(pending,timeout=30,return_when=concurrent.futures.FIRST_COMPLETED)
            for future in done:
                key,status=future.result()
                print(f"{status}: {key}",flush=True)
            summary=aggregate(args.output,members,args.games,args.budget,args.seed,started)
            print(f"Progress: {summary['completed_blocks']}/{summary['planned_blocks']} blocks; {summary['games_completed']:,} games; {time.monotonic()-started:.0f}s",flush=True)
    summary=aggregate(args.output,members,args.games,args.budget,args.seed,started)
    assert summary["completed_blocks"]==summary["planned_blocks"]
    print((args.output/"results.txt").read_text(),flush=True)
    print(json.dumps(summary["timing"],indent=2),flush=True)


if __name__=="__main__":
    main()
