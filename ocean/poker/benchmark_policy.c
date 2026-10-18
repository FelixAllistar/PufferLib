#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <time.h>
#include "native.c"

static double seconds(clockid_t clock) {
    struct timespec t; clock_gettime(clock, &t);
    return t.tv_sec + t.tv_nsec * 1e-9;
}

// Fixed public positions; CPU time excludes time lost to other host processes.
// Integrated match throughput is measured separately by benchmark.c.
int main(void) {
    Poker* g = calloc(1, sizeof(Poker));
    uint8_t deck[2 * POKER_PLAYERS + 5];
    const uint8_t public_cards[7] = {50,46,42,22,0,5,11}; // Ah Kh / Qh 7h 2c 3d 4s
    uint64_t used = 0;
    for (int i = 0; i < 7; i++) used |= UINT64_C(1) << public_cards[i];
    int next = 0;
    for (int p = 0; p < POKER_PLAYERS; p++) for (int h = 0; h < 2; h++) {
        if (p == 1) deck[2*p+h] = public_cards[h];
        else {
            while (used & (UINT64_C(1) << next)) next++;
            deck[2*p+h] = next; used |= UINT64_C(1) << next;
        }
    }
    for (int i = 0; i < 5; i++) deck[2*POKER_PLAYERS+i] = public_cards[i+2];
    uint32_t stacks[POKER_PLAYERS];
    for (int p=0;p<POKER_PLAYERS;p++) stacks[p]=20000;
    poker_reset(g,0,0,stacks,deck);
    int styles[] = {1,4,7};
    int32_t (*volatile decide)(Poker*,int,uint64_t*,int) = poker_bot;
    volatile int32_t sink = 0;
    printf("[\n");
    int first = 1;
    for (int street = 1; street <= 3; street++) {
        while (g->street[0]<street) poker_step(g,0,0);
        assert(g->actor[0]==1);
        for (unsigned s=0;s<sizeof(styles)/sizeof(styles[0]);s++) {
            int style=styles[s];
            if (!poker_bot_supported(style)) continue;
            uint64_t rng=72135;
            for (int i=0;i<100;i++) sink=decide(g,0,&rng,style);
            double wall=seconds(CLOCK_MONOTONIC),cpu=seconds(CLOCK_PROCESS_CPUTIME_ID);
            const int calls=20000;
            for (int i=0;i<calls;i++) sink=decide(g,0,&rng,style);
            cpu=seconds(CLOCK_PROCESS_CPUTIME_ID)-cpu;wall=seconds(CLOCK_MONOTONIC)-wall;
            printf("%s{\"players\":%d,\"street\":%d,\"style\":%d,\"calls\":%d,"
                "\"cpu_us_per_decision\":%.4f,\"wall_us_per_decision\":%.4f}",
                first?"":",\n",POKER_PLAYERS,street,style,calls,1e6*cpu/calls,1e6*wall/calls);
            first=0;
        }
    }
    printf("\n]\n");free(g);(void)sink;
}
