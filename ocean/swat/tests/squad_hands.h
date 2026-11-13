// Squad bot fire, hold and challenge decisions are made on the hands, never
// on the head (encounter.c face_target). Real walls and beams stand between
// the covering bot's eye and the suspect: standing eyes are 1.636 m high, the
// suspect's hands 1.34-1.52 m and the muzzle 0.8 m ahead of the body, so a
// 1.60 m wall just short of the suspect hides the hands and weapon and leaves
// the head, and a beam from 1.61 m to 1.75 m just ahead of the suspect hides
// only the head. The suspect is driven by the test through ordinary inputs.
enum { SWAT_SQUAD_HANDS_CASES=7 };
static void hands_fixture(int wall) {
    fixture();sim.config.hostile_fire=true;
    // The leader stands aside and looks away, so only the bot judges.
    place_actor(0,(b3Pos){-2,0,-1.5f});sim.actors[0].controller.yaw=SWAT_PI;
    place_actor(2,(b3Pos){-4,0,6});
    swat_sim_spawn_actor(&sim,3,SWAT_OFFICER,(b3Pos){0,0,0},0);
    sim.actors[3].mind.bot=true;sim.actors[3].mind.team=1;sim.actors[3].mind.order=SWAT_ORDER_HOLD;
    sim.actor_count=4;
    // An outside policy keeps the role contract's fixed command rule: with
    // full resolve the armed suspect never yields to a command.
    sim.actors[1].mind.external=true;sim.actors[1].mind.resolve=1;
    if(wall==1)swat_world_box(&sim.world,(b3Pos){4.8f,.8f,0},swat_v(.05f,.8f,3),SWAT_CONCRETE,0);
    if(wall==2)swat_world_box(&sim.world,(b3Pos){5.5f,1.68f,0},swat_v(.05f,.07f,3),SWAT_CONCRETE,0);
}
static void hands_step(SwatInput leader,SwatInput suspect,int test,int tick) {
    SwatInput inputs[SWAT_MAX_ACTORS];swat_sim_bot_inputs(&sim,inputs);
    inputs[0]=leader;inputs[1]=suspect;swat_sim_step_inputs(&sim,inputs);task_state(900+test,tick);
}
static SwatHands hands_seen_by_bot(void) {return swat_sim_hands_seen(&sim,3,1,SWAT_ROE_OFFICER_RANGE,SWAT_ROE_HALF_FOV);}
static bool hands_head_seen(int officer) {
    b3Pos eye=swat_controller_eye(&sim.actors[officer].controller),head=swat_controller_eye(&sim.actors[1].controller);
    b3Vec3 d=b3SubPos(head,eye);
    SwatHit hit=swat_world_sight_ray(&sim.world,eye,swat_normalize(d),b3Length(d)+.15f,sim.actors[officer].controller.body.body);
    return hit.kind==SWAT_HIT_ACTOR && hit.index==1;
}
static SwatInput hands_aim_at_bot(float height) {
    return fire_lane_look(1,b3OffsetPos(swat_body_feet_position(&sim.actors[3].controller.body),swat_v(0,height,0)),true);
}
static void task_squad_hands(int test) {
    hands_fixture(test==0 || test==4 ? 1 : test==1 || test==5 || test==6 ? 2 : 0);
    SwatInput neutral=swat_neutral_input(),suspect=neutral;
    const SwatActor* bot=&sim.actors[3];
    int t=0,dropped=-1;
    if(test==0) {
        // (a) Armed, only the head above a chest-high wall, not firing: the
        // bot covers and challenges, never fires, and after the cover time
        // carries out a cuff order on a surrendered civilian.
        place_actor(2,(b3Pos){-3,0,-3});sim.actors[2].gear.surrendered=true;
        for(;t<150;t++) {
            hands_step(neutral,suspect,test,t);
            assert(bot->target_actor==1 && hands_seen_by_bot()==SWAT_HANDS_HIDDEN && hands_head_seen(3));
        }
        assert(bot->mind.challenge_actor==2 && sim.roe[1].challenged_tick && bot->mind.cover_actor==2);
        search_aim(0,swat_controller_eye(&sim.actors[2].controller));
        SwatInput order=neutral;order.squad_order=SWAT_ORDER_CUFF;order.squad_team=1;hands_step(order,suspect,test,t++);
        assert(bot->mind.pending_order==SWAT_ORDER_CUFF && bot->mind.pending_actor==3);
        for(;t<1500 && !sim.actors[2].gear.restrained;t++) {
            hands_step(neutral,suspect,test,t);
            assert(hands_seen_by_bot()!=SWAT_HANDS_WEAPON && !bot->arsenal.shots);
        }
        assert(sim.actors[2].gear.restrained && sim.actors[1].health==100 && sim.roe[1].state==SWAT_ROE_UNKNOWN);
    } else if(test==1) {
        // (b) Head behind a beam, rifle raised at the bot below it: the bot
        // fires on the weapon it sees, lawfully.
        for(;t<400 && !bot->arsenal.shots;t++) {
            assert(!hands_head_seen(3) && hands_seen_by_bot()==SWAT_HANDS_WEAPON);
            suspect=hands_aim_at_bot(1.1f);hands_step(neutral,suspect,test,t);
        }
        assert(bot->arsenal.shots && sim.roe[1].state==SWAT_ROE_HOSTILE_ARMED && !sim.roe[1].challenged_tick);
    } else if(test==2 || test==3) {
        // (c) A shooter who drops the weapon in view is never fired on after
        // the drop. (d) An unarmed civilian half behind cover in a firefight
        // is never targeted or hit.
        if(test==3) {
            place_actor(2,(b3Pos){3,0,3});swat_world_box(&sim.world,(b3Pos){2.5f,.6f,3},swat_v(.05f,.6f,1),SWAT_CONCRETE,0);
        }
        swat_world_box(&sim.world,(b3Pos){6,6,0},swat_v(2,.15f,2),SWAT_CONCRETE,0);
        int shots=0;
        for(;t<(test==2 ? 500 : 600);t++) {
            if(test==3)assert(bot->target_actor!=2 && sim.actors[2].health==100);
            if(dropped<0) {
                suspect=fire_lane_look(1,(b3Pos){6,6,0},true);suspect.fire=t>=30 && t%12==0;
            } else suspect=neutral;
            // The civilian stays half behind the low wall.
            SwatInput inputs[SWAT_MAX_ACTORS];swat_sim_bot_inputs(&sim,inputs);
            inputs[0]=neutral;inputs[1]=suspect;if(test==3)inputs[2]=neutral;
            swat_sim_step_inputs(&sim,inputs);task_state(900+test,t);
            if(test==2 && dropped<0 && sim.actors[1].arsenal.shots) {
                // The weapon leaves the hands as a dropping suspect's does
                // (surrender_impl.h); the encounter drops it as evidence.
                sim.actors[1].gear.disarmed=true;sim.actors[1].arsenal.fire_buffer_ticks=0;dropped=t;shots=bot->arsenal.shots;
            }
            if(test==2 && dropped>=0)assert(bot->arsenal.shots==shots);
        }
        if(test==2)assert(dropped>=0 && sim.actors[1].alive && hands_seen_by_bot()==SWAT_HANDS_EMPTY && sim.roe[1].state==SWAT_ROE_COMPLIANT);
        if(test==3)assert(bot->arsenal.shots && sim.actors[2].health==100 && !sim.debrief.roe_violations);
    } else if(test==4) {
        // (e) Seen firing from behind a chest-high wall with the hands hidden:
        // the muzzle flash proves the weapon and the bot returns fire.
        for(;t<400 && !bot->arsenal.shots;t++) {
            suspect=hands_aim_at_bot(1.5f);suspect.fire=t>=30 && t%12==0;hands_step(neutral,suspect,test,t);
            assert(hands_seen_by_bot()==SWAT_HANDS_HIDDEN && hands_head_seen(3));
        }
        assert(bot->arsenal.shots && sim.roe[1].state==SWAT_ROE_HOSTILE_FIRING);
    } else {
        // (f) Head behind a beam, torso in view. The leader shouts with the
        // aim 8 degrees beside the head, outside the 3 degree focus, so only
        // sight of the torso carries the command: it reaches the person and
        // goes on the record.
        // Unarmed, they surrender at once. Armed and defiant, the bot
        // challenges too and fires lawfully once the challenge time is up,
        // counted from its own, later command.
        if(test==6){sim.actors[1].gear.disarmed=true;sim.actors[1].mind.external=false;}
        place_actor(0,(b3Pos){0,0,-1});search_aim(0,swat_controller_eye(&sim.actors[1].controller));sim.actors[0].controller.yaw+=8*SWAT_RAD;
        for(;t<5;t++)hands_step(neutral,suspect,test,t);
        SwatHit focused=swat_context_hit(&sim,0,9);
        assert(!hands_head_seen(0) && !hands_head_seen(3) && swat_sim_actor_visible(&sim,0,1,24,60*SWAT_RAD));
        assert(!(focused.kind==SWAT_HIT_ACTOR && focused.index==1) && !sim.roe[1].challenged_tick);
        SwatInput shout=neutral;shout.command=true;hands_step(shout,suspect,test,t++);
        int shouted=sim.tick;assert(sim.roe[1].challenged_tick==shouted+1 && sim.actors[1].mind.heard_commands==1);
        if(test==6)assert(sim.actors[1].gear.surrendered && sim.roe[1].state==SWAT_ROE_SURRENDERED);
        else {
            assert(!sim.actors[1].gear.surrendered);
            for(;t<400 && bot->mind.challenge_actor!=2;t++) {
                hands_step(neutral,suspect,test,t);assert(!bot->arsenal.shots && !hands_head_seen(3));
            }
            int challenged=sim.roe[1].challenged_tick-1;
            assert(bot->mind.challenge_actor==2 && challenged>shouted+SWAT_ROE_CHALLENGE_TICKS/4);
            for(;t<400 && !bot->arsenal.shots;t++)hands_step(neutral,suspect,test,t);
            assert(bot->arsenal.shots && sim.tick-challenged>=SWAT_ROE_CHALLENGE_TICKS && sim.roe[1].state==SWAT_ROE_HOSTILE_ARMED);
        }
    }
    for(int n=0;n<60;n++)hands_step(neutral,test==2 || test==6 ? neutral : suspect,test,t+n);
    printf("SQUAD_HANDS case%d tick%d botShots%d suspectShots%d suspectHealth%.1f roe%s lethal%d violations%d\n",test,t,bot->arsenal.shots,
           sim.actors[1].arsenal.shots,sim.actors[1].health,swat_roe_state_name(sim.roe[1].state),sim.debrief.lethal_violations,sim.debrief.roe_violations);
    assert(!sim.debrief.lethal_violations && !sim.debrief.roe_violations && sim.actors[2].health==100);
    swat_sim_close(&sim);
    static const char* const names[SWAT_SQUAD_HANDS_CASES]={
        "armed, head only over a wall: covered, challenged, never fired on, cuff order completes",
        "head behind a beam, rifle raised: the bot fires lawfully",
        "a shooter who drops the weapon in view is not fired on after the drop",
        "an unarmed civilian half behind cover in a firefight is never targeted or hit",
        "a shooter firing behind a chest-high wall, hands hidden: the bot returns fire lawfully",
        "head hidden, torso seen, armed: challenge recorded, fired on lawfully after defiance",
        "head hidden, torso seen, unarmed: challenge recorded and they surrender"};
    printf("PASS squad hands%d: %s\n",test,names[test]);
}
