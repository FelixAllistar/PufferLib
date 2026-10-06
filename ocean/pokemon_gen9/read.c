/* Synchronous flat-word transport; domain behavior remains in Bend. */
#include "data/layout.h"
static uint32_t *pg9_words;
static Term pg9_array;
Term host_read_run(Env e, Term *f, IoWork *w) {
    (void)f; (void)w;
    Term zero = 0;
    Term a = pg9_array ? pg9_array : blk_new(e, false, PG9_ARRAY_BITS, 0, 1, &zero);
    pg9_array = 0;
    for (unsigned i = 0; i < PG9_MUTABLE_WORDS; i++)
        blk_write(e.mem, false, term_loc(a), i, pg9_words[i]);
    /* Explicit catalog upload. Ordinary calls transfer the mutable prefix. */
    if (pg9_words[2] == 1)
        for (unsigned i = PG9_MUTABLE_WORDS; i < PG9_WORD_COUNT; i++)
            blk_write(e.mem, false, term_loc(a), i, pg9_words[i]);
    blk_write(e.mem, false, term_loc(a), 2, 0);
    return a;
}
static void __attribute__((constructor)) host_read_use(void) {
    io_eff(CID(Host.read), host_read_run, 0);
}
