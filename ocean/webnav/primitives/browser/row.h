#ifndef WEBNAV_PRIMITIVE_BROWSER_ROW_H
#define WEBNAV_PRIMITIVE_BROWSER_ROW_H
#include "browser.h"

/* Shared private-wire validation for a single browser embedded in a host row.
 * The caller guarantees at least 32 accessible words. No state is modified. */
static inline int wb_row_validate(const uint32_t *r) {
    if (!r || r[11]!=1 || r[1]<1 || r[1]>32 || r[0]>=r[1] || r[4]>WB_FAILED ||
        r[6]>8 || r[7]>8 || r[6]+r[7]>8 || r[12]!=0) return -1;
    if (r[4]==WB_LOADING ? r[5]>r[2] : r[5]!=0) return -1;
    for (unsigned i=0;i<r[6];i++) if (r[16+i]>=r[1]) return -1;
    for (unsigned i=0;i<r[7];i++) if (r[24+i]>=r[1]) return -1;
    return 0;
}
#endif
