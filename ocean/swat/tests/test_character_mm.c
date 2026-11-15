// Motion matching through the live character runtimes (character_mm.h):
// officer, armed suspect and civilian bodies follow a scripted capsule path
// (walk, stop, 90 degree turn, strafe, crouch walk, crouch idle) set only on
// replicated actor state, with the synthetic database anim/build/test.mmdb.
// Checks finite poses, bone lengths, locked feet on the ground, the look-at
// toward a scripted officer within its limits, the handover to the clip
// system and back, and bit-identical results on two runs.
//   test_character_mm anim/build/test.mmdb ocean/swat/assets/characters
#include "character_runtime.h"
#include "character_mm.h"
#include "raymath.h"
#include "rlgl.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static Matrix matrix(const float* f) {
    return (Matrix){f[0],f[4],f[8],f[12],f[1],f[5],f[9],f[13],f[2],f[6],f[10],f[14],f[3],f[7],f[11],f[15]};
}
static Vector3 joint(const float* matrices,int node,Matrix root) {
    const float* m=matrices+node*16; return Vector3Transform((Vector3){m[12],m[13],m[14]},root);
}
static uint64_t fnv(uint64_t h,const void* bytes,size_t n) {
    const unsigned char* p=bytes; for(size_t i=0;i<n;i++) h=(h^p[i])*UINT64_C(1099511628211); return h;
}
static bool finite_pose(const float* m,size_t count) { for(size_t i=0;i<count;i++) if(!isfinite(m[i])) return false; return true; }
static float angle_between(float a,float b) { return fabsf(swat_angle(a-b)); }
static void place(SwatActor* a,b3Pos feet,float yaw,bool crouch,b3Vec3 velocity) {
    swat_body_set_crouch(&a->controller.body,crouch); a->controller.body.onGround=true;
    a->controller.eye_height=a->controller.body.totalHeight-.2032f; a->controller.yaw=yaw; a->controller.pitch=0;
    b3Pos at=b3Body_GetPosition(a->controller.body.body),now=swat_body_feet_position(&a->controller.body);
    at.x+=feet.x-now.x; at.y+=feet.y-now.y; at.z+=feet.z-now.z;
    b3Body_SetTransform(a->controller.body.body,at,b3Quat_identity); b3Body_SetLinearVelocity(a->controller.body.body,velocity);
}
// Scripted path, 60 Hz: idle, walk 1.4 m/s, stop, turn 90 degrees left in
// place, walk the new heading, strafe right, crouch walk, crouch idle.
typedef struct { b3Pos feet; float yaw; bool crouch; b3Vec3 velocity; const char* phase; } Step;
#define PATH_TICKS 1140
static void path(Step* s,b3Pos start,float start_yaw) {
    b3Pos p=start; float yaw=start_yaw;
    for(int t=0;t<PATH_TICKS;t++) {
        float speed=0,side=0; bool crouch=false; const char* phase="idle";
        if(t>=60 && t<240) { speed=1.4f; phase="walk"; }
        else if(t>=240 && t<330) phase="stop";
        else if(t>=330 && t<390) { yaw=start_yaw+SWAT_PI*.5f*(float)(t-330+1)/60; phase="turn"; }
        else if(t>=390 && t<540) { speed=1.4f; phase="walk turned"; }
        else if(t>=540 && t<720) { side=1.0f; phase="strafe"; }
        else if(t>=720 && t<930) { speed=.9f; crouch=true; phase="crouch walk"; }
        else if(t>=930) { crouch=true; phase="crouch idle"; }
        b3Vec3 f=swat_v(cosf(yaw),0,sinf(yaw)),r=swat_v(-sinf(yaw),0,cosf(yaw));
        b3Vec3 v=swat_add(swat_mul(f,speed),swat_mul(r,side));
        p.x+=v.x/60; p.z+=v.z/60;
        s[t]=(Step){p,yaw,crouch,v,phase};
    }
}
typedef struct {
    uint64_t hash;
    int officer_frames,civilian_frames,suspect_frames,locked_feet;
    float bone_error[3],foot_drift[3],foot_height[3],look_error,officer_head; // [officer, suspect, civilian]
} Result;
// A locked foot is on the ground when its heel (ankle) or toe is within
// 3 cm of its rest height above the floor; a locked ankle must not slide.
static void check_feet(const SwatMMBody* mm,const float* matrices,Matrix root,const int foot[2],const int toe[2],float ground,float ankle,float toe_rest,Result* out,int k) {
    for(int s=0;s<2;s++) {
        const mm_foot_lock* lock=&mm->controller.feet[s]; if(!lock->locked) continue;
        Vector3 a=joint(matrices,foot[s],root),t=joint(matrices,toe[s],root);
        float drift=hypotf(a.x-lock->lock_pos.x,a.z-lock->lock_pos.z),height=fminf(fabsf(a.y-ground-ankle),fabsf(t.y-ground-toe_rest));
        out->foot_drift[k]=fmaxf(out->foot_drift[k],drift); out->foot_height[k]=fmaxf(out->foot_height[k],height); out->locked_feet++;
        if(getenv("MM_DEBUG") && height>.03f) printf("    body %d side %d ankle %.3f ground %.3f rest %.3f\n",k,s,a.y,ground,ankle);
    }
}
// Rest ankle height of the body's left foot above its floor (scene y 0).
static float rest_ankle(SwatCharacterAsset* asset,int foot) {
    assert(swat_character_sample_pose(asset,NULL,0)); return swat_character_node_matrix(asset,foot)[13];
}
// Optional previews (SWAT_MM_CAPTURES=directory): the drawn body from a
// three-quarter camera that follows it, on a floor grid.
static SwatLighting light;
static void capture_view(Image* sheet,int slot,Vector3 feet,float yaw,void (*draw)(void*,int),void* runtime,int actor) {
    Vector3 side={cosf(yaw+1.0f)*3.2f,1.4f,sinf(yaw+1.0f)*3.2f};
    Camera3D camera={Vector3Add(feet,side),Vector3Add(feet,(Vector3){0,.85f,0}),{0,1,0},40,CAMERA_PERSPECTIVE};
    RenderTexture2D target=LoadRenderTexture(512,512); assert(target.id);
    BeginTextureMode(target); ClearBackground((Color){52,56,62,255}); BeginMode3D(camera);
    rlPushMatrix(); rlTranslatef(roundf(feet.x),feet.y,roundf(feet.z)); DrawGrid(12,.5f); rlPopMatrix();
    SwatEnvironmentArt art={0}; SwatWorld world={0};
    swat_lighting_begin(&light,&art,&world,camera.position); draw(runtime,actor); swat_lighting_end(&light,&art);
    EndMode3D(); EndTextureMode();
    Image image=LoadImageFromTexture(target.texture); ImageFlipVertical(&image); UnloadRenderTexture(target);
    ImageDraw(sheet,image,(Rectangle){0,0,512,512},(Rectangle){(float)(512*slot),0,512,512},WHITE); UnloadImage(image);
}
static void draw_officer(void* r,int i) { assert(swat_character_runtime_draw(r,i,&light)); }
static void draw_person(void* r,int i) { assert(swat_civilian_runtime_draw(r,i,&light)); }
static Result run(const char* characters,bool verbose) {
    Result out={0}; out.hash=UINT64_C(14695981039346656037);
    SwatWeaponArt weapons={0}; swat_weapon_art_init(&weapons);
    SwatCharacterRuntime* officers=swat_character_runtime_open(&weapons); assert(officers);
    SwatCivilianRuntime* people=swat_civilian_runtime_open(); assert(people);
    const mm_db* db=swat_mm_db(); assert(db);
    SwatSim* sim=calloc(1,sizeof(*sim)); assert(sim);
    SwatConfig config=swat_default_config(); config.hostile_fire=false; config.mission=SWAT_ANNEX; swat_sim_init(sim,config,42);
    int officer=-1,suspect=-1,civilian=-1;
    for(int i=0;i<sim->actor_count;i++) {
        SwatRole r=sim->actors[i].role;
        if(r==SWAT_OFFICER && officer<0) officer=i; else if(r==SWAT_SUSPECT && suspect<0) suspect=i; else if(r==SWAT_CIVILIAN && civilian<0) civilian=i;
    }
    assert(officer>=0 && suspect>=0 && civilian>=0);
    // Paths far apart (no look-at while walking) on open ground is not needed:
    // presentation never collides; only the look-at phase needs sight.
    static Step steps[3][PATH_TICKS];
    const int who[3]={officer,suspect,civilian};
    path(steps[0],(b3Pos){0,.01,0},0); path(steps[1],(b3Pos){40,.01,30},SWAT_PI*.5f); path(steps[2],(b3Pos){-40,.01,30},SWAT_PI);
    SwatCharacterAsset* officer_asset=officers->banks[0].asset;
    int officer_feet[2]={swat_character_find_node(officer_asset,"mixamorig:LeftFoot"),swat_character_find_node(officer_asset,"mixamorig:RightFoot")};
    int officer_toes[2]={swat_character_find_node(officer_asset,"mixamorig:LeftToeBase"),swat_character_find_node(officer_asset,"mixamorig:RightToeBase")};
    float officer_ankle=rest_ankle(officer_asset,officer_feet[0]),officer_toe=rest_ankle(officer_asset,officer_toes[0]);
    SwatCharacterAsset* civ_asset=people->view.asset;
    int civ_feet[2]={swat_character_find_node(civ_asset,"Foot_L"),swat_character_find_node(civ_asset,"Foot_R")};
    int civ_toes[2]={swat_character_find_node(civ_asset,"Toe_L"),swat_character_find_node(civ_asset,"Toe_R")};
    float civ_ankle=rest_ankle(civ_asset,civ_feet[0]),civ_toe=rest_ankle(civ_asset,civ_toes[0]);
    // Rest bone lengths of the civilian rig (mapped joints keep them).
    int civ_nodes=swat_character_info(civ_asset).nodes; float* civ_rest=calloc((size_t)civ_nodes,sizeof(float)); assert(civ_rest);
    for(int n=0;n<civ_nodes;n++) {
        int p=swat_character_parent_node(civ_asset,n); if(p<0) continue;
        const float* a=swat_character_node_matrix(civ_asset,n),*b=swat_character_node_matrix(civ_asset,p);
        civ_rest[n]=Vector3Distance((Vector3){a[12],a[13],a[14]},(Vector3){b[12],b[13],b[14]});
    }
    for(int t=0;t<PATH_TICKS;t++) {
        sim->tick++;
        for(int k=0;k<3;k++) place(&sim->actors[who[k]],steps[k][t].feet,steps[k][t].yaw,steps[k][t].crouch,steps[k][t].velocity);
        swat_character_runtime_prepare(officers,sim); swat_civilian_runtime_prepare(people,sim,sim->tick/60.0);
        // Officer: matched base pose under the aim layer.
        SwatCharacterActorPose* o=&officers->actors[officer];
        assert(o->valid && o->motion && o->bank==SWAT_CHARACTER_READY && finite_pose(o->matrices,o->count));
        for(size_t n=0;n<o->count/16;n++) {
            const char* name=swat_character_node_name(officer_asset,(int)n); int p=swat_character_parent_node(officer_asset,(int)n);
            if(!name || strncmp(name,"mixamorig:",10) || p<0 || strncmp(swat_character_node_name(officer_asset,p),"mixamorig:",10)) continue;
            float source=Vector3Distance(joint(o->first_person_matrices,(int)n,MatrixIdentity()),joint(o->first_person_matrices,p,MatrixIdentity()));
            float drawn=Vector3Distance(joint(o->matrices,(int)n,MatrixIdentity()),joint(o->matrices,p,MatrixIdentity()));
            out.bone_error[0]=fmaxf(out.bone_error[0],fabsf(source-drawn));
        }
        check_feet(o->mm,o->matrices,o->root,officer_feet,officer_toes,(float)o->feet.y,officer_ankle,officer_toe,&out,0);
        {
            // Officer head follows the aim (the existing aim layer): its
            // skull's forward axis stays inside the look limits of the facing.
            Vector3 head,forward; assert(swat_mm_head_direction(officers->mm_rig,officer_asset,o->matrices,o->root,&head,&forward));
            float yaw=atan2f(forward.z,forward.x),pitch=asinf(Clamp(forward.y,-1,1));
            out.officer_head=fmaxf(out.officer_head,angle_between(yaw,steps[0][t].yaw));
            assert(angle_between(yaw,steps[0][t].yaw)<=SWAT_LOOK_YAW_LIMIT && fabsf(pitch)<=SWAT_LOOK_PITCH_LIMIT);
            out.officer_frames++;
        }
        out.hash=fnv(out.hash,o->matrices,o->count*sizeof(float));
        // Suspect and civilian on the BC_CIV01 rig.
        for(int k=1;k<3;k++) {
            SwatCivilianPose* p=&people->actors[who[k]];
            assert(p->valid && p->motion && p->body<0 && finite_pose(p->matrices,p->count));
            if(k==1) { assert(p->armed); out.suspect_frames++; } else out.civilian_frames++;
            for(int n=0;n<civ_nodes;n++) {
                int q=swat_character_parent_node(civ_asset,n); if(q<0 || civ_rest[n]<1e-4f) continue;
                const char* name=swat_character_node_name(civ_asset,n);
                if(!name || !strcmp(name,"Root") || !strcmp(name,"Hips")) continue;
                float drawn=Vector3Distance(joint(p->matrices,n,MatrixIdentity()),joint(p->matrices,q,MatrixIdentity()));
                if(getenv("MM_DEBUG") && fabsf(drawn-civ_rest[n])>1e-4f) printf("    bone %s %.5f\n",name,drawn-civ_rest[n]);
                out.bone_error[k]=fmaxf(out.bone_error[k],fabsf(drawn-civ_rest[n]));
            }
            check_feet(p->mm,p->matrices,p->root,civ_feet,civ_toes,(float)p->feet.y,civ_ankle,civ_toe,&out,k);
            out.hash=fnv(out.hash,p->matrices,p->count*sizeof(float));
        }
        const char* captures=getenv("SWAT_MM_CAPTURES");
        if(verbose && captures && (t==170 || t==280 || t==470 || t==620 || t==850 || t==1000)) {
            Image sheet=GenImageColor(1024,512,BLACK);
            SwatCivilianPose* c=&people->actors[civilian];
            capture_view(&sheet,0,(Vector3){(float)o->feet.x,(float)o->feet.y,(float)o->feet.z},steps[0][t].yaw,draw_officer,officers,officer);
            capture_view(&sheet,1,(Vector3){(float)c->feet.x,(float)c->feet.y,(float)c->feet.z},steps[2][t].yaw,draw_person,people,civilian);
            char name[32]; snprintf(name,sizeof(name),"%s",steps[0][t].phase); for(char* c=name;*c;c++) if(*c==' ') *c='-';
            assert(ExportImage(sheet,TextFormat("%s/mm-%04d-%s.png",captures,t,name)));
            UnloadImage(sheet);
        }
        if(verbose && t%60==0) printf("  t=%4d %-12s officer frame %5d civilian frame %5d suspect frame %5d\n",t,steps[0][t].phase,
            o->mm->controller.frame,people->actors[civilian].mm->controller.frame,people->actors[suspect].mm->controller.frame);
    }
    // Look-at, on the annex's open staging floor: the civilian stands facing
    // +X; the officer stands at 50 degrees to its right, 3.5 m away, in
    // sight; then behind it at 150 degrees (clamped to 70); then 30 m away
    // (out of range: released).
    {
        SwatActor* c=&sim->actors[civilian],*of=&sim->actors[officer],*su=&sim->actors[suspect];
        b3Pos civ_feet_at={2,.01,-1};
        place(su,(b3Pos){60,.01,60},0,false,b3Vec3_zero);
        const float bearings[3]={50*SWAT_RAD,150*SWAT_RAD,0};
        const float distances[3]={3.5f,3,30};
        for(int phase=0;phase<3;phase++) {
            float facing=0,bearing=facing+bearings[phase];
            b3Pos officer_at={civ_feet_at.x+distances[phase]*cosf(bearing),.01,civ_feet_at.z+distances[phase]*sinf(bearing)};
            float officer_yaw=bearing+SWAT_PI;
            for(int t=0;t<90;t++) {
                sim->tick++;
                place(c,civ_feet_at,facing,false,b3Vec3_zero); place(of,officer_at,officer_yaw,false,b3Vec3_zero);
                place(su,(b3Pos){60,.01,60},0,false,b3Vec3_zero);
                swat_character_runtime_prepare(officers,sim); swat_civilian_runtime_prepare(people,sim,sim->tick/60.0);
                SwatCivilianPose* p=&people->actors[civilian]; assert(p->valid && finite_pose(p->matrices,p->count));
                out.hash=fnv(out.hash,p->matrices,p->count*sizeof(float));
            }
            SwatCivilianPose* p=&people->actors[civilian]; const SwatMMLook* look=&p->mm->look;
            Vector3 head,forward; assert(swat_mm_head_direction(people->mm_rig,civ_asset,p->matrices,p->root,&head,&forward));
            b3Pos eye=swat_controller_eye(&of->controller);
            float head_yaw=atan2f(forward.z,forward.x),want=atan2f((float)eye.z-head.z,(float)eye.x-head.x);
            float relative=swat_angle(head_yaw-facing);
            if(verbose) printf("  look phase %d: target %d weight %.3f head yaw %.2f deg from facing, officer at %.2f deg, error %.2f deg\n",phase,look->target,look->weight,
                relative/SWAT_RAD,swat_angle(want-facing)/SWAT_RAD,angle_between(head_yaw,want)/SWAT_RAD);
            if(phase<2) assert(swat_world_sight_clear(&sim->world,swat_controller_eye(&c->controller),eye));
            if(phase==0) {
                assert(look->target==officer && look->weight>.99f);
                out.look_error=angle_between(head_yaw,want)/SWAT_RAD; assert(out.look_error<5);
            } else if(phase==1) {
                assert(look->target==officer && fabsf(look->yaw-SWAT_LOOK_YAW_LIMIT)<1e-4f);
                assert(fabsf(relative)<=SWAT_LOOK_YAW_LIMIT+5*SWAT_RAD && fabsf(relative)>SWAT_LOOK_YAW_LIMIT-5*SWAT_RAD);
            } else {
                assert(look->target<0 && look->weight<.01f);
            }
        }
        // Clip system handover: surrender (authored kneel) and a reload, then
        // back to matching; every frame valid, the hips continuous.
        SwatCivilianPose* p=&people->actors[civilian]; int hips=swat_character_find_node(civ_asset,"Hips");
        Vector3 last=joint(p->matrices,hips,p->root); float step=0; bool clip=false,back=false;
        SwatWeapon* rifle=&of->arsenal.slots[0]; rifle->magazine=0; rifle->chambered=false;
        SwatInput input=swat_neutral_input(); input.reload=true; swat_weapons_step(&of->arsenal,&input,0,0,true,false); input.reload=false;
        int matched_again=-1;
        for(int t=0;t<900;t++) {
            sim->tick++;
            c->gear.surrendered=t<240;
            if(rifle->reload_remaining>0) swat_weapons_step(&of->arsenal,&input,0,0,true,false);
            place(c,civ_feet_at,0,false,b3Vec3_zero);
            swat_character_runtime_prepare(officers,sim); swat_civilian_runtime_prepare(people,sim,sim->tick/60.0);
            SwatCharacterActorPose* o=&officers->actors[officer];
            assert(o->valid && finite_pose(o->matrices,o->count) && o->motion==(rifle->reload_remaining<=0));
            assert(p->valid && finite_pose(p->matrices,p->count));
            clip=clip || !p->motion; if(clip && p->motion && !back) matched_again=t; back=back || (clip && p->motion);
            Vector3 h=joint(p->matrices,hips,p->root); step=fmaxf(step,Vector3Distance(h,last));
            if(getenv("MM_DEBUG") && Vector3Distance(h,last)>.03f) printf("    t=%d motion=%d loop=%d transition=%d step=%.3f hips=%.3f %.3f %.3f\n",t,p->motion,p->loop,p->transition,Vector3Distance(h,last),h.x,h.y,h.z);
            last=h;
            out.hash=fnv(out.hash,o->matrices,o->count*sizeof(float)); out.hash=fnv(out.hash,p->matrices,p->count*sizeof(float));
        }
        assert(clip && back);
        if(verbose) printf("  clip handover: surrender and reload on clips, matching again after %d ticks, max hips step %.4f m per tick\n",matched_again,step);
        assert(step<.1f);
    }
    free(civ_rest);
    swat_sim_close(sim); free(sim);
    swat_civilian_runtime_close(people); swat_character_runtime_close(officers); swat_weapon_art_close(&weapons);
    (void)characters; return out;
}
static void no_cutaway(const SwatSim* sim,bool cutaway) { (void)sim; (void)cutaway; }
int main(int argc,char** argv) {
    assert(argc==3);
#ifdef _WIN32
    assert(!_putenv_s("SWAT_MM_DB",argv[1]) && !_putenv_s("SWAT_CHARACTER_ASSETS",argv[2]));
#else
    assert(!setenv("SWAT_MM_DB",argv[1],1) && !setenv("SWAT_CHARACTER_ASSETS",argv[2],1));
#endif
    SetTraceLogLevel(LOG_WARNING);
    SetConfigFlags(FLAG_WINDOW_HIDDEN); InitWindow(64,64,"motion matching"); assert(IsWindowReady());
    if(getenv("SWAT_MM_CAPTURES")) {
        swat_lighting_init(&light); static SwatSim empty_sim;
        swat_lighting_prepare(&light,&empty_sim,(Vector3){3,1.7f,3},false,no_cutaway);
    }
    Result a=run(argv[2],true),b=run(argv[2],false);
    printf("officer %d, suspect %d, civilian %d matched frames; %d locked foot-frames\n",a.officer_frames,a.suspect_frames,a.civilian_frames,a.locked_feet);
    const char* names[3]={"officer","suspect","civilian"};
    for(int k=0;k<3;k++) printf("  %-8s locked foot drift %.4f m, ankle height error %.4f m, bone length error %.6f m\n",names[k],a.foot_drift[k],a.foot_height[k],a.bone_error[k]);
    printf("look-at error %.2f deg; officer head at most %.1f deg from the facing; hashes %016llx %016llx\n",a.look_error,a.officer_head/SWAT_RAD,(unsigned long long)a.hash,(unsigned long long)b.hash);
    assert(a.locked_feet>1000);
    for(int k=0;k<3;k++) assert(a.foot_drift[k]<.03f && a.foot_height[k]<.03f && a.bone_error[k]<1e-4f);
    assert(a.hash==b.hash);
    printf("PASS motion matching: officer, armed suspect and civilian on the scripted path, locked feet, bone lengths, look-at, clip handover, deterministic\n");
    if(getenv("SWAT_MM_CAPTURES")) swat_lighting_close(&light);
    swat_mm_shutdown(); CloseWindow(); return 0;
}
