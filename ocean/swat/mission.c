#include "mission.h"
#include <assert.h>

static const SwatMissionDef missions[SWAT_MISSION_COUNT]={
    {"Training annex","Clear the range and extract.",{0,0,0},{21,0,0},0,{{0}}},
    {"Cedar House","Two reported gunmen. Three hostages. Secure occupants and return to staging.",
        {0,0,-2},{-1.5f,0,-2},3,
        {{"South ridge",{15,3,-13},{14,1.4f,-2}},
         {"East utility platform",{25,2.6f,3},{15,1.3f,2.8f}},
         {"South garden",{10.5f,2.4f,-13},{15,1.55f,-2}}}},
    {"Generated residence","Secure the occupants and return to staging.",{0,0,0},{-1.5f,0,0},3,
        {{"Window post A",{0,0,0},{0,0,0}},{"Window post B",{0,0,0},{0,0,0}},{"Window post C",{0,0,0},{0,0,0}}}},
    {"Controller test range","Stairs, ramps, crouch clearance, cover, doors and target lanes. No live hostiles.",
        {0,0,0},{0,0,0},0,{{0}}},
    {"Briar Court motel","Secure the rooms and reception, restrain occupants, then return to staging.",
        {0,-.079f,8},{0,-.079f,10},0,{{0}}},
    {"Morrow Block","Clear the laundromat, pawn shop and shared rear service area. Secure occupants and return to the street.",
        {-3,-.139f,5},{-3,-.139f,7},0,{{0}}},
    {"Generated building scenario","Reported armed occupants and civilians. Secure occupants and return to staging.",{0,0,3},{-1.5f,0,3},0,{{0}}},
};
const SwatMissionDef* swat_mission(int mission) {
    return &missions[mission>=0 && mission<SWAT_MISSION_COUNT ? mission : SWAT_ANNEX];
}
typedef struct SwatWallBuild { SwatWorld* world; int count; } SwatWallBuild;
static int piece(SwatWallBuild* build,b3Pos origin,float yaw,b3Vec3 p,b3Vec3 half,
                  SwatMaterial material,float health,SwatPart part) {
    build->count++;
    SwatWorld* w=build->world; if(!w) return 0;
    float c=cosf(yaw),s=sinf(yaw);
    b3Pos center=b3OffsetPos(origin,swat_v(c*p.x+s*p.z,p.y,-s*p.x+c*p.z));
    int id=swat_world_box(w,center,half,material,health);
    w->objects[id].part=part; swat_world_place(&w->objects[id],yaw); return id;
}
static float fracture_noise(b3Pos origin,int z,int y,int axis) {
    uint32_t h=(uint32_t)llround(origin.x*1000) ^ (uint32_t)llround(origin.z*1000)*0x9e3779b9u ^
        (uint32_t)llround(origin.y*1000)*0x85ebca6bu ^ (uint32_t)z*374761393u ^ (uint32_t)y*668265263u ^ (uint32_t)axis*2246822519u;
    h=(h^(h>>13))*1274126177u; h^=h>>16; return (h%10001)/5000.0f-1;
}
static void stud(SwatWallBuild* w,b3Pos o,float yaw,float z,float lo,float hi) {
    int n=(int)ceilf((hi-lo)/1.0f);
    for(int i=0;i<n;i++) {
        float height=(hi-lo)/n;
        piece(w,o,yaw,swat_v(0,lo+(i+.5f)*height,z),swat_v(.0445f,height*.5f,.019f),SWAT_WOOD,150,SWAT_PART_FRAME);
    }
}
static void skin(SwatWallBuild* w,b3Pos o,float yaw,float z0,float z1,float y0,float y1,bool exterior) {
    if(z1-z0<.001f || y1-y0<.001f) return;
    int columns=(int)ceilf((z1-z0)/.85f),rows=(int)ceilf((y1-y0)/.95f);
    if(!w->world) { w->count+=2*columns*rows; return; }
    float dz=(z1-z0)/columns,dy=(y1-y0)/rows;
    for(int side=-1;side<=1;side+=2) for(int z=0;z<columns;z++) for(int y=0;y<rows;y++) {
        bool outer=exterior && side<0; float thickness=outer ? .016f : .0125f;
        float v[4][2],min_y=1e9f,max_y=-1e9f,min_z=1e9f,max_z=-1e9f;
        const int offsets[4][2]={{0,0},{0,1},{1,1},{1,0}};
        for(int k=0;k<4;k++) {
            int gz=z+offsets[k][0],gy=y+offsets[k][1];
            v[k][0]=y0+gy*dy+(gy>0 && gy<rows ? .16f*dy*fracture_noise(o,gz,gy,0) : 0);
            v[k][1]=z0+gz*dz+(gz>0 && gz<columns ? .20f*dz*fracture_noise(o,gz,gy,1) : 0);
            min_y=fminf(min_y,v[k][0]); max_y=fmaxf(max_y,v[k][0]); min_z=fminf(min_z,v[k][1]); max_z=fmaxf(max_z,v[k][1]);
        }
        float cy=(min_y+max_y)*.5f,cz=(min_z+max_z)*.5f;
        int id=piece(w,o,yaw,swat_v(side*(.045f+thickness*.5f),cy,cz),
            swat_v(thickness*.5f,(max_y-min_y)*.5f,(max_z-min_z)*.5f),outer ? SWAT_PLASTER : SWAT_DRYWALL,
            outer ? 55 : 38,SWAT_PART_SKIN);
        for(int k=0;k<4;k++) { v[k][0]-=cy; v[k][1]-=cz; }
        bool valid=swat_world_fragment(&w->world->objects[id],v); assert(valid); (void)valid;
    }
}
static void framed_wall(SwatWallBuild* w,b3Pos o,float yaw,float length,float height,
                            float opening_center,float opening_width,float sill,float opening_height,bool exterior) {
    float left=-length*.5f,right=length*.5f;
    float lo=opening_center-opening_width*.5f,hi=opening_center+opening_width*.5f;
    if(opening_width>0) {
        skin(w,o,yaw,left,lo,0,height,exterior); skin(w,o,yaw,hi,right,0,height,exterior);
        skin(w,o,yaw,lo,hi,0,sill,exterior); skin(w,o,yaw,lo,hi,sill+opening_height,height,exterior);
    } else skin(w,o,yaw,left,right,0,height,exterior);
    // 38 x 89 mm timber, approximately 400 mm centres. Both board faces can
    // break independently; actual studs/plates remain physical obstructions.
    int count=(int)ceilf(length/.4f);
    for(int i=0;i<=count;i++) {
        float z=left+i*length/count;
        if(opening_width>0 && z>lo-.04f && z<hi+.04f) {
            if(sill>.1f) stud(w,o,yaw,z,0,sill);
            float top=sill+opening_height;
            if(height>top) stud(w,o,yaw,z,top,height);
        } else stud(w,o,yaw,z,0,height);
    }
    piece(w,o,yaw,swat_v(0,height-.019f,0),swat_v(.0445f,.019f,length*.5f),SWAT_WOOD,0,SWAT_PART_SUPPORT);
    if(opening_width<=0 || sill>.1f)
        piece(w,o,yaw,swat_v(0,.019f,0),swat_v(.0445f,.019f,length*.5f),SWAT_WOOD,0,SWAT_PART_SUPPORT);
    else {
        piece(w,o,yaw,swat_v(0,.019f,(left+lo)*.5f),swat_v(.0445f,.019f,(lo-left)*.5f),SWAT_WOOD,0,SWAT_PART_SUPPORT);
        piece(w,o,yaw,swat_v(0,.019f,(hi+right)*.5f),swat_v(.0445f,.019f,(right-hi)*.5f),SWAT_WOOD,0,SWAT_PART_SUPPORT);
    }
    if(opening_width>0) {
        for(int side=-1;side<=1;side+=2)
            piece(w,o,yaw,swat_v(0,height*.5f,opening_center+side*(opening_width*.5f+.038f)),
                  swat_v(.0445f,height*.5f,.038f),SWAT_WOOD,0,SWAT_PART_SUPPORT);
        piece(w,o,yaw,swat_v(0,sill+opening_height+.07f,opening_center),swat_v(.0445f,.07f,opening_width*.5f),SWAT_WOOD,0,SWAT_PART_SUPPORT);
        if(sill>.1f)
            piece(w,o,yaw,swat_v(0,sill+opening_height*.5f,opening_center),
                  swat_v(.003f,opening_height*.5f,opening_width*.5f),SWAT_GLASS,12,SWAT_PART_SOLID);
        else {
            int id=piece(w,o,yaw,swat_v(0,(.08f+opening_height)*.5f,opening_center),
                         swat_v(.022f,(opening_height-.08f)*.5f,opening_width*.5f-.04f),SWAT_WOOD,120,SWAT_PART_SOLID);
            if(w->world) {
                SwatObject* d=&w->world->objects[id]; d->door=true; d->closed_yaw=yaw; d->locked=exterior;
                d->hinge=b3OffsetPos(d->center,swat_v(-sinf(yaw)*d->half.z,0,-cosf(yaw)*d->half.z));
            }
        }
    }
}
void swat_build_framed_wall(SwatWorld* w,b3Pos origin,float yaw,float length,float height,
                            float opening_center,float opening_width,float sill,float opening_height,bool exterior) {
    int first=w->count;
    SwatWallBuild build={w,0};
    framed_wall(&build,origin,yaw,length,height,opening_center,opening_width,sill,opening_height,exterior);
    for(int i=first;i<w->count;i++) w->objects[i].wall_group=first+1;
}
int swat_framed_wall_pieces(float length,float height,float opening_center,float opening_width,
                            float sill,float opening_height,bool exterior) {
    SwatWallBuild build={0};
    framed_wall(&build,(b3Pos){0},0,length,height,opening_center,opening_width,sill,opening_height,exterior);
    return build.count;
}
void swat_mission_build_house(SwatWorld* w) {
    // Exterior ground stops at the foundation. Finish floors own the contact
    // surface; overlapping soil colliders would make friction ambiguous.
    swat_world_box(w,(b3Pos){-5.5f,-.5f,0},swat_v(9.5f,.5f,18),SWAT_SOIL,0);
    swat_world_box(w,(b3Pos){25.5f,-.5f,0},swat_v(7.5f,.5f,18),SWAT_SOIL,0);
    swat_world_box(w,(b3Pos){11,-.5f,-11.5f},swat_v(7,.5f,6.5f),SWAT_SOIL,0);
    swat_world_box(w,(b3Pos){11,-.5f,11.5f},swat_v(7,.5f,6.5f),SWAT_SOIL,0);
    swat_world_box(w,(b3Pos){11,-.12f,0},swat_v(7,.1f,5),SWAT_CONCRETE,0);
    swat_world_box(w,(b3Pos){7.5f,-.01f,0},swat_v(3.5f,.01f,5),SWAT_WOOD,0);
    swat_world_box(w,(b3Pos){14.5f,-.01f,-2},swat_v(3.5f,.01f,3),SWAT_CARPET,0);
    swat_world_box(w,(b3Pos){14.5f,-.01f,3},swat_v(3.5f,.01f,2),SWAT_TILE,0);
    // Frame, skins, headers and foundation use distinct geometry/materials.
    swat_build_framed_wall(w,(b3Pos){4,0,0},0,10,2.7f,-2,1.2f,0,2.1f,true);
    swat_build_framed_wall(w,(b3Pos){18,0,-2.5f},SWAT_PI,5,2.7f,0,1.8f,.85f,1.25f,true);
    swat_build_framed_wall(w,(b3Pos){18,0,2.5f},SWAT_PI,5,2.7f,0,1.8f,.85f,1.25f,true);
    swat_build_framed_wall(w,(b3Pos){7.5f,0,-5},-SWAT_PI*.5f,7,2.7f,0,1.5f,.85f,1.25f,true);
    swat_build_framed_wall(w,(b3Pos){14.5f,0,-5},-SWAT_PI*.5f,7,2.7f,0,1.8f,.85f,1.25f,true);
    swat_build_framed_wall(w,(b3Pos){7.5f,0,5},SWAT_PI*.5f,7,2.7f,-1,1.2f,0,2.1f,true);
    swat_build_framed_wall(w,(b3Pos){14.5f,0,5},SWAT_PI*.5f,7,2.7f,0,1.8f,.85f,1.25f,true);
    swat_build_framed_wall(w,(b3Pos){11,0,0},0,10,2.7f,0,1.2f,0,2.1f,false);
    swat_build_framed_wall(w,(b3Pos){14.5f,0,1},SWAT_PI*.5f,7,2.7f,-.8f,1.2f,0,2.1f,false);
    w->rooms[0]=(SwatRoom){{7.5f,1.35f,0},{3.4f,1.35f,4.9f},SWAT_DRYWALL,SWAT_WOOD};
    w->rooms[1]=(SwatRoom){{14.5f,1.35f,-2},{3.4f,1.35f,2.9f},SWAT_DRYWALL,SWAT_CARPET};
    w->rooms[2]=(SwatRoom){{14.5f,1.35f,3},{3.4f,1.35f,1.9f},SWAT_DRYWALL,SWAT_TILE}; w->room_count=3;
    // Solid roof participates in propagation/collision; planning uses a cutaway.
    swat_world_box(w,(b3Pos){11,2.8f,0},swat_v(7.15f,.10f,5.15f),SWAT_WOOD,0);
    swat_world_box(w,(b3Pos){8,.42f,2.7f},swat_v(1.2f,.42f,.45f),SWAT_WOOD,130);
    swat_world_box(w,(b3Pos){14,.4f,-3.7f},swat_v(1.4f,.4f,.5f),SWAT_WOOD,130);
    swat_world_box(w,(b3Pos){16,.45f,3.8f},swat_v(.8f,.45f,.5f),SWAT_WOOD,130);
    swat_world_box(w,(b3Pos){-3,.3f,5},swat_v(2,.3f,2.5f),SWAT_SOIL,0);
    swat_world_box(w,(b3Pos){22,1,9},swat_v(3,1,1.8f),SWAT_BRICK,0);
    for(int i=0;i<missions[SWAT_HOUSE].overwatch_count;i++) {
        b3Pos p=missions[SWAT_HOUSE].overwatch[i].position;
        float top=(float)p.y-1.6256f;
        swat_world_box(w,(b3Pos){p.x,top*.5f,p.z},swat_v(.85f,top*.5f,.85f),SWAT_BRICK,0);
    }
}

void swat_mission_build_test_range(SwatWorld* w) {
    swat_world_box(w,(b3Pos){12,-.5f,0},swat_v(18,.5f,18),SWAT_CONCRETE,0);
    // Each obstacle occupies a separate approach lane.
    for(int i=0;i<6;i++) swat_world_box(w,(b3Pos){4+i*.65f,(i+1)*.10f,0},swat_v(.325f,(i+1)*.10f,1),SWAT_CONCRETE,0);
    int ramp=swat_world_box(w,(b3Pos){6,.72f,-5},swat_v(2.5f,.15f,1),SWAT_WOOD,0);
    swat_world_tilt(&w->objects[ramp],20*SWAT_RAD);
    ramp=swat_world_box(w,(b3Pos){6,2.15f,-10},swat_v(2.5f,.15f,1),SWAT_CONCRETE,0);
    swat_world_tilt(&w->objects[ramp],60*SWAT_RAD);
    swat_world_box(w,(b3Pos){6,1.7f,5},swat_v(2,.6f,1),SWAT_CONCRETE,0);
    swat_world_box(w,(b3Pos){12,.50f,0},swat_v(2,.50f,1),SWAT_WOOD,120);
    swat_world_box(w,(b3Pos){12,1.3f,-5},swat_v(.08f,1.3f,1.5f),SWAT_DRYWALL,45);
    swat_build_framed_wall(w,(b3Pos){12,0,5},0,5,2.7f,0,1.2f,0,2.1f,false);
    swat_world_box(w,(b3Pos){20,1.2f,0},swat_v(.1f,1.2f,.4f),SWAT_WOOD,100);
    swat_world_box(w,(b3Pos){20,1.2f,-5},swat_v(.1f,1.2f,.4f),SWAT_STEEL,0);
    w->rooms[0]=(SwatRoom){{12,1.5f,0},{17,1.5f,17},SWAT_CONCRETE,SWAT_CONCRETE}; w->room_count=1;
}
