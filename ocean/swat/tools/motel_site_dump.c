// Dumps every authoritative Briar Court object (index, centre, half extents,
// yaw, part, material, flags) plus mission staging/extraction and actor
// spawns as CSV, for site measurement and top-down plan rendering
// (tools/motel_site_plan.py). Simulation only; no window.
//   motel_site_dump > site.csv
#include "sim.h"
#include "motel.h"
#include "mission.h"
#include <stdio.h>
#include <stdlib.h>
int main(void) {
    SwatSim* sim=calloc(1,sizeof(*sim));
    SwatConfig config=swat_default_config();config.mission=SWAT_MOTEL;config.layout_seed=42;
    config.tactical_rules=true;config.squad_bots=3;config.max_ticks=100000;
    swat_sim_init(sim,config,42);
    const SwatWorld* w=&sim->world;
    printf("kind,index,cx,cy,cz,hx,hy,hz,yaw,part,material,active,door,wall_group\n");
    for(int i=0;i<w->count;i++) {
        const SwatObject* o=&w->objects[i];
        printf("obj,%d,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.4f,%d,%d,%d,%d,%d\n",i,o->center.x,o->center.y,o->center.z,
            o->half.x,o->half.y,o->half.z,o->yaw,o->part,o->material,o->active,o->door,o->wall_group);
    }
    const SwatMissionDef* m=swat_mission(SWAT_MOTEL);
    printf("staging,0,%.3f,%.3f,%.3f,0,0,0,0,0,0,1,0,0\n",m->staging.x,m->staging.y,m->staging.z);
    printf("extraction,0,%.3f,%.3f,%.3f,0,0,0,0,0,0,1,0,0\n",m->extraction.x,m->extraction.y,m->extraction.z);
    for(int a=0;a<sim->actor_count;a++) {
        b3Pos p=swat_body_feet_position(&sim->actors[a].controller.body);
        printf("actor,%d,%.3f,%.3f,%.3f,0,0,0,0,%d,0,1,0,0\n",a,p.x,p.y,p.z,(int)sim->actors[a].role);
    }
    for(int r=0;r<w->room_count;r++)printf("room,%d,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,0,0,0,1,0,0\n",r,w->rooms[r].center.x,w->rooms[r].center.y,w->rooms[r].center.z,w->rooms[r].half.x,w->rooms[r].half.y,w->rooms[r].half.z);
    fprintf(stderr,"objects=%d SWAT_MOTEL_OBJECTS=%d actors=%d\n",w->count,SWAT_MOTEL_OBJECTS,sim->actor_count);
    return 0;
}
