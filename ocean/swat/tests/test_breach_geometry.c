#include "protocol.h"
#include "motel.h"
#include "replay.h"
#include "breach_rubble.h"
#include "fracture_geometry.h"
#include "door_damage.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static SwatSim server,replica,late;
static SwatMap map;
static SwatSnapshot state,decoded,bad,untouched;
static unsigned char bytes[SWAT_NET_PACKET_MAX];
static float area(const float p[4][2]) {
    float a=0;for(int i=0;i<4;i++){int j=(i+1)%4;a+=p[i][0]*p[j][1]-p[j][0]*p[i][1];}return a*.5f;
}
static bool only_piece(b3ShapeId shape,void* context) {
    return B3_ID_EQUALS(b3Shape_GetBody(shape),((SwatObject*)context)->body);
}
typedef struct Assembly {SwatWorld* world;int group;} Assembly;
static bool only_assembly(b3ShapeId shape,void* context) {
    Assembly* a=context;SwatTag* tag=b3Body_GetUserData(b3Shape_GetBody(shape));
    return tag && tag->kind==SWAT_HIT_WORLD && a->world->objects[tag->index].wall_group==a->group;
}
static bool assembly_ray(SwatWorld* w,int group,b3Pos from,b3Vec3 delta) {
    for(int i=0;i<w->count;i++)if(w->objects[i].active && w->objects[i].wall_group==group &&
        swat_world_object_ray(&w->objects[i],from,delta))return true;
    return false;
}
static void passage(SwatWorld* w,int group,b3Pos contact,b3Vec3 normal,b3Vec3 tangent) {
    b3Pos from=b3OffsetPos(contact,swat_mul(normal,-.7f));from.y=.06f;
    b3Capsule standing={.center1={0,.32f,0},.center2={0,1.52f,0},.radius=.3f};
    Assembly a={w,group};
    assert(b3World_CastMover(w->id,from,&standing,swat_mul(normal,1.4f),b3DefaultQueryFilter(),only_assembly,&a)>.999f);
    for(int side=-1;side<=1;side+=2) {
        b3Pos p=b3OffsetPos(from,swat_mul(tangent,.60f*side));p.y=1.1f;
        assert(!assembly_ray(w,group,p,swat_mul(normal,1.4f)));
        float widths[2]={0};
        for(int height=0;height<2;height++)for(int step=0;step<40;step++) {
            p=b3OffsetPos(from,swat_mul(tangent,.05f*step*side));p.y=height?2.45f:1.1f;
            if(assembly_ray(w,group,p,swat_mul(normal,1.4f))){widths[height]=.05f*step;break;}
        }
        assert(widths[0]>.3f && widths[1]<widths[0]-.10f); // Crown narrows, unlike a doorway.
        p=b3OffsetPos(from,swat_mul(tangent,1.1f*side));p.y=2.62f;
        assert(assembly_ray(w,group,p,swat_mul(normal,1.4f))); // Upper side structure remains.
    }
}
static void same_geometry(const SwatWorld* a,const SwatWorld* b) {
    assert(a->count==b->count);
    for(int i=0;i<a->count;i++) {
        assert(a->objects[i].active==b->objects[i].active);
        assert(!memcmp(a->objects[i].corners,b->objects[i].corners,sizeof(a->objects[i].corners)));
        assert(swat_fracture_hash(&a->objects[i])==swat_fracture_hash(&b->objects[i]));
    }
}
static void reject_snapshot(const SwatSnapshot* invalid) {
    swat_capture_snapshot(&replica,1,&untouched);
    assert(!swat_apply_snapshot(&replica,invalid));
    swat_capture_snapshot(&replica,1,&decoded);
    assert(!memcmp(&untouched,&decoded,sizeof(untouched)));
}
static void run(bool motel,float yaw) {
    SwatConfig cfg=swat_default_config();cfg.randomize=false;cfg.hostile_fire=false;cfg.mission=SWAT_MOTEL;
    if(motel)swat_sim_init(&server,cfg,81);
    else {
        memset(&server,0,sizeof(server));server.config=cfg;server.rng=81;
        swat_world_init(&server.world);server.extraction=(b3Pos){-15,0,0};server.actor_count=1;
        swat_world_box(&server.world,(b3Pos){0,-.5f,0},swat_v(20,.5f,20),SWAT_CONCRETE,0);
        swat_build_framed_wall(&server.world,(b3Pos){0,0,0},yaw,4,2.7f,0,0,0,0,true);
        swat_sim_spawn_actor(&server,0,SWAT_OFFICER,(b3Pos){-5,0,0},0);
    }
    b3Vec3 normal=motel?swat_v(0,0,1):swat_v(cosf(yaw),0,-sinf(yaw));
    b3Pos from=motel?(b3Pos){-6.7f,1.1f,-7}:b3OffsetPos((b3Pos){0,1.1f,0},swat_mul(normal,-1));
    SwatHit hit=swat_world_ray(&server.world,from,normal,1.5f,b3_nullBodyId);assert(hit.hit);
    int group=server.world.objects[hit.index].wall_group;assert(group>0);
    swat_capture_map(&server,1,&map);swat_apply_map(&replica,&map);swat_apply_map(&late,&map);
    uint32_t rng=server.rng;int removed=swat_world_breach(&server.world,hit.index,hit.point);assert(removed>0 && rng==server.rng);
    assert(swat_world_breach(&late.world,hit.index,hit.point)==removed);
    same_geometry(&server.world,&late.world);swat_sim_close(&late);
    b3Vec3 tangent=motel?swat_v(1,0,0):swat_v(sinf(yaw),0,cosf(yaw));
    passage(&server.world,group,hit.point,normal,tangent);
    int changed=0,probes=0,last=-1,exposed=0;
    for(int i=0;i<server.world.count;i++) {
        SwatObject* o=&server.world.objects[i];const SwatObject* old=&replica.world.objects[i];
        bool modified=o->cuts!=NULL;
        if(o->wall_group!=group)assert(o->active==old->active && !modified);
        if(!modified || !o->active)continue;
        assert(old->active && o->health<old->health && o->cuts->section_count>0);
        SwatFracturePolygon original;swat_source_polygon(old,&original);float retained=0;
        for(int section=0;section<o->cuts->section_count;section++) {
            const SwatFracturePolygon* p=&o->cuts->sections[section];retained+=swat_polygon_area(p);
            for(int v=0;v<p->count;v++)assert(swat_polygon_contains(&original,p->vertices[v][0],p->vertices[v][1],.00001f));
            for(int e=0;e<p->count;e++) {
                const float* a=p->vertices[e];const float* b=p->vertices[(e+1)%p->count];
                float dy=b[0]-a[0],dz=b[1]-a[1],length=hypotf(dy,dz);if(length<.002f || !swat_fracture_edge_open(o,a,b))continue;
                float y=.5f*(a[0]+b[0])+.02f*dz/length,z=.5f*(a[1]+b[1])-.02f*dy/length;
                b3Pos probe=b3OffsetPos(o->center,b3Add(swat_mul(normal,-.3f),swat_mul(tangent,z)));probe.y=o->center.y+y;
                b3Vec3 delta=swat_mul(normal,.6f);if(!swat_world_object_ray(old,probe,delta))continue;
                assert(!swat_world_object_ray(o,probe,delta));
                SwatCutPlane planes[SWAT_CUT_SIDES];swat_cut_planes(&o->cuts->cuts[0],planes);float gap=INFINITY;
                for(int q=0;q<SWAT_CUT_SIDES;q++)gap=fminf(gap,planes[q].offset-planes[q].y*y-planes[q].z*z);
                // Shape casts deliberately have 5 mm slop plus tolerance.
                // Use interior samples with room for that and the tiny capsule.
                if(gap<.012f)continue;
                b3Capsule capsule={.center1={0,-.001f,0},.center2={0,.001f,0},.radius=.001f};
                assert(b3World_CastMover(replica.world.id,probe,&capsule,delta,b3DefaultQueryFilter(),only_piece,(void*)old)<.9f);
                float clear=b3World_CastMover(server.world.id,probe,&capsule,delta,b3DefaultQueryFilter(),only_piece,o);
                if(clear<=.999f) {
                    fprintf(stderr,"Cut CCD owner%d section%d edge%d length%.6f local(%.6f,%.6f) gap%.6f fraction%.6f\n",i,section,e,length,y,z,gap,clear);
                }
                assert(clear>.999f);probes++;
            }
        }
        assert(retained<swat_polygon_area(&original)-.000001f);if(o->part==SWAT_PART_SKIN)last=i;changed++;
        if(motel){SwatMotelEdge edges[32];exposed+=swat_motel_wall_edges(&server.world,o,edges,32);}
    }
    assert(changed>0 && probes>0 && last>=0);
    swat_capture_snapshot(&server,1,&state);
    size_t length=swat_encode_snapshot(bytes,sizeof(bytes),&state);assert(length>0 && length<sizeof(bytes));
    assert(swat_decode_snapshot(&decoded,bytes,length));
    const size_t truncated[]={0,11,length/2,length-33,length-1};
    for(int t=0;t<5;t++){untouched=decoded;assert(!swat_decode_snapshot(&decoded,bytes,truncated[t]));assert(!memcmp(&untouched,&decoded,sizeof(decoded)));}
    assert(!swat_encode_snapshot(bytes,length-1,&state));
    assert(swat_encode_snapshot(bytes,sizeof(bytes),&state)==length);
    bytes[4]=SWAT_NET_VERSION-1;assert(!swat_decode_snapshot(&decoded,bytes,length));bytes[4]=SWAT_NET_VERSION;
    bad=state;bad.objects[last].corners[0][0]=NAN;reject_snapshot(&bad);
    assert(swat_encode_snapshot(bytes,sizeof(bytes),&bad));assert(!swat_decode_snapshot(&decoded,bytes,length));
    bad=state;memcpy(bad.objects[last].corners[0],bad.objects[last].corners[2],sizeof(bad.objects[last].corners[0]));reject_snapshot(&bad);
    bad=state;bad.objects[last].fractured=false;reject_snapshot(&bad);
    bad=state;bad.objects[last].corners[0][0]=server.world.objects[last].half.y+.1f;reject_snapshot(&bad);
    assert(swat_encode_snapshot(bytes,sizeof(bytes),&state)==length && swat_decode_snapshot(&decoded,bytes,length));
    assert(swat_apply_snapshot(&replica,&decoded));same_geometry(&server.world,&replica.world);
    SwatRubbleFragment rubble[SWAT_RUBBLE_FRAGMENTS],received[SWAT_RUBBLE_FRAGMENTS];
    memset(rubble,0,sizeof(rubble));memset(received,0,sizeof(received));
    int chunks=swat_rubble_sample(&server.world,from,rubble);
    assert(chunks>0 && chunks<=SWAT_RUBBLE_FRAGMENTS && swat_rubble_sample(&replica.world,from,received)==chunks);
    assert(!memcmp(rubble,received,sizeof(rubble)));
    for(int i=0;i<chunks;i++) {
        assert(!server.world.objects[rubble[i].owner].active && rubble[i].height>0 && rubble[i].length<.17f);
        for(int j=0;j<i;j++)assert(rubble[j].owner!=rubble[i].owner);
    }
    // Stale or forged updates cannot put material back into the cut aperture.
    bad=state;bad.objects[last].cut_count=0;reject_snapshot(&bad);
    bad=state;bad.objects[last].cuts[0].width*=.5f;reject_snapshot(&bad);
    assert(swat_replay_digest(&server)==swat_replay_digest(&replica));
    swat_capture_map(&server,1,&map);swat_apply_map(&late,&map);assert(!motel || late.world.motel);
    assert(swat_apply_snapshot(&late,&state));same_geometry(&server.world,&late.world);
    swat_sim_close(&late);swat_sim_close(&replica);swat_sim_close(&server);
    printf("PASS breach geometry: %s yaw %.2f, %d affected/%d retained compounds, %d physical ray+CCD cuts, %d exposed edges, %zu-byte snapshot; live/late replicas, digest, malformed/growing updates rejected atomically\n",motel?"masonry":"framed board",yaw,removed,changed,probes,exposed,length);
}
static void spent_partial_charge(void) {
    SwatConfig cfg=swat_default_config();cfg.mission=SWAT_MOTEL;cfg.randomize=false;cfg.hostile_fire=false;
    memset(&server,0,sizeof(server));server.config=cfg;server.rng=81;server.actor_count=1;swat_world_init(&server.world);
    swat_world_box(&server.world,(b3Pos){0,-.5f,0},swat_v(20,.5f,20),SWAT_CONCRETE,0);
    int source=swat_world_box(&server.world,(b3Pos){0,1.35f,.9f},swat_v(.0125f,1.35f,1.1f),SWAT_DRYWALL,38);
    int small=swat_world_box(&server.world,(b3Pos){0,1.05f,-.5f},swat_v(.0125f,.3f,.2f),SWAT_DRYWALL,38);
    server.world.objects[source].part=server.world.objects[small].part=SWAT_PART_SKIN;
    server.world.objects[source].wall_group=server.world.objects[small].wall_group=source+1;
    swat_sim_spawn_actor(&server,0,SWAT_OFFICER,(b3Pos){-5,0,0},0);
    SwatObject* o=&server.world.objects[source];o->breach_owner=0;o->breach_position=(b3Pos){-.0125f,1.1f,-.1f};
    SwatInput detonate=swat_neutral_input();detonate.door_tool=SWAT_DETONATE_CHARGE;
    swat_door_tools(&server,0,&detonate);
    assert(o->active && o->cuts && o->breach_owner<0 && !server.world.objects[small].active);
    int generation=server.world.generation,count=server.sounds.count;
    SwatInput neutral=swat_neutral_input();swat_door_tools(&server,0,&neutral);
    detonate=swat_neutral_input();detonate.door_tool=SWAT_DETONATE_CHARGE;swat_door_tools(&server,0,&detonate);
    assert(server.world.generation==generation && server.sounds.count==count);
    swat_sim_close(&server);
    puts("PASS partial charged survivor: spent mount removed; a second remote press cannot reuse the charge");
}
static void retained_hole(void) {
    memset(&server,0,sizeof(server));server.config=swat_default_config();server.config.mission=SWAT_RANGE;server.actor_count=1;
    swat_world_init(&server.world);swat_world_box(&server.world,(b3Pos){0,-.5f,0},swat_v(10,.5f,10),SWAT_CONCRETE,0);
    int id=swat_world_box(&server.world,(b3Pos){0,1.35f,0},swat_v(.0125f,1.35f,1.2f),SWAT_DRYWALL,38);
    SwatObject* o=&server.world.objects[id];o->part=SWAT_PART_SKIN;o->wall_group=id+1;
    float corners[4][2]={{-1.35f,-1.2f},{1.35f,-1.2f},{1.35f,1.2f},{-1.35f,1.2f}};assert(swat_world_fragment(o,corners));
    swat_sim_spawn_actor(&server,0,SWAT_OFFICER,(b3Pos){-5,0,0},0);
    swat_capture_map(&server,1,&map);swat_apply_map(&replica,&map);
    SwatCut cut={.height=.3f,.width=.25f,.seed=17,.kind=SWAT_CUT_IMPACT};
    assert(swat_world_cut(&server.world,id,cut)==1 && o->active && o->health>35);
    assert(!memcmp(corners,o->corners,sizeof(corners)) && server.world.count==2);
    int clear=0,retained=0;
    for(int y=0;y<41;y++)for(int z=0;z<41;z++) {
        float ly=-1.2f+y*.06f,lz=-1+z*.05f,r=hypotf(ly/.3f,lz/.25f);
        b3Pos origin={-.3f,o->center.y+ly,lz};bool hit=swat_world_object_ray(o,origin,swat_v(.6f,0,0));
        if(r<.8f){assert(!hit);clear++;}
        else if(r>1.2f){assert(hit);retained++;}
    }
    assert(clear>10 && retained>1000);
    b3Pos entry={-o->half.x,1.35f,1.0f};assert(fabsf(swat_world_exit_distance(o,entry,swat_v(1,0,0))-.025f)<1e-5f);
    SwatCut second={.y=.55f,.z=.65f,.height=.18f,.width=.16f,.seed=18,.kind=SWAT_CUT_IMPACT};
    assert(swat_world_cut(&server.world,id,second)==1 && o->cuts->count==2);
    int generation=server.world.generation;assert(swat_world_cut(&server.world,id,second)==0 && server.world.generation==generation);
    swat_capture_snapshot(&server,1,&state);size_t n=swat_encode_snapshot(bytes,sizeof(bytes),&state);
    assert(n && swat_decode_snapshot(&decoded,bytes,n) && swat_apply_snapshot(&replica,&decoded));same_geometry(&server.world,&replica.world);
    bad=decoded;bad.objects[id].cuts[0].kind=SWAT_CUT_KINDS;reject_snapshot(&bad);
    bad=decoded;bad.objects[id].cut_count=1;reject_snapshot(&bad);
    assert(swat_replay_digest(&server)==swat_replay_digest(&replica));
    swat_sim_close(&server);swat_sim_close(&replica);
    printf("PASS enclosed openings: %d independent clear samples, %d retained samples around every side; original corners, material thickness, second aperture, duplicate no-op and atomic replica validation\n",clear,retained);
}
static void impact_across_partition(void) {
    memset(&server,0,sizeof(server));swat_world_init(&server.world);
    int a=swat_world_box(&server.world,(b3Pos){0,1.35f,-.6f},swat_v(.0125f,1.35f,.6f),SWAT_DRYWALL,38);
    int b=swat_world_box(&server.world,(b3Pos){0,1.35f,.6f},swat_v(.0125f,1.35f,.6f),SWAT_DRYWALL,38);
    int back=swat_world_box(&server.world,(b3Pos){.105f,1.35f,0},swat_v(.0125f,1.35f,1.2f),SWAT_DRYWALL,38);
    for(int i=a;i<=back;i++){server.world.objects[i].part=SWAT_PART_SKIN;server.world.objects[i].wall_group=a+1;}
    assert(swat_world_impact_at(&server.world,a,(b3Pos){-.0125f,1.35f,0},34,false));
    assert(server.world.objects[a].cuts && server.world.objects[b].cuts && !server.world.objects[back].cuts);
    for(int side=-1;side<=1;side+=2) {
        b3Pos p={-.3f,1.35f,side*.006f};b3Vec3 ray=swat_v(.4f,0,0);
        assert(!swat_world_object_ray(&server.world.objects[a],p,ray) && !swat_world_object_ray(&server.world.objects[b],p,ray));
    }
    for(int j=0;j<2;j++)assert(swat_world_impact_at(&server.world,a,(b3Pos){-.0125f,1.6f+j*.3f,-.4f},34,false));
    assert(server.world.objects[a].cuts->count==3);
    assert(!swat_world_impact_at(&server.world,a,(b3Pos){-.0125f,2.2f,-.4f},34,false));
    assert(swat_world_breach(&server.world,a,(b3Pos){-.0125f,1.35f,0})>0 && server.world.objects[a].cuts->count==4);
    swat_sim_close(&server);
    puts("PASS impact across hidden divisions: one round spans both neighbors, opposite sheet intact; bounded impacts reserve a wall-tool profile");
}
static void retained_door(void) {
    memset(&server,0,sizeof(server));server.config=swat_default_config();server.config.mission=SWAT_RANGE;server.actor_count=1;
    swat_world_init(&server.world);swat_world_box(&server.world,(b3Pos){0,-.5f,0},swat_v(10,.5f,10),SWAT_CONCRETE,0);
    int id=swat_world_box(&server.world,(b3Pos){0,1.1f,0},swat_v(.022f,1.05f,.54f),SWAT_WOOD,120);
    SwatObject* o=&server.world.objects[id];o->door=o->locked=true;o->hinge=(b3Pos){0,1.1f,-.54f};
    swat_sim_spawn_actor(&server,0,SWAT_OFFICER,(b3Pos){-5,0,0},0);swat_capture_map(&server,1,&map);swat_apply_map(&replica,&map);
    assert(swat_world_impact_at(&server.world,id,(b3Pos){-.022f,1.7f,-.12f},34,false) && o->active && !o->latch_broken && o->cuts);
    float body_health=o->health;
    assert(swat_world_breach(&server.world,id,(b3Pos){-.022f,1.16f,.4f})==1 && o->active && o->latch_broken && !o->locked && o->health==body_health);
    assert(server.world.count==2 && b3Body_GetShapeCount(o->body)==o->cuts->section_count);
    b3Pos center={-.4f,1.1f,0},notch={-.4f,1.16f,.49f},top={-.4f,2.0f,.44f};b3Vec3 across=swat_v(.8f,0,0);
    assert(swat_world_object_ray(&replica.world.objects[id],notch,across) && !swat_world_object_ray(o,notch,across));
    assert(swat_world_object_ray(o,center,across) && swat_world_object_ray(o,top,across));
    b3Capsule standing={.center1={0,.32f,0},.center2={0,1.52f,0},.radius=.3f};
    assert(b3World_CastMover(server.world.id,(b3Pos){-.5f,.05f,0},&standing,swat_v(1,0,0),b3DefaultQueryFilter(),only_piece,o)<.9f);
    assert(fabsf(swat_world_exit_distance(o,(b3Pos){-.022f,1.1f,0},swat_v(1,0,0))-.044f)<1e-5f);
    for(int i=0;i<90;i++)swat_world_step_doors(&server.world);
    assert(o->door_angle>1.5f && o->active && b3Body_GetShapeCount(o->body)==o->cuts->section_count);
    b3Vec3 normal=swat_v(cosf(o->yaw),0,-sinf(o->yaw));
    assert(swat_world_object_ray(o,b3OffsetPos(o->center,swat_mul(normal,-.4f)),swat_mul(normal,.8f)));
    swat_capture_snapshot(&server,1,&state);size_t n=swat_encode_snapshot(bytes,sizeof(bytes),&state);
    assert(n && swat_decode_snapshot(&decoded,bytes,n) && swat_apply_snapshot(&replica,&decoded));
    assert(replica.world.objects[id].latch_broken && b3Body_GetShapeCount(replica.world.objects[id].body)==o->cuts->section_count);
    bad=decoded;bad.objects[id].latch_broken=false;reject_snapshot(&bad);
    bad=decoded;bad.objects[id].locked=true;reject_snapshot(&bad);
    bad=decoded;bad.objects[0].latch_broken=true;reject_snapshot(&bad);
    assert(swat_replay_digest(&server)==swat_replay_digest(&replica));
    swat_sim_close(&server);swat_sim_close(&replica);
    puts("PASS retained door: local physical notch, solid upper/body cover, standing collision, preserved hinge motion and body count, thickness and atomic live replica");
}
int main(void) {
    run(true,0);run(false,0);run(false,.73f);
    spent_partial_charge();retained_hole();impact_across_partition();retained_door();
    return 0;
}
