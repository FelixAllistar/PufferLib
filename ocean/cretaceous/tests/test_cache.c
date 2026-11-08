// Startup cache tests (render/cache.c; no GL, no world).
//   - a stored item loads back byte for byte (one part and several parts);
//   - a different key, a flipped payload or header byte, a truncated or
//     extended file, a missing file and CRET_CACHE=0 are all misses, and a
//     store after a miss makes the next load a hit again (a deleted or
//     corrupt cache rebuilds itself);
//   - no temporary files are left behind;
//   - content keys: a file's hash changes with its bytes and when it
//     appears; cret_hash_files is deterministic, ordered and content-keyed;
//   - the reader refuses short reads and cret_parallel_for runs every index once.
// Runs in a fresh directory under build/cretaceous/test/ (CRET_CACHE_DIR).
#include "test_util.h"
#include "../render/cache.h"
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

static char g_dir[512];
static void path_of(const char* name, char* out, size_t cap) { cret_cache_path(name, out, cap); }
static long file_size(const char* p) { struct stat st; return stat(p, &st) == 0 ? (long)st.st_size : -1; }
static void write_file(const char* p, const void* d, size_t n) { FILE* f = fopen(p, "wb"); if (f) { fwrite(d, 1, n, f); fclose(f); } }
static void flip_byte(const char* p, long at) {
    FILE* f = fopen(p, "r+b"); if (!f) return;
    fseek(f, at, SEEK_SET); int c = fgetc(f); fseek(f, at, SEEK_SET); fputc(c ^ 0x40, f); fclose(f);
}
static bool load_eq(const char* name, uint64_t key, const void* want, size_t n) {
    CretBlob b; bool hit = cret_cache_load(name, key, &b);
    bool eq = hit && b.size == n && (n == 0 || !memcmp(b.data, want, n));
    free(b.data); return eq;
}
static bool miss(const char* name, uint64_t key) {
    CretBlob b; bool hit = cret_cache_load(name, key, &b);
    bool ok = !hit && b.data == NULL && b.size == 0; free(b.data); return ok;
}
static int tmp_files(void) {
    DIR* d = opendir(g_dir); int n = 0; struct dirent* e;
    if (!d) return -1;
    while ((e = readdir(d))) n += strstr(e->d_name, ".tmp") != NULL;
    closedir(d); return n;
}

static void test_store_load(void) {
    uint8_t data[5000]; uint32_t rng = 3;
    for (size_t i = 0; i < sizeof data; i++) data[i] = (uint8_t)t_rand(&rng);
    const void* parts[3] = {data, data + 1000, data + 1000}; size_t sizes[3] = {1000, 0, 4000};
    uint64_t key = cret_hash_str(CRET_HASH_SEED, "test item v1");
    char p[1024]; path_of("item", p, sizeof p);
    CHECK(miss("item", key), "load before any store hit");
    CHECK(cret_cache_store("item", key, parts, sizes, 3), "store failed");
    CHECK(load_eq("item", key, data, sizeof data), "stored item did not load back");
    CHECK(file_size(p) > (long)sizeof data, "cache file %s has %ld bytes", p, file_size(p));
    // Stale: another key (a changed input or version) is a miss; the file stays until rewritten.
    CHECK(miss("item", key ^ 1), "a different key hit");
    CHECK(load_eq("item", key, data, sizeof data), "a miss on another key broke the item");
    // Corrupt payload and header bytes.
    long size = file_size(p);
    for (int k = 0; k < 4; k++) {
        long at = k == 0 ? size - 1 : k == 1 ? size / 2 : k == 2 ? 2 : 9;   // payload end, middle, magic, key
        flip_byte(p, at);
        CHECK(miss("item", key), "flipping byte %ld of %ld still hit", at, size);
        CHECK(cret_cache_store("item", key, parts, sizes, 3) && load_eq("item", key, data, sizeof data), "rebuild after corrupt byte %ld failed", at);
    }
    // Truncated and extended files.
    FILE* f = fopen(p, "rb"); uint8_t* whole = malloc(size); size_t got = f ? fread(whole, 1, size, f) : 0; if (f) fclose(f);
    CHECK(got == (size_t)size, "could not read the cache file back");
    long cuts[4] = {0, 3, 30, size - 1};
    for (int k = 0; k < 4; k++) { write_file(p, whole, cuts[k]); CHECK(miss("item", key), "a file truncated to %ld bytes hit", cuts[k]); }
    write_file(p, whole, size); CHECK(load_eq("item", key, data, sizeof data), "the restored file missed");
    { FILE* g = fopen(p, "ab"); if (g) { fputc(0, g); fclose(g); } }
    CHECK(miss("item", key), "a file with a trailing byte hit");
    free(whole);
    // Deleted: a miss, and the next store rebuilds it.
    remove(p);
    CHECK(miss("item", key), "a deleted item hit");
    CHECK(cret_cache_store("item", key, (const void* const*)&parts[0], &sizes[0], 1) && load_eq("item", key, data, 1000), "single-part rebuild failed");
    // Empty payloads are items too.
    CHECK(cret_cache_store("empty", key, NULL, NULL, 0) && load_eq("empty", key, NULL, 0), "empty item");
    // CRET_CACHE=0: never read (nor write).
    setenv("CRET_CACHE", "0", 1);
    CHECK(!cret_cache_enabled() && miss("item", key), "CRET_CACHE=0 still read the cache");
    CHECK(!cret_cache_store("off", key, (const void* const*)&parts[0], &sizes[0], 1), "CRET_CACHE=0 still wrote");
    unsetenv("CRET_CACHE");
    char po[1024]; path_of("off", po, sizeof po);
    CHECK(file_size(po) < 0 && cret_cache_enabled(), "CRET_CACHE=0 left %s", po);
    CHECK(tmp_files() == 0, "%d temporary files left in %s", tmp_files(), g_dir);
}

static void test_keys(void) {
    char a[600], b[600], c[600];
    snprintf(a, sizeof a, "%s/in_a.txt", g_dir); snprintf(b, sizeof b, "%s/in_b.txt", g_dir); snprintf(c, sizeof c, "%s/in_missing.txt", g_dir);
    remove(c);
    write_file(a, "hello", 5); write_file(b, "world", 5);
    uint64_t ha = cret_hash_file(CRET_HASH_SEED, a), hb = cret_hash_file(CRET_HASH_SEED, b), hm = cret_hash_file(CRET_HASH_SEED, c);
    CHECK(ha != hb && ha != hm && hb != hm, "file hashes collide");
    CHECK(cret_hash_file(CRET_HASH_SEED, a) == ha, "file hash not deterministic");
    const char* ab[2] = {a, b}; const char* ba[2] = {b, a}; const char* am[2] = {a, c};
    uint64_t k_ab = cret_hash_files(CRET_HASH_SEED, ab, 2);
    CHECK(k_ab == cret_hash_files(CRET_HASH_SEED, ab, 2), "cret_hash_files not deterministic");
    CHECK(k_ab != cret_hash_files(CRET_HASH_SEED, ba, 2), "cret_hash_files ignores order");
    uint64_t k_am = cret_hash_files(CRET_HASH_SEED, am, 2);
    write_file(b, "worle", 5);
    CHECK(cret_hash_files(CRET_HASH_SEED, ab, 2) != k_ab, "changing one input byte kept the key");
    write_file(b, "world", 5);
    CHECK(cret_hash_files(CRET_HASH_SEED, ab, 2) == k_ab, "restoring the input did not restore the key");
    write_file(c, "", 0);
    CHECK(cret_hash_files(CRET_HASH_SEED, am, 2) != k_am, "a file appearing (even empty) kept the key");
    remove(c);
    CHECK(cret_hash_files(CRET_HASH_SEED, am, 2) == k_am, "a missing file's key is not stable");
    // Many files (the threaded path) match one at a time... and any reordering changes the result.
    const char* many[40]; for (int i = 0; i < 40; i++) many[i] = i % 2 ? a : b;
    uint64_t k1 = cret_hash_files(CRET_HASH_SEED, many, 40), k2 = cret_hash_files(CRET_HASH_SEED, many, 40);
    CHECK(k1 == k2, "threaded file hashes not deterministic");
    many[0] = a; many[1] = b;
    CHECK(cret_hash_files(CRET_HASH_SEED, many, 40) != k1, "threaded file hashes ignore order");
    CHECK(cret_hash_u64(CRET_HASH_SEED, 1) != cret_hash_u64(CRET_HASH_SEED, 2) && cret_hash_str(CRET_HASH_SEED, "a") != cret_hash_str(CRET_HASH_SEED, "b"), "param hashes collide");
    uint8_t big[1001]; memset(big, 7, sizeof big);
    uint64_t h0 = cret_hash(CRET_HASH_SEED, big, sizeof big); big[1000] = 8;   // a change in the tail (not a whole word)
    CHECK(cret_hash(CRET_HASH_SEED, big, sizeof big) != h0, "the last byte does not reach the hash");
    CHECK(cret_hash(CRET_HASH_SEED, big, 1000) != cret_hash(CRET_HASH_SEED, big, 1001), "the length does not reach the hash");
    remove(a); remove(b);
}

static void count_one(int i, void* ctx) { __atomic_fetch_add(&((int*)ctx)[i], 1, __ATOMIC_RELAXED); }
static void test_reader_and_threads(void) {
    uint8_t buf[10] = {1, 0, 0, 0, 2, 0, 0, 0, 3, 4};
    CretReader r = {buf, sizeof buf, 0, false};
    CHECK(cret_read_u32(&r) == 1 && cret_read_u32(&r) == 2 && !r.bad, "reader u32");
    CHECK(cret_read(&r, 4) == NULL && r.bad, "a short read did not fail");
    CHECK(cret_read(&r, 1) == NULL && r.bad, "a failed reader recovered");
    CretReader z = {buf, sizeof buf, 0, false};
    CHECK(cret_read(&z, (size_t)-1) == NULL && z.bad, "a huge read did not fail");
    int counts[1000] = {0};
    for (int threads = 0; threads <= 9; threads += 3) {
        memset(counts, 0, sizeof counts);
        cret_parallel_for(1000, threads, count_one, counts);
        int bad = 0; for (int i = 0; i < 1000; i++) bad += counts[i] != 1;
        CHECK(bad == 0, "cret_parallel_for with %d threads: %d indices not run exactly once", threads, bad);
    }
    cret_parallel_for(0, 4, count_one, counts);   // nothing to do
}

int main(void) {
    double t0 = t_now();
    snprintf(g_dir, sizeof g_dir, "build/cretaceous/test/cache_%d", (int)getpid());
    setenv("CRET_CACHE_DIR", g_dir, 1); unsetenv("CRET_CACHE"); unsetenv("CRET_PROFILE_LOAD");
    test_store_load();
    test_keys();
    test_reader_and_threads();
    DIR* d = opendir(g_dir); struct dirent* e;   // clean up
    if (d) { while ((e = readdir(d))) if (e->d_name[0] != '.') { char p[1100]; snprintf(p, sizeof p, "%s/%s", g_dir, e->d_name); remove(p); } closedir(d); }
    rmdir(g_dir);
    return t_finish("test_cache", t0);
}
