// Evaluation planning inventory. Never import this into a generator or policy.
// Site envelopes are conservative engineering proposals, not per-task coverage.
const fs = require('node:fs');
const crypto = require('node:crypto');
const {DATASET, REVISION} = require('./manifest.cjs');
const EXPECTED_SHA256 = 'd65275660814663375028e9017e1f929e3c38321041b125795e2713b52243d30';
const SITE_ENVELOPES = {
  shopping: ['commerce', 'account_permissions', 'browser_contexts'],
  shopping_admin: ['commerce', 'account_permissions', 'calendar'],
  reddit: ['discussion', 'account_permissions'],
  gitlab: ['repository', 'account_permissions', 'calendar'],
  wikipedia: ['document'],
  map: ['geography'],
};
const OUTCOME_ENVELOPES = {
  navigate: ['browser_navigation', 'public_page', 'structured_finish'],
  retrieve: ['browser_navigation', 'public_page', 'record_store', 'record_query', 'aggregate', 'structured_finish'],
  mutate: ['browser_navigation', 'public_page', 'record_store', 'record_query', 'draft_transaction', 'structured_finish'],
};
function inventory() {
  const bytes = fs.readFileSync(DATASET);
  const sha256 = crypto.createHash('sha256').update(bytes).digest('hex');
  if (sha256 !== EXPECTED_SHA256) throw Error('Pinned dataset checksum mismatch');
  const tasks = JSON.parse(bytes);
  if (!Array.isArray(tasks) || tasks.length !== 812) throw Error('Unexpected task inventory');
  const templates = new Map(), kinds = {}, memberships = {};
  const ids = new Set();
  let crossSite = 0;
  for (const task of tasks) {
    if (ids.has(task.task_id)) throw Error('Duplicate task ID');
    ids.add(task.task_id);
    const types = [...new Set(task.eval.map(e => e.expected?.task_type?.toLowerCase()).filter(Boolean))];
    if (types.length !== 1 || !OUTCOME_ENVELOPES[types[0]]) throw Error('Unexpected task outcome type');
    const kind = types[0];
    kinds[kind] = (kinds[kind] || 0) + 1;
    if (task.sites.length > 1) crossSite++;
    let template = templates.get(task.intent_template_id);
    if (!template) {
      template = {template_id: task.intent_template_id, task_ids: [], sites: new Set(), outcome_types: new Set(),
        proposed_capability_envelope: new Set(), status: 'not_ported', event_semantics_audited: false};
      templates.set(template.template_id, template);
    }
    template.task_ids.push(task.task_id);
    template.outcome_types.add(kind);
    for (const capability of OUTCOME_ENVELOPES[kind]) template.proposed_capability_envelope.add(capability);
    for (const site of task.sites) {
      if (!SITE_ENVELOPES[site]) throw Error('Unknown site: ' + site);
      memberships[site] = (memberships[site] || 0) + 1;
      template.sites.add(site);
      for (const capability of SITE_ENVELOPES[site]) template.proposed_capability_envelope.add(capability);
    }
    if (task.sites.length > 1) template.proposed_capability_envelope.add('browser_contexts');
  }
  return {
    benchmark: 'WebArena-Verified', revision: REVISION, dataset_sha256: sha256,
    purpose: 'Full evaluation planning inventory; not training input or coverage evidence.',
    planning_method: 'Union of proposed site and outcome capability envelopes; not a minimal or behavior-audited decomposition of each task.',
    total_tasks: tasks.length, total_templates: templates.size, outcome_counts: kinds,
    cross_site_tasks: crossSite, site_memberships: memberships,
    qualified_simulator_templates: 0,
    next_audit: 'Inspect original event/state dependencies for each template, then map them to primitive compositions and compare independently advanced original-site states.',
    templates: [...templates.values()].sort((a, b) => a.template_id - b.template_id).map(t => ({...t,
      task_ids: t.task_ids.sort((a, b) => a - b), sites: [...t.sites].sort(),
      outcome_types: [...t.outcome_types].sort(), proposed_capability_envelope: [...t.proposed_capability_envelope].sort()})),
  };
}
module.exports = {inventory};
if (require.main === module) process.stdout.write(JSON.stringify(inventory(), null, 2) + '\n');
