#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <sys/prctl.h>
int wr_fixture(uint32_t *);
typedef struct {uint32_t id,revision;int32_t value;} Row;
static int ascending(const void *a,const void *b) {
    const Row *x=a,*y=b;
    if (x->value!=y->value) return x->value<y->value?-1:1;
    return x->id<y->id?-1:x->id>y->id;
}
static int descending(const void *a,const void *b) {
    const Row *x=a,*y=b;
    if (x->value!=y->value) return x->value>y->value?-1:1;
    return x->id<y->id?-1:x->id>y->id;
}
int main(void) {
    prctl(PR_SET_DUMPABLE,0,0,0,0);
    unsigned checks=0;
    for (unsigned count=0;count<=16;count++) for (unsigned pred=0;pred<3;pred++)
    for (unsigned desc=0;desc<2;desc++) for (unsigned offset=0;offset<18;offset+=3) {
        uint32_t r[256]={1,count,pred,0,desc,offset,4};Row expected[16];unsigned n=0;
        for (unsigned i=0;i<count;i++) {
            int32_t v=(int32_t)((i*11+count)%7)-3;
            r[32+3*i]=30-i;r[33+3*i]=i;r[34+3*i]=(uint32_t)v;
            if (pred==0 || (pred==1 && v==0) || (pred==2 && v<0)) expected[n++]=(Row){30-i,i,v};
        }
        uint32_t before[256];memcpy(before,r,sizeof r);
        qsort(expected,n,sizeof *expected,desc?descending:ascending);
        assert(!wr_fixture(r));assert(!memcmp(r+32,before+32,48*sizeof *r));
        unsigned length=n>offset?n-offset:0;if (length>4) length=4;
        assert(r[11]==length);
        for (unsigned i=0;i<length;i++) {
            assert(r[128+3*i]==expected[offset+i].id && r[129+3*i]==expected[offset+i].revision);
            assert(r[130+3*i]==(uint32_t)expected[offset+i].value);
        }
        for (unsigned i=3*length;i<48;i++) assert(r[128+i]==0);
        checks++;
    }
    uint32_t r[256]={2,2,0,0,0,0,0,7,2,99};
    r[32]=7;r[33]=2;r[34]=10;r[35]=8;r[36]=3;r[37]=20;
    assert(!wr_fixture(r) && r[10]==0 && r[33]==3 && r[34]==99 && r[37]==20);
    uint32_t before[48];memcpy(before,r+32,sizeof before);
    assert(!wr_fixture(r) && r[10]==2 && !memcmp(before,r+32,sizeof before));
    r[7]=9;assert(!wr_fixture(r) && r[10]==1 && !memcmp(before,r+32,sizeof before));
    r[7]=7;r[8]=UINT32_MAX;r[33]=UINT32_MAX;
    assert(!wr_fixture(r) && r[10]==3 && r[33]==UINT32_MAX && r[34]==99);
    printf("PASS: records primitive, %u independent query cases plus revision conflict, missing identity and overflow\n",checks);
    return 0;
}
