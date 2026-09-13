#define _POSIX_C_SOURCE 200809L
#include "../pokemon.h"
#include "../policy_view.h"
int main(int argc,char** argv) {
    assert(argc==3);
    PKPolicy policies[3]={0};
    pk_load_policy(&policies[0],argv[1],0,0,NULL);
    pk_load_policy(&policies[1],argv[2],0,0,NULL);
    pk_load_policy(&policies[2],argv[2],0,0,NULL);
    PKGame g={0}; g.rng=856; g.draft=1; g.max_updates=512;
    uint64_t rng=233;
    for(int game=0;game<12;game++) {
        pk_game_reset(&g);
        for(int i=0;i<3;i++) pk_reset_policy(&policies[i],game+89);
        while(!g.result) {
            uint8_t aux[PK_OBS]; memcpy(aux,g.obs[0],PK_OBS); aux[476]=1;
            int a=pk_policy_action(&policies[0],g.obs[0],g.masks[0],0);
            int b=pk_policy_action(&policies[1],g.obs[0],g.masks[0],0);
            int c=pk_policy_action(&policies[2],aux,g.masks[0],0);
            assert(a==b && b==c);
            size_t bytes=(PK_ACTIONS+1)*sizeof(float);
            assert(!memcmp(policies[0].net->decoder->output,policies[1].net->decoder->output,bytes));
            assert(!memcmp(policies[1].net->decoder->output,policies[2].net->decoder->output,bytes));
            pk_game_step(&g,pk_random_action(g.masks[0],&rng),pk_random_action(g.masks[1],&rng));
        }
    }
    for(int i=0;i<3;i++) { free_puffernet(policies[i].net); free(policies[i].weights); puf_ini_free(&policies[i].ini); }
    puts("Flag migration parity passed: original root, migrated root, migrated reset flag have identical logits/actions/values through recurrent trajectories.");
}
