#include "replay.h"
#include <stdlib.h>

int main(int argc,char** argv) {
    if(argc!=2) { fprintf(stderr,"usage: swat_replay_tool INPUT.sgrp\n"); return 2; }
    SwatReplay replay;
    if(!swat_replay_open(&replay,argv[1])) { fprintf(stderr,"Invalid or incompatible replay\n"); return 1; }
    SwatSim* sim=calloc(1,sizeof(*sim)); if(!sim) { swat_replay_close(&replay); return 1; }
    swat_sim_init(sim,replay.config,replay.seed);
    SwatInput input; uint32_t expected; int status; bool matched=true;
    while((status=swat_replay_next(&replay,&input,&expected))==1) {
        swat_sim_step(sim,&input);
        if(swat_replay_digest(sim)!=expected) { fprintf(stderr,"Replay divergence at tick %d\n",sim->tick); matched=false; break; }
    }
    if(status<0) fprintf(stderr,"Truncated or malformed replay\n");
    printf("Replay: %u/%u frames, %s\n",replay.frame,replay.count,matched && status==0 ? "exact match" : "failed");
    swat_sim_close(sim); free(sim); bool closed=swat_replay_close(&replay);
    return matched && status==0 && closed ? 0 : 1;
}
