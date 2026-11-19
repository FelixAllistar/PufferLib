// Interior props v2 route check (swat/art-props-interior): replays recorded
// motel scenarios (.sgrp from test_motel_completion) and prints every present
// actor's feet position each tick, so tools/props_interior_clearance.py can
// verify that no actor route crosses a floor-prop footprint of the
// presentation-only layer in motel_props_interior_v2_data.h.
//   props_interior_clearance INPUT.sgrp... > tracks.txt
#include "replay.h"
#include <stdlib.h>
#include <string.h>

int main(int argc,char** argv) {
    if(argc<2) { fprintf(stderr,"usage: %s INPUT.sgrp...\n",argv[0]); return 2; }
    int failures=0;
    for(int f=1;f<argc;f++) {
        SwatReplay replay;
        if(!swat_replay_open(&replay,argv[f])) { fprintf(stderr,"cannot open %s\n",argv[f]); failures++; continue; }
        SwatSim* sim=calloc(1,sizeof(*sim)); if(!sim) return 1;
        swat_sim_init(sim,replay.config,replay.seed);
        SwatInput in; uint32_t expected; int status;
        while((status=swat_replay_next(&replay,&in,&expected))==1) {
            swat_sim_step(sim,&in);
            for(int i=0;i<sim->actor_count;i++) {
                const SwatActor* a=&sim->actors[i]; if(!a->present) continue;
                b3Pos p=swat_body_feet_position(&a->controller.body);
                printf("%d %d %d %.3f %.3f %.3f\n",f,sim->tick,i,(float)p.x,(float)p.y,(float)p.z);
            }
        }
        if(status<0) { fprintf(stderr,"truncated %s\n",argv[f]); failures++; }
        swat_sim_close(sim); free(sim); swat_replay_close(&replay);
    }
    return failures?1:0;
}
