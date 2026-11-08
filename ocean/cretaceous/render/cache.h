// Startup caches and load profiling. No GL, so the tests build it headless.
//
// Derived data that the renderer rebuilds from assets on every start (decoded
// and block-compressed textures, the foliage atlas with its mips, prop
// meshes) is written to build/cretaceous/cache/ as one file per item:
//   header (magic, format version, key, payload size, payload hash) + payload.
// The key is a hash over everything the item is made from: the input files'
// bytes, the loader's own source file (so a code change rebuilds), a version
// constant and the parameters (size, format). A load is a hit only when the
// magic, version, key, file size and payload hash all match; anything else
// (stale, truncated, corrupt, unreadable) is a miss and the caller rebuilds
// and stores again. Stores write a temporary file and rename it, so a killed
// process never leaves a half-written cache behind.
//   CRET_CACHE=0         never read or write caches (cold path every start)
//   CRET_CACHE_DIR=DIR   another cache directory (tests)
//   CRET_PROFILE_LOAD=1  timing line per startup stage (wall ms, process CPU ms)
#ifndef CRET_CACHE_H
#define CRET_CACHE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CRET_CACHE_MAGIC 0x48435243u    // "CRCH"
#define CRET_CACHE_FORMAT 1u

// 64-bit hashes (FNV-style seed, word-at-a-time mixing; not cryptographic).
#define CRET_HASH_SEED 0xcbf29ce484222325ull
uint64_t cret_hash(uint64_t h, const void* data, size_t n);
uint64_t cret_hash_str(uint64_t h, const char* s);
uint64_t cret_hash_u64(uint64_t h, uint64_t v);
// Hashes a file's bytes (and its name); a missing file hashes a marker, so it
// appearing later changes the key.
uint64_t cret_hash_file(uint64_t h, const char* path);

typedef struct CretBlob { void* data; size_t size; } CretBlob;
bool cret_cache_enabled(void);
// Path of a cache item: <dir>/<name>.bin.
void cret_cache_path(const char* name, char* out, size_t cap);
// Hit: *out holds the payload (free with free()). Miss: false, *out zeroed.
bool cret_cache_load(const char* name, uint64_t key, CretBlob* out);
// Writes the parts back to back as one payload. False on any I/O error.
bool cret_cache_store(const char* name, uint64_t key, const void* const* parts, const size_t* sizes, int nparts);

// Little-endian cursor over a payload (bounds-checked: a short read fails and
// sticks, so a parse can check once at the end).
typedef struct CretReader { const uint8_t* p; size_t n, at; bool bad; } CretReader;
const void* cret_read(CretReader* r, size_t n);
uint32_t cret_read_u32(CretReader* r);

// Load profiling (CRET_PROFILE_LOAD=1): cret_load_mark prints the time since
// the previous mark. GL code calls CRET_LOAD_MARK, which also waits for the
// GPU (glFinish) so uploads count toward their own stage.
bool cret_load_profiling(void);
void cret_load_mark(const char* stage);
double cret_load_seconds(void);   // since the first call (process start for the player)
#define CRET_LOAD_MARK(stage) do { if (cret_load_profiling()) { glFinish(); cret_load_mark(stage); } } while (0)

// Runs fn(i, ctx) for i in [0, n) on up to `threads` threads (pthreads; 0:
// the online CPU count, capped at 8). CPU work only: decode and parse, never GL.
void cret_parallel_for(int n, int threads, void (*fn)(int i, void* ctx), void* ctx);
#endif
