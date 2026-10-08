#include "protocol.h"
#include "motel.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static SwatWorld world;
static void fence_hits(SwatWorld* w) {
    int openings=0,wires=0;
    for(int side=0;side<2;side++)for(int y=0;y<30;y++)for(int z=0;z<40;z++) {
        b3Pos origin={side?13:12,.3f+.0271f*y,4.2f+.0317f*z};
        b3Vec3 direction=swat_v(side?-1:1,0,0);
        SwatHit hit=swat_world_ray(w,origin,direction,1,b3_nullBodyId);
        if(!hit.hit){openings++;continue;}
        assert(swat_motel_fence_parent(w,&w->objects[hit.index])==155 && w->objects[hit.index].material==SWAT_STEEL);
        float thickness=swat_world_exit_distance(&w->objects[hit.index],hit.point,direction);
        assert(thickness>0 && thickness<.012f);wires++;
    }
    assert(openings>1800 && wires>50);
    for(int side=0;side<2;side++)for(int post=0;post<4;post++) {
        SwatHit hit=swat_world_ray(w,(b3Pos){side?13:12,.8f,6-2*post},swat_v(side?-1:1,0,0),1,b3_nullBodyId);
        assert(hit.hit && (post==3?hit.index==158:swat_motel_fence_parent(w,&w->objects[hit.index])==155+post));
        float thickness=swat_world_exit_distance(&w->objects[hit.index],hit.point,swat_v(side?-1:1,0,0));
        assert(thickness>.05f && thickness<.06f);
    }
    b3Capsule capsule={.center1={0,.32f,0},.center2={0,1.52f,0},.radius=.3f};
    for(int side=0;side<2;side++)for(int lane=0;lane<5;lane++) {
        float z=lane==0?-.65f:lane==4?6.65f:1+2*(lane-1);
        float f=b3World_CastMover(w->id,(b3Pos){side?13.5:11.5,-.075f,z},&capsule,swat_v(side?-2:2,0,0),b3DefaultQueryFilter(),NULL,NULL);
        assert(lane==0 || lane==4 ? f>.999f : f<.5f);
    }
    printf("PASS east fence: %d open rays/%d thin-wire hits, four posts, both faces, blocked panels and two open capsule routes\n",openings,wires);
}
static void live_fence_routes(void) {
    static SwatSim s;SwatConfig cfg=swat_default_config();cfg.mission=SWAT_MOTEL;cfg.randomize=false;cfg.hostile_fire=false;
    swat_sim_init(&s,cfg,81);
    b3Pos open={0},wire={0};bool found_open=false,found_wire=false;
    for(int i=0;i<200;i++) {
        b3Pos origin={12,.70f+.0019f*i,4.4f+.00131f*i};
        SwatHit hit=swat_world_ray(&s.world,origin,swat_v(1,0,0),1,b3_nullBodyId);
        if(!hit.hit){open=origin;found_open=true;}
        else if(swat_world_exit_distance(&s.world.objects[hit.index],hit.point,swat_v(1,0,0))>.001f){wire=origin;found_wire=true;}
    }
    assert(found_open && found_wire);
    float damage[3];
    for(int i=0;i<3;i++) {
        b3Pos origin=i==0?open:i==1?wire:(b3Pos){12,.8f,4};
        swat_sim_spawn_actor(&s,1,SWAT_CIVILIAN,(b3Pos){13.3,-.07f,origin.z},0);
        swat_sim_shoot(&s,0,origin,swat_v(1,0,0),(SwatShot){.fired=true,.damage=34,.range=2,.energy=1});
        damage[i]=100-s.actors[1].health;
    }
    assert(damage[0]>0 && damage[1]>0 && damage[1]<damage[0] && damage[2]==0);
    printf("PASS fence bullet traversal: opening %.3f damage, wire %.3f, post %.3f\n",damage[0],damage[1],damage[2]);
    for(int lane=0;lane<3;lane++) {
        SwatController* c=&s.actors[0].controller;
        float z=lane==0?-.65f:lane==1?3:6.65f;
        b3Body_SetTransform(c->body.body,(b3Pos){11.3,-.075f+c->body.totalHeight*.5f,z},b3Quat_identity);
        b3Body_SetLinearVelocity(c->body.body,swat_v(0,0,0));c->yaw=0;
        SwatInput in=swat_neutral_input();in.forward=1;
        for(int t=0;t<180;t++)swat_sim_step(&s,&in);
        float x=swat_body_feet_position(&c->body).x;
        printf("Fence live lane%d position %.3f %.3f %.3f\n",lane,x,(float)swat_body_feet_position(&c->body).y,(float)swat_body_feet_position(&c->body).z);fflush(stdout);
        assert(lane==1 ? x<12.31f && x>11.8f : x>13.5f);
    }
    swat_sim_close(&s);
    puts("PASS east fence: live standing controller stopped by wire and walks both open ends");
}
static void fence_charge_route(void) {
    static SwatSim s,replica;SwatConfig cfg=swat_default_config();cfg.mission=SWAT_MOTEL;cfg.randomize=false;cfg.hostile_fire=false;cfg.max_ticks=12000;
    swat_sim_init(&s,cfg,82);int parts=swat_motel_fence_part_count();assert(parts>0 && parts<=SWAT_FENCE_PART_CAPACITY);
    for(int i=0;i<parts;i++)assert(s.world.fence_meshes[i] && s.world.fence_meshes[i]->triangleCount<=240);
    static SwatWorld air;swat_world_init(&air);
    SwatSoundEvent sound={.position={11.5,.8,3},.strength=1,.range=30};b3Pos listener={13.5,.8,3};
    SwatAcousticPath reference=swat_acoustic_path(&air,&sound,listener),through=swat_acoustic_path(&s.world,&sound,listener);
    for(int i=0;i<3;i++)assert(fabsf(reference.bands[i]-through.bands[i])<1e-6f);
    swat_world_close(&air);
    SwatController* c=&s.actors[0].controller;
    b3Body_SetTransform(c->body.body,(b3Pos){11.65,-.065f+c->body.totalHeight*.5f,3},b3Quat_identity);c->yaw=0;
    SwatInput in=swat_neutral_input();for(int i=0;i<10;i++)swat_sim_step(&s,&in);
    b3Pos eye=swat_controller_eye(c);c->pitch=atan2f(1.42f-(float)eye.y,12.5f-(float)eye.x);s.actors[0].gear.breaching_charges=1;
    SwatHit target=swat_context_hit(&s,0,1.7f);assert(target.hit && swat_motel_fence_parent(&s.world,&s.world.objects[target.index])==156);
    assert(swat_world_breachable(&s.world.objects[target.index]));
    int first=s.world.objects[156].wall_group-1;
    for(int i=0;i<parts;i++)if(s.world.objects[first+i].part==SWAT_PART_FENCE_POST)assert(!swat_world_breachable(&s.world.objects[first+i]));
    b3Capsule capsule={.center1={0,.32f,0},.center2={0,1.52f,0},.radius=.3f};b3Pos from={11.6,-.075f,3};
    assert(b3World_CastMover(s.world.id,from,&capsule,swat_v(1.8f,0,0),b3DefaultQueryFilter(),NULL,NULL)<.8f);
    in.door_tool=SWAT_PLACE_CHARGE;for(int i=0;i<30;i++)swat_sim_step(&s,&in);
    assert(s.actors[0].gear.breaching_charges==1);
    in=swat_neutral_input();swat_sim_step(&s,&in);
    in.door_tool=SWAT_PLACE_CHARGE;for(int i=0;i<100;i++)swat_sim_step(&s,&in);
    assert(s.world.objects[target.index].breach_owner==0 && s.actors[0].gear.breaching_charges==0);
    in=swat_neutral_input();in.forward=-1;for(int i=0;i<100;i++)swat_sim_step(&s,&in);
    in=swat_neutral_input();in.door_tool=SWAT_DETONATE_CHARGE;swat_sim_step(&s,&in);
    int removed=0,survivors=0;for(int i=0;i<parts;i++){removed+=!s.world.objects[first+i].active;survivors+=s.world.objects[first+i].active;}
    assert(removed>0 && survivors>0 && s.actors[0].health==100);
    for(int owner=155;owner<=157;owner+=2)for(int i=0;i<parts;i++)assert(s.world.objects[s.world.objects[owner].wall_group-1+i].active);
    assert(b3World_CastMover(s.world.id,from,&capsule,swat_v(1.8f,0,0),b3DefaultQueryFilter(),NULL,NULL)>.999f);
    static SwatMap map;static SwatSnapshot snapshot;swat_capture_map(&s,1,&map);swat_capture_snapshot(&s,1,&snapshot);
    swat_apply_map(&replica,&map);assert(replica.world.motel && swat_apply_snapshot(&replica,&snapshot));
    assert(b3World_CastMover(replica.world.id,from,&capsule,swat_v(1.8f,0,0),b3DefaultQueryFilter(),NULL,NULL)>.999f);
    swat_sim_close(&replica);
    in=swat_neutral_input();in.forward=1;for(int i=0;i<160;i++)swat_sim_step(&s,&in);
    assert(swat_body_feet_position(&c->body).x>13.5f);
    swat_sim_reset(&s);fence_hits(&s.world);
    printf("PASS fence charge: interrupted placement, one charge, %d removed/%d surviving groups, adjacent bays intact, live passage, late replica and reset\n",removed,survivors);
    swat_sim_close(&s);
}
static void utility_hits(SwatWorld* w) {
    for(int room=0;room<4;room++) {
        int rack=SWAT_MOTEL_BASE_INSTANCES+1+2*room,basket=rack+1;
        const SwatMotelInstance* r=swat_motel_instance(rack-1);
        const SwatMotelInstance* b=swat_motel_instance(basket-1);
        // Ray crosses the empty space under the rack; no aggregate box hit.
        b3Pos from={r->origin.x,r->origin.y+.3f,r->origin.z+.3f};
        assert(!swat_world_ray(w,from,swat_v(0,0,-1),.6f,b3_nullBodyId).hit);
        from.y=r->origin.y+.491f;
        SwatHit rail=swat_world_ray(w,from,swat_v(0,0,-1),.6f,b3_nullBodyId);
        assert(rail.hit && rail.index==rack);
        // Through the open top into the base, rather than an invisible lid.
        from=(b3Pos){b->origin.x,b->origin.y+.5f,b->origin.z};
        SwatHit base=swat_world_ray(w,from,swat_v(0,-1,0),.6f,b3_nullBodyId);
        assert(base.hit && base.index==basket && base.distance>.46f && base.distance<.51f);
        from=(b3Pos){b->origin.x,b->origin.y+.18f,b->origin.z};
        SwatHit side=swat_world_ray(w,from,swat_v(1,0,0),.3f,b3_nullBodyId);
        assert(side.hit && side.index==basket && side.distance>.10f && side.distance<.16f);
    }
}
static void route(b3Pos* source,int count) {
    b3Capsule capsule={.center1={0,.32f,0},.center2={0,1.52f,0},.radius=.3f};
    for(int i=1;i<count;i++) {
        b3Pos from={source[i-1].x,source[i-1].z,-source[i-1].y};
        b3Pos to={source[i].x,source[i].z,-source[i].y};
        float f=b3World_CastMover(world.id,from,&capsule,b3SubPos(to,from),b3DefaultQueryFilter(),NULL,NULL);
        printf("capsule route %.2f %.2f -> %.2f %.2f: %.6f\n",(float)from.x,(float)from.z,(float)to.x,(float)to.z,f); fflush(stdout); assert(f>.999f);
    }
}
typedef struct WallQuery {SwatWorld* world;int group;} WallQuery;
static bool wall_only(b3ShapeId shape,void* context) {
    WallQuery* q=context;SwatTag* tag=b3Body_GetUserData(b3Shape_GetBody(shape));
    return tag && tag->kind==SWAT_HIT_WORLD && q->world->objects[tag->index].wall_group==q->group;
}
static void masonry_ballistics(void) {
    static SwatSim s,replica;SwatConfig cfg=swat_default_config();cfg.mission=SWAT_MOTEL;cfg.randomize=false;cfg.hostile_fire=false;
    swat_sim_init(&s,cfg,81);
    // Empty rear-wall lane: the target is behind the masonry, before the bathroom partition.
    b3Pos origin={-6.7f,1.1f,-7};b3Vec3 direction=swat_v(0,0,1);
    SwatHit hit=swat_world_ray(&s.world,origin,direction,1.5f,b3_nullBodyId);
    assert(hit.hit && s.world.objects[hit.index].material==SWAT_BRICK);
    SwatObject* wall=&s.world.objects[hit.index];int group=wall->wall_group;
    assert(swat_world_breachable(wall));
    swat_sim_spawn_actor(&s,1,SWAT_CIVILIAN,(b3Pos){-6.7f,.01f,-5.5f},-SWAT_PI*.5f);
    for(int weapon=0;weapon<SWAT_WEAPON_PROFILES;weapon++) {
        const SwatWeaponDef* def=swat_weapon_def(weapon);
        SwatShot shot={.fired=true,.damage=def->damage,.range=def->range,.energy=def->energy};
        for(int i=0;i<30;i++)swat_sim_shoot(&s,0,origin,direction,shot);
        assert(s.actors[1].health==100 && wall->health==wall->max_health && wall->active);
    }
    assert(!swat_world_impact(&s.world,hit.index,160)); // A ram cannot demolish exterior masonry.
    b3Capsule capsule={.center1={0,.32f,0},.center2={0,1.52f,0},.radius=.3f};
    b3Pos entry={-6.7f,.06f,-6.6f},exit={-6.7f,.06f,-5.4f};
    WallQuery query={&s.world,group};
    assert(b3World_CastMover(s.world.id,entry,&capsule,b3SubPos(exit,entry),b3DefaultQueryFilter(),wall_only,&query)<.99f);
    int removed=swat_world_breach(&s.world,hit.index,hit.point),surviving=0;
    assert(removed>0);
    for(int i=0;i<s.world.count;i++)surviving+=s.world.objects[i].active && s.world.objects[i].wall_group==group;
    assert(surviving>0); // A local aperture, not removal of the entire facade.
    int exposed=0;
    for(int i=group-1;i<s.world.count && s.world.objects[i].wall_group==group;i++) {
        SwatMotelEdge edges[32];int n=swat_motel_wall_edges(&s.world,&s.world.objects[i],edges,32);
        exposed+=n;for(int k=0;k<n;k++) {
            assert(edges[k].owner==i && s.world.objects[i].active && !s.world.objects[edges[k].neighbor].active && edges[k].length>0);
            int neighbor=edges[k].neighbor;s.world.objects[neighbor].active=true;
            assert(swat_motel_wall_edges(&s.world,&s.world.objects[i],edges,32)==n-1);
            s.world.objects[neighbor].active=false;
            break;
        }
    }
    assert(exposed>0);
    // Isolate the assembly clearance: bathroom fixtures behind it deliberately remain solid.
    swat_sim_spawn_actor(&s,1,SWAT_CIVILIAN,(b3Pos){-6.7f,.01f,-5},-SWAT_PI*.5f);
    assert(b3World_CastMover(s.world.id,entry,&capsule,b3SubPos(exit,entry),b3DefaultQueryFilter(),wall_only,&query)>.999f);
    static SwatMap map;static SwatSnapshot snapshot;
    swat_capture_map(&s,1,&map);swat_capture_snapshot(&s,1,&snapshot);
    swat_apply_map(&replica,&map);assert(swat_apply_snapshot(&replica,&snapshot));
    query.world=&replica.world;
    assert(b3World_CastMover(replica.world.id,entry,&capsule,b3SubPos(exit,entry),b3DefaultQueryFilter(),wall_only,&query)>.999f);
    swat_sim_close(&replica);
    SwatShot shot={.fired=true,.damage=34,.range=30,.energy=1};
    swat_sim_shoot(&s,0,origin,direction,shot);assert(s.actors[1].health==66);
    // Actual unobstructed exterior entry, with all world collisions enabled.
    entry=(b3Pos){-12.6f,.06f,-1.17f};exit=(b3Pos){-11.4f,.06f,-1.17f};
    hit=swat_world_ray(&s.world,b3OffsetPos(entry,swat_v(0,1,0)),swat_v(1,0,0),1.2f,b3_nullBodyId);
    assert(hit.hit && s.world.objects[hit.index].material==SWAT_BRICK);
    assert(b3World_CastMover(s.world.id,entry,&capsule,b3SubPos(exit,entry),b3DefaultQueryFilter(),NULL,NULL)<.99f);
    assert(swat_world_breach(&s.world,hit.index,hit.point)>0);
    assert(b3World_CastMover(s.world.id,entry,&capsule,b3SubPos(exit,entry),b3DefaultQueryFilter(),NULL,NULL)>.999f);
    b3Pos next;assert(swat_navigation_next(&s,entry,exit,&next));
    swat_sim_close(&s);
    // Every exterior assembly uses masonry; bathroom partitions use board.
    swat_world_init(&world);swat_motel_build(&world);
    int assemblies=0;
    for(int i=SWAT_MOTEL_INSTANCES+1;i<world.count;i++) {
        SwatObject* o=&world.objects[i];int parent=swat_motel_wall_parent(&world,o);if(parent<0)continue;
        int asset=swat_motel_instance(parent-1)->asset;
        assert(o->material==(asset==22?SWAT_DRYWALL:SWAT_BRICK));assert(swat_world_breachable(o));assemblies++;
    }
    assert(assemblies>100 && world.count<SWAT_MAX_OBJECTS);
    SwatObject probe=world.objects[SWAT_MOTEL_INSTANCES+1];probe.half.x=.09f;probe.material=SWAT_BRICK;assert(swat_world_breachable(&probe));
    probe.half.x=.3f;assert(!swat_world_breachable(&probe));
    probe.half.x=.1f;probe.material=SWAT_CONCRETE;assert(!swat_world_breachable(&probe));
    probe.half.x=.004f;probe.material=SWAT_STEEL;assert(!swat_world_breachable(&probe));
    printf("PASS masonry: %d pieces/%d objects, all weapon profiles stopped, civilian protected, ram resisted, localized charge aperture/capsule/replica, strength and thickness limits\n",assemblies,world.count);
    swat_world_close(&world);
}
static void live_squad_and_evacuation(void) {
    static SwatSim s;SwatConfig cfg=swat_default_config();cfg.mission=SWAT_MOTEL;swat_config_human(&cfg);
    cfg.randomize=false;cfg.hostile_fire=false;cfg.max_ticks=12000;
    swat_sim_init(&s,cfg,81);
    // Isolate navigation/interaction from combat decisions, preserving the
    // real doors, furnished rooms, body controller and civilian escort rules.
    for(int i=1;i<s.actor_count;i++)if(s.actors[i].present) {
        if(s.actors[i].role==SWAT_OFFICER)s.actors[i].mind.order=SWAT_ORDER_HOLD;
        else s.actors[i].gear.surrendered=true;
    }
    SwatActor* officer=&s.actors[3];officer->mind.order=SWAT_ORDER_MOVE;officer->mind.goal=(b3Pos){-6.8f,0,-.85f};
    int t=0;for(;t<2400;t++) {
        swat_sim_step(&s,&(SwatInput){0});
        if(b3Distance(swat_body_feet_position(&officer->controller.body),officer->mind.goal)<.65f)break;
    }
    b3Pos feet=swat_body_feet_position(&officer->controller.body);
    printf("Motel squad room101 after %d ticks: %.3f %.3f %.3f\n",t,(float)feet.x,(float)feet.y,(float)feet.z);fflush(stdout);
    if(t==2400) {
        printf("Route waypoint %.3f %.3f %.3f yaw %.3f\n",(float)officer->mind.waypoint.x,(float)officer->mind.waypoint.y,(float)officer->mind.waypoint.z,officer->controller.yaw);
        for(int i=0;i<s.actor_count;i++)if(s.actors[i].present) {
            b3Pos p=swat_body_feet_position(&s.actors[i].controller.body);
            printf("Actor %d role%d feet %.3f %.3f %.3f\n",i,s.actors[i].role,(float)p.x,(float)p.y,(float)p.z);
        }fflush(stdout);
    }
    assert(t<2400 && s.world.objects[16].door_open);
    // The standing civilian physically fills the narrow chair/bed aisle. Clear
    // that occupant through an ordinary escort before asking a body to pass.
    officer->mind.goal=(b3Pos){-7.2f,0,2};officer->mind.replan_tick=0;
    for(t=0;t<900;t++) {swat_sim_step(&s,&(SwatInput){0});if(b3Distance(swat_body_feet_position(&officer->controller.body),officer->mind.goal)<.65f)break;}
    assert(t<900);officer->mind.order=SWAT_ORDER_HOLD;
    SwatController* leader=&s.actors[0].controller;
    b3Body_SetTransform(leader->body.body,b3OffsetPos(s.extraction,swat_v(0,leader->body.totalHeight*.5f+.01f,0)),b3Quat_identity);
    b3Body_SetLinearVelocity(leader->body.body,swat_v(0,0,0));
    s.actors[2].gear.restrained=true;s.actors[2].mind.escort_owner=0;
    for(t=0;t<2400 && !s.actors[2].rescued;t++)swat_sim_step(&s,&(SwatInput){0});
    feet=swat_body_feet_position(&s.actors[2].controller.body);
    printf("Motel civilian evacuation after %d ticks: %.3f %.3f %.3f\n",t,(float)feet.x,(float)feet.y,(float)feet.z);fflush(stdout);
    assert(s.actors[2].rescued && s.actors[2].mind.escort_owner==-1 && s.end==SWAT_RUNNING);
    officer->mind.order=SWAT_ORDER_MOVE;officer->mind.goal=(b3Pos){-6.15f,0,-3.5f};officer->mind.replan_tick=0;
    for(t=0;t<1200;t++) {swat_sim_step(&s,&(SwatInput){0});if(b3Distance(swat_body_feet_position(&officer->controller.body),officer->mind.goal)<.65f)break;}
    feet=swat_body_feet_position(&officer->controller.body);
    printf("Motel squad cleared aisle after %d ticks: %.3f %.3f %.3f\n",t,(float)feet.x,(float)feet.y,(float)feet.z);fflush(stdout);
    assert(t<1200);
    // Regroup the complete three-officer squad, including the officer still
    // inside. The evacuated civilian remains a real body at staging.
    for(int i=0;i<s.actor_count;i++)if(s.actors[i].present && s.actors[i].mind.bot && s.actors[i].role==SWAT_OFFICER)
        s.actors[i].mind.order=SWAT_ORDER_FALL_IN;
    for(t=0;t<2400 && swat_sim_progress(&s).officers_away;t++)swat_sim_step(&s,&(SwatInput){0});
    printf("Motel full squad regroup after %d ticks, away=%d\n",t,swat_sim_progress(&s).officers_away);fflush(stdout);
    assert(!swat_sim_progress(&s).officers_away);
    swat_sim_close(&s);
    puts("PASS motel live route: squad opens front door and crosses furnished room; restrained civilian physically follows to staging and stays evacuated");
}
static void squad_door_orders(void) {
    static SwatSim s;SwatConfig cfg=swat_default_config();cfg.mission=SWAT_MOTEL;swat_config_human(&cfg);
    cfg.randomize=false;cfg.hostile_fire=false;cfg.max_ticks=12000;
    swat_sim_init(&s,cfg,81);
    for(int i=1;i<s.actor_count;i++)if(s.actors[i].present && s.actors[i].role!=SWAT_OFFICER)s.actors[i].gear.surrendered=true;
    SwatController* leader=&s.actors[0].controller;
    b3Body_SetTransform(leader->body.body,(b3Pos){-3.2f,leader->body.totalHeight*.5f+.03f,2.8f},b3Quat_identity);
    b3Body_SetLinearVelocity(leader->body.body,swat_v(0,0,0));leader->yaw=-SWAT_PI*.5f;leader->pitch=0;
    for(int i=0;i<30;i++)swat_sim_step(&s,&(SwatInput){0});
    SwatHit hit=swat_context_hit(&s,0,24);assert(hit.kind==SWAT_HIT_WORLD && s.world.objects[hit.index].door);
    int door=hit.index;
    swat_sim_step(&s,&(SwatInput){.squad_order=SWAT_ORDER_STACK});
    for(int slot=1;slot<4;slot++)assert(s.actors[swat_player_actor(slot)].mind.pending_order==SWAT_ORDER_STACK);
    int t;for(t=0;t<2400;t++) {
        swat_sim_step(&s,&(SwatInput){0});int arrived=0;
        for(int slot=1;slot<4;slot++) {
            SwatActor* a=&s.actors[swat_player_actor(slot)];
            arrived+=a->mind.order==SWAT_ORDER_STACK && b3Distance(swat_body_feet_position(&a->controller.body),a->mind.goal)<.6f;
        }
        if(arrived==3)break;
    }
    printf("Motel Stack after %d ticks, door open=%d\n",t,s.world.objects[door].door_open);fflush(stdout);
    for(int slot=1;slot<4;slot++) {
        SwatActor* a=&s.actors[swat_player_actor(slot)];b3Pos p=swat_body_feet_position(&a->controller.body);
        printf("  officer%d %.2f %.2f goal %.2f %.2f waypoint %.2f %.2f yaw %.2f\n",slot,(float)p.x,(float)p.z,(float)a->mind.goal.x,(float)a->mind.goal.z,(float)a->mind.waypoint.x,(float)a->mind.waypoint.z,a->controller.yaw);
        assert(p.z>.3f);
    }fflush(stdout);
    assert(t<2400 && !s.world.objects[door].door_open);
    swat_sim_step(&s,&(SwatInput){.squad_order=SWAT_ORDER_CLEAR,.squad_queue=true});
    for(int slot=1;slot<4;slot++)assert(s.actors[swat_player_actor(slot)].mind.pending_order==SWAT_ORDER_CLEAR);
    for(t=0;t<120;t++)swat_sim_step(&s,&(SwatInput){0});
    assert(!s.world.objects[door].door_open);
    for(int slot=1;slot<4;slot++)assert(s.actors[swat_player_actor(slot)].mind.order==SWAT_ORDER_STACK);
    s.world.objects[door].wedge_owner=0;s.world.generation++;
    swat_sim_step(&s,&(SwatInput){.squad_execute=true});
    for(t=0;t<240;t++)swat_sim_step(&s,&(SwatInput){0});
    assert(!s.world.objects[door].door_open);
    for(int slot=1;slot<4;slot++)assert(swat_body_feet_position(&s.actors[swat_player_actor(slot)].controller.body).z>0);
    s.world.objects[door].wedge_owner=-1;s.world.generation++;
    for(t=0;t<2400;t++) {
        swat_sim_step(&s,&(SwatInput){0});int arrived=0;
        for(int slot=1;slot<4;slot++) {
            SwatActor* a=&s.actors[swat_player_actor(slot)];b3Pos p=swat_body_feet_position(&a->controller.body);
            arrived+=a->mind.order==SWAT_ORDER_CLEAR && a->mind.entry_settled && p.z<-.6f &&
                b3Distance(p,a->mind.goal)<.5f && fabsf(swat_angle(a->controller.yaw-a->mind.order_yaw))<.15f;
        }
        if(arrived==3)break;
    }
    printf("Motel Clear after %d ticks, door open=%d\n",t,s.world.objects[door].door_open);fflush(stdout);
    for(int slot=1;slot<4;slot++) {
        SwatActor* a=&s.actors[swat_player_actor(slot)];b3Pos p=swat_body_feet_position(&a->controller.body);
        printf("  officer%d %.2f %.2f goal %.2f %.2f waypoint %.2f %.2f yaw %.2f\n",slot,(float)p.x,(float)p.z,(float)a->mind.goal.x,(float)a->mind.goal.z,(float)a->mind.waypoint.x,(float)a->mind.waypoint.z,a->controller.yaw);
    }fflush(stdout);
    assert(t<2400 && s.world.objects[door].door_open);
    swat_sim_close(&s);
    puts("PASS motel orders: three officers stack without opening; queued Clear waits for execute, respects wedges, then enters to separate supported positions and faces inward");
}
static void live_breach_routes(void) {
    static SwatSim s;SwatConfig cfg=swat_default_config();cfg.mission=SWAT_MOTEL;swat_config_human(&cfg);
    cfg.randomize=false;cfg.hostile_fire=false;cfg.max_ticks=6000;
    const b3Pos starts[]={{-12.7f,0,-1.17f},{-4.6f,.02f,-1.17f}};
    const b3Pos goals[]={{-11.0f,.02f,-1.17f},{-3.1f,.02f,-1.17f}};
    for(int route=0;route<2;route++) {
        swat_sim_init(&s,cfg,81);
        for(int i=1;i<s.actor_count;i++)if(s.actors[i].present) {
            if(s.actors[i].role==SWAT_OFFICER)s.actors[i].mind.order=SWAT_ORDER_HOLD;
            else s.actors[i].gear.surrendered=true;
        }
        // Block door detours: only the physical opening can connect these goals.
        for(int i=0;i<s.world.count;i++)if(s.world.objects[i].door)s.world.objects[i].wedge_owner=0;
        s.world.generation++;
        SwatActor* a=&s.actors[3];SwatController* c=&a->controller;
        b3Body_SetTransform(c->body.body,b3OffsetPos(starts[route],swat_v(0,c->body.totalHeight*.5f+.01f,0)),b3Quat_identity);
        b3Body_SetLinearVelocity(c->body.body,swat_v(0,0,0));c->yaw=0;
        a->mind.order=SWAT_ORDER_MOVE;a->mind.goal=goals[route];
        for(int t=0;t<120;t++)swat_sim_step(&s,&(SwatInput){0});
        b3Pos feet=swat_body_feet_position(&c->body);
        assert(feet.x<starts[route].x+.15f);
        SwatHit wall=swat_world_ray(&s.world,b3OffsetPos(feet,swat_v(0,1,0)),swat_v(1,0,0),1.5f,c->body.body);
        assert(wall.hit && wall.kind==SWAT_HIT_WORLD && swat_world_breachable(&s.world.objects[wall.index]));
        assert(swat_world_breach(&s.world,wall.index,wall.point)>0);
        int t=0;for(;t<900;t++) {
            swat_sim_step(&s,&(SwatInput){0});feet=swat_body_feet_position(&c->body);
            if(b3Distance(feet,goals[route])<.5f)break;
        }
        printf("Motel live %s breach after %d ticks: %.3f %.3f %.3f\n",route?"inter-room":"exterior",t,(float)feet.x,(float)feet.y,(float)feet.z);fflush(stdout);
        assert(t<900 && a->health==100);
        swat_sim_close(&s);
    }
    puts("PASS motel breach movement: wedged doors prevent detours; actual squad controller crosses exterior masonry and inter-room openings after destruction without teleporting");
}
int main(void) {
    live_fence_routes();
    fence_charge_route();
    squad_door_orders();
    live_squad_and_evacuation();
    live_breach_routes();
    masonry_ballistics();
    swat_world_init(&world); swat_motel_build(&world); assert(world.motel && world.count>SWAT_MOTEL_INSTANCES+1 && world.room_count==6);
    utility_hits(&world);
    fence_hits(&world);
    for(int room=1;room<=4;room++) {
        b3Pos bulb;assert(swat_motel_lamp(&world,room,&bulb));
        SwatMotelInstance mount;int owner=swat_motel_dressing(&world,(room-1)*SWAT_MOTEL_DRESSING_ASSETS+7,&mount);
        assert(owner>0 && fabsf((float)(bulb.y-mount.origin.y)-.08959322f)<1e-6f);
        world.objects[owner].active=false;assert(!swat_motel_lamp(&world,room,&bulb));world.objects[owner].active=true;
    }
    for(int i=0;i<SWAT_MOTEL_DRESSING_INSTANCES;i++) {
        SwatMotelInstance mount;int owner=swat_motel_dressing(&world,i,&mount);assert(owner>0);
        world.objects[owner].active=false;assert(swat_motel_dressing(&world,i,&mount)==-1);world.objects[owner].active=true;
    }
    SwatMotelInstance closed_viewer,open_viewer;
    assert(swat_motel_dressing(&world,5,&closed_viewer)==16);
    world.objects[16].yaw+=SWAT_PI*.5f;
    assert(swat_motel_dressing(&world,5,&open_viewer)==16);
    assert(fabsf(open_viewer.origin.x-world.objects[16].hinge.x-.0345f)<1e-5f);
    assert(fabsf(open_viewer.origin.z-world.objects[16].hinge.z+.54f)<1e-5f);
    world.objects[16].yaw-=SWAT_PI*.5f;

    int doors=0,first=-1;
    for(int i=0;i<world.count;i++) if(world.objects[i].door) { doors++;if(first<0)first=i;world.objects[i].door_open=true; }
    assert(doors==5);
    SwatObject* d=&world.objects[first];
    b3Pos from={d->hinge.x+.54f,1.1f,1};
    SwatHit closed=swat_world_ray(&world,from,swat_v(0,0,-1),2,b3_nullBodyId);assert(closed.hit && closed.index==first);
    for(int t=0;t<60;t++)swat_world_step_doors(&world);
    assert(!swat_world_ray(&world,from,swat_v(0,0,-1),2,b3_nullBodyId).hit);
    b3Pos gallery[]={{-11.2f,-.85f,0},{6.8f,-.85f,0}}; route(gallery,2);
    b3Pos reception[]={{-11.25f,-.85f,0},{-11.25f,4.72f,0},{-10,4.72f,0},{-10,8.6f,0}};route(reception,4);
    b3Pos side[]={{-10,8,0},{-6.8f,8,0}};route(side,2);
    b3Pos west[]={{-14,-.6f,-.08f},{-14,3.3f,-.08f}};route(west,2);
    b3Pos ramp[]={{-10.9f,-3.01f,-.078f},{-10.9f,-1.83f,0},{-10.9f,-.8f,0}};route(ramp,3);
    for(int i=0;i<4;i++) {
        float x=-6+4*i;b3Pos room[]={{x-1.2f,-.85f,0},{x-1.2f,.7f,0},{x-.15f,1.12f,0},{x-.15f,3.52f,0},{x-1.2f,3.52f,0},{x-1.2f,4.70f,0}};route(room,6);
    }
    assert(swat_world_damage(&world,first,1000) && !world.objects[first].active);
    assert(swat_world_ray(&world,(b3Pos){-4,1,1},swat_v(0,0,-1),2,b3_nullBodyId).hit);
    swat_world_close(&world);assert(!world.motel && !world.motel_meshes[0]);
    // A network replica must reconstruct exactly the same non-box geometry.
    static SwatSim server,replica;SwatConfig config=swat_default_config();config.mission=SWAT_MOTEL;config.randomize=false;config.hostile_fire=false;
    swat_sim_init(&server,config,42);static SwatMap map,decoded;static unsigned char bytes[SWAT_NET_PACKET_MAX];
    swat_capture_map(&server,1,&map);size_t length=swat_encode_map(bytes,sizeof(bytes),&map);assert(length && swat_decode_map(&decoded,bytes,length));
    swat_apply_map(&replica,&decoded);assert(replica.world.motel && replica.world.count==server.world.count);
    utility_hits(&replica.world);
    fence_hits(&replica.world);
    for(int i=1;i<=SWAT_MOTEL_INSTANCES;i++) if(server.world.objects[i].active && !server.world.objects[i].door) assert(b3Shape_GetType(replica.world.objects[i].shape)==b3_meshShape);
    for(int i=0;i<4;i++) {
        b3Pos p={-7.2f+4*i,1,1};SwatHit a=swat_world_ray(&server.world,p,swat_v(0,0,-1),2,b3_nullBodyId),b=swat_world_ray(&replica.world,p,swat_v(0,0,-1),2,b3_nullBodyId);
        assert(a.hit && b.hit && a.index==b.index && fabsf(a.distance-b.distance)<1e-5f);
    }
    swat_sim_close(&replica);
    // An actual guest-room wall opens once, with the same physics/nav and wire state.
    b3Pos entry={-4.5f,.06f,-1.17f},exit={-3.5f,.06f,-1.17f},next;
    b3Capsule capsule={.center1={0,.32f,0},.center2={0,1.52f,0},.radius=.3f};
    SwatHit wall=swat_world_ray(&server.world,b3OffsetPos(entry,swat_v(0,1,0)),swat_v(1,0,0),1,b3_nullBodyId);
    assert(wall.hit && swat_world_breachable(&server.world.objects[wall.index]));
    assert(b3World_CastMover(server.world.id,entry,&capsule,b3SubPos(exit,entry),b3DefaultQueryFilter(),NULL,NULL)<.9f);
    for(int i=0;i<server.world.count;i++)if(server.world.objects[i].door)server.world.objects[i].wedge_owner=0;
    server.world.generation++;
    assert(!swat_navigation_next(&server,entry,exit,&next));
    int old_generation=server.world.generation;
    assert(swat_world_breach(&server.world,wall.index,wall.point)>4);
    assert(server.world.generation>old_generation);
    assert(!swat_world_ray(&server.world,b3OffsetPos(entry,swat_v(0,1,0)),swat_v(1,0,0),1,b3_nullBodyId).hit);
    assert(b3World_CastMover(server.world.id,entry,&capsule,b3SubPos(exit,entry),b3DefaultQueryFilter(),NULL,NULL)>.999f);
    assert(swat_navigation_next(&server,entry,exit,&next));
    assert(server.navigation->generation==server.world.generation && server.navigation->updated_cells<SWAT_NAV_CELLS);
    static SwatSnapshot snapshot;
    swat_capture_map(&server,1,&map);swat_capture_snapshot(&server,1,&snapshot);
    swat_apply_map(&replica,&map);assert(swat_apply_snapshot(&replica,&snapshot));
    assert(b3World_CastMover(replica.world.id,entry,&capsule,b3SubPos(exit,entry),b3DefaultQueryFilter(),NULL,NULL)>.999f);
    swat_sim_close(&replica);
    for(int i=0;i<server.world.count;i++)server.world.objects[i].wedge_owner=-1;
    // Original 147-object motel maps keep their original mesh collision.
    SwatMap legacy=decoded;legacy.count=SWAT_MOTEL_BASE_INSTANCES+1;
    swat_apply_map(&replica,&legacy);assert(replica.world.motel && replica.world.count==147);
    swat_sim_close(&replica);
    legacy.count=SWAT_MOTEL_INSTANCES+1;
    swat_apply_map(&replica,&legacy);assert(replica.world.motel && replica.world.objects[106].active);
    utility_hits(&replica.world);swat_sim_close(&replica);
    for(int i=1;i<server.world.count;i++) if(server.world.objects[i].door) server.world.objects[i].door_open=true;
    for(int i=0;i<60;i++) swat_world_step_doors(&server.world);
    swat_capture_map(&server,2,&map); length=swat_encode_map(bytes,sizeof(bytes),&map);
    assert(length && swat_decode_map(&decoded,bytes,length));
    swat_apply_map(&replica,&decoded); assert(replica.world.motel);
    swat_sim_close(&replica);
    decoded.objects[2].center.x+=.1f;
    swat_apply_map(&replica,&decoded); assert(!replica.world.motel);
    for(int i=0;i<SWAT_MOTEL_ASSETS;i++) assert(!replica.world.motel_meshes[i]);
    assert(b3Shape_GetType(replica.world.objects[2].shape)==b3_hullShape);
    swat_sim_close(&replica);swat_sim_reset(&server);assert(server.world.motel);swat_sim_close(&server);
    puts("PASS motel: nine capsule routes, five functional hinged/breachable doors, original mesh openings, exact network collision reconstruction, reset, attached dressing, breach capsule/LOS/nav and replicated destruction");return 0;
}
