// Shots for tools/slice_capture.c (included inside main). Actors in the motel
// (layout seed 42): officers 0 (player), 3, 4, 5; suspects 1 (room 102) and 8
// (room 104); civilians 2 (101), 6 (103), 7 (office). Front doors on z=0 at
// x=-7.2 (101), -3.2 (102), 0.8 (103), 4.8 (104).
(void)find;(void)V;(void)run;
#define SHOT(n) if(only<0 || only==(n))
#define SAVE(name) do { snprintf(path,sizeof(path),"%s/%s.png",out,name); frame(cam,path); } while(0)
// 0. Cast line-up in front of the motel: every body variant under game light.
SHOT(0) {
    reset();
    int cast[]={2,6,7,1,8};
    for(int k=0;k<5;k++) { place(cast[k],-3.2f+k*1.6f,12.5f,SWAT_PI*.5f); HOLD(cast[k]); }
    place(0,-6,9,-SWAT_PI*.5f);
    Camera3D cam=look(0,1.55f,17.2f,0,1.0f,12.5f,42);
    advance(120,NULL); for(int t=0;t<4;t++) frame(cam,NULL); SAVE("shot0_cast");
}
// 1. Squad stacked on room 102's door, low three-quarter from the lot.
SHOT(1) {
    reset();
    int stack[]={0,3,4,5};
    for(int k=0;k<4;k++) { place(stack[k],-2.35f+k*.62f,.62f,SWAT_PI); HOLD(stack[k]); sim->actors[stack[k]].controller.pitch=-.12f; }
    Camera3D cam=look(-5.4f,.95f,3.4f,-1.7f,1.15f,.4f,46);
    advance(90,NULL); for(int t=0;t<4;t++) frame(cam,NULL); SAVE("shot1_stack");
}
// 2. Contact: room 102 door open, the hooded gunman covers it from inside;
// the point man is in the doorway. Over the gunman's shoulder.
SHOT(2) {
    reset(); open_door(40,SWAT_PI*.5f);
    place(1,-2.0f,-2.6f,0); face(1,(b3Pos){-3.2f,1.2f,0}); HOLD(1);
    place(0,-3.2f,.25f,-SWAT_PI*.5f); face(0,feet(1)); HOLD(0);
    Camera3D cam=look(-1.05f,1.62f,-3.75f,-3.3f,1.25f,.2f,50);
    advance(60,NULL); for(int t=0;t<4;t++) frame(cam,NULL); SAVE("shot2_contact");
}
// 3. Compliance: the civilian in room 103 kneels, hands up; an officer covers.
SHOT(3) {
    reset(); open_door(64,SWAT_PI*.5f);
    place(6,.5f,-3.1f,SWAT_PI*.5f); sim->actors[6].gear.surrendered=true; HOLD(6);
    place(3,1.1f,-1.0f,-SWAT_PI*.5f); face(3,feet(6)); face(6,feet(3)); HOLD(3); sim->actors[3].controller.pitch=-.28f;
    Camera3D cam=look(2.35f,1.3f,-.55f,.4f,.75f,-3.2f,54);
    advance(360,NULL); for(int t=0;t<4;t++) frame(cam,NULL); SAVE("shot3_surrender");
}
// 4. Arrest: the vest gunman in room 104 restrained on his knees.
SHOT(4) {
    reset(); open_door(88,SWAT_PI*.5f);
    place(8,5.65f,-3.1f,SWAT_PI*.5f); sim->actors[8].gear.surrendered=sim->actors[8].gear.restrained=true; HOLD(8);
    place(4,5.35f,-1.35f,-SWAT_PI*.5f); face(4,feet(8)); face(8,feet(4)); HOLD(4); sim->actors[4].controller.pitch=-.42f;
    Camera3D cam=look(4.35f,1.4f,-.45f,5.6f,.75f,-3.0f,58);
    advance(420,NULL); for(int t=0;t<4;t++) frame(cam,NULL); SAVE("shot4_arrest");
}
// 5. Escort: a restrained civilian follows an officer across the lot.
SHOT(5) {
    reset();
    place(2,-4.2f,4.6f,SWAT_PI*.5f); sim->actors[2].gear.surrendered=sim->actors[2].gear.restrained=true;
    place(5,-3.4f,5.6f,SWAT_PI*.5f);
    advance(60,NULL);
    sim->actors[2].mind.escort_owner=5; sim->actors[2].gear.rise_ticks=0; sim->actors[2].controller.body.crouched=false;
    sim->actors[5].mind.bot=true; sim->actors[5].mind.order=SWAT_ORDER_MOVE; sim->actors[5].mind.goal=(b3Pos){1.5f,0,10.5f};
    advance(150,NULL);
    b3Pos f=feet(2); Camera3D cam=look((float)f.x+4.4f,1.45f,(float)f.z+1.2f,(float)f.x+.3f,1.0f,(float)f.z+.4f,44);
    for(int t=0;t<6;t++) { advance(1,NULL); frame(cam,NULL); }
    SAVE("shot5_escort");
}
// 6. Firefight: the point man fires on the gunman through room 102's door.
SHOT(6) {
    reset(); open_door(40,SWAT_PI*.5f);
    place(1,-2.4f,-2.4f,0); face(1,(b3Pos){-3.2f,1.2f,.6f}); HOLD(1);
    place(0,-3.2f,.9f,-SWAT_PI*.5f); face(0,feet(1)); HOLD(0);
    Camera3D cam=look(-.45f,1.6f,-3.65f,-3.0f,1.2f,.3f,58);
    advance(40,NULL);
    SwatInput fire=swat_neutral_input(); fire.aim=true; advance(20,&fire);
    fire.fire=true; advance(1,&fire); frame(cam,NULL); SAVE("shot6_fire");
}
#undef SHOT
#undef SAVE
