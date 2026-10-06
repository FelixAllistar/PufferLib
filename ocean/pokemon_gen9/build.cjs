#!/usr/bin/env node
'use strict';
const fs = require('fs'), path = require('path'), cp = require('child_process');
const root = path.resolve(__dirname, '../..');
process.chdir(root);
const guardDir = path.join(root, 'ocean/webnav/families');
if (process.argv.includes('--worker-stop-only')) {
  require('./guard.cjs').stopWorkers();
  return;
}
if (process.env.WEBNAV_BUILD_CONFINED !== '1') {
  require('./guard.cjs').launch(process.argv.slice(2));
  return;
}
require(path.join(guardDir, 'resource_guard.cjs')).verify();
const out = 'build/pokemon_gen9/native'; fs.mkdirSync(out, {recursive:true});
const bend = path.resolve('build/pokemon_gen9/toolchain/bend/bin/bend');
const compilerEnv = {...process.env, BEND_NO_TELEMETRY:'1',
  BEND_HOME:path.resolve('build/pokemon_gen9/toolchain/bend'),
  PATH:path.resolve('build/pokemon_gen9/toolchain/lean-4.34.0-linux/bin') + ':' + process.env.PATH};
const selection = process.argv.find(arg => arg.startsWith('--tests='));
const selectedTests = selection ? new Set(selection.slice(8).split(',')) : null;
if (selectedTests) {
  if (!process.argv.includes('--test')) throw Error('--tests requires --test');
  for (const name of selectedTests) {
    if (!/^[a-z_]+$/.test(name) || !fs.existsSync('ocean/pokemon_gen9/test_' + name + '.py'))
      throw Error('Unknown test selection: ' + name);
  }
}
function run(command, args) {
  if (selectedTests && command === 'python3' && /^ocean\/pokemon_gen9\/test_/.test(args[0]) &&
      !selectedTests.has(path.basename(args[0], '.py').slice(5))) return;
  console.log([command, ...args].join(' '));
  const r = cp.spawnSync(command, args, {stdio:'inherit', env:compilerEnv});
  if (r.error) throw r.error;
  if (r.status !== 0) throw Error(command + ' failed (' + (r.signal||r.status) + ')');
}
if (process.argv.includes('--bootstrap-only')) {
  run('python3', ['ocean/pokemon_gen9/bootstrap.py']);
  process.exit(0);
}
if (process.argv.includes('--catalog-only')) {
  run(process.execPath, ['ocean/pokemon_gen9/export_catalog.cjs']);
  run('python3', ['ocean/pokemon_gen9/emit_item_rules.py']);
  process.exit(0);
}
if (process.argv.includes('--js-probe-only')) {
  run(process.execPath, ['ocean/pokemon_gen9/js_probe.cjs']);
  process.exit(0);
}
if (process.argv.includes('--js-pack-only')) {
  run(process.execPath, ['ocean/pokemon_gen9/js_pack.cjs']);
  process.exit(0);
}
if (process.argv.includes('--js-profile-only')) {
  run(process.execPath, ['ocean/pokemon_gen9/js_profile.cjs']);
  process.exit(0);
}
if (process.argv.includes('--worker-deps-only')) {
  fs.mkdirSync('build/pokemon_gen9/worker-deps', {recursive:true});
  fs.copyFileSync('ocean/pokemon_gen9/worker-package.json','build/pokemon_gen9/worker-deps/package.json');
  fs.copyFileSync('ocean/pokemon_gen9/worker-package-lock.json','build/pokemon_gen9/worker-deps/package-lock.json');
  run('npm', ['ci','--prefix','build/pokemon_gen9/worker-deps',
    '--ignore-scripts','--no-audit','--no-fund']);
  process.exit(0);
}
if (process.argv.includes('--batch-deps-only')) {
  fs.mkdirSync('build/pokemon_gen9/batch-deps', {recursive:true});
  fs.copyFileSync('ocean/pokemon_gen9/batch-package.json','build/pokemon_gen9/batch-deps/package.json');
  const lock='ocean/pokemon_gen9/batch-package-lock.json';
  fs.copyFileSync(lock,'build/pokemon_gen9/batch-deps/package-lock.json');
  run('npm', ['ci','--prefix','build/pokemon_gen9/batch-deps','--ignore-scripts','--no-audit','--no-fund']);
  process.exit(0);
}
if (process.argv.includes('--batch-compile-only')) {
  run(process.execPath, ['ocean/pokemon_gen9/batch_compile.cjs']);
  run('clang-19', ['-O3','-std=c11','-fPIC','-shared','-ffp-contract=off',
    'build/pokemon_gen9/batch/kernels.c','-lm','-o','build/pokemon_gen9/batch/kernels.so']);
  process.exit(0);
}
if (process.argv.includes('--continuation-build-only')) {
  run(process.execPath, ['ocean/pokemon_gen9/batch_compile.cjs']);
  run('clang-19', ['-O3','-std=c11','-fPIC','-shared','-ffp-contract=off',
    'build/pokemon_gen9/batch/kernels.c','ocean/pokemon_gen9/batch_addon.c','-lm',
    '-o','build/pokemon_gen9/batch/kernels.node']);
  run(process.execPath, ['ocean/pokemon_gen9/continuation_compile.cjs']);
  process.exit(0);
}
if (process.argv.includes('--continuation-test-only')) {
  run(process.execPath, ['ocean/pokemon_gen9/test_continuation_semantics.cjs']);
  run(process.execPath, ['ocean/pokemon_gen9/test_continuation_mechanics.cjs']);
  run(process.execPath, ['ocean/pokemon_gen9/test_continuation.cjs']);
  process.exit(0);
}
if (process.argv.includes('--continuation-mechanics-only')) {
  run(process.execPath, ['ocean/pokemon_gen9/test_continuation_mechanics.cjs']);
  process.exit(0);
}
if (process.argv.includes('--compact-build-only')) {
  run(process.execPath, ['ocean/pokemon_gen9/compact_compile.cjs']);
  process.exit(0);
}
if (process.argv.includes('--continuation-transport-only')) {
  fs.mkdirSync('build/pokemon_gen9/continuation', {recursive:true});
  run('clang-19', ['-O2','-std=c11','-D_POSIX_C_SOURCE=200809L',
    '-DPG9_LAYOUT_HEADER="compact_layout.h"','-DPG9_MAX_GAMES=512',
    '-DPG9_WORKER_SCRIPT="ocean/pokemon_gen9/continuation_worker.cjs"',
    '-Isrc','-Iraylib-5.5_linux_amd64/include','ocean/pokemon_gen9/test_worker_transport.c','-lm',
    '-o','build/pokemon_gen9/continuation/transport_test']);
  run('build/pokemon_gen9/continuation/transport_test', []);
  process.exit(0);
}
if (process.argv.includes('--continuation-encoder-only')) {
  run('/usr/local/cuda/bin/nvcc', ['-O2','--threads','1','-arch=native','-std=c++17',
    '-I.','-Isrc','-Ivendor','-Iraylib-5.5_linux_amd64/include','-Xcompiler=-fopenmp,-Wno-narrowing',
    '--diag-suppress','2361','ocean/pokemon_gen9_batch/test_encoder.cu',
    'raylib-5.5_linux_amd64/lib/libraylib.a','-lGL','-lpthread','-ldl','-lrt','-lm',
    '-lgomp','-lcudart','-lcublas','-lcusolver','-lcurand','-lnccl','-lnvidia-ml',
    '-o','build/pokemon_gen9/continuation/test_encoder']);
  run('build/pokemon_gen9/continuation/test_encoder', []);
  process.exit(0);
}
if (process.argv.includes('--continuation-trainer-build-only')) {
  if (!compilerEnv.CUDA_HOME && fs.existsSync('/usr/local/cuda/bin/nvcc'))compilerEnv.CUDA_HOME='/usr/local/cuda';
  run('bash', ['build.sh','pokemon_gen9_batch','build/pokemon_gen9/puffer_batch','--float']);
  process.exit(0);
}
if (process.argv.includes('--continuation-train-only')) {
  run('build/pokemon_gen9/puffer_batch',['train',
    ...process.argv.slice(2).filter(arg=>arg.startsWith('--')&&arg.includes('='))]);
  process.exit(0);
}
if (process.argv.includes('--continuation-smoke-only') || process.argv.includes('--continuation-train-bench-only')) {
  const bench=process.argv.includes('--continuation-train-bench-only'),mode=bench?'train-bench':'smoke';
  const dir='build/pokemon_gen9/continuation/'+mode;
  fs.mkdirSync(dir,{recursive:true});
  const args=['train', ...[
    'vec.total_agents='+(bench?256:8),'vec.num_buffers=1','vec.num_threads='+(bench?4:1),
    'env.games_per_worker='+(bench?32:4),'policy.hidden_size=64','policy.num_layers=1',
    'train.horizon='+(bench?4:16),'train.minibatch_size=128',
    'train.total_timesteps='+(bench?32768:1024),'base.async=0',
    'base.checkpoint_interval='+(bench?8:1),'base.eval_episodes='+(bench?0:4),
    'base.checkpoint_dir='+dir+'/checkpoints','base.log_dir='+dir+'/logs'].map(x=>'--'+x)];
  const log=fs.openSync(dir+'/trainer.log','w');
  const result=cp.spawnSync('build/pokemon_gen9/puffer_batch',args,{env:compilerEnv,stdio:['ignore',log,log]});
  fs.closeSync(log);
  if(result.error||result.status!==0)throw result.error||Error('Batched trainer failed; inspect '+dir+'/trainer.log');
  run('python3',['ocean/pokemon_gen9/verify_continuation_training.py',mode]);
  process.exit(0);
}
if (process.argv.includes('--continuation-benchmark-only')) {
  run(process.execPath,['ocean/pokemon_gen9/benchmark_continuation.cjs']);
  process.exit(0);
}
if (process.argv.includes('--continuation-scale-build-only')) {
  run('clang-19',['-O2','-std=c11','-D_XOPEN_SOURCE=700','-D_POSIX_C_SOURCE=200809L',
    '-DPG9_LAYOUT_HEADER="compact_layout.h"','-DPG9_MAX_GAMES=512',
    '-DPG9_WORKER_SCRIPT="ocean/pokemon_gen9/continuation_worker.cjs"',
    '-Isrc','-Iraylib-5.5_linux_amd64/include','ocean/pokemon_gen9/benchmark_workers.c','-lm','-pthread',
    '-o','build/pokemon_gen9/continuation/scale_test']);
  process.exit(0);
}
if (process.argv.includes('--continuation-scale-only')) {
  run(process.execPath,['ocean/pokemon_gen9/benchmark_continuation_workers.cjs']);
  process.exit(0);
}
if (process.argv.includes('--batch-test-only')) {
  run(process.execPath, ['ocean/pokemon_gen9/batch_compile.cjs']);
  run('clang-19', ['-O3','-std=c11','-fPIC','-shared','-ffp-contract=off',
    'build/pokemon_gen9/batch/kernels.c','-lm','-o','build/pokemon_gen9/batch/kernels.so']);
  run('clang-19', ['-O2','-std=c11','ocean/pokemon_gen9/batch_replay.c',
    '-Lbuild/pokemon_gen9/batch','-Wl,-rpath,$ORIGIN','-l:kernels.so','-lm',
    '-o','build/pokemon_gen9/batch/replay']);
  run(process.execPath, ['ocean/pokemon_gen9/batch_trace.cjs']);
  process.exit(0);
}
if (process.argv.includes('--worker-test-only')) {
  run(process.execPath, ['ocean/pokemon_gen9/test_worker.cjs']);
  process.exit(0);
}
if (process.argv.includes('--worker-profile-only')) {
  run(process.execPath, ['ocean/pokemon_gen9/worker_profile.cjs']);
  process.exit(0);
}
if (process.argv.includes('--worker-scale-build-only')) {
  fs.mkdirSync('build/pokemon_gen9/worker', {recursive:true});
  run('clang-19', ['-O2','-std=c11','-D_XOPEN_SOURCE=700','-D_POSIX_C_SOURCE=200809L','-Isrc',
    '-Iraylib-5.5_linux_amd64/include','ocean/pokemon_gen9/benchmark_workers.c','-lm','-pthread',
    '-o','build/pokemon_gen9/worker/scale_test']);
  process.exit(0);
}
if (process.argv.includes('--worker-scale-only')) {
  if(!fs.existsSync('build/pokemon_gen9/worker/scale_test'))
    throw Error('Run --worker-scale-build-only first');
  run(process.execPath, ['ocean/pokemon_gen9/benchmark_workers.cjs']);
  process.exit(0);
}
if (process.argv.includes('--worker-build-only')) {
  if (!compilerEnv.CUDA_HOME && fs.existsSync('/usr/local/cuda/bin/nvcc'))
    compilerEnv.CUDA_HOME='/usr/local/cuda';
  run('bash', ['build.sh','pokemon_gen9','build/pokemon_gen9/puffer_showdown','--float']);
  process.exit(0);
}
if (process.argv.includes('--worker-transport-only')) {
  fs.mkdirSync('build/pokemon_gen9/worker', {recursive:true});
  run('clang-19', ['-O2','-std=c11','-D_POSIX_C_SOURCE=200809L','-Isrc',
    '-Iraylib-5.5_linux_amd64/include','ocean/pokemon_gen9/test_worker_transport.c','-lm',
    '-o','build/pokemon_gen9/worker/transport_test']);
  run('build/pokemon_gen9/worker/transport_test', []);
  process.exit(0);
}
if (process.argv.includes('--worker-smoke-only')) {
  const args=['train', ...[
    'vec.total_agents=8','vec.num_buffers=1','vec.num_threads=1','env.games_per_worker=4',
    'policy.hidden_size=64','policy.num_layers=1','train.horizon=16','train.minibatch_size=128',
    'train.total_timesteps=1024','base.async=0','base.checkpoint_interval=1',
    'base.eval_episodes=4',
    'base.checkpoint_dir=build/pokemon_gen9/worker/checkpoints',
    'base.log_dir=build/pokemon_gen9/worker/logs'].map(option=>'--'+option)];
  const result=cp.spawnSync('build/pokemon_gen9/puffer_showdown',args,
    {env:compilerEnv,encoding:'utf8',maxBuffer:16*1024*1024});
  fs.writeFileSync('build/pokemon_gen9/worker/smoke.log',(result.stdout||'')+(result.stderr||''));
  if(result.error)throw result.error;
  if(result.status!==0)throw Error('Source-worker training failed; inspect build/pokemon_gen9/worker/smoke.log');
  run('python3', ['ocean/pokemon_gen9/verify_worker_smoke.py']);
  process.exit(0);
}
if (process.argv.includes('--worker-smoke-verify-only')) {
  run('python3', ['ocean/pokemon_gen9/verify_worker_smoke.py']);
  process.exit(0);
}
if (process.argv.includes('--worker-batch-only')) {
  const args=['train', ...[
    'vec.total_agents=128','vec.num_buffers=1','vec.num_threads=4','env.games_per_worker=16',
    'policy.hidden_size=64','policy.num_layers=1','train.horizon=4','train.minibatch_size=128',
    'train.total_timesteps=16384','base.async=0','base.checkpoint_interval=8',
    'base.eval_episodes=0',
    'base.checkpoint_dir=build/pokemon_gen9/worker/batch-checkpoints',
    'base.log_dir=build/pokemon_gen9/worker/batch-logs'].map(option=>'--'+option)];
  const log=fs.openSync('build/pokemon_gen9/worker/batch.log','w');
  const result=cp.spawnSync('build/pokemon_gen9/puffer_showdown',args,
    {env:compilerEnv,stdio:['ignore',log,log]});
  fs.closeSync(log);
  if(result.error)throw result.error;
  if(result.status!==0)throw Error('Batch training failed; inspect build/pokemon_gen9/worker/batch.log');
  run('python3', ['ocean/pokemon_gen9/verify_worker_batch.py']);
  process.exit(0);
}
if (process.argv.includes('--worker-train-only')) {
  run('build/pokemon_gen9/puffer_showdown',['train',
    ...process.argv.slice(2).filter(arg=>arg.startsWith('--')&&arg.includes('='))]);
  process.exit(0);
}
if (process.argv.includes('--bench-only')) {
  if (!fs.existsSync(out + '/libpokemon_gen9.so'))
    throw Error('Build and validate the native library first');
  run('python3', ['ocean/pokemon_gen9/benchmark.py']);
  process.exit(0);
}
const existing = process.argv.includes('--test-existing');
if (existing) {
  if (!process.argv.includes('--test')) throw Error('--test-existing requires --test');
  const library = out + '/libpokemon_gen9.so';
  if (!fs.existsSync(library)) throw Error('Build the native library before --test-existing');
  const built = fs.statSync(library).mtimeMs;
  const inputs = fs.readdirSync('ocean/pokemon_gen9', {recursive:true})
    .filter(name => /\.(bend|c|h)$/.test(name)).map(name => 'ocean/pokemon_gen9/' + name);
  inputs.push(out + '/generated.c');
  for (const file of inputs) if (!fs.existsSync(file) || fs.statSync(file).mtimeMs > built)
    throw Error('Native source changed since compilation; run a normal build: ' + file);
}
if (!existing) {
  const version = cp.spawnSync(bend, ['--help'], {encoding:'utf8',env:compilerEnv});
  if (version.status !== 0 || !version.stdout.includes('Bend 2.0.35'))
    throw Error('Expected guarded stock Bend 2.0.35: ' + JSON.stringify({status:version.status,stdout:version.stdout,stderr:version.stderr,error:version.error?.message}));
  run(bend, ['ocean/pokemon_gen9/PROOF.bend', '--verdict']);
  run(bend, ['ocean/pokemon_gen9/Main.bend', '-o', out + '/generated.c']);
  if (process.argv.includes('--emit-only')) process.exit(0);
  run(process.env.CC || 'clang-19', ['-O3','-std=c11','-fPIC','-shared','-Iocean/pokemon_gen9',
    '-DPG9_GENERATED="' + path.resolve(out, 'generated.c') + '"',
    'ocean/pokemon_gen9/bridge.c','-lpthread','-lm','-o',out + '/libpokemon_gen9.so']);
}
if (process.argv.includes('--test'))
  run('python3', ['ocean/pokemon_gen9/test_native.py']);
if (process.argv.includes('--test'))
  run('python3', ['ocean/pokemon_gen9/test_generator.py']);
if (process.argv.includes('--test'))
  run('python3', ['ocean/pokemon_gen9/test_culls.py']);
if (process.argv.includes('--test'))
  run('python3', ['ocean/pokemon_gen9/test_moves.py']);
if (process.argv.includes('--test'))
  run('python3', ['ocean/pokemon_gen9/test_sets.py']);
if (process.argv.includes('--test'))
  run('python3', ['ocean/pokemon_gen9/test_teams.py']);
if (process.argv.includes('--test'))
  run('python3', ['ocean/pokemon_gen9/test_init.py']);
if (process.argv.includes('--test'))
  run('python3', ['ocean/pokemon_gen9/test_type_init.py']);
if (process.argv.includes('--test'))
  run('python3', ['ocean/pokemon_gen9/test_queue.py']);
if (process.argv.includes('--test'))
  run('python3', ['ocean/pokemon_gen9/test_damage.py']);
if (process.argv.includes('--test'))
  run('python3', ['ocean/pokemon_gen9/test_events.py']);
if (process.argv.includes('--test'))
  run('python3', ['ocean/pokemon_gen9/test_pokemon.py']);
if (process.argv.includes('--test'))
  run('python3', ['ocean/pokemon_gen9/test_registry.py']);
if (process.argv.includes('--test'))
  run('python3', ['ocean/pokemon_gen9/test_sodium.py']);
if (process.argv.includes('--test'))
  run('python3', ['ocean/pokemon_gen9/test_hp.py']);
if (process.argv.includes('--test'))
  run('python3', ['ocean/pokemon_gen9/test_suppression.py']);
if (process.argv.includes('--test'))
  run('python3', ['ocean/pokemon_gen9/test_move_view.py']);
if (process.argv.includes('--test'))
  run('python3', ['ocean/pokemon_gen9/test_request.py']);
if (process.argv.includes('--test'))
  run('python3', ['ocean/pokemon_gen9/test_event_gate.py']);
if (process.argv.includes('--test'))
  run('python3', ['ocean/pokemon_gen9/test_event_value.py']);
if (process.argv.includes('--test'))
  run('python3', ['ocean/pokemon_gen9/test_event_context.py']);
if (process.argv.includes('--test'))
  run('python3', ['ocean/pokemon_gen9/test_callbacks.py']);
if (process.argv.includes('--test'))
  run('python3', ['ocean/pokemon_gen9/test_run_event.py']);
if (process.argv.includes('--test'))
  run('python3', ['ocean/pokemon_gen9/test_vector_event.py']);
if (process.argv.includes('--test'))
  run('python3', ['ocean/pokemon_gen9/test_listener_priority.py']);
if (process.argv.includes('--test'))
  run('python3', ['ocean/pokemon_gen9/test_effect_store.py']);
if (process.argv.includes('--test'))
  run('python3', ['ocean/pokemon_gen9/test_value_array.py']);
if (process.argv.includes('--test'))
  run('python3', ['ocean/pokemon_gen9/test_text.py']);
if (process.argv.includes('--test'))
  run('python3', ['ocean/pokemon_gen9/test_type_change.py']);
if (process.argv.includes('--test'))
  run('python3', ['ocean/pokemon_gen9/test_species_change.py']);
if (process.argv.includes('--test'))
  run('python3', ['ocean/pokemon_gen9/test_move_slots.py']);
if (process.argv.includes('--test'))
  run('python3', ['ocean/pokemon_gen9/test_boost_state.py']);
if (process.argv.includes('--test'))
  run('python3', ['ocean/pokemon_gen9/test_types.py']);
if (process.argv.includes('--test'))
  run('python3', ['ocean/pokemon_gen9/test_grounded.py']);
if (process.argv.includes('--test'))
  run('python3', ['ocean/pokemon_gen9/test_effect_scopes.py']);
if (process.argv.includes('--test'))
  run('python3', ['ocean/pokemon_gen9/test_identity.py']);
if (process.argv.includes('--test'))
  run('python3', ['ocean/pokemon_gen9/test_collection.py']);
if (process.argv.includes('--test'))
  run('python3', ['ocean/pokemon_gen9/test_stats.py']);
if (process.argv.includes('--test'))
  run('python3', ['ocean/pokemon_gen9/test_weather.py']);
if (process.argv.includes('--test'))
  run('python3', ['ocean/pokemon_gen9/test_weather_defense.py']);
if (process.argv.includes('--test'))
  run('python3', ['ocean/pokemon_gen9/test_terrain.py']);
if (process.argv.includes('--bench'))
  run('python3', ['ocean/pokemon_gen9/benchmark.py']);
