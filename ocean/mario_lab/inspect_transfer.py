#!/usr/bin/env python3
"""Build an offline trace viewer for a transfer.py evaluation bundle."""
import argparse
import array
import html
import json
from pathlib import Path
import re


def build(bundle):
    manifest = json.loads((bundle / "manifest.json").read_text())
    records = [json.loads(x) for x in (bundle / "episodes.jsonl").read_text().splitlines()]
    episodes = [r for r in records if r["type"] == "episode"]
    traced = []
    for path in sorted(bundle.glob("episode_*.jsonl")):
        match = re.fullmatch(r"episode_(\d+)\.jsonl", path.name)
        if not match: continue
        index = int(match[1]); rows = [json.loads(x) for x in path.read_text().splitlines()]
        observations = array.array("f")
        with path.with_suffix(".obs.f32").open("rb") as f:
            observations.frombytes(f.read())
        if len(observations) != len(rows) * 1128:
            raise ValueError("trace and observation counts differ")
        frames = []
        for i, row in enumerate(rows):
            obs = observations[i*1128:(i+1)*1128]
            grid = [sum((1 << channel) for channel in range(5) if obs[88 + cell*5 + channel] > 0.5)
                    for cell in range(208)]
            entities = [list(obs[24 + j*8:32 + j*8]) for j in range(8) if obs[24+j*8] > 0.5]
            frames.append({"tick": row["tick"], "x": row["x_fp"] / 256, "y": row["feet_fp"] / 256,
                           "vx": row["vx_fp"] / 256, "vy": row["vy_fp"] / 256,
                           "grounded": row["grounded"], "action": row["action"],
                           "probs": row["button_probs"], "entropy": row["entropy"],
                           "unknown": row["unknown_cells"], "grid": grid, "entities": entities})
        screenshots = []
        for image in bundle.glob(f"episode_{index}_*.png"):
            suffix = image.stem.split(f"episode_{index}_", 1)[1]
            tick = (rows[0]["tick"] + 1 if suffix == "start" else rows[-1]["tick"] + 1 if suffix == "end"
                    else int(suffix.split("_")[1]))
            screenshots.append({"tick": tick, "path": image.name})
        traced.append({"episode": index, "frames": frames, "screenshots": sorted(screenshots, key=lambda s: s["tick"])})
    if not traced: raise ValueError("bundle has no detailed traces")
    data = json.dumps(traced, separators=(",", ":"), allow_nan=False)
    summary = records[-1]
    title = html.escape(manifest["label"])
    table = "".join(f"<tr><td>{r['episode']}</td><td>{r['status']}</td><td>{r['frames']}</td>"
                    f"<td>{r['max_x']}</td><td>{r['flag_frame']}</td><td>{r['pipe_visits']}/{r['pipe_returns']}</td>"
                    f"<td>{r['big_player_frames']}</td></tr>" for r in episodes)
    document = TEMPLATE.replace("TITLE_TOKEN", title).replace("SUMMARY_TOKEN", html.escape(json.dumps(summary)))
    document = document.replace("TABLE_TOKEN", table).replace("DATA_TOKEN", data)
    target = bundle / "trace_viewer.html"; target.write_text(document)
    print(target)
    return target


TEMPLATE = r'''<!doctype html><html lang="en"><meta charset="utf-8"><title>Mario transfer trace</title>
<style>body{font:16px system-ui;background:#151920;color:#eee;max-width:1150px;margin:24px auto;padding:16px}
canvas{background:#202734;max-width:100%;border:1px solid #536074}button,select,input{font:inherit}input{width:65%}
.panels{display:flex;gap:24px;flex-wrap:wrap}img{width:512px;max-width:100%;image-rendering:pixelated}
table{border-collapse:collapse}td,th{padding:6px 14px;border-bottom:1px solid #536074;text-align:left}
pre{white-space:pre-wrap}p{line-height:1.5}</style>
<h1>TITLE_TOKEN</h1><p>Frozen synthetic policy in natural ROM 1-1. One policy decision per frame.
The semantic view is the actual policy input. Screenshots are sampled separately; their frame is shown.</p>
<pre>SUMMARY_TOKEN</pre><label>Traced episode <select id="episode"></select></label>
<p><button id="play">Play</button> <input id="slider" type="range" min="0" value="0"><span id="tick"></span></p>
<canvas id="curve" width="1000" height="140"></canvas>
<p>Blue: horizontal position. Orange: jump-button probability. White line: selected frame.</p>
<div class="panels"><div><canvas id="grid" width="512" height="416"></canvas>
<p>Gray terrain, brown brick, green usable pipe, blue unsupported space, yellow goal.<br>
Red player and orange entities use the semantic collision coordinates.</p></div>
<div><img id="rom" alt="Sampled ROM frame"><p id="imageTick"></p><pre id="state"></pre></div></div>
<h2>Complete attempts</h2><table><tr><th>Episode</th><th>Outcome</th><th>Frames</th><th>Max X</th>
<th>Flag frame</th><th>Pipes in/out</th><th>Large-player frames</th></tr>TABLE_TOKEN</table>
<script>const data=DATA_TOKEN; const el=id=>document.getElementById(id);
let selected=0,index=0,timer=null;data.forEach((e,i)=>{let o=document.createElement('option');o.value=i;o.textContent=e.episode;el('episode').append(o)});
function draw(){let e=data[selected],f=e.frames[index];el('slider').max=e.frames.length-1;el('slider').value=index;el('tick').textContent='Frame '+f.tick;
let g=el('grid').getContext('2d');g.clearRect(0,0,512,416);
f.grid.forEach((v,i)=>{let x=i%16*32,y=Math.floor(i/16)*32;g.fillStyle=v&16?'#dfc74e':v&4?'#39996f':v&2?'#b47742':v&1?'#89909a':v&8?'#274b67':'#202734';g.fillRect(x,y,31,31)});
let x=4+((f.x+6)%16)/16-6/16,y=8+(f.y%16)/16;
g.fillStyle='#fc5865';g.fillRect(x*32,(y-12/16)*32,20,24);
f.entities.forEach(v=>{g.fillStyle='#ff9f42';g.fillRect((x+v[1]*12)*32,(y+v[2]*8-0.75)*32,24,24)});
el('state').textContent='x '+f.x.toFixed(2)+'  feet '+f.y.toFixed(2)+'\nvx '+f.vx.toFixed(3)+'  vy '+f.vy.toFixed(3)+'\ngrounded '+f.grounded+'  action '+f.action+'\n'+
['A','B','Up','Down','Left','Right'].map((b,i)=>b+': '+f.probs[i].toFixed(3)).join('  ')+'\nentropy '+f.entropy.toFixed(3)+'  unknown cells '+f.unknown;
let image=e.screenshots[0];e.screenshots.forEach(s=>{if(s.tick<=f.tick)image=s});if(image){el('rom').src=image.path;el('imageTick').textContent='Screenshot frame '+image.tick}
let c=el('curve').getContext('2d');c.clearRect(0,0,1000,140);let maximum=Math.max(...e.frames.map(s=>s.x),1);
['x','a'].forEach(key=>{c.strokeStyle=key==='x'?'#6ebaff':'#ff9f42';c.beginPath();e.frames.forEach((s,i)=>{let xx=i/(e.frames.length-1)*1000,yy=135-(key==='x'?s.x/maximum:s.probs[0])*125;i?c.lineTo(xx,yy):c.moveTo(xx,yy)});c.stroke()});
c.strokeStyle='white';c.beginPath();c.moveTo(index/(e.frames.length-1)*1000,0);c.lineTo(index/(e.frames.length-1)*1000,140);c.stroke();}
el('episode').onchange=()=>{selected=Number(el('episode').value);index=0;draw()};el('slider').oninput=()=>{index=Number(el('slider').value);draw()};
el('play').onclick=()=>{if(timer){clearInterval(timer);timer=null;el('play').textContent='Play'}else{el('play').textContent='Pause';timer=setInterval(()=>{index=(index+3)%data[selected].frames.length;draw()},50)}};draw();</script></html>'''


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__); p.add_argument("bundle", type=Path)
    build(p.parse_args().bundle.resolve())
