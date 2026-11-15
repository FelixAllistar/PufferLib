// Scripted motel officer for the role task: a route-following baseline that
// clears the Briar Court motel like a cautious two-officer team (CONTRACT.md
// "Scripted baselines", motel officer).
//
// It acts on one officer's role observation, exactly as swat_baseline_act
// does, plus two things an officer knows before the call-out: the static
// building plan (SwatMotelMap: the walkable floor grid the game's own
// navigation builds with the doors closed, the room boxes, the door
// positions and the extraction point) and its place in the team (slot and
// team size). The plan says nothing about who is inside or where. Like
// roles_baselines.h this header includes only roles_baselines.h,
// roles_layout.h and roe.h (the published rules' constants, no state),
// never the simulation, so it cannot read hidden state.
//
// Unlike the pure baselines it keeps per-episode state (SwatMotelOfficer),
// built only from its own observations and actions: its route progress, the
// people it has cuffed, and where weapons should lie (where it saw a suspect
// it shot drop out of sight, or where it cuffed a suspect). Everything is
// deterministic given the observation and coin sequences.
//
// The officer localizes itself from the observation alone: the objective
// vector (obs 38-39) points at the extraction point, whose position is on
// the plan, and obs 3-4 give its heading.
//
// Priorities, one decision at a time:
//   1. A non-compliant suspect in view, one who may be armed (a weapon
//      seen in hand, or hands that cannot be seen) before one seen
//      empty-handed: turn on it and challenge it (a compliance command with
//      the suspect on the reticle within command range), closing in first
//      if it is farther. A suspect who may be armed is challenged on the
//      reticle even when an empty-handed one stands nearer. Lethal force
//      follows the rules of engagement (roe.h), judged from the
//      observation alone:
//      - It never fires unless the reticle shows a weapon in the person's
//        hand (ray channel SWAT_RAY_WEAPON reads 1): a suspect who has
//        dropped the weapon, or whose hands cannot be seen, is never a
//        lethal threat on that evidence. One with hidden hands is
//        challenged and covered like an armed one, but not fired on, even
//        after it was hurt.
//      - After a challenge it holds fire, and holds further commands (a new
//        command restarts the suspect's clock), until the challenge has
//        gone unanswered for longer than SWAT_ROE_CHALLENGE_TICKS: an armed
//        person who has not complied by then is a lethal threat. A suspect
//        who is disarmed always complies with a command and shows hands up
//        instead.
//      - Hurt within the last few decisions, it returns fire on the free
//        suspect holding a weapon on its reticle without waiting: someone
//        has just fired on it.
//      - Every shot follows a confirming call at the target in the decision
//        before. A suspect who has dropped the weapon complies with it and
//        shows hands up before the shot.
//      - It holds fire on a suspect whose range opens fast (fleeing is not
//        a lethal threat), never fires with anyone with hands up or cuffed,
//        a civilian or an officer on the reticle, and never fires in a
//        decision that calls out. Officers call only on odd decisions and
//        fire only on even ones, so a teammate's command cannot land in a
//        shot's decision either.
//      With no challenge open, it calls out a compliance command on every
//      other decision that does not fire.
//   2. A person with hands up and no cuffs in view: walk to them and cuff.
//   3. A civilian it has cuffed: take them by the arm (escort). They follow
//      the officer from then on and reach extraction with it.
//   4. A weapon spot it knows of: crouch next to it and sweep the floor with
//      the reticle, pressing interact, to recover the weapon.
//   5. The route: for each assigned room, stack at the door, open it, enter,
//      turn through the corners, and move on. Then go to extraction and hold.
//      If the mission has not ended once the team is back, sweep every room
//      again.
#ifndef SWAT_ROLES_MOTEL_OFFICER_H
#define SWAT_ROLES_MOTEL_OFFICER_H
#include <string.h>
#include "roles_baselines.h"
#include "roe.h"

// Static plan grid: 60 cm cells aligned with the game's navigation samples.
#define SWAT_MOTEL_MAP_WIDTH 50
#define SWAT_MOTEL_MAP_DEPTH 45
#define SWAT_MOTEL_MAP_CELLS (SWAT_MOTEL_MAP_WIDTH*SWAT_MOTEL_MAP_DEPTH)
#define SWAT_MOTEL_MAP_CELL .6f
#define SWAT_MOTEL_MAP_ROOMS 8
#define SWAT_MOTEL_MAP_DOORS 8
#define SWAT_MOTEL_ROUTE (3*SWAT_MOTEL_MAP_ROOMS+1)
#define SWAT_MOTEL_SPOTS 6
// Floor points swept around a weapon spot (where the suspect's feet were
// seen), most likely first: the sim drops a weapon 35 cm to the +x side of
// the feet whenever that floor is clear, otherwise to another side or at the
// feet. The sighting itself is off by up to a few decimetres.
#define SWAT_MOTEL_SWEEP_POINTS 12
// Distance within which a challenge on the reticle reaches the suspect (the
// sim's command range is 9 m), with a margin for the ray's depth.
#define SWAT_MOTEL_CHALLENGE_RANGE 8.5f
// A free suspect seen this far from where the challenged one was is someone
// else, who has not been challenged.
#define SWAT_MOTEL_CHALLENGE_TRACK 1.5f
// Decisions the challenged suspect may be out of view before the challenge
// is dropped and must be made again.
#define SWAT_MOTEL_CHALLENGE_UNSEEN 4
// Decisions after being hurt in which the officer returns fire on a free
// suspect on the reticle: a suspect seen firing is a lethal threat (roe.h).
#define SWAT_MOTEL_RETURN_FIRE 8
// The sim's fixed tick, seconds.
#define SWAT_MOTEL_TICK_SECONDS (1.0f/60.0f)
// Eye heights of the sim's body (standing and crouched), metres.
#define SWAT_MOTEL_EYE_STAND 1.62f
#define SWAT_MOTEL_EYE_CROUCH .81f

typedef struct SwatMotelMap {
    float x0,z0; // world x,z of the grid corner; cell (i,j) centres at x0+(i+.5)*cell
    // Bit d set: the floor links to the neighbour in direction d (-x,+x,-z,+z).
    // Only cells connected to the extraction point keep links.
    unsigned char links[SWAT_MOTEL_MAP_CELLS];
    float extraction[2];
    int rooms; float room[SWAT_MOTEL_MAP_ROOMS][4]; // centre x,z and half extents x,z
    // Closed leaf centre x,z and the unit normal of the doorway x,z.
    int doors; float door[SWAT_MOTEL_MAP_DOORS][4];
} SwatMotelMap;

enum { SWAT_MOTEL_STACK, SWAT_MOTEL_CLEAR, SWAT_MOTEL_EXTRACT };
typedef struct SwatMotelWaypoint { float x,z; int kind; float face; } SwatMotelWaypoint;

enum { SWAT_MOTEL_ESCORT_NONE, SWAT_MOTEL_ESCORT_RELEASE, SWAT_MOTEL_ESCORT_TAKE };

typedef struct SwatMotelOfficer {
    // Team slot and size, and the sim ticks each decision lasts (the role
    // task's roles_repeat), which the officer's own clock gives it.
    int slot,team,ticks;
    // Route, current step, decisions held at it, and decisions at
    // extraction with another officer in sight.
    SwatMotelWaypoint route[SWAT_MOTEL_ROUTE]; int route_length,step,hold,together;
    int sweeps; // completed passes over the route
    // Own previous interact press (presses need a rising edge), the
    // decision count, which cycles the gaze pitch, and whether it has tried
    // the selector for automatic fire.
    bool interact,selector_tried; int decisions;
    // Cuffing: the kind of person the reticle was cuffing last decision.
    int cuffing;
    // Escort phase and decisions in it.
    int escort,escort_wait;
    // Weapon spots and the floor sweep at the current one.
    float spot[SWAT_MOTEL_SPOTS][2]; int spots,sweep_point,sweep_time;
    // The suspect last engaged: where, and decisions since it was in view;
    // whether it was fired on.
    float target[2]; bool fired; int unseen;
    // Decisions since this officer challenged that suspect (0: no challenge
    // open), and its own previous command press (a command needs a fresh
    // press).
    int challenge; bool commanded;
    // Whether its previous decision called at the engaged suspect on the
    // reticle: the confirmation each shot needs.
    bool confirm;
    // Decisions since the officer was last hurt, and the reticle's depth on
    // the engaged suspect last decision (0 when it was not on them).
    int hurt; float range;
    // Stuck detection: position at the last check and decisions since.
    float last[2]; int still,unstick;
    // Decisions left before the next door press: a press on a leaf that is
    // still swinging would shut it again.
    int door_wait;
} SwatMotelOfficer;

static int swat_motel_cell(const SwatMotelMap* m,float x,float z) {
    int i=(int)floorf((x-m->x0)/SWAT_MOTEL_MAP_CELL),j=(int)floorf((z-m->z0)/SWAT_MOTEL_MAP_CELL);
    if(i<0 || j<0 || i>=SWAT_MOTEL_MAP_WIDTH || j>=SWAT_MOTEL_MAP_DEPTH) return -1;
    return j*SWAT_MOTEL_MAP_WIDTH+i;
}
static void swat_motel_cell_centre(const SwatMotelMap* m,int cell,float* x,float* z) {
    *x=m->x0+(cell%SWAT_MOTEL_MAP_WIDTH+.5f)*SWAT_MOTEL_MAP_CELL;
    *z=m->z0+(cell/SWAT_MOTEL_MAP_WIDTH+.5f)*SWAT_MOTEL_MAP_CELL;
}
static int swat_motel_neighbour(int cell,int direction) {
    int i=cell%SWAT_MOTEL_MAP_WIDTH,j=cell/SWAT_MOTEL_MAP_WIDTH;
    if(direction==0) return i>0 ? cell-1 : -1;
    if(direction==1) return i+1<SWAT_MOTEL_MAP_WIDTH ? cell+1 : -1;
    if(direction==2) return j>0 ? cell-SWAT_MOTEL_MAP_WIDTH : -1;
    return j+1<SWAT_MOTEL_MAP_DEPTH ? cell+SWAT_MOTEL_MAP_WIDTH : -1;
}
// The connected cell nearest a point (the point's own cell when connected).
static int swat_motel_snap(const SwatMotelMap* m,float x,float z) {
    int best=-1; float distance=1e9f;
    int own=swat_motel_cell(m,x,z);
    if(own>=0 && m->links[own]) return own;
    for(int cell=0;cell<SWAT_MOTEL_MAP_CELLS;cell++) if(m->links[cell]) {
        float cx,cz; swat_motel_cell_centre(m,cell,&cx,&cz);
        float d=(cx-x)*(cx-x)+(cz-z)*(cz-z);
        if(d<distance) { distance=d; best=cell; }
    }
    return best;
}
// Breadth-first distances (in cells) to `goal` over the plan's links.
static void swat_motel_distances(const SwatMotelMap* m,int goal,int16_t* distance) {
    static const int back[4]={1,0,3,2};
    int queue[SWAT_MOTEL_MAP_CELLS],head=0,tail=0;
    for(int c=0;c<SWAT_MOTEL_MAP_CELLS;c++) distance[c]=-1;
    if(goal<0) return;
    distance[goal]=0; queue[tail++]=goal;
    while(head<tail) {
        int c=queue[head++];
        for(int d=0;d<4;d++) {
            int n=swat_motel_neighbour(c,d);
            if(n<0 || distance[n]>=0 || !(m->links[n]&(1u<<back[d]))) continue;
            distance[n]=(int16_t)(distance[c]+1); queue[tail++]=n;
        }
    }
}

// The plan's door into a room (the door whose leaf lies on the room's
// wall), or -1 for a room with no door of its own.
static int swat_motel_room_door(const SwatMotelMap* m,const float* room) {
    for(int d=0;d<m->doors;d++)
        if(fabsf(m->door[d][0]-room[0])<=room[2]+.5f && fabsf(m->door[d][1]-room[1])<=room[3]+.5f) return d;
    return -1;
}
static float swat_motel_extraction_distance(const SwatMotelMap* m,const float* room) {
    return hypotf(room[0]-m->extraction[0],room[1]-m->extraction[1]);
}
// Route. The first pass covers the rooms with a door of their own: the team
// splits them into contiguous shares, east (nearest the staging point) to
// west, and each officer clears its share from the room farthest from
// extraction inward, because cuffed civilians follow the officer at a slow
// walk and should finish close to extraction. A room is a stack point 1.3 m
// outside its door, facing it, then the room centre. The route ends at
// extraction. full=true is the second sweep: every room on the plan, doors
// or not (the back office is entered through another room), east to west
// starting after its own share, so the teammates' rooms come first.
static void swat_motel_route(SwatMotelOfficer* o,const SwatMotelMap* m,bool full) {
    int order[SWAT_MOTEL_MAP_ROOMS],rooms=0;
    for(int r=0;r<m->rooms;r++) {
        if(!full && swat_motel_room_door(m,m->room[r])<0) continue;
        int i=rooms++; while(i>0 && m->room[order[i-1]][0]<m->room[r][0]) { order[i]=order[i-1]; i--; }
        order[i]=r;
    }
    int team=o->team>0 ? o->team : 1,share=(rooms+team-1)/team;
    int first=full ? 0 : o->slot*share,last=full ? rooms : first+share;
    if(last>rooms) last=rooms;
    // The second sweep starts with the teammates' rooms, after its own share.
    if(full) {
        int rotated[SWAT_MOTEL_MAP_ROOMS],start=(o->slot+1)*share;
        for(int k=0;k<rooms;k++) rotated[k]=order[(start+k)%rooms];
        for(int k=0;k<rooms;k++) order[k]=rotated[k];
    }
    if(!full) for(int k=first+1;k<last;k++) {
        int r=order[k],i=k; float d=swat_motel_extraction_distance(m,m->room[r]);
        while(i>first && swat_motel_extraction_distance(m,m->room[order[i-1]])<d-.05f) { order[i]=order[i-1]; i--; }
        order[i]=r;
    }
    o->route_length=0;
    for(int k=first;k<last;k++) {
        const float* room=m->room[order[k]];
        int d=swat_motel_room_door(m,room);
        if(d>=0) {
            const float* door=m->door[d];
            // Outward: the side of the doorway away from the room centre.
            float out=(door[0]-room[0])*door[2]+(door[1]-room[1])*door[3]>0 ? 1 : -1;
            float nx=door[2]*out,nz=door[3]*out;
            o->route[o->route_length++]=(SwatMotelWaypoint){door[0]+nx*1.3f,door[1]+nz*1.3f,SWAT_MOTEL_STACK,
                atan2f(-nz,-nx)*SWAT_BASELINE_DEGREES};
        }
        o->route[o->route_length++]=(SwatMotelWaypoint){room[0],room[1],SWAT_MOTEL_CLEAR,0};
    }
    o->route[o->route_length++]=(SwatMotelWaypoint){m->extraction[0],m->extraction[1],SWAT_MOTEL_EXTRACT,0};
    // Arrive on the plan: a room centre may be under furniture.
    for(int k=0;k<o->route_length;k++) {
        int cell=swat_motel_snap(m,o->route[k].x,o->route[k].z);
        if(cell>=0) swat_motel_cell_centre(m,cell,&o->route[k].x,&o->route[k].z);
    }
    o->step=0; o->hold=0;
}

// Per-episode start: slot is this officer's place in the team (0 leads the
// eastern rooms), team the number of officers, ticks the sim ticks per
// decision.
static void swat_motel_officer_begin(SwatMotelOfficer* o,const SwatMotelMap* m,int slot,int team,int ticks) {
    memset(o,0,sizeof(*o));
    o->slot=slot; o->team=team; o->ticks=ticks>0 ? ticks : 1; o->cuffing=SWAT_SEEN_NONE;
    o->hurt=SWAT_MOTEL_RETURN_FIRE+1;
    swat_motel_route(o,m,false);
}

// Own position (x,z) and heading (radians, world yaw) from the observation:
// extraction minus the observed objective vector.
static void swat_motel_locate(const float* obs,const SwatMotelMap* m,float* x,float* z,float* yaw) {
    float s=obs[3],c=obs[4];
    *yaw=atan2f(s,c);
    float forward=obs[SWAT_SELF_GOAL_FORWARD]*SWAT_ROLES_RANGE,right=obs[SWAT_SELF_GOAL_RIGHT]*SWAT_ROLES_RANGE;
    // forward=(cos,sin), right=(-sin,cos) in world x,z.
    *x=m->extraction[0]-(forward*c-right*s);
    *z=m->extraction[1]-(forward*s+right*c);
}
// Heading of a world point relative to the view (degrees, + right).
static float swat_motel_bearing(float x,float z,float yaw,float tx,float tz) {
    float dx=tx-x,dz=tz-z,c=cosf(yaw),s=sinf(yaw);
    return atan2f(-dx*s+dz*c,dx*c+dz*s)*SWAT_BASELINE_DEGREES;
}
// World point seen on ray r (horizontal projection), `beyond` metres further.
static void swat_motel_seen(const float* obs,const SwatBaselineView* v,int r,float x,float z,float yaw,
                            float beyond,float* px,float* pz) {
    float pitch=asinf(fmaxf(-1,fminf(1,obs[SWAT_SELF_PITCH_SIN])))+v->ray[r].pitch/SWAT_BASELINE_DEGREES;
    float heading=yaw+v->ray[r].yaw/SWAT_BASELINE_DEGREES,range=v->ray[r].depth*cosf(pitch)+beyond;
    *px=x+cosf(heading)*range; *pz=z+sinf(heading)*range;
}
static void swat_motel_add_spot(SwatMotelOfficer* o,float x,float z) {
    for(int i=0;i<o->spots;i++) if(fabsf(o->spot[i][0]-x)<.8f && fabsf(o->spot[i][1]-z)<.8f) return;
    if(o->spots<SWAT_MOTEL_SPOTS) { o->spot[o->spots][0]=x; o->spot[o->spots][1]=z; o->spots++; }
}
static void swat_motel_drop_spot(SwatMotelOfficer* o) {
    for(int i=1;i<o->spots;i++) { o->spot[i-1][0]=o->spot[i][0]; o->spot[i-1][1]=o->spot[i][1]; }
    if(o->spots>0) o->spots--;
    o->sweep_point=o->sweep_time=0;
}

// Walk toward (tx,tz) along the plan: steer at the cell three links ahead
// on the shortest path, opening doors on the way. Movement uses forward and
// strafe together, so the officer keeps turning to face its path without
// stopping. Returns the remaining straight-line distance.
static float swat_motel_walk(SwatMotelOfficer* o,const SwatMotelMap* m,const SwatBaselineView* v,
                             float x,float z,float yaw,float tx,float tz,int gait,float* action) {
    float remaining=sqrtf((tx-x)*(tx-x)+(tz-z)*(tz-z));
    int goal=swat_motel_snap(m,tx,tz),here=swat_motel_snap(m,x,z);
    float ax=tx,az=tz;
    // Up to six path cells ahead; steer at the third.
    float path[6][2]; int steps=0;
    if(goal>=0 && here>=0 && goal!=here) {
        int16_t distance[SWAT_MOTEL_MAP_CELLS];
        swat_motel_distances(m,goal,distance);
        int cell=here;
        while(steps<6 && distance[cell]>0) {
            int next=-1;
            for(int d=0;d<4 && next<0;d++) {
                int n=swat_motel_neighbour(cell,d);
                if(n>=0 && (m->links[cell]&(1u<<d)) && distance[n]==distance[cell]-1) next=n;
            }
            if(next<0) break;
            cell=next; swat_motel_cell_centre(m,cell,&path[steps][0],&path[steps][1]); steps++;
        }
        if(steps>0 && cell!=goal) { ax=path[steps<3 ? steps-1 : 2][0]; az=path[steps<3 ? steps-1 : 2][1]; }
    }
    // Doorways are barely wider than a body: when the path crosses a nearby
    // doorway, first line up on its axis 0.7 m out, then go straight through.
    for(int d=0;d<m->doors;d++) {
        const float* door=m->door[d];
        float side=(x-door[0])*door[2]+(z-door[1])*door[3];
        float lateral=-(x-door[0])*door[3]+(z-door[1])*door[2];
        if(fabsf(side)>2.5f || fabsf(lateral)>2.0f) continue;
        bool crossing=false;
        for(int k=0;k<steps && !crossing;k++) {
            float s2=(path[k][0]-door[0])*door[2]+(path[k][1]-door[1])*door[3];
            float l2=-(path[k][0]-door[0])*door[3]+(path[k][1]-door[1])*door[2];
            crossing=s2*side<0 && fabsf(l2)<.8f;
        }
        if(!crossing) continue;
        float out=side>0 ? 1 : -1;
        float depth=fabsf(lateral)>.15f && fabsf(side)>.35f ? .7f*out : -.9f*out;
        ax=door[0]+door[2]*depth; az=door[1]+door[3]*depth;
        if(gait>1) gait=1;
        break;
    }
    float bearing=swat_motel_bearing(x,z,yaw,ax,az);
    action[SWAT_ROLE_HEAD_YAW]=(float)swat_baseline_yaw_bin(bearing);
    // Move along the bearing in the body frame: forward within 60 deg,
    // strafe beyond 30 deg, backward past 120 deg.
    float radians=bearing/SWAT_BASELINE_DEGREES,f=cosf(radians),r=sinf(radians);
    if(remaining>.25f) {
        action[SWAT_ROLE_HEAD_FORWARD]=f>.5f ? 2 : f<-.5f ? 0 : 1;
        action[SWAT_ROLE_HEAD_STRAFE]=r>.5f ? 2 : r<-.5f ? 0 : 1;
        action[SWAT_ROLE_HEAD_GAIT]=(float)gait;
    }
    // Never walk into a free person close ahead: wait for them to move (a
    // cuffed person being led moves with the officer).
    for(int r=0;r<SWAT_ROLES_RAYS;r++) if(v->ray[r].kind>=SWAT_SEEN_OFFICER && !v->ray[r].restrained &&
                                          v->ray[r].depth<.9f && fabsf(v->ray[r].yaw-bearing)<30)
        action[SWAT_ROLE_HEAD_FORWARD]=action[SWAT_ROLE_HEAD_STRAFE]=1;
    // Stuck against a person or furniture: side-step for a moment.
    if(fabsf(x-o->last[0])+fabsf(z-o->last[1])>.4f) { o->last[0]=x; o->last[1]=z; o->still=0; }
    else if(remaining>.6f && ++o->still>20) { o->still=0; o->unstick=6; }
    if(o->unstick>0) {
        o->unstick--;
        action[SWAT_ROLE_HEAD_STRAFE]=(o->step+o->sweeps)&1 ? 2 : 0;
        action[SWAT_ROLE_HEAD_FORWARD]=o->unstick<3 ? 2 : 0;
    }
    // A closed door ahead within reach on the plan: open it (one press). The
    // reticle must hit the plane of the closed leaf; a press on a leaf that
    // already stands open would shut it. Back off a leaf that is opening
    // into the officer.
    const SwatBaselineRay* ahead=&v->ray[0];
    if(o->door_wait>0) o->door_wait--;
    if(ahead->kind==SWAT_SEEN_DOOR && ahead->depth<1.8f) {
        float hx=x+cosf(yaw)*ahead->depth,hz=z+sinf(yaw)*ahead->depth;
        for(int d=0;d<m->doors;d++) {
            const float* door=m->door[d];
            float side=(hx-door[0])*door[2]+(hz-door[1])*door[3];
            float lateral=-(hx-door[0])*door[3]+(hz-door[1])*door[2];
            if(fabsf(side)>.3f || fabsf(lateral)>.6f) continue;
            if(o->door_wait==0 && !o->interact) { action[SWAT_ROLE_HEAD_INTERACT]=1; o->door_wait=30; }
            if(ahead->depth<.7f) action[SWAT_ROLE_HEAD_FORWARD]=0;
            break;
        }
        // A leaf jammed half open in the way: shut it, so the next press
        // swings it fully open.
        if(ahead->depth<.8f && o->still>10 && o->door_wait==0 && !o->interact) {
            action[SWAT_ROLE_HEAD_INTERACT]=1; o->door_wait=30;
        }
    }
    return remaining;
}

// Crouch beside a weapon spot and sweep its drop points with the reticle,
// pressing interact on each. Returns false when the sweep is over.
static bool swat_motel_sweep(SwatMotelOfficer* o,const SwatMotelMap* m,const SwatBaselineView* v,const float* obs,
                             float x,float z,float yaw,float* action) {
    static const float offsets[SWAT_MOTEL_SWEEP_POINTS][2]={{.35f,0},{.35f,.25f},{.35f,-.25f},{.6f,0},{.1f,0},
        {.6f,.25f},{.6f,-.25f},{.1f,.25f},{.1f,-.25f},{-.35f,0},{0,.35f},{0,-.35f}};
    if(o->sweep_time++>100 || o->sweep_point>=SWAT_MOTEL_SWEEP_POINTS) { swat_motel_drop_spot(o); return false; }
    float sx=o->spot[0][0],sz=o->spot[0][1];
    float distance=sqrtf((sx-x)*(sx-x)+(sz-z)*(sz-z));
    if(distance>1.2f) {
        // Stand off 0.9 m on this side of the spot.
        float tx=sx+(x-sx)/fmaxf(distance,1e-3f)*.9f,tz=sz+(z-sz)/fmaxf(distance,1e-3f)*.9f;
        swat_motel_walk(o,m,v,x,z,yaw,tx,tz,0,action);
        return true;
    }
    action[SWAT_ROLE_HEAD_CROUCH]=1;
    float px=sx+offsets[o->sweep_point][0],pz=sz+offsets[o->sweep_point][1];
    float bearing=swat_motel_bearing(x,z,yaw,px,pz),flat=sqrtf((px-x)*(px-x)+(pz-z)*(pz-z));
    float eye=obs[8]>.5f ? SWAT_MOTEL_EYE_CROUCH : SWAT_MOTEL_EYE_STAND;
    float want=atan2f(-eye,flat)*SWAT_BASELINE_DEGREES;
    float pitch=asinf(fmaxf(-1,fminf(1,obs[SWAT_SELF_PITCH_SIN])))*SWAT_BASELINE_DEGREES;
    action[SWAT_ROLE_HEAD_YAW]=(float)swat_baseline_yaw_bin(bearing);
    action[SWAT_ROLE_HEAD_PITCH]=(float)swat_baseline_pitch_bin(want-pitch);
    // A press with a person on the reticle would take their arm instead.
    bool person=false;
    for(int r=0;r<SWAT_ROLES_RAYS;r++)
        person|=v->ray[r].kind>=SWAT_SEEN_OFFICER && v->ray[r].depth<2.3f && fabsf(v->ray[r].yaw)<15;
    if(person && o->sweep_time%8==0) o->sweep_point++;
    else if(fabsf(bearing)<4 && fabsf(want-pitch)<4 && obs[8]>.5f && !person) {
        action[SWAT_ROLE_HEAD_INTERACT]=!o->interact;
        if(o->interact) o->sweep_point++;
    }
    return true;
}

// Follows the route (priority 5).
static void swat_motel_officer_route(SwatMotelOfficer* o,const SwatMotelMap* m,const SwatBaselineView* v,const float* obs,
                                     float x,float z,float yaw,uint32_t coin,float* action) {
    const SwatMotelWaypoint* w=&o->route[o->step];
    // At a door and inside a room the officer crouches, weapon up: its eye
    // is then level with a crouched ambusher's, and it is a smaller target.
    // Long legs in the open are run.
    bool room=w->kind==SWAT_MOTEL_CLEAR;
    float distance=sqrtf((w->x-x)*(w->x-x)+(w->z-z)*(w->z-z));
    float remaining=swat_motel_walk(o,m,v,x,z,yaw,w->x,w->z,room ? 1 : distance>5 ? 2 : 1,action);
    bool close=room || (w->kind==SWAT_MOTEL_STACK && remaining<=.5f);
    action[SWAT_ROLE_HEAD_AIM]=close; action[SWAT_ROLE_HEAD_CROUCH]=close;
    if(w->kind==SWAT_MOTEL_EXTRACT) {
        if(remaining>1.0f) return;
        // Hold at extraction, turning slowly to watch the lot. When the
        // rest of the team is back too (another officer seen within 5 m for
        // 10 s) and the mission is still running, something was missed:
        // sweep every room again. Alone, wait 40 s (a teammate may be down).
        // While a cuffed civilian is still seen on the way in, keep waiting.
        action[SWAT_ROLE_HEAD_YAW]=(float)((coin>>4)&1 ? 4 : 2);
        for(int r=0;r<SWAT_ROLES_RAYS;r++) if(v->ray[r].kind==SWAT_SEEN_OFFICER && v->ray[r].depth<5) { o->together++; break; }
        for(int r=0;r<SWAT_ROLES_RAYS;r++) if(v->ray[r].kind==SWAT_SEEN_CIVILIAN && v->ray[r].restrained &&
                                              v->ray[r].depth>3.5f && v->ray[r].depth<25) { o->hold=o->together=0; break; }
        if(++o->hold>600 || o->together>150) { swat_motel_route(o,m,true); o->sweeps++; o->together=0; }
        return;
    }
    if(remaining>.5f) return;
    if(w->kind==SWAT_MOTEL_STACK) {
        // Stacked outside the door, weapon up: open it, then look into the
        // room through the doorway, left and right, before going in.
        action[SWAT_ROLE_HEAD_FORWARD]=action[SWAT_ROLE_HEAD_STRAFE]=1;
        action[SWAT_ROLE_HEAD_AIM]=1;
        float look=o->hold<6 ? 0 : o->hold<11 ? -20 : o->hold<16 ? 20 : 0;
        action[SWAT_ROLE_HEAD_YAW]=(float)swat_baseline_yaw_bin(swat_baseline_wrap(w->face+look-yaw*SWAT_BASELINE_DEGREES));
        if(++o->hold>=18) { o->hold=0; o->step++; }
        return;
    }
    // Room centre: turn through every corner (a full turn), then most of a
    // turn again looking 35 deg down for anyone kneeling or hiding low,
    // then move on.
    action[SWAT_ROLE_HEAD_FORWARD]=action[SWAT_ROLE_HEAD_STRAFE]=1;
    action[SWAT_ROLE_HEAD_YAW]=4;
    if(o->hold>=16) {
        float pitch=asinf(fmaxf(-1,fminf(1,obs[SWAT_SELF_PITCH_SIN])))*SWAT_BASELINE_DEGREES;
        action[SWAT_ROLE_HEAD_PITCH]=(float)swat_baseline_pitch_bin(-35-pitch);
    }
    if(++o->hold>=28) { o->hold=0; o->step++; }
}

// Whether a ray's person may be armed: a weapon seen in hand, or hands that
// cannot be seen.
static bool swat_motel_may_be_armed(const SwatBaselineRay* ray) {
    return ray->armed || ray->hands_hidden;
}
// The non-compliant suspect to engage: the nearest one who may be armed,
// else the nearest one at all (who still needs a challenge to put their
// hands up). The reticle wins ties, as in swat_baseline_pick.
static int swat_motel_threat(const SwatBaselineView* v) {
    int best=-1;
    for(int r=0;r<SWAT_ROLES_RAYS;r++) {
        const SwatBaselineRay* ray=&v->ray[r];
        if(ray->kind!=SWAT_SEEN_SUSPECT || ray->compliant || !swat_motel_may_be_armed(ray)) continue;
        if(best<0 || ray->depth<v->ray[best].depth-.25f) best=r;
    }
    return best>=0 ? best : swat_baseline_pick(v,SWAT_SEEN_SUSPECT,0);
}

static void swat_motel_officer_decide(SwatMotelOfficer* o,const SwatMotelMap* m,const float* obs,uint32_t coin,float* action) {
    SwatBaselineView v; swat_baseline_read(obs,&v);
    float x,z,yaw; swat_motel_locate(obs,m,&x,&z,&yaw);
    // 1. Threats: challenge a non-compliant suspect in view, and engage it
    // once the challenge has gone unanswered; keep turning onto one that
    // slipped between the rays for a few decisions.
    if(o->challenge>0) o->challenge++;
    o->hurt=obs[SWAT_SELF_TAKEN]>0 ? 0 : o->hurt+(o->hurt<=SWAT_MOTEL_RETURN_FIRE);
    int threat=swat_motel_threat(&v);
    if(threat>=0) {
        int memory=SWAT_BASELINE_IDLE; float tx,tz;
        swat_motel_seen(obs,&v,threat,x,z,yaw,.3f,&tx,&tz);
        if(hypotf(tx-o->target[0],tz-o->target[1])>SWAT_MOTEL_CHALLENGE_TRACK) { o->challenge=0; o->confirm=false; }
        o->target[0]=tx; o->target[1]=tz; o->unseen=0;
        // An empty-handed suspect on the reticle is not the target while
        // one who may be armed is in view: turn onto that one, so the
        // challenge reaches them.
        SwatBaselineView aim=v;
        if(swat_motel_may_be_armed(&v.ray[threat]) && !swat_motel_may_be_armed(&v.ray[0])) aim.ray[0].kind=SWAT_SEEN_NONE;
        bool on=swat_baseline_engage(obs,&aim,threat,SWAT_SEEN_SUSPECT,action,&memory);
        // The command lands on the first tick of the challenging decision,
        // and a shot is lawful from SWAT_ROE_CHALLENGE_TICKS+1 ticks later.
        // Wait one decision more than that.
        int wait=(SWAT_ROE_CHALLENGE_TICKS+o->ticks)/o->ticks+1;
        // A suspect moving away fast is fleeing, never a lethal threat
        // (roe.h), even after a challenge: hold fire while the reticle's
        // depth opens at more than half SWAT_ROE_FLEE_SPEED.
        bool opening=on && o->range>0 &&
            v.ray[0].depth-o->range>.5f*SWAT_ROE_FLEE_SPEED*SWAT_MOTEL_TICK_SECONDS*(float)o->ticks;
        o->range=on ? v.ray[0].depth : 0;
        // Officers fire only on even decisions and call out only on odd
        // ones. Every officer counts decisions from the same reset, so a
        // teammate's command never lands in the decision a shot is fired,
        // where it would make the target surrender just before the hit.
        bool even=(o->decisions&1)==0;
        bool reach=on && v.ray[0].depth<SWAT_MOTEL_CHALLENGE_RANGE;
        // An unanswered challenge makes a suspect seen armed a lethal
        // threat; return fire does not wait for it. Both need the weapon
        // seen in hand on the reticle (below). While the reticle shows
        // hidden hands no shot can follow, so no confirming call is made
        // either: a call would restart the suspect's challenge clock.
        bool lethal=(o->challenge>wait || o->hurt<=SWAT_MOTEL_RETURN_FIRE) && !v.ray[0].hands_hidden;
        // Every shot follows a call at the target in the decision before.
        // A disarmed suspect always complies with a command, so one who
        // dropped the weapon in the meantime shows hands up before the
        // shot. A call does not undo a lethal threat already on the record
        // (roe.h).
        bool confirmed=o->confirm; o->confirm=false;
        // Never fire on a person the reticle does not show with a weapon in
        // hand: empty hands, or hands it cannot see.
        if(!on || !v.ray[0].armed || !even || !confirmed || opening || !lethal) action[SWAT_ROLE_HEAD_TRIGGER]=0;
        // On automatic, hold the trigger while the reticle stays on them.
        else if(obs[SWAT_SELF_SELECTOR]>.75f) action[SWAT_ROLE_HEAD_TRIGGER]=1;
        if(reach && !even && !o->commanded && (o->challenge==0 || lethal)) {
            // The first call is the challenge; while lethal force is
            // allowed, each call confirms the target for the next shot. A
            // call restarts an unanswered challenge's clock.
            action[SWAT_ROLE_HEAD_COMMAND]=1; o->confirm=true;
            if(o->challenge<=wait) o->challenge=1;
        } else if(o->challenge==0 && v.ray[threat].depth>=SWAT_MOTEL_CHALLENGE_RANGE) {
            // Out of command range: close in along the plan, weapon up,
            // turning onto the suspect once the path allows.
            float remaining=swat_motel_walk(o,m,&v,x,z,yaw,tx,tz,1,action);
            if(remaining<SWAT_MOTEL_CHALLENGE_RANGE+2) action[SWAT_ROLE_HEAD_YAW]=(float)swat_baseline_yaw_bin(v.ray[threat].yaw);
            action[SWAT_ROLE_HEAD_AIM]=1;
        }
        o->fired|=action[SWAT_ROLE_HEAD_TRIGGER]>.5f;
        swat_baseline_weapon(obs,true,coin,action);
        return;
    }
    o->range=0; o->confirm=false;
    if(++o->unseen>SWAT_MOTEL_CHALLENGE_UNSEEN) o->challenge=0;
    if(o->fired) {
        if(o->unseen<4) {
            float bearing=swat_motel_bearing(x,z,yaw,o->target[0],o->target[1]);
            action[SWAT_ROLE_HEAD_AIM]=1;
            action[SWAT_ROLE_HEAD_YAW]=(float)swat_baseline_yaw_bin(bearing);
            swat_baseline_weapon(obs,true,coin,action);
            return;
        }
        // The suspect it shot at is out of sight: its weapon may lie there.
        if(o->unseen>=8) { swat_motel_add_spot(o,o->target[0],o->target[1]); o->fired=false; }
    }
    swat_baseline_weapon(obs,false,coin,action);
    // Once, with nothing in view: one selector press from semi, which selects
    // automatic if the weapon has it (otherwise safe, and the upkeep above
    // returns it to semi on the next decision).
    if(!o->selector_tried && obs[SWAT_SELF_SELECTOR]>.25f && obs[SWAT_SELF_SELECTOR]<.75f) {
        action[SWAT_ROLE_HEAD_SELECTOR]=1; o->selector_tried=true;
    }
    // Carry the view a little low: standing, 10 deg (a crouched person a
    // few metres off sits below a level gaze); crouched, 3 deg.
    action[SWAT_ROLE_HEAD_PITCH]=(float)swat_baseline_pitch_bin((obs[8]>.5f ? -3 : -10)-
        asinf(fmaxf(-1,fminf(1,obs[SWAT_SELF_PITCH_SIN])))*SWAT_BASELINE_DEGREES);
    // 2. Cuffs. A cuff it was holding has just closed when the reticle
    // shows that person cuffed: a suspect's weapon lies at its feet, and a
    // civilian is to be led out.
    if(o->cuffing!=SWAT_SEEN_NONE && obs[SWAT_SELF_CUFFING]<=0 && v.ray[0].kind==o->cuffing &&
       v.ray[0].restrained && v.ray[0].depth<SWAT_BASELINE_REACH+.2f) {
        if(o->cuffing==SWAT_SEEN_SUSPECT) {
            float sx,sz; swat_motel_seen(obs,&v,0,x,z,yaw,.25f,&sx,&sz);
            swat_motel_add_spot(o,sx,sz);
        } else o->escort=SWAT_MOTEL_ESCORT_RELEASE;
    }
    o->cuffing=SWAT_SEEN_NONE;
    int cuff=swat_baseline_pick_uncuffed(&v,SWAT_SEEN_SUSPECT);
    if(cuff<0) cuff=swat_baseline_pick_uncuffed(&v,SWAT_SEEN_CIVILIAN);
    if(cuff>=0 && v.ray[cuff].depth<12) {
        const SwatBaselineRay* c=&v.ray[cuff];
        if(cuff==0 && c->depth<SWAT_BASELINE_REACH) {
            // Hold still while the cuff progresses; otherwise press again.
            action[SWAT_ROLE_HEAD_PITCH]=2;
            action[SWAT_ROLE_HEAD_INTERACT]=obs[SWAT_SELF_CUFFING]>0 ? 1 : !o->interact;
            o->cuffing=c->kind;
            return;
        }
        // Walk over along the plan (round beds and furniture); close by,
        // or when the plan has nothing better, step straight in, facing them.
        float px,pz; swat_motel_seen(obs,&v,cuff,x,z,yaw,.2f,&px,&pz);
        float remaining=swat_motel_walk(o,m,&v,x,z,yaw,px,pz,0,action);
        if(c->depth<2.5f && (remaining<1.3f || o->still>10)) {
            action[SWAT_ROLE_HEAD_YAW]=(float)swat_baseline_yaw_bin(c->yaw);
            action[SWAT_ROLE_HEAD_FORWARD]=fabsf(c->yaw)<15 && c->depth>1.0f ? 2 : 1;
            action[SWAT_ROLE_HEAD_STRAFE]=1;
        }
        action[SWAT_ROLE_HEAD_PITCH]=(float)swat_baseline_pitch_bin(c->pitch);
        return;
    }
    // 3. Escort: release the cuff press, then take the cuffed civilian by
    // the arm (one press). They follow the officer from then on and reach
    // extraction with it at the end of the route.
    if(o->escort==SWAT_MOTEL_ESCORT_RELEASE) {
        action[SWAT_ROLE_HEAD_PITCH]=2;
        o->escort=SWAT_MOTEL_ESCORT_TAKE; o->escort_wait=0;
        return;
    }
    if(o->escort==SWAT_MOTEL_ESCORT_TAKE) {
        int arm=-1;
        for(int r=0;r<SWAT_ROLES_RAYS;r++) if(v.ray[r].kind==SWAT_SEEN_CIVILIAN && v.ray[r].restrained && v.ray[r].depth<2.0f &&
            (arm<0 || v.ray[r].depth<v.ray[arm].depth-.25f)) arm=r;
        if(arm==0 && v.ray[0].depth<1.6f) {
            action[SWAT_ROLE_HEAD_PITCH]=2;
            action[SWAT_ROLE_HEAD_INTERACT]=1; o->escort=SWAT_MOTEL_ESCORT_NONE;
        } else if(arm>=0) {
            action[SWAT_ROLE_HEAD_PITCH]=(float)swat_baseline_pitch_bin(v.ray[arm].pitch);
            action[SWAT_ROLE_HEAD_YAW]=(float)swat_baseline_yaw_bin(v.ray[arm].yaw);
        }
        if(++o->escort_wait>12) o->escort=SWAT_MOTEL_ESCORT_NONE;
        return;
    }
    // 4. Weapons to recover.
    if(o->spots>0 && swat_motel_sweep(o,m,&v,obs,x,z,yaw,action)) return;
    // 5. The route.
    swat_motel_officer_route(o,m,&v,obs,x,z,yaw,coin,action);
}

// The motel officer's action for one decision, with that decision's coin
// (swat_baseline_coin). Writes all SWAT_ROLES_HEADS heads, always legal
// under the officer mask.
static void swat_motel_officer_act(SwatMotelOfficer* o,const SwatMotelMap* m,const float obs[SWAT_ROLES_OBS],
                                   uint32_t coin,float action[SWAT_ROLES_HEADS]) {
    for(int h=0;h<SWAT_ROLES_MOTOR_HEADS;h++) action[h]=(float)swat_roles_neutral[h];
    bool held=obs[SWAT_SELF_ALIVE]<.5f || obs[SWAT_SELF_SURRENDERED]>.5f ||
        obs[SWAT_SELF_RESTRAINED]>.5f || obs[SWAT_SELF_STUNNED]>0;
    if(!held && obs[SWAT_SELF_OFFICER]>.5f) {
        swat_motel_officer_decide(o,m,obs,coin,action);
        // With no challenge open, call out a compliance command on every
        // other decision (a command needs a fresh press): anyone it reaches
        // within 9 m of the aim may put their hands up (sim command rule).
        // Only a call with the suspect on the reticle counts as the
        // challenge, because only then is it sure to reach them. Never
        // while a challenge runs: a new command restarts the challenged
        // suspect's clock. Never on a decision that fires: the sim applies
        // the command first, and the shot would then hit a person who has
        // just surrendered.
        if(o->challenge==0 && (o->decisions&1) && !o->commanded && action[SWAT_ROLE_HEAD_TRIGGER]<.5f)
            action[SWAT_ROLE_HEAD_COMMAND]=1;
    }
    o->commanded=action[SWAT_ROLE_HEAD_COMMAND]>.5f;
    // Gaze: the baseline fan. The +-10 and +-25 rays step down through
    // -15 and -40 deg in turn, so kneeling and crouched people close by
    // are seen too.
    o->decisions++;
    for(int k=0;k<SWAT_GAZE_RAYS;k++) {
        int pitch=SWAT_GAZE_PITCH_BINS/2;
        if(k==3 || k==4) pitch=2-(o->decisions+k)%3;
        else if(k==2 || k==5) pitch=2-(o->decisions+k)%2;
        action[SWAT_ROLE_HEAD_GAZE+2*k]=(float)swat_baseline_fan[k];
        action[SWAT_ROLE_HEAD_GAZE+2*k+1]=(float)pitch;
    }
    o->interact=action[SWAT_ROLE_HEAD_INTERACT]>.5f;
}
#endif
