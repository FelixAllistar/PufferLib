#include "sim.h"
#include "locomotion.h"
#include <stdlib.h>
#include <string.h>

static b3Pos cell_position(const SwatNavigation* nav,int index) {
    return (b3Pos){nav->x[index],nav->height[index],nav->z[index]};
}
static SwatNavigation* navigation_create(const SwatWorld* world) {
    int width=world->motel?216:SWAT_NAV_SIDE,depth=world->motel?168:SWAT_NAV_SIDE;
    int cells=width*depth,nodes=cells*SWAT_NAV_LAYERS;
    // Float/index arrays first preserve alignment; all pointers live in this
    // allocation so existing reset/map replacement/close free it exactly once.
    size_t bytes=sizeof(SwatNavigation)+(size_t)nodes*(5*sizeof(uint32_t)+5)+(size_t)cells*2;
    SwatNavigation* nav=calloc(1,bytes);if(!nav)return NULL;
    unsigned char* at=(unsigned char*)(nav+1);
    nav->height=(float*)at;at+=(size_t)nodes*sizeof(float);
    nav->x=(float*)at;at+=(size_t)nodes*sizeof(float);
    nav->z=(float*)at;at+=(size_t)nodes*sizeof(float);
    nav->parent=(int32_t*)at;at+=(size_t)nodes*sizeof(int32_t);
    nav->queue=(uint32_t*)at;at+=(size_t)nodes*sizeof(uint32_t);
    nav->walkable=at;at+=nodes;nav->links=(unsigned char(*)[4])at;at+=(size_t)nodes*4;
    nav->dirty_cells=at;nav->dirty_edges=at+cells;
    nav->width=width;nav->depth=depth;nav->cells=cells;nav->nodes=nodes;
    // -64.4 preserves the original -32m indoor X sampling phase.
    nav->min_x=world->motel?-64.4f:-8;nav->min_z=world->motel?-48:-24;
    return nav;
}
static b3Pos grid_position(const SwatNavigation* nav,int index) {
    if(nav->width==216) {
        // Preserve the previous indoor arithmetic, not merely its nominal
        // phase. Reassociating floats after shifting the origin changed some
        // doorway samples enough to alter controller arrival and encounters.
        int x=index%nav->width-54,z=index/nav->width-40;
        return (b3Pos){-32+(x+.5f)*.6f,0,-24+(z+.5f)*.6f};
    }
    return (b3Pos){nav->min_x+(index%nav->width+.5f)*.6f,0,nav->min_z+(index/nav->width+.5f)*.6f};
}
typedef struct NavQuery { const SwatWorld* world; bool blocked; } NavQuery;
static bool obstacle(b3ShapeId shape,void* context) {
    NavQuery* query=context;
    SwatTag* tag=b3Body_GetUserData(b3Shape_GetBody(shape));
    if(!tag || tag->kind!=SWAT_HIT_WORLD) return true;
    const SwatObject* o=&query->world->objects[tag->index];
    if(o->door && o->wedge_owner<0 && o->door_angle<SWAT_PI*.5f-.03f) return true;
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
static int adjacent(const SwatNavigation* nav,int cell,int direction) {
    int x=cell%nav->width,z=cell/nav->width;
    const int neighbors[]={x>0 ? cell-1 : -1,x+1<nav->width ? cell+1 : -1,z>0 ? cell-nav->width : -1,z+1<nav->depth ? cell+nav->width : -1};
    return neighbors[direction];
}
static unsigned char navigation_object_state(const SwatObject* o) {
    return (unsigned char)(o->active ? 1+(o->door && o->wedge_owner>=0)+
        4*(o->door && o->door_angle>=SWAT_PI*.5f-.03f) : 0);
}
static bool navigation_sample(const SwatWorld* world,SwatNavigation* nav,int cell,b3Pos p) {
    NavFloors floors={0};p.y=nav->top;
    b3World_CastRay(world->id,p,swat_v(0,nav->bottom-nav->top,0),b3DefaultQueryFilter(),floor_hit,&floors);
    for(int i=0;i<floors.count;i++)for(int j=i+1;j<floors.count;j++)if(floors.y[j]<floors.y[i]) {
        float swap=floors.y[i];floors.y[i]=floors.y[j];floors.y[j]=swap;
    }
    bool complete=floors.count>0;
    for(int layer=0;layer<floors.count;layer++) {
        p.y=floors.y[layer]+.015f;unsigned char walk=0;
        if(nav_clear(world,p,1.016f))walk=1;
        if(walk && nav_clear(world,p,1.8288f))walk=3;
        if(!walk) {complete=false;continue;}
        int slot=-1;bool existing=false;
        for(int l=0;l<SWAT_NAV_LAYERS;l++) {
            int at=l*nav->cells+cell;
            if(nav->walkable[at] && fabsf(nav->height[at]-floors.y[layer])<.05f)existing=true;
            if(!nav->walkable[at] && slot<0)slot=at;
        }
        if(existing || slot<0)continue;
        nav->height[slot]=floors.y[layer];nav->x[slot]=(float)p.x;nav->z[slot]=(float)p.z;nav->walkable[slot]=walk;
    }
    return complete;
}
static void navigation_build(SwatSim* s) {
    SwatNavigation* nav=s->navigation;
    bool full=!nav->built || nav->count!=s->world.count;
    unsigned char *cells=nav->dirty_cells,*edges=nav->dirty_edges;
    memset(cells,0,(size_t)nav->cells);memset(edges,0,(size_t)nav->cells);
    if(full) {
        memset(nav->walkable,0,(size_t)nav->nodes);memset(cells,1,(size_t)nav->cells);
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
        for(int cell=0;cell<nav->cells;cell++) {
            b3Pos p=grid_position(nav,cell);
            if(fabs(p.x-o->center.x)<=x && fabs(p.z-o->center.z)<=z) cells[cell]=1;
        }
    }
    nav->updated_cells=0;
    for(int cell=0;cell<nav->cells;cell++) if(cells[cell]) {
        nav->updated_cells++; edges[cell]=1;
        for(int d=0;d<4;d++) { int other=adjacent(nav,cell,d); if(other>=0) edges[other]=1; }
        for(int layer=0;layer<SWAT_NAV_LAYERS;layer++) nav->walkable[layer*nav->cells+cell]=0;
        b3Pos p=grid_position(nav,cell);
        // Align narrow door apertures before checking physical body clearance.
        for(int j=0;j<s->world.count;j++) {
            const SwatObject* o=&s->world.objects[j]; if(!o->active || !o->door) continue;
            b3Pos center=b3OffsetPos(o->hinge,swat_v(sinf(o->closed_yaw)*o->half.z,0,cosf(o->closed_yaw)*o->half.z));
            b3Vec3 d=b3SubPos(p,center),t=swat_v(sinf(o->closed_yaw),0,cosf(o->closed_yaw)),n=swat_v(cosf(o->closed_yaw),0,-sinf(o->closed_yaw));
            float lateral=b3Dot(d,t);
            if(fabsf(lateral)<.31f && fabsf(b3Dot(d,n))<.7f) p=b3OffsetPos(p,swat_mul(t,-lateral));
        }
        if(!navigation_sample(&s->world,nav,cell,p)) {
            // A cell centre can land on furniture beside a perfectly usable
            // aperture. Try bounded points inside the same 60 cm cell; every
            // candidate resamples its floor and every link still casts a body.
            static const float offsets[8][2]={{0,-.28f},{0,.28f},{-.28f,0},{.28f,0},
                {-.28f,-.28f},{.28f,-.28f},{-.28f,.28f},{.28f,.28f}};
            for(int sample=0;sample<8;sample++)
                if(navigation_sample(&s->world,nav,cell,b3OffsetPos(p,swat_v(offsets[sample][0],0,offsets[sample][1]))))break;
        }
    }
    for(int at=0;at<nav->nodes;at++) if(edges[at%nav->cells]) {
        memset(nav->links[at],0,sizeof(nav->links[at])); if(!nav->walkable[at]) continue;
        for(int direction=0;direction<4;direction++) {
            int neighbor=adjacent(nav,at%nav->cells,direction); if(neighbor<0) continue;
            for(int layer=0;layer<SWAT_NAV_LAYERS;layer++) {
                int other=layer*nav->cells+neighbor; if(!nav->walkable[other]) continue;
                float height=(nav->walkable[at]&nav->walkable[other]&2) ? 1.8288f : 1.016f;
                if(nav_edge(&s->world,cell_position(nav,at),cell_position(nav,other),height)) nav->links[at][direction]|=1u<<layer;
            }
        }
    }
    for(int i=0;i<s->world.count;i++) nav->object_state[i]=navigation_object_state(&s->world.objects[i]);
    nav->generation=s->world.generation; nav->count=s->world.count; nav->built_tick=s->tick; nav->built=true;
}
int swat_navigation_nearest(const SwatNavigation* nav,b3Pos point) {
    int best=-1; float distance=2;
    // Samples stay within 59 cm of their nominal centre (door alignment plus
    // furniture offsets). Only nearby cells can satisfy the existing 2 m
    // search radius. Keep layer/row/column order, including strict tie breaks.
    int cx=(int)floor((point.x-nav->min_x)/.6),cz=(int)floor((point.z-nav->min_z)/.6);
    int x0=cx-5,x1=cx+5,z0=cz-5,z1=cz+5;
    if(x0<0)x0=0;
    if(x1>=nav->width)x1=nav->width-1;
    if(z0<0)z0=0;
    if(z1>=nav->depth)z1=nav->depth-1;
    for(int layer=0;layer<SWAT_NAV_LAYERS;layer++) for(int z=z0;z<=z1;z++) for(int x=x0;x<=x1;x++) {
        int i=layer*nav->cells+z*nav->width+x;
        if(!nav->walkable[i])continue;
        b3Pos p=cell_position(nav,i);
        if(fabs(p.y-point.y)>.65) continue;
        float d=b3Distance(point,p); if(d<distance) { distance=d; best=i; }
    }
    return best;
}
bool swat_navigation_crouch(const SwatSim* s,b3Pos position) {
    if(!s->navigation || !s->navigation->built) return false;
    int at=swat_navigation_nearest(s->navigation,position); return at>=0 && !(s->navigation->walkable[at]&2);
}
typedef struct NavPeople {b3Pos feet[SWAT_MAX_ACTORS];int count;} NavPeople;
static bool door_leaves_clear(const SwatWorld* world,const int doors[],int count,b3Pos from,b3Pos to) {
    for(int i=0;i<count;i++) {
        const SwatObject* o=&world->objects[doors[i]];
        if(from.y>o->center.y+o->half.y || from.y+1.8288f<o->center.y-o->half.y)continue;
        b3Vec3 p=b3SubPos(from,o->center),d=b3SubPos(to,from);float c=cosf(o->yaw),s=sinf(o->yaw);
        float origin[]={c*p.x-s*p.z,s*p.x+c*p.z},delta[]={c*d.x-s*d.z,s*d.x+c*d.z};
        float half[]={o->half.x+.289f,o->half.z+.289f},enter=0,exit=1;
        // A controller touching a leaf can sit just inside this conservative
        // margin. Let it separate along the nearest face, never cross the leaf.
        if(fabsf(origin[0])<half[0] && fabsf(origin[1])<half[1]) {
            int face=(half[0]-fabsf(origin[0])<half[1]-fabsf(origin[1]))?0:1;
            if(origin[face]*delta[face]>0 && fabsf(origin[face]+delta[face])>half[face])continue;
        }
        bool intersects=true;
        for(int axis=0;axis<2;axis++) {
            if(fabsf(delta[axis])<1e-7f) {if(fabsf(origin[axis])>half[axis]){intersects=false;break;}}
            else {
                float a=(-half[axis]-origin[axis])/delta[axis],b=(half[axis]-origin[axis])/delta[axis];
                enter=fmaxf(enter,fminf(a,b));exit=fminf(exit,fmaxf(a,b));
                if(exit<=enter){intersects=false;break;}
            }
        }
        if(intersects)return false;
    }
    return true;
}
static bool people_clear(const NavPeople* people,b3Pos from,b3Pos to) {
    b3Vec3 segment=b3SubPos(to,from);segment.y=0;float length=b3LengthSquared(segment);
    for(int i=0;i<people->count;i++) {
        b3Vec3 delta=b3SubPos(people->feet[i],from);if(fabsf(delta.y)>1)continue;delta.y=0;
        float t=length>1e-6f?swat_clamp(b3Dot(delta,segment)/length,0,1):0;
        if(b3LengthSquared(b3Sub(delta,swat_mul(segment,t)))<.62f*.62f) {
            if(b3LengthSquared(delta)<.62f*.62f && b3Dot(delta,segment)<0 && b3Length(b3Sub(delta,segment))>b3Length(delta)+.02f)continue;
            return false;
        }
    }
    return true;
}
static bool navigation_next_actor(SwatSim* s,int actor,b3Pos start,b3Pos goal,b3Pos* next) {
    if(!s->navigation) {
        s->navigation=navigation_create(&s->world);
        if(!s->navigation) return false;
    }
    SwatNavigation* nav=s->navigation;
    bool dirty=!nav->built || nav->generation!=s->world.generation || nav->count!=s->world.count;
    if(!dirty)for(int i=0;i<s->world.count;i++)if(s->world.objects[i].door && nav->object_state[i]!=navigation_object_state(&s->world.objects[i])) {dirty=true;break;}
    if(dirty)navigation_build(s);
    NavPeople people={0};
    int doors[SWAT_MAX_OBJECTS],door_count=0;
    bool stacking=actor>=0 && s->actors[actor].mind.order==SWAT_ORDER_STACK;
    // Closed, usable doors remain actionable route edges. An opened leaf is
    // still solid cover: evaluate its current pose per request without baking
    // moving geometry into the shared static grid.
    for(int i=0;i<s->world.count;i++) {
        const SwatObject* o=&s->world.objects[i];
        if(o->active && o->door && (fabsf(o->door_angle)>.05f || (stacking && !o->door_open)))doors[door_count++]=i;
    }
    if(actor>=0)for(int i=0;i<s->actor_count;i++) {
        SwatActor* other=&s->actors[i];if(i==actor || !other->present || !other->alive)continue;
        b3Pos feet=swat_body_feet_position(&other->controller.body);
        // Plan around visible bodies over the same range as target perception.
        // Restricting this to the 3 m contact-avoidance radius made destination
        // occupancy disappear/reappear during approach, reversing routes at
        // that boundary (especially escorts joining a crowded staging area).
        // Walls still hide occupants; the shared static nav stays unchanged.
        if(b3Distance(start,feet)<24 && swat_world_visible(&s->world,swat_controller_eye(&s->actors[actor].controller),swat_controller_eye(&other->controller)))
            people.feet[people.count++]=feet;
    }
    int from=swat_navigation_nearest(nav,start),to=swat_navigation_nearest(nav,goal); if(from<0 || to<0) return false;
    if(!door_leaves_clear(&s->world,doors,door_count,start,cell_position(nav,from))) {
        float best=2;from=-1;
        for(int i=0;i<nav->nodes;i++)if(nav->walkable[i]) {
            b3Pos p=cell_position(nav,i);float distance=b3Distance(start,p);
            float height=(nav->walkable[i]&2)?1.8288f:1.016f;
            if(fabs(p.y-start.y)<.65 && distance<best &&
               door_leaves_clear(&s->world,doors,door_count,start,p) && nav_edge(&s->world,start,p,height)) {best=distance;from=i;}
        }
        if(from<0)return false;
    }
    // Dynamic occupancy is local to this route request. Never bake people into
    // the shared static nav grid or reveal occupants behind closed cover.
    bool occupied=!people_clear(&people,cell_position(nav,to),cell_position(nav,to)) ||
       !door_leaves_clear(&s->world,doors,door_count,cell_position(nav,to),cell_position(nav,to));
    if(occupied) {
        float best=2;to=-1;
        for(int i=0;i<nav->nodes;i++)if(nav->walkable[i]) {
            b3Pos p=cell_position(nav,i);float distance=b3Distance(p,goal);
            if(fabs(p.y-goal.y)<.65 && distance<best && people_clear(&people,p,p) && door_leaves_clear(&s->world,doors,door_count,p,p)) {best=distance;to=i;}
        }
        if(to<0)return false;
    }
    int32_t* parent=nav->parent;uint32_t* queue=nav->queue;
    for(int i=0;i<nav->nodes;i++) parent[i]=-1;
    int head=0,tail=0; queue[tail++]=(uint32_t)from; parent[from]=from;
    while(head<tail && parent[to]<0) {
        int at=queue[head++];
        for(int direction=0;direction<4;direction++) {
            int cell=adjacent(nav,at%nav->cells,direction); if(cell<0) continue;
            for(int layer=0;layer<SWAT_NAV_LAYERS;layer++) {
                int n=layer*nav->cells+cell;
                if((nav->links[at][direction]&(1u<<layer)) && parent[n]<0 &&
                   people_clear(&people,at==from?start:cell_position(nav,at),cell_position(nav,n)) &&
                   door_leaves_clear(&s->world,doors,door_count,at==from?start:cell_position(nav,at),cell_position(nav,n))) { parent[n]=at; queue[tail++]=(uint32_t)n; }
            }
        }
    }
    if(parent[to]<0) {
        if(!occupied)return false;
        // The closest free sample beside a person may be across a desk/wall.
        // Choose the closest reachable alternative, not an isolated pocket.
        float best=2;to=-1;
        for(int i=0;i<nav->nodes;i++)if(parent[i]>=0) {
            b3Pos p=cell_position(nav,i);float distance=b3Distance(p,goal);
            if(fabs(p.y-goal.y)<.65 && distance<best && people_clear(&people,p,p) &&
               door_leaves_clear(&s->world,doors,door_count,p,p)) {best=distance;to=i;}
        }
        if(to<0)return false;
    }
    // Reuse the completed BFS queue to reconstruct the chosen path backwards.
    int length=0,at=to;
    while(at!=from) {queue[length++]=(uint32_t)at;at=parent[at];}
    queue[length++]=(uint32_t)from;int route=length>1?length-2:0;at=queue[route];
    // Door alignment can place adjacent grid samples at the same X/Z, with
    // the court and thin room floor supplying two almost coincident heights.
    // Returning that alias forever prevents the controller reaching the next
    // real edge. Advance past arrived samples only with a fresh body sweep.
    while(route>0) {
        b3Pos p=cell_position(nav,at);
        if(hypotf(p.x-start.x,p.z-start.z)>=.30f || fabs(p.y-start.y)>=.30f)break;
        int ahead=queue[route-1];b3Pos target=cell_position(nav,ahead);
        float height=(nav->walkable[from]&nav->walkable[ahead]&2)?1.8288f:1.016f;
        if(!nav_edge(&s->world,start,target,height) || !people_clear(&people,start,target) ||
           !door_leaves_clear(&s->world,doors,door_count,start,target))break;
        route--;at=ahead;
    }
    *next=cell_position(nav,at); return true;
}
bool swat_navigation_next(SwatSim* s,b3Pos start,b3Pos goal,b3Pos* next) {return navigation_next_actor(s,-1,start,goal,next);}
bool swat_navigation_next_for_actor(SwatSim* s,int actor,b3Pos goal,b3Pos* next) {
    if(actor<0 || actor>=s->actor_count || !s->actors[actor].present)return false;
    return navigation_next_actor(s,actor,swat_body_feet_position(&s->actors[actor].controller.body),goal,next);
}
bool swat_navigation_yield_door(const SwatSim* s,int actor,SwatInput* in) {
    if(actor<0 || actor>=s->actor_count || !s->actors[actor].present)return false;
    const SwatController* c=&s->actors[actor].controller;b3Pos feet=swat_body_feet_position(&c->body);
    for(int i=0;i<s->world.count;i++) {
        const SwatObject* o=&s->world.objects[i];
        float target=o->peek?12*SWAT_RAD:SWAT_PI*.5f;
        if(!o->active || !o->door || !o->door_open || o->wedge_owner>=0 || target-o->door_angle<.03f)continue;
        if(feet.y>o->center.y+o->half.y || feet.y+c->body.totalHeight<o->center.y-o->half.y)continue;
        b3Vec3 n=swat_v(cosf(o->closed_yaw),0,-sinf(o->closed_yaw));
        b3Vec3 t=swat_v(sinf(o->closed_yaw),0,cosf(o->closed_yaw)),d=b3SubPos(feet,o->hinge);
        float along=b3Dot(d,t),across=b3Dot(d,n);
        // Give the swinging leaf room before approaching its opening. Keep
        // the retreat on the current side and let the real controller collide.
        if(along<-.32f || along>2*o->half.z+.32f || fabsf(across)>1.4f)continue;
        b3Vec3 retreat=swat_mul(n,across<0?-1:1);
        b3Vec3 forward=swat_direction(c->yaw,0),right=swat_v(-forward.z,0,forward.x);
        in->forward=.7f*b3Dot(retreat,forward);in->strafe=.7f*b3Dot(retreat,right);in->gait=SWAT_WALK;
        return true;
    }
    return false;
}
static bool actor_lane_clear(const SwatSim* s,int index,b3Pos from,b3Pos to) {
    b3Vec3 segment=b3SubPos(to,from);segment.y=0;float length2=b3LengthSquared(segment);
    for(int i=0;i<s->actor_count;i++) {
        const SwatActor* other=&s->actors[i];if(i==index || !other->present || !other->alive)continue;
        b3Pos position=swat_body_feet_position(&other->controller.body);
        b3Vec3 delta=b3SubPos(position,from);if(fabsf(delta.y)>1 || hypotf(delta.x,delta.z)>3)continue;
        // Local visible body avoidance only; this is not an omniscient occupancy
        // map of people in other rooms. Static geometry still owns the route.
        if(!swat_world_visible(&s->world,swat_controller_eye(&s->actors[index].controller),swat_controller_eye(&other->controller)))continue;
        delta.y=0;float t=length2>1e-6f ? swat_clamp(b3Dot(delta,segment)/length2,0,1) : 0;
        float distance=b3Length(b3Sub(delta,swat_mul(segment,t)));
        if(distance<.62f) {
            // A crowded actor must be allowed to separate, rather than freezing
            // because its initial position is already inside the comfort margin.
            float end=b3Length(b3Sub(delta,segment));
            if(b3Length(delta)<.62f && end>b3Length(delta)+.02f && b3Dot(delta,segment)<0)continue;
            return false;
        }
    }
    return true;
}
static bool avoid_local_actors(SwatSim* s,int index,b3Pos feet,b3Pos goal,b3Pos* waypoint) {
    if(actor_lane_clear(s,index,feet,*waypoint))return true;
    b3Vec3 direction=swat_normalize(b3SubPos(*waypoint,feet)),right=swat_v(-direction.z,0,direction.x);
    float height=s->actors[index].controller.body.crouched ? 1.016f : 1.8288f;
    bool found=false;float best=1e9f;b3Pos chosen=*waypoint;
    for(int ring=1;ring<=3;ring++)for(int side=-1;side<=1;side+=2) {
        b3Pos candidate=b3OffsetPos(*waypoint,swat_mul(right,side*ring*.28f));
        NavFloors floors={0};
        b3World_CastRay(s->world.id,b3OffsetPos(candidate,swat_v(0,.6f,0)),swat_v(0,-1.2f,0),b3DefaultQueryFilter(),floor_hit,&floors);
        float nearest=1e9f,support=0;
        for(int i=0;i<floors.count;i++) {
            float difference=fabsf(floors.y[i]-(float)feet.y);
            if(difference<nearest) {nearest=difference;support=floors.y[i];}
        }
        if(nearest>.4572f)continue; // Never steer off a ledge to evade another body.
        candidate.y=support;
        if(!actor_lane_clear(s,index,feet,candidate) || !nav_clear(&s->world,candidate,height) || !nav_edge(&s->world,feet,candidate,height))continue;
        float score=b3Distance(candidate,goal)+.2f*b3Distance(feet,candidate);
        if(score<best) {best=score;chosen=candidate;found=true;}
    }
    if(found)*waypoint=chosen;
    return found;
}
static void move_to(SwatSim* s,int index,b3Pos goal,SwatInput* in) {
    SwatActor* a=&s->actors[index]; SwatMind* mind=&a->mind;
    b3Pos feet=swat_body_feet_position(&a->controller.body); b3Vec3 delta=b3SubPos(goal,feet);
    if(hypotf(delta.x,delta.z)<.45f && fabsf(delta.y)<.45f) return;
    if(swat_navigation_yield_door(s,index,in)) {mind->replan_tick=0;return;}
    if(s->tick>=mind->replan_tick || (hypotf((float)(feet.x-mind->waypoint.x),(float)(feet.z-mind->waypoint.z))<.30f && fabs(feet.y-mind->waypoint.y)<.55)) {
        if(!navigation_next_actor(s,index,feet,goal,&mind->waypoint)) return;
        mind->replan_tick=s->tick+30;
    }
    in->crouch=swat_navigation_crouch(s,feet) || swat_navigation_crouch(s,mind->waypoint);
    if(!avoid_local_actors(s,index,feet,goal,&mind->waypoint))return;
    delta=b3SubPos(mind->waypoint,feet); float yaw=atan2f(delta.z,delta.x),error=swat_angle(yaw-a->controller.yaw);
    in->yaw_delta=swat_clamp(error,-3*SWAT_RAD,3*SWAT_RAD);
    if(fabsf(error)<50*SWAT_RAD) in->forward=.7f;
    in->gait=SWAT_SLOW;
    swat_locomotion_input(s,index,mind->waypoint,in);
    // Interact only with the first actual leaf encountered. Never teleport through a planned route.
    SwatHit ahead=swat_context_hit(s,index,1.7f);
    if(ahead.kind==SWAT_HIT_WORLD && ahead.index>=0 && s->world.objects[ahead.index].door) {
        SwatObject* door=&s->world.objects[ahead.index];
        if(a->role==SWAT_OFFICER && mind->order==SWAT_ORDER_STACK && !door->door_open)return;
        if(door->wedge_owner>=0) { in->forward=0; mind->replan_tick=0; return; }
        if(door->locked) { in->forward=0; if(a->role==SWAT_OFFICER) in->door_tool=SWAT_LOCKPICK; return; }
        if(!door->door_open) { in->forward=0; in->interact=!a->last_interact; }
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
static bool order_support(const SwatWorld* world,b3Pos* point) {
    NavFloors floors={0};
    b3World_CastRay(world->id,b3OffsetPos(*point,swat_v(0,.5f,0)),swat_v(0,-1,0),b3DefaultQueryFilter(),floor_hit,&floors);
    float best=.4572f,support=0;
    for(int i=0;i<floors.count;i++)if(fabsf(floors.y[i]-(float)point->y)<best) {
        best=fabsf(floors.y[i]-(float)point->y);support=floors.y[i];
    }
    if(best==.4572f)return false;
    point->y=support+.015f;
    return nav_clear(world,*point,1.8288f);
}
static bool order_separated(b3Pos point,const b3Pos assigned[],int count) {
    for(int i=0;i<count;i++)if(b3Distance(point,assigned[i])<.9f)return false;
    return true;
}
// Use the targeted doorway's frame and actual supported clearance, never hidden
// occupants or a scripted room route. Each officer reserves a separate endpoint.
static bool order_position(const SwatSim* s,int actor,SwatHit hit,b3Pos source,int order,int rank,
                           const b3Pos assigned[],int count,b3Pos* goal,float* yaw) {
    if(hit.kind!=SWAT_HIT_WORLD || hit.index<0)return false;
    const SwatObject* object=&s->world.objects[hit.index];
    bool entry=object->door && (order==SWAT_ORDER_STACK || order==SWAT_ORDER_CLEAR);
    b3Pos center=hit.point; b3Vec3 normal=swat_normalize(swat_v((float)(source.x-center.x),0,(float)(source.z-center.z)));
    if(entry) {
        center=b3OffsetPos(object->hinge,swat_v(sinf(object->closed_yaw)*object->half.z,-object->half.y,cosf(object->closed_yaw)*object->half.z));
        normal=swat_v(cosf(object->closed_yaw),0,-sinf(object->closed_yaw));
        if(b3Dot(normal,b3SubPos(source,center))<0)normal=swat_mul(normal,-1);
    } else {
        if(hit.normal.y<.5f) {center=b3OffsetPos(center,swat_mul(hit.normal,.6f));center.y=source.y;}
    }
    b3Vec3 right=swat_v(-normal.z,0,normal.x);bool found=false;float best=1e9f;
    b3Pos portal=b3OffsetPos(center,swat_mul(normal,order==SWAT_ORDER_CLEAR?-.45f:.75f));
    if(entry && !order_support(&s->world,&portal))return false;
    float entry_side=-1,span=0;
    if(entry && order==SWAT_ORDER_CLEAR)for(int side=-1;side<=1;side+=2)for(int reach=1;reach<=5;reach++) {
        b3Pos probe=b3OffsetPos(center,swat_add(swat_mul(normal,-1.1f),swat_mul(right,side*reach*.55f)));
        if(reach*.55f>span && order_support(&s->world,&probe) && nav_edge(&s->world,portal,probe,1.8288f)) {span=reach*.55f;entry_side=(float)side;}
    }
    for(int depth=0;depth<7;depth++)for(int side=-5;side<=5;side++) {
        float lateral=side*.55f,forward=depth*.4f;
        if(entry && order==SWAT_ORDER_STACK) {
            if(fabsf(lateral)<object->half.z+.30f)continue;
            forward=.75f+depth*.25f;
        } else if(entry)forward=-(1.1f+depth*.4f);
        else forward=(depth-3)*.4f;
        b3Pos candidate=b3OffsetPos(center,swat_add(swat_mul(normal,forward),swat_mul(right,lateral)));
        if(!order_support(&s->world,&candidate) || !order_separated(candidate,assigned,count) ||
           !actor_lane_clear(s,actor,candidate,candidate))continue;
        b3Pos path_start=entry?portal:center;path_start.y=candidate.y;
        if(!nav_edge(&s->world,path_start,candidate,1.8288f))continue;
        float preference=entry && order==SWAT_ORDER_CLEAR ? entry_side*(2-rank)*1.1f : (rank&1?1:-1)*(.55f+.4f*(rank/2));
        float score=entry ? fabsf(lateral-preference)+.4f*fabsf(forward-(order==SWAT_ORDER_CLEAR?-1.65f:.8f)) : b3Distance(candidate,center);
        if(score<best) {best=score;*goal=candidate;found=true;}
    }
    if(found) {
        b3Vec3 facing=entry && order==SWAT_ORDER_CLEAR ? swat_mul(normal,-1) : b3SubPos(center,*goal);
        if(!entry)facing=swat_mul(normal,-1);
        *yaw=atan2f(facing.z,facing.x);
    }
    return found;
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
        b3Pos assigned[SWAT_MAX_PLAYERS];int count=0;
        for(int i=0;i<s->actor_count;i++) {
            SwatActor* a=&s->actors[i]; if(!a->present || !a->alive || !a->mind.bot || a->role!=SWAT_OFFICER || (in->squad_team && in->squad_team!=a->mind.team)) continue;
            b3Pos destination=goal;float yaw=source->controller.yaw;
            bool position=in->squad_order==SWAT_ORDER_STACK || in->squad_order==SWAT_ORDER_CLEAR || in->squad_order==SWAT_ORDER_MOVE;
            if(position && !order_position(s,i,hit,swat_body_feet_position(&source->controller.body),in->squad_order,count,assigned,count,&destination,&yaw))continue;
            a->mind.pending_order=in->squad_order;a->mind.pending_goal=destination;a->mind.pending_yaw=yaw;
            a->mind.pending_door=hit.kind==SWAT_HIT_WORLD && hit.index>0 && s->world.objects[hit.index].door ? hit.index : 0;
            a->mind.queued=in->squad_queue; a->mind.command_tick=s->tick+18+(in->squad_order==SWAT_ORDER_CLEAR?count*18:0);
            if(count<SWAT_MAX_PLAYERS)assigned[count++]=destination;
        }
    }
    for(int i=0;i<s->actor_count;i++) {
        SwatMind* mind=&s->actors[i].mind;
        if(!mind->pending_order || mind->command_tick>s->tick || (mind->queued && !in->squad_execute)) continue;
        mind->order=mind->pending_order; mind->goal=mind->pending_goal;mind->order_yaw=mind->pending_yaw;mind->order_door=mind->pending_door;mind->entry_settled=false; mind->pending_order=0; mind->queued=false; mind->replan_tick=0;
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
            if(a->rescued)continue;
            if((a->role==SWAT_CIVILIAN || a->role==SWAT_SUSPECT) && mind->escort_owner>=0 && mind->escort_owner<s->actor_count && s->actors[mind->escort_owner].alive) {
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
                // Fill the far sector first. A narrow door is a physical queue,
                // not three independent walkers racing toward its center.
                bool entry_wait=false;
                if(mind->order==SWAT_ORDER_CLEAR && mind->order_door>0)for(int j=0;j<i;j++) {
                    const SwatActor* peer=&s->actors[j];if(!peer->present || !peer->alive || !peer->mind.bot)continue;
                    entry_wait|=(peer->mind.pending_order==SWAT_ORDER_CLEAR && peer->mind.pending_door==mind->order_door) ||
                        (peer->mind.order==SWAT_ORDER_CLEAR && peer->mind.order_door==mind->order_door && !peer->mind.entry_settled);
                }
                if(entry_wait)continue;
                // A newly visible occupant can occupy a planned sector. Pick
                // another supported position inside the same doorway instead
                // of trying to push through that person or consulting hidden AI.
                if(mind->order==SWAT_ORDER_CLEAR && mind->order_door>0 && mind->order_door<s->world.count &&
                   s->tick>=mind->replan_tick && !actor_lane_clear(s,i,mind->goal,mind->goal)) {
                    const SwatObject* door=&s->world.objects[mind->order_door];
                    b3Pos center=b3OffsetPos(door->hinge,swat_v(sinf(door->closed_yaw)*door->half.z,-door->half.y,cosf(door->closed_yaw)*door->half.z));
                    b3Pos outside=b3OffsetPos(center,b3SubPos(center,mind->goal)),assigned[SWAT_MAX_PLAYERS];int count=0;
                    for(int j=0;j<s->actor_count && count<SWAT_MAX_PLAYERS;j++) {
                        SwatActor* peer=&s->actors[j];
                        if(j==i || !peer->present || !peer->alive || !peer->mind.bot)continue;
                        if(peer->mind.pending_order==SWAT_ORDER_CLEAR && peer->mind.pending_door==mind->order_door)assigned[count++]=peer->mind.pending_goal;
                        else if(peer->mind.order==SWAT_ORDER_CLEAR && peer->mind.order_door==mind->order_door)assigned[count++]=peer->mind.goal;
                    }
                    SwatHit hit={.kind=SWAT_HIT_WORLD,.index=mind->order_door,.point=center};
                    order_position(s,i,hit,outside,SWAT_ORDER_CLEAR,i-3,assigned,count,&mind->goal,&mind->order_yaw);
                }
                move_to(s,i,mind->goal,in);
                if(mind->order==SWAT_ORDER_STACK || mind->order==SWAT_ORDER_CLEAR || mind->order==SWAT_ORDER_MOVE) {
                    b3Pos feet=swat_body_feet_position(&a->controller.body);
                    if(b3Distance(feet,mind->goal)<.45f) {
                        mind->entry_settled=true;
                        in->forward=0;in->yaw_delta=swat_clamp(swat_angle(mind->order_yaw-a->controller.yaw),-3*SWAT_RAD,3*SWAT_RAD);
                        in->pitch_delta=swat_clamp(-a->controller.pitch,-2*SWAT_RAD,2*SWAT_RAD);
                    }
                }
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
        if(a->role==SWAT_CIVILIAN && a->alive && a->gear.restrained && b3Distance(swat_body_feet_position(&a->controller.body),s->extraction)<SWAT_STAGING_RADIUS) {
            a->rescued=true;a->mind.escort_owner=-1;
        }
        s->debrief.rescued+=a->role==SWAT_CIVILIAN && a->rescued;
    }
}
