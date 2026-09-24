// Offline encoder comparison. This is a research harness, not a WebNav runtime.
// It uses the pinned Hugging Face Rust tokenizer Node binding for both static
// models and onnxruntime-node for the MiniLM reference. No Python is used.
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');

const ROOT = path.resolve(__dirname, '../..', '..');
const BUILD = path.join(ROOT, 'build/webnav/encoder-comparison');
const TOKENIZER_PACKAGE = path.join(ROOT, 'build/webnav/reference/tokenizers/node_modules/tokenizers');
const {Tokenizer} = require(TOKENIZER_PACKAGE);
const ort = require(path.join(BUILD, 'node_modules/onnxruntime-node'));

const now = () => process.hrtime.bigint();
const micros = (a, b) => Number(b - a) / 1000;
const percentile = (xs, p) => {
  const ys = xs.slice().sort((a, b) => a - b);
  return ys[Math.min(ys.length - 1, Math.floor(ys.length * p))];
};
const mean = xs => xs.reduce((a, b) => a + b, 0) / xs.length;
const round = x => Number(x.toFixed(6));
const sha256 = file => crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
const fileBytes = files => files.reduce((n, f) => n + fs.statSync(f).size, 0);

function readJson(file) {
  return JSON.parse(fs.readFileSync(file, 'utf8'));
}

function validateCases(cases) {
  for (const [i, c] of cases.entries()) {
    if (!c || typeof c.query !== 'string' || !Array.isArray(c.options) ||
        !Number.isInteger(c.expected) || c.expected < 0 || c.expected >= c.options.length ||
        c.options.some(x => typeof x !== 'string')) {
      throw new Error(`invalid case ${i}`);
    }
  }
}

class StaticEncoder {
  constructor(name, dir, tokenizerFile, modelFile) {
    this.name = name;
    this.dir = dir;
    this.tokenizerFile = tokenizerFile;
    this.modelFile = modelFile;
    this.kind = 'static';
  }

  load() {
    const started = now();
    this.tokenizer = Tokenizer.fromFile(this.tokenizerFile);
    this.tokenizer.disablePadding();
    this.tokenizer.disableTruncation();
    const tokenizerJson = readJson(this.tokenizerFile);
    this.unkId = tokenizerJson.model.vocab['[UNK]'];
    const model = fs.readFileSync(this.modelFile);
    const headerLength = Number(model.readBigUInt64LE(0));
    const headerStart = 8;
    const headerEnd = headerStart + headerLength;
    const header = JSON.parse(model.subarray(headerStart, headerEnd).toString('utf8'));
    const tensorName = Object.keys(header).find(k => k !== '__metadata__');
    if (!tensorName) throw new Error(`${this.name}: missing tensor`);
    const tensor = header[tensorName];
    if (tensor.dtype !== 'F32' || tensor.shape.length !== 2 || tensor.data_offsets.length !== 2) {
      throw new Error(`${this.name}: unsupported tensor metadata`);
    }
    const [rows, dim] = tensor.shape;
    const dataStart = headerEnd + Number(tensor.data_offsets[0]);
    const count = rows * dim;
    if (model.byteOffset + dataStart + count * 4 > model.byteOffset + model.byteLength) {
      throw new Error(`${this.name}: truncated weights`);
    }
    if ((model.byteOffset + dataStart) % 4 !== 0) throw new Error(`${this.name}: unaligned weights`);
    this.modelBytes = model;
    this.vectors = new Float32Array(model.buffer, model.byteOffset + dataStart, count);
    this.vocab = rows;
    this.dim = dim;
    this.loadSeconds = micros(started, now()) / 1e6;
    this.modelFiles = [this.tokenizerFile, path.join(this.dir, 'config.json'), this.modelFile].filter(fs.existsSync);
  }

  async encode(text) {
    const encoding = await this.tokenizer.encode(text, null, {addSpecialTokens: false});
    const ids = encoding.getIds().filter(id => id !== this.unkId).slice(0, 512);
    const vector = new Float32Array(this.dim);
    for (const id of ids) {
      if (id < 0 || id >= this.vocab) continue;
      const base = id * this.dim;
      for (let d = 0; d < this.dim; d++) vector[d] = Math.fround(vector[d] + this.vectors[base + d]);
    }
    let norm = 0;
    for (const x of vector) norm += x * x;
    if (norm > 0) {
      const scale = Math.fround(1 / Math.sqrt(norm));
      for (let d = 0; d < this.dim; d++) vector[d] = Math.fround(vector[d] * scale);
    }
    return vector;
  }
}

class MiniLMEncoder {
  constructor(dir) {
    this.name = 'MiniLM-L6-v2-ONNX-quint8-avx2';
    this.dir = dir;
    this.tokenizerFile = path.join(dir, 'tokenizer.json');
    this.modelFile = path.join(dir, 'onnx-model.onnx');
    this.kind = 'contextual';
  }

  async load() {
    const started = now();
    this.tokenizer = Tokenizer.fromFile(this.tokenizerFile);
    this.tokenizer.disablePadding();
    this.tokenizer.disableTruncation();
    const tokenizerJson = readJson(this.tokenizerFile);
    this.special = Object.fromEntries(tokenizerJson.added_tokens.map(x => [x.content, x.id]));
    this.session = await ort.InferenceSession.create(this.modelFile, {
      executionProviders: ['cpu'],
      intraOpNumThreads: 1,
      interOpNumThreads: 1,
    });
    if (this.session.inputNames.join(',') !== 'input_ids,attention_mask,token_type_ids') {
      throw new Error(`unexpected MiniLM inputs: ${this.session.inputNames.join(',')}`);
    }
    if (this.session.outputNames.length !== 1 || this.session.outputNames[0] !== 'last_hidden_state') {
      throw new Error(`unexpected MiniLM outputs: ${this.session.outputNames.join(',')}`);
    }
    this.dim = 384;
    this.loadSeconds = micros(started, now()) / 1e6;
    this.modelFiles = [path.join(this.dir, 'tokenizer.json'), path.join(this.dir, 'config.json'), this.modelFile];
  }

  async encode(text) {
    let encoding = await this.tokenizer.encode(text, null, {addSpecialTokens: true});
    let ids = encoding.getIds();
    if (ids.length > 128) ids = ids.slice(0, 127).concat(this.special['[SEP]']);
    const n = ids.length;
    const inputIds = new BigInt64Array(n);
    const mask = new BigInt64Array(n);
    const typeIds = new BigInt64Array(n);
    for (let i = 0; i < n; i++) {
      inputIds[i] = BigInt(ids[i]);
      mask[i] = 1n;
    }
    const outputs = await this.session.run({
      input_ids: new ort.Tensor('int64', inputIds, [1, n]),
      attention_mask: new ort.Tensor('int64', mask, [1, n]),
      token_type_ids: new ort.Tensor('int64', typeIds, [1, n]),
    });
    const data = outputs.last_hidden_state.data;
    const vector = new Float32Array(this.dim);
    for (let i = 0; i < n; i++) {
      const base = i * this.dim;
      for (let d = 0; d < this.dim; d++) vector[d] += data[base + d];
    }
    let norm = 0;
    for (let d = 0; d < this.dim; d++) {
      vector[d] /= n;
      norm += vector[d] * vector[d];
    }
    const scale = norm > 0 ? 1 / Math.sqrt(norm) : 0;
    for (let d = 0; d < this.dim; d++) vector[d] *= scale;
    return vector;
  }
}

function dot(a, b) {
  let sum = 0;
  const n = Math.min(a.length, b.length);
  for (let i = 0; i < n; i++) sum += a[i] * b[i];
  return sum;
}

function rankCase(c, vectors) {
  const query = vectors.get(c.query);
  const scores = c.options.map(x => dot(query, vectors.get(x)));
  const order = scores.map((score, index) => ({score, index})).sort((a, b) => b.score - a.score);
  const top = order[0];
  const second = order[1];
  const maximum = top.score;
  const nearMaximum = scores.map((score, index) => ({score, index})).filter(x => maximum - x.score <= 1e-6);
  return {
    correct: top.index === c.expected,
    strict_unique_correct: nearMaximum.length === 1 && nearMaximum[0].index === c.expected,
    expected_credit: nearMaximum.some(x => x.index === c.expected) ? 1 / nearMaximum.length : 0,
    tie: nearMaximum.length > 1,
    near_maximum_count: nearMaximum.length,
    scores: scores.map(round),
    top: top.index,
    margin: round(top.score - second.score),
  };
}

async function encodeAll(encoder, texts) {
  const vectors = new Map();
  for (const text of texts) vectors.set(text, await encoder.encode(text));
  return vectors;
}

async function measureFresh(encoder, texts, rounds) {
  const samples = [];
  for (let r = 0; r < rounds; r++) {
    for (const text of texts) {
      const start = now();
      await encoder.encode(text);
      samples.push(micros(start, now()));
    }
  }
  return {mean_us: round(mean(samples)), p50_us: round(percentile(samples, 0.50)), p95_us: round(percentile(samples, 0.95)), p99_us: round(percentile(samples, 0.99)), samples: samples.length};
}

function measureCached(encoder, cases, vectors) {
  const samples = [];
  const count = 10000;
  for (let i = 0; i < count; i++) {
    const c = cases[i % cases.length];
    const start = now();
    const query = vectors.get(c.query);
    let score = 0;
    for (const option of c.options) score += dot(query, vectors.get(option));
    if (score === Number.MIN_VALUE) throw new Error('unreachable sink');
    samples.push(micros(start, now()));
  }
  return {mean_us: round(mean(samples)), p50_us: round(percentile(samples, 0.50)), p95_us: round(percentile(samples, 0.95)), p99_us: round(percentile(samples, 0.99)), candidates_per_case: round(mean(cases.map(c => c.options.length))), samples: count};
}

async function compare(encoder, groups) {
  const allCases = groups.flatMap(g => g.cases);
  const texts = [...new Set(allCases.flatMap(c => [c.query, ...c.options]))];
  const vectors = await encodeAll(encoder, texts);
  const metrics = {};
  for (const group of groups) {
    const rows = group.cases.map(c => rankCase(c, vectors));
    const correct = rows.filter(x => x.correct).length;
    const strictUnique = rows.filter(x => x.strict_unique_correct).length;
    const expectedCredit = rows.reduce((sum, x) => sum + x.expected_credit, 0);
    metrics[group.name] = {
      cases: rows.length,
      correct,
      accuracy: round(correct / rows.length),
      strict_unique_correct: strictUnique,
      strict_unique_accuracy: round(strictUnique / rows.length),
      expected_credit: round(expectedCredit),
      expected_credit_accuracy: round(expectedCredit / rows.length),
      ties: rows.filter(x => x.tie).length,
      mean_margin: round(mean(rows.map(x => x.margin))),
    };
    if (group.categories) {
      metrics[group.name].categories = {};
      for (const category of [...new Set(group.cases.map(x => x.category))]) {
        const selected = group.cases.map((c, i) => ({c, row: rows[i]})).filter(x => x.c.category === category);
        const categoryStrictUnique = selected.filter(x => x.row.strict_unique_correct).length;
        const categoryCredit = selected.reduce((sum, x) => sum + x.row.expected_credit, 0);
        metrics[group.name].categories[category] = {
          cases: selected.length,
          correct: selected.filter(x => x.row.correct).length,
          accuracy: round(selected.filter(x => x.row.correct).length / selected.length),
          strict_unique_correct: categoryStrictUnique,
          strict_unique_accuracy: round(categoryStrictUnique / selected.length),
          expected_credit: round(categoryCredit),
          expected_credit_accuracy: round(categoryCredit / selected.length),
        };
      }
    }
  }
  const fresh = await measureFresh(encoder, texts, encoder.kind === 'contextual' ? 2 : 8);
  const cached = measureCached(encoder, allCases, vectors);
  return {
    name: encoder.name,
    kind: encoder.kind,
    dimensions: encoder.dim,
    vocabulary: encoder.vocab || null,
    load_seconds: round(encoder.loadSeconds),
    model_bytes: fileBytes(encoder.modelFiles),
    model_files: encoder.modelFiles.map(f => ({path: path.relative(ROOT, f), bytes: fs.statSync(f).size, sha256: sha256(f)})),
    rss_kib_process_cumulative_after_load_and_warmup: Math.round(process.memoryUsage().rss / 1024),
    semantic: metrics,
    latency: {batch1_text: fresh, cached_case_scoring: cached},
  };
}

async function main() {
  if (require(TOKENIZER_PACKAGE + '/package.json').version !== '0.23.2') throw new Error('requires tokenizers 0.23.2');
  const existing = readJson(path.join(ROOT, 'ocean/webnav/tests/text_cases.json')).map(x => ({...x, category: 'existing_24'}));
  const broader = readJson(path.join(ROOT, 'ocean/webnav/tools/encoder_compare_cases.json'));
  validateCases(existing);
  validateCases(broader);
  const groups = [
    {name: 'existing_24', cases: existing},
    {name: 'broader_frozen', cases: broader, categories: true},
    {name: 'combined', cases: existing.concat(broader)},
  ];
  const encoders = [
    new StaticEncoder('Potion-8M', path.join(ROOT, 'build/webnav/reference'), path.join(ROOT, 'build/webnav/reference/potion-tokenizer.json'), path.join(ROOT, 'build/webnav/reference/potion-model.safetensors')),
    new StaticEncoder('Potion-32M', path.join(BUILD, 'potion32'), path.join(BUILD, 'potion32/tokenizer.json'), path.join(BUILD, 'potion32/model.safetensors')),
    new MiniLMEncoder(path.join(BUILD, 'minilm')),
  ];
  const results = [];
  for (const encoder of encoders) {
    process.stderr.write(`loading ${encoder.name}\n`);
    if (encoder.kind === 'contextual') await encoder.load(); else encoder.load();
    results.push(await compare(encoder, groups));
    process.stderr.write(`finished ${encoder.name}\n`);
  }
  const output = {
    schema: 1,
    generated_at_utc: new Date().toISOString(),
    scope: 'offline semantic ranking and encoder microbenchmark; no policy training, no environment transitions',
    backend: {
      node: process.version,
      tokenizer: 'Hugging Face Rust Node binding tokenizers 0.23.2',
      static: 'Node lookup/pool implementation over safetensors F32; independent from production text_encoder.c',
      contextual: 'onnxruntime-node 1.22.0 CPU execution provider; one thread; MiniLM quantized AVX2 ONNX; mean pooling and L2 normalization',
    },
    datasets: {
      existing_24: 'ocean/webnav/tests/text_cases.json, unchanged',
      broader_frozen: 'ocean/webnav/tools/encoder_compare_cases.json, hand-authored before this run; no tuning on results',
      counts: {existing_24: existing.length, broader_frozen: broader.length, combined: existing.length + broader.length},
    },
    assets: {
      potion32_revision: '1e5a03f8eeb2c98b928fbbd846f22f816360919f',
      minilm_revision: '1110a243fdf4706b3f48f1d95db1a4f5529b4d41',
      checksums_file: path.relative(ROOT, path.join(BUILD, 'SHA256SUMS')),
    },
    results,
    limitations: [
      'Semantic cases are a small transparent diagnostic, not a representative WebNav accuracy benchmark.',
      'Static and MiniLM vectors use different model/tokenizer architectures; latency numbers are backend-specific Node research measurements.',
      'Fresh latency includes Node tokenizer and runtime call overhead; cached case scoring measures vector lookup plus dot products only.',
      'No downstream RL policy was trained or evaluated with these features.',
      'MiniLM is a contextual reference lane; it is not integrated into the Bend environment.',
    ],
  };
  fs.writeFileSync(path.join(BUILD, 'encoder-comparison.json'), JSON.stringify(output, null, 2) + '\n');
  console.log(JSON.stringify({dataset: output.datasets.counts, results: results.map(x => ({name: x.name, semantic: x.semantic, latency: x.latency, load_seconds: x.load_seconds, rss_kib_process_cumulative: x.rss_kib_process_cumulative_after_load_and_warmup}))}, null, 2));
}

main().catch(error => { console.error(error.stack || error); process.exit(1); });
