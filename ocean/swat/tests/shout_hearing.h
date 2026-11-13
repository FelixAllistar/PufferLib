// A shout is heard, not aimed (sim.c swat_sim_command_heard): every person
// whose acoustic path from the command sound reaches SWAT_COMMAND_HEARD_GAIN
// receives the order. Real walls, doors and doorways stand between officer
// and people; the people are scripted occupants, never placed in a state.
enum { SWAT_SHOUT_HEARING_CASES=5 };
static void shout_wall(b3Pos center,b3Vec3 half) {swat_world_box(&sim.world,center,half,SWAT_CONCRETE,0);}
// A wall across x with a 1.08 m doorway centred on z=0 and its timber leaf,
// hinged at z=-0.54: shut, or opening (it swings fully open within 60 ticks).
static int shout_door_wall(float x,bool open) {
    shout_wall((b3Pos){x,1.5f,-3.27f},swat_v(.1f,1.5f,2.73f));
    shout_wall((b3Pos){x,1.5f,3.27f},swat_v(.1f,1.5f,2.73f));
    shout_wall((b3Pos){x,2.6f,0},swat_v(.1f,.4f,.54f));
    int owner=swat_world_box(&sim.world,(b3Pos){x,1.095f,0},swat_v(.025f,1.095f,.54f),SWAT_WOOD,120);
    SwatObject* door=&sim.world.objects[owner];door->door=true;door->closed_yaw=-SWAT_PI*.5f;door->hinge=(b3Pos){x,1.095f,-.54f};
    door->door_open=open;
    return owner;
}
static void shout_person(int slot,SwatRole role,b3Pos feet,float yaw) {
    swat_sim_spawn_actor(&sim,slot,role,feet,yaw);if(sim.actor_count<=slot)sim.actor_count=slot+1;
}
// Officer 0 at the origin aiming along +x at eye level; room walls at z=+-6,
// x=-3 and x=9 (one room, or two with a dividing wall). The fixture's
// civilian kneels out of the way behind the officer unless a case reuses it.
static void shout_fixture(void) {
    fixture();
    shout_person(2,SWAT_CIVILIAN,(b3Pos){-2,0,-5},SWAT_PI*.5f);sim.actors[2].gear.surrendered=true;
    shout_wall((b3Pos){3,1.5f,-6},swat_v(6.2f,1.5f,.1f));shout_wall((b3Pos){3,1.5f,6},swat_v(6.2f,1.5f,.1f));
    shout_wall((b3Pos){-3,1.5f,0},swat_v(.1f,1.5f,6));shout_wall((b3Pos){9,1.5f,0},swat_v(.1f,1.5f,6));
    sim.actors[0].controller.yaw=0;sim.actors[0].controller.pitch=0;
}
static void shout_idle(int ticks) {for(int t=0;t<ticks;t++)swat_sim_step(&sim,&(SwatInput){0});}
static void shout_command(void) {SwatInput in=swat_neutral_input();in.command=true;swat_sim_step(&sim,&in);}
static b3Pos shout_feet(int i) {return swat_body_feet_position(&sim.actors[i].controller.body);}
static int shout_wall_contacts(int i) {
    int contacts=0;
    for(int k=0;k<8;k++) {
        SwatHit hit=swat_world_ray(&sim.world,b3OffsetPos(shout_feet(i),swat_v(0,.8f,0)),swat_direction(k*SWAT_PI/4,0),.9f,sim.actors[i].controller.body.body);
        contacts+=hit.kind==SWAT_HIT_WORLD;
    }
    return contacts;
}
// Heard-only civilian: walks (never sprints) at most 4 m to a wall and ends
// kneeling with hands shown.
static void shout_takes_cover(int i,b3Pos start) {
    const SwatActor* c=&sim.actors[i];
    assert(c->mind.complying && !c->gear.surrendered);
    float walked=0;b3Pos last=shout_feet(i);
    for(int t=0;t<SWAT_COMPLY_TICKS+60 && !c->gear.surrendered;t++) {
        shout_idle(1);
        assert(!c->controller.sprinting);
        walked+=b3Distance(last,shout_feet(i));last=shout_feet(i);
    }
    shout_idle(40);
    b3Vec3 moved=b3SubPos(shout_feet(i),start);
    assert(c->gear.surrendered && c->controller.body.crouched && !c->mind.complying);
    assert(hypotf(moved.x,moved.z)<=SWAT_COMPLY_RADIUS+.5f && shout_wall_contacts(i)>0 && walked>.3f);
}
static void shout_hearing(int test) {
    shout_fixture();
    if(test==0) {
        // (a) Three calm civilians 4 m off at 0, 50 and 120 degrees from the
        // aim, all in one room: one shout commands all three (5.0: one).
        float angles[3]={0,50,120};
        for(int n=0;n<3;n++) {
            float a=angles[n]*SWAT_RAD;
            shout_person(n+1,SWAT_CIVILIAN,(b3Pos){4*cosf(a),0,4*sinf(a)},a+SWAT_PI);
        }
        shout_idle(5);
        for(int n=1;n<=3;n++)assert(!sim.actors[n].gear.surrendered && !sim.roe[n].challenged_tick);
        shout_command();
        for(int n=1;n<=3;n++)assert(sim.actors[n].mind.heard_commands==1 && sim.roe[n].challenged_tick);
        shout_idle(SWAT_COMPLY_TICKS+60);
        for(int n=1;n<=3;n++)assert(sim.actors[n].gear.surrendered);
        puts("PASS shout hearing (a): one shout commands three civilians at 0, 50 and 120 degrees from the aim; all kneel");
    } else if(test==1) {
        // (b) A calm civilian in the next room, behind an open door and out of
        // sight, facing away: hears, walks to a wall or corner and kneels.
        shout_door_wall(3,true);
        shout_person(1,SWAT_CIVILIAN,(b3Pos){6,0,2.5f},0);
        shout_idle(60);
        assert(!swat_sim_actor_visible(&sim,1,0,24,60*SWAT_RAD) && !swat_sim_actor_visible(&sim,0,1,SWAT_ROE_OFFICER_RANGE,SWAT_ROE_HALF_FOV));
        b3Pos start=shout_feet(1);
        shout_command();
        assert(sim.actors[1].mind.heard_commands==1);
        shout_takes_cover(1,start);
        puts("PASS shout hearing (b): a civilian behind an open door hears, walks at most 4 m to a wall or corner and kneels with hands shown");
    } else if(test==2) {
        // (c) Behind two closed doors the shout is not heard; behind one, at
        // 6 m, it is (the control).
        shout_door_wall(2,false);shout_door_wall(4,false);
        shout_person(1,SWAT_CIVILIAN,(b3Pos){6,0,0},SWAT_PI);
        shout_idle(5);
        shout_command();
        assert(!sim.actors[1].mind.heard_commands && !sim.roe[1].challenged_tick);
        shout_idle(SWAT_COMPLY_TICKS);
        assert(!sim.actors[1].gear.surrendered && !sim.actors[1].mind.complying);
        swat_sim_close(&sim);
        shout_fixture();shout_door_wall(3,false);
        shout_person(1,SWAT_CIVILIAN,(b3Pos){5.5f,0,0},SWAT_PI);
        shout_idle(5);
        shout_command();
        assert(sim.actors[1].mind.heard_commands==1 && sim.roe[1].challenged_tick);
        puts("PASS shout hearing (c): two closed doors keep the shout from a civilian; one closed door at 6 m does not");
    } else if(test==3) {
        // (d) An armed suspect of full resolve behind a thin wall hears, does
        // not comply and, seeing no officer, investigates toward the voice.
        swat_world_box(&sim.world,(b3Pos){3,1.5f,0},swat_v(.00625f,1.5f,5.9f),SWAT_DRYWALL,0);
        shout_person(1,SWAT_SUSPECT,(b3Pos){6,0,0},SWAT_PI);sim.actors[1].mind.resolve=1;
        shout_idle(5);
        assert(sim.actors[1].mind.memory_ticks==0 && !swat_sim_actor_visible(&sim,1,0,24,60*SWAT_RAD));
        shout_command();
        const SwatMind* mind=&sim.actors[1].mind;
        assert(mind->heard_commands==1 && sim.roe[1].challenged_tick && !sim.actors[1].gear.surrendered);
        assert(mind->yield==SWAT_YIELD_FIGHT && mind->memory_ticks>170);
        b3Vec3 toward=b3SubPos(mind->memory,shout_feet(1));
        assert(toward.x<-3.5f && fabsf(toward.z)<.5f);
        // A later order the suspect only overhears warns again but does not
        // restart the response time on the ROE record.
        int challenged=sim.roe[1].challenged_tick;
        shout_idle(20);shout_command();
        assert(mind->heard_commands==2 && sim.roe[1].challenged_tick==challenged);
        puts("PASS shout hearing (d): a suspect behind a wall who hears and fights on investigates a point 4 m toward the officer; an overheard order does not restart its challenge");
    } else {
        // (e) The ball speaks: a civilian whose head is hidden from the ball
        // by a thin panel still hears it and takes cover, and a suspect in
        // view answers through the surrender model, not a fixed resolve rule.
        assert(swat_device_deploy(&sim,0,SWAT_BALL));
        SwatDevice* d=&sim.devices[0];
        d->position=(b3Pos){1,.12f,0};b3Body_SetTransform(d->body,d->position,b3Quat_identity);b3Body_SetLinearVelocity(d->body,swat_v(0,0,0));
        swat_world_box(&sim.world,(b3Pos){2.5f,1.4f,3},swat_v(.00625f,1.1f,2.2f),SWAT_DRYWALL,0);
        shout_person(1,SWAT_SUSPECT,(b3Pos){3,0,-2},SWAT_PI);sim.actors[1].mind.resolve=.2f;
        shout_person(3,SWAT_CIVILIAN,(b3Pos){3.5f,0,3},0);
        shout_idle(5);
        b3Pos head=swat_controller_eye(&sim.actors[3].controller),start=shout_feet(3);
        assert(!swat_world_visible(&sim.world,swat_device_eye(d),head) && b3Distance(d->position,head)<9);
        sim.actors[0].controller.yaw=SWAT_PI;
        SwatInput in=swat_neutral_input();in.device_control=true;in.device_unit=0;in.command=true;
        swat_sim_step(&sim,&in);
        assert(sim.actors[3].mind.heard_commands==1 && sim.roe[3].challenged_tick);
        assert(sim.actors[1].mind.heard_commands==1 && sim.actors[1].mind.yield_count==1 && sim.actors[1].mind.yield!=SWAT_YIELD_NONE);
        shout_takes_cover(3,start);
        puts("PASS shout hearing (e): the ball's order reaches a civilian whose head it cannot see, and a suspect's answer comes from the surrender model");
    }
    swat_sim_close(&sim);
}
