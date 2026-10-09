/* Numeric fixture transport only. App bindings call the Bend models directly. */
#include <stdint.h>
void primitive_records_batch(uint32_t *);
__attribute__((visibility("default"))) int wr_fixture(uint32_t *r) {
    if (!r || r[0]<1 || r[0]>2 || r[1]>16 || r[2]>2 || r[4]>1 || r[5]>16 || r[6]>16) return -1;
    for (unsigned i=0;i<r[1];i++) {
        if (!r[32+3*i]) return -1;
        for (unsigned j=0;j<i;j++) if (r[32+3*i]==r[32+3*j]) return -1;
    }
    primitive_records_batch(r);return 0;
}
