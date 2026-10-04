#include "sim.h"
#include <string.h>

static b3Pos cell_position(int index) { return (b3Pos){-8+(index%SWAT_NAV_SIDE+.5f)*.6f,0,-24+(index/SWAT_NAV_SIDE+.5f)*.6f}; }
static int cell_index(b3Pos point) {
    int x=(int)floorf((point.x+8)/.6f),z=(int)floorf((point.z+24)/.6f);
    return x>=0 && x<SWAT_NAV_SIDE && z>=0 && z<SWAT_NAV_SIDE ? z*SWAT_NAV_SIDE+x : -1;
}
typedef struct NavQuery { const SwatWorld* world; bool blocked; } NavQuery;
static bool obstacle(b3ShapeId shape,void* context) {
    NavQuery* query=context;
    SwatTag* tag=b3Body_GetUserData(b3Shape_GetBody(shape));
    if(!tag || tag->kind!=SWAT_HIT_WORLD) return true;
    const SwatObject* o=&query->world->objects[tag->index];
    if(o->door && o->wedge_owner<0) return true; // Route through doors; use actual opening/picking on approach.
    query->blocked=true; return false;
}
static void navigation_build(SwatSim* s) {
    SwatNavigation* nav=&s->navigation;
    memset(nav->walkable,0,sizeof(nav->walkable));
    b3Vec3 points[]={{0,.38f,0},{0,1.42f,0}}; b3ShapeProxy proxy={points,2,.25f};
    for(int i=0;i<SWAT_NAV_CELLS;i++) {
        b3Pos p=cell_position(i); bool supported=false;
        for(int j=0;j<s->world.count;j++) {
            const SwatObject* floor=&s->world.objects[j];
            if(floor->active && floor->center.y+floor->half.y<.08f && floor->center.y+floor->half.y>-.1f &&
                fabsf((float)(p.x-floor->center.x))<floor->half.x-.25f && fabsf((float)(p.z-floor->center.z))<floor->half.z-.25f) { supported=true; break; }
        }
        if(!supported) continue;
        NavQuery query={&s->world,false};
        b3World_OverlapShape(s->world.id,p,&proxy,b3DefaultQueryFilter(),obstacle,&query);
        nav->walkable[i]=!query.blocked;
    }
    nav->generation=s->world.generation; nav->count=s->world.count; nav->built_tick=s->tick; nav->built=true;
}
static int nearest(const SwatNavigation* nav,b3Pos point) {
    int at=cell_index(point); if(at>=0 && nav->walkable[at]) return at;
    int best=-1; float distance=4;
    for(int i=0;i<SWAT_NAV_CELLS;i++) if(nav->walkable[i]) {
        float d=b3Distance(point,cell_position(i)); if(d<distance) { distance=d; best=i; }
    }
    return best;
}
bool swat_navigation_next(SwatSim* s,b3Pos start,b3Pos goal,b3Pos* next) {
    SwatNavigation* nav=&s->navigation;
    if(!nav->built || nav->generation!=s->world.generation || nav->count!=s->world.count) navigation_build(s);
    int from=nearest(nav,start),to=nearest(nav,goal); if(from<0 || to<0) return false;
    int16_t parent[SWAT_NAV_CELLS]; uint16_t queue[SWAT_NAV_CELLS];
    for(int i=0;i<SWAT_NAV_CELLS;i++) parent[i]=-1;
    int head=0,tail=0; queue[tail++]=(uint16_t)to; parent[to]=(int16_t)to;
    while(head<tail && parent[from]<0) {
        int at=queue[head++],x=at%SWAT_NAV_SIDE,z=at/SWAT_NAV_SIDE;
        int neighbors[]={x>0 ? at-1 : -1,x+1<SWAT_NAV_SIDE ? at+1 : -1,z>0 ? at-SWAT_NAV_SIDE : -1,z+1<SWAT_NAV_SIDE ? at+SWAT_NAV_SIDE : -1};
        for(int k=0;k<4;k++) { int n=neighbors[k]; if(n>=0 && nav->walkable[n] && parent[n]<0) { parent[n]=(int16_t)at; queue[tail++]=(uint16_t)n; } }
    }
    if(parent[from]<0) return false;
    *next=from==to ? goal : cell_position(parent[from]); return true;
}
static void move_to(SwatSim* s,int index,b3Pos goal,SwatInput* in) {
    SwatActor* a=&s->actors[index]; SwatMind* mind=&a->mind;
    b3Pos feet=swat_body_feet_position(&a->controller.body); b3Vec3 delta=b3SubPos(goal,feet);
    if(hypotf(delta.x,delta.z)<.65f) return;
    if(s->tick>=mind->replan_tick || b3Distance(feet,mind->waypoint)<.3f) {
        if(!swat_navigation_next(s,feet,goal,&mind->waypoint)) return;
        mind->replan_tick=s->tick+30;
    }
    delta=b3SubPos(mind->waypoint,feet); float yaw=atan2f(delta.z,delta.x),error=swat_angle(yaw-a->controller.yaw);
    in->yaw_delta=swat_clamp(error,-3*SWAT_RAD,3*SWAT_RAD);
    if(fabsf(error)<50*SWAT_RAD) in->forward=.7f;
    in->gait=SWAT_SLOW;
    // Interact only with the first actual leaf encountered. Never teleport through a planned route.
    SwatHit ahead=swat_context_hit(s,index,1.7f);
    if(ahead.kind==SWAT_HIT_WORLD && ahead.index>=0 && s->world.objects[ahead.index].door) {
        SwatObject* door=&s->world.objects[ahead.index];
        if(door->wedge_owner>=0) { in->forward=0; mind->replan_tick=0; return; }
        if(door->locked) { in->forward=0; if(a->role==SWAT_OFFICER) in->door_tool=SWAT_LOCKPICK; return; }
        if(!door->door_open) { in->forward=0; in->interact=!a->last_interact; }
    }
    // Yield for teammates close ahead; keep the commanded goal.
    for(int i=0;i<s->actor_count;i++) if(i!=index && s->actors[i].present && s->actors[i].alive) {
        b3Vec3 d=b3SubPos(swat_body_feet_position(&s->actors[i].controller.body),feet);
        if(hypotf(d.x,d.z)<.75f && b3Dot(swat_direction(a->controller.yaw,0),d)>.15f) in->forward=0;
    }
}
static int visible_target(SwatSim* s,int index,bool officer) {
    SwatActor* a=&s->actors[index]; b3Pos eye=swat_controller_eye(&a->controller);
    int best=-1; float closest=24;
    for(int i=0;i<s->actor_count;i++) {
        SwatActor* target=&s->actors[i]; if(i==index || !target->present || !target->alive) continue;
        if(officer ? target->role!=SWAT_SUSPECT || target->gear.surrendered : (target->role!=SWAT_OFFICER && target->role!=SWAT_SNIPER)) continue;
        b3Pos head=swat_controller_eye(&target->controller); b3Vec3 delta=b3SubPos(head,eye); float distance=b3Length(delta);
        if(distance>=closest || b3Dot(swat_controller_aim(&a->controller),swat_normalize(delta))<cosf(60*SWAT_RAD)) continue;
        SwatHit hit=swat_world_ray(&s->world,eye,swat_normalize(delta),distance+.15f,a->controller.body.body);
        if(hit.kind==SWAT_HIT_ACTOR && hit.index==i) { best=i; closest=distance; }
    }
    return best;
}
static void face_target(SwatSim* s,int index,int target,SwatInput* in) {
    SwatActor* a=&s->actors[index]; b3Vec3 d=b3SubPos(swat_controller_eye(&s->actors[target].controller),swat_controller_eye(&a->controller));
    float yaw=atan2f(d.z,d.x),pitch=atan2f(d.y,hypotf(d.x,d.z));
    in->yaw_delta=swat_clamp(swat_angle(yaw-a->controller.yaw),-3*SWAT_RAD,3*SWAT_RAD);
    in->pitch_delta=swat_clamp(pitch-a->controller.pitch,-2*SWAT_RAD,2*SWAT_RAD); in->aim=true;
    a->target_actor=target; a->visible_ticks++;
    bool threat=a->role==SWAT_SUSPECT || (s->tick-s->actors[target].last_shot_tick<120);
    // The first ray must be that suspect: an officer or hostage crossing the muzzle inhibits the shot.
    SwatHit hit=swat_world_ray(&s->world,swat_controller_eye(&a->controller),swat_controller_aim(&a->controller),24,a->controller.body.body);
    in->fire=threat && s->config.hostile_fire && a->visible_ticks>36 && hit.kind==SWAT_HIT_ACTOR && hit.index==target && s->tick%18==0;
    in->reload=!a->arsenal.slots[a->arsenal.active].chambered;
}
const char* swat_squad_order_name(int order) {
    static const char* names[]={"","Fall in","Hold","Move","Stack","Clear","Pick lock","Wedge","Restrain","Search","Cover"};
    return names[order>=0 && order<SWAT_SQUAD_ORDERS ? order : 0];
}
void swat_encounter_orders(SwatSim* s,const SwatInput inputs[]) {
    if(!s->config.tactical_rules) return;
    int leader=s->commander_actor; if(leader<0 || leader>=s->actor_count || !s->actors[leader].alive) return;
    const SwatInput* in=&inputs[leader];
    bool edge=in->squad_order && in->squad_order!=s->last_squad_order;
    s->last_squad_order=in->squad_order;
    if(edge && in->squad_order>0 && in->squad_order<SWAT_SQUAD_ORDERS) {
        SwatActor* source=&s->actors[leader]; b3Pos eye=swat_controller_eye(&source->controller);
        SwatHit hit=swat_context_hit(s,leader,24); b3Pos goal=hit.point;
        if(hit.kind==SWAT_HIT_WORLD) goal=b3OffsetPos(hit.point,swat_mul(hit.normal,.6f));
        goal.y=0;
        swat_sound_emit(&s->sounds,s->tick,leader,SWAT_SOUND_COMMAND,eye,1,20);
        for(int i=0;i<s->actor_count;i++) {
            SwatActor* a=&s->actors[i]; if(!a->present || !a->alive || !a->mind.bot || a->role!=SWAT_OFFICER || (in->squad_team && in->squad_team!=a->mind.team)) continue;
            a->mind.pending_order=in->squad_order; a->mind.pending_goal=goal;
            a->mind.queued=in->squad_queue; a->mind.command_tick=s->tick+18; // Radio acknowledgement delay.
        }
    }
    for(int i=0;i<s->actor_count;i++) {
        SwatMind* mind=&s->actors[i].mind;
        if(!mind->pending_order || mind->command_tick>s->tick || (mind->queued && !in->squad_execute)) continue;
        mind->order=mind->pending_order; mind->goal=mind->pending_goal; mind->pending_order=0; mind->queued=false; mind->replan_tick=0;
        swat_sound_emit(&s->sounds,s->tick,i,SWAT_SOUND_COMMAND,swat_controller_eye(&s->actors[i].controller),.5f,12);
    }
}
void swat_encounter_inputs(SwatSim* s,SwatInput inputs[]) {
    for(int i=0;i<s->actor_count;i++) {
        SwatActor* a=&s->actors[i]; SwatMind* mind=&a->mind;
        if(!a->present || !a->alive || (a->role==SWAT_OFFICER && !mind->bot) || a->role==SWAT_SNIPER) continue;
        SwatInput* in=&inputs[i];
        if(a->gear.stunned_ticks) continue;
        if(a->gear.restrained) {
            mind->state=SWAT_DETAINED;
            if(a->role==SWAT_CIVILIAN && mind->escort_owner>=0 && mind->escort_owner<s->actor_count && s->actors[mind->escort_owner].alive) {
                mind->state=SWAT_ESCORT; in->crouch=false;
                move_to(s,i,swat_body_feet_position(&s->actors[mind->escort_owner].controller.body),in);
            }
            continue;
        }
        if(a->gear.surrendered) { mind->state=SWAT_SURRENDER; continue; }
        int target=visible_target(s,i,a->role==SWAT_OFFICER);
        if(target>=0) {
            if(mind->target!=target) a->visible_ticks=0;
            mind->target=target; mind->memory=swat_controller_eye(&s->actors[target].controller); mind->memory_ticks=300;
            if(a->role!=SWAT_CIVILIAN) { mind->state=SWAT_ENGAGE; face_target(s,i,target,in); continue; }
            mind->state=SWAT_FLEE;
            b3Pos feet=swat_body_feet_position(&a->controller.body);
            b3Vec3 away=swat_normalize(b3SubPos(feet,mind->memory)); mind->goal=b3OffsetPos(feet,swat_mul(away,3));
            move_to(s,i,mind->goal,in); continue;
        }
        a->visible_ticks=0; a->target_actor=-1; mind->target=-1;
        SwatHeardSound heard;
        for(int n=0;n<4 && swat_hearing_next(&s->world,&s->sounds,s->tick,swat_controller_eye(&a->controller),i,&a->hearing,&heard);n++) {
            if(heard.gain<.02f || (heard.kind!=SWAT_SOUND_SHOT && heard.kind!=SWAT_SOUND_BREAK && heard.kind!=SWAT_SOUND_DOOR && heard.kind!=SWAT_SOUND_FLASH && heard.kind!=SWAT_SOUND_HANDLE)) continue;
            // Hearing supplies bearing, not a hidden actor location. Investigate a coarse point four metres along it.
            mind->memory=b3OffsetPos(swat_body_feet_position(&a->controller.body),swat_mul(swat_direction(heard.bearing,0),4)); mind->memory_ticks=180;
        }
        if(a->role==SWAT_OFFICER) {
            mind->state=SWAT_IDLE;
            if(mind->order==SWAT_ORDER_FALL_IN || mind->order==0) {
                if(s->actors[s->commander_actor].alive) {
                    b3Pos goal=swat_body_feet_position(&s->actors[s->commander_actor].controller.body);
                    goal=b3OffsetPos(goal,swat_v(-1.2f,(float)0,(float)(i-4)*1.0f)); move_to(s,i,goal,in);
                }
            } else if(mind->order!=SWAT_ORDER_HOLD && mind->order!=SWAT_ORDER_COVER) {
                move_to(s,i,mind->goal,in);
                SwatContext c=swat_context(s,i);
                if(mind->order==SWAT_ORDER_PICK && c.action==SWAT_CONTEXT_LOCKED && c.ready) in->door_tool=SWAT_LOCKPICK;
                if(mind->order==SWAT_ORDER_WEDGE && c.hit.kind==SWAT_HIT_WORLD && c.ready) in->door_tool=SWAT_WEDGE;
                if(mind->order==SWAT_ORDER_CUFF) { if(c.action==SWAT_CONTEXT_CUFF && c.ready) in->interact=true; else in->command=s->tick%90==0; }
                if(mind->order==SWAT_ORDER_SEARCH) in->interact=s->tick%30==0;
            }
        } else if(mind->memory_ticks>0) {
            mind->memory_ticks--; mind->state=SWAT_INVESTIGATE;
            if(a->role==SWAT_SUSPECT) move_to(s,i,mind->memory,in);
        } else mind->state=SWAT_IDLE;
    }
}
bool swat_collect_evidence(SwatSim* s,int actor) {
    if(!s->config.tactical_rules || s->actors[actor].role!=SWAT_OFFICER) return false;
    b3Pos eye=swat_controller_eye(&s->actors[actor].controller); b3Vec3 aim=swat_controller_aim(&s->actors[actor].controller);
    for(int i=0;i<SWAT_MAX_ACTORS;i++) {
        SwatEvidence* e=&s->evidence[i]; if(!e->dropped || e->collected) continue;
        b3Vec3 delta=b3SubPos(e->position,eye);
        if(b3Length(delta)>2.2f || b3Dot(aim,swat_normalize(delta))<cosf(12*SWAT_RAD) || !swat_world_visible(&s->world,eye,e->position)) continue;
        e->collected=true; s->debrief.evidence++; swat_sound_emit(&s->sounds,s->tick,actor,SWAT_SOUND_HANDLE,e->position,.2f,5); return true;
    }
    return false;
}
void swat_encounter_step(SwatSim* s) {
    if(!s->config.tactical_rules) return;
    s->debrief.arrests=0; s->debrief.rescued=0;
    for(int i=0;i<s->actor_count;i++) {
        SwatActor* a=&s->actors[i]; if(!a->present) continue;
        s->debrief.arrests+=a->role==SWAT_SUSPECT && a->gear.restrained;
        if(a->role==SWAT_SUSPECT && (!a->alive || a->gear.surrendered) && !s->evidence[i].dropped) {
            s->evidence[i]=(SwatEvidence){true,false,i,b3OffsetPos(swat_body_feet_position(&a->controller.body),swat_v(.35f,.055f,0))};
        }
        if(a->role==SWAT_CIVILIAN && a->gear.restrained && b3Distance(swat_body_feet_position(&a->controller.body),s->extraction)<2) a->rescued=true;
        s->debrief.rescued+=a->role==SWAT_CIVILIAN && a->rescued;
    }
}
