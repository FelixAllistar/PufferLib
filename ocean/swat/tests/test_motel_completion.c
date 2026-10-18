#include "replay.h"
#include "pose.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

// This driver knows the level plan, like a repeatable human QA route. It only
// sends ordinary player inputs: no actor teleports, forced surrender, evidence
// collection, door state edits or scripted success conditions.
static SwatSim sim;
static SwatReplay journal;
static int watch_actor=-1;
static int contact_overflows;
static bool track_breach,crossed_breach;
static bool squad_restraint;
static int squad_cuffs;
static bool squad_search;
static int squad_weapons;
static const char* search_save;
static bool squad_equipment;
static const char* equipment_save;
static int squad_wedges;
static bool squad_rooms;
static const char* room_save;
static int squad_room_routes;
static b3Pos squad_waiting;
static b3Pos search_waiting;
static float breach_plane;
static void physics_log(const char* message) {
    if(strstr(message,"triangle buffer capacity")) {
        contact_overflows++;
        fprintf(stderr,"Contact overflow at simulation tick %d\n",sim.tick);
    }
    puts(message);
}
static b3Pos feet(int actor) { return swat_body_feet_position(&sim.actors[actor].controller.body); }
static void fail(const char* task) {
    fprintf(stderr,"FAIL %s at tick %d end %d\n",task,sim.tick,sim.end);
    SwatHit h=swat_context_hit(&sim,0,2.2f);SwatController* c=&sim.actors[0].controller;
    fprintf(stderr," context kind%d index%d distance%.3f yaw%.3f pitch%.3f cuffticks%d last%d\n",h.kind,h.index,h.distance,c->yaw,c->pitch,sim.actors[0].gear.cuff_ticks,sim.actors[0].last_interact);
    fprintf(stderr," kit%d primary%d shots%d ammo%d chamber%d reload%d\n",sim.actors[0].gear.kit,sim.actors[0].arsenal.primary,sim.actors[0].arsenal.shots,
        sim.actors[0].arsenal.slots[0].magazine,sim.actors[0].arsenal.slots[0].chambered,sim.actors[0].arsenal.slots[0].reload_remaining);
    fprintf(stderr," muzzleblocked%d ready%.3f active%d\n",sim.actors[0].controller.muzzle_blocked,sim.actors[0].controller.ready_blend,sim.actors[0].arsenal.active);
    for(int i=0;i<sim.actor_count;i++)if(sim.actors[i].present) {
        b3Pos p=feet(i);SwatActor* a=&sim.actors[i];
        fprintf(stderr," actor %d %.3f %.3f %.3f health%.1f surrender%d cuff%d escort%d rescued%d\n",i,(float)p.x,(float)p.y,(float)p.z,a->health,a->gear.surrendered,a->gear.restrained,a->mind.escort_owner,a->rescued);
        fprintf(stderr,"  waypoint %.3f %.3f %.3f replan%d yaw%.3f\n",(float)a->mind.waypoint.x,(float)a->mind.waypoint.y,(float)a->mind.waypoint.z,a->mind.replan_tick,a->controller.yaw);
        fprintf(stderr,"  order%d target%d goal %.3f %.3f %.3f state%d\n",a->mind.order,a->mind.order_actor,
            (float)a->mind.goal.x,(float)a->mind.goal.y,(float)a->mind.goal.z,a->mind.state);
        if(a->role==SWAT_OFFICER) {
            fprintf(stderr,"  perception pitch%.6f heard%.6f ttl%d visible%d shots%d\n",a->controller.pitch,a->heard_yaw,a->hearing_ticks,a->target_actor,a->arsenal.shots);
            SwatHearingMemory probe={0};SwatHeardSound cue;
            for(int n=0;n<128 && swat_hearing_next(&sim.world,&sim.sounds,sim.tick,swat_controller_eye(&a->controller),i,&probe,&cue);n++)
                if(cue.kind==SWAT_SOUND_SHOT)fprintf(stderr,"  audible shot id%u gain%.6f bearing%.6f\n",cue.id,cue.gain,cue.bearing);
        }
    }
    (void)swat_replay_close(&journal);exit(1);
}
static void step(SwatInput in) {
    if(sim.end!=SWAT_RUNNING)fail("unexpected scenario end");
    b3Pos before=feet(0);
    swat_sim_step(&sim,&in);assert(swat_replay_append(&journal,&in,&sim));
    b3Pos after=feet(0);
    if(track_breach && before.x<breach_plane && after.x>=breach_plane && fabsf(after.z+1.17f)<.8f)crossed_breach=true;
    if(!sim.actors[0].alive)fail("officer casualty");
    for(int i=1;i<sim.actor_count;i++)if(sim.actors[i].present && !sim.actors[i].alive && sim.actors[i].role!=SWAT_SUSPECT)
        fail("protected occupant or squad casualty");
}
static void idle(int ticks) { for(int i=0;i<ticks;i++)step(swat_neutral_input()); }
static SwatInput look(b3Pos point) {
    SwatInput in=swat_neutral_input();SwatController* c=&sim.actors[0].controller;
    b3Vec3 d=b3SubPos(point,swat_controller_eye(c));
    in.yaw_delta=swat_clamp(swat_angle(atan2f(d.z,d.x)-c->yaw),-6*SWAT_RAD,6*SWAT_RAD);
    in.pitch_delta=swat_clamp(atan2f(d.y,hypotf(d.x,d.z))-c->pitch,-6*SWAT_RAD,6*SWAT_RAD);
    return in;
}
static b3Pos chest(int actor) { return b3OffsetPos(swat_controller_eye(&sim.actors[actor].controller),swat_v(0,-.2f,0)); }
static bool launcher_lane(int actor) {
    SwatActor* player=&sim.actors[0];SwatPose pose=swat_pose(&player->controller,&player->arsenal);
    SwatHit hit=swat_world_sphere_cast(&sim.world,pose.muzzle,b3SubPos(chest(actor),pose.muzzle),.04f,player->controller.body.body);
    return hit.kind==SWAT_HIT_ACTOR && hit.index==actor;
}
static bool cover_visible_attack(SwatInput* in,float route_yaw) {
    if(!sim.config.hostile_fire)return false;
    static bool confirmed[SWAT_MAX_ACTORS];
    int target=-1;float nearest=24;
    for(int i=0;i<sim.actor_count;i++) {
        const SwatActor* a=&sim.actors[i];
        if(a->role!=SWAT_SUSPECT || !a->present || !a->alive || a->gear.surrendered || a->gear.restrained ||
           !swat_sim_actor_visible(&sim,0,i,24,60*SWAT_RAD))continue;
        if(a->last_shot_tick>=0 && sim.tick-a->last_shot_tick<120)confirmed[i]=true;
        // A visible raised weapon aimed at the team is also an immediate
        // threat. Use observable pose, never the NPC's private target field.
        if(a->controller.ads>.5f)for(int j=0;j<sim.actor_count;j++) {
            const SwatActor* teammate=&sim.actors[j];
            if(!teammate->present || !teammate->alive || teammate->role!=SWAT_OFFICER)continue;
            b3Vec3 to=b3SubPos(swat_controller_eye(&teammate->controller),swat_controller_eye(&a->controller));
            if(b3Dot(swat_controller_aim(&a->controller),swat_normalize(to))>cosf(8*SWAT_RAD))confirmed[i]=true;
        }
        // Resolve an observed shooting threat before resuming the route.
        // A two-second pause between shots does not establish compliance.
        if(!confirmed[i])continue;
        float distance=b3Distance(feet(0),feet(i));if(distance<nearest){nearest=distance;target=i;}
    }
    if(target<0)return false;
    static bool covered[SWAT_MAX_ACTORS];
    if(!covered[target]){printf("Covering visible active shooter %d at tick %d\n",target,sim.tick);fflush(stdout);covered[target]=true;}
    // The route driver must respond to visible attacks from another room,
    // using the same ordinary aim, fire, reload and muzzle-clearance rules.
    // Aim at the exposed point that passed actor visibility, as the ordinary
    // NPC target tracker does. A visible head need not expose the chest.
    b3Pos exposed=swat_controller_eye(&sim.actors[target].controller);
    *in=look(exposed);in->aim=true;
    // Track the actual reticle with ordinary bounded mouse input. Leaving the
    // nominal camera on the target while recoil lifts the reticle delayed
    // follow-up shots until the controller recovered by itself.
    const SwatController* camera=&sim.actors[0].controller;b3Vec3 aim=b3SubPos(exposed,swat_controller_eye(camera));
    in->yaw_delta=swat_clamp(swat_angle(atan2f(aim.z,aim.x)-camera->yaw-camera->recoil_yaw),-6*SWAT_RAD,6*SWAT_RAD);
    in->pitch_delta=swat_clamp(atan2f(aim.y,hypotf(aim.x,aim.z))-camera->pitch-camera->recoil_pitch,-6*SWAT_RAD,6*SWAT_RAD);
    in->fire=!sim.actors[0].arsenal.last_fire && swat_sim_firing_line_clear(&sim,0,target,24);
    in->reload=!sim.actors[0].arsenal.slots[sim.actors[0].arsenal.active].chambered;
    if(sim.actors[0].controller.muzzle_blocked) {
        in->fire=false;float delta=route_yaw-sim.actors[0].controller.yaw;
        in->forward=.6f*cosf(delta);in->strafe=.6f*sinf(delta);
    }
    return true;
}
static void walk_until(b3Pos goal,float radius,int restrained_actor) {
    // A 60 cm cell centre can be sqrt(2)*30 cm from the requested point.
    // Breach plane crossing, cuffs and extraction are checked independently.
    radius=fmaxf(radius,.43f);
    b3Pos waypoint=goal;int replan=0;
    for(int t=0;t<3000;t++) {
        if(restrained_actor>=0 && sim.actors[restrained_actor].gear.restrained)return;
        b3Pos p=feet(0);b3Vec3 d=b3SubPos(goal,p);
        if(hypotf(d.x,d.z)<radius && fabsf(d.y)<.4f)return;
        SwatInput yield=swat_neutral_input();
        if(swat_navigation_yield_door(&sim,0,&yield)) {step(yield);replan=0;continue;}
        if(t>=replan || b3Distance(p,waypoint)<.3f) {
            if(!swat_navigation_next_for_actor(&sim,0,goal,&waypoint)) {
                // Look toward the intended route while waiting for a person
                // or leaf to clear. Remaining aimed backward can pin recalled
                // followers in the doorway behind the commander's old view.
                step(look(b3OffsetPos(goal,swat_v(0,1.65f,0))));continue;
            }
            replan=t+30;
        }
        SwatInput in=look(b3OffsetPos(waypoint,swat_v(0,1.65f,0)));
        float yaw=atan2f(waypoint.z-p.z,waypoint.x-p.x);
        if(fabsf(swat_angle(yaw-sim.actors[0].controller.yaw))<35*SWAT_RAD)in.forward=.8f;
        // Keep this delegated-task route at the team's existing formation
        // pace, instead of running the commander away from their coverage.
        in.gait=squad_restraint&&sim.config.hostile_fire?SWAT_SLOW:SWAT_WALK;
        in.crouch=swat_navigation_crouch(&sim,p)||swat_navigation_crouch(&sim,waypoint);
        bool defending=cover_visible_attack(&in,yaw);
        if(!defending && watch_actor>=0 && sim.actors[watch_actor].alive && !sim.actors[watch_actor].gear.surrendered) {
            b3Pos eye=swat_controller_eye(&sim.actors[0].controller);b3Vec3 to=b3SubPos(chest(watch_actor),eye);
            SwatHit visible=swat_world_ray(&sim.world,eye,swat_normalize(to),b3Length(to)+.1f,sim.actors[0].controller.body.body);
            if(visible.kind==SWAT_HIT_ACTOR && visible.index==watch_actor) {
                in=look(chest(watch_actor)); // Stop and cover the exposed occupant before advancing.
                SwatHit aimed=swat_world_ray(&sim.world,eye,swat_controller_aim(&sim.actors[0].controller),7,sim.actors[0].controller.body.body);
                if(aimed.kind==SWAT_HIT_ACTOR && aimed.index==watch_actor) {
                    in.command=t%15==0;
                    in.taser=sim.actors[watch_actor].role==SWAT_SUSPECT && !sim.actors[0].gear.taser_cooldown && !sim.actors[0].gear.last_taser;
                    if(sim.actors[0].gear.kit==2 && sim.actors[watch_actor].role==SWAT_SUSPECT) {
                        // Quiet breach QA selects the real impact launcher at
                        // staging; Entry has no taser charges. Combat keeps its carbine.
                        in.aim=true;in.fire=(sim.config.hostile_fire || sim.actors[0].arsenal.primary==2) && !sim.actors[0].arsenal.last_fire && !sim.actors[watch_actor].gear.stunned_ticks;
                        in.reload=!sim.actors[0].arsenal.slots[0].chambered;
                        // Camera sight can clear a jamb before this longer
                        // primary's muzzle does. Continue the planned approach
                        // in the current aim basis until the gun clears cover.
                        if(sim.actors[0].controller.muzzle_blocked || (sim.actors[0].arsenal.primary==2 && !launcher_lane(watch_actor))) {
                            in.fire=false;
                            float delta=yaw-sim.actors[0].controller.yaw;
                            in.forward=.6f*cosf(delta);in.strafe=.6f*sinf(delta);
                        }
                    }
                }
            }
        }
        SwatHit hit=swat_context_hit(&sim,0,1.7f);
        if(!defending && !track_breach && hit.kind==SWAT_HIT_WORLD && hit.index>=0 && sim.world.objects[hit.index].door) {
            SwatObject* door=&sim.world.objects[hit.index];
            if(door->locked) {
                in.forward=0;
                if(swat_kit(sim.actors[0].gear.kit)->ram)in.melee=!sim.actors[0].gear.last_melee;
                else in.door_tool=SWAT_LOCKPICK;
            }
            else if(!door->door_open) {in.forward=0;in.interact=!sim.actors[0].last_interact;}
        }
        step(in);
    }
    fprintf(stderr," walk goal %.3f %.3f %.3f waypoint %.3f %.3f %.3f\n",(float)goal.x,(float)goal.y,(float)goal.z,(float)waypoint.x,(float)waypoint.y,(float)waypoint.z);
    SwatTraceResult trace=swat_body_trace_body(&sim.actors[0].controller.body,feet(0),waypoint,1,1);
    if(trace.hit) {SwatTag* tag=b3Body_GetUserData(b3Shape_GetBody(trace.shapeId));if(tag)fprintf(stderr," blocked trace kind%d owner%d normal %.2f %.2f %.2f\n",tag->kind,tag->index,trace.normal.x,trace.normal.y,trace.normal.z);}
    for(int h=0;h<4;h++) {
        SwatHit ray=swat_world_ray(&sim.world,b3OffsetPos(feet(0),swat_v(0,.2f+h*.4f,0)),swat_direction(sim.actors[0].controller.yaw,0),1,sim.actors[0].controller.body.body);
        fprintf(stderr," forward y%.2f kind%d owner%d distance%.3f\n",.2f+h*.4f,ray.kind,ray.index,ray.distance);
    }
    fail("walking timeout");
}
static void walk(b3Pos goal,float radius) {walk_until(goal,radius,-1);}
static void delegate_room(float door_x) {
    b3Pos point={door_x,1.1f,0};SwatInput in=swat_neutral_input();in.squad_order=SWAT_ORDER_HOLD;step(in);idle(20);
    for(int n=0;n<60;n++)step(look(point));
    in=look(point);in.squad_order=SWAT_ORDER_ROOM_SEARCH;in.squad_queue=true;step(in);
    int recipient=-1;
    for(int i=3;i<6;i++)if(sim.actors[i].mind.pending_order==SWAT_ORDER_ROOM_SEARCH) {
        if(recipient>=0)fail("multiple room search recipients");
        recipient=i;
    }
    if(recipient<0 || sim.actors[recipient].mind.pending_room!=1)fail("authored room search recipient");
    static SwatSim restored;char error[256];assert(swat_replay_checkpoint(&journal,room_save));
    assert(swat_replay_restore(&restored,room_save,error,sizeof(error)));
    assert(swat_replay_digest(&restored)==swat_replay_digest(&sim));
    assert(restored.actors[recipient].mind.pending_room==1 && restored.actors[recipient].mind.queued);
    swat_sim_close(&restored);
    puts("PASS queued room search survives journal checkpoint/restore with its room identity");fflush(stdout);
    bool restrained[SWAT_MAX_ACTORS];for(int i=0;i<sim.actor_count;i++)restrained[i]=sim.actors[i].gear.restrained;
    in=look(point);in.squad_execute=true;step(in);
    SwatMind* mind=&sim.actors[recipient].mind;
    for(int n=0;n<8000 && (mind->order!=SWAT_ORDER_HOLD || mind->room_step!=SWAT_ROOM_SEARCH_POINTS);n++) {
        in=look(point);cover_visible_attack(&in,sim.actors[0].controller.yaw);step(in);
    }
    if(mind->order!=SWAT_ORDER_HOLD || mind->room_step!=SWAT_ROOM_SEARCH_POINTS || mind->order_room!=1)fail("authored room route search");
    int cuffs=0;
    for(int i=0;i<sim.actor_count;i++)if(!restrained[i] && sim.actors[i].gear.restrained){cuffs++;squad_cuffs++;}
    squad_room_routes++;
    printf("Squad officer %d searched room route 1 at tick %d with %d observed restraints\n",recipient,sim.tick,cuffs);fflush(stdout);
    if(squad_restraint){in=look(point);in.squad_order=SWAT_ORDER_FALL_IN;step(in);idle(30);}
}
static void delegate_wedge(float door_x) {
    int owner=-1;
    for(int i=1;i<sim.world.count;i++)if(sim.world.objects[i].door) {
        b3Pos closed=swat_door_center(&sim.world.objects[i],sim.world.objects[i].closed_yaw);
        if(fabsf(closed.x-door_x)<.1f && fabsf(closed.z)<.1f)owner=i;
    }
    if(owner<0 || sim.world.objects[owner].wedge_owner>=0)fail("available authored wedge target");
    SwatObject* door=&sim.world.objects[owner];
    if(door->door_open) {
        walk((b3Pos){door_x,0,.75f},.3f);
        for(int n=0;n<60;n++)step(look(door->center));
        for(int n=0;n<240 && (door->door_open || fabsf(door->door_angle)>.001f);n++) {
            SwatInput close=look(door->center);SwatHit hand=swat_context_hit(&sim,0,2.2f);
            close.interact=door->door_open && hand.kind==SWAT_HIT_WORLD && hand.index==owner && !sim.actors[0].last_interact;step(close);
        }
        if(door->door_open || fabsf(door->door_angle)>.001f)fail("close authored equipment door");
        // Back away while watching the leaf; the generic walking helper opens
        // closed doors on its route and could reopen it during the view turn.
        for(int n=0;n<240 && feet(0).z<2.3f;n++) {
            SwatInput back=look(door->center);back.forward=-.5f;back.gait=SWAT_WALK;step(back);
        }
        if(feet(0).z<2.3f || door->door_open)fail("back clear of closed equipment door");
    }
    SwatInput in=swat_neutral_input();in.squad_order=SWAT_ORDER_HOLD;step(in);idle(20);
    for(int n=0;n<60;n++)step(look(door->center));
    SwatHit hit=swat_context_hit(&sim,0,24);
    if(hit.kind!=SWAT_HIT_WORLD || hit.index!=owner)fail("visible authored wedge target");
    in=look(door->center);in.squad_order=SWAT_ORDER_WEDGE;in.squad_team=1;in.squad_queue=true;step(in);
    int recipient=-1;
    for(int i=3;i<6;i++)if(sim.actors[i].mind.pending_order==SWAT_ORDER_WEDGE && sim.actors[i].mind.pending_door==owner) {
        if(recipient>=0)fail("multiple wedge recipients");
        recipient=i;
    }
    if(recipient<0)fail("squad wedge recipient");
    static SwatSim restored;char error[256];assert(swat_replay_checkpoint(&journal,equipment_save));
    assert(swat_replay_restore(&restored,equipment_save,error,sizeof(error)));
    assert(swat_replay_digest(&restored)==swat_replay_digest(&sim));
    assert(restored.actors[recipient].mind.pending_door==owner && restored.actors[recipient].mind.queued);
    swat_sim_close(&restored);
    puts("PASS queued wedge survives journal checkpoint/restore with the authored door identity");fflush(stdout);
    // Clear the real working space while the task is still queued.
    walk((b3Pos){door_x+1.2f,0,1.0f},.3f);
    in=look(door->center);in.squad_execute=true;step(in);
    for(int n=0;n<3000 && (door->wedge_owner!=recipient || sim.actors[recipient].mind.order!=SWAT_ORDER_HOLD);n++)step(look(door->center));
    if(door->wedge_owner!=recipient || door->door_open || sim.actors[recipient].mind.order!=SWAT_ORDER_HOLD || sim.actors[recipient].mind.order_door || !sim.actors[recipient].gear.used_tools)fail("squad authored door wedge");
    squad_wedges++;
    // Recall the operator out of the working space before ordinary removal.
    in=look(door->center);in.squad_order=SWAT_ORDER_FALL_IN;in.squad_team=1;step(in);idle(90);
    // Remove the actual wedge using ordinary human reach/tool input.
    walk((b3Pos){door_x,0,1.0f},.3f);
    for(int n=0;n<60;n++)step(look(door->center));
    for(int n=0;n<240 && door->wedge_owner>=0;n++) {
        in=look(door->center);in.door_tool=SWAT_REMOVE_WEDGE;step(in);
    }
    if(door->wedge_owner>=0 || sim.actors[recipient].gear.wedges!=1)fail("human removal of squad wedge");
    in=look(door->center);in.squad_order=SWAT_ORDER_FALL_IN;step(in);
    printf("Squad officer %d wedged door %d at tick %d\n",recipient,owner,sim.tick);fflush(stdout);
}
static void secure(int actor) {
    if(!sim.actors[actor].alive) {
        if(sim.actors[actor].role!=SWAT_SUSPECT)fail("protected occupant killed");
        return; // A lawful squad response can end an armed threat before cuffing.
    }
    if(sim.actors[actor].gear.restrained)return;
    if(sim.actors[0].arsenal.primary==2 && !sim.actors[actor].gear.surrendered && !launcher_lane(actor))walk(feet(actor),1.45f);
    for(int t=0;t<600 && sim.actors[actor].alive && !sim.actors[actor].gear.surrendered;t++) {
        SwatInput in=look(chest(actor));SwatHit hit=swat_context_hit(&sim,0,9);
        if(hit.kind==SWAT_HIT_ACTOR && hit.index==actor) {
            in.command=t%30==0;
            in.taser=sim.actors[actor].role==SWAT_SUSPECT && t%30==15;
            if(sim.actors[0].gear.kit==2 && sim.actors[actor].role==SWAT_SUSPECT) {
                in.aim=true;in.fire=(sim.config.hostile_fire || sim.actors[0].arsenal.primary==2) && !sim.actors[0].arsenal.last_fire && !sim.actors[actor].gear.stunned_ticks;
                in.reload=!sim.actors[0].arsenal.slots[0].chambered;
            }
        }
        step(in);
    }
    if(!sim.actors[actor].alive){if(sim.actors[actor].role!=SWAT_SUSPECT)fail("protected occupant killed");return;}
    if(!sim.actors[actor].gear.surrendered)fail("compliance");
    walk(feet(actor),1.45f);
    for(int t=0;t<60;t++)step(look(chest(actor)));
    if(squad_restraint) {
        SwatInput hold=look(chest(actor));hold.squad_order=SWAT_ORDER_HOLD;step(hold);idle(20);
        SwatInput order=look(chest(actor));order.squad_order=SWAT_ORDER_CUFF;step(order);
        int recipient=-1;
        for(int i=3;i<6;i++)if(sim.actors[i].mind.pending_order==SWAT_ORDER_CUFF && sim.actors[i].mind.pending_actor==actor+1) {
            if(recipient>=0)fail("multiple cuff recipients");
            recipient=i;
        }
        if(recipient<0)fail("squad cuff recipient");
        // The commander was standing in the doorway after covering this
        // occupant. Clear that actual capsule obstruction before asking the
        // officer to pass; the test never grants collision exceptions.
        // Stop clearing space as soon as the real cuff completes; a blocked
        // furniture-adjacent waiting point cannot invalidate a finished task.
        walk_until(squad_waiting,.43f,actor);
        for(int t=0;t<2400 && !sim.actors[actor].gear.restrained;t++)step(look(chest(actor)));
        if(!sim.actors[actor].gear.restrained || sim.actors[recipient].gear.cuff_target!=actor)fail("squad cuffing");
        squad_cuffs++;
        // Release the covering positions before escorting through the same
        // narrow entrance; a completed restraint deliberately holds position.
        SwatInput follow=look(chest(actor));follow.squad_order=SWAT_ORDER_FALL_IN;step(follow);
        printf("Squad officer %d secured actor %d at tick %d\n",recipient,actor,sim.tick);fflush(stdout);
        return;
    }
    for(int t=0;t<180 && !sim.actors[actor].gear.restrained;t++) {
        SwatInput in=look(chest(actor));SwatHit hit=swat_context_hit(&sim,0,1.7f);
        in.interact=hit.kind==SWAT_HIT_ACTOR && hit.index==actor;step(in);
    }
    if(!sim.actors[actor].gear.restrained)fail("cuffing");
    idle(1);
    printf("Secured actor %d at tick %d\n",actor,sim.tick);fflush(stdout);
}
static void collect(int actor) {
    // This QA plan moves arrested suspects clear before recovering weapons.
    // Use normal escort/hold commands; body interaction keeps its priority.
    if(sim.actors[actor].alive && sim.actors[actor].gear.restrained) {
        // Keep the actual 1.7 m hand-ray test below as the reach authority.
        // A supported cell beside a bed/person need not be within 1 m of feet.
        walk(feet(actor),1.45f);
        for(int n=0;n<120 && sim.actors[actor].mind.escort_owner!=0;n++) {
            SwatInput follow=look(chest(actor));follow.interact=n%2==0;step(follow);
        }
        if(sim.actors[actor].mind.escort_owner!=0)fail("clear evidence escort");
        walk(sim.extraction,.35f);
        for(int n=0;n<3000 && b3Distance(feet(0),feet(actor))>1.0f;n++)idle(1);
        if(b3Distance(feet(0),feet(actor))>1.3f)fail("suspect did not clear room");
        for(int n=0;n<120 && sim.actors[actor].mind.escort_owner==0;n++) {
            SwatInput hold=look(chest(actor));hold.interact=n%2==0;step(hold);
        }
        if(sim.actors[actor].mind.escort_owner==0)fail("hold suspect clear of evidence");
    }
    for(int t=0;t<240 && !sim.evidence[actor].collected;t++) {
        if(t==60) {
            b3Pos evidence=sim.evidence[actor].position;
            // Furniture can occupy any one side of dropped evidence. Choose a
            // physically reachable floor sample within ordinary interaction
            // range rather than requiring the player to stand inside a prop.
            b3Pos approach={0};bool found=false;float best=1e9f;
            for(int side=0;side<48;side++) {
                float angle=(side%16)*SWAT_PI*.125f,radius=.75f+(side/16)*.3f;
                b3Pos candidate=b3OffsetPos(evidence,swat_v(cosf(angle)*radius,0,sinf(angle)*radius)),next;
                candidate.y=feet(0).y;
                if(!swat_navigation_next_for_actor(&sim,0,candidate,&next))continue;
                int cell=swat_navigation_nearest(sim.navigation,candidate);if(cell<0)continue;
                b3Pos floor={sim.navigation->x[cell],sim.navigation->height[cell],sim.navigation->z[cell]};
                if(hypotf(floor.x-evidence.x,floor.z-evidence.z)>1.55f)continue;
                b3Pos eye=b3OffsetPos(floor,swat_v(0,1.65f,0));
                if(b3Distance(eye,evidence)>=2.15f || !swat_world_visible(&sim.world,eye,evidence))continue;
                SwatHit aimed=swat_world_ray(&sim.world,eye,swat_normalize(b3SubPos(evidence,eye)),2.2f,sim.actors[0].controller.body.body);
                if(aimed.kind==SWAT_HIT_ACTOR || aimed.kind==SWAT_HIT_DEVICE)continue;
                if(aimed.kind==SWAT_HIT_WORLD && aimed.index>=0 &&
                   (sim.world.objects[aimed.index].door || swat_world_breachable(&sim.world.objects[aimed.index])))continue;
                float distance=b3Distance(feet(0),floor);if(distance<best){approach=floor;best=distance;found=true;}
            }
            if(!found)fail("reachable evidence approach");
            walk(approach,.43f);
            // This goal is already an exact, checked floor sample. Finish the
            // last part of the approach using ordinary controller input.
            for(int n=0;n<120 && hypotf(feet(0).x-approach.x,feet(0).z-approach.z)>.1f;n++) {
                SwatInput move=look(b3OffsetPos(approach,swat_v(0,1.65f,0)));
                float yaw=atan2f(approach.z-feet(0).z,approach.x-feet(0).x);
                if(fabsf(swat_angle(yaw-sim.actors[0].controller.yaw))<20*SWAT_RAD)move.forward=.25f;
                move.gait=SWAT_WALK;move.crouch=swat_navigation_crouch(&sim,approach);step(move);
            }
        }
        if(squad_search && t==61) {
            // Walking turns the actual view. Finish the bounded input turn
            // before issuing an order that names what the commander sees.
            for(int n=0;n<60;n++)step(look(sim.evidence[actor].position));
            SwatInput order=look(sim.evidence[actor].position);order.squad_order=SWAT_ORDER_SEARCH;order.squad_queue=true;step(order);
            int recipient=-1;
            for(int i=3;i<6;i++)if(sim.actors[i].mind.pending_order==SWAT_ORDER_SEARCH && sim.actors[i].mind.pending_actor==actor+1) {
                if(recipient>=0)fail("multiple search recipients");
                recipient=i;
            }
            if(recipient<0)fail("squad search recipient");
            if(!squad_weapons) {
                static SwatSim restored;char error[256];assert(swat_replay_checkpoint(&journal,search_save));
                assert(swat_replay_restore(&restored,search_save,error,sizeof(error)));
                assert(swat_replay_digest(&restored)==swat_replay_digest(&sim));
                assert(restored.actors[recipient].mind.pending_actor==actor+1 && restored.actors[recipient].mind.queued);
                assert(b3Distance(restored.actors[recipient].mind.pending_goal,sim.actors[recipient].mind.pending_goal)<1e-6f);
                swat_sim_close(&restored);puts("PASS queued evidence order survives journal checkpoint/restore with its target and reported position");fflush(stdout);
            }
            // The commander clears the physical working space, then gives the
            // queued officer the ordinary Execute pulse from outside the room.
            walk(search_waiting,.43f);order=look(sim.evidence[actor].position);order.squad_execute=true;step(order);
            for(int n=0;n<3000 && !sim.evidence[actor].collected;n++)step(look(sim.evidence[actor].position));
            if(!sim.evidence[actor].collected)fail("squad weapon retrieval");
            for(int n=0;n<30 && sim.actors[recipient].mind.order!=SWAT_ORDER_HOLD;n++)idle(1);
            if(sim.actors[recipient].mind.order!=SWAT_ORDER_HOLD || sim.actors[recipient].mind.order_actor)fail("squad search completion");
            squad_weapons++;order=look(sim.evidence[actor].position);order.squad_order=SWAT_ORDER_FALL_IN;step(order);
            printf("Squad officer %d recovered weapon %d at tick %d\n",recipient,actor,sim.tick);fflush(stdout);return;
        }
        SwatInput in=look(sim.evidence[actor].position);SwatContext context=swat_context(&sim,0);
        if(squad_search){step(in);continue;}
        in.interact=context.action==SWAT_CONTEXT_EVIDENCE && context.hit.index==actor && t%2==0;step(in);
    }
    if(!sim.evidence[actor].collected)fail("collect evidence");
}
static void evacuate(int actor,bool exterior) {
    for(int t=0;t<120 && sim.actors[actor].mind.escort_owner!=0;t++) {
        SwatInput in=look(chest(actor));in.interact=t%2==0;step(in);
    }
    if(sim.actors[actor].mind.escort_owner!=0)fail("start escort");
    if(exterior) {
        walk((b3Pos){-14,0,-1.17f},.4f);
        for(int t=0;t<1800 && feet(actor).x>=-12.2f;t++)idle(1);
        if(feet(actor).x>=-12.2f)fail("civilian did not follow through exterior breach");
        walk((b3Pos){-13,0,2.5f},.4f);
    } else walk((b3Pos){feet(0).x,0,2.5f},.3f);
    walk(sim.extraction,.35f);
    for(int t=0;t<3000 && !sim.actors[actor].rescued;t++)idle(1);
    if(!sim.actors[actor].rescued)fail("evacuation");
    printf("Evacuated actor %d at tick %d\n",actor,sim.tick);fflush(stdout);
}
static void hold_breach_team(b3Pos charge) {
    if(!squad_restraint)return;
    SwatInput in=swat_neutral_input();in.squad_order=SWAT_ORDER_HOLD;step(in);
    for(int t=0;t<120;t++) {
        bool held=true;
        for(int i=3;i<6;i++)held&=sim.actors[i].mind.order==SWAT_ORDER_HOLD && !sim.actors[i].mind.pending_order;
        if(held) {
            for(int i=3;i<6;i++)if(b3Distance(chest(i),charge)<3.2f)fail("squad inside planned charge exposure");
            printf("Squad holding outside charge exposure at tick %d\n",sim.tick);fflush(stdout);return;
        }
        idle(1);
    }
    fail("squad breach hold acknowledgement");
}
static void enter_breach(float wall,b3Pos retreat) {
    // Staging need not occupy a specific 60 cm grid centre beside a surrendered
    // person. Actual tool reach, finite placement and aperture crossing are
    // independently checked below using the ordinary player inputs.
    b3Pos start={wall-.8f,0,-1.17f},aim={wall,1,-1.17f};walk(start,.6f);
    for(int i=0;i<35;i++)step(look(aim));
    SwatHit hit=swat_context_hit(&sim,0,1.7f);
    if(hit.kind!=SWAT_HIT_WORLD || !swat_world_breachable(&sim.world.objects[hit.index]) || sim.world.objects[hit.index].door)fail("select masonry charge surface");
    int charges=sim.actors[0].gear.wall_charges,destroyed=sim.totals.destroyed;
    for(int t=0;t<180 && sim.actors[0].gear.wall_charges==charges;t++) {
        SwatInput in=look(aim);in.door_tool=SWAT_PLACE_CHARGE;step(in);
    }
    if(sim.actors[0].gear.wall_charges!=charges-1)fail("mount finite charge");
    idle(1);walk(retreat,.4f);
    if(squad_restraint)for(int t=0;t<35;t++)step(look(aim));
    SwatInput detonate=squad_restraint?look(aim):swat_neutral_input();detonate.door_tool=SWAT_DETONATE_CHARGE;step(detonate);
    if(squad_restraint)for(int t=0;t<180;t++) {
        SwatInput cover=look(aim);b3Pos p=feet(0);
        (void)cover_visible_attack(&cover,atan2f(aim.z-p.z,aim.x-p.x));step(cover);
    } else idle(180);
    if(sim.totals.destroyed<=destroyed)fail("remote masonry breach");
    walk(start,.6f);
    if(wall>-10 && sim.actors[watch_actor].gear.surrendered && b3Distance(feet(0),feet(watch_actor))<2) {
        secure(watch_actor);
        for(int t=0;t<120 && sim.actors[watch_actor].mind.escort_owner!=0;t++) {
            SwatInput follow=look(chest(watch_actor));follow.interact=t%2==0;step(follow);
        }
        if(sim.actors[watch_actor].mind.escort_owner!=0)fail("start arrested suspect escort");
        walk((b3Pos){wall-3.2f,0,2.5f},.43f);
        walk((b3Pos){wall-2,0,3.7f},.43f);
        for(int t=0;t<900 && b3Distance(feet(0),feet(watch_actor))>.85f;t++)idle(1);
        for(int t=0;t<120 && sim.actors[watch_actor].mind.escort_owner==0;t++) {
            SwatInput hold=look(chest(watch_actor));hold.interact=t%2==0;step(hold);
        }
        if(sim.actors[watch_actor].mind.escort_owner==0)fail("hold arrested suspect clear of breach");
        walk((b3Pos){wall-3.2f,0,2.5f},.43f);
        walk((b3Pos){wall-3.2f,0,-.85f},.43f);
        walk(start,.6f);
    }
    if(wall>-10) {
        // An occupant can swing the front leaf across this side-wall aperture.
        // Close that real, reachable obstruction before choosing this entry.
        b3Pos leaf={wall+.26f,1,-.54f};
        for(int t=0;t<35;t++)step(look(leaf));
        SwatHit obstruction=swat_context_hit(&sim,0,1.7f);
        if(obstruction.kind==SWAT_HIT_WORLD && sim.world.objects[obstruction.index].door && sim.world.objects[obstruction.index].door_open) {
            SwatInput close=look(leaf);close.interact=true;step(close);idle(120);
            printf("Closed obstructing leaf %d through breach at tick %d\n",obstruction.index,sim.tick);fflush(stdout);
        }
    }
    bool door_open[SWAT_MAX_OBJECTS];
    for(int i=0;i<sim.world.count;i++)door_open[i]=sim.world.objects[i].door_open;
    track_breach=true;crossed_breach=false;breach_plane=wall;
    walk((b3Pos){wall+.9f,0,-1.17f},.4f);track_breach=false;
    if(!crossed_breach)fail("entry detoured around breach");
    for(int i=0;i<sim.world.count;i++)if(sim.world.objects[i].door && !door_open[i] && sim.world.objects[i].door_open)fail("breach entry opened a door detour");
    printf("PASS actual player charge entry through x=%.1f at tick %d: placement, retreat, remote detonation and physical aperture crossing\n",wall,sim.tick);fflush(stdout);
}
int main(int argc,char** argv) {
    b3SetLogFcn(physics_log);
    assert(argc>=2 && argc<=9);bool combat=false,seed_given=false;const char* route="front";uint32_t seed=81;
    for(int i=2;i<argc;i++) {
        if(!strcmp(argv[i],"combat"))combat=true;
        else if(!strcmp(argv[i],"squad"))squad_restraint=true;
        else if(!strcmp(argv[i],"search"))squad_search=true;
        else if(!strcmp(argv[i],"equipment"))squad_equipment=true;
        else if(!strcmp(argv[i],"rooms"))squad_rooms=true;
        else if(!strcmp(argv[i],"exterior")||!strcmp(argv[i],"interroom"))route=argv[i];
        else if(!strncmp(argv[i],"seed=",5)) {
            const char* digits=argv[i]+5;assert(!seed_given&&digits[0]&&strspn(digits,"0123456789")==strlen(digits));
            errno=0;char* end;unsigned long value=strtoul(digits,&end,10);assert(!errno&&!*end&&value<=UINT32_MAX);
            seed=(uint32_t)value;seed_given=true;
        }
        else assert(false);
    }
    SwatConfig cfg=swat_default_config();cfg.mission=SWAT_MOTEL;swat_config_human(&cfg);
    cfg.randomize=false;cfg.hostile_fire=combat;cfg.max_ticks=60000;
    char save[1024];assert(snprintf(save,sizeof(save),"%s.save",argv[1])<(int)sizeof(save));
    char task_save[1024];assert(snprintf(task_save,sizeof(task_save),"%s.search.save",argv[1])<(int)sizeof(task_save));search_save=task_save;
    char wedge_save[1024];assert(snprintf(wedge_save,sizeof(wedge_save),"%s.wedge.save",argv[1])<(int)sizeof(wedge_save));equipment_save=wedge_save;
    char route_save[1024];assert(snprintf(route_save,sizeof(route_save),"%s.room.save",argv[1])<(int)sizeof(route_save));room_save=route_save;
    assert(!squad_rooms || !strcmp(route,"front"));
    int saved_tick=0;uint32_t saved_hash=0;
    swat_sim_init(&sim,cfg,seed);assert(swat_replay_record(&journal,argv[1],&sim));
    printf("Motel QA seed %u initial resolve suspect1=%.6f civilian2=%.6f civilian6=%.6f civilian7=%.6f suspect8=%.6f\n",seed,sim.actors[1].mind.resolve,sim.actors[2].mind.resolve,sim.actors[6].mind.resolve,sim.actors[7].mind.resolve,sim.actors[8].mind.resolve);fflush(stdout);
    SwatInput in=swat_neutral_input();in.squad_order=squad_restraint?SWAT_ORDER_FALL_IN:SWAT_ORDER_HOLD;
    if(strcmp(route,"front")) {
        in.loadout=3; // Entry's actual finite wall stock and ram.
        if(!combat)in.primary_profile=3; // Select LL impact through the ordinary staging input.
    }
    step(in);idle(30);
    if(strcmp(route,"front"))assert(sim.actors[0].gear.kit==2 && sim.actors[0].arsenal.primary==(combat?0:2));
    const int occupants[]={7,2,1,6,8};const float doors[]={-10.9f,-7.2f,-3.2f,.8f,4.8f};
    for(int i=0;i<5;i++) {
        if(squad_restraint) {in=swat_neutral_input();in.squad_order=SWAT_ORDER_FALL_IN;step(in);}
        squad_waiting=(b3Pos){doors[i]+1.2f,0,-.65f};
        search_waiting=(b3Pos){doors[i],0,2.5f};
        watch_actor=occupants[i];
        if(i==0 && !strcmp(route,"exterior")) {
            walk((b3Pos){-13,0,2.5f},.3f);hold_breach_team((b3Pos){-12,1,-1.17f});
            enter_breach(-12,(b3Pos){-15.5f,0,-1.17f});
            if(squad_restraint){in=swat_neutral_input();in.squad_order=SWAT_ORDER_FALL_IN;step(in);}
        } else if(i==2 && !strcmp(route,"interroom")) {
            hold_breach_team((b3Pos){-4,1,-1.17f});
            walk((b3Pos){doors[1],0,2.5f},.3f);
            walk((b3Pos){doors[1],0,-.85f},.3f);
            enter_breach(-4,(b3Pos){-7.2f,0,2.5f});
            if(squad_restraint){in=swat_neutral_input();in.squad_order=SWAT_ORDER_FALL_IN;step(in);}
        } else {
            walk((b3Pos){doors[i],0,2.5f},.3f);
            if(squad_rooms && i==0)delegate_room(doors[i]);
            walk((b3Pos){doors[i],0,-.85f},.3f);
        }
        secure(occupants[i]);
        if(sim.actors[occupants[i]].role==SWAT_CIVILIAN)evacuate(occupants[i],i==0&&!strcmp(route,"exterior"));
        else {collect(occupants[i]);walk((b3Pos){doors[i],0,2.5f},.3f);}
        if(i==2) {assert(swat_replay_checkpoint(&journal,save));saved_tick=sim.tick;saved_hash=swat_replay_digest(&sim);}
    }
    // Account for armed suspects before asking an officer to put its hands on
    // equipment. Visible threats correctly interrupt this work.
    if(squad_equipment) {watch_actor=-1;walk((b3Pos){doors[1],0,2.5f},.3f);delegate_wedge(doors[1]);}
    walk(sim.extraction,.35f);in=swat_neutral_input();in.squad_order=SWAT_ORDER_FALL_IN;step(in);
    for(int t=0;t<3000 && sim.end==SWAT_RUNNING;t++)idle(1);
    if(sim.end!=SWAT_SUCCESS)fail("completion");
    int dead_suspects=0;
    for(int i=0;i<sim.actor_count;i++)if(sim.actors[i].present && !sim.actors[i].alive) {
        if(sim.actors[i].role!=SWAT_SUSPECT || sim.actors[i].gear.restrained)fail("protected casualty at debrief");
        dead_suspects++;
    }
    if(sim.debrief.arrests+dead_suspects!=2 || sim.debrief.rescued!=3 || sim.debrief.evidence!=2 || sim.debrief.roe_violations) {
        fprintf(stderr,"debrief arrests%d dead%d rescued%d evidence%d roe%d unlawful%.2f\n",sim.debrief.arrests,dead_suspects,sim.debrief.rescued,sim.debrief.evidence,sim.debrief.roe_violations,sim.debrief.unlawful_damage);
        fail("truthful lawful debrief");
    }
    // Entry choice does not script the encounter outcome. Hostile runs permit
    // lawful squad return fire on any approach; the checks above still reject
    // protected deaths, restrained deaths, unlawful force and missing evidence.
    if(!combat)assert(!dead_suspects);
    if(squad_restraint)assert(squad_cuffs==5-dead_suspects);
    if(squad_search)assert(squad_weapons==2);
    if(squad_equipment)assert(squad_wedges==1);
    if(squad_rooms)assert(squad_room_routes==1);
    assert(swat_replay_close(&journal));
    printf("PASS motel complete player-input run (%s, %s route): %d ticks, %d arrests, %d armed suspects killed, three evacuations, two weapons, squad regroup, no civilian/officer deaths or unlawful force\n",cfg.hostile_fire?"hostile fire enabled":"interaction isolation",route,sim.tick,sim.debrief.arrests,dead_suspects);fflush(stdout);
    static SwatSim replica;static SwatMap map,map_decoded;static SwatSnapshot snapshot,decoded;static unsigned char bytes[SWAT_NET_PACKET_MAX];
    swat_capture_map(&sim,1,&map);size_t size=swat_encode_map(bytes,sizeof(bytes),&map);
    assert(size && swat_decode_map(&map_decoded,bytes,size));swat_apply_map(&replica,&map_decoded);swat_capture_snapshot(&sim,1,&snapshot);
    size=swat_encode_snapshot(bytes,sizeof(bytes),&snapshot);assert(size && swat_decode_snapshot(&decoded,bytes,size));
    assert(swat_apply_snapshot(&replica,&decoded));
    assert(replica.end==SWAT_SUCCESS && !memcmp(&replica.debrief,&sim.debrief,sizeof(sim.debrief)));
    assert(swat_sim_progress(&replica).secured && !swat_sim_progress(&replica).officers_away);
    swat_sim_close(&replica);swat_sim_close(&sim);
    SwatReplay reader;assert(swat_replay_open(&reader,argv[1]));swat_sim_init(&sim,reader.config,reader.seed);
    uint32_t expected;int status;
    while((status=swat_replay_next(&reader,&in,&expected))==1) {
        swat_sim_step(&sim,&in);
        if(swat_replay_digest(&sim)!=expected)fail("input replay divergence");
    }
    assert(status==0 && sim.end==SWAT_SUCCESS && swat_replay_close(&reader));swat_sim_close(&sim);
    char error[256];assert(swat_replay_restore(&sim,save,error,sizeof(error)));
    assert(sim.tick==saved_tick && swat_replay_digest(&sim)==saved_hash);
    assert(swat_replay_open(&reader,argv[1]));
    while((status=swat_replay_next(&reader,&in,&expected))==1) {
        if((int)reader.frame<=saved_tick)continue;
        swat_sim_step(&sim,&in);
        if(swat_replay_digest(&sim)!=expected)fail("saved continuation divergence");
    }
    assert(status==0 && sim.end==SWAT_SUCCESS && swat_replay_close(&reader));
    swat_sim_close(&sim);assert(!remove(save));
    assert(!contact_overflows);
    puts("PASS full completion: exact every-tick replay, mid-run save/resume, fresh replica geometry/state, truthful final debrief and no contact-buffer overflow");return 0;
}
