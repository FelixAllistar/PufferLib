#include "mission.h"

static const SwatMissionDef missions[SWAT_MISSION_COUNT]={
    {"Training annex","Clear the range and extract.",{0,0,0},{21,0,0},0,{{0}}},
    {"Cedar House","Two reported gunmen. Three hostages. Secure occupants and return to staging.",
        {0,0,-2},{-1.5f,0,-2},2,
        {{"South ridge",{15,3,-13},{14,1.4f,-2}},
         {"East utility platform",{25,3.6f,3},{15,1.3f,2.8f}}}},
};
const SwatMissionDef* swat_mission(int mission) {
    return &missions[mission>=0 && mission<SWAT_MISSION_COUNT ? mission : SWAT_ANNEX];
}
static int piece(SwatWorld* w,b3Pos origin,float yaw,b3Vec3 p,b3Vec3 half,
                  SwatMaterial material,float health,SwatPart part) {
    float c=cosf(yaw),s=sinf(yaw);
    b3Pos center=b3OffsetPos(origin,swat_v(c*p.x+s*p.z,p.y,-s*p.x+c*p.z));
    int id=swat_world_box(w,center,half,material,health);
    w->objects[id].part=part; swat_world_place(&w->objects[id],yaw); return id;
}
static void skin(SwatWorld* w,b3Pos o,float yaw,float z0,float z1,float y0,float y1,bool exterior) {
    if(z1-z0<.001f || y1-y0<.001f) return;
    int columns=(int)ceilf((z1-z0)/.6f),rows=(int)ceilf((y1-y0)/.9f);
    float dz=(z1-z0)/columns,dy=(y1-y0)/rows;
    for(int side=-1;side<=1;side+=2) for(int z=0;z<columns;z++) for(int y=0;y<rows;y++) {
        bool outer=exterior && side<0; float thickness=outer ? .016f : .0125f;
        piece(w,o,yaw,swat_v(side*(.045f+thickness*.5f),y0+(y+.5f)*dy,z0+(z+.5f)*dz),
            swat_v(thickness*.5f,dy*.5f,dz*.5f),outer ? SWAT_PLASTER : SWAT_DRYWALL,
            outer ? 55 : 38,SWAT_PART_SKIN);
    }
}
void swat_build_framed_wall(SwatWorld* w,b3Pos o,float yaw,float length,float height,
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
            if(sill>.1f) piece(w,o,yaw,swat_v(0,sill*.5f,z),swat_v(.0445f,sill*.5f,.019f),SWAT_WOOD,150,SWAT_PART_FRAME);
            float top=sill+opening_height;
            if(height>top) piece(w,o,yaw,swat_v(0,(height+top)*.5f,z),swat_v(.0445f,(height-top)*.5f,.019f),SWAT_WOOD,150,SWAT_PART_FRAME);
        } else piece(w,o,yaw,swat_v(0,height*.5f,z),swat_v(.0445f,height*.5f,.019f),SWAT_WOOD,180,SWAT_PART_FRAME);
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
            SwatObject* d=&w->objects[id]; d->door=true; d->closed_yaw=yaw;
            d->hinge=b3OffsetPos(d->center,swat_v(-sinf(yaw)*d->half.z,0,-cosf(yaw)*d->half.z));
        }
    }
}
void swat_mission_build_house(SwatWorld* w) {
    swat_world_box(w,(b3Pos){9,-.5f,0},swat_v(24,.5f,18),SWAT_SOIL,0);
    swat_world_box(w,(b3Pos){11,-.1f,0},swat_v(7,.1f,5),SWAT_CONCRETE,0);
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
}
