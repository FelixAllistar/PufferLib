// Cretaceous web loader. Downloads the packed data (data/manifest.json, written
// by tools/web_pack.py), inflates it in the browser and writes every file into
// Emscripten's in-memory file system at the path the native game opens, then
// lets main run. Static hosting only: the files are gzipped by the packer and
// inflated here with DecompressionStream, large bundles come in parts of at
// most 14 MiB, and nothing relies on the server's Content-Encoding (if a server
// does inflate a part on the way, the gzip magic is missing and the bytes are
// taken as they are). All paths are relative to the page.
"use strict";
(function () {
  const status = document.getElementById("status");
  const bar = document.getElementById("bar");
  const canvas = document.getElementById("canvas");
  const params = new URLSearchParams(location.search);
  const t0 = performance.now();
  // The build stamps the page with loader.js?v=N (build_web.sh); the same query
  // goes on every fetch below, so a cached copy from an older build is never used.
  const ver = document.currentScript ? new URL(document.currentScript.src).search : "";
  let got = 0, total = 1;
  const say = (s) => { if (status) status.textContent = s; };
  const progress = () => { if (bar) bar.style.width = (100 * Math.min(1, got / total)).toFixed(1) + "%"; };
  // A failure brings the loading card back with the reason: once the game runs
  // the card is hidden, and an error behind it would leave a black page.
  const fail = (e) => {
    say("Could not start: " + (e && e.message ? e.message : e) + ". The browser console (F12) has the details.");
    console.error("cretaceous:", e);
    document.body.classList.remove("running"); document.body.classList.add("failed");
  };
  window.addEventListener("error", (e) => fail(e.message || e.error));
  window.addEventListener("unhandledrejection", (e) => fail(e.reason));
  window.cretLoad = { downloadMs: 0, startMs: 0, bytes: 0 };

  // No WebGL2, no game: say so before downloading anything. Chrome and Edge no
  // longer fall back to software rendering, so a browser with hardware
  // acceleration turned off, or with a blocked graphics driver, has no WebGL.
  const probe = document.createElement("canvas").getContext("webgl2");
  if (!probe) {
    say("This browser has 3D graphics (WebGL2) turned off, so the game cannot start. In Edge or Chrome, turn on " +
        "Settings > System > \"Use graphics acceleration when available\" and restart the browser; edge://gpu or " +
        "chrome://gpu should then show WebGL2 as hardware accelerated. In Firefox, see about:support under Graphics.");
    document.body.classList.add("failed");
    return;
  }
  const lose = probe.getExtension("WEBGL_lose_context"); if (lose) lose.loseContext();

  // Emscripten 6 views the wasm heap through a resizable ArrayBuffer when the
  // browser offers one (Memory.toResizableBuffer), but Chrome and Edge 157
  // refuse resizable buffers in TextDecoder.decode, so the first path string
  // (a mkdir in main) threw and the page stayed black. Without the method the
  // runtime takes its older route (a fixed buffer, views rebuilt on growth).
  if (window.WebAssembly && WebAssembly.Memory && WebAssembly.Memory.prototype.toResizableBuffer)
    WebAssembly.Memory.prototype.toResizableBuffer = function () { throw new Error("off"); };

  // Bytes of one URL, counted into the progress bar as they arrive.
  async function fetchBytes(url) {
    const r = await fetch(url);
    if (!r.ok) throw new Error(url + ": HTTP " + r.status);
    const reader = r.body.getReader(), chunks = [];
    let n = 0;
    for (;;) {
      const { done, value } = await reader.read();
      if (done) break;
      chunks.push(value); n += value.length; got += value.length; progress();
    }
    const out = new Uint8Array(n); let o = 0;
    for (const c of chunks) { out.set(c, o); o += c.length; }
    return out;
  }

  // The parts of a gzip stream, inflated into one buffer of the known size.
  async function inflate(parts, size) {
    const first = parts[0];
    if (first.length < 2 || first[0] !== 0x1f || first[1] !== 0x8b) {   // already inflated by the server
      const out = new Uint8Array(size); let o = 0;
      for (const p of parts) { out.set(p, o); o += p.length; }
      return out;
    }
    const ds = new DecompressionStream("gzip");
    const writer = ds.writable.getWriter();
    const writing = (async () => { for (const p of parts) await writer.write(p); await writer.close(); })();
    const out = new Uint8Array(size); let o = 0;
    const reader = ds.readable.getReader();
    for (;;) {
      const { done, value } = await reader.read();
      if (done) break;
      if (o + value.length > size) throw new Error("bundle larger than its manifest says");
      out.set(value, o); o += value.length;
    }
    await writing;
    if (o !== size) throw new Error("bundle truncated (" + o + " of " + size + " bytes)");
    return out;
  }

  // Inverse of web_pack.py's world filter: byte planes back into samples, then
  // the row deltas summed (32-bit integers for floats, bytes for the rest).
  function unfilter(buf, segs) {
    for (const s of segs) {
      const n = s.n, N = n * n, k = s.k, src = buf.slice(s.o, s.o + N * k), dst = buf.subarray(s.o, s.o + N * k);
      if (s.m === "f32") {
        for (let i = 0; i < N; i++) {
          const j = 4 * i;
          dst[j] = src[i]; dst[j + 1] = src[N + i]; dst[j + 2] = src[2 * N + i]; dst[j + 3] = src[3 * N + i];
        }
        const a = new Uint32Array(buf.buffer, buf.byteOffset + s.o, N);   // the header is 32 bytes: aligned
        for (let z = 1; z < n; z++) a[z * n] = (a[z * n] + a[(z - 1) * n]) >>> 0;
        for (let z = 0; z < n; z++) { const r = z * n; for (let x = 1; x < n; x++) a[r + x] = (a[r + x] + a[r + x - 1]) >>> 0; }
      } else {
        for (let p = 0; p < k; p++) {
          const pl = src.subarray(p * N, (p + 1) * N);
          for (let z = 0; z < n; z++) { const r = z * n; for (let x = 1; x < n; x++) pl[r + x] = (pl[r + x] + pl[r + x - 1]) & 255; }
          for (let i = 0; i < N; i++) dst[i * k + p] = pl[i];
        }
      }
    }
  }

  function writeFiles(FS, bundle, buf) {
    for (const [path, off, size] of bundle.files) {
      const dir = path.substring(0, path.lastIndexOf("/"));
      if (dir) FS.mkdirTree(dir);
      FS.writeFile(path, buf.subarray(off, off + size), { canOwn: true });
    }
  }

  // Downloads start with the page, alongside the wasm; the files go into the
  // file system once the runtime exists (preRun).
  async function download() {
    const man = await (await fetch("data/manifest.json" + ver)).json();
    total = man.bundles.reduce((a, b) => a + b.csize, 0);
    say("Downloading " + (total / 1048576).toFixed(0) + " MB");
    // Bundles download one after another (parts in order) and inflate as soon
    // as their bytes are in, overlapping the next download.
    const ready = [];
    for (const b of man.bundles) {
      const parts = [];
      for (const p of b.parts) parts.push(await fetchBytes(p + ver));
      ready.push(inflate(parts, b.size).then((buf) => { if (b.filter) unfilter(buf, b.filter); return [b, buf]; }));
    }
    window.cretLoad.downloadMs = performance.now() - t0; window.cretLoad.bytes = got;
    say("Unpacking");
    return [man, await Promise.all(ready)];
  }
  const data = download();
  data.catch(fail);

  // Command line: the canvas size, plus ?play (straight into solo play),
  // ?species=raptor|stego|trex|pachy|sauropelta|galli, ?fight (the scenario
  // menu) and the test switches ?quit=N&shot=PATH. ?profile prints the load stages.
  const w = Math.max(640, Math.floor(window.innerWidth)), h = Math.max(360, Math.floor(window.innerHeight));
  const args = ["--size", String(params.get("w") || w), String(params.get("h") || h)];
  if (params.get("quit")) args.push("--quit-after", params.get("quit"));
  if (params.get("species")) args.push("--species", params.get("species"));
  if (params.has("fight")) args.push("play", "scenario");   // last: a word after it would be taken as a scenario name
  else if (params.has("play")) args.push("play");

  window.Module = {
    canvas,
    arguments: args,
    print: (s) => console.log(s),
    printErr: (s) => console.warn(s),
    // The wasm is gzipped too; inflate it before instantiating.
    instantiateWasm(imports, done) {
      (async () => {
        const z = await fetchBytes("cretaceous.wasm.bin" + ver);
        const bytes = await inflate([z], z[0] === 0x1f && z[1] === 0x8b ? new DataView(z.buffer, z.length - 4, 4).getUint32(0, true) : z.length);
        const { instance, module } = await WebAssembly.instantiate(bytes, imports);
        // miniaudio's Web Audio callback reads Module.HEAPF32, which Emscripten 6
        // no longer puts on Module unless it is exported (build_web.sh does that
        // now). For builds without it, a view of the wasm memory stands in; an
        // exported HEAPF32, assigned by the runtime, replaces it.
        const mem = Object.values(instance.exports).find((e) => e instanceof WebAssembly.Memory);
        const M = window.Module;
        if (mem && !("HEAPF32" in M))
          Object.defineProperty(M, "HEAPF32", { configurable: true, get: () => new Float32Array(mem.buffer),
            set: (v) => Object.defineProperty(M, "HEAPF32", { value: v, writable: true, configurable: true }) });
        done(instance, module);
      })().catch(fail);
      return {};
    },
    preRun: [function () {
      const M = window.Module;
      M.ENV.HOME = "/home/web_user";
      if (params.has("profile")) M.ENV.CRET_PROFILE_LOAD = "1";
      if (params.get("preset")) M.ENV.CRET_PRESET = params.get("preset");
      // ?env=CRET_SHADOWS=0,CRET_GRASS=0: the native debug knobs (README, Debug knobs).
      for (const kv of (params.get("env") || "").split(",")) { const i = kv.indexOf("="); if (i > 0) M.ENV[kv.slice(0, i)] = kv.slice(i + 1); }
      M.addRunDependency("cret-data");
      data.then(([man, bundles]) => {
        for (const [b, buf] of bundles) writeFiles(M.FS, b, buf);
        for (const [k, v] of Object.entries(man.env || {})) M.ENV[k] = v;
        window.cretLoad.startMs = performance.now() - t0;
        say(""); document.body.classList.add("running");
        M.removeRunDependency("cret-data");
      }).catch(fail);
    }],
    onAbort: (e) => fail(e),
    // The game ended: Quit (code 0), or main gave up, e.g. without a GL context.
    onExit: (code) => {
      if (code) { fail("the game stopped with exit code " + code); return; }
      say("The game has quit. Reload the page to play again.");
      document.body.classList.remove("running");
    },
  };
  // raylib 5.5's ES2 batch renderer enables a vertex normal attribute that its
  // default shader does not have (location -1) on every 2D flush. Desktop GL
  // ignores that call with a silent GL_INVALID_VALUE; WebGL also ignores it but
  // logs a warning each time. A negative location is skipped here, the same
  // result without the noise.
  for (const C of [window.WebGL2RenderingContext, window.WebGLRenderingContext]) {
    if (!C) continue;
    for (const f of ["enableVertexAttribArray", "disableVertexAttribArray", "vertexAttribPointer"]) {
      const orig = C.prototype[f];
      C.prototype[f] = function (index, ...rest) { if (index < 0) return; return orig.call(this, index, ...rest); };
    }
  }
  canvas.addEventListener("webglcontextlost", (e) => { e.preventDefault(); say("The WebGL context was lost; reload the page."); }, false);
  const s = document.createElement("script");
  s.src = "cretaceous.js" + ver; s.onerror = () => fail("cretaceous.js did not load");
  document.body.appendChild(s);
})();
