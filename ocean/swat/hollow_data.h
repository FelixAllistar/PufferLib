// The Hollow: shared placement table for props (HOLLOW.md section 7).
// Rows with `solid` become SWAT_PART_OBSTACLE world boxes (cover, navigation,
// sight) built by swat_mission_build_hollow; every row with a model is drawn
// by hollow_art.h at the same transform, and its collider box is then hidden.
// Rows without `solid` are presentation-only dressing (no simulation effect).
// y is the floor the prop stands on; the box spans y .. y+2*hy.
#ifndef SWAT_HOLLOW_DATA_H
#define SWAT_HOLLOW_DATA_H
typedef struct SwatHollowProp {
    const char* model;          // assets/environment/hollow_v1/<model>.glb, or NULL
    float x,y,z,yaw;
    float hx,hy,hz;             // collider half extents (solid rows)
    SwatMaterial material;
    bool solid;
} SwatHollowProp;
static const SwatHollowProp swat_hollow_props[]={
    // Barn
    {"stall_partition",33.6f,0,-3.0f,0,1.5f,.65f,.03f,SWAT_WOOD,true},
    {"stall_partition",33.6f,0,-.6f,0,1.5f,.65f,.03f,SWAT_WOOD,true},
    {"stall_partition",33.6f,0,1.8f,0,1.5f,.65f,.03f,SWAT_WOOD,true},
    {"workbench",40.55f,0,-6.6f,0,.35f,.45f,1.1f,SWAT_WOOD,true},
    {"chest_freezer",32.6f,0,2.6f,0,.4f,.45f,.75f,SWAT_STEEL,true},
    {"hay_bale",38.5f,2.6f,2.9f,0,.42f,.22f,.8f,SWAT_CLOTH,true},
    {"hay_bale",37.6f,2.6f,2.9f,0,.42f,.22f,.8f,SWAT_CLOTH,true},
    {"hay_bale",36.7f,2.6f,2.9f,0,.42f,.22f,.8f,SWAT_CLOTH,true},
    {"hay_bale",35.8f,2.6f,2.9f,0,.42f,.22f,.8f,SWAT_CLOTH,true},
    // House
    {"couch",15.0f,.3f,-2.0f,0,.45f,.42f,.95f,SWAT_CLOTH,true},
    {"recliner",17.2f,.3f,-2.6f,0,.45f,.5f,.45f,SWAT_CLOTH,true},
    {"kitchen_table",16.0f,.3f,2.0f,0,.5f,.38f,.8f,SWAT_WOOD,true},
    {"wood_stove",18.4f,.3f,3.0f,0,.3f,.45f,.35f,SWAT_STEEL,true},
    {"counter",14.4f,.3f,1.4f,0,.3f,.45f,1.0f,SWAT_WOOD,true},
    {"mattress",15.3f,2.9f,-4.6f,0,.95f,.12f,.7f,SWAT_CLOTH,true},
    {"flyer_table",21.4f,2.9f,-4.0f,0,.3f,.38f,.7f,SWAT_WOOD,true},
    {"wardrobe",19.4f,.3f,-4.8f,0,.4f,.9f,.25f,SWAT_WOOD,true},
    // Cellar and smokehouse
    {"cot",19.2f,-1.4f,17.4f,0,.95f,.2f,.4f,SWAT_WOOD,true},
    {"jar_shelf",16.8f,-1.4f,16.5f,0,.2f,.75f,1.6f,SWAT_WOOD,true},
    {"salt_box",25.6f,0,5.6f,0,.6f,.4f,.25f,SWAT_WOOD,true},
    // Yard cover
    {"school_bus",.25f,0,1,0,1.25f,1.5f,5.0f,SWAT_STEEL,true},
    {"junk_car",8,0,-8,.5f,.9f,.7f,2.25f,SWAT_STEEL,true},
    {"junk_car",11,0,-11.5f,-.3f,.9f,.7f,2.25f,SWAT_STEEL,true},
    {"camper",44,0,10,.15f,1.15f,1.3f,3.0f,SWAT_ALUMINUM,true},
    {"burn_barrel",11.2f,0,-2,0,.3f,.45f,.3f,SWAT_STEEL,true},
    {"woodpile",23.55f,0,.4f,0,.45f,.6f,1.2f,SWAT_WOOD,true},
    // A spoil berm grown over with laurel screens the lane end from the house.
    {"laurel_berm",2.6f,0,-15.2f,.45f,3.6f,.5f,1.6f,SWAT_SOIL,true},
    {NULL,2.6f,1.0f,-15.2f,.45f,3.4f,.95f,1.3f,SWAT_CLOTH,true},
    {"yard_light_pole",28.2f,0,-3.6f,0,.13f,3.6f,.13f,SWAT_WOOD,true}, // sodium head over (27.4,6.5,-3.6)
    {"pen_post",27,0,-12,0,.05f,.9f,.05f,SWAT_STEEL,true},
    {"pen_post",30,0,-12,0,.05f,.9f,.05f,SWAT_STEEL,true},
    {"pen_post",27,0,-9,0,.05f,.9f,.05f,SWAT_STEEL,true},
    {"pen_post",30,0,-9,0,.05f,.9f,.05f,SWAT_STEEL,true},
    // Presentation-only dressing (horror beats are implied, see HOLLOW.md 6).
    {"dog_pen_wire",28.5f,0,-10.5f,0,0,0,0,SWAT_STEEL,false},
    {"mailbox_flyer",-1.2f,0,-20.6f,0,0,0,0,SWAT_WOOD,false},
    {"lime_and_shovel",27.6f,0,9.0f,.3f,0,0,0,SWAT_SOIL,false},
    {"turned_ground",27.4f,0,9.4f,.3f,0,0,0,SWAT_SOIL,false},
    {"hooks_chains",25.5f,0,4.5f,0,0,0,0,SWAT_STEEL,false},
    {"tarp_shape",38.6f,0,-1.8f,1.4f,0,0,0,SWAT_CLOTH,false},
    {"shoe_pile",33.4f,0,3.2f,0,0,0,0,SWAT_CLOTH,false},
    {"flyer_wall",21.95f,2.9f,-4.0f,0,0,0,0,SWAT_WOOD,false},
    {"clothesline",9.6f,0,-1.0f,0,0,0,0,SWAT_WOOD,false},
    {"wood_chair",20.9f,2.9f,-4.2f,1.57f,0,0,0,SWAT_WOOD,false},
    {"bucket",18.0f,-1.4f,18.0f,0,0,0,0,SWAT_STEEL,false},
    {"porch_woodpile",18.0f,.3f,-6.42f,0,.75f,.45f,.24f,SWAT_WOOD,true},  // split wood stacked between the front doors
    {"wood_chair",21.55f,.3f,-7.25f,-.6f,0,0,0,SWAT_WOOD,false},
    {"washtub",14.55f,.3f,-6.55f,0,0,0,0,SWAT_STEEL,false},
    {"lino_rug",20.0f,.3f,-3.0f,0,0,0,0,SWAT_CLOTH,false},   // linoleum rugs over the boards
    {"lino_rug",16.1f,.3f,-3.6f,1.5708f,0,0,0,SWAT_CLOTH,false},
    {"ladder",12.6f,0,-2.1f,1.5708f,0,0,0,SWAT_WOOD,false}, // left leaning on the west gable
    {"tree_stand",-4,0,-12,0,0,0,0,SWAT_WOOD,false},        // over the "Lane bend" overwatch plinth
};
#define SWAT_HOLLOW_PROPS (int)(sizeof(swat_hollow_props)/sizeof(swat_hollow_props[0]))
#endif
