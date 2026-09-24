Term host_write_run(Env e, Term *f, IoWork *w) {
    (void)w;
    for (unsigned i = 0; i < 8192; ++i) {
        webnav_words[i] = (uint32_t)blk_read(e.mem, false, term_loc(f[0]), i);
    }
    term_drop(e, f[0]);
    return term_pak(CID_UNIT, 0);
}
static void __attribute__((constructor)) host_write_use(void) {
    io_eff(CID_HOST_WRITE, host_write_run, 0);
}
