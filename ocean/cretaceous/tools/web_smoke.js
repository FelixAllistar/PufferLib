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
// --short-press throttles nothing (a SwiftShader frame already takes seconds,
// far slower than 10 fps) and sends every press as a down and an up 10-30 ms
// apart, so most fall inside one frame: on the title a click on Settings, Esc
// back, Up five times to Play and Enter (play only starts if every one of them
// counted, in order); in play left and right clicks, Space (twice within a
// frame too), B, 1-3, E, G, Q, W held with key repeats, and Esc to the menu
// and back. It fails when the game saw any press other than exactly once,
// for one frame, in the order sent (touch.c records each frame's presses).
// Both modes report memory at the title and in play: the wasm heap, the
// in-memory file system, the JS heap, an estimate of GPU memory (texture and
// renderbuffer uploads, counted by wrapping WebGL) and the browser's processes.
//   node ocean/cretaceous/tools/web_smoke.js [--fight | --phone | --short-press] [WEB_DIR] [SHOT_DIR]
//   (defaults build/cretaceous/web and build/cretaceous/web-shots)
// Needs Playwright (require("playwright")) with its Chromium. Exit status 1 on failure.
"use strict";
const http = require("http"), fs = require("fs"), path = require("path");
const { chromium } = require("playwright");

const FIGHT = process.argv.includes("--fight"), PHONE = process.argv.includes("--phone"), SHORT = process.argv.includes("--short-press");
const { execSync } = require("child_process");
const POS = process.argv.slice(2).filter((a) => !a.startsWith("--"));
const DIR = path.resolve(POS[0] || "build/cretaceous/web");
const SHOTS = path.resolve(POS[1] || "build/cretaceous/web-shots");
const W = PHONE ? 844 : 960, H = PHONE ? 390 : 540;   // the phone: CSS pixels, landscape
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
    await p.addInitScript(GPU_COUNT);
    if (PHONE) await p.addInitScript(NO_S3TC);
    // --short-press: Esc keys the browser delivers, and pointer lock changes
    // (this listener comes before the page's own).
    if (SHORT) await p.addInitScript(() => {
      window.cretEsc = { keys: 0, locks: 0, unlocks: 0 };
      window.addEventListener("keydown", (e) => { if (e.keyCode === 27 && e.isTrusted) cretEsc.keys++; }, true);
      document.addEventListener("pointerlockchange", () => { if (document.pointerLockElement) cretEsc.locks++; else cretEsc.unlocks++; });
    });
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
  // --short-press. raylib's key codes, and 1000 + button for the mouse (touch.c).
  const CODE = { Enter: 257, Escape: 256, ArrowUp: 265, Space: 32, b: 66, 1: 49, 2: 50, 3: 51, e: 69, g: 71, q: 81, w: 87, left: 1000, right: 1001 };
  const seen = () => page.evaluate(() => (window.Module && Module.cretInput || []).slice());
  // A press of 10-30 ms, as a quick click or tap of a key is.
  const quick = async (what) => {
    const ms = 10 + Math.random() * 20;
    if (what === "left" || what === "right") { await page.mouse.down({ button: what }); await page.waitForTimeout(ms); await page.mouse.up({ button: what }); }
    else { await page.keyboard.down(what); await page.waitForTimeout(ms); await page.keyboard.up(what); }
  };
  // Sends the presses, each after a pause of up to one frame (so some share a
  // frame), waits until the game has seen them all, and checks what it saw:
  // every press down for exactly one frame, in the order sent.
  // leaves: the last press ends a loop (Enter on Play). The game then yields
  // twice before the next loop draws (cretaceous.c), so its release shows in
  // that loop's first frame, a few counts on.
  const presses = async (label, list, leaves = false) => {
    const f0 = await frames(), w0 = Date.now(); await waitFrames(1);
    const frameMs = (Date.now() - w0) / Math.max(1, (await frames()) - f0);
    const from = await frames();
    for (const p of list) { await quick(p); await page.waitForTimeout(Math.random() * frameMs); }
    await waitFrames(2 * list.length + 2, 20 * 60 * 1000);
    const got = (await seen()).filter((e) => e[0] > from);
    let lost = 0, extra = 0, held = 0, order = 0, last = -1; const notes = [];
    const used = new Set();
    for (const p of list) {
      const i = got.findIndex((e, k) => !used.has(k) && e[1] === CODE[p] && e[2] === 1);
      if (i < 0) { lost++; continue; }
      used.add(i);
      const up = got.find((e) => e[1] === CODE[p] && e[2] === 0 && e[0] > got[i][0]);
      if (!up || (up[0] !== got[i][0] + 1 && !(leaves && p === list[list.length - 1]))) { held++; notes.push(`${p} down in frame ${got[i][0]}, up ${up ? "in frame " + up[0] : "never"}`); }
      if (got[i][0] < last) order++;
      last = got[i][0];
    }
    extra = got.filter((e, k) => e[2] === 1 && !used.has(k) && list.some((p) => CODE[p] === e[1])).length;
    console.log(`${label}: ${list.length} presses at ${(1000 / frameMs).toFixed(2)} fps (${frameMs.toFixed(0)} ms a frame): ` +
                `${list.length - lost} seen, ${lost} lost, ${extra} extra, ${held} not exactly one frame, ${order} out of order` + (notes.length ? ` (${notes.join("; ")})` : ""));
    return { lost, bad: lost + extra + held + order };
  };
  const short_press = async () => {
    // The title's rows at 960x540 (menu.c, short windows: the HUD scale is
    // 0.75, rows from 128 UI units down, 59 apart): Settings is the seventh.
    await page.mouse.move(120, (128 + 6 * 59.1 + 11) * 0.75);
    await waitFrames(1);
    const title = await presses("title (click Settings, Esc back, Up x5 to Play, Enter)", ["left", "Escape", "ArrowUp", "ArrowUp", "ArrowUp", "ArrowUp", "ArrowUp", "Enter"], true);
    let played = await waitLog(/first play frame/, 5 * 60 * 1000);
    console.log(`solo play from those presses: ${played ? "started" : "NOT started"}`);
    let ok = played && !title.bad;
    if (!played) {   // still measure play: start it from the page
      log.length = 0; await page.close(); page = await open();
      await page.goto(`http://127.0.0.1:${port}/?profile&play&w=${W}&h=${H}`);
      played = await waitLog(/first play frame/, 15 * 60 * 1000);
      if (!played) return false;
    }
    await waitFrames(2);
    await page.mouse.move(W / 2, H / 2);
    const play = await presses("play (clicks, Space twice, B, 1-3, E, G, Q)", ["left", "right", "Space", "Space", "b", "1", "2", "3", "e", "g", "q", "left"]);
    // W held over several frames with key repeats: one press, let go when it is.
    const from = await frames();
    await page.keyboard.down("w"); for (let i = 0; i < 3; i++) { await waitFrames(1); await page.keyboard.down("w"); }
    const upAt = await frames(); await page.keyboard.up("w"); await waitFrames(2);
    const w = (await seen()).filter((e) => e[0] > from && e[1] === CODE.w);
    const wOk = w.length === 2 && w[0][2] === 1 && w[1][2] === 0 && w[1][0] <= upAt + 1;
    console.log(`W held for 3 frames with repeats: ${w.map((e) => (e[2] ? "down" : "up") + " in frame " + e[0]).join(", ")} (let go after frame ${upAt}): ${wOk ? "ok" : "WRONG"}`);
    // W and the left button held when the page loses focus (another window
    // or tab; here the window's blur event): both let go, nothing stays down.
    const from2 = await frames();
    await page.keyboard.down("w"); await page.mouse.down(); await waitFrames(2);
    await page.evaluate(() => window.dispatchEvent(new FocusEvent("blur")));
    await waitFrames(2);
    const after = (await seen()).filter((e) => e[0] > from2 && (e[1] === CODE.w || e[1] === CODE.left));
    await page.keyboard.up("w"); await page.mouse.up(); await waitFrames(2);
    const blurOk = [CODE.w, CODE.left].every((c) => after.filter((e) => e[1] === c).map((e) => e[2]).join() === "1,0");
    console.log(`W and the left button held, then the page loses focus: ${blurOk ? "both let go" : "STUCK (" + JSON.stringify(after) + ")"}`);
    const lock = await page.evaluate(() => document.pointerLockElement === Module.canvas), esc0 = await page.evaluate(() => Object.assign({}, cretEsc));
    const menu = await presses("pause menu (Esc, Esc)", ["Escape", "Escape"]);
    const esc = await page.evaluate(() => cretEsc);
    console.log(`pointer lock in play: ${lock ? "held" : "not held"}; during the two Esc presses the browser delivered ${esc.keys - esc0.keys} Esc keys, ` +
                `the lock ended ${esc.unlocks - esc0.unlocks} and began ${esc.locks - esc0.locks} times; focus on the canvas: ${await page.evaluate(() => document.activeElement === Module.canvas)}`);
    await snap("short_play.png");
    return ok && !play.bad && wOk && blurOk && !menu.bad;
  };
  // Memory now: the page's view plus the resident size of the browser's
  // processes (the GPU process holds SwiftShader's textures).
  const MB = (b) => (b / 1048576).toFixed(0) + " MB";
  const memory = async (label) => {
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
  const t0 = Date.now();
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
    if (PHONE) ok = ok && await phone_play(); else if (SHORT) ok = ok && await short_press(); else {
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
