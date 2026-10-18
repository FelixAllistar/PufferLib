// Geometry/cover fixture events are explicit; operators use ordinary inputs.
enum { SWAT_DAMAGED_EQUIPMENT_CASES=60 };
static b3Pos damaged_point(const SwatObject* door,float x,float y,float z) {
    return b3OffsetPos(door->center,b3RotateVector(swat_object_rotation(door),swat_v(x,y,z)));
}
static void task_damaged_equipment(int test) {
    bool transparent=test>=42;int extra=test-42;
    int order=test&1?SWAT_ORDER_WEDGE:SWAT_ORDER_PICK;
    int variant=transparent?7+(extra/2)%3:(test/2)%7,rotation=transparent?extra/6:test/14;
    bool low_gap=variant==6 || variant==9;
    SwatMaterial cover_material=transparent?SWAT_GLASS:SWAT_CONCRETE;
    const float yaw[3]={0,SWAT_PI*.5f,.37f};task_fixture();sim.tick=240;
    sim.actors[1].gear.surrendered=sim.actors[2].gear.surrendered=true;
    place_actor(1,(b3Pos){8,0,8});place_actor(2,(b3Pos){9,0,9});
    int owner=swat_world_box(&sim.world,(b3Pos){0,1.3f,0},swat_v(.04f,1.3f,.8f),SWAT_WOOD,120);
    SwatObject* door=&sim.world.objects[owner];door->door=true;door->locked=order==SWAT_ORDER_PICK;door->closed_yaw=yaw[rotation];
    door->yaw=yaw[rotation];door->hinge=damaged_point(door,0,0,-.8f);swat_world_place(door,yaw[rotation]);
    assert(b3Distance(door->center,(b3Pos){0,1.3f,0})<1e-6f);
    for(int side=-1;side<=1;side+=2){int wall=swat_world_box(&sim.world,damaged_point(door,0,0,side*2.15f),swat_v(.18f,1.3f,1.35f),SWAT_CONCRETE,0);swat_world_place(&sim.world.objects[wall],yaw[rotation]);}
    for(int side=-1;side<=1;side+=2){int wall=swat_world_box(&sim.world,damaged_point(door,-2.5f,0,side*3.5f),swat_v(2.5f,1.3f,.18f),SWAT_CONCRETE,0);swat_world_place(&sim.world.objects[wall],yaw[rotation]);}
    int back=swat_world_box(&sim.world,damaged_point(door,-5,0,0),swat_v(.18f,1.3f,3.5f),SWAT_CONCRETE,0);swat_world_place(&sim.world.objects[back],yaw[rotation]);
    const int actors[]={0,3,4,5};const float x[]={-4,-2,-3,-3},z[]={0,-1.5f,-4,4};
    for(int n=0;n<4;n++){b3Pos p=damaged_point(door,x[n],-1.3f,z[n]);place_actor(actors[n],p);}
    if(variant==0)assert(swat_world_cut(&sim.world,owner,(SwatCut){.height=.5f,.width=.4f,.seed=81,.kind=SWAT_CUT_BREACH})>0);
    if(variant==5)for(int y=0;y<5;y++)for(int z=0;z<3;z++)assert(swat_world_cut(&sim.world,owner,(SwatCut){.y=-.8f+y*.35f,.z=-.35f+z*.35f,.height=.065f,.width=.065f,.seed=(uint32_t)(81+y*3+z),.kind=SWAT_CUT_BREACH})>0);
    int cover=-1;
    if(low_gap){cover=swat_world_box(&sim.world,damaged_point(door,-.25f,.3f,0),swat_v(.1f,1.0f,.85f),cover_material,100);swat_world_place(&sim.world.objects[cover],yaw[rotation]);}
    if(variant==1 || variant==8){cover=swat_world_box(&sim.world,damaged_point(door,-.25f,0,0),swat_v(.1f,.55f,.45f),cover_material,100);swat_world_place(&sim.world.objects[cover],yaw[rotation]);}
    if(low_gap){SwatInput duck=swat_neutral_input();duck.crouch=true;for(int n=0;n<24;n++)swat_sim_step(&sim,&duck);}
    b3Vec3 aim=b3SubPos(damaged_point(door,0,low_gap?-1.05f:.75f,0),swat_controller_eye(&sim.actors[0].controller));
    sim.actors[0].controller.yaw=b3Atan2(aim.z,aim.x);sim.actors[0].controller.pitch=b3Atan2(aim.y,hypotf(aim.x,aim.z));
    SwatHit seen=swat_context_hit(&sim,0,24);assert(seen.kind==SWAT_HIT_WORLD && seen.index==owner);
    SwatInput in=swat_neutral_input();in.squad_order=order;in.squad_team=1;in.squad_queue=true;swat_sim_step(&sim,&in);
    int recipient=-1;for(int i=3;i<6;i++)if(sim.actors[i].mind.pending_order==order){assert(recipient<0);recipient=i;}assert(recipient>=0);
    if(variant==2 || variant==4 || variant==5 || variant==7){cover=swat_world_box(&sim.world,door->center,swat_v(.2f,1.3f,.85f),cover_material,100);swat_world_place(&sim.world.objects[cover],yaw[rotation]);}
    if(variant==7) {
        b3Pos eye=swat_controller_eye(&sim.actors[0].controller);b3Vec3 direction=swat_controller_aim(&sim.actors[0].controller);
        SwatHit optical=swat_world_sight_ray(&sim.world,eye,direction,24,sim.actors[0].controller.body.body);
        SwatHit physical=swat_world_ray(&sim.world,eye,direction,24,sim.actors[0].controller.body.body);
        assert(optical.kind==SWAT_HIT_WORLD && optical.index==owner && physical.kind==SWAT_HIT_WORLD && physical.index==cover);
    }
    in=swat_neutral_input();in.squad_execute=true;swat_sim_step(&sim,&in);
    bool changed=false,blocked=false,complete=false,codec_checked=false,crouched_work=false;int scan_max=0;int stock=sim.actors[recipient].gear.wedges;
    for(int t=0;t<1800;t++) {
        SwatActor* a=&sim.actors[recipient];SwatMind* m=&a->mind;
        if(variant==3 && !changed && a->gear.door_ticks>=10){assert(swat_world_cut(&sim.world,owner,(SwatCut){.height=.5f,.width=.4f,.seed=81,.kind=SWAT_CUT_BREACH})>0);changed=true;}
        if((variant==2 || variant==5 || variant==7) && t==360){
            if(!blocked)fprintf(stderr,"Door access test%d: optical cover%d but blocked%d goal%d scan%d toolticks%d feet%.5f %.5f %.5f\n",test,variant,blocked,m->tool_goal,m->tool_scan,a->gear.door_ticks,swat_body_feet_position(&a->controller.body).x,swat_body_feet_position(&a->controller.body).y,swat_body_feet_position(&a->controller.body).z);
            assert(blocked && door->locked==(order==SWAT_ORDER_PICK) && door->wedge_owner<0 && a->gear.wedges==stock);
            assert(swat_world_damage(&sim.world,cover,1000));changed=true;
        }
        if(variant==4 && t==120){assert(blocked);assert(swat_sim_set_player(&sim,recipient-2,true));assert(!a->mind.bot && a->mind.order==SWAT_ORDER_HOLD && !a->mind.tool_blocked && !a->gear.door_ticks);task_state(100+test,t);complete=true;break;}
        SwatHit hand=swat_context_hit(&sim,recipient,1.7f);int before=a->gear.door_ticks;
        swat_sim_step(&sim,&(SwatInput){0});task_state(100+test,t);
        if(a->gear.door_ticks>before){assert(hand.kind==SWAT_HIT_WORLD && hand.index==owner);crouched_work|=a->controller.body.crouched;}
        blocked|=m->tool_blocked;scan_max=scan_max>m->tool_scan?scan_max:m->tool_scan;
        if(m->tool_blocked){
            assert(!a->gear.door_ticks && m->order==order);
            if(!codec_checked){
                static SwatMap map;static SwatSnapshot state,bad,decoded;static unsigned char bytes[SWAT_NET_PACKET_MAX];
                swat_capture_map(&sim,1,&map);swat_apply_map(&replica,&map);swat_capture_snapshot(&sim,1,&state);
                assert(swat_apply_snapshot(&replica,&state) && replica.actors[recipient].mind.tool_blocked);
                uint32_t before=swat_replay_digest(&replica);bad=state;bad.actors[recipient].mind.order=SWAT_ORDER_HOLD;
                assert(!swat_apply_snapshot(&replica,&bad) && before==swat_replay_digest(&replica));
                size_t n=swat_encode_snapshot(bytes,sizeof(bytes),&bad);assert(n && !swat_decode_snapshot(&decoded,bytes,n));
                swat_sim_close(&replica);codec_checked=true;
            }
        }
        b3Pos feet=swat_body_feet_position(&a->controller.body);assert(feet.y>-.15f && feet.y<.5f && sim.end==SWAT_RUNNING);
        bool done=order==SWAT_ORDER_PICK?!door->locked:door->wedge_owner==recipient;
        if(done && m->order==SWAT_ORDER_HOLD){complete=true;assert(!m->tool_blocked && !m->order_door && a->gear.used_tools);break;}
    }
    if(!complete){SwatActor*a=&sim.actors[recipient];b3Pos p=swat_body_feet_position(&a->controller.body);fprintf(stderr,"Damaged task%d stalled order%d blocked%d scan%d point%.4f %.4f %.4f feet%.4f %.4f %.4f\n",test,a->mind.order,a->mind.tool_blocked,a->mind.tool_scan,a->mind.task_point.x,a->mind.task_point.y,a->mind.task_point.z,p.x,p.y,p.z);}
    assert(complete);if(variant==3 || variant==7)assert(changed);if(variant==5)assert(scan_max>16);if(low_gap)assert(crouched_work);
    if(variant==8 || variant==9)assert(sim.world.objects[cover].active && sim.world.objects[cover].health==100);
    for(int i=0;i<sim.actor_count;i++)assert(sim.actors[i].health==100 && !sim.actors[i].arsenal.shots);
    swat_sim_close(&sim);printf("PASS damaged door task%d: order%d geometry/cover%d rotation%d, physical reach, bounded search and truthful replica/cancellation\n",test,order,variant,rotation);
}
