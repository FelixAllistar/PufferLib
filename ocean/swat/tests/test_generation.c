#include "protocol.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static SwatSim sim,replica;
static void plans(void) {
    int arrangements[3]={0},sizes[9]={0};
    assert(swat_layout_policy_id()!=0);
    for(int mode=0;mode<2;mode++) for(uint32_t seed=0;seed<512;seed++) {
        SwatLayout a,b; int difficulty=(int)(seed%3);
        assert(swat_layout_generate(&a,seed,difficulty,(SwatGenerator)mode));
        assert(swat_layout_generate(&b,seed,difficulty,(SwatGenerator)mode));
        assert(a.fingerprint==b.fingerprint && !memcmp(a.tokens,b.tokens,sizeof(a.tokens)));
        assert(a.spawn_count==difficulty+4 && a.room_count>=3 && a.room_count<=5);
        assert(isfinite(a.quality) && a.path_length>0 && a.mission.overwatch_count>=2);
        if(mode) { arrangements[a.tokens[2]]++; sizes[a.tokens[0]*3+a.tokens[1]]++; }
        int invalid[SWAT_LAYOUT_TOKENS]; memcpy(invalid,a.tokens,sizeof(invalid)); invalid[1]=3;
        assert(!swat_layout_plan(&b,invalid,difficulty));
        a.walls[0].width=100; assert(!swat_layout_validate(&a));
    }
    for(int i=0;i<3;i++) assert(arrangements[i]>20);
    for(int i=0;i<9;i++) assert(sizes[i]>0);
    puts("PASS generation: 1024 deterministic valid plans, rejected malformed openings/tokens, all sizes and arrangements retained by learned policy");
}
static void doors(void) {
    SwatConfig config=swat_default_config(); config.mission=SWAT_GENERATED; config.hostile_fire=false; config.max_ticks=36000;
    int crossed=0,largest=0;
    for(uint32_t seed=1;seed<=24;seed++) {
        config.layout_seed=seed; config.difficulty=(int)(seed%3); swat_sim_init(&sim,config,42);
        assert(swat_sim_hostiles(&sim)==1+config.difficulty && swat_sim_unsecured(&sim)==3);
        assert(sim.world.count==sim.layout.object_count);
        if(sim.world.count>largest) largest=sim.world.count;
        // Actor placement is settled in the same real collision world.
        for(int a=0;a<sim.actor_count;a++) if(sim.actors[a].present) {
            b3Pos p=swat_body_feet_position(&sim.actors[a].controller.body);
            assert(isfinite(p.x) && p.y>-.1f && p.y<.15f);
        }
        for(int i=0;i<sim.world.count;i++) if(sim.world.objects[i].door) sim.world.objects[i].door_open=true;
        for(int i=0;i<90;i++) swat_world_step_doors(&sim.world);
        // Isolate geometry from temporary human/NPC blockage, retaining all
        // wall faces, timber framing, swinging leaves and furniture.
        for(int a=1;a<sim.actor_count;a++) if(sim.actors[a].present) {
            b3DestroyBody(sim.actors[a].controller.body.body); memset(&sim.actors[a],0,sizeof(sim.actors[a]));
        }
        SwatController* c=&sim.actors[0].controller;
        for(int i=0;i<sim.layout.wall_count;i++) {
            const SwatPlanWall* wall=&sim.layout.walls[i]; if(!wall->door || !wall->width) continue;
            b3Vec3 normal=swat_v(cosf(wall->yaw),0,-sinf(wall->yaw));
            b3Pos center=b3OffsetPos(wall->origin,swat_v(sinf(wall->yaw)*wall->opening,0,cosf(wall->yaw)*wall->opening));
            for(int side=-1;side<=1;side+=2) {
                b3Pos start=b3OffsetPos(center,swat_mul(normal,-side*.95f));
                b3Body_SetTransform(c->body.body,b3OffsetPos(start,swat_v(0,c->body.totalHeight*.5f+.01f,0)),b3Quat_identity);
                b3Body_SetLinearVelocity(c->body.body,swat_v(0,0,0));
                c->yaw=atan2f(side*normal.z,side*normal.x); c->pitch=0;
                SwatInput inputs[SWAT_MAX_ACTORS]; for(int a=0;a<SWAT_MAX_ACTORS;a++) inputs[a]=swat_neutral_input();
                inputs[0].forward=1; inputs[0].gait=SWAT_SLOW;
                float progress=-1;
                for(int tick=0;tick<160 && progress<.65f;tick++) {
                    swat_sim_step_inputs(&sim,inputs);
                    progress=side*b3Dot(b3SubPos(swat_body_feet_position(&c->body),center),normal);
                }
                if(progress<.65f) fprintf(stderr,"Blocked door seed=%u wall=%d yaw=%.2f side=%d progress=%.3f\n",seed,i,wall->yaw,side,progress);
                assert(progress>=.65f); assert(!c->body.crouched); crossed++;
            }
        }
        swat_sim_close(&sim);
    }
    assert(largest<SWAT_MAX_OBJECTS);
    printf("PASS generated collision: %d standing door crossings in both directions across 24 houses; largest %d objects\n",crossed,largest);
}
static void replication(void) {
    SwatConfig config=swat_default_config(); config.mission=SWAT_GENERATED; config.layout_seed=UINT32_MAX; config.difficulty=2;
    swat_sim_init(&sim,config,42);
    static SwatMap map,decoded; static unsigned char bytes[SWAT_NET_PACKET_MAX];
    swat_capture_map(&sim,17,&map); size_t size=swat_encode_map(bytes,sizeof(bytes),&map);
    assert(size && swat_decode_map(&decoded,bytes,size));
    swat_apply_map(&replica,&decoded);
    assert(replica.layout.fingerprint==sim.layout.fingerprint && replica.layout.policy_id==sim.layout.policy_id);
    assert(replica.config.layout_seed==UINT32_MAX && replica.config.difficulty==2);
    assert(b3Distance(replica.mission.staging,sim.mission.staging)<1e-5f);
    for(int i=0;i<3;i++) assert(b3Distance(replica.mission.overwatch[i].position,sim.mission.overwatch[i].position)<1e-5f);
    // Rejection is transactional: callers never receive partial config state.
    uint32_t epoch=99; SwatConfig received=swat_default_config();
    size=swat_encode_scenario(bytes,sizeof(bytes),17,&config);
    assert(size && swat_decode_scenario(bytes,size,&epoch,&received));
    assert(epoch==17 && received.mission==SWAT_GENERATED && received.layout_seed==UINT32_MAX && received.difficulty==2);
    for(size_t n=0;n<size;n++) assert(!swat_decode_scenario(bytes,n,&epoch,&received));
    bytes[size-3]=3; assert(!swat_decode_scenario(bytes,size,&epoch,&received));
    assert(received.difficulty==2 && epoch==17);
    swat_sim_close(&sim); swat_sim_close(&replica);
    puts("PASS generated protocol: exact tokens/model/seed, geometry, staging/posts and strict transactional scenario decoding");
}
int main(void) { plans(); doors(); replication(); return 0; }
