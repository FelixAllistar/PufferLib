// Simulation-owned initial situations. Geometry and accepted art stay fixed.
// The seed chooses authored floor bays; full physical clearance and separation
// are checked before creating each alternate occupant. No mid-round relocation.
static bool motel_spawn_floor(SwatSim* s,b3Pos desired,b3Pos* result) {
    float low=1e9f,high=-1e9f,half=s->actors[0].controller.body.bodyRadius*.5f;
    for(int x=-1;x<=1;x++)for(int z=-1;z<=1;z++) {
        b3Pos from=b3OffsetPos(desired,swat_v(x*half,.30f,z*half));
        SwatHit hit=swat_world_ray(&s->world,from,swat_v(0,-1,0),.6f,s->actors[0].controller.body.body);
        if(hit.kind!=SWAT_HIT_WORLD || hit.normal.y<.7f || hit.point.y<-.15f || hit.point.y>.1f)return false;
        const SwatObject* o=&s->world.objects[hit.index];
        if(o->door || o->part==SWAT_PART_FIXTURE || o->part==SWAT_PART_OBSTACLE)return false;
        low=fminf(low,(float)hit.point.y);high=fmaxf(high,(float)hit.point.y);
    }
    if(high-low>.025f)return false;
    desired.y=high+.015f;
    for(int i=1;i<s->actor_count;i++)if(s->actors[i].present &&
        b3Distance(desired,swat_body_feet_position(&s->actors[i].controller.body))<.95f)return false;
    if(!swat_body_clear_at(&s->actors[0].controller.body,desired))return false;
    *result=desired;return true;
}
static uint32_t motel_spawn_hash(uint32_t seed,int actor,int profile) {
    uint32_t h=seed^((uint32_t)actor*0x85ebca6bu)^((uint32_t)profile*0x9e3779b9u);
    h^=h>>16;h*=0x7feb352du;h^=h>>15;h*=0x846ca68bu;return h^(h>>16);
}
static bool motel_spawn_occupants(SwatSim* s) {
    const int actors[]={1,2,6,7,8,9};
    static const b3Pos original[]={{-2,0,-2},{-6,0,-2},{2,0,-2},{-10,0,-1.4f},{6,0,-1.4f}};
    int profile=s->config.motel_occupancy;
    if(profile==SWAT_MOTEL_STANDARD) {
        for(int n=0;n<5;n++)swat_sim_spawn_actor(s,actors[n],n==0 || n==4?SWAT_SUSPECT:SWAT_CIVILIAN,original[n],SWAT_PI*.5f);
        s->actor_count=9;return true;
    }
    static const int rooms[SWAT_MOTEL_OCCUPANCIES][6]={
        {2,1,3,0,4,-1},{2,1,3,0,4,-1},{1,2,4,0,3,-1},{2,1,3,0,4,1}
    };
    static const float guest[6][2]={{0,-2.8f},{-.4f,-3.2f},{-.8f,-3.6f},{1.6f,-1.6f},{0,-1.2f},{-.8f,-1.2f}};
    static const float reception[6][2]={{-11.2f,-2.8f},{-10.4f,-2.4f},{-9.2f,-2.4f},{-11.2f,-1.6f},{-10,-1.2f},{-8.8f,-1.6f}};
    int count=profile==SWAT_MOTEL_BUSY?6:5,bays=profile==SWAT_MOTEL_DISPERSED?3:6;
    s->actor_count=count==6?10:9;
    for(int n=0;n<count;n++) {
        int room=rooms[profile][n];uint32_t hash=motel_spawn_hash(s->config.layout_seed,actors[n],profile);
        bool found=false;b3Pos feet={0};
        for(int step=0;step<bays;step++) {
            int bay=((int)(hash%(uint32_t)bays)+step)%bays;
            b3Pos desired=room?(b3Pos){-10+room*4+guest[bay][0],0,guest[bay][1]}:(b3Pos){reception[bay][0],0,reception[bay][1]};
            if(swat_world_room(&s->world,b3OffsetPos(desired,swat_v(0,.9f,0)))!=room)continue;
            if(motel_spawn_floor(s,desired,&feet)){found=true;break;}
        }
        if(!found){fprintf(stderr,"Motel occupancy%d seed%u cannot place actor%d in room%d\n",profile,s->config.layout_seed,actors[n],room);return false;}
        float yaw=SWAT_PI*.5f+((int)((hash>>16)%5)-2)*.12f;
        swat_sim_spawn_actor(s,actors[n],n==0 || n==4?SWAT_SUSPECT:SWAT_CIVILIAN,feet,yaw);
    }
    return true;
}
