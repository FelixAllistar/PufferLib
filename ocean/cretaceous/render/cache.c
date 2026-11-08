// Startup caches, hashes, load profiling and a small thread pool (cache.h).
// build.sh lists the player's sources outside this directory, so view.c
// compiles this file by including it; the tests compile it on its own.
#include "cache.h"
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

// ---------------------------------------------------------------- hashing
static inline uint64_t mix64(uint64_t x) {
    x ^= x >> 33; x *= 0xff51afd7ed558ccdull; x ^= x >> 33; x *= 0xc4ceb9fe1a85ec53ull; x ^= x >> 33; return x;
}
uint64_t cret_hash(uint64_t h, const void* data, size_t n) {
    const uint8_t* p = data; size_t i = 0;
    // Four independent lanes keep the multiply chains short (about 4 GB/s).
    uint64_t l[4] = {h, h ^ 0x9e3779b97f4a7c15ull, h ^ 0x632be59bd9b4e019ull, h ^ 0x85ebca77c2b2ae63ull};
    for (; i + 32 <= n; i += 32)
        for (int k = 0; k < 4; k++) { uint64_t w; memcpy(&w, p + i + 8 * k, 8); l[k] = (l[k] ^ w) * 0x100000001b3ull; l[k] ^= l[k] >> 29; }
    uint64_t r = mix64(l[0]) ^ mix64(l[1] + 1) ^ mix64(l[2] + 2) ^ mix64(l[3] + 3);
    for (; i < n; i++) { r ^= p[i]; r *= 0x100000001b3ull; }
    return mix64(r ^ (uint64_t)n);
}
uint64_t cret_hash_str(uint64_t h, const char* s) { return cret_hash(h, s, s ? strlen(s) : 0); }
uint64_t cret_hash_u64(uint64_t h, uint64_t v) { return cret_hash(h, &v, sizeof v); }
uint64_t cret_hash_file(uint64_t h, const char* path) {
    h = cret_hash_str(h, path);
    FILE* f = fopen(path, "rb"); if (!f) return cret_hash_u64(h, 0x6d697373696e67ull);   // "missing"
    enum { CH = 1 << 20 }; uint8_t* buf = malloc(CH); size_t n;
    while (buf && (n = fread(buf, 1, CH, f)) > 0) h = cret_hash(h, buf, n);
    free(buf); fclose(f); return h;
}

// ---------------------------------------------------------------- blob cache
typedef struct { uint32_t magic, format; uint64_t key, size, hash; } CacheHeader;   // 32 bytes

bool cret_cache_enabled(void) { const char* e = getenv("CRET_CACHE"); return !(e && atoi(e) == 0); }
static const char* cache_dir(void) { const char* d = getenv("CRET_CACHE_DIR"); return d && *d ? d : "build/cretaceous/cache"; }
void cret_cache_path(const char* name, char* out, size_t cap) { snprintf(out, cap, "%s/%s.bin", cache_dir(), name); }

bool cret_cache_load(const char* name, uint64_t key, CretBlob* out) {
    memset(out, 0, sizeof *out);
    if (!cret_cache_enabled()) return false;
    char path[1024]; cret_cache_path(name, path, sizeof path);
    FILE* f = fopen(path, "rb"); if (!f) return false;
    CacheHeader h; struct stat st;
    bool ok = fread(&h, sizeof h, 1, f) == 1 && h.magic == CRET_CACHE_MAGIC && h.format == CRET_CACHE_FORMAT && h.key == key &&
              fstat(fileno(f), &st) == 0 && (uint64_t)st.st_size == sizeof h + h.size && h.size < ((uint64_t)1 << 34);
    void* data = ok ? malloc(h.size ? h.size : 1) : NULL;
    ok = ok && data && fread(data, 1, h.size, f) == h.size && cret_hash(CRET_HASH_SEED, data, h.size) == h.hash;
    fclose(f);
    if (!ok) {
        free(data);
        if (cret_load_profiling()) fprintf(stderr, "cache: %s stale or corrupt, rebuilding\n", name);
        return false;
    }
    out->data = data; out->size = h.size;
    return true;
}

static void mkdirs(const char* dir) {
    char p[1024]; snprintf(p, sizeof p, "%s", dir);
    for (char* s = p + 1; *s; s++) if (*s == '/') { *s = 0; mkdir(p, 0755); *s = '/'; }
    mkdir(p, 0755);
}
bool cret_cache_store(const char* name, uint64_t key, const void* const* parts, const size_t* sizes, int nparts) {
    if (!cret_cache_enabled()) return false;
    mkdirs(cache_dir());
    char path[1024], tmp[1100]; cret_cache_path(name, path, sizeof path);
    snprintf(tmp, sizeof tmp, "%s.%d.tmp", path, (int)getpid());
    // Hash of the concatenated parts, streamed in the same order as written.
    // (cret_hash is not incremental, so hash a contiguous copy when split.)
    uint64_t total = 0; for (int i = 0; i < nparts; i++) total += sizes[i];
    uint8_t* joined = NULL; const void* whole = nparts == 1 ? parts[0] : NULL;
    if (!whole) {
        joined = malloc(total ? total : 1); if (!joined) return false;
        size_t at = 0; for (int i = 0; i < nparts; i++) { if (sizes[i]) memcpy(joined + at, parts[i], sizes[i]); at += sizes[i]; }
        whole = joined;
    }
    CacheHeader h = {CRET_CACHE_MAGIC, CRET_CACHE_FORMAT, key, total, cret_hash(CRET_HASH_SEED, whole, total)};
    FILE* f = fopen(tmp, "wb");
    bool ok = f && fwrite(&h, sizeof h, 1, f) == 1 && (total == 0 || fwrite(whole, 1, total, f) == total);
    if (f && fclose(f) != 0) ok = false;
    free(joined);
    if (ok) ok = rename(tmp, path) == 0;
    if (!ok) { remove(tmp); fprintf(stderr, "cache: could not write %s (%s)\n", path, strerror(errno)); }
    return ok;
}

const void* cret_read(CretReader* r, size_t n) {
    if (r->bad || n > r->n - r->at) { r->bad = true; return NULL; }
    const void* p = r->p + r->at; r->at += n; return p;
}
uint32_t cret_read_u32(CretReader* r) { const void* p = cret_read(r, 4); uint32_t v = 0; if (p) memcpy(&v, p, 4); return v; }

// ---------------------------------------------------------------- load profiling
static double now_s(clockid_t c) { struct timespec t; clock_gettime(c, &t); return t.tv_sec + t.tv_nsec * 1e-9; }
static double g_t0 = -1, g_last, g_cpu_last;
static int g_prof = -1;
bool cret_load_profiling(void) {
    if (g_prof < 0) { const char* e = getenv("CRET_PROFILE_LOAD"); g_prof = e && atoi(e) != 0; }
    return g_prof;
}
double cret_load_seconds(void) {
    if (g_t0 < 0) { g_t0 = g_last = now_s(CLOCK_MONOTONIC); g_cpu_last = now_s(CLOCK_PROCESS_CPUTIME_ID); }
    return now_s(CLOCK_MONOTONIC) - g_t0;
}
void cret_load_mark(const char* stage) {
    if (!cret_load_profiling()) return;
    double total = cret_load_seconds(), t = now_s(CLOCK_MONOTONIC), c = now_s(CLOCK_PROCESS_CPUTIME_ID);
    fprintf(stderr, "load: %-34s %8.1f ms  cpu %8.1f ms  total %6.2f s\n", stage, (t - g_last) * 1000, (c - g_cpu_last) * 1000, total);
    g_last = t; g_cpu_last = c;
}

// ---------------------------------------------------------------- thread pool
typedef struct { int n, next; void (*fn)(int, void*); void* ctx; pthread_mutex_t m; } ParFor;
static void* par_worker(void* arg) {
    ParFor* p = arg;
    for (;;) {
        pthread_mutex_lock(&p->m); int i = p->next++; pthread_mutex_unlock(&p->m);
        if (i >= p->n) return NULL;
        p->fn(i, p->ctx);
    }
}
void cret_parallel_for(int n, int threads, void (*fn)(int i, void* ctx), void* ctx) {
    if (n <= 0) return;
    if (threads <= 0) { long c = sysconf(_SC_NPROCESSORS_ONLN); threads = c > 0 ? (int)c : 4; if (threads > 8) threads = 8; }
    const char* e = getenv("CRET_LOAD_THREADS"); if (e && atoi(e) > 0) threads = atoi(e);
    if (threads > n) threads = n;
    ParFor p = {n, 0, fn, ctx, PTHREAD_MUTEX_INITIALIZER};
    if (threads <= 1) { for (int i = 0; i < n; i++) fn(i, ctx); return; }
    pthread_t th[64]; int started = 0;
    for (int t = 1; t < threads && t < 64; t++) if (pthread_create(&th[started], NULL, par_worker, &p) == 0) started++;
    par_worker(&p);   // the calling thread works too
    for (int t = 0; t < started; t++) pthread_join(th[t], NULL);
    pthread_mutex_destroy(&p.m);
}
