/* Synchronous transport only. Task behavior lives in Game.bend. */
static uint32_t *webnav_words;
Term host_read_run(Env e, Term *f, IoWork *w) {
    (void)f; (void)w;
    Term zero = 0;
    Term a = blk_new(e, false, 13, 0, 1, &zero);
    for (unsigned i = 0; i < 8192; ++i) {
        blk_write(e.mem, false, term_loc(a), i, webnav_words[i]);
    }
    return a;
}
static void __attribute__((constructor)) host_read_use(void) {
    io_eff(CID_HOST_READ, host_read_run, 0);
}
