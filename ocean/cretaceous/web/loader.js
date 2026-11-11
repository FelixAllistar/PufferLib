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
  window.cretLoad = { downloadMs: 0, startMs: 0, bytes: 0, stored: 0, texRestored: 0, texSaved: 0 };

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
  // Phones and tablets: touch first (nothing hovers), or a GPU without S3TC
  // (desktop browsers have it; mobile GPUs do not, so the game's textures stay
  // uncompressed there, four times the memory). Those get the small-texture
  // profile below; a desktop is unchanged.
  const touchFirst = !!(window.matchMedia && matchMedia("(pointer: coarse)").matches && !matchMedia("(hover: hover)").matches);
  const s3tc = !!(probe.getExtension("WEBGL_compressed_texture_s3tc") && probe.getExtension("WEBGL_compressed_texture_s3tc_srgb"));
  const mobile = params.has("mobile") || touchFirst || !s3tc;
  const lose = probe.getExtension("WEBGL_lose_context"); if (lose) lose.loseContext();

  // Touch devices: the first tap asks for fullscreen (the browser bars go,
  // and Android can then lock landscape). iPhones refuse element fullscreen;
  // their page simply stays as it is.
  if (touchFirst) {
    const full = () => {
      const el = document.documentElement, req = el.requestFullscreen || el.webkitRequestFullscreen;
      if (document.fullscreenElement || document.webkitFullscreenElement || !req) return;
      try {
        const p = req.call(el, { navigationUI: "hide" });
        if (p && p.then) p.then(() => { if (screen.orientation && screen.orientation.lock) screen.orientation.lock("landscape").catch(() => {}); }).catch(() => {});
      } catch (e) { /* not allowed here */ }
    };
    window.addEventListener("touchend", full, { passive: true });
  }

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

  // Browser storage (Cache Storage), so a second visit neither downloads nor
  // decodes again. Two caches: the downloaded files (data parts keyed by the
  // content hash web_pack.py writes, the wasm by the build stamp), and the
  // game's derived-data cache (render/cache.c: decoded, mipped and compressed
  // textures), which lives in the in-memory file system during a visit and is
  // copied out once the game is up (saveTex). The game checks every item's key
  // and hash, so a stale item is rebuilt, never used. ?fresh empties both
  // first. Without storage (private windows, blocked site data) the page works
  // as before, uncached.
  const TCDIR = "build/cretaceous/cache";
  const openStore = async (name) => {
    try {
      if (!window.caches) return null;
      if (params.has("fresh")) await caches.delete(name);
      return await caches.open(name);
    } catch (e) { return null; }
  };
  const dlStore = openStore("cretaceous-download-v1"), tcStore = openStore("cretaceous-texcache-v1");
  const used = new Set();
  // A file from storage when it holds this key (null: never stored), else
  // from the network, then stored.
  async function fetchKept(url, key) {
    const c = key ? await dlStore : null;
    const k = c ? new URL("kept/" + key, location.href).href : null;
    if (c) {
      used.add(k);
      try {
        const r = await c.match(k);
        if (r) {
          const b = new Uint8Array(await r.arrayBuffer());
          got += b.length; window.cretLoad.stored += b.length; progress();
          say("Loading the island from this browser's copy");
          return b;
        }
      } catch (e) { /* the network instead */ }
    }
    const b = await fetchBytes(url);
    if (c) c.put(k, new Response(b)).catch(() => {});
    return b;
  }
  // Drops stored downloads this visit did not use (older builds and packs).
  async function pruneDownloads() {
    const c = await dlStore; if (!c) return;
    try { for (const r of await c.keys()) if (!used.has(r.url)) await c.delete(r); } catch (e) { /* kept */ }
  }
  // Puts the stored texture cache back where render/cache.c looks, before main
  // runs. True when storage works (the game may then write its cache).
  const restored = new Map();   // name -> mtime after restoring: unchanged items are not saved again
  async function restoreTex(FS) {
    const c = await tcStore; if (!c) return false;
    try {
      FS.mkdirTree(TCDIR);
      for (const r of await c.keys()) {
        const res = await c.match(r); if (!res) continue;
        const name = r.url.substring(r.url.lastIndexOf("/") + 1), path = TCDIR + "/" + name;
        FS.writeFile(path, new Uint8Array(await res.arrayBuffer()), { canOwn: true });
        restored.set(name, +FS.stat(path).mtime);
        window.cretLoad.texRestored++;
      }
    } catch (e) { console.warn("cretaceous: texture cache not restored:", e); }
    return true;
  }
  // Copies new or rebuilt cache items into storage and frees the in-memory
  // copies (the game reads them only while loading). Terrain items are large
  // and rebuild in about a second: not kept.
  let saving = false;
  async function saveTex() {
    const c = await tcStore, FS = window.Module.FS; if (!c || saving) return;
    saving = true;
    try {
      let names = []; try { names = FS.readdir(TCDIR); } catch (e) { return; }
      for (const name of names) {
        if (!name.endsWith(".bin")) continue;   // ".", ".." and half-written ".tmp" files
        const path = TCDIR + "/" + name;
        if (!name.startsWith("terrain_") && restored.get(name) !== +FS.stat(path).mtime) {
          await c.put(new URL("tex/" + name, location.href).href, new Response(FS.readFile(path)));
          window.cretLoad.texSaved++;
        }
        FS.unlink(path);
      }
    } catch (e) { console.warn("cretaceous: texture cache not saved:", e); }
    finally { saving = false; }
  }
  // Saved once the game has drawn a few frames (loading is over), and again
  // whenever the page is hidden, for items built later.
  const loaded = () => (window.Module.cretFrames || 0) >= 30;
  function watchTex() {
    const t = setInterval(() => { if (loaded()) { clearInterval(t); saveTex(); } }, 1000);
    document.addEventListener("visibilitychange", () => { if (document.hidden && loaded()) saveTex(); });
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

  // The file system takes each file as a view into its bundle's buffer, so
  // one file keeps the whole buffer alive. The largest file of a bundle keeps
  // the buffer; the others get copies, so when the game deletes the island
  // after loading it (world.c) its 185 MB can go.
  function writeFiles(FS, bundle, buf) {
    for (const [path, off, size] of bundle.files) {
      const dir = path.substring(0, path.lastIndexOf("/"));
      if (dir) FS.mkdirTree(dir);
      FS.writeFile(path, size * 2 >= buf.length ? buf.subarray(off, off + size) : buf.slice(off, off + size), { canOwn: true });
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
      const hs = b.hashes || [];   // packs before the hashes: never stored
      for (let i = 0; i < b.parts.length; i++)
        parts.push(await fetchKept(b.parts[i] + ver, hs[i] ? b.parts[i] + "." + hs[i] : null));
      ready.push(inflate(parts, b.size).then((buf) => { if (b.filter) unfilter(buf, b.filter); return [b, buf]; }));
    }
    window.cretLoad.downloadMs = performance.now() - t0; window.cretLoad.bytes = got;
    say("Unpacking");
    return [man, await Promise.all(ready)];
  }
  const data = download();
  data.catch(fail);

  // The small-texture profile (phones, tablets, ?mobile): ground materials,
  // props and dinosaurs at 512 texels, the plant atlas at 256 and tree
  // impostors at 128, unless ?env= sets a knob itself (the low preset is
  // already the web default). tools/web_smoke.js --phone measures it.
  const mobileEnv = { CRET_MAT_SIZE: "512", CRET_PROP_TEX: "512", CRET_DINO_TEX: "512", CRET_VEG_ATLAS: "256", CRET_VEG_IMP: "128" };
  window.cretLoad.mobile = mobile;

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
        const z = await fetchKept("cretaceous.wasm.bin" + ver, ver ? "cretaceous.wasm.bin" + ver : null);
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
      M.addRunDependency("cret-data");
      data.then(async ([man, bundles]) => {
        for (const e of bundles) { writeFiles(M.FS, e[0], e[1]); e[1] = null; }   // the file system holds the bytes now
        // Knobs: the pack's, then the small-texture profile, then ?env=, which
        // wins (?env=CRET_SHADOWS=0,CRET_GRASS=0: the native debug knobs, README).
        for (const [k, v] of Object.entries(man.env || {})) M.ENV[k] = v;
        if (mobile) for (const [k, v] of Object.entries(mobileEnv)) M.ENV[k] = v;
        for (const kv of (params.get("env") || "").split(",")) { const i = kv.indexOf("="); if (i > 0) M.ENV[kv.slice(0, i)] = kv.slice(i + 1); }
        // The pack turns the derived-data cache off (CRET_CACHE=0: in memory
        // only, it would just cost RAM); with browser storage it pays, unless
        // ?env= set CRET_CACHE itself.
        say("Loading saved textures");
        if (await restoreTex(M.FS) && !/(^|,)CRET_CACHE=/.test(params.get("env") || "")) M.ENV.CRET_CACHE = "1";
        pruneDownloads();
        window.cretLoad.startMs = performance.now() - t0;
        say(""); document.body.classList.add("running");
        M.removeRunDependency("cret-data");
        watchTex();
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
