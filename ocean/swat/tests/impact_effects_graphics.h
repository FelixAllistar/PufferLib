#include "impact_effects_draw.h"

static Image impact_capture(SwatSoundLog* log,int tick,int cover) {
    Camera3D camera={{0,1,3},{0,1,0},{0,1,0},42,CAMERA_PERSPECTIVE};
    RenderTexture2D target=LoadRenderTexture(512,512);
    BeginTextureMode(target);ClearBackground(MAGENTA);BeginMode3D(camera);
    if(cover==1)DrawCube((Vector3){0,1,1},2,2,.1f,DARKBLUE);
    swat_impact_draw(log,tick,camera,NULL);
    if(cover==2) {
        // Farther geometry drawn afterward must pass through translucent cues;
        // ordinary geometry after the effects must still write its own depth.
        DrawCube((Vector3){0,1,-.5f},2,2,.1f,RED);
        DrawCube((Vector3){0,1,-1},2,2,.1f,BLUE);
    }
    EndMode3D();EndTextureMode();Image image=LoadImageFromTexture(target.texture);ImageFlipVertical(&image);
    UnloadRenderTexture(target);return image;
}
static int impact_pixel_difference(Image a,Image b) {
    Color* left=LoadImageColors(a);Color* right=LoadImageColors(b);int different=0;
    for(int i=0;i<a.width*a.height;i++)different+=memcmp(left+i,right+i,sizeof(Color))!=0;
    UnloadImageColors(left);UnloadImageColors(right);return different;
}
static void impact_effects_graphics(SwatView* view,const char* directory) {
    SwatSoundLog empty={0},log={0};char path[4096];
    int materials[]={SWAT_OPAQUE_GLASS,SWAT_BRICK,SWAT_WOOD};
    for(int i=0;i<3;i++) {
        log=(SwatSoundLog){0};swat_sound_surface(&log,100,0,SWAT_SOUND_BREAK,(b3Pos){0,1,0},1.2f,35,materials[i]);
        if(i==1)swat_sound_surface(&log,100,0,SWAT_SOUND_FLASH,(b3Pos){0,1,0},2.5f,80,SWAT_BRICK);
        Image baseline=impact_capture(&empty,110,0),visible=impact_capture(&log,110,0);
        int pixels=impact_pixel_difference(baseline,visible);assert(pixels>30);
        snprintf(path,sizeof(path),"%s/impact-material-%d.png",directory,materials[i]);assert(ExportImage(visible,path));
        Image expired=impact_capture(&log,160,0);assert(!impact_pixel_difference(baseline,expired));UnloadImage(expired);
        for(int cover=1;cover<=2;cover++) {
            Image expected=impact_capture(&empty,110,cover),actual=impact_capture(&log,110,cover);
            assert(!impact_pixel_difference(expected,actual));UnloadImage(expected);UnloadImage(actual);
        }
        UnloadImage(baseline);UnloadImage(visible);
        printf("PASS impact graphics: material %d, %d cue pixels, cover occlusion, expiry and restored depth writes\n",materials[i],pixels);
    }
    log=(SwatSoundLog){0};for(int i=0;i<128;i++)swat_sound_surface(&log,100,i,SWAT_SOUND_BREAK,(b3Pos){(i%3-1)*.25f,1+(i%4-2)*.25f,0},1.2f,35,SWAT_BRICK);
    Camera3D budget_camera={{0,1,3},{0,1,0},{0,1,0},42,CAMERA_PERSPECTIVE};
    RenderTexture2D budget_target=LoadRenderTexture(512,512);
    double elapsed=0;
    for(int i=0;i<210;i++) {
        BeginTextureMode(budget_target);ClearBackground(MAGENTA);BeginMode3D(budget_camera);
        double start=GetTime();swat_impact_draw(&log,110,budget_camera,NULL);
        if(i>=10)elapsed+=GetTime()-start;
        EndMode3D();EndTextureMode();
    }
    UnloadRenderTexture(budget_target);
    printf("Impact sampler + maximum-batch CPU submission mean: %.3f ms (200 iterations; GPU completion excluded)\n",1000*elapsed/200);
    // A shot through a real lobby pane provides authoritative material/events.
    SwatConfig cfg=swat_default_config();cfg.mission=SWAT_MOTEL;cfg.hostile_fire=false;cfg.randomize=false;cfg.squad_bots=0;
    swat_sim_init(&sim,cfg,81);
    const SwatMotelInstance* bay=swat_motel_instance(5);
    b3Pos target=b3OffsetPos(bay->origin,swat_v(cosf(bay->yaw)*1.31f,1.7f,-sinf(bay->yaw)*1.31f));
    b3Vec3 normal=swat_v(sinf(bay->yaw),0,cosf(bay->yaw));
    swat_sim_shoot(&sim,0,b3OffsetPos(target,swat_mul(normal,.09f)),swat_mul(normal,-1),(SwatShot){.fired=true,.damage=34,.range=.3f,.energy=6});
    assert(!sim.world.objects[SWAT_MOTEL_PANES_FIRST+1].active);
    SwatController* c=&sim.actors[0].controller;
    b3Pos feet=b3OffsetPos(target,swat_v(normal.x*2,-1.7f,normal.z*2));
    b3Body_SetTransform(c->body.body,b3OffsetPos(feet,swat_v(0,c->body.totalHeight*.5f,0)),b3Quat_identity);
    c->yaw=atan2f(-normal.z,-normal.x);c->pitch=0;sim.tick+=5;
    before=sim.world;SwatSoundLog sounds=sim.sounds;
    for(int i=0;i<2;i++){BeginDrawing();swat_view_draw(view,&sim,false,75);EndDrawing();}
    assert(!memcmp(&before,&sim.world,sizeof(before)) && !memcmp(&sounds,&sim.sounds,sizeof(sounds)));
    snprintf(path,sizeof(path),"%s/impact-lobby-native.png",directory);Image scene=LoadImageFromScreen();assert(ExportImage(scene,path));UnloadImage(scene);
    swat_sim_close(&sim);
}
