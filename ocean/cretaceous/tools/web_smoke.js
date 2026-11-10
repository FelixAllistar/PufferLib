// Headless smoke test of the web build in Chromium with software WebGL2
// (SwiftShader), no GPU needed. Serves the build directory itself, loads the
// title screen, starts solo play from the menu (Down, Enter on "Play"), and
// checks that frames render without page exceptions, failed shader compiles or
// WebGL errors. Prints the load times, the frame time and the download size,
// and saves screenshots. --fight then reloads the page and starts "Play a
// fight" from the title (Fights and scenarios, Start the fight).
//   node ocean/cretaceous/tools/web_smoke.js [--fight] [WEB_DIR] [SHOT_DIR]
//   (defaults build/cretaceous/web and build/cretaceous/web-shots)
// Needs Playwright (require("playwright")) with its Chromium. Exit status 1 on failure.
"use strict";
const http = require("http"), fs = require("fs"), path = require("path");
const { chromium } = require("playwright");

const FIGHT = process.argv.includes("--fight");
const POS = process.argv.slice(2).filter((a) => !a.startsWith("--"));
const DIR = path.resolve(POS[0] || "build/cretaceous/web");
const SHOTS = path.resolve(POS[1] || "build/cretaceous/web-shots");
const W = 960, H = 540;
const TYPES = { ".html": "text/html", ".js": "text/javascript", ".json": "application/json", ".bin": "application/octet-stream" };

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
    const p = await browser.newPage({ viewport: { width: W, height: H } });
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
  const t0 = Date.now();
  await page.goto(`http://127.0.0.1:${port}/?profile&w=${W}&h=${H}`);
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
    await snap("web_title.png");
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
