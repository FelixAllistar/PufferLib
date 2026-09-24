#include "bridge.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static uint32_t rng(uint32_t *x) {
    *x ^= *x << 13; *x ^= *x >> 17; *x ^= *x << 5; return *x;
}

/* Independent small specification, used ONLY for verification. */
static void reference(uint32_t *s, unsigned action) {
    if (s[9]) return;
    unsigned task=s[1], target=s[2];
    if ((task==0 && action==5) || (task==2 && action>=2 && action<=4)) action=0;
    if (action==11) s[5]=task==2 ? (s[5]==1 ? 5 : 1) : s[5]%(task==0 ? 4 : 5)+1;
    if (action==12) {
        if (task==0 && s[5]) action=s[5];
        else if ((task==1 && s[5]==5) || (task==2 && s[5])) {
            s[9]=1; s[10]=(task==1 ? s[4]==target : s[7]==2 && s[6]==target) ? 1 : 2;
        }
    }
    if (action>=1 && action<=5) {
        unsigned node=action-1, label=(node+s[3])%4;
        s[5]=node+1;
        if (task==0) { s[9]=1; s[10]=label==target ? 1 : 2; }
        else if (node==4) { s[9]=1; s[10]=(task==1 ? s[4]==target : s[7]==2 && s[6]==target) ? 1 : 2; }
        else if (task==1) s[4]^=1u<<label;
    }
    if (task==2 && s[5]==1) {
        if (action>=6 && action<=9 && s[7]<2) { s[6]+=(action-6)<<(2*s[7]); s[7]++; }
        if (action==10 && s[7]) { s[7]--; s[6]=s[7] ? s[6]%4 : 0; }
    }
    s[8]++;
    if (s[8]>=16 && !s[9]) { s[9]=1; s[10]=2; }
}

static void *check(void *arg) {
    uint32_t seed=(uint32_t)(uintptr_t)arg;
    uint32_t words[WEBNAV_BATCH*WEBNAV_WORDS]={0};
    uint32_t expected[WEBNAV_BATCH][11];
    for (int episode=0; episode<40; episode++) {
        for (int i=0;i<WEBNAV_BATCH;i++) { words[i*256]=rng(&seed); words[i*256+14]=1; }
        webnav_batch(words);
        for (int i=0;i<WEBNAV_BATCH;i++) memcpy(expected[i],words+i*256,sizeof expected[i]);
        for (int step=0;step<20;step++) {
            for (int i=0;i<WEBNAV_BATCH;i++) {
                unsigned action=rng(&seed)%16;
                words[i*256+13]=action; words[i*256+14]=0;
                reference(expected[i],action);
            }
            webnav_batch(words);
            for (int i=0;i<WEBNAV_BATCH;i++) {
                for (int j=0;j<11;j++) if (words[i*256+j]!=expected[i][j]) {
                    fprintf(stderr,"Mismatch episode=%d step=%d lane=%d field=%d task=%u action=%u actual=%u expected=%u\n",episode,step,i,j,words[i*256+1],words[i*256+13],words[i*256+j],expected[i][j]); abort();
                }
                /* No node name contains the private target marker. */
                for(int j=32;j<160;j++) assert(words[i*256+j]<256);
                for(int j=160;j<173;j++) assert(words[i*256+j]<=1);
            }
        }
    }
    return NULL;
}

static double now(void) { struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec*1e-9; }
int main(int argc,char **argv) {
    if(argc>1 && !strcmp(argv[1],"bench")) {
        uint32_t words[8192]={0};
        for(int i=0;i<32;i++){words[i*256]=i+123;words[i*256+14]=1;}
        webnav_batch(words);
        double start=now();long steps=0;unsigned checksum=0;
        while(now()-start<3) {
            for(int i=0;i<32;i++) {
                uint32_t *r=words+i*256;
                r[14]=r[9] ? 1 : 0;
                if(r[9])r[0]++;
                r[13]=(steps/32+i)%13;
            }
            webnav_batch(words);steps+=32;checksum+=words[32];
        }
        printf("{\"backend\":\"stock-bend-cpu\",\"batch\":32,\"steps\":%ld,\"seconds\":%.6f,\"steps_per_second\":%.0f,\"checksum\":%u}\n",steps,now()-start,steps/(now()-start),checksum);
        return 0;
    }
    pthread_t threads[4];
    for(uintptr_t i=0;i<4;i++)assert(!pthread_create(&threads[i],NULL,check,(void*)(i+17)));
    for(int i=0;i<4;i++)assert(!pthread_join(threads[i],NULL));
    puts("PASS: 102400 state transitions; repeated calls and 4 concurrent callers");
}
