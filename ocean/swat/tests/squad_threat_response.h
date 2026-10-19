// Actual encounter AI on both sides. Explicit geometry/starts, then queued
// ordinary Move, real weapons, original damage and full physical bodies.
enum { SWAT_THREAT_DUELS=10 };
static void task_threat_response_duel(int variant) {
    assert(variant>=0 && variant<SWAT_THREAT_DUELS);task_fixture();sim.tick=variant==4?241:variant==5?248:variant==6?257:240;sim.config.hostile_fire=true;sim.config.max_ticks=10000;
    float yaw=variant==2?SWAT_PI*.5f:variant==3?.37f:0,mirror=variant==1?-1:1;
    place_actor(0,fire_lane_point(yaw,-5,0,5));place_actor(1,fire_lane_point(yaw,8,0,0));place_actor(2,fire_lane_point(yaw,-9,0,8));sim.actors[2].gear.surrendered=true;
    place_actor(3,fire_lane_point(yaw,2.8f,0,-2*mirror));place_actor(4,(b3Pos){0,0,0});place_actor(5,fire_lane_point(yaw,-7,0,-5));
    sim.actors[4].controller.yaw=yaw;sim.actors[1].controller.yaw=swat_angle(yaw+SWAT_PI);
    if(variant==7)sim.actors[1].mind.resolve=1;
    if(variant==9)swat_body_set_crouch(&sim.actors[4].controller.body,true);
    int side=swat_world_box(&sim.world,fire_lane_point(yaw,5,1.5f,-1.1f*mirror),swat_v(.15f,1.5f,.7f),SWAT_CONCRETE,0);swat_world_place(&sim.world.objects[side],-yaw);
    if(variant==8){int pane=swat_world_box(&sim.world,fire_lane_point(yaw,4,1.5f,0),swat_v(.02f,1.5f,2),SWAT_GLASS,8);swat_world_place(&sim.world.objects[pane],-yaw);}
    b3Pos goal=fire_lane_point(yaw,2.8f,0,2*mirror);search_aim(0,goal);SwatInput in=swat_neutral_input();in.squad_order=SWAT_ORDER_MOVE;in.squad_team=1;in.squad_queue=true;swat_sim_step(&sim,&in);assert(sim.actors[3].mind.pending_order==SWAT_ORDER_MOVE);
    for(int t=0;t<10;t++)swat_sim_step(&sim,&(SwatInput){0});
    bool crossed=false,finished=false,observed=false;int t;for(t=0;t<1800 && sim.end==SWAT_RUNNING;t++) {
        observed|=swat_sim_observed_weapon_threat(&sim,4,1,24);in=swat_neutral_input();in.squad_execute=t==0;swat_sim_step(&sim,&in);task_state(400+variant,t);
        crossed|=fire_lane_body_on_ray(4,3);
        if(sim.actors[3].health<100 || !sim.actors[4].alive || sim.debrief.roe_violations)fprintf(stderr,"DUEL_FAIL variant%d t%d worker%.3f cover%.3f roe%d coverShots%d hostileShots%d\n",variant,t,sim.actors[3].health,sim.actors[4].health,sim.debrief.roe_violations,sim.actors[4].arsenal.shots,sim.actors[1].arsenal.shots);
        assert(sim.actors[3].health==100 && sim.actors[4].alive && !sim.debrief.roe_violations);
        if(b3Distance(swat_body_feet_position(&sim.actors[3].controller.body),sim.actors[3].mind.goal)<.65f){finished=true;break;}
    }
    printf("THREAT_DUEL variant%d tick%d officerShots%d hostileShots%d moverHealth%.3f sourceHealth%.3f crossed%d finished%d\n",variant,t,sim.actors[4].arsenal.shots,sim.actors[1].arsenal.shots,sim.actors[3].health,sim.actors[4].health,crossed,finished);
    assert(finished && !crossed && observed && sim.actors[4].arsenal.shots>0 && (!sim.actors[1].alive || sim.actors[1].gear.surrendered));
    if(!variant)assert(sim.actors[1].arsenal.shots>0);
    swat_sim_close(&sim);printf("PASS observed threat duel%d: actual hostile and covering-officer AI, ordinary queued Move, original damage, protected arrival and fresh replicas\n",variant);
}
