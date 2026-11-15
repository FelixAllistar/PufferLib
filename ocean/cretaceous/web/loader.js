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
  // A promise nobody handled. Once the game is drawing, only a crash of the
  // wasm itself ends it: the browser refusing the pointer lock (a click in a
  // page without focus, a second request too soon after Esc) or fullscreen
  // rejects such a promise too, and the game plays on without the lock.
  window.addEventListener("unhandledrejection", (e) => {
    const r = e.reason, crash = window.WebAssembly && r instanceof WebAssembly.RuntimeError;
    if (!crash && window.Module && Module.cretFrames > 0) { console.warn("cretaceous: ignored:", r); e.preventDefault(); return; }
    fail(r);
  });
  window.cretLoad = { downloadMs: 0, startMs: 0, bytes: 0, stored: 0, texRestored: 0, texSaved: 0, config: 0 };

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
  // First-visit quality (used only while no settings are saved; menu.c,
  // cret_settings_default): a desktop with a hardware GPU starts on the high
  // preset at full resolution, and the game steps down if the frame rate
  // stays low. The renderer's name is only a hint, not a GPU list: a software
  // renderer (SwiftShader, llvmpipe and the like) starts low, as phones do.
  const dbg = probe.getExtension("WEBGL_debug_renderer_info");
  const renderer = String(probe.getParameter(dbg ? dbg.UNMASKED_RENDERER_WEBGL : probe.RENDERER) || "");
  const software = /swiftshader|llvmpipe|softpipe|software|basic render/i.test(renderer);
  const quality = mobile || software ? "low" : "high";
  window.cretLoad.quality = quality; window.cretLoad.renderer = renderer;
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

  // Settings and saved characters. The game writes them under its config
  // directory (menu.c, character.c) in the in-memory file system, which is
  // gone on reload, so every save is copied at once into localStorage (one
  // key, file name -> text) and put back before main runs. localStorage rather
  // than an IndexedDB mount (IDBFS): it is synchronous, so a save is in
  // storage before the page can go away (FS.syncfs is asynchronous and may
  // not finish in pagehide), there is nothing to wait for before the title,
  // and the files are small (well under a kilobyte each). In a private window
  // it lasts for the session; where storage is blocked or full, the game
  // works as before and forgets on reload.
  const CFG = "/home/web_user/.config/cretaceous", CFG_KEY = "cretaceous-config-v1";
  const cfgStore = (() => { try { const st = window.localStorage; st.getItem(CFG_KEY); return st; } catch (e) { return null; } })();
  function restoreConfig(FS) {
    if (!cfgStore) return;
    try {
      const files = JSON.parse(cfgStore.getItem(CFG_KEY) || "{}");
      for (const [rel, text] of Object.entries(files)) {
        if (!/^(characters\/)?[\w-]+\.ini$/.test(rel) || typeof text !== "string") continue;   // only what the game itself writes
        const p = CFG + "/" + rel;
        FS.mkdirTree(p.substring(0, p.lastIndexOf("/")));
        FS.writeFile(p, text);
        window.cretLoad.config++;
      }
    } catch (e) { console.warn("cretaceous: saved settings not restored:", e); }
  }
  function saveConfig() {
    if (!cfgStore) return;
    const FS = window.Module.FS, files = {};
    const add = (rel) => { try { files[rel] = FS.readFile(CFG + "/" + rel, { encoding: "utf8" }); } catch (e) { /* not there */ } };
    add("settings.ini");
    try { for (const n of FS.readdir(CFG + "/characters")) if (/^[\w-]+\.ini$/.test(n)) add("characters/" + n); } catch (e) { /* no characters yet */ }
    try { cfgStore.setItem(CFG_KEY, JSON.stringify(files)); } catch (e) { console.warn("cretaceous: settings not kept (browser storage full or blocked):", e); }
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
  // impostors at 128, unless ?env= sets a knob itself (such pages also start
  // on the low preset, menu.c). tools/web_smoke.js --phone measures it.
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

  // Short presses. raylib's web backend keeps only the current state of each
  // key and mouse button, set by GLFW's listeners as the browser's events
  // arrive, and the game asks whether one went down since the last frame
  // (IsKeyPressed, IsMouseButtonPressed). A click or a tap of a key that went
  // down and up again while a frame was being drawn was never seen: at 10 fps
  // that is most quick presses. So an event reaches raylib only once the game
  // has begun a frame since that key or button last changed; until then it
  // waits here, in the order it came, and goes on as the next frame begins.
  // A press then shows for one frame however short it was, and a key held
  // longer than a frame changes the moment it does, as before. These
  // listeners come before GLFW's (same target and phase, added earlier).
  // Repeats of a held key pass straight on; one for a key whose press is still
  // waiting is dropped, so it cannot become a second press.
  const input = (() => {
    let frame = 0;                                   // frames the game has begun (Module.cretFrames)
    const changed = new Map(), down = new Set();     // per key ("k" + keyCode) or button ("m" + button): the frame raylib last saw it change, and which are down there
    const queue = [], mine = new Set();              // events waiting [id, event, target], oldest first; events passed on from here
    let last = null;                                 // the last mouse event on the canvas (where a let-go button is released)
    let escAt = -1e9, synthAt = -1e9, swallowUp = false, asked = false, locked = false;
    const isDown = (t) => t === "keydown" || t === "mousedown";
    // Tells raylib (through GLFW's listeners) now.
    const pass = (id, e, target) => {
      if (isDown(e.type) !== down.has(id)) { changed.set(id, frame); if (isDown(e.type)) down.add(id); else down.delete(id); }
      if (!target) return;   // the event itself is on its way to GLFW
      mine.add(e); try { target.dispatchEvent(e); } finally { mine.delete(e); }
    };
    const ready = (id) => !(changed.get(id) >= frame);
    const take = (e, target) => {
      const key = e.type[0] === "k", id = key ? "k" + e.keyCode : "m" + e.button;
      if (key && e.repeat) { if (!down.has(id)) e.stopImmediatePropagation(); return; }
      if (!queue.length && ready(id)) { pass(id, e, target); return; }
      if (!target) {
        e.stopImmediatePropagation();
        if (key && (e.key === "Tab" || e.key === "Backspace")) e.preventDefault();   // as GLFW does: no focus change or page back
      }
      queue.push([id, e, target || e.target]);
    };
    // Keys the browser would act on before the game. Always: F1 (help), F3
    // (find), F6 (address bar), F7 (caret browsing), F10 and a lone Alt (the
    // menu bar; Alt is the web build's walk key, hud.h), and Alt with another
    // key (Alt+D address bar, Alt+F menu, Alt+Left back). While playing also
    // F5 and the Ctrl combinations a page may keep (Ctrl+S, D, R, F, P...), as
    // Ctrl still walks there; on the title they reload and so on as usual.
    // Ctrl+W, T, N and Tab never reach a page outside fullscreen: there leaving
    // asks first (beforeunload, below), and in fullscreen the keyboard lock
    // hands them to the game. AltGr (typing in the console) and F11, F12 stay
    // the browser's. Tab and Backspace are kept by GLFW itself.
    const browserKey = (e) => {
      if (e.getModifierState && e.getModifierState("AltGraph")) return false;
      const k = e.key, playing = !!(window.Module && Module.cretPlaying);
      return k === "Alt" || (e.altKey && e.type === "keydown") || /^F([1-46-9]|10)$/.test(k) || (playing && (k === "F5" || e.ctrlKey));
    };
    const onKey = (e) => {
      if (mine.has(e)) return;
      if (e.isTrusted && browserKey(e)) e.preventDefault();
      if (e.keyCode === 27 && e.isTrusted) {
        // Esc delivered by the browser just after the one passed on for a
        // lost pointer lock (below): not a second press.
        if (e.type === "keydown" && performance.now() - synthAt < 500) { e.stopImmediatePropagation(); swallowUp = true; return; }
        if (e.type === "keyup" && swallowUp) { e.stopImmediatePropagation(); swallowUp = false; return; }
        if (e.type === "keydown") escAt = performance.now();
      }
      take(e, null);
    };
    // Mouse buttons: GLFW only listens on the canvas. A button let go off the
    // canvas (a drag out of the window) is released there too, so it does
    // not stay down.
    const onMouse = (e) => {
      if (mine.has(e)) return;
      const id = "m" + e.button, onCanvas = e.target === canvas;
      if (onCanvas) last = e;
      if (e.type === "mousedown" && onCanvas) {
        if (document.activeElement !== canvas) canvas.focus({ preventScroll: true });   // keys go to the game after the first click
        take(e, null);
      } else if (e.type === "mouseup" && (onCanvas || down.has(id) || queue.some((q) => q[0] === id))) {
        if (onCanvas) take(e, null); else take(new MouseEvent("mouseup", e), canvas);
      }
    };
    for (const t of ["keydown", "keyup"]) window.addEventListener(t, onKey, true);
    for (const t of ["mousedown", "mouseup"]) window.addEventListener(t, onMouse, true);
    window.addEventListener("mousemove", (e) => { if (e.target === canvas) last = e; }, { capture: true, passive: true });
    // Leaving the page (another window or tab): every key and button still
    // down is let go, through the queue like any other release, and presses
    // still waiting are dropped (they belong to a moment that is over). GLFW
    // would let go of the keys itself, but at once, before a press just
    // passed on had its frame, and never of the mouse buttons.
    window.addEventListener("blur", (e) => {
      if (e.target !== window) return;   // an element losing focus, not the page
      e.stopImmediatePropagation();
      for (let i = queue.length - 1; i >= 0; i--) if (isDown(queue[i][1].type)) queue.splice(i, 1);
      const ups = new Set(queue.map((q) => q[0]));
      for (const id of [...down]) {
        if (ups.has(id)) continue;
        const n = +id.slice(1);
        if (id[0] === "k") take(new KeyboardEvent("keyup", { keyCode: n, which: n, bubbles: true }), window);
        else take(new MouseEvent("mouseup", Object.assign(last ? { clientX: last.clientX, clientY: last.clientY } : {}, { button: n, bubbles: true })), canvas);
      }
    }, true);
    // Pointer lock: the browser takes the first Esc to end it, and the page
    // does not see that key, so in play it took a second Esc to open the menu.
    // When the lock ends without the game asking (Esc, or switching away),
    // Esc is pressed here instead; an Esc the browser does deliver within
    // half a second is not taken as a second press.
    const exitLock = Document.prototype.exitPointerLock;
    if (exitLock) Document.prototype.exitPointerLock = function () { asked = true; return exitLock.apply(this, arguments); };
    document.addEventListener("pointerlockchange", () => {
      if (document.pointerLockElement === canvas) { locked = true; asked = false; return; }
      const lost = locked && !asked; locked = asked = false;
      if (!lost || performance.now() - escAt < 500) return;
      synthAt = performance.now();
      for (const t of ["keydown", "keyup"]) {
        take(new KeyboardEvent(t, { key: "Escape", code: "Escape", keyCode: 27, which: 27, bubbles: true, cancelable: true }), window);
      }
    });
    canvas.addEventListener("touchstart", () => { if (document.activeElement !== canvas) canvas.focus({ preventScroll: true }); }, { passive: true });
    // Fullscreen (Settings > Display, or the first tap on a touch device):
    // the Keyboard Lock API (Chrome and Edge) hands the game every key, Ctrl+W
    // and Esc too; the browser then says to hold Esc to leave fullscreen.
    document.addEventListener("fullscreenchange", () => {
      const kb = navigator.keyboard; if (!kb || !kb.lock) return;
      if (document.fullscreenElement) kb.lock().catch(() => {}); else kb.unlock();
    });
    return {
      get frame() { return frame; },
      // A frame begins: what waits goes on in order, each key or button
      // changing at most once per frame (raylib must see it for a frame first).
      set frame(n) {
        while (queue.length && ready(queue[0][0])) { const [id, e, target] = queue.shift(); pass(id, e, target); }
        frame = n;
      },
    };
  })();

  // Leaving while a game runs (Ctrl+W, Ctrl+R, F5, the back gesture, closing
  // the tab) asks first; Module.cretPlaying is set by the game (cretaceous.c,
  // cret_web_playing). Whether or not the player stays, the character is
  // saved when the page goes away or out of sight (cret_web_save), and its
  // file goes to browser storage with it (saveConfig).
  window.addEventListener("beforeunload", (e) => { if (window.Module && Module.cretPlaying) { e.preventDefault(); e.returnValue = ""; } });
  const saveNow = () => { try { if (window.Module && Module._cret_web_save) Module._cret_web_save(); } catch (e) { console.warn("cretaceous: character not saved:", e); } };
  window.addEventListener("pagehide", saveNow);
  document.addEventListener("visibilitychange", () => { if (document.hidden) saveNow(); });

  window.Module = {
    canvas,
    arguments: args,
    // Counted by the game at the start of each frame (cretaceous.c,
    // cret_web_next_frame); short presses wait for it (above).
    get cretFrames() { return input.frame; },
    set cretFrames(n) { input.frame = n; },
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
    // The game calls this after writing a settings or character file (character.c, cret_config_saved).
    cretConfigSaved: saveConfig,
    preRun: [function () {
      const M = window.Module;
      M.ENV.HOME = "/home/web_user";
      restoreConfig(M.FS);
      M.ENV.CRET_WEB_QUALITY = quality;
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
