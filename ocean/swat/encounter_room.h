// Private room-search implementation, included by encounter.c after the shared
// navigation and directed hands-on tasks. No mission occupant list is a plan.
#ifndef SWAT_ENCOUNTER_ROOM_H
#define SWAT_ENCOUNTER_ROOM_H
static bool room_search_position_plan(SwatSim* s,int actor,const SwatRoom* room,int step,b3Pos* goal,b3Pos feet,const NavPersonProposal* proposal) {
    const SwatMind* mind=&s->actors[actor].mind;
    int row=step/3,column=(row&1)?2-step%3:step%3;
    b3CosSin direction=b3ComputeCosSin(mind->order_yaw);
    float x=fmaxf(0,room->half.x-.65f),z=fmaxf(0,room->half.z-.65f);
    b3Pos target=room->center;target.y-=room->half.y;
    if(fabsf(direction.cosine)>fabsf(direction.sine)) {
        target.x+=(row-1)*x*(direction.cosine<0?-1:1);target.z+=(column-1)*z;
    } else {
        target.x+=(column-1)*x;target.z+=(row-1)*z*(direction.sine<0?-1:1);
    }
    bool found=false;float best=1e9f;
    for(int ring=0;ring<5;ring++)for(int side=0;side<(ring?16:1);side++) {
        float angle=side*SWAT_PI*.125f;
        b3Pos candidate=b3OffsetPos(target,swat_v(cosf(angle)*ring*.4f,0,sinf(angle)*ring*.4f));
        if(fabsf(candidate.x-room->center.x)>room->half.x-.35f || fabsf(candidate.z-room->center.z)>room->half.z-.35f)continue;
        float score=b3Distance(candidate,target)+.02f*b3Distance(candidate,feet);if(score>=best)continue;
        if(!order_support(&s->world,&candidate) || !search_room_route(room,true,candidate) || !actor_lane_clear(s,actor,candidate,candidate))continue;
        b3Pos next;if(!navigation_next_actor_plan(s,actor,feet,candidate,&next,proposal))continue;
        *goal=candidate;best=score;found=true;
    }
    return found;
}
static bool room_search_position(SwatSim* s,int actor,const SwatRoom* room,int step,b3Pos* goal) {
    return room_search_position_plan(s,actor,room,step,goal,swat_body_feet_position(&s->actors[actor].controller.body),NULL);
}
static void room_search_resume(SwatMind* mind) {
    mind->order=SWAT_ORDER_ROOM_SEARCH;mind->room_task=SWAT_ROOM_TASK_NONE;mind->order_actor=0;
    mind->room_goal=false;mind->room_blocked=false;mind->room_tool_ticks=0;mind->replan_tick=0;
}
static bool room_holding_position(SwatSim* s,int actor,const SwatRoom* room,b3Pos person,b3Pos* goal) {
    b3Pos feet=swat_body_feet_position(&s->actors[actor].controller.body);
    for(int outside=1;outside>=0;outside--) {
        b3Pos sites[256];int count=0;bool found=false;float best=1e9f;
        if(outside) {
            // A small furnished room may have no passing bay. Use the actual
            // entry side of a usable doorway, through ordinary escort motion.
            for(int d=0;d<s->world.count;d++) {
                const SwatObject* door=&s->world.objects[d];
                if(!door->active || !door->door || swat_door_loose(door) || door->wedge_owner>=0 || door->breach_owner>=0)continue;
                b3Pos portal=swat_door_center(door,door->closed_yaw);portal.y-=door->half.y;
                b3Vec3 n=swat_v(cosf(door->closed_yaw),0,-sinf(door->closed_yaw));if(b3Dot(n,b3SubPos(room->center,portal))>0)n=swat_mul(n,-1);
                if(!search_room_contains(room,b3OffsetPos(portal,swat_mul(n,-.7f))))continue;
                b3Pos out=b3OffsetPos(portal,swat_mul(n,.7f));if(swat_world_room(&s->world,b3OffsetPos(out,swat_v(0,.9f,0)))>=0)continue;
                b3Vec3 tangent=swat_v(-n.z,0,n.x);
                for(int depth=0;depth<4;depth++)for(int side=-2;side<=2;side++)if(side && count<256)
                    sites[count++]=b3OffsetPos(portal,swat_add(swat_mul(n,2.6f+.4f*depth),swat_mul(tangent,side*.8f)));
            }
        } else {
            float stride=fmaxf(.35f,fmaxf(room->half.x,room->half.z)/7);
            for(float x=-room->half.x+.35f;x<room->half.x-.34f;x+=stride)for(float z=-room->half.z+.35f;z<room->half.z-.34f && count<256;z+=stride)
                sites[count++]=b3OffsetPos(room->center,swat_v(x,-room->half.y,z));
        }
        for(int i=0;i<count;i++) {
            b3Pos point=sites[i];float distance=b3Distance(feet,point);
            if(distance>=best || hypotf(point.x-person.x,point.z-person.z)<1.1f)continue;
            if(!order_support(&s->world,&point) || !search_room_route(room,!outside,point) || !actor_lane_clear(s,actor,point,point))continue;
            b3Pos next;if(!swat_navigation_next_for_actor(s,actor,point,&next))continue;
            b3Vec3 trailing=b3SubPos(person,point);trailing.y=0;
            b3Pos parked=b3OffsetPos(point,swat_mul(swat_normalize(trailing),.75f));parked.y=point.y;
            if(!order_support(&s->world,&parked) || !search_room_route(room,!outside,parked))continue;
            NavPersonProposal proposal={s->actors[actor].mind.order_actor-1,parked};b3Pos resumed;
            int step=s->actors[actor].mind.room_step;if(step>=SWAT_ROOM_SEARCH_POINTS)step=SWAT_ROOM_SEARCH_POINTS-1;
            if(!room_search_position_plan(s,actor,room,step,&resumed,point,&proposal))continue;
            *goal=point;best=distance;found=true;
        }
        if(found)return true;
    }
    return false;
}
static void room_move_person(SwatSim* s,int actor,const SwatRoom* room,SwatInput* in) {
    SwatActor* a=&s->actors[actor];SwatMind* mind=&a->mind;int target=mind->order_actor-1;
    if(target<0 || target>=s->actor_count || !s->actors[target].present || !s->actors[target].alive || !s->actors[target].gear.restrained ||
       (s->actors[target].mind.escort_owner>=0 && s->actors[target].mind.escort_owner!=actor)) {room_search_resume(mind);return;}
    SwatActor* person=&s->actors[target];bool seen=swat_sim_actor_visible(s,actor,target,24,60*SWAT_RAD);
    if(seen)mind->task_point=swat_controller_eye(&person->controller);
    if(mind->room_task==SWAT_ROOM_TASK_REPOSITION && person->mind.escort_owner!=actor){room_search_resume(mind);return;}
    if(!mind->room_goal) {
        if(mind->room_blocked && s->tick<mind->replan_tick)return;
        if(!room_holding_position(s,actor,room,mind->task_point,&mind->room_point)) {mind->room_blocked=true;mind->replan_tick=s->tick+30;return;}
        mind->room_goal=true;mind->room_blocked=false;mind->replan_tick=0;
    }
    b3Vec3 delta=b3SubPos(mind->task_point,swat_controller_eye(&a->controller));
    in->yaw_delta=swat_clamp(swat_angle(b3Atan2(delta.z,delta.x)-a->controller.yaw),-3*SWAT_RAD,3*SWAT_RAD);
    in->pitch_delta=swat_clamp(b3Atan2(delta.y,hypotf(delta.x,delta.z))-a->controller.pitch,-2*SWAT_RAD,2*SWAT_RAD);
    if(mind->room_task==SWAT_ROOM_TASK_ESCORT) {
        if(person->mind.escort_owner==actor){mind->room_task=SWAT_ROOM_TASK_REPOSITION;mind->replan_tick=0;return;}
        SwatHit hand=swat_context_hit(s,actor,2.2f);
        if(hand.kind==SWAT_HIT_ACTOR && hand.index==target && hand.distance<1.7f)in->interact=!a->last_interact;
        else {
            if(seen && (!mind->replan_tick || s->tick%30==0)) {
                b3Pos feet=swat_body_feet_position(&a->controller.body),at=swat_body_feet_position(&person->controller.body),best=feet;float score=1e9f;
                for(int side=0;side<16;side++) {
                    float angle=side*SWAT_PI*.125f;b3Pos candidate=b3OffsetPos(at,swat_v(cosf(angle)*1.05f,0,sinf(angle)*1.05f));candidate.y=room->center.y-room->half.y;
                    float distance=b3Distance(feet,candidate);if(distance>=score || !order_support(&s->world,&candidate) || !search_room_route(room,true,candidate))continue;
                    b3Pos eye=b3OffsetPos(candidate,swat_v(0,a->controller.eye_height,0));b3Vec3 aim=b3SubPos(mind->task_point,eye);
                    SwatHit reach=swat_world_ray(&s->world,eye,swat_normalize(aim),1.7f,a->controller.body.body);b3Pos next;
                    if(reach.kind!=SWAT_HIT_ACTOR || reach.index!=target || !actor_lane_clear(s,actor,candidate,candidate) || !swat_navigation_next_for_actor(s,actor,candidate,&next))continue;
                    score=distance;best=candidate;
                }
                mind->goal=best;mind->replan_tick=0;
            }
            move_to_input(s,actor,mind->goal,in,true);
        }
        return;
    }
    mind->goal=mind->room_point;move_to_input(s,actor,mind->goal,in,true);
    b3Pos feet=swat_body_feet_position(&a->controller.body);
    b3Vec3 approach=b3SubPos(mind->goal,feet);float remaining=hypotf(approach.x,approach.z);
    if(remaining<.45f && remaining>.06f && fabsf(approach.y)<.45f && actor_lane_clear(s,actor,feet,mind->goal) &&
       nav_edge(&s->world,feet,mind->goal,a->controller.body.crouched?1.016f:1.8288f)) {
        b3CosSin turn=b3ComputeCosSin(swat_angle(b3Atan2(approach.z,approach.x)-a->controller.yaw-in->yaw_delta));
        in->forward=.25f*turn.cosine;in->strafe=.25f*turn.sine;
    }
    if(hypotf(feet.x-mind->goal.x,feet.z-mind->goal.z)<.1f && seen) {
        b3Pos at=swat_body_feet_position(&person->controller.body);
        if(hypotf(at.x-mind->goal.x,at.z-mind->goal.z)<.85f) {
            b3Pos resumed;int step=mind->room_step<SWAT_ROOM_SEARCH_POINTS?mind->room_step:SWAT_ROOM_SEARCH_POINTS-1;
            if(!room_search_position(s,actor,room,step,&resumed)){mind->room_goal=false;mind->replan_tick=0;return;}
            SwatHit hand=swat_context_hit(s,actor,2.2f);
            if(hand.kind==SWAT_HIT_ACTOR && hand.index==target && hand.distance<1.7f){in->forward=in->strafe=0;in->interact=!a->last_interact;}
        }
    }
}
static void room_close_door(SwatSim* s,int actor,const SwatRoom* room,SwatInput* in) {
    SwatActor* a=&s->actors[actor];SwatMind* mind=&a->mind;int owner=mind->order_door;
    if(owner<=0 || owner>=s->world.count || !s->world.objects[owner].active || !s->world.objects[owner].door || swat_door_loose(&s->world.objects[owner])){room_search_resume(mind);return;}
    SwatObject* door=&s->world.objects[owner];
    if(!door->door_open && fabsf(door->door_angle)<.01f){room_search_resume(mind);return;}
    if(++mind->room_tool_ticks>240){room_search_resume(mind);mind->room_blocked=true;return;}
    b3Vec3 aim=b3SubPos(door->center,swat_controller_eye(&a->controller));
    in->yaw_delta=swat_clamp(swat_angle(b3Atan2(aim.z,aim.x)-a->controller.yaw),-3*SWAT_RAD,3*SWAT_RAD);
    in->pitch_delta=swat_clamp(b3Atan2(aim.y,hypotf(aim.x,aim.z))-a->controller.pitch,-2*SWAT_RAD,2*SWAT_RAD);
    if(door->door_open) {
        SwatHit hand=swat_context_hit(s,actor,2.2f);if(hand.kind==SWAT_HIT_WORLD && hand.index==owner)in->interact=!a->last_interact;
    } else {
        b3Pos feet=swat_body_feet_position(&a->controller.body);b3Vec3 at=b3SubPos(feet,door->hinge);
        if(hypotf(at.x,at.z)<2*door->half.z+.35f) {
            bool found=false;float best=1e9f;b3Pos goal=feet;
            for(int ring=0;ring<3;ring++)for(int side=0;side<16;side++) {
                float angle=side*SWAT_PI*.125f,radius=.65f+ring*.25f;
                b3Pos point=b3OffsetPos(feet,swat_v(cosf(angle)*radius,0,sinf(angle)*radius));b3Vec3 d=b3SubPos(point,door->hinge);
                if(hypotf(d.x,d.z)<2*door->half.z+.35f || !order_support(&s->world,&point) || !search_room_route(room,true,point) ||
                   !actor_lane_clear(s,actor,feet,point) || !nav_edge(&s->world,feet,point,1.8288f))continue;
                float score=b3Distance(feet,point);if(score<best){best=score;goal=point;found=true;}
            }
            if(found){mind->goal=goal;move_to_input(s,actor,goal,in,true);}
        }
    }
}
static void officer_room_search(SwatSim* s,int actor,SwatInput* in) {
    SwatActor* a=&s->actors[actor];SwatMind* mind=&a->mind;
    if(mind->order_room<=0 || mind->order_room>s->world.room_count) {
        mind->order=SWAT_ORDER_HOLD;mind->order_room=mind->order_actor=mind->order_door=0;mind->room_task=mind->room_step=0;return;
    }
    const SwatRoom* room=&s->world.rooms[mind->order_room-1];
    if(mind->room_task==SWAT_ROOM_TASK_CLOSE_DOOR){room_close_door(s,actor,room,in);return;}
    if(mind->room_task==SWAT_ROOM_TASK_ESCORT || mind->room_task==SWAT_ROOM_TASK_REPOSITION){room_move_person(s,actor,room,in);return;}
    // Subtasks name only an actually observed actor/evidence owner. The shared
    // helpers retain last-seen points, aimed reach, ordinary tools and duration.
    if(mind->room_task) {
        int target=mind->order_actor-1;bool left=false;
        if(target>=0 && target<SWAT_MAX_ACTORS) {
            if(mind->room_task==SWAT_ROOM_TASK_PERSON) {
                if(swat_sim_actor_visible(s,actor,target,24,60*SWAT_RAD))left=!search_room_contains(room,swat_body_feet_position(&s->actors[target].controller.body));
                if(!left)officer_restrain(s,actor,in);
            } else {
                if(evidence_seen(s,actor,target,60*SWAT_RAD))left=!search_room_contains(room,s->evidence[target].position);
                if(!left)officer_search(s,actor,in);
            }
        } else left=true;
        if(left || mind->order==SWAT_ORDER_HOLD)room_search_resume(mind);
        return;
    }
    // Use the actual achieved view throughout movement and each scan. Nothing
    // behind cover becomes a target because the task knows the room's bounds.
    for(int i=0;i<s->actor_count;i++) {
        const SwatActor* person=&s->actors[i];
        if(!person->present || !person->alive || person->gear.restrained ||
           (person->role!=SWAT_SUSPECT && person->role!=SWAT_CIVILIAN))continue;
        if(!swat_sim_actor_visible(s,actor,i,24,60*SWAT_RAD))continue;
        b3Pos point=swat_body_feet_position(&person->controller.body);if(!search_room_contains(room,point))continue;
        mind->room_task=SWAT_ROOM_TASK_PERSON;mind->order_actor=i+1;mind->goal=point;mind->room_goal=false;mind->replan_tick=0;return;
    }
    for(int i=0;i<SWAT_MAX_ACTORS;i++)if(evidence_seen(s,actor,i,60*SWAT_RAD) && search_room_contains(room,s->evidence[i].position)) {
        mind->room_task=SWAT_ROOM_TASK_WEAPON;mind->order_actor=i+1;mind->task_point=mind->goal=s->evidence[i].position;
        mind->room_goal=false;mind->replan_tick=0;return;
    }
    if(mind->room_blocked) {
        // Look for the actual blockage. Only a currently seen cuffed occupant
        // can be asked to move, through the same escort/hold presses as a human.
        for(int i=0;i<s->actor_count;i++) {
            const SwatActor* person=&s->actors[i];
            if(!person->present || !person->alive || !person->gear.restrained || person->mind.escort_owner>=0 ||
               (person->role!=SWAT_CIVILIAN && person->role!=SWAT_SUSPECT) || !swat_sim_actor_visible(s,actor,i,24,60*SWAT_RAD))continue;
            b3Pos point=swat_body_feet_position(&person->controller.body);if(!search_room_contains(room,point))continue;
            mind->room_task=SWAT_ROOM_TASK_ESCORT;mind->order_actor=i+1;mind->task_point=swat_controller_eye(&person->controller);
            mind->room_goal=false;mind->room_blocked=false;mind->replan_tick=0;return;
        }
        SwatHit leaf=swat_context_hit(s,actor,2.2f);
        if(leaf.kind==SWAT_HIT_WORLD && leaf.index>0) {
            const SwatObject* door=&s->world.objects[leaf.index];
            if(door->door && door->door_open && !swat_door_loose(door) && door->breach_owner<0) {
                mind->room_task=SWAT_ROOM_TASK_CLOSE_DOOR;mind->order_door=leaf.index;mind->order_actor=0;mind->room_tool_ticks=0;mind->room_blocked=false;return;
            }
        }
        in->yaw_delta=2*SWAT_RAD;in->pitch_delta=swat_clamp(-.2f-a->controller.pitch,-2*SWAT_RAD,2*SWAT_RAD);
    }
    if(mind->room_step>=SWAT_ROOM_SEARCH_POINTS) {
        mind->order=SWAT_ORDER_HOLD;mind->order_actor=mind->order_door=0;mind->room_task=mind->room_scan=0;
        mind->room_goal=mind->room_blocked=false;
        swat_sound_emit(&s->sounds,s->tick,actor,SWAT_SOUND_COMMAND,swat_controller_eye(&a->controller),.5f,12);return;
    }
    b3Pos feet=swat_body_feet_position(&a->controller.body);
    if(mind->room_blocked) {
        b3Pos opened;
        if(s->tick%30==0 && room_search_position(s,actor,room,mind->room_step,&opened)) {
            mind->room_point=opened;mind->room_goal=true;mind->room_blocked=false;mind->room_tool_ticks=0;mind->replan_tick=0;
        } else {
            // Survey reachable parts while retaining the unfinished region.
            // Moving to a new view can reveal the actual obstructing occupant.
            int probe=(mind->room_tool_ticks/280)%SWAT_ROOM_SEARCH_POINTS;
            if(!mind->room_goal) {
                if(!room_search_position(s,actor,room,probe,&mind->room_point)) {
                    mind->room_tool_ticks=(probe+1)*280;return;
                }
                mind->room_goal=true;mind->replan_tick=0;
            }
            mind->goal=mind->room_point;b3Vec3 d=b3SubPos(mind->goal,feet);
            if(hypotf(d.x,d.z)>.48f || fabsf(d.y)>.45f)move_to(s,actor,mind->goal,in);
            else {
                in->yaw_delta=2*SWAT_RAD;in->pitch_delta=swat_clamp(-.2f-a->controller.pitch,-2*SWAT_RAD,2*SWAT_RAD);
                if(++mind->room_tool_ticks%280==0)mind->room_goal=false;
            }
            return;
        }
    }
    if(mind->room_goal && s->tick%30==0) {
        b3Pos next;if(!actor_lane_clear(s,actor,mind->room_point,mind->room_point) || !swat_navigation_next_for_actor(s,actor,mind->room_point,&next))mind->room_goal=false;
    }
    if(!mind->room_goal) {
        if(mind->room_blocked && s->tick<mind->replan_tick)return;
        if(!room_search_position(s,actor,room,mind->room_step,&mind->room_point)) {
            mind->room_blocked=true;mind->replan_tick=s->tick+30;return;
        }
        mind->room_goal=true;mind->room_blocked=false;mind->replan_tick=0;
    }
    mind->goal=mind->room_point;
    b3Vec3 delta=b3SubPos(mind->goal,feet);
    if(hypotf(delta.x,delta.z)>.48f || fabsf(delta.y)>.45f || !search_room_contains(room,feet)) {
        move_to(s,actor,mind->goal,in);return;
    }
    float yaw=swat_angle(mind->order_yaw+(mind->room_scan/12)*SWAT_PI*.25f);
    in->yaw_delta=swat_clamp(swat_angle(yaw-a->controller.yaw),-3*SWAT_RAD,3*SWAT_RAD);
    in->pitch_delta=swat_clamp(-.45f-a->controller.pitch,-2*SWAT_RAD,2*SWAT_RAD);
    in->forward=in->strafe=0;
    if(fabsf(swat_angle(yaw-a->controller.yaw))<5*SWAT_RAD && fabsf(a->controller.pitch+.45f)<5*SWAT_RAD) {
        if(++mind->room_scan>=96){mind->room_scan=0;mind->room_step++;mind->room_goal=false;mind->replan_tick=0;}
    }
}
#endif
