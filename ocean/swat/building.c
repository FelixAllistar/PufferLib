// Seeded scenario assembly: initial conditions only. No action sequence,
// trigger volumes, forced breach, or authored encounter choreography.
#include "generation.h"
#include <assert.h>
#include <string.h>

static void wall(SwatLayout* p,b3Pos origin,float yaw,float length,float opening,float width,float sill,bool exterior,int a,int b) {
    assert(p->wall_count<SWAT_LAYOUT_WALLS);
    p->walls[p->wall_count++]=(SwatPlanWall){.origin=origin,.yaw=yaw,.length=length,.opening=opening,.width=width,.sill=sill,.exterior=exterior,.door=width>0 && sill==0,.a=a,.b=b};
}
static void furniture(SwatLayout* p,b3Pos center,b3Vec3 half,SwatMaterial material) {
    assert(p->furniture_count<SWAT_LAYOUT_FURNITURE);
    p->furniture[p->furniture_count++]=(SwatPlanFurniture){center,half,material};
}
static void furnish(SwatLayout* p,const SwatPlanRoom* r,uint32_t* rng) {
    // Keep the room centre, door swing and window approaches usable. Identity
    // controls the solid assembly; small tabletop art uses the existing recipe.
    float x=r->x1-1.05f,z=r->z1-.85f,y=r->y;
    if(r->identity==0) { // living/work area: sofa, back, desk and cupboard
        furniture(p,(b3Pos){x,y+.28f,z},swat_v(.85f,.28f,.5f),SWAT_CARPET);
        furniture(p,(b3Pos){x,y+.64f,z+.40f},swat_v(.85f,.20f,.10f),SWAT_CARPET);
        furniture(p,(b3Pos){r->x0+1.1f,y+.38f,r->z0+.85f},swat_v(.65f,.38f,.42f),SWAT_WOOD);
        furniture(p,(b3Pos){r->x1-.45f,y+.85f,r->z0+.65f},swat_v(.38f,.85f,.50f),SWAT_WOOD);
    } else if(r->identity==1) { // kitchen/utility: low counter, sink and table
        furniture(p,(b3Pos){x,y+.45f,z},swat_v(.85f,.45f,.48f),SWAT_WOOD);
        furniture(p,(b3Pos){x,y+.92f,z},swat_v(.65f,.025f,.38f),SWAT_STEEL);
        furniture(p,(b3Pos){r->x0+1.2f,y+.38f,r->z0+.95f},swat_v(.70f,.38f,.48f),SWAT_WOOD);
    } else { // bedroom/storage: low bed, headboard, nightstand and wardrobe
        furniture(p,(b3Pos){x,y+.22f,r->z0+1.25f},swat_v(.82f,.22f,1.0f),SWAT_WOOD);
        furniture(p,(b3Pos){x,y+.49f,r->z0+1.25f},swat_v(.80f,.05f,.98f),SWAT_CARPET);
        furniture(p,(b3Pos){x,y+.72f,r->z0+.22f},swat_v(.82f,.34f,.06f),SWAT_WOOD);
        furniture(p,(b3Pos){r->x0+.65f,y+.30f,r->z1-.70f},swat_v(.45f,.30f,.38f),SWAT_WOOD);
    }
    if(swat_rand01(rng)>.5f) furniture(p,(b3Pos){r->x1-.5f,y+.7f,r->z1-2.0f},swat_v(.35f,.7f,.30f),SWAT_WOOD);
}
static bool spawn(SwatLayout* p,int actor,int role,int preferred,uint32_t* rng) {
    const int rooms[4]={1,2,4,5};
    for(int pass=0;pass<4;pass++) {
        const SwatPlanRoom* r=&p->rooms[rooms[(preferred+pass)%4]];
        for(int trial=0;trial<24;trial++) {
            b3Pos feet={(r->x0+r->x1)*.5f+(swat_rand01(rng)-.5f)*2,r->y,
                        (r->z0+r->z1)*.5f+(swat_rand01(rng)-.5f)*1.8f};
            bool blocked=false;
            for(int j=0;j<p->spawn_count;j++) if(b3Distance(feet,p->spawns[j].feet)<1.05f) blocked=true;
            for(int j=0;j<p->furniture_count;j++) {
                const SwatPlanFurniture* f=&p->furniture[j]; b3Vec3 d=b3SubPos(feet,f->center);
                if(fabsf(d.x)<f->half.x+.48f && fabsf(d.z)<f->half.z+.48f && fabsf(d.y)<f->half.y+1.8f) blocked=true;
            }
            if(blocked) continue;
            p->spawns[p->spawn_count++]=(SwatPlanSpawn){actor,role,feet,swat_rand01(rng)*2*SWAT_PI-SWAT_PI}; return true;
        }
    }
    return false;
}
bool swat_building_plan(SwatLayout* p,uint32_t seed,int difficulty) {
    if(difficulty<0 || difficulty>2) return false;
    memset(p,0,sizeof(*p)); p->seed=seed; p->difficulty=difficulty;
    uint32_t rng=seed^0xa341316cu; if(!rng) rng=1;
    p->width=10+(int)(swat_rand01(&rng)*3); p->depth=8+(int)(swat_rand01(&rng)*3);
    float x1=4+p->width,z0=-p->depth*.5f,z1=p->depth*.5f;
    float split=(swat_rand01(&rng)-.5f)*.8f,entry=z1-1.1f;
    p->mission=(SwatMissionDef){.name="Generated building scenario",.briefing="Reported armed occupants and civilians. Assess the situation, secure occupants and return to staging.",.staging={0,0,entry},.extraction={-1.5f,0,entry}};
    p->room_count=6;
    for(int floor=0;floor<2;floor++) {
        float y=floor*3.0f; int base=floor*3;
        p->rooms[base]=(SwatPlanRoom){.x0=4,.x1=8,.z0=z0,.z1=z1,.hall=true,.floor=SWAT_TILE,.y=y};
        for(int side=0;side<2;side++) {
            int identity=(floor*2+side+(int)(swat_rand01(&rng)*3))%3;
            p->rooms[base+1+side]=(SwatPlanRoom){.x0=8,.x1=x1,.z0=side ? split : z0,.z1=side ? z1 : split,.floor=identity==1 ? SWAT_TILE : identity==2 ? SWAT_CARPET : SWAT_WOOD,.y=y,.identity=identity};
            const SwatPlanRoom* r=&p->rooms[base+1+side]; float cz=(r->z0+r->z1)*.5f,dz=r->z1-r->z0;
            wall(p,(b3Pos){8,y,cz},0,dz,(swat_rand01(&rng)-.5f)*.35f,1.2f,0,false,base,base+1+side);
            bool rear=floor==0 && side==(int)(seed%2);
            wall(p,(b3Pos){x1,y,cz},SWAT_PI,dz,0,rear ? 1.2f : 1.6f,rear ? 0 : .85f,true,base+1+side,-1);
            furnish(p,r,&rng);
        }
        wall(p,(b3Pos){4,y,0},0,p->depth,floor==0 ? entry : 0,floor==0 ? 1.2f : 1.5f,floor==0 ? 0 : .85f,true,base,-1);
        wall(p,(b3Pos){6,y,z0},-SWAT_PI*.5f,4,0,1.2f,.85f,true,base,-1);
        wall(p,(b3Pos){6,y,z1},SWAT_PI*.5f,4,0,1.2f,.85f,true,base,-1);
        wall(p,(b3Pos){(8+x1)*.5f,y,z0},-SWAT_PI*.5f,x1-8,0,1.6f,.85f,true,base+1,-1);
        wall(p,(b3Pos){(8+x1)*.5f,y,z1},SWAT_PI*.5f,x1-8,0,1.6f,.85f,true,base+2,-1);
        wall(p,(b3Pos){(8+x1)*.5f,y,split},SWAT_PI*.5f,x1-8,(swat_rand01(&rng)-.5f)*.4f,1.2f,0,false,base+1,base+2);
    }
    const int suspects[3]={1,8,11},civilians[3]={2,6,7};
    for(int i=0;i<=difficulty;i++) if(!spawn(p,suspects[i],1,(int)(swat_rand01(&rng)*4),&rng)) return false;
    for(int i=0;i<3;i++) if(!spawn(p,civilians[i],2,(int)(swat_rand01(&rng)*4),&rng)) return false;
    p->object_count=43+p->furniture_count;
    for(int i=0;i<p->wall_count;i++) {
        const SwatPlanWall* w=&p->walls[i];
        p->object_count+=swat_framed_wall_pieces(w->length,2.8f,w->opening,w->width,w->sill,w->door ? 2.1f : 1.25f,w->exterior);
    }
    if(p->object_count>=SWAT_MAX_OBJECTS) return false;
    p->fingerprint=2166136261u;
    const unsigned char* bytes=(const unsigned char*)p->rooms;
    for(size_t i=0;i<sizeof(p->rooms);i++) p->fingerprint=(p->fingerprint^bytes[i])*16777619u;
    p->fingerprint^=seed; p->quality=0; // No learned preference claim for this grammar.
    return true;
}
static void floor_box(SwatWorld* w,float x0,float x1,float z0,float z1,float top,SwatMaterial material,float thick) {
    swat_world_box(w,(b3Pos){(x0+x1)*.5f,top-thick*.5f,(z0+z1)*.5f},swat_v((x1-x0)*.5f,thick*.5f,(z1-z0)*.5f),material,0);
}
void swat_building_build(SwatWorld* w,const SwatLayout* p) {
    float x1=4+p->width,z0=-p->depth*.5f,z1=p->depth*.5f;
    floor_box(w,-8,4,-18,18,0,SWAT_SOIL,1); floor_box(w,x1,x1+8,-18,18,0,SWAT_SOIL,1);
    floor_box(w,4,x1,-18,z0,0,SWAT_SOIL,1); floor_box(w,4,x1,z1,18,0,SWAT_SOIL,1);
    for(int i=0;i<p->room_count;i++) {
        const SwatPlanRoom* r=&p->rooms[i];
        if(!r->hall || r->y==0) floor_box(w,r->x0,r->x1,r->z0,r->z1,r->y,r->floor,r->y==0 ? .04f : .2f);
        w->rooms[w->room_count++]=(SwatRoom){{(r->x0+r->x1)*.5f,r->y+1.4f,(r->z0+r->z1)*.5f},{(r->x1-r->x0)*.5f-.08f,1.4f,(r->z1-r->z0)*.5f-.08f},SWAT_DRYWALL,r->floor};
        // Visible fixture matches the existing ceiling-light origin.
        int fixture=swat_world_box(w,(b3Pos){r->hall ? 7.2f : (r->x0+r->x1)*.5f,r->y+2.62f,(r->z0+r->z1)*.5f},swat_v(.28f,.025f,.28f),SWAT_STEEL,0);
        w->objects[fixture].part=SWAT_PART_LIGHT;
    }
    // Floor-edge beams seal the exterior between storeys without filling the
    // stairwell opening or drawing a cosmetic slab across it.
    floor_box(w,3.85f,4.15f,z0-.15f,z1+.15f,3,SWAT_WOOD,.2f);
    floor_box(w,x1-.15f,x1+.15f,z0-.15f,z1+.15f,3,SWAT_WOOD,.2f);
    floor_box(w,4.15f,x1-.15f,z0-.15f,z0+.15f,3,SWAT_WOOD,.2f);
    floor_box(w,4.15f,x1-.15f,z1-.15f,z1+.15f,3,SWAT_WOOD,.2f);
    // Upper hall deck surrounds a real stairwell hole. Nothing fills that hole
    // in the renderer, physics, rays, acoustics or navigation.
    floor_box(w,4,4.7f,z0,z1,3,SWAT_TILE,.2f); floor_box(w,6.5f,8,z0,z1,3,SWAT_TILE,.2f);
    floor_box(w,4.7f,6.5f,z0,-3.1f,3,SWAT_TILE,.2f); floor_box(w,4.7f,6.5f,2.5f,z1,3,SWAT_TILE,.2f);
    for(int step=0;step<18;step++) {
        float top=(step+1)/6.0f;
        floor_box(w,4.85f,6.35f,-3+step*.3f,-3+(step+1)*.3f,top,SWAT_WOOD,top);
    }
    // Stair guard follows the incline, leaving both landings accessible.
    int rail=swat_world_box(w,(b3Pos){6.48f,2.25f,-.3f},swat_v(3.08f,.04f,.04f),SWAT_STEEL,0);
    swat_world_place(&w->objects[rail],-SWAT_PI*.5f); swat_world_tilt(&w->objects[rail],29.05f*SWAT_RAD);
    for(int i=0;i<p->wall_count;i++) {
        const SwatPlanWall* wall=&p->walls[i];
        swat_build_framed_wall(w,wall->origin,wall->yaw,wall->length,2.8f,wall->opening,wall->width,wall->sill,wall->door ? 2.1f : 1.25f,wall->exterior);
    }
    for(int i=0;i<p->furniture_count;i++) {
        const SwatPlanFurniture* f=&p->furniture[i]; swat_world_box(w,f->center,f->half,f->material,130);
    }
    floor_box(w,3.85f,x1+.15f,z0-.15f,z1+.15f,5.95f,SWAT_WOOD,.15f);
}
