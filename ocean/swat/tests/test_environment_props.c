#include "environment_props.h"
#include "protocol.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static SwatSim authority,replica;
static SwatWorld before,scratch;
static SwatMap map,decoded_map;
static SwatSnapshot state,decoded_state;
static unsigned char packet[SWAT_NET_PACKET_MAX];

static void equal_props(const SwatEnvironmentProp* a,const SwatEnvironmentProp* b,int count) {
    for(int i=0;i<count;i++) {
        assert(a[i].support==b[i].support && a[i].furniture==b[i].furniture);
        assert(a[i].room==b[i].room && a[i].kind==b[i].kind && a[i].yaw==b[i].yaw);
        assert(!memcmp(&a[i].local,&b[i].local,sizeof(a[i].local)));
    }
}
static void synchronize(unsigned epoch) {
    swat_capture_map(&authority,epoch,&map);
    size_t size=swat_encode_map(packet,sizeof(packet),&map); assert(size);
    assert(swat_decode_map(&decoded_map,packet,size)); swat_apply_map(&replica,&decoded_map);
    swat_capture_snapshot(&authority,epoch,&state);
    size=swat_encode_snapshot(packet,sizeof(packet),&state); assert(size);
    assert(swat_decode_snapshot(&decoded_state,packet,size)); assert(swat_apply_snapshot(&replica,&decoded_state));
}

int main(void) {
    SwatConfig config=swat_default_config(); config.mission=SWAT_GENERATED; config.hostile_fire=false;
    unsigned seen=0; int total=0,maps_with_props=0;
    for(int seed=0;seed<96;seed++) {
        config.layout_seed=(uint32_t)seed; config.generator=(SwatGenerator)(seed%2); config.difficulty=seed%3;
        swat_sim_init(&authority,config,42);
        memcpy(&before,&authority.world,sizeof(before));
        SwatEnvironmentProp props[SWAT_ENV_PROP_MAX],again[SWAT_ENV_PROP_MAX];
        int count=swat_environment_props(&authority.world,&authority.layout,props);
        assert(count>=0 && count<=SWAT_ENV_PROP_MAX && count%2==0);
        assert(swat_environment_props(&authority.world,&authority.layout,again)==count);
        equal_props(props,again,count);
        assert(swat_environment_props(&authority.world,NULL,again)==0);
        assert(!memcmp(&before,&authority.world,sizeof(before)));
        total+=count; maps_with_props+=count>0;
        for(int i=0;i<count;i++) {
            const SwatEnvironmentProp* p=&props[i]; const SwatObject* o=&authority.world.objects[p->support];
            const SwatPlanRoom* room=&authority.layout.rooms[p->room];
            b3Vec3 size=swat_environment_prop_specs[p->kind].size;
            seen|=1u<<p->kind; assert(!room->hall && !o->door && o->part==SWAT_PART_SOLID);
            assert(p->local.y>o->half.y && p->local.y+size.y<o->half.y+.102f);
            // Every horizontal visual corner is inside the existing collider's
            // footprint, with 80 mm minimum inset; walking clearance is unchanged.
            for(int x=-1;x<=1;x+=2) for(int z=-1;z<=1;z+=2) {
                float px=p->local.x+x*size.x*.5f,pz=p->local.z+z*size.z*.5f;
                assert(fabsf(px)<=o->half.x-.08f && fabsf(pz)<=o->half.z-.08f);
                assert(o->center.x+px>room->x0 && o->center.x+px<room->x1);
                assert(o->center.z+pz>room->z0 && o->center.z+pz<room->z1);
            }
            // Distinct tabletop pair bounds never overlap.
            if(i%2) assert(props[i-1].local.x+swat_environment_prop_specs[props[i-1].kind].size.x*.5f <
                           p->local.x-size.x*.5f);
        }
        synchronize((unsigned)seed+1);
        assert(swat_environment_props(&replica.world,&replica.layout,again)==count);
        equal_props(props,again,count);
        if(count) {
            int support=props[0].support;
            // Damage shade may change; recipe identity does not reroll.
            assert(swat_world_damage(&authority.world,support,1)==false);
            assert(swat_environment_props(&authority.world,&authority.layout,again)==count);
            equal_props(props,again,count);
            memcpy(&scratch,&authority.world,sizeof(scratch));
            scratch.objects[support].pitch=.1f; // Unexpected support geometry fails closed.
            assert(swat_environment_props(&scratch,&authority.layout,again)==count-2);
            scratch.objects[support]=authority.world.objects[support];
            assert(scratch.count<SWAT_MAX_OBJECTS);
            scratch.objects[scratch.count++]=scratch.objects[support];
            assert(swat_environment_props(&scratch,&authority.layout,again)==count-2);
            // Independent support destruction hides only its props. No falling
            // debris or guessed collision is introduced by presentation.
            assert(swat_world_damage(&authority.world,support,10000));
            int remaining=swat_environment_props(&authority.world,&authority.layout,again);
            assert(remaining==count-2);
            for(int i=0;i<remaining;i++) assert(again[i].support!=support);
            synchronize((unsigned)seed+1);
            SwatEnvironmentProp remote[SWAT_ENV_PROP_MAX];
            assert(swat_environment_props(&replica.world,&replica.layout,remote)==remaining);
            equal_props(again,remote,remaining);
        }
        swat_sim_close(&replica); swat_sim_close(&authority);
    }
    assert(seen==(1u<<SWAT_ENV_PROP_KINDS)-1 && maps_with_props>40);
    printf("PASS props: %d placements in %d/96 uniform/neural maps, all six kinds, repeat/wire determinism, support destruction, bounded tabletop/room clearance, ambiguity rejection and immutable simulation\n",total,maps_with_props);
    return 0;
}
