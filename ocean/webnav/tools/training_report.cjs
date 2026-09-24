// Usage: node ocean/webnav/tools/training_report.cjs RUN_DIRECTORY [LABEL ...]
// Consume completed experiments only; reject partial per-task evaluation files.
const fs = require('fs');
const path = require('path');
const [root, ...labels] = process.argv.slice(2);
if (!root || !labels.length) throw Error('Specify run directory and completed experiment labels');
const jsonl = file => fs.readFileSync(file, 'utf8').trim().split('\n').filter(Boolean).map(JSON.parse);
function checkpoints(dir) {
  return fs.readdirSync(dir, {withFileTypes:true}).flatMap(e => {
    const p = path.join(dir, e.name);
    return e.isDirectory() ? checkpoints(p) : p.endsWith('.bin.webnav.json') ? [p] : [];
  });
}
const experiments = labels.map(label => {
  const candidates = checkpoints(path.join(root, label)).sort((a,b) => path.basename(a).localeCompare(path.basename(b)));
  if (!candidates.length) throw Error(`No checkpoint for ${label}`);
  const sidecar = candidates.at(-1), checkpoint = sidecar.slice(0, -12);
  const evaluation = jsonl(path.join(root, `${label}-eval.jsonl`));
  if (evaluation.length !== 12 || new Set(evaluation.map(x => x.task)).size !== 12) throw Error(`Incomplete evaluation: ${label}`);
  const browser = jsonl(path.join(root, `${label}-browser.jsonl`));
  const learnedBrowser = browser.filter(x => x.controller === 'learned-greedy');
  if (learnedBrowser.length !== 5) throw Error(`Incomplete browser evaluation: ${label}`);
  const trainingLog = fs.readFileSync(path.join(root, `${label}.log`), 'utf8');
  const wall = trainingLog.match(/^real (\d+(?:\.\d+)?)$/m);
  if (!wall) throw Error(`Incomplete training: ${label}`);
  const steps = Number(path.basename(checkpoint, '.bin'));
  const total = evaluation.reduce((a,x) => a + x.episodes, 0);
  const successes = evaluation.reduce((a,x) => a + x.success, 0);
  return {label, checkpoint, contract:JSON.parse(fs.readFileSync(sidecar)), steps,
    training_wall_seconds:Number(wall[1]), aggregate_training_sps:steps / Number(wall[1]),
    episodes:total, successes, success_rate:successes / total, per_task:evaluation,
    browser:learnedBrowser, cpu_cuda_policy_parity:jsonl(path.join(root, `${label}-policy.log`))};
});
console.log(JSON.stringify({date:new Date().toISOString().slice(0,10), scope:'12 bounded CPU Bend generator presets of 125 registered task names; no full MiniWoB parity claim',
  evaluation:'Greedy learned policy; 100 episodes/task on separate high-bit reset seeds. Finite lexicons/templates recur. Longer-budget results reuse this development evaluation set.',
  browser_evaluation:'Pinned original MiniWoB generators; five tasks, 20 episodes/task, seeds 100000..100019, real CDP clicks, controlled 250-ms clock. Small transfer smoke test, not a complete benchmark.',
  comparison_limits:'One training seed per configuration; no confidence across training seeds. Concurrent CPU checks/shared machine load make wall times approximate. Five-million-step runs restart from scratch and are not continuations.',
  task_sampling:'The two-draw LCG/modulo picker partitions even/odd task IDs per environment. Four default environments cover all twelve tasks, but individual streams see six. Aggregate training success is episode-weighted, unlike balanced per-task evaluation.',
  evidence:fs.existsSync(path.join(root,'evidence.json'))?JSON.parse(fs.readFileSync(path.join(root,'evidence.json'))):undefined,
  experiments}, null, 2));
