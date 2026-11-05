// One-officer animation review: renders a contact sheet of the live gameplay
// pose across aim pitch, stance and movement, and prints hand-on-gun metrics.
// Presentation only; reads the same runtime path the game draws.
//   officer_review ASSET_DIR OUT.png
#include "character_runtime.h"
#include "aim_tuning_io.h"
#include "raymath.h"
#include "rlgl.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static const float rifle_joint[16]={0,-1,0,0, 0,0,1,0, -1,0,0,0, .0012969123f,.4499999881f,.0226502232f,1};
static Matrix fm(const float* f) {
    return (Matrix){f[0],f[4],f[8],f[12],f[1],f[5],f[9],f[13],f[2],f[6],f[10],f[14],f[3],f[7],f[11],f[15]};
}
static Matrix compose(Matrix a,Matrix b) { return MatrixMultiply(b,a); }
static Vector3 org(Matrix m) { return (Vector3){m.m12,m.m13,m.m14}; }
static Matrix cached(const SwatCharacterActorPose* c,SwatCharacterAsset* a,const char* name) {
    int i=swat_character_find_node(a,name); assert(i>=0); return fm(c->matrices+i*16);
}
static void empty(const SwatSim* sim,bool cutaway) { (void)sim; (void)cutaway; }
#define TILE 320
static Image frame(SwatCharacterView* view,SwatLighting* light,Camera3D camera,Matrix root) {
    RenderTexture2D target=LoadRenderTexture(TILE,TILE); assert(target.id);
    BeginTextureMode(target); ClearBackground((Color){40,44,52,255}); BeginMode3D(camera);
    SwatEnvironmentArt art={0}; SwatWorld world={0};
    swat_lighting_begin(light,&art,&world,camera.position);
    swat_character_view_draw(view,light,root);
    swat_lighting_end(light,&art);
    DrawGrid(10,.5f);
    EndMode3D(); EndTextureMode();
    Image result=LoadImageFromTexture(target.texture); ImageFlipVertical(&result); UnloadRenderTexture(target); return result;
}

int main(int argc,char** argv) {
    if(argc<3) { fprintf(stderr,"usage: %s ASSET_DIR OUT.png\n",argv[0]); return 2; }
    setenv("SWAT_CHARACTER_ASSETS",argv[1],1);
    SetTraceLogLevel(LOG_WARNING);
    SetConfigFlags(FLAG_WINDOW_HIDDEN); InitWindow(TILE,TILE,"officer review"); assert(IsWindowReady());
    SwatLighting light={0}; swat_lighting_init(&light);
    static SwatSim lsim; swat_lighting_prepare(&light,&lsim,(Vector3){3,1.7f,3},false,empty);
    SwatWeaponArt weapons={0}; swat_weapon_art_init(&weapons);
    SwatCharacterRuntime* runtime=swat_character_runtime_open(&weapons); assert(runtime);
    if(getenv("SWAT_AIM_TUNING")) printf("loaded %d tuning values\n",aim_tuning_load(&swat_aim_tuning,getenv("SWAT_AIM_TUNING")));

    // Canonical grip: hands in the rifle frame on the Ready clip's first key.
    SwatCharacterAsset* ready=runtime->banks[SWAT_CHARACTER_READY].asset;
    assert(swat_character_view_sample(&runtime->banks[SWAT_CHARACTER_READY],"Standing Empty / Shared Ready N Carry F",0));
    Matrix gun0=compose(fm(swat_character_node_matrix(ready,swat_character_find_node(ready,"Prop_Rifle"))),fm(rifle_joint));
    Matrix inv0=MatrixInvert(gun0); Vector3 grip[2];
    const char* hands[2]={"mixamorig:LeftHand","mixamorig:RightHand"};
    const char* arms[2]={"mixamorig:LeftArm","mixamorig:RightArm"};
    const char* fores[2]={"mixamorig:LeftForeArm","mixamorig:RightForeArm"};
    for(int h=0;h<2;h++) grip[h]=Vector3Transform(org(fm(swat_character_node_matrix(ready,swat_character_find_node(ready,hands[h])))),inv0);
    {
        #define N(n) org(fm(swat_character_node_matrix(ready,swat_character_find_node(ready,n))))
        Vector3 up=Vector3Normalize(Vector3Subtract(N("mixamorig:Neck"),N("mixamorig:Spine2")));
        Vector3 right=Vector3Normalize(Vector3Subtract(N("mixamorig:RightArm"),N("mixamorig:LeftArm")));
        Vector3 fwd=Vector3Normalize(Vector3CrossProduct(up,right)); right=Vector3CrossProduct(fwd,up);
        Vector3 o=Vector3Subtract(org(gun0),N("mixamorig:Spine2")),gf={gun0.m0,gun0.m1,gun0.m2},gu={gun0.m4,gun0.m5,gun0.m6};
        Vector3 rs=Vector3Subtract(N("mixamorig:RightArm"),N("mixamorig:Spine2")),hd=Vector3Subtract(N("mixamorig:Head"),N("mixamorig:Spine2"));
        printf("CANON chest-frame (fwd,up,right) cm: gun_origin %.1f %.1f %.1f | rshoulder %.1f %.1f %.1f | head %.1f %.1f %.1f\n",
            100*Vector3DotProduct(o,fwd),100*Vector3DotProduct(o,up),100*Vector3DotProduct(o,right),
            100*Vector3DotProduct(rs,fwd),100*Vector3DotProduct(rs,up),100*Vector3DotProduct(rs,right),
            100*Vector3DotProduct(hd,fwd),100*Vector3DotProduct(hd,up),100*Vector3DotProduct(hd,right));
        printf("CANON gun x-axis in chest: %.3f %.3f %.3f  y-axis: %.3f %.3f %.3f\n",Vector3DotProduct(gf,fwd),Vector3DotProduct(gf,up),Vector3DotProduct(gf,right),Vector3DotProduct(gu,fwd),Vector3DotProduct(gu,up),Vector3DotProduct(gu,right));
        for(int h=0;h<2;h++) printf("CANON grip %d in gun frame cm: %.1f %.1f %.1f\n",h,100*grip[h].x,100*grip[h].y,100*grip[h].z);
    }

    SwatSim* sim=calloc(1,sizeof(*sim)); assert(sim);
    SwatConfig config=swat_default_config(); config.hostile_fire=false; config.mission=SWAT_ANNEX; swat_sim_init(sim,config,42);
    SwatActor* actor=&sim->actors[0];
    const float pitches[]={-60,-30,0,30,60};
    bool walk=getenv("REVIEW_WALK")!=NULL; int COLS=walk ? 8 : 5;
    const char* rows[]={"stand aim","walk aim","crouch aim","crouch walk","strafe L aim","low ready"};
    enum {ROWS=6};
    Image sheet=GenImageColor(COLS*TILE,ROWS*2*TILE,BLACK);
    float worst[2]={0,0},sum[2]={0,0}; int straight=0,samples=0;
    printf("%-12s %6s %8s %8s %6s %6s\n","row","pitch","Lgrip_cm","Rgrip_cm","Lreach","Rreach");
    for(int r=0;r<ROWS;r++) for(int c=0;c<COLS;c++) {
        if(walk && !(r==1||r==3||r==4)) continue;
        bool crouch=r==2||r==3; b3Vec3 v={0,0,0};
        if(r==1||r==3) v=(b3Vec3){1,0,0}; if(r==4) v=(b3Vec3){0,0,-1};
        swat_body_set_crouch(&actor->controller.body,crouch); actor->controller.body.onGround=true;
        actor->controller.eye_height=actor->controller.body.totalHeight-.2032f;
        actor->controller.yaw=0; actor->controller.pitch=walk ? 0 : pitches[c]*SWAT_RAD;
        actor->controller.ads=0; actor->controller.ready_blend=r==5 ? 1 : 0;
        actor->controller.recoil_pitch=actor->controller.recoil_yaw=actor->controller.lean=0;
        b3Body_SetLinearVelocity(actor->controller.body.body,v);
        runtime->actors[0].valid=false; sim->tick++; swat_character_runtime_prepare(runtime,sim);
        runtime->actors[0].phase=walk ? c/8.0 : .3; sim->tick+=14; swat_character_runtime_prepare(runtime,sim);
        SwatCharacterActorPose* cache=&runtime->actors[0]; assert(cache->valid);
        SwatCharacterView* view=&runtime->banks[cache->bank]; SwatCharacterAsset* a=view->asset;
        Matrix gun=compose(cached(cache,a,"Prop_Rifle"),fm(rifle_joint)),inv=MatrixInvert(gun);
        float err[2],reach[2];
        for(int h=0;h<2;h++) {
            Vector3 hand=org(cached(cache,a,hands[h])),sh=org(cached(cache,a,arms[h])),el=org(cached(cache,a,fores[h]));
            err[h]=100*Vector3Distance(Vector3Transform(hand,inv),grip[h]);
            reach[h]=Vector3Distance(sh,hand)/(Vector3Distance(sh,el)+Vector3Distance(el,hand));
            worst[h]=fmaxf(worst[h],err[h]); sum[h]+=err[h]; straight+=reach[h]>.995f;
        }
        samples++;
        SwatPose pose=swat_pose(&actor->controller,&actor->arsenal);
        Vector3 eye={pose.eye.x,pose.eye.y,pose.eye.z},stock={pose.shoulder.x,pose.shoulder.y,pose.shoulder.z};
        Vector3 head=Vector3Transform(org(cached(cache,a,"mixamorig:Head")),cache->root);
        Vector3 rsh=Vector3Transform(org(gun),cache->root);
        if(!walk) printf("%-12s %6.0f %8.1f %8.1f %6.3f %6.3f  head-eye %+5.1f,%+5.1f,%+5.1f  gun-auth %+5.1f,%+5.1f,%+5.1f\n",rows[r],pitches[c],err[0],err[1],reach[0],reach[1],
            100*(head.x-eye.x),100*(head.y-eye.y),100*(head.z-eye.z),100*(rsh.x-stock.x),100*(rsh.y-stock.y),100*(rsh.z-stock.z));
        assert(swat_character_restore_pose(a,cache->matrices,cache->count));
        memcpy(view->visible,cache->visible,(size_t)view->model.meshCount);
        Vector3 feet={cache->feet.x,cache->feet.y,cache->feet.z};
        float h=crouch ? .75f : 1.05f;
        // Controller yaw 0 faces +X. Side view from the officer's right (+Z),
        // three-quarter front view from the left.
        float zoom=getenv("REVIEW_CLOSE") ? .45f : 1;
        if(zoom<1) h+=.35f;
        Camera3D side={Vector3Add(feet,(Vector3){.3f,h+.1f,3.0f*zoom}),Vector3Add(feet,(Vector3){.3f,h,0}),{0,1,0},40,CAMERA_PERSPECTIVE};
        Camera3D front={Vector3Add(feet,(Vector3){2.2f*zoom+.2f*(1-zoom),h+.35f*zoom,-1.8f*zoom}),Vector3Add(feet,(Vector3){.2f,h,0}),{0,1,0},40,CAMERA_PERSPECTIVE};
        for(int k=0;k<2;k++) {
            Image tile=frame(view,&light,k ? front : side,cache->root);
            ImageDrawText(&tile,walk ? TextFormat("%s  phase %.3f%s",rows[r],c/8.0,k?"  (front)":"") : TextFormat("%s  pitch %+.0f%s",rows[r],pitches[c],k?"  (front)":""),6,6,14,RAYWHITE);
            ImageDrawText(&tile,TextFormat("grip err L %.1fcm R %.1fcm",err[0],err[1]),6,TILE-20,14,err[0]>3||err[1]>3 ? (Color){255,120,100,255} : (Color){150,230,150,255});
            ImageDraw(&sheet,tile,(Rectangle){0,0,TILE,TILE},(Rectangle){c*TILE,(r*2+k)*TILE,TILE,TILE},WHITE);
            UnloadImage(tile);
        }
    }
    printf("SUMMARY grip_err_cm L mean %.2f max %.2f | R mean %.2f max %.2f | fully-straight arms %d/%d\n",
        sum[0]/samples,worst[0],sum[1]/samples,worst[1],straight,samples*2);
    bool ok=ExportImage(sheet,argv[2]); UnloadImage(sheet);
    swat_character_runtime_close(runtime); swat_weapon_art_close(&weapons); swat_lighting_close(&light); CloseWindow();
    return ok ? 0 : 1;
}
