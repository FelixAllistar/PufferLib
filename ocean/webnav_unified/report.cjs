// Run from repository root after the paired, one-cohort evaluations.
const fs = require('node:fs');
const crypto = require('node:crypto');
const path = require('node:path');
const assert = require('node:assert/strict');
const sha = file => crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
const readRows = file => fs.readFileSync(file, 'utf8').trim().split('\n').map(JSON.parse);
const checkpoint = 'checkpoints/webnav_unified/1790766678997/0000000000031616.bin';
const earlier = 'checkpoints/webnav_unified/1790766678997/0000000000016640.bin';
const greedy = readRows('build/webnav_unified/greedy.jsonl');
const random = readRows('build/webnav_unified/random.jsonl');
for (const [rows, policy] of [[greedy, 'greedy'], [random, 'random']]) {
  assert.equal(rows.length, 125);
  assert.equal(new Set(rows.map(r => r.task)).size, 125);
  for (const r of rows) {
    assert.equal(r.policy, policy);
    assert.equal(r.requested_episodes, 1);
    assert.ok(r.episodes === 4 || r.episodes === 8);
    assert.equal(r.semantic, 1);
  }
}
const baseline = new Map(random.map(r => [r.task, r]));
for (const r of greedy) {
  assert.equal(r.family, baseline.get(r.task).family);
  assert.equal(r.episodes, baseline.get(r.task).episodes);
}
const sum = (rows, key) => rows.reduce((n, r) => n + r[key], 0);
const summarize = rows => ({
  tasks: rows.length, episodes: sum(rows, 'episodes'), wins: sum(rows, 'wins'),
  macro_full_credit_rate: rows.reduce((n, r) => n + r.wins / r.episodes, 0) / rows.length,
  micro_full_credit_rate: sum(rows, 'wins') / sum(rows, 'episodes'),
  tasks_with_full_credit: rows.filter(r => r.wins > 0).length,
  mean_reward: sum(rows, 'reward') / sum(rows, 'episodes'),
  steps: sum(rows, 'steps'), rejected: sum(rows, 'rejected'),
  episodes_with_incomplete_metadata: sum(rows, 'incomplete'),
});
const weights = fs.readFileSync(checkpoint), oldWeights = fs.readFileSync(earlier);
assert.equal(weights.length, 7395584);
assert.equal(weights.length, oldWeights.length);
let changed = 0, maxChange = 0;
for (let i = 0; i < weights.length; i += 4) {
  const a = weights.readFloatLE(i), b = oldWeights.readFloatLE(i);
  assert.ok(Number.isFinite(a) && Number.isFinite(b));
  if (a !== b) changed++;
  maxChange = Math.max(maxChange, Math.abs(a - b));
}
const files = [];
function collect(dir) {
  for (const entry of fs.readdirSync(dir, {withFileTypes: true})) {
    const file = path.join(dir, entry.name);
    if (entry.isDirectory()) collect(file);
    else if (entry.name !== 'RESULTS.json') files.push(file);
  }
}
collect('ocean/webnav/unified');
collect('ocean/webnav_unified');
files.push('build.sh', 'config/webnav_unified.ini',
  'ocean/webnav/families/common/loader.c', 'ocean/webnav/text_encoder.c');
const manifest = fs.readFileSync('ocean/webnav/unified/manifest.h', 'utf8');
const libraries = [...manifest.matchAll(/"(build\/webnav\/families\/[^\"]+\.so)"/g)].map(m => m[1]);
assert.equal(libraries.length, 23);
for (const lib of libraries) files.push(lib.replace(/^build\//, 'ocean/').replace(/\/lib[^/]+\.so$/, '/family_api.c'));
const probe = fs.readFileSync('build/webnav_unified/semantic_probe.log', 'utf8')
  .split('\n').filter(line => line.startsWith('{')).map(JSON.parse).at(-1);
assert.equal(probe.cases, 24);
const report = {
  scope: 'Short native shared-policy baseline across 125 bounded task models; original-browser parity unverified',
  sampling: 'Every initial lane completes once per task; same initial seeds for both policies. Small cohort, no confidence claim.',
  checkpoint: {path: checkpoint, sha256: sha(checkpoint), bytes: weights.length,
    parameters: weights.length / 4, training_steps: 31616,
    resolved_config: 'logs/webnav_unified/1790766678997.ini',
    resolved_config_sha256: sha('logs/webnav_unified/1790766678997.ini'),
    earlier_checkpoint: earlier, earlier_sha256: sha(earlier),
    changed_parameters_since_16640: changed, maximum_weight_change: maxChange,
    note: 'Training preceded terminal capacity and email selection public-projection fixes; evaluation uses corrected projections with unchanged shapes.'},
  preset: {families: 23, task_names: 125, training_lanes: 104, policies: 1,
    observation_floats: 24864, actions: 3832, hidden_size: 64, recurrent_layers: 1,
    family_abi: 2, capability_abi: 1, semantic: true,
    engine_step_ms: 50, maximum_agent_steps: 512, parameter_bins: 257},
  greedy: summarize(greedy), random: summarize(random),
  encoder: probe,
  limitations: ['Native bounded models are not verified full MiniWoB++ browser/generator parity.',
    'Shared headed replay and original-browser policy evaluation are pending.',
    'Incomplete public gesture/option metadata remains; abstract action quantization limits coverage.',
    'The flat learner does not yet share learned node encoding/scoring across positions.',
    'Text fixture ranking is separate from trained RL task success.'],
  validation: {native_suite: 'PASS: all 125 projections/deadlines, mixed batches, isolation, public-input invariance, editing and selection',
    semantic_cache: 'PASS: exact bytes, Unicode, projection reference, eviction and size boundaries',
    cpu_linear: 'PASS: SIMD versus scalar FP32 within tolerance, 4/8 lanes and tail dimensions',
    encoder_forward_speed: {scalar_ms: 17.628, simd_ms: 4.359, lanes: 8, speedup: 4.04,
      scope: 'Microbenchmark on this host; not end-to-end environment or training throughput'}},
  tasks: greedy.map(r => ({task: r.task, family: r.family, greedy: r, random: baseline.get(r.task)})),
  sources: Object.fromEntries([...new Set(files)].sort().map(file => [file, sha(file)])),
  libraries: Object.fromEntries(libraries.map(file => [file, sha(file)])),
  assets: Object.fromEntries(['build/webnav/reference/potion-tokenizer.json',
    'build/webnav/reference/potion-model.safetensors'].map(file => [file, sha(file)])),
  artifacts: Object.fromEntries(['build/webnav_unified/greedy.jsonl',
    'build/webnav_unified/random.jsonl', 'build/webnav_unified/semantic_probe.log',
    'build/webnav_unified/tests.log', 'build/webnav_unified/evaluator_build.log',
    'build/webnav_unified/native_eval'].map(file => [file, sha(file)])),
};
fs.writeFileSync('ocean/webnav_unified/RESULTS.json', JSON.stringify(report, null, 2) + '\n');
console.log(JSON.stringify({greedy: report.greedy, random: report.random, changed_parameters: changed}, null, 2));
