#include "effects_transport.h"
Term host_write_effects_run(Env e, Term *f, IoWork *w) {
    (void)e; (void)w;
    if (pg9_effect_image) abort();
    pg9_effect_image = f[0];
    return term_pak(CID(Unit), 0);
}
static void __attribute__((constructor)) host_write_effects_use(void) {
    io_eff(CID(Host.write_effects), host_write_effects_run, 0);
}
