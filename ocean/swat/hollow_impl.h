// The Hollow (SWAT_HOLLOW): authoritative collision map for a rural property
// at the end of a gravel lane. See HOLLOW.md for the site plan and sources.
// Included at the end of mission.c so it shares the wall-piece helper
// (piece(), SwatWallBuild). Every object is an ordinary world box, so the map
// replicates, saves and replays like Cedar House; the Blender shells in
// assets/environment/hollow_v1 are drawn over these boxes.
//
// The house is an Appalachian boxed house: one layer of wide vertical boards
// nailed to a sill and a plate, no studs. Each board is its own breakable
// SKIN piece; sills, plates, corner posts, jambs and heads are SUPPORT.

// at: world coordinate along the wall. sill 0 makes a door; hole: no leaf and no glass.
#include "hollow_data.h"
typedef struct HollowOpening { float at,width,sill,height; bool hole; } HollowOpening;
enum { HOLLOW_BOARD_HEALTH=45 };

static float hollow_yaw(bool along_x,int swing) {
    // The framed-wall door leaf swings to its local +x; local z runs along the wall.
    if(along_x) return swing>0 ? -SWAT_PI*.5f : SWAT_PI*.5f;
    return swing>0 ? 0 : SWAT_PI;
}
static void hollow_boards(SwatWallBuild* b,b3Pos o,float yaw,float z0,float z1,float y0,float y1,float board) {
    if(z1-z0<.01f || y1-y0<.01f) return;
    int n=(int)ceilf((z1-z0)/board-.15f); if(n<1) n=1;
    float dz=(z1-z0)/n;
    for(int i=0;i<n;i++)
        piece(b,o,yaw,swat_v(0,(y0+y1)*.5f,z0+(i+.5f)*dz),swat_v(.0125f,(y1-y0)*.5f,dz*.5f),SWAT_WOOD,HOLLOW_BOARD_HEALTH,SWAT_PART_SKIN);
}
static void hollow_door_leaf(SwatWallBuild* b,b3Pos o,float yaw,float center,float width,float height,bool locked) {
    // Same clearances, stops and hinge as the framed-wall door (mission.c).
    const float hinge_gap=.012f,latch_gap=.005f,head_gap=.003f,floor_gap=.025f;
    int id=piece(b,o,yaw,swat_v(0,(floor_gap+height-head_gap)*.5f,center+(hinge_gap-latch_gap)*.5f),
                 swat_v(.022f,(height-head_gap-floor_gap)*.5f,width*.5f-(hinge_gap+latch_gap)*.5f),SWAT_WOOD,110,SWAT_PART_SOLID);
    const float stop_x=-(.022f+.002f+.006f),stop_t=.006f,stop_w=.008f;
    for(int side=-1;side<=1;side+=2)
        piece(b,o,yaw,swat_v(stop_x,height*.5f,center+side*(width*.5f-stop_w)),swat_v(stop_t,height*.5f,stop_w),SWAT_WOOD,0,SWAT_PART_SUPPORT);
    piece(b,o,yaw,swat_v(stop_x,height-stop_w,center),swat_v(stop_t,stop_w,width*.5f-2*stop_w),SWAT_WOOD,0,SWAT_PART_SUPPORT);
    if(b->world) {
        SwatObject* d=&b->world->objects[id]; d->door=true; d->closed_yaw=yaw; d->locked=locked;
        d->hinge=b3OffsetPos(d->center,swat_v(-sinf(yaw)*d->half.z,0,-cosf(yaw)*d->half.z));
        swat_world_tilt(d,0);
    }
}
// Axis-aligned board wall from (x0,z0) to (x1,z1) standing on y. Doors have
// sill 0 and swing toward `swing` (+1: +z for an x wall, +x for a z wall).
// height<=0 openings are plain holes (no leaf, no glass).
static void hollow_wall_build(SwatWallBuild* b,float x0,float z0,float x1,float z1,float y,float height,int swing,
                              const HollowOpening* open,int count,bool locked,float board) {
    bool along_x=fabsf(x1-x0)>fabsf(z1-z0);
    float yaw=hollow_yaw(along_x,swing);
    b3Pos o={(x0+x1)*.5f,y,(z0+z1)*.5f};
    float length=along_x ? fabsf(x1-x0) : fabsf(z1-z0),dir_x=sinf(yaw),dir_z=cosf(yaw);
    float lo[8],hi[8],sill[8],top[8]; bool hole[8]; int n=0;
    for(int i=0;i<count && n<8;i++) {
        float c=along_x ? (open[i].at-(float)o.x)*dir_x : (open[i].at-(float)o.z)*dir_z;
        lo[n]=c-open[i].width*.5f; hi[n]=c+open[i].width*.5f; sill[n]=open[i].sill;
        top[n]=open[i].height>0 ? open[i].sill+open[i].height : height; hole[n]=open[i].hole || open[i].height<=0; n++;
    }
    for(int i=1;i<n;i++) for(int j=i;j>0 && lo[j]<lo[j-1];j--) { // sort along the wall
        float t; t=lo[j];lo[j]=lo[j-1];lo[j-1]=t; t=hi[j];hi[j]=hi[j-1];hi[j-1]=t;
        t=sill[j];sill[j]=sill[j-1];sill[j-1]=t; t=top[j];top[j]=top[j-1];top[j-1]=t;
        bool h=hole[j];hole[j]=hole[j-1];hole[j-1]=h;
    }
    float at=-length*.5f;
    for(int i=0;i<n;i++) {
        hollow_boards(b,o,yaw,at,lo[i]-.05f,0,height,board);
        hollow_boards(b,o,yaw,lo[i],hi[i],0,sill[i]>.1f ? sill[i]-.04f : 0,board);
        hollow_boards(b,o,yaw,lo[i],hi[i],top[i]+.06f,height,board);
        // Jambs, head and (for windows) the sash with its glazing.
        for(int side=-1;side<=1;side+=2)
            piece(b,o,yaw,swat_v(0,height*.5f,side<0 ? lo[i]-.025f : hi[i]+.025f),swat_v(.04f,height*.5f,.025f),SWAT_WOOD,0,SWAT_PART_SUPPORT);
        if(top[i]<height-.01f)
            piece(b,o,yaw,swat_v(0,top[i]+.03f,(lo[i]+hi[i])*.5f),swat_v(.04f,.03f,(hi[i]-lo[i])*.5f),SWAT_WOOD,0,SWAT_PART_SUPPORT);
        float width=hi[i]-lo[i],center=(lo[i]+hi[i])*.5f;
        if(!hole[i] && sill[i]>.1f) {
            piece(b,o,yaw,swat_v(0,sill[i]-.02f,center),swat_v(.05f,.02f,width*.5f),SWAT_WOOD,0,SWAT_PART_SUPPORT);
            piece(b,o,yaw,swat_v(0,sill[i]+(top[i]-sill[i])*.5f,center),swat_v(.003f,(top[i]-sill[i])*.5f,width*.5f),SWAT_GLASS,12,SWAT_PART_SOLID);
        } else if(!hole[i]) hollow_door_leaf(b,o,yaw,center,width,top[i],locked);
        at=hi[i]+.05f;
    }
    hollow_boards(b,o,yaw,at,length*.5f,0,height,board);
    // Sill and plate: the boards are nailed to these. Sills break at door openings.
    piece(b,o,yaw,swat_v(0,height-.05f,0),swat_v(.05f,.05f,length*.5f),SWAT_WOOD,0,SWAT_PART_SUPPORT);
    float start=-length*.5f;
    for(int i=0;i<=n;i++) {
        float end=i<n ? lo[i] : length*.5f;
        if(i<n && sill[i]>.1f) continue;
        if(end-start>.02f) piece(b,o,yaw,swat_v(0,.04f,(start+end)*.5f),swat_v(.05f,.04f,(end-start)*.5f),SWAT_WOOD,0,SWAT_PART_SUPPORT);
        if(i<n) start=hi[i];
    }
}
static void hollow_wall(SwatWorld* w,float x0,float z0,float x1,float z1,float y,float height,int swing,
                        const HollowOpening* open,int count,bool locked,float board) {
    int first=w->count;
    SwatWallBuild b={w,0};
    hollow_wall_build(&b,x0,z0,x1,z1,y,height,swing,open,count,locked,board);
    for(int i=first;i<w->count;i++) w->objects[i].wall_group=first+1;
}
static int hollow_box(SwatWorld* w,float x0,float y0,float z0,float x1,float y1,float z1,SwatMaterial m,float hp,SwatPart part) {
    int id=swat_world_box(w,(b3Pos){(x0+x1)*.5f,(y0+y1)*.5f,(z0+z1)*.5f},swat_v((x1-x0)*.5f,(y1-y0)*.5f,(z1-z0)*.5f),m,hp);
    w->objects[id].part=part; return id;
}
// A pitched roof plane: a slab from the eave line to the ridge.
static void hollow_roof(SwatWorld* w,b3Pos eave_mid,b3Pos ridge_mid,float half_length,bool ridge_along_x,SwatMaterial m) {
    b3Vec3 d=b3SubPos(ridge_mid,eave_mid);
    float run=ridge_along_x ? (float)d.z : (float)d.x,rise=(float)d.y,span=sqrtf(run*run+rise*rise);
    b3Pos c={(eave_mid.x+ridge_mid.x)*.5f,(eave_mid.y+ridge_mid.y)*.5f+.06f,(eave_mid.z+ridge_mid.z)*.5f};
    int id=swat_world_box(w,c,swat_v(span*.5f+.25f,.05f,half_length),m,0);
    // The slab's local x runs up the slope: yaw turns it, pitch tips it about local z.
    float yaw=ridge_along_x ? (run>0 ? -SWAT_PI*.5f : SWAT_PI*.5f) : (run>0 ? 0 : SWAT_PI);
    swat_world_place(&w->objects[id],yaw); swat_world_tilt(&w->objects[id],atan2f(rise,fabsf(run)));
}
// Stepped board gable fill above the plate, closing the triangle for sight and light.
static void hollow_gable(SwatWorld* w,float x0,float z0,float x1,float z1,float y,float rise) {
    bool along_x=fabsf(x1-x0)>fabsf(z1-z0);
    float length=along_x ? fabsf(x1-x0) : fabsf(z1-z0); const int steps=6;
    for(int i=0;i<steps;i++) {
        float h=rise/steps,width=length*(1-(i+.5f)/steps);
        b3Pos c={(x0+x1)*.5f,y+(i+.5f)*h,(z0+z1)*.5f};
        int id=swat_world_box(w,c,along_x ? swat_v(width*.5f,h*.5f,.0125f) : swat_v(.0125f,h*.5f,width*.5f),SWAT_WOOD,HOLLOW_BOARD_HEALTH);
        w->objects[id].part=SWAT_PART_SKIN;
    }
}
static void hollow_room(SwatWorld* w,float x0,float y0,float z0,float x1,float y1,float z1,SwatMaterial floor,b3Pos bulb) {
    w->rooms[w->room_count++]=(SwatRoom){{(x0+x1)*.5f,(y0+y1)*.5f,(z0+z1)*.5f},{(x1-x0)*.5f-.06f,(y1-y0)*.5f,(z1-z0)*.5f-.06f},SWAT_WOOD,floor};
    // One bare bulb per room: a small fixture marks the lamp origin.
    int id=swat_world_box(w,bulb,swat_v(.05f,.06f,.05f),SWAT_STEEL,0); w->objects[id].part=SWAT_PART_LIGHT;
}
// Permanent props (SWAT_PART_OBSTACLE carries no health in the map protocol).
static void hollow_obstacle(SwatWorld* w,float x,float y,float z,float hx,float hy,float hz,float yaw,SwatMaterial m,float hp) {
    (void)hp; int id=swat_world_box(w,(b3Pos){x,y+hy,z},swat_v(hx,hy,hz),m,0);
    w->objects[id].part=SWAT_PART_OBSTACLE; if(yaw!=0) swat_world_place(&w->objects[id],yaw);
}

void swat_mission_build_hollow(SwatWorld* w) {
    w->hollow=true;
    const float F=HOLLOW_FLOOR,U=HOLLOW_UPPER,UT=HOLLOW_UPPER_TOP;
    // --- Ground. Soil stops at the raised house; its own floors own contact.
    hollow_box(w,-8,-1,-24,57,0,-8,SWAT_SOIL,0,SWAT_PART_SOLID);          // lane, yard south
    hollow_box(w,-8,-1,-8,14,0,12,SWAT_SOIL,0,SWAT_PART_SOLID);           // yard west
    hollow_box(w,22,-1,-8,57,0,12,SWAT_SOIL,0,SWAT_PART_SOLID);           // yard east, barn and smokehouse floors
    hollow_box(w,19,-1,0,22,0,3.5f,SWAT_SOIL,0,SWAT_PART_SOLID);          // behind the stair room
    hollow_box(w,14,-1,3.5f,22,0,12,SWAT_SOIL,0,SWAT_PART_SOLID);         // north yard, drag marks to the cellar
    // --- The hill bank north of the yard, with the root cellar dug into it.
    const float bank=2.2f,deep=-1.8f;
    hollow_box(w,-8,deep,12,16.1f,bank,41,SWAT_SOIL,0,SWAT_PART_SOLID);
    hollow_box(w,20.9f,deep,12,57,bank,41,SWAT_SOIL,0,SWAT_PART_SOLID);
    hollow_box(w,16.1f,deep,18.9f,20.9f,bank,41,SWAT_SOIL,0,SWAT_PART_SOLID);
    hollow_box(w,16.1f,deep,12,17.6f,bank,14.1f,SWAT_STONE,0,SWAT_PART_SOLID);   // headwall and passage sides
    hollow_box(w,18.4f,deep,12,20.9f,bank,14.1f,SWAT_STONE,0,SWAT_PART_SOLID);
    hollow_box(w,17.6f,2.0f,12,18.4f,bank,14.1f,SWAT_STONE,0,SWAT_PART_SOLID);  // lintel and earth over the passage
    hollow_box(w,17.6f,deep,12,18.4f,0,12.2f,SWAT_STONE,0,SWAT_PART_SOLID);      // threshold
    for(int i=1;i<=6;i++)                                                         // seven 0.2 m risers down
        hollow_box(w,17.6f,deep,12.2f+(i-1)*.28f,18.4f,-.2f*i,12.2f+i*.28f,SWAT_STONE,0,SWAT_PART_SOLID);
    hollow_box(w,17.6f,deep,13.88f,18.4f,HOLLOW_CELLAR,14.2f,SWAT_STONE,0,SWAT_PART_SOLID);
    hollow_box(w,16.5f,deep,14.2f,20.5f,HOLLOW_CELLAR,18.5f,SWAT_SOIL,0,SWAT_PART_SOLID);  // packed-earth floor
    hollow_box(w,16.1f,deep,14.1f,16.5f,1.0f,18.9f,SWAT_STONE,0,SWAT_PART_SOLID);          // stone liner
    hollow_box(w,20.5f,deep,14.1f,20.9f,1.0f,18.9f,SWAT_STONE,0,SWAT_PART_SOLID);
    hollow_box(w,16.5f,deep,18.5f,20.5f,1.0f,18.9f,SWAT_STONE,0,SWAT_PART_SOLID);
    hollow_box(w,16.5f,deep,14.1f,17.6f,1.0f,14.5f,SWAT_STONE,0,SWAT_PART_SOLID);
    hollow_box(w,18.4f,deep,14.1f,20.5f,1.0f,14.5f,SWAT_STONE,0,SWAT_PART_SOLID);
    hollow_box(w,16.1f,1.0f,14.1f,20.9f,1.2f,18.9f,SWAT_WOOD,0,SWAT_PART_SOLID);          // timber ceiling
    hollow_box(w,16.1f,1.2f,14.1f,20.9f,bank,18.9f,SWAT_SOIL,0,SWAT_PART_SOLID);          // earth over it
    { // Plank cellar door in the headwall, padlocked; it opens outward into the yard.
        SwatWallBuild b={w,0}; int first=w->count;
        hollow_door_leaf(&b,(b3Pos){18,0,12},hollow_yaw(true,-1),0,.8f,1.97f,true);
        for(int i=first;i<w->count;i++) w->objects[i].wall_group=first+1;
    }
    // --- Main house: raised floor on piers, porch, two storeys and a lean-to kitchen.
    hollow_box(w,14,0,-6,22,F,0,SWAT_WOOD,0,SWAT_PART_SOLID);
    hollow_box(w,14,0,0,19,F,3.5f,SWAT_WOOD,0,SWAT_PART_SOLID);
    hollow_box(w,14,0,-8,22,F,-6,SWAT_WOOD,0,SWAT_PART_SOLID);              // porch deck
    hollow_box(w,14.6f,0,-8.4f,21.4f,F*.5f,-8,SWAT_STONE,0,SWAT_PART_SOLID); // one stone step
    const float gh=U-F; // ground storey board height, sill to the upper floor
    HollowOpening south_g[]={{16,.86f,0,2.0f,false},{14.9f,.7f,.85f,1.1f,false},{20,.86f,0,2.0f,false},{21.2f,.7f,.85f,1.1f,false}};
    hollow_wall(w,14,-6,22,-6,F,gh,1,south_g,4,true,.30f);
    HollowOpening north_g[]={{16,.82f,0,2.0f,false}};
    hollow_wall(w,14,0,19,0,F,gh,-1,north_g,1,false,.30f);                 // front room -> kitchen
    hollow_wall(w,19,0,22,0,F,gh,-1,NULL,0,false,.30f);                    // behind the stair
    HollowOpening west_g[]={{-3.6f,.7f,.85f,1.1f,false}};
    hollow_wall(w,14,-6,14,0,F,gh,1,west_g,1,false,.30f);
    hollow_wall(w,22,-6,22,0,F,gh,-1,NULL,0,false,.30f);                   // chimney gable: blind
    HollowOpening part_g[]={{-4.6f,.82f,0,2.0f,false}};
    hollow_wall(w,18,-6,18,0,F,gh,1,part_g,1,false,.30f);
    // Lean-to kitchen.
    HollowOpening kn[]={{17.6f,.86f,0,2.0f,false},{15.3f,.7f,.85f,1.0f,false}};
    hollow_wall(w,14,3.5f,19,3.5f,F,HOLLOW_KITCHEN_H,-1,kn,2,false,.30f);
    HollowOpening kw[]={{1.8f,.7f,.85f,1.0f,false}};
    hollow_wall(w,14,0,14,3.5f,F,HOLLOW_KITCHEN_H,1,kw,1,false,.30f);
    hollow_wall(w,19,0,19,3.5f,F,HOLLOW_KITCHEN_H,-1,NULL,0,false,.30f);
    hollow_box(w,13.9f,F+HOLLOW_KITCHEN_H,-.05f,19.1f,F+HOLLOW_KITCHEN_H+.1f,3.6f,SWAT_STEEL,0,SWAT_PART_SOLID); // tin lean-to roof
    // Upper floor with the stairwell hole, and the steep boxed stair: 0.9 m
    // wide treads between a closet (behind) and a board side wall (room side),
    // open at the foot. The tread line sits on a navigation row (z -0.9).
    const float s0=HOLLOW_STAIR_X,sx0=18.05f,sx1=s0+14*HOLLOW_TREAD,sz0=-1.27f,sz1=-.33f;
    hollow_box(w,14,U-.15f,-6,sx0,U,0,SWAT_WOOD,0,SWAT_PART_SOLID);
    hollow_box(w,sx1,U-.15f,-6,22,U,0,SWAT_WOOD,0,SWAT_PART_SOLID);
    hollow_box(w,sx0,U-.15f,-6,sx1,U,sz0,SWAT_WOOD,0,SWAT_PART_SOLID);
    hollow_box(w,sx0,U-.15f,sz1,sx1,U,0,SWAT_WOOD,0,SWAT_PART_SOLID);
    for(int i=0;i<14;i++)
        hollow_box(w,s0+i*HOLLOW_TREAD,F,-1.25f,s0+(i+1)*HOLLOW_TREAD,F+(i+1)*HOLLOW_RISE,-.35f,SWAT_WOOD,0,SWAT_PART_SOLID);
    hollow_box(w,18.05f,F,-.35f,sx1,U-.15f,-.04f,SWAT_WOOD,0,SWAT_PART_SOLID);          // closet behind the stair
    hollow_box(w,s0+HOLLOW_TREAD,F,-1.285f,sx1,U-.15f,-1.26f,SWAT_WOOD,HOLLOW_BOARD_HEALTH,SWAT_PART_SKIN); // board side wall
    hollow_box(w,sx0,U+.86f,sz0-.04f,sx1-.35f,U+.92f,sz0+.02f,SWAT_WOOD,0,SWAT_PART_SUPPORT);   // stair rail
    for(int i=0;i<3;i++) hollow_box(w,sx0+.02f+i*.9f,U,sz0-.04f,sx0+.08f+i*.9f,U+.86f,sz0+.02f,SWAT_WOOD,0,SWAT_PART_SUPPORT);
    // Upper storey.
    const float uh=UT-U;
    HollowOpening south_u[]={{15.4f,.7f,.75f,1.05f,false},{17.0f,.7f,.75f,1.05f,false},{19.6f,.7f,.75f,1.05f,false},{21.2f,.7f,.75f,1.05f,false}};
    hollow_wall(w,14,-6,22,-6,U,uh,1,south_u,4,false,.30f);
    HollowOpening north_u[]={{16,.7f,.75f,1.05f,false}};
    hollow_wall(w,14,0,22,0,U,uh,-1,north_u,1,false,.30f);
    HollowOpening west_u[]={{-3,.6f,.8f,.9f,false}};
    hollow_wall(w,14,-6,14,0,U,uh,1,west_u,1,false,.30f);
    HollowOpening east_u[]={{-1.2f,.6f,.8f,.9f,false}};
    hollow_wall(w,22,-6,22,0,U,uh,-1,east_u,1,false,.30f);
    HollowOpening part_u[]={{-3.0f,.82f,0,2.0f,false}};
    hollow_wall(w,18,-6,18,0,U,uh,-1,part_u,1,false,.30f);
    hollow_box(w,14,UT,-6,22,UT+.1f,0,SWAT_WOOD,0,SWAT_PART_SOLID);         // attic floor / ceiling
    hollow_gable(w,14,-6,14,0,UT,HOLLOW_RIDGE-UT); hollow_gable(w,22,-6,22,0,UT,HOLLOW_RIDGE-UT);
    hollow_roof(w,(b3Pos){18,UT,-6.4f},(b3Pos){18,HOLLOW_RIDGE,-3},4.3f,true,SWAT_STEEL);
    hollow_roof(w,(b3Pos){18,UT,.4f},(b3Pos){18,HOLLOW_RIDGE,-3},4.3f,true,SWAT_STEEL);
    hollow_roof(w,(b3Pos){18,2.42f,-8.25f},(b3Pos){18,3.05f,-6.02f},4.4f,true,SWAT_STEEL); // porch shed roof (tin), down to the posts
    for(int i=0;i<4;i++) hollow_box(w,14.05f+i*2.6f,F,-7.95f,14.2f+i*2.6f,2.45f,-7.8f,SWAT_WOOD,0,SWAT_PART_SUPPORT);
    // Field-stone gable chimney, stepped like the Wolcott farmhouse's: a broad
    // firebox base, shoulders at the first floor and a stack past the ridge.
    hollow_box(w,22.05f,0,-4.0f,23.15f,2.3f,-2.0f,SWAT_STONE,0,SWAT_PART_SOLID);
    hollow_box(w,22.05f,2.3f,-3.65f,22.95f,UT+.2f,-2.35f,SWAT_STONE,0,SWAT_PART_SOLID);
    hollow_box(w,22.05f,UT+.2f,-3.4f,22.8f,HOLLOW_RIDGE+.45f,-2.6f,SWAT_STONE,0,SWAT_PART_SOLID);
    // --- Smokehouse: windowless board box with a low door.
    HollowOpening smoke_door[]={{25.5f,.78f,0,1.95f,false}};
    hollow_wall(w,24,3,27,3,0,2.2f,1,smoke_door,1,false,.30f);
    hollow_wall(w,24,6,27,6,0,2.2f,-1,NULL,0,false,.30f);
    hollow_wall(w,24,3,24,6,0,2.2f,1,NULL,0,false,.30f);
    hollow_wall(w,27,3,27,6,0,2.2f,-1,NULL,0,false,.30f);
    hollow_gable(w,24,3,27,3,2.2f,1.2f); hollow_gable(w,24,6,27,6,2.2f,1.2f);
    hollow_roof(w,(b3Pos){23.8f,2.2f,4.5f},(b3Pos){25.5f,3.4f,4.5f},1.8f,false,SWAT_STEEL);
    hollow_roof(w,(b3Pos){27.2f,2.2f,4.5f},(b3Pos){25.5f,3.4f,4.5f},1.8f,false,SWAT_STEEL);
    // --- Tool shed.
    HollowOpening shed_door[]={{3.5f,.8f,0,1.98f,false}};
    hollow_wall(w,8.5f,2,8.5f,5,0,2.2f,1,shed_door,1,false,.30f);
    hollow_wall(w,6,2,6,5,0,2.0f,-1,NULL,0,false,.30f);
    hollow_wall(w,6,2,8.5f,2,0,2.0f,1,NULL,0,false,.30f);
    hollow_wall(w,6,5,8.5f,5,0,2.0f,-1,NULL,0,false,.30f);
    hollow_box(w,5.8f,2.2f,1.8f,8.7f,2.28f,5.2f,SWAT_STEEL,0,SWAT_PART_SOLID);
    // --- Barn: tall grey boards, gable end to the yard, hay loft over the rear.
    const float bx0=32,bx1=41,bz0=-8,bz1=4,eave=4.2f,ridge=8.2f;
    HollowOpening barn_s[]={{36.5f,3.0f,0,3.2f,true}}; // big doorway, boards above a header
    hollow_wall(w,bx0,bz0,bx1,bz0,0,eave,1,barn_s,1,false,.36f);
    HollowOpening barn_w[]={{-5.5f,.9f,0,2.0f,false},{1.5f,.6f,1.6f,.6f,true}};
    hollow_wall(w,bx0,bz0,bx0,bz1,0,eave,1,barn_w,2,false,.36f);
    HollowOpening barn_e[]={{-2,.6f,1.6f,.6f,true}};
    hollow_wall(w,bx1,bz0,bx1,bz1,0,eave,-1,barn_e,1,false,.36f);
    HollowOpening barn_n[]={{36.5f,1.2f,2.9f,1.0f,true}};   // open loft hay door
    hollow_wall(w,bx0,bz1,bx1,bz1,0,eave,-1,barn_n,1,false,.36f);
    hollow_gable(w,bx0,bz0,bx1,bz0,eave,ridge-eave); hollow_gable(w,bx0,bz1,bx1,bz1,eave,ridge-eave);
    hollow_roof(w,(b3Pos){bx0-.75f,eave-.33f,(bz0+bz1)*.5f},(b3Pos){(bx0+bx1)*.5f,ridge,(bz0+bz1)*.5f},6.7f,false,SWAT_STEEL);
    hollow_roof(w,(b3Pos){bx1+.75f,eave-.33f,(bz0+bz1)*.5f},(b3Pos){(bx0+bx1)*.5f,ridge,(bz0+bz1)*.5f},6.7f,false,SWAT_STEEL);
    hollow_box(w,bx0+.05f,HOLLOW_LOFT-.12f,-1,bx1-.05f,HOLLOW_LOFT,bz1-.05f,SWAT_WOOD,0,SWAT_PART_SOLID);   // loft floor
    for(int i=0;i<13;i++)   // steep stair up the east wall to the loft
        hollow_box(w,bx1-1.0f,0,-1-(13-i)*.25f,bx1-.1f,(i+1)*HOLLOW_LOFT/13,-1-(12-i)*.25f,SWAT_WOOD,0,SWAT_PART_SOLID);
    hollow_box(w,bx0+.1f,HOLLOW_LOFT,-1.04f,bx1-1.1f,HOLLOW_LOFT+.95f,-.98f,SWAT_WOOD,0,SWAT_PART_SUPPORT); // loft edge rail
    // --- Props with real footprints: cover and set dressing (hollow_data.h).
    for(int i=0;i<SWAT_HOLLOW_PROPS;i++) {
        const SwatHollowProp* p=&swat_hollow_props[i];
        if(p->solid) hollow_obstacle(w,p->x,p->y,p->z,p->hx,p->hy,p->hz,p->yaw,p->material,0);
    }
    // Overwatch stands, as at Cedar House (the hill post stands on the bank).
    for(int i=0;i<missions[SWAT_HOLLOW].overwatch_count;i++) {
        b3Pos p=missions[SWAT_HOLLOW].overwatch[i].position; float top=(float)p.y-1.6256f;
        if(p.z>=12 && top<=bank+.01f) continue;
        swat_world_box(w,(b3Pos){p.x,top*.5f,p.z},swat_v(.85f,top*.5f,.85f),SWAT_STONE,0);
    }
    // --- Rooms (eight: the SwatRoom limit), each with its own bare bulb.
    w->room_count=0;
    hollow_room(w,14,F,-6,18,U-.15f,0,SWAT_WOOD,(b3Pos){16,U-.25f,-3});
    hollow_room(w,18,F,-6,22,U-.15f,0,SWAT_WOOD,(b3Pos){20,U-.25f,-3.4f});
    hollow_room(w,14,F,0,19,F+HOLLOW_KITCHEN_H,3.5f,SWAT_WOOD,(b3Pos){16.5f,F+HOLLOW_KITCHEN_H-.25f,1.8f});
    hollow_room(w,14,U,-6,18,UT,0,SWAT_WOOD,(b3Pos){16,UT-.2f,-3});
    hollow_room(w,18,U,-6,22,UT,0,SWAT_WOOD,(b3Pos){20.4f,UT-.2f,-3.4f});
    hollow_room(w,bx0,0,bz0,bx1,eave,bz1,SWAT_SOIL,(b3Pos){36.5f,HOLLOW_LOFT-.3f,-4});
    hollow_room(w,24,0,3,27,2.2f,6,SWAT_SOIL,(b3Pos){25.5f,2.0f,4.5f});
    hollow_room(w,16.5f,HOLLOW_CELLAR,14.5f,20.5f,1.0f,18.5f,SWAT_SOIL,(b3Pos){18.5f,.85f,16.5f});
}
