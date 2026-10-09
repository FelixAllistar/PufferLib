#include <stdint.h>
void primitive_transaction_batch(uint32_t *);
__attribute__((visibility("default"))) int wt_fixture(uint32_t *r) {
    if (!r || r[0]<1 || r[0]>4 || r[1]>16 || r[12]>3 || r[16]>3 || r[20]>1 ||
        (int32_t)r[18]>(int32_t)r[19]) return -1;
    for (unsigned i=0;i<r[1];i++) {
        if (!r[32+3*i]) return -1;
        for (unsigned j=0;j<i;j++) if (r[32+3*i]==r[32+3*j]) return -1;
    }
    primitive_transaction_batch(r);return 0;
}
