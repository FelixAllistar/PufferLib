#include "protocol.h"
#include "motel.h"
#include "replay.h"
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
    return B3_ID_EQUALS(shape,((SwatObject*)context)->shape);
}
static void same_geometry(const SwatWorld* a,const SwatWorld* b) {
    assert(a->count==b->count);
    for(int i=0;i<a->count;i++) {
        assert(a->objects[i].active==b->objects[i].active);
        assert(!memcmp(a->objects[i].corners,b->objects[i].corners,sizeof(a->objects[i].corners)));
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
    int changed=0,probes=0,last=-1,exposed=0;
    for(int i=0;i<server.world.count;i++) {
        SwatObject* o=&server.world.objects[i];const SwatObject* old=&replica.world.objects[i];
        bool modified=memcmp(o->corners,old->corners,sizeof(o->corners))!=0;
        if(o->wall_group!=group)assert(o->active==old->active && !modified);
        if(!modified)continue;
        assert(o->active && old->active && o->fractured && swat_world_fragment_valid(o,o->corners));
        assert(area(o->corners)<area(old->corners) && area(o->corners)>.5f*area(old->corners));
        assert(o->health<old->health);
        float max_depth=o->material==SWAT_BRICK?.07f:.12f;
        for(int v=0;v<4;v++)assert(hypotf(o->corners[v][0]-old->corners[v][0],o->corners[v][1]-old->corners[v][1])<2*max_depth+.001f);
        last=i;changed++;
        if(motel){SwatMotelEdge edges[32];int n=swat_motel_wall_edges(&server.world,o,edges,32);assert(n>0);exposed+=n;}
        for(int e=0;e<4;e++) {
            int k=(e+1)%4;
            float y=.225f*(old->corners[e][0]+old->corners[k][0])+.275f*(o->corners[e][0]+o->corners[k][0]);
            float z=.225f*(old->corners[e][1]+old->corners[k][1])+.275f*(o->corners[e][1]+o->corners[k][1]);
            float dy=o->corners[k][0]-o->corners[e][0],dz=o->corners[k][1]-o->corners[e][1];
            if(dy*(z-o->corners[e][1])-dz*(y-o->corners[e][0])>=-.002f)continue;
            b3Vec3 n=swat_v(cosf(o->yaw),0,-sinf(o->yaw));
            b3Pos p=b3OffsetPos(o->center,swat_v(sinf(o->yaw)*z,y,cosf(o->yaw)*z));
            p=b3OffsetPos(p,swat_mul(n,-.5f));b3Vec3 delta=n;
            assert(b3Shape_RayCast(old->shape,p,delta).hit);
            assert(!b3Shape_RayCast(o->shape,p,delta).hit);
            b3Capsule tiny={.center1={0,0,0},.center2={0,.001f,0},.radius=.001f};
            assert(b3World_CastMover(replica.world.id,p,&tiny,delta,b3DefaultQueryFilter(),only_piece,(void*)old)<.99f);
            assert(b3World_CastMover(server.world.id,p,&tiny,delta,b3DefaultQueryFilter(),only_piece,o)>.999f);
            probes++;break;
        }
    }
    assert(changed>0 && probes>0);
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
    // Stale or forged updates cannot put material back into the cut aperture.
    bad=state;memcpy(bad.objects[last].corners,map.objects[last].corners,sizeof(bad.objects[last].corners));reject_snapshot(&bad);
    assert(swat_replay_digest(&server)==swat_replay_digest(&replica));
    swat_capture_map(&server,1,&map);swat_apply_map(&late,&map);assert(!motel || late.world.motel);
    assert(swat_apply_snapshot(&late,&state));same_geometry(&server.world,&late.world);
    swat_sim_close(&late);swat_sim_close(&replica);swat_sim_close(&server);
    printf("PASS breach geometry: %s yaw %.2f, %d removed/%d chipped, %d physical ray+CCD cuts, %d authored edges, %zu-byte snapshot; live/late replicas, digest, malformed/growing updates rejected atomically\n",motel?"masonry":"framed board",yaw,removed,changed,probes,exposed,length);
}
int main(void) {
    run(true,0);run(false,0);run(false,.73f);
    return 0;
}
