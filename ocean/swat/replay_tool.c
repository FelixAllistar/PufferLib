#include "replay.h"
#include <stdlib.h>
#include <string.h>

int main(int argc,char** argv) {
    const char* input=argc==2 ? argv[1] : NULL;
    if(argc==4 && !strcmp(argv[1],"--weapon-data")) {
        char error[512];
        if(!swat_weapons_load(argv[2],error,sizeof(error))) { fprintf(stderr,"Invalid weapon data: %s\n",error); return 2; }
        input=argv[3];
    }
    if(!input) { fprintf(stderr,"usage: swat_replay_tool [--weapon-data FILE.ini] INPUT.sgrp\n"); return 2; }
    SwatReplay replay;
    if(!swat_replay_open(&replay,input)) {
        if(replay.weapon_mismatch) fprintf(stderr,"Replay weapon table %08x does not match the loaded table %08x; pass the recording's --weapon-data file\n",
            replay.weapon_table,swat_weapons_table_id());
        else fprintf(stderr,"Invalid or incompatible replay\n");
        return 1;
    }
    SwatSim* sim=calloc(1,sizeof(*sim)); if(!sim) { swat_replay_close(&replay); return 1; }
    swat_sim_init(sim,replay.config,replay.seed);
    SwatInput input_frame; uint32_t expected; int status; bool matched=true;
    while((status=swat_replay_next(&replay,&input_frame,&expected))==1) {
        swat_sim_step(sim,&input_frame);
        if(swat_replay_digest(sim)!=expected) { fprintf(stderr,"Replay divergence at tick %d\n",sim->tick); matched=false; break; }
    }
    if(status<0) fprintf(stderr,"Truncated or malformed replay\n");
    printf("Replay: %u/%u frames, %s\n",replay.frame,replay.count,matched && status==0 ? "exact match" : "failed");
    swat_sim_close(sim); free(sim); bool closed=swat_replay_close(&replay);
    return matched && status==0 && closed ? 0 : 1;
}
