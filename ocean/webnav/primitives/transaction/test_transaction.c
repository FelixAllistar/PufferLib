#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <sys/prctl.h>
int wt_fixture(uint32_t *);
static void send(uint32_t *r,unsigned op) {r[0]=op;assert(!wt_fixture(r));}
static void reset(uint32_t *r) {
    memset(r,0,256*sizeof *r);r[1]=2;r[7]=7;r[18]=0;r[19]=100;r[20]=1;
    r[32]=7;r[33]=1;r[34]=3;r[35]=9;r[36]=0;r[37]=5;
}
int main(void) {
    prctl(PR_SET_DUMPABLE,0,0,0,0);
    uint32_t r[256],records[6];unsigned checks=0;
    for (unsigned value=0;value<130;value++) for (unsigned cancel=0;cancel<2;cancel++) {
        reset(r);memcpy(records,r+32,sizeof records);
        send(r,1);assert(r[12]==1 && r[13]==7 && r[14]==1 && r[15]==3);
        r[9]=value;send(r,2);assert(r[15]==value && !memcmp(records,r+32,sizeof records));
        send(r,cancel?4:3);
        if (cancel) {assert(r[12]==3 && !memcmp(records,r+32,sizeof records));send(r,3);assert(!memcmp(records,r+32,sizeof records));}
        else if (value>100) {assert(r[12]==1 && r[16]==3 && !memcmp(records,r+32,sizeof records));}
        else {
            assert(r[12]==2 && r[16]==0 && r[33]==2 && r[34]==value && r[36]==0 && r[37]==5);
            send(r,3);assert(r[33]==2 && r[34]==value);
        }
        checks++;
    }
    reset(r);send(r,1);r[9]=8;send(r,2);
    r[33]=2;r[34]=6;send(r,3);assert(r[12]==1 && r[16]==2 && r[15]==8 && r[34]==6);
    send(r,4);send(r,1);assert(r[15]==6 && r[14]==2 && !r[16]);
    r[20]=0;r[9]=99;send(r,2);assert(r[15]==6 && r[16]==3);
    r[20]=1;r[21]=1;send(r,2);assert(r[15]==6 && r[16]==3);
    r[21]=0;r[9]=101;send(r,2);send(r,3);assert(r[16]==3 && r[34]==6);
    r[9]=9;send(r,2);send(r,3);assert(r[12]==2 && r[33]==3 && r[34]==9);
    reset(r);r[7]=99;send(r,1);assert(r[12]==0 && r[16]==1 && r[34]==3);
    printf("PASS: transaction primitive, %u draft/commit/cancel cases, conflict recovery, validation and readonly fields\n",checks);
    return 0;
}
