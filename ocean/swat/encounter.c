#include "sim.h"
#include "locomotion.h"
#include <string.h>

static b3Pos cell_position(const SwatNavigation* nav,int index) {
    return (b3Pos){nav->x[index],nav->height[index],nav->z[index]};
}
static b3Pos grid_position(int index) { return (b3Pos){-8+(index%SWAT_NAV_SIDE+.5f)*.6f,0,-24+(index/SWAT_NAV_SIDE+.5f)*.6f}; }
typedef struct NavQuery { const SwatWorld* world; bool blocked; } NavQuery;
static bool obstacle(b3ShapeId shape,void* context) {
    NavQuery* query=context;
    SwatTag* tag=b3Body_GetUserData(b3Shape_GetBody(shape));
    if(!tag || tag->kind!=SWAT_HIT_WORLD) return true;
    const SwatObject* o=&query->world->objects[tag->index];
    if(o->door && o->wedge_owner<0) return true;
    query->blocked=true; return false;
}
typedef struct NavFloors { float y[SWAT_NAV_LAYERS]; int count; float top,span; } NavFloors;
static float floor_hit(b3ShapeId shape,b3Pos point,b3Vec3 normal,float fraction,uint64_t material,int triangle,int child,void* context) {
    SwatTag* tag=b3Body_GetUserData(b3Shape_GetBody(shape)); NavFloors* floors=context;
    if(!tag || tag->kind!=SWAT_HIT_WORLD || normal.y<.70f) return 1;
    for(int i=0;i<floors->count;i++) if(fabsf(floors->y[i]-(float)point.y)<.05f) return 1;
    if(floors->count<SWAT_NAV_LAYERS) floors->y[floors->count++]=(float)point.y;
    else {
        int lowest=0; for(int i=1;i<SWAT_NAV_LAYERS;i++) if(floors->y[i]<floors->y[lowest]) lowest=i;
        if(point.y>floors->y[lowest]) floors->y[lowest]=(float)point.y;
    }
    return 1;
}
static bool nav_clear(const SwatWorld* world,b3Pos p,float height) {
    b3Vec3 points[]={{0,height*.5f+.1437f,0},{0,fmaxf(height-.2873f,height*.5f+.1537f),0}}; b3ShapeProxy proxy={points,2,.2873f};
    NavQuery query={world,false};
    b3World_OverlapShape(world->id,p,&proxy,b3DefaultQueryFilter(),obstacle,&query);
    // The lower box may step over risers within the same 457 mm controller
    // limit, while solid cover above that limit remains an obstacle.
    b3Vec3 feet[8];
    for(int i=0;i<8;i++) feet[i]=swat_v(i&1 ? .2032f : -.2032f,i&2 ? fmaxf(height*.5f,.49f) : .46f,i&4 ? .2032f : -.2032f);
    b3ShapeProxy lower={feet,8,0};
    if(!query.blocked) b3World_OverlapShape(world->id,p,&lower,b3DefaultQueryFilter(),obstacle,&query);
    return !query.blocked;
}
static float edge_hit(b3ShapeId shape,b3Pos point,b3Vec3 normal,float fraction,uint64_t material,int triangle,int child,void* context) {
    return obstacle(shape,context) ? -1 : 0;
}
static bool nav_edge(const SwatWorld* world,b3Pos a,b3Pos b,float height) {
    float rise=(float)fabs(a.y-b.y);
    if(rise>.4572f) {
        // A grid edge can span several ordinary stair treads. Validate each
        // intermediate support instead of treating their summed rise as a ledge.
        float previous=(float)a.y;
        for(int i=1;i<=4;i++) {
            float t=i*.25f,expected=(float)(a.y+(b.y-a.y)*t);
            b3Pos p={a.x+(b.x-a.x)*t,expected+.6f,a.z+(b.z-a.z)*t}; NavFloors floors={0};
            b3World_CastRay(world->id,p,swat_v(0,-1.2f,0),b3DefaultQueryFilter(),floor_hit,&floors);
            float nearest=1e9f,height_y=0;
            for(int j=0;j<floors.count;j++) { float d=fabsf(floors.y[j]-expected); if(d<nearest) { nearest=d; height_y=floors.y[j]; } }
            if(nearest>.3f || fabsf(height_y-previous)>.4572f) return false;
            previous=height_y;
        }
    }
    // Step up/across/down matches the controller's bounded stair clearance.
    b3Pos raised=a; raised.y=fmax(a.y,b.y)+.015;
    b3Vec3 points[]={{0,height*.5f+.1437f,0},{0,fmaxf(height-.2873f,height*.5f+.1537f),0}}; b3ShapeProxy proxy={points,2,.2873f};
    NavQuery query={world,false}; b3Vec3 translation=b3SubPos(b,raised); translation.y=0;
    b3World_CastShape(world->id,raised,&proxy,translation,b3DefaultQueryFilter(),edge_hit,&query);
    b3Vec3 feet[8];
    for(int i=0;i<8;i++) feet[i]=swat_v(i&1 ? .2032f : -.2032f,i&2 ? fmaxf(height*.5f,.49f) : .46f,i&4 ? .2032f : -.2032f);
    b3ShapeProxy lower={feet,8,0};
    if(!query.blocked) b3World_CastShape(world->id,raised,&lower,translation,b3DefaultQueryFilter(),edge_hit,&query);
    return !query.blocked;
}
static int adjacent(int cell,int direction) {
    int x=cell%SWAT_NAV_SIDE,z=cell/SWAT_NAV_SIDE;
    const int neighbors[]={x>0 ? cell-1 : -1,x+1<SWAT_NAV_SIDE ? cell+1 : -1,z>0 ? cell-SWAT_NAV_SIDE : -1,z+1<SWAT_NAV_SIDE ? cell+SWAT_NAV_SIDE : -1};
    return neighbors[direction];
}
static unsigned char navigation_object_state(const SwatObject* o) {
    return (unsigned char)(o->active ? 1+(o->door && o->wedge_owner>=0) : 0);
}
static void navigation_build(SwatSim* s) {
    SwatNavigation* nav=&s->navigation;
    bool full=!nav->built || nav->count!=s->world.count;
    unsigned char cells[SWAT_NAV_CELLS]={0},edges[SWAT_NAV_CELLS]={0};
    if(full) {
        memset(nav,0,sizeof(*nav)); memset(cells,1,sizeof(cells));
        nav->top=3; nav->bottom=-1;
        for(int i=0;i<s->world.count;i++) if(s->world.objects[i].active) {
            const SwatObject* o=&s->world.objects[i];
            nav->top=fmaxf(nav->top,(float)o->center.y+b3Length(o->half)+1);
            nav->bottom=fminf(nav->bottom,(float)o->center.y-b3Length(o->half)-1);
        }
    } else for(int i=0;i<s->world.count;i++) {
        const SwatObject* o=&s->world.objects[i];
        if(nav->object_state[i]==navigation_object_state(o)) continue;
        // Removal and wedges affect only this footprint, controller clearance
        // and the adjoining edges. Mounted charges/health changes need no work.
        float radius=o->door ? b3Length(o->half)*2 : 0;
        float x=fabsf(cosf(o->yaw))*o->half.x+fabsf(sinf(o->yaw))*o->half.z+1.1f+radius;
        float z=fabsf(sinf(o->yaw))*o->half.x+fabsf(cosf(o->yaw))*o->half.z+1.1f+radius;
        for(int cell=0;cell<SWAT_NAV_CELLS;cell++) {
            b3Pos p=grid_position(cell);
            if(fabs(p.x-o->center.x)<=x && fabs(p.z-o->center.z)<=z) cells[cell]=1;
        }
    }
    nav->updated_cells=0;
    for(int cell=0;cell<SWAT_NAV_CELLS;cell++) if(cells[cell]) {
        nav->updated_cells++; edges[cell]=1;
        for(int d=0;d<4;d++) { int other=adjacent(cell,d); if(other>=0) edges[other]=1; }
        for(int layer=0;layer<SWAT_NAV_LAYERS;layer++) nav->walkable[layer*SWAT_NAV_CELLS+cell]=0;
        b3Pos p=grid_position(cell);
        // Align narrow door apertures before checking physical body clearance.
        for(int j=0;j<s->world.count;j++) {
            const SwatObject* o=&s->world.objects[j]; if(!o->active || !o->door) continue;
            b3Pos center=b3OffsetPos(o->hinge,swat_v(sinf(o->closed_yaw)*o->half.z,0,cosf(o->closed_yaw)*o->half.z));
            b3Vec3 d=b3SubPos(p,center),t=swat_v(sinf(o->closed_yaw),0,cosf(o->closed_yaw)),n=swat_v(cosf(o->closed_yaw),0,-sinf(o->closed_yaw));
            float lateral=b3Dot(d,t);
            if(fabsf(lateral)<.31f && fabsf(b3Dot(d,n))<.7f) p=b3OffsetPos(p,swat_mul(t,-lateral));
        }
        NavFloors floors={0}; p.y=nav->top;
        b3World_CastRay(s->world.id,p,swat_v(0,nav->bottom-nav->top,0),b3DefaultQueryFilter(),floor_hit,&floors);
        for(int i=0;i<floors.count;i++) for(int j=i+1;j<floors.count;j++) if(floors.y[j]<floors.y[i]) { float swap=floors.y[i]; floors.y[i]=floors.y[j]; floors.y[j]=swap; }
        for(int layer=0;layer<floors.count;layer++) {
            int at=layer*SWAT_NAV_CELLS+cell; p.y=floors.y[layer]+.015f;
            nav->height[at]=floors.y[layer]; nav->x[at]=(float)p.x; nav->z[at]=(float)p.z;
            if(nav_clear(&s->world,p,1.016f)) nav->walkable[at]=1;
            if(nav->walkable[at] && nav_clear(&s->world,p,1.8288f)) nav->walkable[at]=3;
        }
    }
    for(int at=0;at<SWAT_NAV_NODES;at++) if(edges[at%SWAT_NAV_CELLS]) {
        memset(nav->links[at],0,sizeof(nav->links[at])); if(!nav->walkable[at]) continue;
        for(int direction=0;direction<4;direction++) {
            int neighbor=adjacent(at%SWAT_NAV_CELLS,direction); if(neighbor<0) continue;
            for(int layer=0;layer<SWAT_NAV_LAYERS;layer++) {
                int other=layer*SWAT_NAV_CELLS+neighbor; if(!nav->walkable[other]) continue;
                float height=(nav->walkable[at]&nav->walkable[other]&2) ? 1.8288f : 1.016f;
                if(nav_edge(&s->world,cell_position(nav,at),cell_position(nav,other),height)) nav->links[at][direction]|=1u<<layer;
            }
        }
    }
    for(int i=0;i<s->world.count;i++) nav->object_state[i]=navigation_object_state(&s->world.objects[i]);
    nav->generation=s->world.generation; nav->count=s->world.count; nav->built_tick=s->tick; nav->built=true;
}
static int nearest(const SwatNavigation* nav,b3Pos point) {
    int best=-1; float distance=2;
    for(int i=0;i<SWAT_NAV_NODES;i++) if(nav->walkable[i]) {
        b3Pos p=cell_position(nav,i);
        if(fabs(p.y-point.y)>.65) continue;
        float d=b3Distance(point,p); if(d<distance) { distance=d; best=i; }
    }
    return best;
}
bool swat_navigation_crouch(const SwatSim* s,b3Pos position) {
    int at=nearest(&s->navigation,position); return at>=0 && !(s->navigation.walkable[at]&2);
}
bool swat_navigation_next(SwatSim* s,b3Pos start,b3Pos goal,b3Pos* next) {
    SwatNavigation* nav=&s->navigation;
    if(!nav->built || nav->generation!=s->world.generation || nav->count!=s->world.count) navigation_build(s);
    int from=nearest(nav,start),to=nearest(nav,goal); if(from<0 || to<0) return false;
    int32_t parent[SWAT_NAV_NODES]; uint16_t queue[SWAT_NAV_NODES];
    for(int i=0;i<SWAT_NAV_NODES;i++) parent[i]=-1;
    int head=0,tail=0; queue[tail++]=(uint16_t)to; parent[to]=to;
    while(head<tail && parent[from]<0) {
        int at=queue[head++];
        for(int direction=0;direction<4;direction++) {
            int cell=adjacent(at%SWAT_NAV_CELLS,direction); if(cell<0) continue;
            for(int layer=0;layer<SWAT_NAV_LAYERS;layer++) {
                int n=layer*SWAT_NAV_CELLS+cell;
                // Reverse traversal; each edge may require a lower stance.
                if((nav->links[n][direction^1]&(1u<<(at/SWAT_NAV_CELLS))) && parent[n]<0) { parent[n]=at; queue[tail++]=(uint16_t)n; }
            }
        }
    }
    if(parent[from]<0) return false;
    *next=cell_position(nav,from==to ? to : parent[from]); return true;
}
static void move_to(SwatSim* s,int index,b3Pos goal,SwatInput* in) {
    SwatActor* a=&s->actors[index]; SwatMind* mind=&a->mind;
    b3Pos feet=swat_body_feet_position(&a->controller.body); b3Vec3 delta=b3SubPos(goal,feet);
    if(hypotf(delta.x,delta.z)<.45f && fabsf(delta.y)<.45f) return;
    if(s->tick>=mind->replan_tick || (hypotf((float)(feet.x-mind->waypoint.x),(float)(feet.z-mind->waypoint.z))<.30f && fabs(feet.y-mind->waypoint.y)<.55)) {
        if(!swat_navigation_next(s,feet,goal,&mind->waypoint)) return;
        mind->replan_tick=s->tick+30;
    }
    in->crouch=swat_navigation_crouch(s,feet) || swat_navigation_crouch(s,mind->waypoint);
    delta=b3SubPos(mind->waypoint,feet); float yaw=atan2f(delta.z,delta.x),error=swat_angle(yaw-a->controller.yaw);
    in->yaw_delta=swat_clamp(error,-3*SWAT_RAD,3*SWAT_RAD);
    if(fabsf(error)<50*SWAT_RAD) in->forward=.7f;
    in->gait=SWAT_SLOW;
    swat_locomotion_input(s,index,mind->waypoint,in);
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
        if(fabsf(d.y)<1 && hypotf(d.x,d.z)<.85f && b3Dot(swat_direction(a->controller.yaw,0),d)>.15f) in->forward=0;
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
        // Preserve the targeted floor; navigation resolves its support height.
        if(hit.kind==SWAT_HIT_WORLD && hit.normal.y<.5f) goal.y=eye.y-source->controller.eye_height;
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
