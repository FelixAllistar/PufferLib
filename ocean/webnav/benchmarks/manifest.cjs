const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const ROOT = process.env.WEBNAV_BENCH_ROOT || '/mnt/d/puffertank/webnav-bench';
const REVISION = '6473f72db5dcefc97b5725b59e734504edc28a21';
const DATASET_SHA256 = 'd65275660814663375028e9017e1f929e3c38321041b125795e2713b52243d30';
const DATASET = path.join(ROOT, 'webarena-verified/assets/dataset/webarena-verified.json');
// Selection stays in the evaluation harness. Neither references nor evaluator
// definitions are sent to the policy process or used as simulator training data.
function loadManifest() {
  const bytes = fs.readFileSync(DATASET);
  const checksum=crypto.createHash('sha256').update(bytes).digest('hex');
  if(checksum!==DATASET_SHA256)throw Error('Pinned WebArena-Verified dataset checksum mismatch');
  const tasks = JSON.parse(bytes);
  if (!Array.isArray(tasks) || tasks.length !== 812) throw Error('Unexpected pinned dataset');
  const counts = {};
  for (const task of tasks) for (const site of task.sites) counts[site] = (counts[site] || 0) + 1;
  const navigation = tasks.filter(task => task.sites.length === 1 && task.sites[0] === 'shopping_admin'
    && task.eval.some(e => e.expected?.task_type?.toLowerCase() === 'navigate'));
  return {benchmark: 'WebArena-Verified', revision: REVISION,
    dataset_sha256: checksum,
    full_tasks: tasks.length, site_memberships: counts,
    preset: 'shopping_admin navigation; fixed action budget; DOM; header login',
    split: 'evaluation only; never used to generate training goals',
    tasks: navigation.map(task => ({task_id: task.task_id, template_id: task.intent_template_id,
      intent: task.intent, start_urls: task.start_urls, sites: task.sites}))};
}
module.exports = {ROOT, REVISION, DATASET, loadManifest};
if (require.main === module) console.log(JSON.stringify(loadManifest(), null, 2));
