// Dump the authoritative motel instances and wall skin sections as JSON for
// the exterior architecture overlay builder (tools/blender/motel_exterior_arch.py).
// Read-only: builds the ordinary motel mission world (layout seed 42) and
// prints object boxes; nothing is changed.
//   motel_exterior_sections > assets/environment/motel_exterior/sections.json
#include "sim.h"
#include "motel.h"
#include <stdio.h>
#include <stdlib.h>
int main(void) {
    SwatSim* sim=calloc(1,sizeof(*sim));
    SwatConfig config=swat_default_config();config.mission=SWAT_MOTEL;config.layout_seed=42;
    config.tactical_rules=true;config.squad_bots=3;config.hostile_fire=false;config.max_ticks=100000;
    swat_sim_init(sim,config,42);
    const SwatWorld* w=&sim->world;
    printf("{\n \"schema\": \"swat_motel_sections_v1\",\n \"instances\": [\n");
    for(int i=1;i<=SWAT_MOTEL_INSTANCES && i<w->count;i++) {
        const SwatMotelInstance* p=swat_motel_instance(i-1);const SwatMotelAsset* a=swat_motel_asset(p->asset);
        const SwatObject* o=&w->objects[i];
        printf("  {\"tag\": %d, \"asset\": \"%s\", \"origin\": [%.4f, %.4f, %.4f], \"scale\": [%.4f, %.4f, %.4f], \"yaw\": %.6f, \"wall_group\": %d, \"active\": %d}%s\n",
            i,a?a->file:"",p->origin.x,p->origin.y,p->origin.z,p->scale.x,p->scale.y,p->scale.z,p->yaw,o->wall_group,o->active,i<SWAT_MOTEL_INSTANCES?",":"");
    }
    printf(" ],\n \"sections\": [\n");
    int n=0;
    for(int i=SWAT_MOTEL_INSTANCES+1;i<w->count;i++) {
        const SwatObject* o=&w->objects[i];if(o->part!=SWAT_PART_SKIN || !o->wall_group)continue;
        int owner=0;for(int k=1;k<=SWAT_MOTEL_INSTANCES && k<w->count;k++)if(w->objects[k].wall_group==o->wall_group)owner=k;
        printf("%s  {\"index\": %d, \"owner\": %d, \"group\": %d, \"center\": [%.4f, %.4f, %.4f], \"half\": [%.4f, %.4f, %.4f], \"yaw\": %.6f, \"corners\": [[%.4f, %.4f], [%.4f, %.4f], [%.4f, %.4f], [%.4f, %.4f]]}",
            n++?",\n":"",i,owner,o->wall_group,o->center.x,o->center.y,o->center.z,o->half.x,o->half.y,o->half.z,o->yaw,
            o->corners[0][0],o->corners[0][1],o->corners[1][0],o->corners[1][1],o->corners[2][0],o->corners[2][1],o->corners[3][0],o->corners[3][1]);
    }
    printf("\n ]\n}\n");
    swat_sim_close(sim);free(sim);return 0;
}
