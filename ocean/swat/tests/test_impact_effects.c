#include "impact_effects.h"
#include "sim.h"
#include "motel.h"
#include "protocol.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <time.h>

static SwatSim sim,unchanged;
static SwatSnapshot snapshot,received;
static unsigned char packet[SWAT_NET_PACKET_MAX];
static SwatImpactParticle particles[SWAT_IMPACT_EFFECT_PARTICLES],again[SWAT_IMPACT_EFFECT_PARTICLES];
static const b3Pos eye={0,1,3},point={0,1,0};
static const b3Vec3 forward={0,0,-1};
static int sample(const SwatSoundLog* log,int tick) {return swat_impact_sample(log,tick,eye,forward,particles);}
static void emit(SwatSoundLog* log,int tick,SwatSoundKind kind,SwatMaterial material) {
    swat_sound_surface(log,tick,0,kind,point,1,35,material);
}
int main(void) {
    SwatSoundLog log={0},before;
    for(int m=0;m<SWAT_MATERIAL_COUNT;m++) {
        log=(SwatSoundLog){0};emit(&log,100,SWAT_SOUND_BREAK,m);before=log;
        int n=sample(&log,110);assert(n==(m==SWAT_CARPET?0:8));
        assert(!memcmp(&before,&log,sizeof(log)));
        assert(swat_impact_sample(&log,110,eye,forward,again)==n);
        assert(!memcmp(particles,again,n*sizeof(*particles)));
        for(int p=0;p<n;p++) {
            assert(particles[p].color[3]>0 && particles[p].radius>0 && particles[p].radius<.3f);
            assert(particles[p].dust || particles[p].radius<.03f);
            if(m==SWAT_GLASS || m==SWAT_OPAQUE_GLASS || m==SWAT_STEEL)assert(!particles[p].dust);
        }
        assert(!sample(&log,99));assert(!sample(&log,160));
    }
    log=(SwatSoundLog){0};emit(&log,100,SWAT_SOUND_IMPACT,SWAT_GLASS);
    assert(sample(&log,101)==4);emit(&log,100,SWAT_SOUND_BREAK,SWAT_GLASS);
    assert(sample(&log,101)==8); // One break replaces the matching impact.
    emit(&log,101,SWAT_SOUND_IMPACT,SWAT_GLASS);assert(sample(&log,101)==12);
    assert(!swat_impact_sample(&log,101,(b3Pos){0,1,35},forward,particles));
    assert(!swat_impact_sample(&log,101,eye,(b3Vec3){0,0,1},particles));
    log=(SwatSoundLog){0};emit(&log,100,SWAT_SOUND_BREAK,SWAT_BRICK);
    assert(!sample(&log,145));
    swat_sound_surface(&log,100,0,SWAT_SOUND_FLASH,point,2.5f,80,SWAT_BRICK);
    assert(sample(&log,145)==8); // The existing charge event expands the cue.
    assert(!sample(&log,160));
    // Ring wrap + saturation: preserve breaks ahead of newer ordinary impacts.
    log=(SwatSoundLog){0};for(int i=0;i<150;i++)emit(&log,100,SWAT_SOUND_STEP,SWAT_CONCRETE);
    emit(&log,100,SWAT_SOUND_BREAK,SWAT_BRICK);uint32_t break_id=log.next_id-1;
    for(int i=0;i<100;i++)emit(&log,101,SWAT_SOUND_IMPACT,SWAT_CONCRETE);
    assert(sample(&log,102)==52);int breaks=0;
    for(int p=0;p<52;p++)breaks+=particles[p].event_id==break_id;
    assert(breaks==8);
    for(int i=0;i<30;i++)emit(&log,102,SWAT_SOUND_BREAK,SWAT_CONCRETE);
    assert(sample(&log,102)==SWAT_IMPACT_EFFECT_PARTICLES);
    for(int p=1;p<96;p++)assert(particles[p-1].distance_squared>=particles[p].distance_squared);
    log=(SwatSoundLog){0};emit(&log,INT_MAX,SWAT_SOUND_BREAK,SWAT_GLASS);
    assert(!sample(&log,INT_MIN));assert(sample(&log,INT_MAX)==8);
    log.events[0].material=SWAT_MATERIAL_COUNT;assert(!sample(&log,INT_MAX));
    log=(SwatSoundLog){0};emit(&log,100,SWAT_SOUND_BREAK,SWAT_GLASS);
    log.events[0].position.x=NAN;assert(!sample(&log,100));
    log=(SwatSoundLog){0};assert(!sample(&log,0));

    // Real ballistic event, not an artificial renderer event. Sampling must not
    // consume audio or modify state, damage, physics, inventory or shared RNG.
    SwatConfig cfg=swat_default_config();cfg.mission=SWAT_MOTEL;cfg.hostile_fire=false;cfg.randomize=false;
    swat_sim_init(&sim,cfg,81);
    const SwatMotelInstance* bay=swat_motel_instance(5);
    int pane=swat_motel_window_find(&sim.world,6,"lobby_glass_right_lower",SWAT_OPAQUE_GLASS);assert(pane>=0);
    b3Pos target=sim.world.objects[pane].center;
    b3Vec3 normal=swat_v(sinf(bay->yaw),0,cosf(bay->yaw));
    b3Pos from=b3OffsetPos(target,swat_mul(normal,.09f));
    swat_sim_shoot(&sim,0,from,swat_mul(normal,-1),(SwatShot){.fired=true,.damage=34,.range=.3f,.energy=6});
    assert(!sim.world.objects[pane].active);
    unchanged=sim;before=sim.sounds;
    assert(swat_impact_sample(&sim.sounds,sim.tick+5,b3OffsetPos(target,swat_mul(normal,2)),swat_mul(normal,-1),particles)==8);
    assert(!memcmp(&unchanged,&sim,sizeof(sim)) && !memcmp(&before,&sim.sounds,sizeof(before)));
    swat_capture_snapshot(&sim,1,&snapshot);
    size_t bytes=swat_encode_snapshot(packet,sizeof(packet),&snapshot);
    assert(bytes && swat_decode_snapshot(&received,packet,bytes));
    SwatSoundLog remote={0};for(int i=0;i<received.sound_count;i++)swat_sound_append(&remote,received.sounds[i]);
    assert(swat_impact_sample(&remote,sim.tick+5,b3OffsetPos(target,swat_mul(normal,2)),swat_mul(normal,-1),again)==8);
    assert(!memcmp(particles,again,8*sizeof(*particles)));
    swat_sim_reset(&sim);assert(!swat_impact_sample(&sim.sounds,sim.tick,eye,forward,particles));
    swat_sim_close(&sim);
    log=(SwatSoundLog){0};for(int i=0;i<SWAT_SOUND_CAPACITY;i++)emit(&log,100,SWAT_SOUND_BREAK,SWAT_BRICK);
    clock_t start=clock();int total=0;
    for(int i=0;i<10000;i++)total+=sample(&log,110);
    assert(total==10000*SWAT_IMPACT_EFFECT_PARTICLES);
    printf("Impact sampler saturated-log CPU mean: %.3f microseconds (10000 iterations)\n",1e6*(clock()-start)/CLOCKS_PER_SEC/10000);
    puts("PASS impact effects: material cues, deterministic replay sampling, expiry, reset, wrapped log, bounded saturation, break priority, charge cue, real ballistic event and immutable authority");
    return 0;
}
