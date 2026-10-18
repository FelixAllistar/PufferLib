// Explicit starts/cover, then ordinary queued Move, aiming, bullets and bodies.
enum { SWAT_FIRE_LANE_CASES=17 };
static b3Pos fire_lane_point(float yaw,float along,float y,float across) {
    b3CosSin q=b3ComputeCosSin(yaw);return (b3Pos){q.cosine*along-q.sine*across,y,q.sine*along+q.cosine*across};
}
static SwatInput fire_lane_look(int actor,b3Pos target,bool aim) {
    SwatController* c=&sim.actors[actor].controller;b3Vec3 d=b3SubPos(target,swat_controller_eye(c));
    SwatInput in=swat_neutral_input();in.aim=aim;
    in.yaw_delta=swat_clamp(swat_angle(b3Atan2(d.z,d.x)-c->yaw-c->recoil_yaw),-6*SWAT_RAD,6*SWAT_RAD);
    in.pitch_delta=swat_clamp(b3Atan2(d.y,hypotf(d.x,d.z))-c->pitch-c->recoil_pitch,-4*SWAT_RAD,4*SWAT_RAD);return in;
}
static void fire_lane_step(SwatInput leader,int variant) {
    if(variant!=15){swat_sim_step(&sim,&leader);return;}
    SwatInput inputs[SWAT_MAX_ACTORS];swat_sim_bot_inputs(&sim,inputs);inputs[0]=leader;
    inputs[4]=fire_lane_look(4,(b3Pos){-1.5f,1.6f,8},true);inputs[4].fire=leader.fire;swat_sim_step_inputs(&sim,inputs);
}
static bool fire_lane_body_on_ray(int shooter,int mover) {
    const SwatActor* a=&sim.actors[shooter];
    if(a->controller.ads<.5f || fabsf(a->controller.ready_blend)>.12f || swat_weapons_busy(&a->arsenal) || a->arsenal.slots[a->arsenal.active].mode==SWAT_SAFE || !swat_weapon_rounds(&a->arsenal.slots[a->arsenal.active]))return false;
    SwatPose pose=swat_pose(&a->controller,&a->arsenal);
    SwatHit hit=swat_world_ray(&sim.world,pose.muzzle,swat_controller_aim(&a->controller),24,a->controller.body.body);
    return hit.kind==SWAT_HIT_ACTOR && hit.index==mover;
}
static void task_fire_lane(int variant) {
    assert(variant>=0 && variant<SWAT_FIRE_LANE_CASES);task_fixture();sim.tick=240;sim.config.max_ticks=10000;
    sim.actors[1].gear.surrendered=sim.actors[2].gear.surrendered=true;place_actor(1,(b3Pos){-8,0,9});place_actor(2,(b3Pos){-9,0,8});
    float yaw=variant==2?SWAT_PI*.5f:variant==3?.37f:0,mirror=variant==1?-1:1,level=variant==9?1:0;
    b3Pos target=fire_lane_point(yaw,8,1.6f+level,0),goal=fire_lane_point(yaw,variant==15?-2.8f:2.8f,0,2*mirror);
    if(level)swat_world_box(&sim.world,(b3Pos){0,.5f,0},swat_v(.7f,.5f,.7f),SWAT_CONCRETE,0);
    place_actor(0,(b3Pos){0,level,0});place_actor(3,fire_lane_point(yaw,2.8f,0,-2*mirror));place_actor(4,(b3Pos){-7,0,-5});place_actor(5,(b3Pos){-7,0,5});
    if(variant==15){place_actor(4,(b3Pos){-1.5f,0,-3});assert(swat_sim_set_player(&sim,2,true));}
    int backstop=swat_world_box(&sim.world,fire_lane_point(yaw,8,1.5f+level,0),swat_v(.15f,1.5f,4),SWAT_CONCRETE,0);swat_world_place(&sim.world.objects[backstop],-yaw);
    if(variant==6 || variant==15)swat_world_box(&sim.world,(b3Pos){variant==15?-1.5f:0,1.5f,8},swat_v(4,1.5f,.15f),SWAT_CONCRETE,0);
    if(variant==11)sim.actors[0].arsenal.slots[0].mode=SWAT_SAFE;
    if(variant==13){SwatWeapon* w=&sim.actors[0].arsenal.slots[0];w->chambered=false;w->magazine=w->reserve=0;}
    search_aim(0,goal);SwatInput in=swat_neutral_input();in.squad_order=SWAT_ORDER_MOVE;in.squad_team=1;in.squad_queue=true;fire_lane_step(in,variant);assert(sim.actors[3].mind.pending_order==SWAT_ORDER_MOVE);
    // Cover is established after the visible floor command, before Execute.
    int cover=-1;
    if(variant==7 || variant==8 || variant==16)cover=swat_world_box(&sim.world,(b3Pos){1.2f,1.5f,0},swat_v(.15f,1.5f,variant==8?.55f:3),variant==16?SWAT_GLASS:SWAT_CONCRETE,variant==16?8:0);
    if(variant==12){swat_body_set_crouch(&sim.actors[3].controller.body,true);cover=swat_world_box(&sim.world,(b3Pos){2.8f,1.4f,0},swat_v(1.4f,.25f,3),SWAT_CONCRETE,0);}
    for(int t=0;t<60;t++){bool aiming=variant!=4 && variant!=10 && variant!=14;in=fire_lane_look(0,target,aiming);if(variant==10)in.ready=SWAT_LOW_READY;fire_lane_step(in,variant);}
    if(variant==7 || variant==8){b3Pos from=swat_controller_eye(&sim.actors[3].controller),to=swat_controller_eye(&sim.actors[0].controller);SwatHit seen=swat_world_sight_ray(&sim.world,from,swat_normalize(b3SubPos(to,from)),b3Distance(from,to)+.15f,sim.actors[3].controller.body.body);assert(variant==7?(seen.kind==SWAT_HIT_WORLD && seen.index==cover):(seen.kind==SWAT_HIT_ACTOR && seen.index==0));}
    bool crossed=false,finished=false,crouched=false;int t;float min_along=100,min_z=100;
    for(t=0;t<1800 && sim.end==SWAT_RUNNING;t++){
        bool aiming=variant!=10 && variant!=14 && (variant!=4 || t>=85) && (variant!=5 || t<180);
        b3Pos aim_target=variant==6 && t>=120?(b3Pos){0,1.6f,8}:target;
        in=fire_lane_look(0,aim_target,aiming);in.fire=t<180 && (variant!=4 || t>=96) && variant!=10 && variant!=14 && t%12==0;in.squad_execute=t==0;
        if(variant==5 && t>=180)in.ready=SWAT_LOW_READY;
        if(variant==10)in.ready=SWAT_LOW_READY;
        fire_lane_step(in,variant);task_state(300+variant,t);
        b3Pos feet=swat_body_feet_position(&sim.actors[3].controller.body);b3CosSin frame=b3ComputeCosSin(yaw);
        min_along=fminf(min_along,feet.x*frame.cosine+feet.z*frame.sine);min_z=fminf(min_z,feet.z);crouched|=sim.actors[3].controller.body.crouched;
        crossed|=fire_lane_body_on_ray(0,3) || (variant==15 && fire_lane_body_on_ray(4,3));
        if(sim.actors[3].health<100)break;
        if(b3Distance(feet,sim.actors[3].mind.goal)<.65f){finished=true;break;}
    }
    b3Pos p=swat_body_feet_position(&sim.actors[3].controller.body);
    printf("FIRE_LANE %d tick%d shots%d health%.3f crossed%d finished%d minAlong%.6f minZ%.6f feet %.9f %.9f %.9f\n",variant,t,sim.actors[0].arsenal.shots,sim.actors[3].health,crossed,finished,min_along,min_z,p.x,p.y,p.z);fflush(stdout);
    assert(!crossed && sim.actors[3].health==100 && finished && !sim.debrief.roe_violations);
    if(variant<=3)assert(min_along<-.5f);
    if(variant>=7 && variant<=14)assert(min_along>1 && t<500);
    if(variant==10 || variant==11 || variant==13 || variant==14)assert(!sim.actors[0].arsenal.shots);
    if(variant==12)assert(crouched && sim.world.objects[cover].active);
    if(variant==16)assert(!sim.world.objects[cover].active && min_along<-.5f);
    if(variant==15)assert(min_z<-3.5f && sim.actors[4].arsenal.shots);
    for(int i=0;i<sim.actor_count;i++)assert(sim.actors[i].health==100);
    swat_sim_close(&sim);printf("PASS friendly firing lane%d: observed weapon posture, physical routing/arrival, original bullet damage and bounded cover/stance awareness\n",variant);
}
static void task_fire_surface(void) {
    task_fixture();place_actor(0,(b3Pos){0,0,0});place_actor(3,(b3Pos){3,0,0});
    sim.actors[1].gear.surrendered=sim.actors[2].gear.surrendered=true;place_actor(1,(b3Pos){8,0,8});place_actor(2,(b3Pos){9,0,9});
    int pane=swat_world_box(&sim.world,(b3Pos){1.5f,1.5f,0},swat_v(.02f,1.5f,2),SWAT_GLASS,8);
    int wall=swat_world_box(&sim.world,(b3Pos){8,1.5f,0},swat_v(.1f,1.5f,3),SWAT_CONCRETE,0);
    b3Pos eye=swat_controller_eye(&sim.actors[0].controller);b3Vec3 aim=swat_v(1,0,0);
    SwatHit physical=swat_world_ray(&sim.world,eye,aim,24,sim.actors[0].controller.body.body),optical=swat_world_sight_ray(&sim.world,eye,aim,24,sim.actors[0].controller.body.body),surface=swat_world_sight_surface(&sim.world,eye,aim,24);
    assert(physical.kind==SWAT_HIT_WORLD && physical.index==pane && optical.kind==SWAT_HIT_ACTOR && optical.index==3 && surface.kind==SWAT_HIT_WORLD && surface.index==wall);
    int near=swat_world_box(&sim.world,(b3Pos){4,1.5f,0},swat_v(.1f,1.5f,3),SWAT_CONCRETE,0);
    surface=swat_world_sight_surface(&sim.world,eye,aim,24);assert(surface.kind==SWAT_HIT_WORLD && surface.index==near && surface.distance<4);
    swat_sim_close(&sim);puts("PASS firing surface query: physical glass, optical actor and opaque-world endpoints stay distinct; actors never extend a lane through solid cover");
}
// The covering officer uses actual encounter AI. Hostile gunfire is scripted
// through ordinary inputs toward an overhead backstop to isolate friendly
// route clearance from winning an unprotected stationary duel.
static void fire_lane_bot_step(SwatInput command,bool firing) {
    SwatInput inputs[SWAT_MAX_ACTORS];swat_sim_bot_inputs(&sim,inputs);inputs[0]=command;
    inputs[1]=fire_lane_look(1,(b3Pos){8,6,0},true);inputs[1].fire=firing && sim.tick%12==0;swat_sim_step_inputs(&sim,inputs);
}
static void task_fire_lane_bot(void) {
    task_fixture();sim.tick=240;sim.config.hostile_fire=true;sim.config.max_ticks=10000;
    place_actor(0,(b3Pos){-5,0,5});place_actor(1,(b3Pos){8,0,0});place_actor(2,(b3Pos){-9,0,8});sim.actors[2].gear.surrendered=true;
    place_actor(3,(b3Pos){2.8f,0,-2});place_actor(4,(b3Pos){0,0,0});place_actor(5,(b3Pos){-7,0,-5});
    sim.actors[4].controller.yaw=0;sim.actors[1].controller.yaw=SWAT_PI;
    swat_world_box(&sim.world,(b3Pos){8,6,0},swat_v(2,.15f,2),SWAT_CONCRETE,0);
    b3Pos goal={2.8f,0,2};search_aim(0,goal);SwatInput in=swat_neutral_input();in.squad_order=SWAT_ORDER_MOVE;in.squad_team=1;in.squad_queue=true;fire_lane_bot_step(in,false);assert(sim.actors[3].mind.pending_order==SWAT_ORDER_MOVE);
    for(int t=0;t<60;t++)fire_lane_bot_step(swat_neutral_input(),false);
    bool crossed=false,finished=false;int t;for(t=0;t<1800 && sim.end==SWAT_RUNNING;t++) {
        in=swat_neutral_input();in.squad_execute=t==0;fire_lane_bot_step(in,true);task_state(317,t);
        crossed|=fire_lane_body_on_ray(4,3);assert(sim.actors[3].health==100 && sim.actors[4].alive && !sim.debrief.roe_violations);
        if(b3Distance(swat_body_feet_position(&sim.actors[3].controller.body),sim.actors[3].mind.goal)<.65f){finished=true;break;}
    }
    printf("BOT_FIRE_LANE tick%d officerShots%d hostileShots%d moverHealth%.3f sourceHealth%.3f crossed%d finished%d\n",t,sim.actors[4].arsenal.shots,sim.actors[1].arsenal.shots,sim.actors[3].health,sim.actors[4].health,crossed,finished);
    assert(finished && !crossed && sim.actors[4].arsenal.shots>0 && sim.actors[1].arsenal.shots>0);
    swat_sim_close(&sim);puts("PASS bot firing lane: scripted ordinary hostile shots into backstop, actual covering-officer return fire, ordinary queued Move, protected arrival and fresh replicas");
}
