#include "effects_transport.h"
Term host_read_effects_run(Env e, Term *f, IoWork *w) {
    (void)e; (void)f; (void)w;
    Term image = pg9_effect_image;
    pg9_effect_image = 0;
    return image ? image : term_pak(CID(EmptyEffectImage), 0);
}
static void __attribute__((constructor)) host_read_effects_use(void) {
    io_eff(CID(Host.read_effects), host_read_effects_run, 0);
}
