#include "replay.h"
#include "motel.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static SwatSim sim,replica;
static SwatMap map,decoded_map;
static SwatSnapshot snapshot,decoded_snapshot;
static unsigned char bytes[SWAT_NET_PACKET_MAX];
static FILE* trace;
static int samples,relocated;
static float maximum_height,maximum_speed;
static void sample(bool fresh) {
    if(fresh) {
        swat_sim_close(&replica);swat_capture_map(&sim,1,&map);
        size_t n=swat_encode_map(bytes,sizeof(bytes),&map);assert(n && swat_decode_map(&decoded_map,bytes,n));
        swat_apply_map(&replica,&decoded_map);assert(replica.world.motel);
    }
    swat_capture_snapshot(&sim,1,&snapshot);size_t n=swat_encode_snapshot(bytes,sizeof(bytes),&snapshot);
    assert(n && swat_decode_snapshot(&decoded_snapshot,bytes,n) && swat_apply_snapshot(&replica,&decoded_snapshot));
    uint32_t hash=swat_replay_digest(&sim);assert(hash==swat_replay_digest(&replica));
    assert(fprintf(trace,"%d %08x\n",sim.tick,hash)>0);samples++;
}
static void bounded(const char* phase,int owner,int trial,int frame,float ceiling) {
    const SwatController* c=&sim.actors[0].controller;
    b3Pos p=swat_body_feet_position(&c->body);b3Vec3 v=b3Body_GetLinearVelocity(c->body.body);
    bool valid=isfinite(p.x) && isfinite(p.y) && isfinite(p.z) && isfinite(v.x) && isfinite(v.y) && isfinite(v.z) &&
        p.y>-.15f && p.y<ceiling && fabsf(v.y)<8 && sim.actors[0].alive && sim.end==SWAT_RUNNING;
    if(!valid)fprintf(stderr,"Contact %s owner%d trial%d frame%d feet %.9g %.9g %.9g velocity %.9g %.9g %.9g grounded%d\n",phase,owner,trial,frame,p.x,p.y,p.z,v.x,v.y,v.z,c->body.onGround);
    assert(valid);maximum_height=fmaxf(maximum_height,(float)p.y);maximum_speed=fmaxf(maximum_speed,fabsf(v.y));
}
// A point under the car is not a valid standing start. Check the entire player
// footprint from above, then move the start farther out until it is clear.
// Every trial still runs; a car-side approach encounters the real parked body.
static b3Pos clear_start(const SwatObject* stop,float x,int side) {
    for(int step=0;step<12;step++) {
        float distance=1.2f+step*.6f;bool clear=true;float height=0;
        for(int ix=-1;ix<=1;ix++)for(int iz=-1;iz<=1;iz++) {
            b3Pos from={x+ix*.30f,3,stop->center.z+side*distance+iz*.30f};
            SwatHit hit=swat_world_ray(&sim.world,from,swat_v(0,-1,0),4,sim.actors[0].controller.body.body);
            if(hit.kind!=SWAT_HIT_WORLD || hit.point.y>.1f || hit.normal.y<.7f)clear=false;
            if(!ix && !iz)height=(float)hit.point.y;
        }
        if(clear){relocated+=step!=0;return (b3Pos){x,height+.015f,stop->center.z+side*distance};}
    }
    assert(!"No clear supported contact-test start");return b3Pos_zero;
}
static void fixture_step(SwatInput in) {
    SwatInput inputs[SWAT_MAX_ACTORS];for(int i=0;i<SWAT_MAX_ACTORS;i++)inputs[i]=swat_neutral_input();
    inputs[0]=in;swat_sim_step_inputs(&sim,inputs);
}
static void crossings(void) {
    int stops=0,trials=0;
    for(int owner=1;owner<=SWAT_MOTEL_INSTANCES;owner++)if(swat_motel_instance(owner-1)->asset==SWAT_MOTEL_PARKING_STOP_ASSET) {
        const SwatObject* stop=&sim.world.objects[owner];assert(stop->part==SWAT_PART_OBSTACLE && b3Shape_GetType(stop->shape)==b3_meshShape);
        assert(sim.world.motel_meshes[SWAT_MOTEL_PARKING_STOP_ASSET]->triangleCount==swat_motel_asset(SWAT_MOTEL_PARKING_STOP_ASSET)->triangle_count);stops++;
        for(int stance=0;stance<2;stance++)for(int gait=0;gait<3;gait++)for(int side=-1;side<=1;side+=2)for(int lane=-1;lane<=1;lane++) {
            b3Pos start=clear_start(stop,(float)stop->center.x+lane*.72f,side);
            swat_sim_spawn_actor(&sim,0,SWAT_OFFICER,start,-side*SWAT_PI*.5f);
            SwatInput in=swat_neutral_input();in.crouch=stance;
            for(int t=0;t<20;t++)fixture_step(in);
            b3Pos settled=swat_body_feet_position(&sim.actors[0].controller.body);
            assert(b3Distance(start,settled)<.08f && sim.actors[0].controller.body.onGround);
            float minimum_z=100,maximum_z=-100;
            for(int t=0;t<240;t++) {
                in=swat_neutral_input();in.crouch=stance;in.gait=(SwatGait)gait;in.forward=t<120?1:-1;
                fixture_step(in);bounded("crossing",owner,trials,t,owner==142?1.75f:.4f);
                float z=(float)swat_body_feet_position(&sim.actors[0].controller.body).z;
                minimum_z=fminf(minimum_z,z);maximum_z=fmaxf(maximum_z,z);
                if(t%30==0)sample(false);
            }
            // These unobstructed paths must really cross the original concrete
            // lip; mere survival while parked beside it is insufficient.
            if(owner!=142)assert(minimum_z<stop->center.z-.3f && maximum_z>stop->center.z+.3f);
            sample(false);trials++;
        }
        sample(true);
    }
    assert(stops==5 && trials==180 && relocated>0);
    printf("PASS player wheel-stop matrix: %d trials across five original meshes, both directions, three lanes, three gaits and both stances; %d starts moved clear of actual parked geometry\n",trials,relocated);
}
static SwatInput towards(b3Pos goal,float throttle,int gait,bool crouch) {
    const SwatController* c=&sim.actors[0].controller;b3Vec3 d=b3SubPos(goal,swat_body_feet_position(&c->body));
    float angle=swat_angle(b3Atan2(d.z,d.x)-c->yaw);SwatInput in=swat_neutral_input();
    in.yaw_delta=swat_clamp(angle,-3*SWAT_RAD,3*SWAT_RAD);in.forward=fabsf(angle)<50*SWAT_RAD?throttle:0;
    in.gait=(SwatGait)gait;in.crouch=crouch;return in;
}
static void lip_reentry(void) {
    for(int stance=0;stance<2;stance++)for(int gait=0;gait<2;gait++) {
        // Retained grounded departure state, now driven solely by ordinary
        // player inputs. No navigation planner or per-frame pose correction.
        swat_sim_spawn_actor(&sim,0,SWAT_OFFICER,(b3Pos){-2.385337114,.064999998,4.499994278},SWAT_PI);
        SwatInput idle=swat_neutral_input();idle.crouch=stance;for(int t=0;t<20;t++)fixture_step(idle);
        assert(sim.actors[0].controller.body.onGround);
        b3Body_SetLinearVelocity(sim.actors[0].controller.body.body,swat_v(-.161651954f,-.250439614f,.000012911f));
        for(int t=0;t<360;t++) {
            b3Pos goal=t<30?(b3Pos){-2.3,-.08,5.7}:t<75?(b3Pos){-2.9,-.08,4.5}:(b3Pos){-2.9,-.08,6.3};
            fixture_step(towards(goal,.7f,gait,stance));bounded("lip reentry",143,stance*2+gait,t,.4f);
            if(t%30==0)sample(false);
        }
        b3Pos end=swat_body_feet_position(&sim.actors[0].controller.body);
        fprintf(stderr,"Lip departure stance%d gait%d end %.6f %.6f %.6f\n",stance,gait,end.x,end.y,end.z);
        assert(end.z>5.2f && end.y<.02f && b3Distance(end,(b3Pos){-2.9,-.065,6.3})<1);
        sample(false);
    }
    puts("PASS captured lip departure/reentry: four ordinary-input standing/crouched slow/walk cases retain bounded height/velocity without the discarded planner");
}
static void recorded_route(const char* path,SwatConfig cfg) {
    swat_sim_close(&sim);swat_sim_init(&sim,cfg,81);sample(true);
    SwatReplay journal;assert(swat_replay_record(&journal,path,&sim));char save[4096];assert(snprintf(save,sizeof(save),"%s.save",path)<(int)sizeof(save));
    for(int leg=0;leg<13;leg++) {
        b3Pos goal={leg==0?-2.38:(leg%4<2?-2.38:-2.64),-.08,leg%2?3.2:6.2};int t;
        for(t=0;t<900;t++) {
            b3Pos p=swat_body_feet_position(&sim.actors[0].controller.body);
            if(hypotf(p.x-goal.x,p.z-goal.z)<.22f)break;
            SwatInput in=towards(goal,1,(leg/2)%3,leg>=7);
            swat_sim_step(&sim,&in);assert(swat_replay_append(&journal,&in,&sim));bounded("recorded route",143,leg,t,.4f);
            if(t%30==0)sample(false);
        }
        if(t==900)fprintf(stderr,"Contact route failed leg%d\n",leg);
        assert(t<900);sample(false);
        if(leg==6) {
            assert(swat_replay_checkpoint(&journal,save));static SwatSim restored;char error[256];
            assert(swat_replay_restore(&restored,save,error,sizeof(error)) && swat_replay_digest(&restored)==swat_replay_digest(&sim));
            swat_sim_close(&restored);
        }
    }
    assert(!sim.totals.shots && sim.actors[0].health==100 && swat_replay_close(&journal));sample(true);
    printf("PASS ordinary player journal: %d ticks from default motel staging through twelve wheel-stop crossings/reversals, both stances and three gaits, exact checkpoint restore and fresh encoded replica\n",sim.tick);
}
int main(int argc,char** argv) {
    assert(argc==3 || (argc==4 && !strcmp(argv[3],"lip")));setvbuf(stdout,NULL,_IONBF,0);trace=fopen(argv[1],"w");assert(trace);
    SwatConfig cfg=swat_default_config();cfg.mission=SWAT_MOTEL;cfg.hostile_fire=false;cfg.randomize=false;cfg.max_ticks=1000000;
    swat_sim_init(&sim,cfg,81);sample(true);
    if(argc==3)crossings();
    lip_reentry();
    if(argc==3)recorded_route(argv[2],cfg);
    printf("PASS player contact evidence: %d encoded/hash samples, maximum feet Y %.6f, maximum absolute vertical speed %.6f; original geometry and ordinary player inputs\n",samples,maximum_height,maximum_speed);
    swat_sim_close(&replica);swat_sim_close(&sim);assert(!fclose(trace));return 0;
}
