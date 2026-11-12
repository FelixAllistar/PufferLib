// Headless smoke test of the web build in Chromium with software WebGL2
// (SwiftShader), no GPU needed. Serves the build directory itself, loads the
// title screen, starts solo play from the menu (Down, Enter on "Play"), and
// checks that frames render without page exceptions, failed shader compiles or
// WebGL errors. Prints the load times, the frame time and the download size,
// and saves screenshots. --fight then reloads the page and starts "Play a
// fight" from the title (Fights and scenarios, Start the fight).
// --phone instead emulates a phone held sideways (390x844 CSS pixels at DPR 3,
// touch, no S3TC as on mobile GPUs): taps Play on the title, then holds the
// stick (pushed to sprint) while a second finger turns the camera, lifts the
// camera finger first, checks that the animal moved, turns the phone upright
// and back (the canvas follows), and taps the menu button and Resume.
// Both modes report memory at the title and in play: the wasm heap, the
// in-memory file system, the JS heap, an estimate of GPU memory (texture and
// renderbuffer uploads, counted by wrapping WebGL) and the browser's processes.
// --perf measures the frame time instead (no other checks): the title, then
// solo play as a raptor hatchling in a fern-dense spot and in a forest, at
// 1280x720 (or the phone's size with --phone). For each it prints the game's
// per-pass CPU times (render/gprof.h, through CRET_GPU_PROFILE=1 and
// CRET_PROFILE_EVERY), the WebGL calls per frame by kind with their time and
// the bytes uploaded (counted by wrapping WebGL), and the frame rate.
// --cpuprofile (with --perf) also records a DevTools CPU profile of play in
// the fern spot and prints the functions with the most self time; build with
// PROFILE=1 tools/build_web.sh so the wasm functions have names. --only=ferns
// (or title, forest) measures one spot.
//   node ocean/cretaceous/tools/web_smoke.js [--fight | --phone | --perf [--phone] [--cpuprofile] [--only=SPOT]] [WEB_DIR] [SHOT_DIR]
//   (defaults build/cretaceous/web and build/cretaceous/web-shots)
// Needs Playwright (require("playwright")) with its Chromium. Exit status 1 on failure.
"use strict";
const http = require("http"), fs = require("fs"), path = require("path");
const { chromium } = require("playwright");

const FIGHT = process.argv.includes("--fight"), PHONE = process.argv.includes("--phone"), PERF = process.argv.includes("--perf");
const CPUPROF = process.argv.includes("--cpuprofile");
const ONLY = (process.argv.find((a) => a.startsWith("--only=")) || "").slice(7);   // --perf: only the spots whose label contains this
const { execSync } = require("child_process");
const POS = process.argv.slice(2).filter((a) => !a.startsWith("--"));
const DIR = path.resolve(POS[0] || "build/cretaceous/web");
const SHOTS = path.resolve(POS[1] || "build/cretaceous/web-shots");
const W = PHONE ? 844 : PERF ? 1280 : 960, H = PHONE ? 390 : PERF ? 720 : 540;   // the phone: CSS pixels, landscape
const TYPES = { ".html": "text/html", ".js": "text/javascript", ".json": "application/json", ".bin": "application/octet-stream" };

// Test only: counts the bytes of every texture level and renderbuffer the page
// allocates (window.cretGpu.total, .peak). Formats by their texel size; block
// formats by the size of the data given.
const GPU_COUNT = () => {
  const P = WebGL2RenderingContext.prototype;
  const B = { 0x8058: 4, 0x8C43: 4, 0x881A: 8, 0x8814: 16, 0x822E: 4, 0x8230: 8, 0x822D: 2, 0x822F: 4, 0x8229: 1, 0x822B: 2, 0x8051: 3,
              0x8C3A: 4, 0x81A6: 4, 0x8CAC: 4, 0x88F0: 4, 0x81A5: 2, 0x881B: 6, 0x8815: 12, 0x8D76: 8, 0x8234: 2, 0x8232: 1, 0x8236: 4,
              0x823A: 4, 0x8059: 4, 0x8D62: 2, 0x8056: 2, 0x8057: 2, 0x8F97: 4, 0x1908: 4, 0x1907: 3, 0x1906: 1, 0x1909: 1, 0x190A: 2 };
  const st = { unit: 0, bound: new Map(), tex: new Map(), rb: new Map(), rbBound: null, total: 0, peak: 0, unknown: new Set() };
  const bpp = (f, type) => { if (f === 0x1908 && type === 0x1406) return 16; if (f === 0x1908 && type === 0x140B) return 8; if (B[f]) return B[f]; st.unknown.add(f); return 4; };
  const shape = new Map();   // texture or renderbuffer -> "format WxHxD" (for the largest ones)
  const add = (m, obj, k, bytes, what) => {
    if (!obj) return; let e = m.get(obj); if (!e) m.set(obj, e = new Map());
    if (what && !shape.has(obj)) shape.set(obj, what);
    st.total += bytes - (e.get(k) || 0); e.set(k, bytes); st.peak = Math.max(st.peak, st.total);
  };
  const drop = (m, obj) => { const e = m.get(obj); if (e) { for (const b of e.values()) st.total -= b; m.delete(obj); } shape.delete(obj); };
  const hex = (f) => "0x" + f.toString(16);
  // The largest live allocations: [bytes, "format WxHxD"].
  st.top = (n) => [...st.tex, ...st.rb].map(([o, e]) => [[...e.values()].reduce((a, b) => a + b, 0), shape.get(o) || "?"]).sort((a, b) => b[0] - a[0]).slice(0, n);
  const cur = (t) => st.bound.get(st.unit + ":" + (t >= 0x8515 && t <= 0x851A ? 0x8513 : t));
  const wrap = (name, f) => { const o = P[name]; P[name] = function (...a) { try { f(...a); } catch (e) { /* counting only */ } return o.apply(this, a); }; };
  const lv = (s, l) => Math.max(1, s >> l);
  wrap("activeTexture", (u) => { st.unit = u - 0x84C0; });
  wrap("bindTexture", (t, tex) => st.bound.set(st.unit + ":" + t, tex));
  wrap("deleteTexture", (tex) => drop(st.tex, tex));
  wrap("texStorage2D", (t, n, f, w, h) => { for (let l = 0; l < n; l++) add(st.tex, cur(t), l, bpp(f) * lv(w, l) * lv(h, l) * (t === 0x8513 ? 6 : 1), `${hex(f)} ${w}x${h}`); });
  wrap("texStorage3D", (t, n, f, w, h, d) => { for (let l = 0; l < n; l++) add(st.tex, cur(t), l, bpp(f) * lv(w, l) * lv(h, l) * (t === 0x806F ? lv(d, l) : d), `${hex(f)} ${w}x${h}x${d}`); });
  wrap("texImage2D", (t, l, f, w, h, b, fmt, type) => { if (typeof w === "number") add(st.tex, cur(t), l * 8 + (t >= 0x8515 && t <= 0x851A ? t - 0x8515 : 0), bpp(f, type) * w * h, l ? null : `${hex(f)} ${w}x${h}`); });
  wrap("texImage3D", (t, l, f, w, h, d, b, fmt, type) => add(st.tex, cur(t), l, bpp(f, type) * w * h * d, l ? null : `${hex(f)} ${w}x${h}x${d}`));
  const csize = (x, over) => typeof x === "number" ? x : x ? (over || x.byteLength) : 0;
  wrap("compressedTexImage2D", (t, l, f, w, h, b, x, o, over) => add(st.tex, cur(t), l, csize(x, over), l ? null : `${hex(f)} ${w}x${h}`));
  wrap("compressedTexImage3D", (t, l, f, w, h, d, b, x, o, over) => add(st.tex, cur(t), l, csize(x, over), l ? null : `${hex(f)} ${w}x${h}x${d}`));
  wrap("bindRenderbuffer", (t, rb) => { st.rbBound = rb; });
  wrap("deleteRenderbuffer", (rb) => drop(st.rb, rb));
  wrap("renderbufferStorage", (t, f, w, h) => add(st.rb, st.rbBound, 0, bpp(f) * w * h, `rb ${hex(f)} ${w}x${h}`));
  wrap("renderbufferStorageMultisample", (t, s, f, w, h) => add(st.rb, st.rbBound, 0, bpp(f) * w * h * Math.max(1, s), `rb ${hex(f)} ${w}x${h}x${s}s`));
  window.cretGpu = st;
};
// Test only (--perf): counts every WebGL2 call per frame by name, the time
// spent inside them (performance.now around each call: the browser's
// validation and command encoding, not the GPU's work) and the bytes handed
// to buffer and texture uploads (window.cretGl; reset() starts a window).
const GL_PERF = () => {
  const P = WebGL2RenderingContext.prototype;
  const st = { calls: 0, ms: 0, draws: 0, bytes: 0, uploads: 0, by: new Map() };
  st.reset = () => { st.calls = st.ms = st.draws = st.bytes = st.uploads = 0; st.by.clear(); };
  const DRAW = /^draw(Arrays|Elements|RangeElements)/;
  const UP = { bufferData: 1, bufferSubData: 2, texSubImage2D: -1, texSubImage3D: -1, texImage2D: -1, texImage3D: -1, compressedTexSubImage2D: -1, compressedTexSubImage3D: -1 };
  const size = (x) => x && typeof x === "object" && "byteLength" in x ? x.byteLength : 0;
  for (const name of Object.getOwnPropertyNames(P)) {
    const d = Object.getOwnPropertyDescriptor(P, name);
    if (!d || typeof d.value !== "function" || name === "constructor") continue;
    const o = d.value, draw = DRAW.test(name), up = UP[name];
    P[name] = function (...a) {
      const t = performance.now(); const r = o.apply(this, a); const dt = performance.now() - t;
      st.calls++; st.ms += dt;
      let e = st.by.get(name); if (!e) st.by.set(name, e = { n: 0, ms: 0, bytes: 0 }); e.n++; e.ms += dt;
      if (draw) st.draws++;
      if (up) {
        // Buffers: Emscripten calls (target, heap, usage, offset, length) and (target, dst, heap, offset, length);
        // bufferData(target, size, usage) only allocates. Textures: texels times the texel size.
        let b = 0;
        if (up === 1) b = typeof a[1] === "number" ? 0 : a.length > 4 && a[4] ? a[4] * (a[1].BYTES_PER_ELEMENT || 1) : size(a[1]) - (a[3] || 0) * (a[1] && a[1].BYTES_PER_ELEMENT || 1);
        else if (up === 2) b = a.length > 4 && a[4] ? a[4] * (a[2].BYTES_PER_ELEMENT || 1) : size(a[2]) - (a[3] || 0) * (a[2] && a[2].BYTES_PER_ELEMENT || 1);
        else if (name.startsWith("compressed")) b = a[a.length - 1] && typeof a[a.length - 1] === "number" ? a[a.length - 1] : size(a[a.length - 1]);
        else {
          // Texel size from the format and type; the pixels may be null (allocation only).
          const sub = name.startsWith("texSub"), d3 = name.endsWith("3D"), f = sub ? (d3 ? 8 : 6) : (d3 ? 7 : 6);
          const dims = sub ? (d3 ? a[5] * a[6] * a[7] : a[4] * a[5]) : (d3 ? a[3] * a[4] * a[5] : a[3] * a[4]);
          const ch = { 0x1908: 4, 0x8D99: 4, 0x1907: 3, 0x8D98: 3, 0x8227: 2, 0x8228: 2, 0x1903: 1, 0x8D94: 1, 0x1902: 1 }[a[f]] || 4;
          const ty = { 0x1401: 1, 0x1400: 1, 0x1403: 2, 0x1402: 2, 0x140B: 2, 0x1405: 4, 0x1404: 4, 0x1406: 4 }[a[f + 1]] || 1;
          b = a[f + 2] ? dims * ch * ty : 0;
        }
        st.bytes += b; st.uploads++; e.bytes += b;
      }
      return r;
    };
  }
  window.cretGl = st;
};
// Test only: a phone's GPU has no S3TC (desktop GPUs do, SwiftShader too).
const NO_S3TC = () => {
  const P = WebGL2RenderingContext.prototype, ge = P.getExtension, gs = P.getSupportedExtensions;
  P.getExtension = function (n) { return /s3tc/i.test(n) ? null : ge.call(this, n); };
  P.getSupportedExtensions = function () { return (gs.call(this) || []).filter((n) => !/s3tc/i.test(n)); };
};

function serve() {   // plain static files, no Content-Encoding and no COOP/COEP, like the Artifact host
  const srv = http.createServer((req, res) => {
    const p = path.join(DIR, decodeURIComponent(new URL(req.url, "http://x").pathname));
    if (!p.startsWith(DIR) || !fs.existsSync(p) || fs.statSync(p).isDirectory()) {
      const idx = path.join(p, "index.html");
      if (fs.existsSync(idx)) { res.writeHead(200, { "Content-Type": "text/html" }); return fs.createReadStream(idx).pipe(res); }
      res.writeHead(404); return res.end();
    }
    res.writeHead(200, { "Content-Type": TYPES[path.extname(p)] || "application/octet-stream" });
    fs.createReadStream(p).pipe(res);
  });
  return new Promise((ok) => srv.listen(0, "127.0.0.1", () => ok(srv)));
}

(async () => {
  fs.mkdirSync(SHOTS, { recursive: true });
  const srv = await serve(), port = srv.address().port;
  const browser = await chromium.launch({
    args: ["--use-gl=angle", "--use-angle=swiftshader", "--enable-unsafe-swiftshader", "--ignore-gpu-blocklist"],
  });
  const log = [], problems = [];
  let bytes = 0, page = null;
  // A fresh page per phase, so the second game does not share a renderer with the first.
  const open = async () => {
    const p = await browser.newPage(PHONE ? { viewport: { width: W, height: H }, deviceScaleFactor: 3, isMobile: true, hasTouch: true } : { viewport: { width: W, height: H } });
    await p.addInitScript(PERF ? GL_PERF : GPU_COUNT);
    if (PHONE) await p.addInitScript(NO_S3TC);
    // Test only: keep the drawing buffer so the canvas can be read between frames
    // (a page screenshot waits for a compositor frame, which takes minutes on
    // SwiftShader while the game holds the main thread).
    await p.addInitScript(() => {
      const get = HTMLCanvasElement.prototype.getContext;
      HTMLCanvasElement.prototype.getContext = function (type, attrs) {
        if (type === "webgl2" || type === "webgl") attrs = Object.assign({}, attrs, { preserveDrawingBuffer: true });
        return get.call(this, type, attrs);
      };
    });
    p.on("response", async (r) => { try { bytes += (await r.body()).length; } catch (e) { /* redirects */ } });
    p.on("console", (m) => {
      const t = m.text(); log.push(t);
      if (/^INFO:/.test(t)) return;   // raylib's start-up report names the WebGL version
      if (/WebGL|GL_INVALID|GL ERROR|gfx: |framebuffer incomplete|shader load failed|Uncaught|Aborted|RuntimeError/i.test(t)) problems.push(t);
    });
    p.on("pageerror", (e) => problems.push("page error: " + e.message));
    p.on("crash", () => problems.push("page crashed"));
    return p;
  };
  page = await open();
  const snap = async (name) => {
    const url = await page.evaluate(() => document.getElementById("canvas").toDataURL("image/png"));
    fs.writeFileSync(path.join(SHOTS, name), Buffer.from(url.split(",")[1], "base64"));
  };
  const waitLog = async (re, ms) => {
    const t0 = Date.now();
    while (Date.now() - t0 < ms) {
      if (log.some((l) => re.test(l))) return true;
      if (problems.some((p) => /page error|crashed|Aborted|RuntimeError/.test(p))) return false;
      await page.waitForTimeout(250);
    }
    return false;
  };
  // raylib samples keys once per frame and a SwiftShader frame takes seconds,
  // so each key is held for a few frames.
  const frames = () => page.evaluate(() => (window.Module && Module.cretFrames) || 0);   // counted by the game (cretaceous.c)
  const waitFrames = async (n, ms = 10 * 60 * 1000) => {
    const f0 = await frames(), t = Date.now();
    while ((await frames()) < f0 + n) { if (Date.now() - t > ms) throw new Error("no new frame for " + ms / 1000 + " s"); await page.waitForTimeout(200); }
  };
  const tap = async (key) => { await page.keyboard.down(key); await waitFrames(2); await page.keyboard.up(key); await waitFrames(2); };
  // Touch through the DevTools protocol: fingers held across frames (a
  // SwiftShader frame takes seconds). Each event lists the fingers down after
  // it, and touchEnd lifts them all, so it cannot lift one of two fingers.
  let cdp = null;
  const touch = async (type, points) => { cdp = cdp || await page.context().newCDPSession(page); await cdp.send("Input.dispatchTouchEvent", { type, touchPoints: points }); };
  // Touch events as a browser sends them, dispatched on the canvas: the
  // fingers down after the event and the ones it changed ({id, x, y} in CSS pixels).
  const finger = (type, down, changed) => page.evaluate(([type, down, changed]) => {
    const c = Module.canvas, r = c.getBoundingClientRect();
    const mk = (p) => new Touch({ identifier: p.id, target: c, clientX: r.left + p.x, clientY: r.top + p.y, pageX: r.left + p.x, pageY: r.top + p.y,
                                  screenX: r.left + p.x, screenY: r.top + p.y, radiusX: 4, radiusY: 4, force: 1 });
    c.dispatchEvent(new TouchEvent(type, { touches: down.map(mk), targetTouches: down.map(mk), changedTouches: changed.map(mk), bubbles: true, cancelable: true }));
  }, [type, down, changed]);
  const player = () => page.evaluate(() => window.Module && Module.cretPlayer);
  // The phone run: title -> Play by a quick tap, then stick and camera at once.
  const phone_play = async () => {
    // The title's rows on a 390-pixel screen (menu.c, short windows): "Play"
    // is the second row, about 130 pixels down; its word starts at x 54.
    await touch("touchStart", [{ x: 64, y: 131, id: 1 }]); await touch("touchEnd", []);   // shorter than a frame: the game's start queue keeps it
    const played = await waitLog(/first play frame/, 5 * 60 * 1000);
    console.log(`solo play by tap: ${played ? "started" : "NOT started"} at ${((Date.now() - t0) / 1000).toFixed(1)} s`);
    if (!played) return false;
    await waitFrames(2);
    const p0 = await player();
    await snap("phone_play.png");
    await memory("play");
    // Left thumb on the stick, pushed up past its rim (sprint); right finger
    // drags the camera. The stick has the higher identifier: when the camera
    // finger lifts, raylib 5.5's list keeps the lifted finger and loses the
    // stick (touch.c checks it against the page's own record).
    const S0 = { id: 7, x: 96, y: H - 96 }, S = { id: 7, x: 106, y: H - 176 }, C = (i) => ({ id: 3, x: 560 + 40 * i, y: 160 });
    await finger("touchstart", [S0], [S0]); await waitFrames(1);
    await finger("touchmove", [S], [S]); await waitFrames(1);
    await finger("touchstart", [S, C(0)], [C(0)]); await waitFrames(1);
    for (let i = 1; i <= 3; i++) { await finger("touchmove", [S, C(i)], [C(i)]); await waitFrames(1); }
    await snap("phone_move.png");
    await finger("touchend", [S], [C(3)]); await waitFrames(1);   // the camera finger lifts first
    const p1 = await player(); await waitFrames(6); const p2 = await player();
    await finger("touchend", [], [S]); await waitFrames(2);
    const d = (a, b) => a && b ? Math.hypot(b.x - a.x, b.z - a.z) : 0;
    console.log(`stick: moved ${d(p0, p1).toFixed(1)} m with both fingers down, ${d(p1, p2).toFixed(1)} m more after the camera finger lifted` +
                ` (speed ${p1.speed.toFixed(1)} then ${p2.speed.toFixed(1)} m/s, forward input ${p1.forward.toFixed(2)} then ${p2.forward.toFixed(2)}); touch controls ${p2 && p2.touch ? "showing" : "HIDDEN"}`);
    let ok = d(p0, p1) > 0.5 && d(p1, p2) > 0.3 && p2 && p2.touch;
    // The first tap asked for fullscreen (web/loader.js); the window has to leave it to be resized.
    const full = await page.evaluate(() => !!document.fullscreenElement);
    console.log(`fullscreen after the first tap: ${full ? "yes" : "no"}`);
    if (full) { await page.evaluate(() => document.exitFullscreen()); await page.waitForTimeout(500); }
    // Upright and back: the canvas follows the window (raylib's resize callback).
    await page.setViewportSize({ width: H, height: W }); await waitFrames(2);
    const up = await page.evaluate(() => [Module.canvas.width, Module.canvas.height]);
    await snap("phone_upright.png");
    await page.setViewportSize({ width: W, height: H }); await waitFrames(2);
    const back = await page.evaluate(() => [Module.canvas.width, Module.canvas.height]);
    const kept = await player();
    console.log(`rotation: canvas ${up.join("x")} upright, ${back.join("x")} sideways again; touch controls ${kept && kept.touch ? "still showing" : "HIDDEN"}`);
    ok = ok && up[0] === H && up[1] === W && back[0] === W && back[1] === H && kept && kept.touch;
    // Menu button (top right, left of the minimap) opens the pause menu; Resume (its first row) closes it.
    const mx = W - (28 + 180) * 0.75 - 14 - 22, my = 24 * 0.75 + 22;   // hud.c map_box at the phone's HUD scale (0.75)
    await touch("touchStart", [{ x: mx, y: my, id: 3 }]); await waitFrames(2); await touch("touchEnd", []); await waitFrames(2);
    await snap("phone_menu.png");
    await touch("touchStart", [{ x: W * 0.5 - 100, y: H * 0.32 + 8, id: 4 }]); await waitFrames(2); await touch("touchEnd", []); await waitFrames(2);
    await snap("phone_resumed.png");
    return ok;
  };
  // Memory now: the page's view plus the resident size of the browser's
  // processes (the GPU process holds SwiftShader's textures).
  const MB = (b) => (b / 1048576).toFixed(0) + " MB";
  const memory = async (label) => {
    await page.evaluate(() => { Module.cretMallocWanted = true; }); await waitFrames(1);   // the game reads malloc's figures on request (cretaceous.c)
    const m = await page.evaluate(() => {
      const M = window.Module || {}; let fs = 0; const dirs = {};
      try {
        const walk = (p) => { for (const n of M.FS.readdir(p)) {
          if (n === "." || n === ".." || /^\/(proc|dev|sys)$/.test(p + n)) continue;
          const q = (p === "/" ? "/" : p + "/") + n, s = M.FS.stat(q);
          if (M.FS.isDir(s.mode)) walk(q); else { fs += s.size; dirs[p] = (dirs[p] || 0) + s.size; } } };
        walk("/");
      } catch (e) { /* no file system yet */ }
      const g = window.cretGpu || {};
      const ma = M.cretMalloc || {};
      return { heap: M.HEAPF32 ? M.HEAPF32.buffer.byteLength : 0, used: ma.used || 0, arena: ma.arena || 0, fs, js: performance.memory ? performance.memory.usedJSHeapSize : 0,
               gpu: g.total || 0, gpuPeak: g.peak || 0, unknown: g.unknown ? [...g.unknown].map((f) => "0x" + f.toString(16)) : [],
               top: g.top ? g.top(8) : [], dirs: Object.entries(dirs).sort((a, b) => b[1] - a[1]).slice(0, 5) };
    });
    let procs = "";
    try {
      const ps = execSync("ps -eo rss=,args= | grep -E 'chrom(e|ium).*--type=(renderer|gpu-process)' | grep -v grep", { encoding: "utf8" });
      const ren = [], gpu = [];
      for (const line of ps.trim().split("\n")) (/--type=gpu-process/.test(line) ? gpu : ren).push(MB(parseInt(line) * 1024));
      procs = `; processes: renderer ${ren.join(" + ")}, GPU ${gpu.join(" + ")}`;
    } catch (e) { /* no ps */ }
    console.log(`memory at ${label}: wasm heap ${MB(m.heap)} (malloc in use ${MB(m.used)}, arena ${MB(m.arena)}), files ${MB(m.fs)}, JS heap ${MB(m.js)}, GPU estimate ${MB(m.gpu)} (peak ${MB(m.gpuPeak)})${procs}` +
                (m.unknown.length ? ` [texel size guessed for ${m.unknown.join(" ")}]` : ""));
    console.log("  largest GPU allocations: " + m.top.map(([b, w]) => `${w} ${MB(b)}`).join(", "));
    console.log("  files by directory: " + m.dirs.map(([d, b]) => `${d} ${MB(b)}`).join(", "));
    return m;
  };
  // --perf: the title and two play spots, one page each. SwiftShader would
  // otherwise spread over every core and slow the page's own thread at random,
  // so the GPU process is pinned to half the cores and the renderers to the
  // rest (Linux taskset, when there are 4 cores or more).
  const pin = () => {
    try {
      const n = require("os").cpus().length; if (n < 4) return;
      const half = Math.floor(n / 2), gpu = `0-${half - 1}`, ren = `${half}-${n - 1}`;
      const ps = execSync("ps -eo pid=,args= | grep -E 'chrom(e|ium).*--type=(renderer|gpu-process)' | grep -v grep", { encoding: "utf8" });
      for (const line of ps.trim().split("\n")) {
        const pid = parseInt(line); execSync(`taskset -a -cp ${/--type=gpu-process/.test(line) ? gpu : ren} ${pid}`, { stdio: "ignore" });
      }
    } catch (e) { /* no taskset or ps: unpinned */ }
  };
  const perfRun = async () => {
    const EVERY = 8;   // frames per profile window (a SwiftShader frame takes seconds)
    const spots = [["title", null], ["play, ferns", "tp 2200 1528"], ["play, forest", "tp 1624 2568"]].filter(([label]) => label.includes(ONLY));
    const rows = [];
    for (const [label, tp] of spots) {
      if (page) await page.close();
      log.length = 0; page = await open();
      const env = ["CRET_GPU_PROFILE=1", "CRET_PROFILE_EVERY=" + EVERY].concat(tp ? ["CRET_CONSOLE=" + tp] : []).join(",");
      const q = (PHONE ? "" : `w=${W}&h=${H}&`) + "env=" + encodeURIComponent(env) + (tp ? "&play&species=raptor" : "");
      const t1 = Date.now();
      await page.goto(`http://127.0.0.1:${port}/?profile&${q}`);
      pin();
      const up = await waitLog(tp ? /first play frame/ : /first title frame/, 20 * 60 * 1000);
      console.log(`${label}: ${up ? "first frame" : "NOT reached"} at ${((Date.now() - t1) / 1000).toFixed(1)} s`);
      if (!up) { rows.push([label, null]); continue; }
      if (tp && !await waitLog(/teleported to/, 10 * 60 * 1000)) console.log("  (no teleport reply)");
      // Settle: the first window after the cut rebuilds plant rings and cloud history.
      const reports = () => log.filter((l) => /^gpu-profile /.test(l)).length;
      const nextReport = async () => { const n0 = reports(), t = Date.now(); while (reports() <= n0) { if (Date.now() - t > 20 * 60 * 1000) throw new Error("no profile report"); await page.waitForTimeout(250); } };
      await nextReport();
      let prof = null;
      if (CPUPROF && tp && /fern/.test(label)) { prof = await page.context().newCDPSession(page); await prof.send("Profiler.enable"); await prof.send("Profiler.setSamplingInterval", { interval: 200 }); await prof.send("Profiler.start"); }
      await page.evaluate(() => window.cretGl.reset());
      const f0 = await frames(), w0 = Date.now(), at = log.length;
      await nextReport();
      const nf = (await frames()) - f0, wall = (Date.now() - w0) / nf;
      const gl = await page.evaluate(() => ({ calls: cretGl.calls, ms: cretGl.ms, draws: cretGl.draws, bytes: cretGl.bytes, uploads: cretGl.uploads,
                                             by: [...cretGl.by.entries()].sort((a, b) => b[1].ms - a[1].ms).slice(0, 12) }));
      if (prof) {
        const { profile } = await prof.send("Profiler.stop");
        fs.writeFileSync(path.join(SHOTS, "play_ferns.cpuprofile"), JSON.stringify(profile));
        const self = new Map(), dt = profile.timeDeltas, total = dt.reduce((a, b) => a + b, 0) / 1000;
        const byId = new Map(profile.nodes.map((n) => [n.id, n]));
        for (let i = 0; i < profile.samples.length; i++) {
          const n = byId.get(profile.samples[i]); const k = (n.callFrame.functionName || "(anonymous)") + (n.callFrame.url ? " " + path.basename(n.callFrame.url).replace(/\?.*/, "") : "");
          self.set(k, (self.get(k) || 0) + (dt[i] || 0) / 1000);
        }
        console.log(`  CPU profile (main thread, ${nf} frames, ${(total / nf).toFixed(0)} ms per frame sampled), self time per frame:`);
        for (const [k, ms] of [...self.entries()].sort((a, b) => b[1] - a[1]).slice(0, 30)) console.log(`    ${(ms / nf).toFixed(2).padStart(8)} ms  ${k}`);
      }
      // The report printed at the end of the window: "gpu-profile <label> (N frames) ... CPU x ms" then "  pass gpu g cpu c" lines.
      const rep = []; let head = null;
      for (let i = at; i < log.length; i++) {
        if (/^gpu-profile /.test(log[i])) { head = log[i]; for (let j = i + 1; j < log.length && /^  \S/.test(log[j]); j++) { const m = log[j].match(/^\s+(\S+)\s+gpu\s+([\d.]+)\s+cpu\s+([\d.]+)/); if (m) rep.push([m[1], +m[3]]); } break; }
      }
      console.log(`  ${head}`);
      console.log(`  ${nf} frames, ${wall.toFixed(0)} ms per frame wall clock (${(1000 / wall).toFixed(2)} fps)`);
      console.log(`  WebGL per frame: ${(gl.calls / nf).toFixed(0)} calls in ${(gl.ms / nf).toFixed(1)} ms, ${(gl.draws / nf).toFixed(0)} draws, ${(gl.uploads / nf).toFixed(0)} uploads of ${(gl.bytes / nf / 1048576).toFixed(2)} MB`);
      console.log("    " + gl.by.map(([k, e]) => `${k} ${(e.n / nf).toFixed(0)}x ${(e.ms / nf).toFixed(2)} ms${e.bytes ? " " + (e.bytes / nf / 1048576).toFixed(2) + " MB" : ""}`).join(", "));
      for (const [k, ms] of rep) console.log(`    ${k.padEnd(16)} ${ms.toFixed(2).padStart(8)} ms`);
      rows.push([label, { nf, wall, gl, rep }]);
      await snap(`perf_${label.replace(/\W+/g, "_")}.png`);
    }
    // One table: passes as rows, the spots as columns (CPU ms per frame).
    const names = []; for (const [, r] of rows) if (r) for (const [k] of r.rep) if (!names.includes(k)) names.push(k);
    const col = (r, k) => { if (!r) return "-"; const e = r.rep.find((x) => x[0] === k); return e ? e[1].toFixed(2) : ""; };
    console.log("\n| pass (CPU ms per frame) | " + rows.map((r) => r[0]).join(" | ") + " |\n|---|" + rows.map(() => "---|").join(""));
    for (const k of names) console.log(`| ${k} | ` + rows.map(([, r]) => col(r, k)).join(" | ") + " |");
    console.log("| WebGL calls / time | " + rows.map(([, r]) => r ? `${(r.gl.calls / r.nf).toFixed(0)} / ${(r.gl.ms / r.nf).toFixed(1)} ms` : "-").join(" | ") + " |");
    console.log("| draws | " + rows.map(([, r]) => r ? (r.gl.draws / r.nf).toFixed(0) : "-").join(" | ") + " |");
    console.log("| uploaded MB | " + rows.map(([, r]) => r ? (r.gl.bytes / r.nf / 1048576).toFixed(2) : "-").join(" | ") + " |");
    console.log("| wall ms per frame | " + rows.map(([, r]) => r ? r.wall.toFixed(0) : "-").join(" | ") + " |");
    return rows.every(([, r]) => r);
  };
  const t0 = Date.now();
  if (PERF) {
    const ok = await perfRun();
    console.log(problems.length ? `problems (${problems.length}):\n  ` + [...new Set(problems)].slice(0, 30).join("\n  ") : "no GL errors, shader failures or exceptions");
    fs.writeFileSync(path.join(SHOTS, "console_perf.log"), log.join("\n"));
    await browser.close(); srv.close();
    process.exit(ok && !problems.length ? 0 : 1);
  }
  if (PHONE) {
    const media = await page.evaluate(() => ({ coarse: matchMedia("(pointer: coarse)").matches, hover: matchMedia("(hover: hover)").matches }));
    console.log(`phone: ${W}x${H} CSS pixels at DPR 3, pointer coarse ${media.coarse}, hover ${media.hover}, S3TC hidden`);
  }
  await page.goto(`http://127.0.0.1:${port}/?profile` + (PHONE ? "" : `&w=${W}&h=${H}`));
  const gl = await page.evaluate(() => {
    const c = document.createElement("canvas").getContext("webgl2");
    return c ? c.getParameter(c.getExtension("WEBGL_debug_renderer_info") ? 0x9246 : c.RENDERER) : "no WebGL2";
  });
  console.log("renderer: " + gl);
  const titled = await waitLog(/first title frame/, 15 * 60 * 1000);
  const tTitle = (Date.now() - t0) / 1000;
  const load = await page.evaluate(() => window.cretLoad);
  const files = []; const walk = (d) => { for (const f of fs.readdirSync(d)) { const p = path.join(d, f); fs.statSync(p).isDirectory() ? walk(p) : files.push(p); } }; walk(DIR);
  const sizes = files.map((p) => [p, fs.statSync(p).size]).sort((a, b) => b[1] - a[1]);
  console.log(`files: ${(sizes.reduce((a, b) => a + b[1], 0) / 1048576).toFixed(1)} MB on the server, largest ${path.relative(DIR, sizes[0][0])} ${(sizes[0][1] / 1048576).toFixed(1)} MB`);
  console.log(`download: ${(bytes / 1048576).toFixed(1)} MB fetched in ${(load.downloadMs / 1000).toFixed(1)} s (local server); data in place at ${(load.startMs / 1000).toFixed(1)} s`);
  console.log(`title screen: ${titled ? "reached" : "NOT reached"} at ${tTitle.toFixed(1)} s`);
  for (const l of log) if (/^load: /.test(l) && !/frame [12]:/.test(l)) console.log("  " + l);
  let ok = titled;
  if (titled) {
    // The flyover fades in over about 1.5 s of game time, and a frame advances
    // it by at most 0.1 s: the title is fully up after about 16 frames.
    const tf0 = await frames(), tw0 = Date.now(); await waitFrames(16); const titleMs = (Date.now() - tw0) / ((await frames()) - tf0);
    await snap(PHONE ? "phone_title.png" : "web_title.png");
    await memory("the title");
    if (PHONE) ok = ok && await phone_play(); else {
    await tap("ArrowDown"); await tap("Enter");            // Character, then Play: start solo play
    const played = await waitLog(/first play frame/, 5 * 60 * 1000);
    console.log(`solo play: ${played ? "started" : "NOT started"} at ${((Date.now() - t0) / 1000).toFixed(1)} s`);
    ok = ok && played;
    if (played) {
      await page.keyboard.down("w"); await waitFrames(4); await page.keyboard.up("w");
      await waitFrames(1);
      const f0 = await frames(), w0 = Date.now(); await waitFrames(6);
      const ms = (Date.now() - w0) / ((await frames()) - f0);
      console.log(`frame time (SwiftShader, ${W}x${H}, low preset): title ${titleMs.toFixed(0)} ms, play ${ms.toFixed(0)} ms per frame`);
      await snap("web_play.png");
      await memory("play");
    }
    }
  }
  if (ok && FIGHT) {
    // Title -> Fights and scenarios (four rows down) -> the scenario menu; Up
    // wraps to "Start the fight"; the window title then names the fight.
    fs.writeFileSync(path.join(SHOTS, "console.log"), log.join("\n"));
    log.length = 0; await page.close(); page = await open();
    await page.goto(`http://127.0.0.1:${port}/?profile&w=${W}&h=${H}`);
    const titled2 = await waitLog(/first title frame/, 15 * 60 * 1000);
    if (titled2) { await waitFrames(2); for (let i = 0; i < 4; i++) await tap("ArrowDown"); await tap("Enter"); }
    const menu = titled2 && await waitLog(/^scenario /, 10 * 60 * 1000);
    if (menu) { await waitFrames(3); await tap("ArrowUp"); await tap("Enter"); }
    let fought = false;
    for (let i = 0; menu && i < 240 && !fought; i++) { fought = /you:/i.test(await page.title()); if (!fought) await page.waitForTimeout(2500); }
    console.log(`play a fight: ${fought ? "started (" + (await page.title()) + ")" : "NOT started"}`);
    if (fought) { await waitFrames(4); await snap("web_fight.png"); }
    ok = ok && fought;
  }
  console.log(problems.length ? `problems (${problems.length}):\n  ` + [...new Set(problems)].slice(0, 30).join("\n  ") : "no GL errors, shader failures or exceptions");
  fs.writeFileSync(path.join(SHOTS, FIGHT ? "console_fight.log" : "console.log"), log.join("\n"));
  await browser.close(); srv.close();
  process.exit(ok && !problems.length ? 0 : 1);
})().catch((e) => { console.error(e); process.exit(1); });
