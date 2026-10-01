// Correctness and moving-scene regressions for the real viewer implementation.
#define main arpg_viewer_main
#include "../arpg.c"
#undef main
#include <assert.h>
extern void glFinish(void);
extern const unsigned char* glGetString(unsigned int name);

static void setup(ARPG* e,float* obs,float* actions,float* reward,float* terminal) {
    e->cfg=ar_config_from_kwargs(puf_ini_section(&g_controls_ini,"env",0));
    e->cfg.max_steps=INT32_MAX;e->rng=42;e->num_agents=1;
    e->agents[0].observations=obs;e->agents[0].actions=actions;
    e->agents[0].rewards=reward;e->agents[0].terminals=terminal;c_reset(e);
}
static void test_budget(void) {
    double accumulator=0,dropped=0;int ticks=0;
    // Ten seconds at 10 FPS cannot leave old input debt to replay on recovery.
    for(int frame=0;frame<100;frame++) {
        accumulator=ar_frame_budget(accumulator,.1,&dropped);int steps=0;
        while(accumulator>=AR_FAST_SIM_DT && steps<AR_FAST_MAX_CATCHUP_STEPS) {
            accumulator-=AR_FAST_SIM_DT;steps++;ticks++;
        }
        assert(steps<=5 && accumulator<AR_FAST_SIM_DT);
    }
    assert(ticks==500 && dropped>1.6 && dropped<1.7);
    accumulator=ar_frame_budget(accumulator,AR_FAST_SIM_DT,&dropped);
    int recovered=0;while(accumulator>=AR_FAST_SIM_DT){accumulator-=AR_FAST_SIM_DT;recovered++;}
    assert(recovered==1);
    accumulator=0;dropped=0;ticks=0;
    for(int frame=0;frame<1200;frame++) {
        accumulator=ar_frame_budget(accumulator,AR_FAST_SIM_DT*.5,&dropped);
        while(accumulator>=AR_FAST_SIM_DT){accumulator-=AR_FAST_SIM_DT;ticks++;}
    }
    assert(ticks==600 && dropped==0);
    accumulator=ar_frame_budget(AR_FAST_SIM_DT*.25,3,&dropped);
    assert(accumulator<6*AR_FAST_SIM_DT && dropped>2.9);
}
static void test_poses(void) {
    ARPG e={0};float obs[AR_OBS_SIZE],actions[NUM_ATNS]={0},reward=0,terminal=0;
    setup(&e,obs,actions,&reward,&terminal);ARClient* c=ar_client(&e);
    ar_pose_reset(c,&e);ar_pose_before_step(c,&e);
    float old=e.px;e.px+=.1f;e.pets.x[0]+=.1f;e.tick++;
    ar_pose_after_step(c,&e);ARPG before=e;
    for(int i=0;i<=4;i++) {
        c->render_alpha=i*.25f;ar_view_prepare(c,&e);
        assert(fabsf(ar_view_keeper(c,&e).x-(old+i*.025f))<1e-5f);
        Vector2 badges[AR_MAX_PETS];ar_camera_project(c,1440,900);ar_pet_badges(c,&e,badges);
        assert(ar_pick_pet(&e,c,badges[0])==0);
        assert(!memcmp(&before,&e,sizeof(e)));
    }
    c->paused=1;c->render_alpha=0;ar_view_prepare(c,&e);assert(c->view_keeper.x==e.px);c->paused=0;
    ar_pose_before_step(c,&e);e.px+=20;e.tick++;ar_pose_after_step(c,&e);
    c->render_alpha=.25f;ar_view_prepare(c,&e);assert(c->view_keeper.x==e.px);
    // A rebase preserves global positions; a replaced body must snap instead.
    ARPose a={{14,2},7,1,0},b={{-1.9f,2},7,1,0};
    Vector2 at=ar_pose_blend(a,b,16,0,.5f);assert(fabsf(at.x+1.95f)<1e-5f);
    b.body=8;assert(ar_pose_blend(a,b,16,0,.5f).x==b.at.x);
    // External stepping and same-tick input changes still display current state.
    e.pets.x[0]+=3;ar_view_prepare(c,&e);assert(c->view_pets[0].x==e.pets.x[0]);
    c_reset(&e);ar_view_prepare(c,&e);assert(c->view_keeper.x==e.px);
    puf_close(&e);
}
static void check_ground(ARClient* c,ARPG* e) {
    int minx=c->ground_key[0],miny=c->ground_key[1],maxx=c->ground_key[2],maxy=c->ground_key[3];
    int vw=maxx-minx+1,tw=vw+1;
    for(int y=miny-1;y<=maxy;y++)for(int x=minx-1;x<=maxx;x++)
        assert(c->ground_tiles[(y-miny+1)*tw+x-minx+1]==ar_world_sample(e,x,y));
    for(int y=miny;y<=maxy;y++)for(int x=minx;x<=maxx;x++) {
        Color actual=c->ground_vertices[(y-miny)*vw+x-minx];
        Color expected=ar_ground_corner(c,e,x,y,&c->ground_tiles[(y-miny+1)*tw+x-minx+1],tw);
        assert(!memcmp(&actual,&expected,sizeof(Color)));
    }
}
static void test_ground(void) {
    ARPG e={0};float obs[AR_OBS_SIZE],actions[NUM_ATNS]={0},reward=0,terminal=0;
    setup(&e,obs,actions,&reward,&terminal);ar_world_begin(&e);ARClient* c=ar_client(&e);
    c->ground_vertices=(Color*)calloc(256*256,sizeof(Color));c->ground_tiles=(uint8_t*)calloc(256*256,1);
    assert(ar_ground_prepare(c,&e,-40,-40,100,100));check_ground(c,&e);
    assert(c->ground_colored==141*141);
    assert(ar_ground_prepare(c,&e,-39,-40,101,100));check_ground(c,&e);
    assert(c->ground_generated==142 && c->ground_colored==141);
    assert(ar_ground_prepare(c,&e,-39,-40,101,100));assert(!c->ground_generated && !c->ground_colored);
    assert(ar_ground_prepare(c,&e,-40,-40,100,100));assert(!c->ground_generated && !c->ground_colored);
    // A terrain edit dirties exactly its four shared vertex colors.
    e.dungeon[32*AR_DUN_W+32]=e.dungeon[32*AR_DUN_W+32]==AR_TILE_ROCK ? AR_TILE_GRASS : AR_TILE_ROCK;
    assert(ar_ground_prepare(c,&e,-40,-40,100,100));assert(c->ground_colored==4);check_ground(c,&e);
    // Streaming changes local indices, but retains the same world-space cache.
    ar_world_shift(&e,16,0);c->origin_x=((ARWorld*)e.campaign)->origin_x;
    assert(ar_ground_prepare(c,&e,-56,-40,84,100));assert(!c->ground_generated && !c->ground_colored);check_ground(c,&e);
    ar_world_shift(&e,-32,-16);c->origin_x=((ARWorld*)e.campaign)->origin_x;c->origin_y=((ARWorld*)e.campaign)->origin_y;
    assert(ar_ground_prepare(c,&e,-24,-24,116,116));check_ground(c,&e);
    // Edit and rebase in one tick: the changed tile leaves the live window,
    // but its cached corners must reflect the edit captured in the old chunk.
    e.dungeon[32*AR_DUN_W+8]=e.dungeon[32*AR_DUN_W+8]==AR_TILE_ROCK ? AR_TILE_GRASS : AR_TILE_ROCK;
    ar_world_shift(&e,16,0);c->origin_x=((ARWorld*)e.campaign)->origin_x;
    assert(ar_ground_prepare(c,&e,-40,-24,100,116));assert(c->ground_colored==4);check_ground(c,&e);
    // Eviction, negative coordinates and a large teleport must still match.
    assert(ar_ground_prepare(c,&e,-500,-450,-360,-310));check_ground(c,&e);
    assert(ar_ground_prepare(c,&e,-24,-24,116,116));check_ground(c,&e);
    e.dungeon[0]=e.dungeon[0]==AR_TILE_ROCK ? AR_TILE_GRASS : AR_TILE_ROCK;
    assert(ar_ground_prepare(c,&e,-24,-24,116,116));check_ground(c,&e);
    puf_close(&e);
}
static int cmp_ms(const void* a,const void* b) {
    double delta=*(const double*)a-*(const double*)b;return delta<0 ? -1 : delta>0;
}
static void bench(ARPG* e,const char* name,float zoom,int light,int atlas) {
    ARClient* c=ar_client(e);c->zoom=zoom;c->light_mode=light;c->map_open=atlas;c->camera_free=1;
    c->cam_x=c->cam_y=0;c->map_x=c->map_y=0;c->map_span=640;
    for(int i=0;i<60;i++)c_render(e);glFinish();
    double ms[120],ground=0,map=0;int generated=0,colored=0;
    for(int i=0;i<120;i++) {
        e->agents[0].actions[0]=i<60 ? 4 : 3;
        ar_pose_before_step(c,e);c_step(e);ar_pose_after_step(c,e);c->render_alpha=.5f;
        c->cam_x+=.075f;if(atlas)c->map_x+=1.5;
        ARPG before=*e;double start=GetTime();c_render(e);glFinish();ms[i]=(GetTime()-start)*1000;
        assert(!memcmp(&before,e,sizeof(*e)));assert(c->ground_colored<600);
        ground+=c->ground_prepare_ms;generated+=c->ground_generated;colored+=c->ground_colored;
        if(atlas)map+=c->map_prepare_ms;
    }
    qsort(ms,120,sizeof(double),cmp_ms);
    printf("Moving %s: median %.2f / p95 %.2f ms; terrain %.3f ms, %d samples / %d vertices over 120 frames; atlas %.3f ms\n",
        name,ms[60],ms[114],ground/120,generated,colored,map/120);
}
static void test_atlas(ARPG* e) {
    ARClient* c=ar_client(e);c->map_span=640;c->map_x=-513.75;c->map_y=-241.25;
    for(int frame=0;frame<250;frame++){assert(ar_map_prepare(c,e,256,182));if(!c->map_pending)break;}
    assert(!c->map_pending);
    assert(ar_map_prepare(c,e,256,182));assert(!c->map_generated);
    c->map_x+=.1;assert(ar_map_prepare(c,e,256,182));assert(!c->map_generated);
    c->map_x+=2.5;assert(ar_map_prepare(c,e,256,182));assert(c->map_generated<=185);
    double step=c->map_span/256;
    int x0=(int)floor(c->map_x/step-128),y0=(int)floor(c->map_y/step-91);
    ARWorld* w=(ARWorld*)e->campaign;
    for(int y=y0;y<y0+182;y++)for(int x=x0;x<x0+256;x++) {
        int i=ar_map_index(c,x,y);assert(c->map_samples[i].valid);
        double gx=(x+.5)*step,gy=(y+.5)*step;
        Color expected=ar_land_color(w->seed,(float)gx,(float)gy,ar_world_global_tile(e,w,(int)floor(gx),(int)floor(gy)));
        if(ar_world_chunk_id(w,(int)floor(gx/16),(int)floor(gy/16))<0)expected=ar_mix(expected,AR_INK,.57f);
        assert(!memcmp(c->map_pixels+i,&expected,sizeof(Color)));
    }
    c->map_span=320;assert(ar_map_prepare(c,e,256,182));assert(c->map_pending>0);
    // A resize has a different stride and must not retain old texture indices.
    assert(ar_map_prepare(c,e,256,300));assert(c->map_height>=364);
}
static void render_tests(ARPG* e,const char* dir) {
    float obs[AR_OBS_SIZE],actions[NUM_ATNS]={0},reward=0,terminal=0;
    setup(e,obs,actions,&reward,&terminal);ar_world_begin(e);SetTraceLogLevel(LOG_WARNING);c_render(e);SetTargetFPS(0);
    printf("Renderer: %s\n",glGetString(0x1F01));
    bench(e,"normal",1,0,0);bench(e,"wide",.55f,0,0);bench(e,"dusk",1,2,0);
    bench(e,"atlas drag",1,0,1);test_atlas(e);
    if(dir) {
        ARClient* c=ar_client(e);c->map_open=1;c->map_span=640;c->map_x=c->map_y=0;
        for(int i=0;i<120;i++){c_render(e);if(!c->map_pending)break;}
        char path[4096];snprintf(path,sizeof(path),"%s/motion-atlas.png",dir);
        Image screen=LoadImageFromScreen();assert(ExportImage(screen,path));UnloadImage(screen);
        c->map_open=0;c->zoom=.55f;c->light_mode=2;ar_pose_reset(c,e);c_render(e);
        snprintf(path,sizeof(path),"%s/motion-wide.png",dir);
        screen=LoadImageFromScreen();assert(ExportImage(screen,path));UnloadImage(screen);
    }
    puf_close(e);
}
int main(int argc,char** argv) {
    ARPG e={0};ARControls controls;ar_load_config(&e,&controls);
    test_budget();test_poses();test_ground();
    if(argc>1 && !strcmp(argv[1],"--render"))render_tests(&e,argc>2 ? argv[2] : NULL);
    puf_ini_free(&g_controls_ini);
    puts("ARPG presentation: bounded catch-up, interpolated poses/picking, incremental terrain and streaming: PASS");
    return 0;
}
