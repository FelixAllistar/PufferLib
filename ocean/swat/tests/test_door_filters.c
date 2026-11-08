// Hinged-leaf collision filters on every canonical map. Each hinged leaf must
// skip exactly its own frame pieces and still collide with everything else,
// including frame pieces it shares with other hinged leaves (the storefront
// site slab, building finish floors), whatever order the doors hinged or
// released in, and identically on a replica.
#include "protocol.h"
#include "door_damage.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static SwatSim server,replica;
static SwatMap map;
static SwatSnapshot state,decoded;
static unsigned char bytes[SWAT_NET_PACKET_MAX];
static b3ShapeId shapes_a[1024],shapes_b[1024];

// Box3D's pair rule (b3ShouldShapesCollide in box3d src/shape.h).
static bool pair(b3Filter a,b3Filter b) {
    if(a.groupIndex==b.groupIndex && a.groupIndex!=0)return a.groupIndex>0;
    return (a.maskBits&b.categoryBits)!=0 && (a.categoryBits&b.maskBits)!=0;
}
// Some active static bodies carry no shapes: the motel's legacy window-bay
// aggregates (windows_bind, objects 6/17/41/65/89) stay as logical opening
// anchors with no collision. They collide with nothing, so have no filter.
static bool collides(const SwatObject* p) {return p->active && B3_IS_NON_NULL(p->body) && b3Body_GetShapeCount(p->body)>0;}
// Every shape of one object carries the same filter.
static b3Filter body_filter(b3BodyId body,b3ShapeId* shapes) {
    int count=b3Body_GetShapeCount(body);assert(count>0 && count<=1024);
    assert(b3Body_GetShapes(body,shapes,1024)==count);
    b3Filter f=b3Shape_GetFilter(shapes[0]);
    for(int i=1;i<count;i++) {
        b3Filter g=b3Shape_GetFilter(shapes[i]);
        assert(g.categoryBits==f.categoryBits && g.maskBits==f.maskBits && g.groupIndex==f.groupIndex);
    }
    return f;
}
static bool in_frame(const SwatDoorMotion* m,int piece) {
    for(int k=0;k<m->frame_count;k++)if(m->frame[k]==piece)return true;
    return false;
}
// Returns the number of hinged leaves; *shared counts pieces framing two or more.
static int check(const SwatWorld* w,int* shared) {
    int hinged=0;uint64_t bits=0;
    for(int i=0;i<w->count;i++) {
        const SwatObject* o=&w->objects[i];if(!o->door)continue;
        uint64_t bit=swat_world_door_frame_bit(w,i);assert(!(bits&bit));bits|=bit; // one bit per door on these maps
        if(!o->active || B3_IS_NULL(o->body))continue;
        bool h=swat_door_hinged(o);hinged+=h;
        b3Filter leaf=body_filter(o->body,shapes_a);
        assert(leaf.groupIndex==0 && leaf.maskBits==B3_DEFAULT_MASK_BITS);
        assert(leaf.categoryBits==(h?bit:B3_DEFAULT_CATEGORY_BITS));
        if(h)assert(o->motion->frame_count>0);
        for(int j=0;j<w->count;j++) {
            const SwatObject* p=&w->objects[j];
            if(j==i || !collides(p))continue;
            bool frame=h && in_frame(o->motion,j);
            if(frame)assert(!p->door && b3Body_GetType(p->body)==b3_staticBody);
            if(pair(leaf,body_filter(p->body,shapes_b))==frame) {
                fprintf(stderr,"door %d vs object %d: frame%d but collide%d\n",i,j,frame,!frame);assert(false);
            }
        }
    }
    *shared=0;
    for(int j=0;j<w->count;j++) {
        int framing=0;
        for(int i=0;i<w->count;i++){const SwatObject* o=&w->objects[i];framing+=o->door && o->active && swat_door_hinged(o) && in_frame(o->motion,j);}
        *shared+=framing>1;
    }
    // Frame pieces still collide with people.
    const SwatActor* a=&server.actors[0];
    if(w==&server.world && a->present) {
        b3Filter actor=body_filter(a->controller.body.body,shapes_a);
        for(int j=0;j<w->count;j++)if(collides(&w->objects[j]))
            assert(pair(actor,body_filter(w->objects[j].body,shapes_b)));
    }
    return hinged;
}
// A fresh replica from the canonical map plus one snapshot carries the
// same filters as the authority.
static void replicate(void) {
    swat_capture_map(&server,1,&map);size_t n=swat_encode_map(bytes,sizeof(bytes),&map);
    assert(n && swat_decode_map(&map,bytes,n));swat_apply_map(&replica,&map);
    swat_capture_snapshot(&server,1,&state);n=swat_encode_snapshot(bytes,sizeof(bytes),&state);
    assert(n && swat_decode_snapshot(&decoded,bytes,n) && swat_apply_snapshot(&replica,&decoded));
    assert(replica.world.count==server.world.count);
    for(int i=0;i<server.world.count;i++) {
        const SwatObject* a=&server.world.objects[i];const SwatObject* b=&replica.world.objects[i];
        assert(a->active==b->active && B3_IS_NULL(a->body)==B3_IS_NULL(b->body));
        if(!collides(a))continue;
        b3Filter fa=body_filter(a->body,shapes_a),fb=body_filter(b->body,shapes_b);
        assert(fa.categoryBits==fb.categoryBits && fa.maskBits==fb.maskBits && fa.groupIndex==fb.groupIndex);
    }
    int shared;(void)check(&replica.world,&shared);
    swat_sim_close(&replica);
}
static void step(int count) {
    SwatInput neutral=swat_neutral_input();
    for(int t=0;t<count;t++)swat_sim_step(&server,&neutral);
}
static void mission(int id) {
    SwatConfig cfg=swat_default_config();cfg.mission=id;cfg.randomize=false;cfg.hostile_fire=false;
    swat_sim_init(&server,cfg,81);server.extraction=(b3Pos){500,0,500};
    for(int i=1;i<server.actor_count;i++)if(server.actors[i].present) {
        b3DestroyBody(server.actors[i].controller.body.body);memset(&server.actors[i],0,sizeof(server.actors[i]));
    }
    SwatWorld* w=&server.world;int doors=0,shared,last=-1;
    for(int i=0;i<w->count;i++)if(w->objects[i].door && w->objects[i].active){doors++;last=i;}
    if(!doors){swat_sim_close(&server);printf("SKIP door filters mission%d: no doors\n",id);return;}
    assert(check(w,&shared)==0);
    // Hinge every leaf in reverse index order; the per-tick refresh then runs
    // in index order. Neither order may decide a shared piece's filter.
    for(int i=w->count-1;i>=0;i--)if(w->objects[i].door && w->objects[i].active)assert(swat_world_break_latch(w,i));
    assert(check(w,&shared)==doors);
    step(3);assert(check(w,&shared)==doors);
    int shared_hinged=shared;
    if(id==SWAT_STOREFRONT)assert(shared>0); // site.glb frames all five storefront leaves
    replicate();
    // Release the last door's hinge (two hinges cut): its frame pieces must
    // keep every other hinged leaf's exclusion, before and after a tick.
    SwatObject* o=&w->objects[last];
    for(int h=0;h<SWAT_DOOR_HINGES && swat_door_hinged(o);h++) {
        b3Vec3 p=swat_door_hinge(o,h);
        assert(swat_world_cut(w,last,(SwatCut){.y=p.y,.z=p.z,.height=.08f*(o->half.y/1.095f),.width=.035f*(o->half.z/.54f),.kind=SWAT_CUT_BREACH})>0);
    }
    assert(o->active && !swat_door_hinged(o));
    assert(check(w,&shared)==doors-1);
    step(1);assert(check(w,&shared)==doors-1);
    replicate();
    swat_sim_close(&server);
    printf("PASS door filters mission%d: %d hinged leaves skip only their frames (%d shared pieces), order-independent, kept on release, same on replicas\n",id,doors,shared_hinged);
}
int main(void) {
    setvbuf(stdout,NULL,_IONBF,0);
    for(int id=0;id<SWAT_MISSION_COUNT;id++)mission(id);
    return 0;
}
