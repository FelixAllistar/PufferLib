Term host_write_run(Env e, Term *f, IoWork *w) {
    (void)w;
    for (unsigned i = 0; i < PG9_MUTABLE_WORDS; i++)
        pg9_words[i] = (uint32_t)blk_read(e.mem, false, term_loc(f[0]), i);
    pg9_array = f[0];
    return term_pak(CID(Unit), 0);
}
static void __attribute__((constructor)) host_write_use(void) {
    io_eff(CID(Host.write), host_write_run, 0);
}
