// Film for tools/slice_capture.c --film (included inside main): the real
// simulation at 60 Hz, two ticks per 30 fps frame, staged with ordinary sim
// state and inputs; each scene's camera moves between two keyframes.
static int film_index=0;
#define FILM_FRAME(cam) do { snprintf(path,sizeof(path),"%s/film/frame_%05d.png",out,film_index++); frame(cam,path); } while(0)
{
    char dir[1024]; snprintf(dir,sizeof(dir),"%s/film",out); MakeDirectory(dir);
    // Camera between keyframes a -> b with ease-in-out over n frames.
    #define DOLLY(a,b,n,body) for(int f_=0;f_<(n);f_++) { float u_=(float)f_/((n)-1>0?(n)-1:1); u_=u_*u_*(3-2*u_); \
        Camera3D cam={Vector3Lerp((a).position,(b).position,u_),Vector3Lerp((a).target,(b).target,u_),{0,1,0},(a).fovy+((b).fovy-(a).fovy)*u_,CAMERA_PERSPECTIVE}; \
        body; FILM_FRAME(cam); }
    SwatInput idle=swat_neutral_input();
    if(only<0 || only==0) { // scene 0
    // A. Establishing: the squad crosses the lot toward room 102.
    reset(); open_door(40,0);
    {
        int squad[]={0,3,4,5};
        for(int k=0;k<4;k++) { place(squad[k],-1.0f+k*.75f,7.0f+k*.9f,-SWAT_PI*.55f); HOLD(squad[k]); }
        sim->actors[3].mind.bot=sim->actors[4].mind.bot=sim->actors[5].mind.bot=true;
        sim->actors[3].mind.order=SWAT_ORDER_FALL_IN; sim->actors[4].mind.order=SWAT_ORDER_FALL_IN; sim->actors[5].mind.order=SWAT_ORDER_FALL_IN;
        advance(30,&idle);
        Camera3D a=look(-10.5f,1.7f,12.5f,-3.0f,1.3f,4.0f,50),b=look(-9.0f,1.45f,9.2f,-2.6f,1.2f,1.5f,42);
        SwatInput walk=swat_neutral_input(); walk.forward=.55f;
        DOLLY(a,b,120,{ b3Pos f0=feet(0); sim->actors[0].controller.yaw=atan2f((float)(.9f-f0.z),(float)(-2.5f-f0.x)); advance(2,&walk); })
    }
    }
    if(only<0 || only==1) { // scene 1
    // B. Stack on 102; the door swings and the point man goes in.
    reset();
    {
        int stack[]={0,3,4,5};
        for(int k=0;k<4;k++) { place(stack[k],-2.35f+k*.62f,.62f,SWAT_PI); HOLD(stack[k]); sim->actors[stack[k]].controller.pitch=-.12f; }
        advance(40,&idle);
        Camera3D a=look(-5.6f,.9f,3.6f,-1.8f,1.15f,.4f,48),b=look(-5.0f,1.0f,2.6f,-2.6f,1.15f,.1f,44);
        DOLLY(a,b,75,{ float t=(float)f_/74; if(t>.35f) open_door(40,SWAT_PI*.5f*fminf(1,(t-.35f)/.35f)); advance(2,&idle); })
    }
    }
    if(only<0 || only==2) { // scene 2
    // C. Contact: the gunman covers the door; ordered down by the point man,
    // he lowers the rifle and goes to his knees.
    reset(); open_door(40,SWAT_PI*.5f);
    {
        place(1,-2.0f,-2.6f,0); face(1,(b3Pos){-3.2f,1.2f,0}); HOLD(1);
        place(0,-3.2f,.25f,-SWAT_PI*.5f); face(0,feet(1)); HOLD(0);
        advance(40,&idle);
        Camera3D a=look(-1.05f,1.62f,-3.75f,-3.3f,1.25f,.2f,50),b=look(-.9f,1.5f,-3.3f,-2.9f,1.05f,-1.2f,52);
        SwatInput aim=swat_neutral_input(); aim.aim=true;
        DOLLY(a,b,135,{
            SwatInput in=aim; in.command=f_==45;
            if(f_==50) sim->actors[1].gear.surrendered=true;
            advance(2,&in); })
    }
    }
    if(only<0 || only==3) { // scene 3
    // D. Compliance: a civilian in 103 kneels, hands up, under cover.
    reset(); open_door(64,SWAT_PI*.5f);
    {
        place(6,.5f,-3.1f,SWAT_PI*.5f); HOLD(6);
        place(3,1.1f,-1.0f,-SWAT_PI*.5f); face(3,feet(6)); face(6,feet(3)); HOLD(3); sim->actors[3].controller.pitch=-.28f;
        advance(20,&idle); sim->actors[6].gear.surrendered=true;
        Camera3D a=look(2.6f,1.45f,-.4f,.6f,.85f,-3.0f,56),b=look(2.1f,1.2f,-.9f,.45f,.75f,-3.1f,48);
        DOLLY(a,b,105,{ advance(2,&idle); })
    }
    }
    if(only<0 || only==4) { // scene 4
    // E. Arrest: the second gunman restrained on his knees in 104.
    reset(); open_door(88,SWAT_PI*.5f);
    {
        place(8,5.65f,-3.1f,SWAT_PI*.5f); sim->actors[8].gear.surrendered=sim->actors[8].gear.restrained=true; HOLD(8);
        place(4,5.35f,-1.35f,-SWAT_PI*.5f); face(4,feet(8)); face(8,feet(4)); HOLD(4); sim->actors[4].controller.pitch=-.42f;
        advance(420,&idle);
        Camera3D a=look(4.35f,1.4f,-.45f,5.6f,.75f,-3.0f,58),b=look(4.7f,1.25f,-.8f,5.6f,.7f,-3.0f,50);
        DOLLY(a,b,90,{ advance(2,&idle); })
    }
    }
    if(only<0 || only==5) { // scene 5
    // F. Escort: a restrained civilian follows an officer to staging.
    reset();
    {
        // The rest of the squad holds the stack on 102, clear of the walk.
        int rest[]={0,3,4};
        for(int k=0;k<3;k++) { place(rest[k],-2.35f+k*.62f,.62f,SWAT_PI); HOLD(rest[k]); }
        place(2,4.4f,1.4f,SWAT_PI*.5f); sim->actors[2].gear.surrendered=sim->actors[2].gear.restrained=true;
        place(5,4.9f,2.4f,SWAT_PI*.5f);
        advance(60,&idle);
        sim->actors[2].mind.escort_owner=5; sim->actors[2].gear.rise_ticks=0; sim->actors[2].controller.body.crouched=false;
        sim->actors[5].mind.bot=true; sim->actors[5].mind.order=SWAT_ORDER_MOVE; sim->actors[5].mind.goal=(b3Pos){7.5f,0,11.0f};
        advance(40,&idle);
        // Track from the lot side (the building is at z <= 0), slowly pushing in.
        for(int f_=0;f_<120;f_++) {
            b3Pos c=feet(2),o=feet(5); float u=(float)f_/119;
            float mx=(float)(c.x+o.x)*.5f,mz=(float)(c.z+o.z)*.5f;
            Camera3D cam=look(mx-2.2f+u*.6f,1.55f,fmaxf(mz+5.0f-u*1.0f,2.6f),mx,1.0f,mz,42);
            advance(2,&idle); FILM_FRAME(cam);
        }
        printf("escort: civ restrained %d owner %d feet %.2f %.2f | officer order %d bot %d feet %.2f %.2f\n",sim->actors[2].gear.restrained,sim->actors[2].mind.escort_owner,
            feet(2).x,feet(2).z,sim->actors[5].mind.order,sim->actors[5].mind.bot,feet(5).x,feet(5).z);
    }
    }
    #undef DOLLY
    printf("film: %d frames\n",film_index);
}
#undef FILM_FRAME
