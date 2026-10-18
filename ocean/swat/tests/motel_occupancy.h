// This full-motel fixture reproduces the off-grid reception corner from the
// failed ordinary-input journal. The route must first clear the outer corner,
// rather than shortcutting through it from a displaced player position.
static void test_motel_occupancy_corner(void) {
    static SwatSim s;SwatConfig cfg=swat_default_config();cfg.mission=SWAT_MOTEL;cfg.motel_occupancy=SWAT_MOTEL_DISPERSED;
    cfg.randomize=cfg.hostile_fire=false;swat_sim_init(&s,cfg,82);
    assert(s.world.objects[120].door);s.world.objects[120].locked=false;s.world.objects[120].door_open=true;
    for(int n=0;n<120;n++)swat_world_step_doors(&s.world);
    assert(s.world.objects[120].door_angle>1.5f);
    swat_sim_spawn_actor(&s,0,SWAT_OFFICER,(b3Pos){-11.724060f,.015f,.381795f},-1.685f);
    b3Pos next;assert(swat_navigation_next_for_actor(&s,0,(b3Pos){-11.2f,.015f,-2.8f},&next));
    printf("CORNER_NEXT %.9f %.9f %.9f\n",next.x,next.y,next.z);
    assert(next.z>.45f && next.x>-12.2f);
    swat_sim_close(&s);puts("PASS reception corner: off-grid start joins a physically reachable sample before turning around the solid facade");
}
static void test_motel_occupancy_portal(int variant) {
    static SwatSim s;SwatConfig cfg=swat_default_config();cfg.mission=SWAT_MOTEL;cfg.motel_occupancy=SWAT_MOTEL_DISPERSED;
    cfg.randomize=cfg.hostile_fire=false;cfg.max_ticks=6000;swat_config_human(&cfg);swat_sim_init(&s,cfg,82);
    for(int n=0;n<s.world.count;n++)if(s.world.objects[n].door){s.world.objects[n].locked=false;s.world.objects[n].door_open=true;}
    for(int n=0;n<120;n++)swat_world_step_doors(&s.world);
    for(int n=1;n<s.actor_count;n++)if(s.actors[n].present) {
        if(s.actors[n].role==SWAT_OFFICER)s.actors[n].mind.order=SWAT_ORDER_HOLD;
        else s.actors[n].gear.surrendered=true;
    }
    const b3Pos leaders[]={{-6.543f,.015f,-1.897f},{-6.301f,.015f,-.9f},{-7.102f,-.065f,2.083f}};
    const b3Pos followers[]={{-6.804f,.015f,-1.101f},{-6.142f,.015f,-1.97f},{-5.588f,.015f,-1.008f}};
    const float yaw[]={SWAT_PI*.5f,-SWAT_PI*.5f,-.809f};
    assert(variant>=0 && variant<3);
    swat_sim_spawn_actor(&s,0,SWAT_OFFICER,leaders[variant],yaw[variant]);
    swat_sim_spawn_actor(&s,4,SWAT_OFFICER,followers[variant],0);
    s.actors[4].mind.bot=true;s.actors[4].mind.order=SWAT_ORDER_FALL_IN;
    SwatInput in=swat_neutral_input();
    for(int n=0;n<600;n++)swat_sim_step(&s,&in);
    b3Pos p=swat_body_feet_position(&s.actors[4].controller.body);
    printf("PORTAL_FOLLOWER %d %.9f %.9f %.9f\n",variant,p.x,p.y,p.z);fflush(stdout);
    assert(p.z>.35f && s.actors[4].health==100 && s.actors[0].health==100 && s.end==SWAT_RUNNING);
    if(variant==2)assert(b3Distance(swat_controller_eye(&s.actors[4].controller),(b3Pos){-4,1,-1.17f})>=3.2f);
    swat_sim_close(&s);puts("PASS follower portal: a recalled officer just inside the furnished room clears the leader's narrow exit using ordinary movement");
}
// Seeded initial populations use the actual motel collision, not a test room.
static void test_motel_occupancy_restraints(const char* trace_path,int only) {
    static SwatSim s;const float doors[]={0,-7.2f,-3.2f,.8f,4.8f};
    FILE* trace=trace_path?fopen(trace_path,"w"):NULL;assert(!trace_path || trace);
    for(int variant=0;variant<8;variant++)if(only<0 || only==variant) {
        int room=1+variant/2;SwatConfig cfg=swat_default_config();cfg.mission=SWAT_MOTEL;cfg.randomize=cfg.hostile_fire=false;cfg.max_ticks=6000;swat_config_human(&cfg);
        swat_sim_init(&s,cfg,82);
        for(int n=1;n<s.actor_count;n++)if(s.actors[n].present && s.actors[n].role!=SWAT_OFFICER){b3DestroyBody(s.actors[n].controller.body.body);memset(&s.actors[n],0,sizeof(s.actors[n]));}
        for(int n=0;n<s.world.count;n++)if(s.world.objects[n].door){s.world.objects[n].locked=false;s.world.objects[n].door_open=true;}
        for(int n=0;n<120;n++)swat_world_step_doors(&s.world);
        float centre=-10+4*room;
        swat_sim_spawn_actor(&s,1,variant&1?SWAT_CIVILIAN:SWAT_SUSPECT,(b3Pos){centre-.8f,.015f,-3.6f},SWAT_PI*.5f);s.actors[1].gear.surrendered=true;
        assert(swat_body_clear_at(&s.actors[1].controller.body,swat_body_feet_position(&s.actors[1].controller.body)));
        swat_sim_spawn_actor(&s,0,SWAT_OFFICER,(b3Pos){doors[room],.015f,2.5f},0);
        swat_sim_spawn_actor(&s,3,SWAT_OFFICER,(b3Pos){doors[room]+1.2f,.015f,3.8f},0);
        for(int i=3;i<6;i++){s.actors[i].mind.bot=true;s.actors[i].mind.order=SWAT_ORDER_HOLD;s.actors[i].mind.team=i==3?1:2;}
        b3Vec3 aim=b3SubPos(swat_controller_eye(&s.actors[1].controller),swat_controller_eye(&s.actors[0].controller));
        s.actors[0].controller.yaw=atan2f(aim.z,aim.x);s.actors[0].controller.pitch=atan2f(aim.y,hypotf(aim.x,aim.z));
        SwatInput in=swat_neutral_input();in.squad_order=SWAT_ORDER_CUFF;in.squad_team=1;swat_sim_step(&s,&in);
        assert(s.actors[3].mind.pending_order==SWAT_ORDER_CUFF && s.actors[3].mind.pending_actor==2);
        // Fixture clears the commander out of the working path; only ordinary
        // squad input/controller/physical cuffing advances the measured task.
        swat_sim_spawn_actor(&s,0,SWAT_OFFICER,(b3Pos){doors[room]+2,.015f,4.5f},0);
        int tick=0;in=swat_neutral_input();
        for(;tick<1800 && !s.actors[1].gear.restrained;tick++){swat_sim_step(&s,&in);if(trace)fprintf(trace,"CUFF %d %d %08x\n",variant,s.tick,swat_replay_digest(&s));}
        b3Pos p=swat_body_feet_position(&s.actors[3].controller.body);
        printf("REAR_CUFF %d %d %.9f %.9f %.9f restrained%d\n",variant,tick,p.x,p.y,p.z,s.actors[1].gear.restrained);
        if(tick==1800){SwatMind* m=&s.actors[3].mind;SwatContext c=swat_context(&s,3);fprintf(stderr,"Rear cuff blocked goal %.6f %.6f %.6f waypoint %.6f %.6f %.6f cache%d next%d context%d/%d range%.6f ready%d crouch%d yaw%.6f pitch%.6f\n",m->goal.x,m->goal.y,m->goal.z,m->waypoint.x,m->waypoint.y,m->waypoint.z,m->restraint_goal,m->restraint_replan_tick,c.action,c.hit.index,c.hit.distance,c.ready,s.actors[3].controller.body.crouched,s.actors[3].controller.yaw,s.actors[3].controller.pitch);}
        assert(tick<1800 && s.actors[1].gear.restrained && s.actors[1].health==100 && !s.debrief.roe_violations && !s.actors[3].arsenal.shots);
        swat_sim_close(&s);
    }
    if(trace)assert(!fclose(trace));
    puts("PASS rear restraint: suspects and civilians behind room furniture in every guest room use reachable physical hand positions");
}
static void test_motel_occupancy_escort(const char* trace_path,int only) {
    static SwatSim s;FILE* trace=trace_path?fopen(trace_path,"w"):NULL;assert(!trace_path || trace);
    assert(only>=-1 && only<12);
    for(int variant=0;variant<12;variant++)if(only<0 || only==variant) {
    int room=1+variant/3,bay=variant%3;const float bays[3][2]={{0,-2.8f},{-.4f,-3.2f},{-.8f,-3.6f}};
    SwatConfig cfg=swat_default_config();cfg.mission=SWAT_MOTEL;cfg.tactical_rules=true;cfg.randomize=cfg.hostile_fire=false;cfg.max_ticks=6000;
    swat_sim_init(&s,cfg,82);
    for(int i=1;i<s.actor_count;i++)if(s.actors[i].present){b3DestroyBody(s.actors[i].controller.body.body);memset(&s.actors[i],0,sizeof(s.actors[i]));}
    for(int n=0;n<s.world.count;n++)if(s.world.objects[n].door){s.world.objects[n].locked=false;s.world.objects[n].door_open=true;}
    for(int n=0;n<120;n++)swat_world_step_doors(&s.world);
    swat_sim_spawn_actor(&s,0,SWAT_OFFICER,(b3Pos){.1f,-.065f,9.672f},0);
    swat_sim_spawn_actor(&s,1,SWAT_SUSPECT,(b3Pos){-10+4*room+bays[bay][0],.015f,bays[bay][1]},SWAT_PI*.5f);
    s.actors[1].gear.surrendered=s.actors[1].gear.restrained=true;s.actors[1].mind.escort_owner=0;s.actor_count=3;
    SwatInput in=swat_neutral_input();for(int n=0;n<1800;n++){swat_sim_step(&s,&in);if(trace)fprintf(trace,"ESCORT %d %d %08x\n",variant,s.tick,swat_replay_digest(&s));}
    SwatActor* a=&s.actors[1];b3Pos p=swat_body_feet_position(&a->controller.body);
    printf("REAR_ESCORT %d %.9f %.9f %.9f crouch%d height%.9f clear%d waypoint %.9f %.9f %.9f\n",variant,p.x,p.y,p.z,a->controller.body.crouched,a->controller.body.totalHeight,swat_body_clear_at(&a->controller.body,p),a->mind.waypoint.x,a->mind.waypoint.y,a->mind.waypoint.z);
    assert(p.z>1 && a->health==100);swat_sim_close(&s);
    }
    if(trace)assert(!fclose(trace));
    puts("PASS rear escort: all guest rooms and three rear bays walk around original furniture and exit through actual doors");
}
static void test_motel_occupancy_portal_crowd(const char* trace_path) {
    static SwatSim s;SwatConfig cfg=swat_default_config();cfg.mission=SWAT_MOTEL;cfg.motel_occupancy=SWAT_MOTEL_MIXED;cfg.layout_seed=7;
    cfg.randomize=cfg.hostile_fire=false;cfg.max_ticks=6000;swat_config_human(&cfg);swat_sim_init(&s,cfg,2026);
    assert(s.world.objects[120].door);s.world.objects[120].locked=false;s.world.objects[120].door_open=true;
    for(int n=0;n<120;n++)swat_world_step_doors(&s.world);
    for(int i=1;i<s.actor_count;i++)if(s.actors[i].present && s.actors[i].role!=SWAT_OFFICER)s.actors[i].gear.surrendered=true;
    swat_sim_spawn_actor(&s,0,SWAT_OFFICER,(b3Pos){-9.524246216f,.014999986f,-1.410268664f},-1.254205108f);
    const b3Pos starts[]={{-11.591469765f,-.027019441f,2.671634197f},{-10.455988884f,.014999986f,.939448833f},{-12.471404076f,-.064999998f,1.267489672f}};
    const float yaw[]={-.088194847f,-.110424586f,-1.648510814f};
    for(int i=3;i<6;i++){swat_sim_spawn_actor(&s,i,SWAT_OFFICER,starts[i-3],yaw[i-3]);s.actors[i].mind.bot=true;s.actors[i].mind.order=SWAT_ORDER_FALL_IN;}
    s.actors[7].gear.restrained=true;s.actors[7].mind.escort_owner=0;
    FILE* trace=trace_path?fopen(trace_path,"w"):NULL;assert(!trace_path || trace);
    b3Pos goal={-9.524f,0,2.5f},waypoint=goal;int replan=0,t=0;
    for(;t<1200;t++) {
        b3Pos p=swat_body_feet_position(&s.actors[0].controller.body);b3Vec3 d=b3SubPos(goal,p);
        if(hypotf(d.x,d.z)<.43f && fabsf(d.y)<.4f)break;
        SwatInput in=swat_neutral_input();
        if(!swat_navigation_yield_door(&s,0,&in)) {
            if(t>=replan || b3Distance(p,waypoint)<.3f){
                if(!swat_navigation_next_for_actor(&s,0,goal,&waypoint)){swat_sim_step(&s,&in);if(trace)fprintf(trace,"CROWD %d %08x\n",s.tick,swat_replay_digest(&s));continue;}
                replan=t+30;
            }
            float yaw=b3Atan2(waypoint.z-p.z,waypoint.x-p.x);
            in.yaw_delta=swat_clamp(swat_angle(yaw-s.actors[0].controller.yaw),-6*SWAT_RAD,6*SWAT_RAD);
            if(fabsf(swat_angle(yaw-s.actors[0].controller.yaw))<35*SWAT_RAD)in.forward=.8f;
            in.gait=SWAT_SLOW;in.crouch=swat_navigation_crouch(&s,p)||swat_navigation_crouch(&s,waypoint);
        }else replan=0;
        swat_sim_step(&s,&in);if(trace)fprintf(trace,"CROWD %d %08x\n",s.tick,swat_replay_digest(&s));
    }
    b3Pos p=swat_body_feet_position(&s.actors[0].controller.body);
    printf("CROWDED_PORTAL %d %.9f %.9f %.9f\n",t,p.x,p.y,p.z);fflush(stdout);
    assert(t<1200);
    SwatInput in=swat_neutral_input();for(int n=0;n<600;n++){swat_sim_step(&s,&in);if(trace)fprintf(trace,"CROWD %d %08x\n",s.tick,swat_replay_digest(&s));}
    assert(swat_body_feet_position(&s.actors[7].controller.body).z>.6f && s.actors[7].health==100 && s.actors[0].health==100);
    for(int i=3;i<6;i++)assert(s.actors[i].health==100);
    if(trace)assert(!fclose(trace));
    swat_sim_close(&s);puts("PASS crowded reception portal: three recalled officers leave a physical exit for the slow-moving commander and escorted civilian");
}
static void test_motel_occupancy_follow(void) {
    static SwatSim s;SwatConfig cfg=swat_default_config();cfg.mission=SWAT_MOTEL;cfg.motel_occupancy=SWAT_MOTEL_MIXED;
    cfg.randomize=cfg.hostile_fire=false;cfg.max_ticks=6000;swat_config_human(&cfg);swat_sim_init(&s,cfg,82);
    for(int n=0;n<s.world.count;n++)if(s.world.objects[n].door){s.world.objects[n].locked=false;s.world.objects[n].door_open=true;}
    for(int n=0;n<120;n++)swat_world_step_doors(&s.world);
    for(int i=1;i<s.actor_count;i++)if(s.actors[i].present && s.actors[i].role!=SWAT_OFFICER)s.actors[i].gear.surrendered=s.actors[i].gear.restrained=true;
    swat_sim_spawn_actor(&s,0,SWAT_OFFICER,(b3Pos){-6.059f,-.065f,3.295f},-1.561f);
    const b3Pos starts[]={{-8.883f,-.065f,2.729f},{-6.581f,.015f,-1.143f},{-6.916f,.065f,4.059f}};
    for(int i=3;i<6;i++){swat_sim_spawn_actor(&s,i,SWAT_OFFICER,starts[i-3],0);s.actors[i].mind.bot=true;s.actors[i].mind.order=SWAT_ORDER_FALL_IN;}
    SwatInput in=swat_neutral_input();for(int n=0;n<1200;n++)swat_sim_step(&s,&in);
    b3Pos p=swat_body_feet_position(&s.actors[4].controller.body);
    printf("BLOCKED_TRAIL %.9f %.9f %.9f\n",p.x,p.y,p.z);
    assert(p.z>.6f && b3Distance(p,swat_body_feet_position(&s.actors[0].controller.body))<3.5f && s.actors[4].health==100);
    swat_sim_close(&s);puts("PASS recalled worker: blocked trailing formation chooses a supported rear flank and physically leaves the room");
}
static void test_motel_occupancy(const char* trace_path) {
    static SwatSim s;static b3Pos first[SWAT_MOTEL_OCCUPANCIES][6];
    const int ids[]={1,2,6,7,8,9};
    const int rooms[SWAT_MOTEL_OCCUPANCIES][6]={{2,1,3,0,4,-1},{2,1,3,0,4,-1},{1,2,4,0,3,-1},{2,1,3,0,4,1}};
    const uint32_t seeds[]={0,1,2,7,81,947,UINT32_MAX};
    bool varied[SWAT_MOTEL_OCCUPANCIES]={0};FILE* trace=trace_path?fopen(trace_path,"w"):NULL;assert(!trace_path || trace);
    int cases=0;
    for(int p=0;p<SWAT_MOTEL_OCCUPANCIES;p++)for(int k=0;k<7;k++) {
        b3Pos expected[6]={0};float yaw[6]={0};uint32_t digest=0;
        for(int repeat=0;repeat<3;repeat++) {
            SwatConfig cfg=swat_default_config();cfg.mission=SWAT_MOTEL;cfg.motel_occupancy=p;cfg.layout_seed=seeds[k];
            cfg.randomize=cfg.hostile_fire=false;swat_config_human(&cfg);
            swat_sim_init(&s,cfg,repeat==2?481:81);
            assert(s.world.motel && s.world.count==SWAT_MOTEL_OBJECTS && s.tick==0 && s.end==SWAT_RUNNING);
            int civilians=swat_motel_occupancy_civilians(p),count=civilians+2;
            assert(s.actor_count== (p==SWAT_MOTEL_BUSY?10:9));
            int armed=0,unarmed=0;
            for(int n=0;n<6;n++) {
                SwatActor* a=&s.actors[ids[n]];
                if(n>=count){assert(!a->present);continue;}
                assert(a->present && a->alive && a->health==100 && !a->gear.restrained && !a->rescued);
                armed+=a->role==SWAT_SUSPECT;unarmed+=a->role==SWAT_CIVILIAN;
                b3Pos feet=swat_body_feet_position(&a->controller.body);
                assert(swat_world_room(&s.world,b3OffsetPos(feet,swat_v(0,.9f,0)))==rooms[p][n]);
                if(p!=SWAT_MOTEL_STANDARD) {
                    if(!swat_body_clear_at(&a->controller.body,feet))fprintf(stderr,"Occupied spawn clearance profile%d layout%u actor%d %.5f %.5f %.5f\n",p,seeds[k],ids[n],(float)feet.x,(float)feet.y,(float)feet.z);
                    assert(swat_body_clear_at(&a->controller.body,feet));
                    assert(feet.y>-.12f && feet.y<.05f);
                    for(int j=0;j<n;j++)assert(b3Distance(feet,swat_body_feet_position(&s.actors[ids[j]].controller.body))>.94f);
                }
                if(!repeat){expected[n]=feet;yaw[n]=a->controller.yaw;if(!k)first[p][n]=feet;else if(b3Distance(first[p][n],feet)>.2f)varied[p]=true;}
                else assert(b3Distance(expected[n],feet)<1e-6f && yaw[n]==a->controller.yaw);
                if(trace)fprintf(trace,"SPAWN %d %u %d %d %.9f %.9f %.9f %.9f\n",p,seeds[k],repeat,ids[n],feet.x,feet.y,feet.z,(double)a->controller.yaw);
            }
            assert(armed==2 && unarmed==civilians && !s.actors[10].present && !s.actors[11].present);
            if(!repeat)digest=swat_replay_digest(&s);else if(repeat==1)assert(digest==swat_replay_digest(&s));
            if(trace)fprintf(trace,"STATE %d %u %d %08x\n",p,seeds[k],repeat,swat_replay_digest(&s));
            // Moving the hypothetical standing body into a wall must fail and
            // leave its actual pose and shape unchanged.
            SwatBody* body=&s.actors[0].controller.body;b3Pos before=swat_body_feet_position(body);
            assert(!swat_body_clear_at(body,(b3Pos){-4,0,-2}));
            assert(b3Distance(before,swat_body_feet_position(body))==0);
            swat_sim_close(&s);cases++;
        }
    }
    assert(!varied[0] && varied[1] && varied[2] && varied[3]);
    if(trace)assert(!fclose(trace));
    printf("PASS motel occupancy: %d real-world starts, four populations, seven layout seeds, exact repeats, reset-independent placement, full standing clearance and separated occupants\n",cases);
}
static void test_motel_occupancy_deep_exit(const char* trace_path) {
    static SwatSim s;SwatConfig cfg=swat_default_config();cfg.mission=SWAT_MOTEL;cfg.motel_occupancy=SWAT_MOTEL_MIXED;cfg.layout_seed=7;
    cfg.randomize=cfg.hostile_fire=false;cfg.max_ticks=6000;swat_config_human(&cfg);swat_sim_init(&s,cfg,2026);
    for(int n=0;n<s.world.count;n++)if(s.world.objects[n].door){s.world.objects[n].locked=false;s.world.objects[n].door_open=true;}
    for(int n=0;n<120;n++)swat_world_step_doors(&s.world);
    for(int i=1;i<s.actor_count;i++)if(s.actors[i].present && s.actors[i].role!=SWAT_OFFICER)s.actors[i].gear.surrendered=s.actors[i].gear.restrained=true;
    // Starts captured from the post-combat Mixed failure. Original furniture
    // and full capsule collisions stay in place; this is fixture setup only.
    swat_sim_spawn_actor(&s,0,SWAT_OFFICER,(b3Pos){5.807599068f,.014999986f,-2.604274988f},1.486f);swat_equipment_kit(&s.actors[0].gear,2);
    const b3Pos starts[]={{4.027945995f,.016238511f,.909071267f},{6.107934952f,.014999986f,1.211516857f},{5.033127308f,.014999986f,.497940004f}};
    for(int i=3;i<6;i++){swat_sim_spawn_actor(&s,i,SWAT_OFFICER,starts[i-3],0);s.actors[i].mind.bot=true;s.actors[i].mind.order=SWAT_ORDER_FALL_IN;}
    FILE* trace=trace_path?fopen(trace_path,"w"):NULL;assert(!trace_path || trace);b3Pos goal={6.3f,0,3.2f},waypoint=goal;int replan=0,t=0;
    for(;t<1200;t++) {
        b3Pos p=swat_body_feet_position(&s.actors[0].controller.body);if(b3Distance(p,goal)<.45f)break;
        SwatInput in=swat_neutral_input();if(!swat_navigation_yield_door(&s,0,&in)) {
            bool route=true;if(t>=replan || b3Distance(p,waypoint)<.3f){route=swat_navigation_next_for_actor(&s,0,goal,&waypoint);if(route)replan=t+30;}
            b3Vec3 d=b3SubPos(route?waypoint:goal,p);float yaw=b3Atan2(d.z,d.x);in.yaw_delta=swat_clamp(swat_angle(yaw-s.actors[0].controller.yaw),-6*SWAT_RAD,6*SWAT_RAD);
            in.forward=route && fabsf(swat_angle(yaw-s.actors[0].controller.yaw))<35*SWAT_RAD?.8f:0;in.gait=SWAT_SLOW;in.crouch=swat_navigation_crouch(&s,p)||swat_navigation_crouch(&s,waypoint);
        }else replan=0;
        swat_sim_step(&s,&in);if(trace)fprintf(trace,"DEEP_EXIT %d %08x\n",s.tick,swat_replay_digest(&s));
    }
    b3Pos p=swat_body_feet_position(&s.actors[0].controller.body);printf("DEEP_EXIT tick%d %.9f %.9f %.9f\n",t,p.x,p.y,p.z);fflush(stdout);assert(t<1200);for(int i=0;i<s.actor_count;i++)if(s.actors[i].present)assert(s.actors[i].health==100);
    if(trace){assert(!fclose(trace));}
    swat_sim_close(&s);puts("PASS furnished rear exit: three recalled officers clear the doorway before the commander leaves the rear bay, original bed and ordinary movement intact");
}
