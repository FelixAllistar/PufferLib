#define _POSIX_C_SOURCE 200809L
#include "../pokemon.h"
#include "../policy_view.h"
int main(void) {
    // ABI 3 learns the flag directly; legacy zero-column migration is invalid.
    PKGame g={0}; g.rng=856; g.draft=1; g.max_updates=512;
    uint64_t rng=233;
    for(int game=0;game<12;game++) {
        pk_game_reset(&g);
        while(!g.result) {
            float root[PK_OBS],aux[PK_OBS];
            for(int i=0;i<PK_OBS;i++) root[i]=aux[i]=g.obs[0][i];
            root[476]=0; aux[476]=1;
            for(int f=0;f<PK_GLOBAL;f++) {
                float a=pk_global_feature(root,f),b=pk_global_feature(aux,f);
                if(f==56) assert(a==0 && b==1);
                else assert(a==b);
            }
            pk_game_step(&g,pk_random_action(g.masks[0],&rng),pk_random_action(g.masks[1],&rng));
        }
    }
    puts("Semantic reset flag: explicit public feature independent of other globals PASS");
}
