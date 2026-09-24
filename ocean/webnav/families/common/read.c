/* Synchronous transport only. WF_TOTAL_WORDS and WF_ARRAY_DEPTH are supplied
 * by the family bridge before including generated C. */
static uint32_t *wf_words;
Term host_read_run(Env e, Term *f, IoWork *w) {
    (void)f;(void)w;
    Term zero=0;
    Term a=blk_new(e,false,WF_ARRAY_DEPTH,0,1,&zero);
    for(unsigned i=0;i<WF_TOTAL_WORDS;i++)blk_write(e.mem,false,term_loc(a),i,wf_words[i]);
    return a;
}
static void __attribute__((constructor)) host_read_use(void) {io_eff(CID_HOST_READ,host_read_run,0);}
