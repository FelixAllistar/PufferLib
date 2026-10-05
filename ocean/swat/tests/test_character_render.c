// Explicit display test: full-weight GPU vs independent CPU-deformed meshes,
// corrected gameplay poses, camera reuse and authoritative reload boundaries.
#include "character_runtime.h"
#include "raymath.h"
#include "rlgl.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
static Vector3 point(const float* m,Vector3 p) {
    return (Vector3){m[0]*p.x+m[4]*p.y+m[8]*p.z+m[12],
        m[1]*p.x+m[5]*p.y+m[9]*p.z+m[13],m[2]*p.x+m[6]*p.y+m[10]*p.z+m[14]};
}
static void carrier_check(SwatCharacterAsset* asset) {
    for(int side=0;side<2;side++) {
        int carrier=swat_character_find_node(asset,side ? "Gear_Elbow_R" : "Gear_Elbow_L");
        if(carrier<0) continue;
        const float* g=swat_character_node_matrix(asset,carrier);
        const float* arm=swat_character_node_matrix(asset,swat_character_find_node(asset,side ? "mixamorig:RightArm" : "mixamorig:LeftArm"));
        const float* elbow=swat_character_node_matrix(asset,swat_character_find_node(asset,side ? "mixamorig:RightForeArm" : "mixamorig:LeftForeArm"));
        const float* hand=swat_character_node_matrix(asset,swat_character_find_node(asset,side ? "mixamorig:RightHand" : "mixamorig:LeftHand"));
        Vector3 x={g[0],g[1],g[2]},y={g[4],g[5],g[6]},z={g[8],g[9],g[10]};
        Vector3 upper={elbow[12]-arm[12],elbow[13]-arm[13],elbow[14]-arm[14]};
        Vector3 lower={hand[12]-elbow[12],hand[13]-elbow[13],hand[14]-elbow[14]};
        assert(Vector3Distance((Vector3){g[12],g[13],g[14]},(Vector3){elbow[12],elbow[13],elbow[14]})<1e-5f);
        assert(Vector3Distance(y,Vector3Normalize(lower))<1e-4f);
        assert(fabsf(Vector3DotProduct(z,Vector3Normalize(upper)))<1e-4f);
        assert(fabsf(Vector3DotProduct(z,Vector3Normalize(lower)))<1e-4f);
        assert(Vector3Distance(Vector3CrossProduct(x,y),z)<1e-4f);
        assert(fabsf(Vector3Length(x)-1)<1e-4f && fabsf(Vector3Length(z)-1)<1e-4f);
        // Every exported cap vertex remains wholly rigid. Its skin matrix
        // preserves actual bind-space distances even after world arm IK.
        for(int i=0;i<swat_character_info(asset).meshes;i++) {
            const SwatArtMesh* m=swat_character_mesh(asset,i);
            if(strcmp(m->node_name,side ? "SWAT_ElbowCap_R" : "SWAT_ElbowCap_L")) continue;
            for(int v=0;v<m->vertices;v++) {
                int used=0;
                for(int w=0;w<m->influences;w++) if(m->joint_weights[v*m->influences+w]>0) {
                    int slot=m->joint_ids[v*m->influences+w]; used++;
                    assert(m->joint_weights[v*m->influences+w]==1 && m->joint_nodes[slot]==carrier);
                    Vector3 a={m->bind_positions[0],m->bind_positions[1],m->bind_positions[2]};
                    Vector3 b={m->bind_positions[v*3],m->bind_positions[v*3+1],m->bind_positions[v*3+2]};
                    assert(fabsf(Vector3Distance(point(m->palette+slot*16,a),point(m->palette+slot*16,b))-Vector3Distance(a,b))<1e-5f);
                }
                assert(used==1);
            }
        }
    }
}
static void empty(const SwatSim* sim,bool cutaway) { (void)sim; (void)cutaway; }
static Image frame(SwatCharacterView* view,SwatLighting* light,Camera3D camera,Matrix root) {
    RenderTexture2D target=LoadRenderTexture(512,512); assert(target.id);
    BeginTextureMode(target); ClearBackground(BLACK); BeginMode3D(camera);
    SwatEnvironmentArt art={0}; SwatWorld world={0};
    swat_lighting_begin(light,&art,&world,camera.position);
    swat_character_view_draw(view,light,root);
    // Ordinary geometry after a GPU character must not inherit skinning/maps.
    DrawCube((Vector3){2.5f,.5f,0},.2f,.2f,.2f,WHITE);
    swat_lighting_end(light,&art); EndMode3D(); EndTextureMode();
    Image result=LoadImageFromTexture(target.texture); ImageFlipVertical(&result); UnloadRenderTexture(target); return result;
}
static void grip_check(SwatCharacterRuntime* runtime,SwatLighting* light) {
    const float bridge[16]={0,-1,0,0,0,0,1,0,-1,0,0,0,.0012969123f,.4499999881f,.0226502232f,1};
    Matrix joint={bridge[0],bridge[4],bridge[8],bridge[12],bridge[1],bridge[5],bridge[9],bridge[13],
        bridge[2],bridge[6],bridge[10],bridge[14],bridge[3],bridge[7],bridge[11],bridge[15]};
    for(int bank=0;bank<SWAT_CHARACTER_BANKS;bank++) {
        SwatCharacterView* view=&runtime->banks[bank]; SwatCharacterAsset* asset=view->asset; if(!asset) continue;
        size_t count=(size_t)swat_character_info(asset).nodes*16;
        float* source=malloc(count*sizeof(float)),*corrected=malloc(count*sizeof(float)); assert(source && corrected);
        assert(swat_character_view_sample(view,swat_character_clip_name(asset,0),0));
        assert(swat_character_capture_pose(asset,source,count));
        int prop=swat_character_find_node(asset,"Prop_Rifle");
        const float* p=source+prop*16;
        Matrix gun={p[0],p[4],p[8],p[12],p[1],p[5],p[9],p[13],p[2],p[6],p[10],p[14],p[3],p[7],p[11],p[15]};
        Matrix inverse=MatrixInvert(MatrixMultiply(joint,gun));
        const float amounts[]={0,.25f,.5f,1};
        for(int step=0;step<4;step++) {
            assert(swat_character_restore_pose(asset,source,count));
            assert(swat_character_support_grip(asset,amounts[step]));
            assert(swat_character_capture_pose(asset,corrected,count));
            for(size_t i=0;i<count;i++) assert(isfinite(corrected[i]));
            for(int n=0;n<swat_character_info(asset).nodes;n++) {
                // Only three thumb joints and their terminal descendant move.
                if(!strstr(swat_character_node_name(asset,n),"LeftHandThumb"))
                    assert(!memcmp(source+n*16,corrected+n*16,16*sizeof(float)));
            }
            Vector3 previous_old={0},previous_new={0};
            for(int segment=1;segment<=4;segment++) {
                char name[64]; snprintf(name,sizeof(name),"mixamorig:LeftHandThumb%d",segment);
                int n=swat_character_find_node(asset,name);
                Vector3 old=point(source+n*16,(Vector3){0}),now=point(corrected+n*16,(Vector3){0});
                if(segment==1) assert(Vector3Distance(old,now)<1e-6f);
                else assert(fabsf(Vector3Distance(old,previous_old)-Vector3Distance(now,previous_new))<1e-6f);
                previous_old=old; previous_new=now;
            }
            Vector3 tip=Vector3Transform(previous_new,inverse);
            if(amounts[step]==0) assert(!memcmp(source,corrected,count*sizeof(float)));
            if(amounts[step]==1) {
                // It must actually wrap up and across the outside edge rather
                // than merely change quaternion values along the original axis.
                assert(tip.x<.565f && tip.y>.07f && tip.z>-.03f && tip.z<-.016f);
                assert(swat_character_skin(asset)); // all deformed vertices/normals finite
                printf("Grip bank=%d thumb tip=(%.6f,%.6f,%.6f), original bone lengths retained\n",bank,tip.x,tip.y,tip.z);
            }
            if(bank==0 && (step==0 || step==3) && getenv("SWAT_CHARACTER_TEST_CAPTURES")) {
                const Vector3 eyes[]={{.57f,.14f,-.30f},{.56f,.14f,.30f},{.56f,-.28f,-.05f}};
                for(int angle=0;angle<3;angle++) {
                    Image image=frame(view,light,(Camera3D){eyes[angle],{.55f,.025f,0},{0,1,0},35,CAMERA_PERSPECTIVE},inverse);
                    ExportImage(image,TextFormat("%s/grip-%s-%d.png",getenv("SWAT_CHARACTER_TEST_CAPTURES"),step ? "wrap" : "source",angle)); UnloadImage(image);
                }
            }
        }
        free(source); free(corrected);
    }
}
static void parity(const char* path,SwatLighting* light) {
    SwatCharacterView cpu={0},gpu={0}; char error[256];
    assert(swat_character_view_init(&cpu,path,error,sizeof(error)));
    assert(swat_character_view_init_gpu(&gpu,path,error,sizeof(error)));
    SwatArtInfo info=swat_character_info(cpu.asset); double duration=swat_character_clip_duration(cpu.asset,0);
    const char* clip=swat_character_clip_name(cpu.asset,0); double times[]={0,duration*.137,duration*.95,duration,0};
    Camera3D camera={{3,1.7f,3},{0,.9f,0},{0,1,0},40,CAMERA_PERSPECTIVE};
    for(size_t t=0;t<sizeof(times)/sizeof(*times);t++) {
        assert(swat_character_view_sample(&cpu,clip,times[t])); assert(swat_character_view_sample(&gpu,clip,times[t]));
        for(int i=0;i<info.meshes;i++) assert(cpu.visible[i]==gpu.visible[i]);
        Image a=frame(&cpu,light,camera,MatrixIdentity()),b=frame(&gpu,light,camera,MatrixIdentity());
        Color* x=LoadImageColors(a),*y=LoadImageColors(b); int changed=0,lit=0,max=0; long difference=0;
        for(int i=0;i<512*512;i++) {
            int delta=abs(x[i].r-y[i].r)+abs(x[i].g-y[i].g)+abs(x[i].b-y[i].b);
            if(delta>12) changed++;
            if(delta>max) max=delta;
            difference+=delta; lit+=x[i].r+x[i].g+x[i].b>0;
        }
        printf("GPU parity influences=%d time=%.6f coverage=%d changed=%d mean_rgb_error=%.6f max_rgb_sum=%d\n",info.max_influences,times[t],lit,changed,(double)difference/(512*512*3),max);
        fflush(stdout);
        if(changed>=512*512/1000 || difference>=(long)512*512*3) {
            ExportImage(a,TextFormat("%sgpu-parity-cpu.png",GetApplicationDirectory()));
            ExportImage(b,TextFormat("%sgpu-parity-gpu.png",GetApplicationDirectory()));
        }
        assert(lit>100); assert(changed<512*512/1000); assert(difference<(long)512*512*3);
        UnloadImageColors(x); UnloadImageColors(y); UnloadImage(a); UnloadImage(b);
    }
    swat_character_view_close(&gpu); swat_character_view_close(&cpu);
}
static Image first_person_frame(SwatCharacterRuntime* runtime,SwatSim* sim,SwatLighting* light,float size,float horizontal,float vertical) {
    SwatActor* actor=&sim->actors[0]; SwatPose pose=swat_pose(&actor->controller,&actor->arsenal);
    pose=swat_weapon_view_pose(&pose,&actor->arsenal,actor->controller.ads,horizontal,vertical,.12f);
    float hip=2*atanf(tanf(31*SWAT_RAD)/size)/SWAT_RAD;
    float fov=hip+(45-hip)*actor->controller.ads;
    Camera3D camera={{pose.eye.x,pose.eye.y,pose.eye.z},
        {pose.eye.x+pose.forward.x,pose.eye.y+pose.forward.y,pose.eye.z+pose.forward.z},
        {pose.up.x,pose.up.y,pose.up.z},fov,CAMERA_PERSPECTIVE};
    RenderTexture2D target=LoadRenderTexture(960,540); assert(target.id);
    BeginTextureMode(target); ClearBackground(BLACK); BeginMode3D(camera);
    SwatEnvironmentArt art={0}; SwatWorld world={0}; swat_lighting_begin(light,&art,&world,camera.position);
    assert(swat_character_runtime_draw_first_person(runtime,0,light,&pose));
    swat_lighting_end(light,&art); EndMode3D(); EndTextureMode();
    Image result=LoadImageFromTexture(target.texture); ImageFlipVertical(&result); UnloadRenderTexture(target); return result;
}
static void first_person_check(SwatCharacterRuntime* runtime,SwatSim* sim,SwatLighting* light) {
    SwatActor* actor=&sim->actors[0]; actor->controller.ready_blend=actor->controller.ads=0;
    b3Body_SetLinearVelocity(actor->controller.body.body,b3Vec3_zero);
    for(int stance=0;stance<2;stance++) {
        swat_body_set_crouch(&actor->controller.body,stance!=0);
        actor->controller.eye_height=actor->controller.body.totalHeight-.2032f;
        const float pitch[]={0,-85,-70,70,85}; Color* reference=NULL; int original=0,enlarged=0;
        for(int i=0;i<5;i++) {
            actor->controller.pitch=pitch[i]*SWAT_RAD; actor->controller.yaw=i*SWAT_PI/2;
            sim->tick++; swat_character_runtime_prepare(runtime,sim);
            SwatCharacterActorPose* cache=&runtime->actors[0]; SwatCharacterView* view=&runtime->banks[cache->bank];
            SwatSim* before=malloc(sizeof(*before)); assert(before); memcpy(before,sim,sizeof(*before));
            Image image=first_person_frame(runtime,sim,light,1.7f,-.055f,.075f);
            Color* pixels=LoadImageColors(image); int coverage=0,upper=0,changed=0;
            for(int p=0;p<960*540;p++) {
                bool filled=pixels[p].r+pixels[p].g+pixels[p].b>0;
                coverage+=filled; upper+=filled && p/960<540*45/100;
                if(reference) changed+=filled!=(reference[p].r+reference[p].g+reference[p].b>0);
            }
            printf("First-person pitch=%.0f coverage=%d upper=%d mask_difference=%d\n",pitch[i],coverage,upper,changed); fflush(stdout);
            // Camera-relative framing must survive the look limits. This catches
            // the giant sleeves/missing weapon caused by reusing upright body IK.
            assert(coverage>960*540/20 && upper==0 && changed<960*540/200);
            assert(!memcmp(before,sim,sizeof(*before))); free(before);
            float* restored=malloc(cache->count*sizeof(float)); assert(restored);
            assert(swat_character_capture_pose(view->asset,restored,cache->count));
            assert(!memcmp(restored,cache->matrices,cache->count*sizeof(float))); free(restored);
            if(getenv("SWAT_CHARACTER_TEST_CAPTURES")) ExportImage(image,TextFormat("%s/first-person-%d-%d.png",getenv("SWAT_CHARACTER_TEST_CAPTURES"),stance,i));
            if(!reference) { reference=pixels; enlarged=coverage; } else UnloadImageColors(pixels);
            UnloadImage(image);
        }
        actor->controller.pitch=actor->controller.yaw=0; sim->tick++; swat_character_runtime_prepare(runtime,sim);
        Image old=first_person_frame(runtime,sim,light,1,0,0); Color* pixels=LoadImageColors(old);
        for(int p=0;p<960*540;p++) original+=pixels[p].r+pixels[p].g+pixels[p].b>0;
        printf("First-person original=%d enlarged=%d coverage_ratio=%.3f\n",original,enlarged,(double)enlarged/original);
        assert(enlarged>original*1.5f); UnloadImageColors(pixels); UnloadImage(old); UnloadImageColors(reference);
        // At ADS all hip offsets fade out, even at opposite slider extremes.
        actor->controller.ads=1; sim->tick++; swat_character_runtime_prepare(runtime,sim);
        Image a=first_person_frame(runtime,sim,light,1,-.10f,-.08f),b=first_person_frame(runtime,sim,light,2.4f,.10f,.12f);
        if(getenv("SWAT_CHARACTER_TEST_CAPTURES")) ExportImage(a,TextFormat("%s/first-person-ads-%d.png",getenv("SWAT_CHARACTER_TEST_CAPTURES"),stance));
        pixels=LoadImageColors(a); Color* other=LoadImageColors(b);
        assert(!memcmp(pixels,other,(size_t)960*540*sizeof(Color)));
        int coverage=0; for(int p=0;p<960*540;p++) coverage+=pixels[p].r+pixels[p].g+pixels[p].b>0;
        assert(coverage>960*540/20);
        UnloadImageColors(pixels); UnloadImageColors(other); UnloadImage(a); UnloadImage(b); actor->controller.ads=0;
    }
}
static void ads_projection_check(SwatCharacterRuntime* runtime,SwatSim* sim,SwatLighting* light) {
    SwatActor* actor=&sim->actors[0]; actor->controller.ready_blend=0; actor->controller.ads=1;
    const float pitches[]={-85,-45,0,45,85},reliefs[]={.08f,.12f,.22f};
    for(int stance=0;stance<2;stance++) {
        swat_body_set_crouch(&actor->controller.body,stance!=0);
        actor->controller.eye_height=actor->controller.body.totalHeight-.2032f;
        Color* reference=NULL;
        for(int i=0;i<5;i++) {
            actor->controller.pitch=pitches[i]*SWAT_RAD; actor->controller.yaw=i*SWAT_PI/2;
            actor->controller.recoil_pitch=2*SWAT_RAD; actor->controller.recoil_yaw=.5f*SWAT_RAD;
            actor->controller.lean=(i-2)*.1f; sim->tick++; swat_character_runtime_prepare(runtime,sim);
            SwatCharacterActorPose* cache=&runtime->actors[0]; SwatCharacterAsset* asset=runtime->banks[cache->bank].asset;
            carrier_check(asset);
            // Actual source aperture center and post tip, not the controller's
            // named sight helper: this also checks the complete attachment bridge.
            int prop=swat_character_find_node(asset,"Prop_Rifle");
            Vector3 rear=point(cache->first_person_matrices+prop*16,(Vector3){.0012967195f,.1501783282f,.1473989636f});
            Vector3 front=point(cache->first_person_matrices+prop*16,(Vector3){.0012967659f,-.2049736381f,.1473761201f});
            SwatPose achieved=swat_pose(&actor->controller,&actor->arsenal);
            Camera3D camera={{achieved.eye.x,achieved.eye.y,achieved.eye.z},
                {achieved.eye.x+achieved.forward.x,achieved.eye.y+achieved.forward.y,achieved.eye.z+achieved.forward.z},
                {achieved.up.x,achieved.up.y,achieved.up.z},45,CAMERA_PERSPECTIVE};
            for(int r=0;r<3;r++) {
                SwatPose pose=swat_weapon_view_pose(&achieved,&actor->arsenal,1,.10f,-.08f,reliefs[r]);
                Matrix root=MatrixMultiply(cache->first_person_inverse_gun,swat_weapon_art_transform(&pose));
                Vector3 a=Vector3Transform(rear,root),b=Vector3Transform(front,root);
                Vector2 ap=GetWorldToScreenEx(a,camera,960,540),bp=GetWorldToScreenEx(b,camera,960,540);
                assert(fabsf(ap.x-480)<.1f && fabsf(ap.y-270)<.1f);
                assert(fabsf(bp.x-480)<.1f && fabsf(bp.y-270)<.15f);
                assert(fabsf(Vector3DotProduct(Vector3Subtract(a,camera.position),(Vector3){pose.forward.x,pose.forward.y,pose.forward.z})-reliefs[r])<1e-5f);
                SwatPose unchanged=swat_pose(&actor->controller,&actor->arsenal); assert(!memcmp(&achieved,&unchanged,sizeof(achieved)));
            }
            Image image=first_person_frame(runtime,sim,light,1.7f,-.055f,.075f); Color* pixels=LoadImageColors(image);
            int changed=0,coverage=0; for(int p=0;p<960*540;p++) {
                bool filled=pixels[p].r+pixels[p].g+pixels[p].b>0; coverage+=filled;
                if(reference) changed+=filled!=(reference[p].r+reference[p].g+reference[p].b>0);
            }
            assert(coverage>960*540/20 && changed<960*540/200);
            if(!reference) reference=pixels; else UnloadImageColors(pixels);
            UnloadImage(image);
        }
        UnloadImageColors(reference);
    }
    actor->controller.pitch=actor->controller.yaw=actor->controller.recoil_pitch=actor->controller.recoil_yaw=actor->controller.lean=actor->controller.ads=0;
    printf("PASS ADS: source aperture/post centered at all eye distances, yaw, lean, recoil and pitch limits; rigid F elbow carriers\n");
}
static void backward_check(SwatCharacterRuntime* runtime,SwatSim* sim,SwatLighting* light) {
    SwatActor* actor=&sim->actors[0];
    swat_body_set_crouch(&actor->controller.body,false); actor->controller.body.onGround=true;
    actor->controller.eye_height=actor->controller.body.totalHeight-.2032f;
    bool installed=runtime->banks[SWAT_CHARACTER_BACKWARD].asset!=NULL;
    const float directions[]={0,30,60,90,120,150,180,-150,-120,-90,-60,-30};
    for(int yaw=0;yaw<4;yaw++) for(size_t d=0;d<sizeof(directions)/sizeof(*directions);d++) {
        actor->controller.yaw=yaw*SWAT_PI/2;
        float local=directions[d]*SWAT_RAD,world=actor->controller.yaw+local;
        b3Body_SetLinearVelocity(actor->controller.body.body,(b3Vec3){cosf(world),0,sinf(world)});
        sim->tick++; swat_character_runtime_prepare(runtime,sim);
        int expected=fabsf(sinf(local))>fabsf(cosf(local)) ? (local<0 ? SWAT_CHARACTER_LEFT : SWAT_CHARACTER_RIGHT) :
            cosf(local)<0 && installed ? SWAT_CHARACTER_BACKWARD : SWAT_CHARACTER_WALK;
        if(!runtime->banks[expected].asset) expected=SWAT_CHARACTER_WALK;
        assert(runtime->actors[0].valid && runtime->actors[0].bank==expected);
    }
    actor->controller.yaw=0;
    b3Body_SetLinearVelocity(actor->controller.body.body,(b3Vec3){-1,0,0}); sim->tick++;
    swat_character_runtime_prepare(runtime,sim); double phase=runtime->actors[0].phase;
    b3Pos at=b3Body_GetPosition(actor->controller.body.body);at.x-=.1f;
    b3Body_SetTransform(actor->controller.body.body,at,b3Quat_identity); sim->tick+=2;
    swat_character_runtime_prepare(runtime,sim);
    double advance=fmod(runtime->actors[0].phase-phase+1,1);
    assert(fabs(advance-.1/(installed ? 1.92126144912079 : 1.9212602376939625))<1e-6);
    if(installed) {
        SwatCharacterActorPose* cached=&runtime->actors[0]; SwatCharacterView* view=&runtime->banks[cached->bank];
        assert(swat_character_clip_duration(view->asset,0)==1);
        carrier_check(view->asset);
        if(getenv("SWAT_CHARACTER_TEST_CAPTURES")) {
            Vector3 feet={cached->feet.x,cached->feet.y,cached->feet.z};
            memcpy(view->visible,cached->visible,(size_t)view->model.meshCount);
            Image image=frame(view,light,(Camera3D){Vector3Add(feet,(Vector3){3,1.5f,3}),Vector3Add(feet,(Vector3){0,.9f,0}),{0,1,0},40,CAMERA_PERSPECTIVE},cached->root);
            ExportImage(image,TextFormat("%s/bank-backward.png",getenv("SWAT_CHARACTER_TEST_CAPTURES")));UnloadImage(image);
            image=first_person_frame(runtime,sim,light,1.7f,-.055f,.075f);
            ExportImage(image,TextFormat("%s/first-person-backward.png",getenv("SWAT_CHARACTER_TEST_CAPTURES")));UnloadImage(image);
        }
        // No installed retreat asset must still use the existing forward fallback.
        SwatCharacterView saved=runtime->banks[SWAT_CHARACTER_BACKWARD];runtime->banks[SWAT_CHARACTER_BACKWARD]=(SwatCharacterView){0};
        sim->tick++; swat_character_runtime_prepare(runtime,sim);assert(runtime->actors[0].bank==SWAT_CHARACTER_WALK);
        runtime->banks[SWAT_CHARACTER_BACKWARD]=saved;
    }
    phase=runtime->actors[0].phase;
    b3Body_SetLinearVelocity(actor->controller.body.body,b3Vec3_zero);sim->tick++;
    swat_character_runtime_prepare(runtime,sim);
    assert(runtime->actors[0].bank==SWAT_CHARACTER_READY && runtime->actors[0].phase==phase);
    printf("PASS backward: local direction across yaw/diagonals, measured travel pace, stop and optional-bank fallback\n");
}
static void runtime_check(const char* directory,SwatLighting* light) {
#ifdef _WIN32
    assert(!_putenv_s("SWAT_CHARACTER_ASSETS",directory));
#else
    assert(!setenv("SWAT_CHARACTER_ASSETS",directory,1));
#endif
    SwatWeaponArt weapons={0}; swat_weapon_art_init(&weapons);
    SwatCharacterRuntime* runtime=swat_character_runtime_open(&weapons); assert(runtime);
    for(int bank=0;bank<SWAT_CHARACTER_BANKS;bank++) if(runtime->banks[bank].asset && weapons.carbine.meshCount) {
        assert(runtime->banks[bank].rigid_rifle==&weapons.carbine);
        assert(runtime->banks[bank].weapon_normal_scale==weapons.normal_scale);
    }
    grip_check(runtime,light);
    SwatSim* sim=calloc(1,sizeof(*sim)); SwatSim* before=malloc(sizeof(*before)); assert(sim && before);
    SwatConfig config=swat_default_config(); config.hostile_fire=false; config.mission=SWAT_ANNEX; swat_sim_init(sim,config,42);
    SwatActor* actor=&sim->actors[0];
    bool movement=runtime->banks[SWAT_CHARACTER_LEFT].asset && runtime->banks[SWAT_CHARACTER_RIGHT].asset &&
        runtime->banks[SWAT_CHARACTER_CROUCH_READY].asset && runtime->banks[SWAT_CHARACTER_CROUCH_WALK].asset;
    if(movement) {
        const int expected[]={SWAT_CHARACTER_WALK,SWAT_CHARACTER_LEFT,SWAT_CHARACTER_RIGHT,SWAT_CHARACTER_CROUCH_READY,SWAT_CHARACTER_CROUCH_WALK};
        const b3Vec3 velocities[]={{1,0,0},{0,0,-1},{0,0,1},{0,0,0},{1,0,0}};
        for(int k=0;k<5;k++) {
            actor->controller.yaw=0;swat_body_set_crouch(&actor->controller.body,k>=3);actor->controller.body.onGround=true;
            actor->controller.eye_height=actor->controller.body.totalHeight-.2032f;
            b3Body_SetLinearVelocity(actor->controller.body.body,velocities[k]);sim->tick++;
            memcpy(before,sim,sizeof(*before));swat_character_runtime_prepare(runtime,sim);
            assert(!memcmp(before,sim,sizeof(*before)) && runtime->actors[0].valid && runtime->actors[0].bank==expected[k]);
            if(k==3) assert(runtime->actors[0].source_time==0 && swat_character_clip_duration(runtime->banks[expected[k]].asset,0)==0);
            if(getenv("SWAT_CHARACTER_TEST_CAPTURES")) {
                SwatCharacterActorPose* pose=&runtime->actors[0];SwatCharacterView* bank=&runtime->banks[pose->bank];
                assert(swat_character_restore_pose(bank->asset,pose->matrices,pose->count));
                memcpy(bank->visible,pose->visible,(size_t)bank->model.meshCount);
                Vector3 feet={pose->feet.x,pose->feet.y,pose->feet.z};
                Image image=frame(bank,light,(Camera3D){Vector3Add(feet,(Vector3){3,1.5f,3}),Vector3Add(feet,(Vector3){0,k>=3 ? .65f : .9f,0}),{0,1,0},40,CAMERA_PERSPECTIVE},pose->root);
                ExportImage(image,TextFormat("%s/bank-%d.png",getenv("SWAT_CHARACTER_TEST_CAPTURES"),pose->bank));UnloadImage(image);
                image=first_person_frame(runtime,sim,light,1.7f,-.055f,.075f);
                ExportImage(image,TextFormat("%s/first-person-bank-%d.png",getenv("SWAT_CHARACTER_TEST_CAPTURES"),pose->bank));UnloadImage(image);
            }
        }
        // A stopped collider cannot animate a stride merely from held input or
        // elapsed time. Actual 10 cm motion uses this crouch clip's own pace.
        double phase=runtime->actors[0].phase;sim->tick++;
        swat_character_runtime_prepare(runtime,sim);assert(runtime->actors[0].phase==phase);
        b3Pos at=b3Body_GetPosition(actor->controller.body.body);at.x+=.1f;
        b3Body_SetTransform(actor->controller.body.body,at,b3Quat_identity);sim->tick+=2;
        swat_character_runtime_prepare(runtime,sim);
        double delta=fmod(runtime->actors[0].phase-phase+1,1);
        assert(fabs(delta-.1/2.0390741825103778)<1e-6);
        // Optional bank absence falls back to forward locomotion without
        // disabling the character. Restore ownership immediately afterwards.
        SwatCharacterView left=runtime->banks[SWAT_CHARACTER_LEFT];runtime->banks[SWAT_CHARACTER_LEFT]=(SwatCharacterView){0};
        swat_body_set_crouch(&actor->controller.body,false);actor->controller.body.onGround=true;
        b3Body_SetLinearVelocity(actor->controller.body.body,(b3Vec3){0,0,-1});sim->tick++;
        swat_character_runtime_prepare(runtime,sim);assert(runtime->actors[0].bank==SWAT_CHARACTER_WALK);
        runtime->banks[SWAT_CHARACTER_LEFT]=left;
        b3Body_SetLinearVelocity(actor->controller.body.body,b3Vec3_zero);
    }
    backward_check(runtime,sim,light);
    for(int stance=0;stance<2;stance++) for(int angle=0;angle<4;angle++) {
        actor->controller.yaw=angle*SWAT_PI/2; actor->controller.pitch=angle==3 ? 70*SWAT_RAD : 0;
        swat_body_set_crouch(&actor->controller.body,stance!=0); sim->tick++;
        actor->controller.eye_height=actor->controller.body.totalHeight-.2032f;
        memcpy(before,sim,sizeof(*before)); swat_character_runtime_prepare(runtime,sim); assert(!memcmp(before,sim,sizeof(*before)));
        assert(runtime->actors[0].valid);
        if(movement) assert(runtime->actors[0].bank==(stance ? SWAT_CHARACTER_CROUCH_READY : SWAT_CHARACTER_READY));
        unsigned int preparations=runtime->preparations; swat_character_runtime_prepare(runtime,sim); assert(preparations==runtime->preparations);
        SwatCharacterActorPose* cached=&runtime->actors[0]; SwatCharacterView* view=&runtime->banks[cached->bank];
        // Upper aiming must retain the abdomen's sampled bone length, even
        // through large pitch/crouch targets that arms cannot fully reach.
        int spine=swat_character_find_node(view->asset,"mixamorig:Spine2"),parent=swat_character_find_node(view->asset,"mixamorig:Spine1");
        assert(swat_character_view_sample(view,swat_character_clip_name(view->asset,0),cached->source_time));
        const float* a=swat_character_node_matrix(view->asset,spine),*b=swat_character_node_matrix(view->asset,parent);
        float source_length=Vector3Distance((Vector3){a[12],a[13],a[14]},(Vector3){b[12],b[13],b[14]});
        assert(swat_character_restore_pose(view->asset,cached->matrices,cached->count));
        a=swat_character_node_matrix(view->asset,spine);b=swat_character_node_matrix(view->asset,parent);
        assert(fabsf(Vector3Distance((Vector3){a[12],a[13],a[14]},(Vector3){b[12],b[13],b[14]})-source_length)<1e-5f);
        // Physical rifle stock transform exactly follows achieved pose at every
        // yaw/stance/pitch, independent of anatomical markers and source roots.
        const float* w=swat_character_node_matrix(view->asset,swat_character_find_node(view->asset,"Prop_Rifle"));
        Vector3 stock={.0012969123f,.4499999881f,.0226502232f};
        Vector3 model={w[0]*stock.x+w[4]*stock.y+w[8]*stock.z+w[12],w[1]*stock.x+w[5]*stock.y+w[9]*stock.z+w[13],w[2]*stock.x+w[6]*stock.y+w[10]*stock.z+w[14]};
        Vector3 actual=Vector3Transform(model,cached->root); SwatPose pose=swat_pose(&actor->controller,&actor->arsenal);
        assert(Vector3Distance(actual,(Vector3){pose.shoulder.x,pose.shoulder.y,pose.shoulder.z})<1e-4f);
        unsigned int draws=runtime->draws;
        Vector3 feet_camera={cached->feet.x,cached->feet.y+1,cached->feet.z};
        BeginDrawing(); BeginMode3D((Camera3D){Vector3Add(feet_camera,(Vector3){4,0,0}),Vector3Add(feet_camera,(Vector3){5,0,0}),{0,1,0},40,CAMERA_PERSPECTIVE});
        assert(swat_character_runtime_draw(runtime,0,NULL)); assert(runtime->draws==draws); EndMode3D(); EndDrawing();
        BeginDrawing(); BeginMode3D((Camera3D){Vector3Add(feet_camera,(Vector3){4,0,0}),feet_camera,{0,1,0},40,CAMERA_PERSPECTIVE});
        assert(swat_character_runtime_draw(runtime,0,NULL)); assert(runtime->draws==draws+1); EndMode3D(); EndDrawing();
        Vector3 feet={cached->feet.x,cached->feet.y,cached->feet.z};
        memcpy(view->visible,cached->visible,(size_t)view->model.meshCount);
        for(int pass=0;pass<3;pass++) {
            Image image=frame(view,light,(Camera3D){Vector3Add(feet,(Vector3){3,1.5f,3}),Vector3Add(feet,(Vector3){0,stance ? .5f : .9f,0}),{0,1,0},40,CAMERA_PERSPECTIVE},cached->root);
            if(pass==0 && angle==0 && getenv("SWAT_CHARACTER_TEST_CAPTURES")) ExportImage(image,TextFormat("%s/live-%s.png",getenv("SWAT_CHARACTER_TEST_CAPTURES"),stance ? "crouch" : "standing"));
            UnloadImage(image);
        }
        assert(!memcmp(before,sim,sizeof(*before)));
    }
    first_person_check(runtime,sim,light);
    ads_projection_check(runtime,sim,light);
    SwatWeapon* weapon=&actor->arsenal.slots[0]; weapon->magazine=0; weapon->chambered=false;
    SwatInput input=swat_neutral_input(); input.reload=true;
    swat_weapons_step(&actor->arsenal,&input,0,0,true,false); input.reload=false;
    int duration=weapon->reload_duration; assert(duration>0);
    for(int elapsed=1;elapsed<=duration;elapsed++) {
        swat_weapons_step(&actor->arsenal,&input,0,0,true,false); sim->tick++;
        memcpy(before,sim,sizeof(*before)); swat_character_runtime_prepare(runtime,sim); assert(runtime->actors[0].valid);
        assert(!memcmp(before,sim,sizeof(*before)));
        if(weapon->reload_remaining>0) assert(runtime->actors[0].bank==SWAT_CHARACTER_READY);
        double time=runtime->actors[0].source_time;
        if(time>=.12 && time<=5.82) {
            SwatCharacterAsset* asset=runtime->banks[0].asset;
            assert(swat_character_sample_pose(asset,swat_character_clip_name(asset,0),time));
            for(int segment=1;segment<=4;segment++) {
                char name[64]; snprintf(name,sizeof(name),"mixamorig:LeftHandThumb%d",segment);
                int n=swat_character_find_node(asset,name);
                assert(!memcmp(swat_character_node_matrix(asset,n),runtime->actors[0].first_person_matrices+n*16,16*sizeof(float)));
            }
            assert(swat_character_restore_pose(asset,runtime->actors[0].matrices,runtime->actors[0].count));
        }
        if(elapsed==duration/4) assert(fabs(time-.88)<1e-7 && !weapon->magazine_seated);
        if(elapsed==2*duration/3) assert(fabs(time-3.65)<1e-7 && weapon->magazine_seated);
        if(elapsed==duration/4 || elapsed==2*duration/3) {
            Image image=first_person_frame(runtime,sim,light,1.7f,-.055f,.075f); UnloadImage(image);
            assert(!memcmp(before,sim,sizeof(*before)));
        }
    }
    assert(runtime->actors[0].source_time==0 && weapon->magazine_seated);
    // Cancellation after removal, then a fresh INSERT start, cannot restore A.
    weapon->magazine=0; weapon->chambered=false; input.reload=true;
    swat_weapons_step(&actor->arsenal,&input,0,0,true,false); input.reload=false;
    while(weapon->reload_stage==SWAT_RELOAD_REMOVE) swat_weapons_step(&actor->arsenal,&input,0,0,true,false);
    swat_weapons_cancel_reload(weapon); sim->tick++; swat_character_runtime_prepare(runtime,sim);
    assert(!weapon->magazine_seated); assert(runtime->actors[0].source_time==0);
    SwatCharacterView* ready=&runtime->banks[0];
    for(int m=0;m<ready->model.meshCount;m++) if(!strcmp(swat_character_mesh(ready->asset,m)->node_name,"Removed magazine")) assert(!runtime->actors[0].visible[m]);
    input.reload=true; swat_weapons_step(&actor->arsenal,&input,0,0,true,false); sim->tick++; swat_character_runtime_prepare(runtime,sim);
    assert(weapon->reload_stage==SWAT_RELOAD_INSERT && runtime->actors[0].source_time>=.88);
    printf("PASS live GPU character: yaw, crouch, pitch, immutable authority, camera reuse, reload commits/cancellation/restart\n");
    swat_sim_close(sim); free(sim); free(before); swat_character_runtime_close(runtime); swat_weapon_art_close(&weapons);
}
int main(int argc,char** argv) {
    assert(argc>=2); SetConfigFlags(FLAG_WINDOW_HIDDEN); InitWindow(512,512,"character GPU contract"); assert(IsWindowReady());
    SwatLighting light={0}; swat_lighting_init(&light); assert(light.enabled);
    static SwatSim sim; swat_lighting_prepare(&light,&sim,(Vector3){3,1.7f,3},false,empty);
    parity(argv[1],&light); if(argc==3) runtime_check(argv[2],&light);
    swat_lighting_close(&light); CloseWindow(); return 0;
}
