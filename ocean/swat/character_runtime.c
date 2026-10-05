#include "character_runtime.h"
#include "pose.h"
#include "raymath.h"
#include "rlgl.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

static const char* clips[]={"Standing Empty / Shared Ready N Carry F","Forward Walk / Shared Ready N C1 Seam Repair B",
    "Walk Left / Shared Ready N C1 Loop A","Walk Right / Shared Ready N C1 Loop A",
    "Crouch Ready / Planted Shared N Low-Ready","Crouch Forward / Shared Ready N C1 Loop A",
    "Walk Backward / Shared Ready N C1 Loop A"};
static const char* files[]={"ready","walk","walk_left","walk_right","crouch_ready","crouch_walk","walk_backward"};
// Authored cycle distances, not controller speeds. The static crouch is a
// single STEP key with no duration and is always sampled at zero.
static const double cycle_metres[]={0,1.9212602376939625,1.9212619066480534,1.921261549020877,0,2.0390741825103778,1.92126144912079};
// Lateral source travel measured AFTER its fixed preview fit; preserve native
// chest/pelvis counter-rotation instead of inferring facing from clip labels.
static const float travel_yaw[]={0,.214434941f,-1.683827915f,1.295261099f,0,.103343568f,3.001731908f};
#include "character_movement_data.h"
// Immutable measured model-to-heading-zero/feet transforms, metres, scale 1.
static const float fits[2][16]={
    {.0064592065f,0,-.9999791391f,0, 0,1,0,0, .9999791391f,0,.0064592065f,0, .041890702f,-.0039999791f,.0199043523f,1},
    {.2127957631f,0,-.977096701f,0, 0,1,0,0, .977096701f,0,.2127957631f,0, -.0668057707f,-.0040001063f,-.035970121f,1}
};
// J = IBM[resolved Prop_Rifle skin slot] * measured rigid-to-bind bridge.
static const float rifle_joint[16]={0,-1,0,0, 0,0,1,0, -1,0,0,0, .0012969123f,.4499999881f,.0226502232f,1};
static Matrix matrix(const float* f) {
    return (Matrix){f[0],f[4],f[8],f[12],f[1],f[5],f[9],f[13],f[2],f[6],f[10],f[14],f[3],f[7],f[11],f[15]};
}
// Column vectors: compose(a,b) applies b first, then a. Raymath's multiply
// parameter convention is the reverse of this expression.
static Matrix compose(Matrix a,Matrix b) { return MatrixMultiply(b,a); }
static uint64_t fingerprint(uint64_t hash,const void* bytes,size_t count) {
    const unsigned char* p=bytes; for(size_t i=0;i<count;i++) hash=(hash^p[i])*UINT64_C(1099511628211); return hash;
}
static bool outside_camera(b3Pos feet) {
    Matrix clip=MatrixMultiply(rlGetMatrixModelview(),rlGetMatrixProjection());
    const float rows[4][4]={{clip.m0,clip.m4,clip.m8,clip.m12},{clip.m1,clip.m5,clip.m9,clip.m13},{clip.m2,clip.m6,clip.m10,clip.m14},{clip.m3,clip.m7,clip.m11,clip.m15}};
    // Conservative two-metre sphere includes head, limbs and measured rifle.
    // Each main/feed/shadow camera has its own current matrices. This changes
    // draw submission only and never feeds gameplay visibility or policy.
    Vector3 center={feet.x,feet.y+.9f,feet.z};
    for(int axis=0;axis<3;axis++) for(int side=-1;side<=1;side+=2) {
        Vector3 normal={rows[3][0]+side*rows[axis][0],rows[3][1]+side*rows[axis][1],rows[3][2]+side*rows[axis][2]};
        float distance=Vector3DotProduct(normal,center)+rows[3][3]+side*rows[axis][3];
        if(distance < -2*Vector3Length(normal)) return true;
    }
    return false;
}
static Vector3 origin(Matrix m) { return (Vector3){m.m12,m.m13,m.m14}; }
static Matrix node(SwatCharacterAsset* asset,const char* name) { return matrix(swat_character_node_matrix(asset,swat_character_find_node(asset,name))); }
static bool transform(SwatCharacterAsset* asset,const char* name,Matrix delta) {
    float16 f=MatrixToFloatV(delta); return swat_character_transform_node(asset,swat_character_find_node(asset,name),f.v);
}
static bool pivot_rotate(SwatCharacterAsset* asset,const char* name,Vector3 from,Vector3 to,Vector3 pivot) {
    if(Vector3LengthSqr(from)<1e-12f || Vector3LengthSqr(to)<1e-12f) return false;
    Matrix rotation=QuaternionToMatrix(QuaternionFromVector3ToVector3(Vector3Normalize(from),Vector3Normalize(to)));
    Matrix delta=compose(MatrixTranslate(pivot.x,pivot.y,pivot.z),compose(rotation,MatrixTranslate(-pivot.x,-pivot.y,-pivot.z)));
    return transform(asset,name,delta);
}
// Presentation-only two-bone solve. Lengths come from the sampled source pose;
// unreachable targets clamp to the limb's reach rather than stretching it.
static bool limb(SwatCharacterAsset* asset,const char* upper,const char* lower,const char* end,Matrix target) {
    Vector3 a=origin(node(asset,upper)),b=origin(node(asset,lower)),c=origin(node(asset,end)),goal=origin(target);
    float ab=Vector3Distance(a,b),bc=Vector3Distance(b,c),distance=Vector3Distance(a,goal);
    if(ab<1e-5f || bc<1e-5f || distance<1e-5f) return true;
    Vector3 direction=Vector3Scale(Vector3Subtract(goal,a),1/distance);
    float reach=Clamp(distance,fabsf(ab-bc)+1e-5f,ab+bc-1e-5f);
    float along=(ab*ab-bc*bc+reach*reach)/(2*reach),height=sqrtf(fmaxf(0,ab*ab-along*along));
    Vector3 pole=Vector3Subtract(Vector3Subtract(b,a),Vector3Scale(direction,Vector3DotProduct(Vector3Subtract(b,a),direction)));
    if(Vector3LengthSqr(pole)<1e-10f) pole=Vector3CrossProduct(direction,fabsf(direction.y)<.9f ? (Vector3){0,1,0} : (Vector3){1,0,0});
    Vector3 elbow=Vector3Add(a,Vector3Add(Vector3Scale(direction,along),Vector3Scale(Vector3Normalize(pole),height)));
    if(!pivot_rotate(asset,upper,Vector3Subtract(b,a),Vector3Subtract(elbow,a),a)) return false;
    b=origin(node(asset,lower)); c=origin(node(asset,end)); goal=Vector3Add(a,Vector3Scale(direction,reach));
    if(!pivot_rotate(asset,lower,Vector3Subtract(c,b),Vector3Subtract(goal,b),b)) return false;
    Matrix current=node(asset,end); Vector3 reached=origin(current);
    target.m12=reached.x; target.m13=reached.y; target.m14=reached.z;
    return transform(asset,end,compose(target,MatrixInvert(current)));
}

// The F geometry's rigid pads use an anatomical elbow frame, separate from
// forearm pronation. Reconstruct it after sampling and after arm IK; otherwise
// the old motion banks cannot drive the two newly introduced carrier joints.
static bool elbow_carrier(SwatCharacterAsset* asset,const char* side,const char* carrier) {
    int index=swat_character_find_node(asset,carrier); if(index<0) return true;
    char name[64]; snprintf(name,sizeof(name),"mixamorig:%sArm",side); Vector3 a=origin(node(asset,name));
    snprintf(name,sizeof(name),"mixamorig:%sForeArm",side); Vector3 b=origin(node(asset,name));
    snprintf(name,sizeof(name),"mixamorig:%sHand",side); Vector3 c=origin(node(asset,name));
    Vector3 y=Vector3Subtract(c,b),z=Vector3CrossProduct(Vector3Subtract(b,a),y);
    if(Vector3LengthSqr(y)<1e-10f) return false;
    y=Vector3Normalize(y); Matrix current=node(asset,carrier);
    if(Vector3LengthSqr(z)<1e-10f) {
        z=(Vector3){current.m8,current.m9,current.m10};
        z=Vector3Subtract(z,Vector3Scale(y,Vector3DotProduct(y,z)));
        if(Vector3LengthSqr(z)<1e-10f) return false;
    }
    z=Vector3Normalize(z); Vector3 x=Vector3Normalize(Vector3CrossProduct(y,z)); z=Vector3CrossProduct(x,y);
    Matrix target={.m0=x.x,.m1=x.y,.m2=x.z,.m4=y.x,.m5=y.y,.m6=y.z,
        .m8=z.x,.m9=z.y,.m10=z.z,.m12=b.x,.m13=b.y,.m14=b.z,.m15=1};
    return transform(asset,carrier,compose(target,MatrixInvert(current)));
}
static bool elbow_carriers(SwatCharacterAsset* asset) {
    return elbow_carrier(asset,"Left","Gear_Elbow_L") && elbow_carrier(asset,"Right","Gear_Elbow_R");
}

// Rifle-specific support thumb, in the measured stock-origin frame (+X
// forward, +Y up, +Z right). Rotate the sampled joints, never substitute the
// Blender matrix_basis quaternions: those use a different native rest frame.
// The thumb comes up the outside of the handguard and curls over its edge.
// Its wrist, root, segment lengths and the other fingers remain authored.
bool swat_character_support_grip(SwatCharacterAsset* asset,float amount) {
    if(!asset || !isfinite(amount)) return false;
    amount=Clamp(amount,0,1); if(amount==0) return true;
    const char* names[]={"mixamorig:LeftHandThumb1","mixamorig:LeftHandThumb2",
        "mixamorig:LeftHandThumb3","mixamorig:LeftHandThumb4"};
    if(swat_character_find_node(asset,"Prop_Rifle")<0) return false;
    for(int i=0;i<4;i++) if(swat_character_find_node(asset,names[i])<0) return false;
    Matrix gun=compose(node(asset,"Prop_Rifle"),matrix(rifle_joint));
    const Vector3 directions[]={{.013f,.0317f,-.0135f},{.003f,.034f,.011f},{-.002f,.023f,.018f}};
    Vector3 sampled[3];
    for(int i=0;i<3;i++) sampled[i]=Vector3Subtract(origin(node(asset,names[i+1])),origin(node(asset,names[i])));
    for(int i=0;i<3;i++) {
        Vector3 direction=Vector3Subtract(Vector3Transform(directions[i],gun),origin(gun));
        Quaternion rotation=QuaternionFromVector3ToVector3(Vector3Normalize(sampled[i]),Vector3Normalize(direction));
        direction=Vector3RotateByQuaternion(sampled[i],QuaternionSlerp(QuaternionIdentity(),rotation,amount));
        Vector3 pivot=origin(node(asset,names[i]));
        if(!pivot_rotate(asset,names[i],Vector3Subtract(origin(node(asset,names[i+1])),pivot),direction,pivot)) return false;
    }
    return true;
}

double swat_character_reload_time(const SwatWeapon* w) {
    if(!w || w->reload_remaining<=0 || w->reload_duration<=0) return 0;
    int elapsed=w->reload_duration-w->reload_remaining,remove=w->reload_duration/4,insert=2*w->reload_duration/3;
    if(w->reload_stage==SWAT_RELOAD_REMOVE && elapsed<remove) return .88*(double)elapsed/fmax(1,remove);
    if(w->reload_stage==SWAT_RELOAD_INSERT && elapsed<insert) return .88+(3.65-.88)*fmax(0,elapsed-remove)/fmax(1,insert-remove);
    return 3.65+(6-3.65)*fmax(0,elapsed-insert)/fmax(1,w->reload_duration-insert);
}

static Texture2D texture(const char* directory,const char* file) {
    char path[4096]; snprintf(path,sizeof(path),"%s/textures/%s",directory,file);
    if(!FileExists(path)) return (Texture2D){0};
    Image image=LoadImage(path); if(!image.data) return (Texture2D){0};
    // glTF TEXCOORD_0 is unchanged; PNG rows follow the same convention as
    // Raylib's embedded glTF texture upload. No image/UV flip is introduced.
    Texture2D result=LoadTextureFromImage(image); UnloadImage(image);
    if(result.id) { GenTextureMipmaps(&result); SetTextureFilter(result,TEXTURE_FILTER_TRILINEAR); }
    return result;
}
SwatCharacterRuntime* swat_character_runtime_open(const SwatWeaponArt* weapons) {
    const char* enabled=getenv("SWAT_CHARACTER_ART"); if(enabled && !strcmp(enabled,"0")) return NULL;
    char directory[2048],path[4096],error[256]; const char* custom=getenv("SWAT_CHARACTER_ASSETS");
    if(custom && strlen(custom)>=sizeof(directory)) return NULL;
    if(custom && *custom) snprintf(directory,sizeof(directory),"%s",custom);
    else { snprintf(directory,sizeof(directory),"%sassets/characters",GetApplicationDirectory()); if(!DirectoryExists(directory)) snprintf(directory,sizeof(directory),"build/swat/assets/characters"); }
    snprintf(path,sizeof(path),"%s/ready.glb",directory); if(!FileExists(path)) { TraceLog(LOG_INFO,"SWAT: private character absent; procedural actors"); return NULL; }
    SwatCharacterRuntime* runtime=calloc(1,sizeof(*runtime)); if(!runtime) return NULL;
    char geometry[4096]; snprintf(geometry,sizeof(geometry),"%s/upper_gear_f",directory);
    const char* gear=getenv("SWAT_CHARACTER_GEAR");
    snprintf(path,sizeof(path),"%s/ready.glb",geometry);
    bool use_gear=(!gear || strcmp(gear,"0")) && FileExists(path);
    if(use_gear) TraceLog(LOG_INFO,"SWAT: F gear geometry / original gameplay motion / anatomical elbow carriers");
    for(int i=0;i<SWAT_CHARACTER_BANKS;i++) {
        snprintf(path,sizeof(path),"%s/%s.glb",use_gear ? geometry : directory,files[i]);
        if(i>=2 && !FileExists(path)) continue;
        if(!swat_character_view_init_gpu(&runtime->banks[i],path,error,sizeof(error))) {
            TraceLog(LOG_WARNING,"SWAT: character bank %s: %s",files[i],error);
            if(i<2) goto failed;
            continue;
        }
        SwatArtInfo info=swat_character_info(runtime->banks[i].asset);
        if(info.meshes>SWAT_CHARACTER_MESHES || info.clips!=1 || strcmp(swat_character_clip_name(runtime->banks[i].asset,0),clips[i]) ||
            swat_character_find_node(runtime->banks[i].asset,"Prop_Rifle")<0 || swat_character_find_node(runtime->banks[i].asset,"mixamorig:Spine2")<0) {
            if(i<2) goto failed;
            swat_character_view_close(&runtime->banks[i]); continue;
        }
        const char* required[]={"mixamorig:Hips","mixamorig:LeftUpLeg","mixamorig:LeftLeg","mixamorig:LeftFoot","mixamorig:RightUpLeg","mixamorig:RightLeg","mixamorig:RightFoot","mixamorig:LeftArm","mixamorig:LeftForeArm","mixamorig:LeftHand","mixamorig:RightArm","mixamorig:RightForeArm","mixamorig:RightHand","Prop_Magazine_A","Prop_Magazine_B"};
        bool valid=true;
        if(use_gear && (info.joints!=72 || swat_character_find_node(runtime->banks[i].asset,"Gear_Elbow_L")<0 ||
                       swat_character_find_node(runtime->banks[i].asset,"Gear_Elbow_R")<0)) valid=false;
        for(size_t j=0;j<sizeof(required)/sizeof(*required);j++) if(swat_character_find_node(runtime->banks[i].asset,required[j])<0) valid=false;
        double duration=swat_character_clip_duration(runtime->banks[i].asset,0);
        valid=valid && (i==SWAT_CHARACTER_CROUCH_READY ? duration==0 : duration>0);
        if(!valid) { if(i<2) goto failed; swat_character_view_close(&runtime->banks[i]); }
    }
    runtime->diffuse[0]=texture(directory,"Ch15_1001_Diffuse.png"); runtime->diffuse[1]=texture(directory,"Ch15_1002_Diffuse.png");
    runtime->emissive=texture(directory,"Ch15_1002_Emissive.png");
    const char* finish=getenv("SWAT_CHARACTER_FINISH");
    if(!finish || strcmp(finish,"0")) for(int i=0;i<2;i++) {
        char file[128]; snprintf(file,sizeof(file),"Ch15_100%d_Specular.png",i+1); runtime->specular[i]=texture(directory,file);
        snprintf(file,sizeof(file),"Ch15_100%d_Glossiness.png",i+1); runtime->gloss[i]=texture(directory,file);
    }
    const char* normals=getenv("SWAT_CHARACTER_NORMALS");
    if(!normals || strcmp(normals,"0")) { runtime->normal[0]=texture(directory,"Ch15_1001_Normal.png"); runtime->normal[1]=texture(directory,"Ch15_1002_Normal.png"); }
    Image white=GenImageColor(1,1,WHITE); runtime->orm=LoadTextureFromImage(white); UnloadImage(white);
    for(int b=0;b<SWAT_CHARACTER_BANKS;b++) for(int i=0;i<runtime->banks[b].model.meshCount;i++) {
        if(weapons && weapons->carbine.meshCount) {
            runtime->banks[b].rigid_rifle=&weapons->carbine;
            runtime->banks[b].rifle_bridge=matrix(rifle_joint);
            runtime->banks[b].weapon_normal_scale=weapons->normal_scale;
        }
        const SwatArtMesh* source=swat_character_mesh(runtime->banks[b].asset,i);
        Material* material=&runtime->banks[b].model.materials[i];
        if(swat_character_body_mesh(source->node_name)) {
            int set=source->primitive; if(set>=2) goto failed;
            if(runtime->diffuse[set].id) { material->maps[MATERIAL_MAP_ALBEDO].texture=runtime->diffuse[set]; material->maps[MATERIAL_MAP_ALBEDO].color=WHITE; }
            material->maps[MATERIAL_MAP_ROUGHNESS].texture=runtime->orm; material->maps[MATERIAL_MAP_ROUGHNESS].value=.78f; material->maps[MATERIAL_MAP_METALNESS].value=0;
            if(runtime->specular[set].id && runtime->gloss[set].id) {
                // Source specular color is sRGB; gloss is linear. Do not turn
                // arbitrary specular pixels into a guessed metallic mask.
                material->maps[MATERIAL_MAP_SPECULAR].texture=runtime->specular[set];
                material->maps[MATERIAL_MAP_ROUGHNESS].texture=runtime->gloss[set];
                material->maps[MATERIAL_MAP_ROUGHNESS].value=-1;
            }
            if(set==1 && runtime->emissive.id) material->maps[MATERIAL_MAP_EMISSION].texture=runtime->emissive;
            material->maps[MATERIAL_MAP_NORMAL].texture=runtime->normal[set];
            material->maps[MATERIAL_MAP_NORMAL].value=normals && !strcmp(normals,"-y") ? -2 : 2;
        } else if((!strcmp(source->node_name,"Rifle 7") || strstr(source->node_name,"magazine")) && !strstr(source->node_name,"sleeve") && weapons && weapons->carbine.meshCount) {
            // Same original UVs and measured mesh; borrow the private 4K gun maps.
            memcpy(material->maps,weapons->carbine.materials[weapons->carbine.meshMaterial[0]].maps,(MATERIAL_MAP_BRDF+1)*sizeof(MaterialMap));
            material->maps[MATERIAL_MAP_NORMAL].value=2; // signed UV derivative basis on posed geometry
        }
    }
    TraceLog(LOG_INFO,"SWAT: live character Ready/reload and movement banks, all influences on GPU, original body maps"); return runtime;
failed:
    TraceLog(LOG_WARNING,"SWAT: unsupported character handoff; procedural actors"); swat_character_runtime_close(runtime); return NULL;
}

void swat_character_runtime_prepare(SwatCharacterRuntime* runtime,const SwatSim* sim) {
    if(!runtime) return;
    for(int i=0;i<SWAT_MAX_ACTORS;i++) {
        SwatCharacterActorPose* cache=&runtime->actors[i]; const SwatActor* actor=&sim->actors[i];
        bool eligible=i<sim->actor_count && actor->present && actor->alive && (actor->role==SWAT_OFFICER || actor->role==SWAT_SNIPER) &&
            !actor->gear.surrendered && !actor->gear.restrained && actor->arsenal.active==0 && actor->arsenal.primary==0;
        if(!eligible) { cache->valid=false; continue; }
        b3Pos feet=swat_body_feet_position(&actor->controller.body);
        b3Vec3 velocity=b3Body_GetLinearVelocity(actor->controller.body.body); float speed=hypotf(velocity.x,velocity.z);
        uint64_t signature=fingerprint(UINT64_C(14695981039346656037),&actor->controller,sizeof(actor->controller));
        signature=fingerprint(signature,&actor->arsenal,sizeof(actor->arsenal)); signature=fingerprint(signature,&feet,sizeof(feet)); signature=fingerprint(signature,&velocity,sizeof(velocity));
        if(cache->valid && cache->tick==sim->tick && cache->episode==sim->episode && cache->signature==signature) continue;
        bool reset=!cache->valid || cache->episode!=sim->episode || sim->tick<=cache->tick;
        double travel=reset ? 0 : hypot((double)feet.x-cache->feet.x,(double)feet.z-cache->feet.z);
        if(travel>6*(sim->tick-cache->tick)/60.0+.25) travel=0; // teleport/restore, no invented travel
        cache->distance=reset ? 0 : cache->distance+travel;
        const SwatWeapon* weapon=&actor->arsenal.slots[0]; bool reload=weapon->reload_remaining>0;
        bool moving=actor->controller.body.onGround && speed>.12f;
        bool crouch=actor->controller.body.totalHeight<actor->controller.body.standHeight-.01f;
        float direction=moving ? swat_angle(atan2f(velocity.z,velocity.x)-actor->controller.yaw) : 0;
        int bank=SWAT_CHARACTER_READY;
        if(!reload) {
            if(crouch) bank=moving ? SWAT_CHARACTER_CROUCH_WALK : SWAT_CHARACTER_CROUCH_READY;
            else if(moving) bank=fabsf(sinf(direction))>fabsf(cosf(direction)) ? (direction<0 ? SWAT_CHARACTER_LEFT : SWAT_CHARACTER_RIGHT) :
                cosf(direction)<0 ? SWAT_CHARACTER_BACKWARD : SWAT_CHARACTER_WALK;
            if(!runtime->banks[bank].asset) bank=moving ? SWAT_CHARACTER_WALK : SWAT_CHARACTER_READY;
        }
        cache->phase=reset ? 0 : cache->phase;
        if(cycle_metres[bank]>0) cache->phase=fmod(cache->phase+travel/cycle_metres[bank],1.0);
        double time=reload ? swat_character_reload_time(weapon) : cycle_metres[bank]>0 ? cache->phase*swat_character_clip_duration(runtime->banks[bank].asset,0) : 0;
        SwatCharacterView* view=&runtime->banks[bank]; SwatCharacterAsset* asset=view->asset;
        double started=GetTime();
        // Release the holding correction for the authored magazine handling;
        // short endpoint fades avoid snapping between the source and hold pose.
        float grip=reload ? Clamp(1-(float)time/.12f,0,1)+Clamp(((float)time-5.82f)/.18f,0,1) : 1;
        cache->valid=false; if(!swat_character_view_sample(view,clips[bank],time) ||
            !swat_character_support_grip(asset,grip) || !elbow_carriers(asset)) continue;
        size_t count=(size_t)swat_character_info(asset).nodes*16;
        if(cache->count!=count) {
            free(cache->matrices); free(cache->first_person_matrices);
            cache->matrices=calloc(count,sizeof(float)); cache->first_person_matrices=calloc(count,sizeof(float)); cache->count=count;
        }
        if(!cache->matrices || !cache->first_person_matrices ||
           !swat_character_capture_pose(asset,cache->first_person_matrices,count)) continue;
        cache->first_person_inverse_gun=MatrixInvert(compose(node(asset,"Prop_Rifle"),matrix(rifle_joint)));
        Matrix heading=MatrixRotateY(-actor->controller.yaw);
        Matrix root=compose(MatrixTranslate(feet.x,feet.y,feet.z),compose(heading,matrix(bank<2 ? fits[bank] : movement_fits[bank-2])));
        float turn=0;
        if(cycle_metres[bank]>0 && moving) turn=swat_angle(direction-travel_yaw[bank]);
        // Rotate only the difference from each bank's actual source travel;
        // forward/backward/diagonal fallbacks still counter-aim the torso.
        Vector3 hip=origin(node(asset,"mixamorig:Hips"));
        Matrix turn_delta=compose(MatrixTranslate(hip.x,hip.y,hip.z),compose(MatrixRotateY(-turn),MatrixTranslate(-hip.x,-hip.y,-hip.z)));
        Matrix leg_targets[2]={compose(turn_delta,node(asset,"mixamorig:LeftFoot")),compose(turn_delta,node(asset,"mixamorig:RightFoot"))};
        // Phase-zero hip height relative to the fixed sole origin: Ready
        // .93581725 m, both authored crouches .6669743 m. Apply only the
        // remaining controller crouch drop, keeping the sampled foot targets.
        float authored_drop=bank==SWAT_CHARACTER_CROUCH_READY ? .268842936f : bank==SWAT_CHARACTER_CROUCH_WALK ? .268843055f : 0;
        float drop=fmaxf(0,.55f*(actor->controller.body.standHeight-actor->controller.body.totalHeight)-authored_drop);
        Matrix lowered=compose(MatrixTranslate(0,-drop,0),turn_delta);
        if(!transform(asset,"mixamorig:Hips",lowered)) continue;
        const char* props[]={"Prop_Rifle","Prop_Magazine_A","Prop_Magazine_B"};
        bool ok=true; for(int p=0;p<3;p++) ok=transform(asset,props[p],lowered) && ok;
        ok=limb(asset,"mixamorig:LeftUpLeg","mixamorig:LeftLeg","mixamorig:LeftFoot",leg_targets[0]) && ok;
        ok=limb(asset,"mixamorig:RightUpLeg","mixamorig:RightLeg","mixamorig:RightFoot",leg_targets[1]) && ok;
        SwatPose achieved=swat_pose(&actor->controller,&actor->arsenal);
        Matrix inverse_root=MatrixInvert(root),source_gun=compose(node(asset,"Prop_Rifle"),matrix(rifle_joint));
        Matrix target_gun=compose(inverse_root,swat_weapon_art_transform(&achieved));
        Matrix gun_delta=compose(target_gun,MatrixInvert(source_gun));
        Matrix hand_targets[2]={compose(gun_delta,node(asset,"mixamorig:LeftHand")),compose(gun_delta,node(asset,"mixamorig:RightHand"))};
        // Keep torso pitch bounded; the limb solve handles remaining weapon
        // pitch/ready motion without rotating the entire body through the floor.
        SwatPose bounded=achieved;
        float body_pitch=Clamp(asinf(Clamp(achieved.weapon_forward.y,-1,1)),-20*SWAT_RAD,20*SWAT_RAD);
        bounded.weapon_forward=swat_direction(actor->controller.yaw,body_pitch);
        bounded.weapon_up=swat_normalize(b3Cross(achieved.right,bounded.weapon_forward));
        bounded.weapon_forward=swat_normalize(b3Cross(bounded.weapon_up,achieved.right));
        Matrix torso_gun=compose(inverse_root,swat_weapon_art_transform(&bounded));
        Matrix torso_delta=compose(torso_gun,MatrixInvert(source_gun));
        // Aim rotates the torso around its sampled spine joint. Translating
        // the whole upper subtree to a gun target stretches the abdomen when
        // crouch/camera height differ, even though the arm IK is length-safe.
        torso_delta.m12=torso_delta.m13=torso_delta.m14=0;
        Vector3 spine=origin(node(asset,"mixamorig:Spine2"));
        torso_delta=compose(MatrixTranslate(spine.x,spine.y,spine.z),compose(torso_delta,MatrixTranslate(-spine.x,-spine.y,-spine.z)));
        ok=transform(asset,"mixamorig:Spine2",torso_delta) && ok;
        for(int p=0;p<3;p++) ok=transform(asset,props[p],gun_delta) && ok;
        ok=limb(asset,"mixamorig:LeftArm","mixamorig:LeftForeArm","mixamorig:LeftHand",hand_targets[0]) && ok;
        ok=limb(asset,"mixamorig:RightArm","mixamorig:RightForeArm","mixamorig:RightHand",hand_targets[1]) && ok;
        if(!ok || !elbow_carriers(asset) || !swat_character_finalize_pose(asset)) continue;
        if(!cache->matrices || !swat_character_capture_pose(asset,cache->matrices,count)) continue;
        for(int m=0;m<view->model.meshCount;m++) {
            const SwatArtMesh* source=swat_character_mesh(asset,m); bool visible=source->visible;
            if(!strcmp(source->node_name,"Removed magazine")) visible=weapon->magazine_seated && (!reload || time<.88);
            if(!strcmp(source->node_name,"Fresh magazine")) visible=reload && time>=1.4 && (time<3.65 || weapon->magazine_seated);
            // Authority retains rounds/magazines; the authored empty-mag fall
            // must not invent a persistent world drop or refill carrier slots.
            if(strstr(source->node_name,"sleeve")) visible=false;
            cache->visible[m]=(unsigned char)visible;
        }
        cache->root=root; cache->feet=feet; cache->source_time=time; cache->bank=bank; cache->tick=sim->tick; cache->episode=sim->episode; cache->signature=signature; cache->valid=true;
        runtime->preparations++;
        double elapsed=GetTime()-started; runtime->prepare_seconds+=elapsed; runtime->prepare_max_seconds=fmax(runtime->prepare_max_seconds,elapsed);
    }
}
bool swat_character_runtime_draw(SwatCharacterRuntime* runtime,int actor,SwatLighting* lighting) {
    if(!runtime || actor<0 || actor>=SWAT_MAX_ACTORS || !runtime->actors[actor].valid) return false;
    SwatCharacterActorPose* cache=&runtime->actors[actor]; SwatCharacterView* view=&runtime->banks[cache->bank];
    if(outside_camera(cache->feet)) return true;
    if(!swat_character_restore_pose(view->asset,cache->matrices,cache->count)) return false;
    memcpy(view->visible,cache->visible,(size_t)view->model.meshCount);
    swat_character_view_draw(view,lighting,cache->root); runtime->draws++; return true;
}
bool swat_character_runtime_draw_first_person(SwatCharacterRuntime* runtime,int actor,SwatLighting* lighting,const SwatPose* presentation) {
    if(!runtime || actor<0 || actor>=SWAT_MAX_ACTORS || !runtime->actors[actor].valid) return false;
    SwatCharacterActorPose* cache=&runtime->actors[actor]; SwatCharacterView* view=&runtime->banks[cache->bank];
    // First-person shoulders follow the camera with the authored grip. Reusing
    // world-body IK here leaves upper sleeves attached to upright shoulders
    // while the gun turns through +/-85 degrees, sweeping them across the eye.
    // Align the entire sampled arm rig to the measured rifle, without changing
    // bone lengths, source weights, world-body pose or authoritative weapon.
    // Both source and world poses are cached during prepare: extra render
    // passes never resample a clip or advance its phase.
    if(!swat_character_restore_pose(view->asset,cache->first_person_matrices,cache->count)) return false;
    memcpy(view->visible,cache->visible,(size_t)view->model.meshCount);
    Matrix root=compose(swat_weapon_art_transform(presentation),cache->first_person_inverse_gun);
    swat_character_view_draw_first_person(view,lighting,root);
    // Shared banks may be used by a subsequent feed/world draw in this frame.
    bool restored=swat_character_restore_pose(view->asset,cache->matrices,cache->count);
    runtime->draws++; return restored;
}
void swat_character_runtime_close(SwatCharacterRuntime* runtime) {
    if(!runtime) return;
    for(int i=0;i<SWAT_CHARACTER_BANKS;i++) swat_character_view_close(&runtime->banks[i]);
    for(int i=0;i<2;i++) {
        if(runtime->diffuse[i].id) UnloadTexture(runtime->diffuse[i]);
        if(runtime->normal[i].id) UnloadTexture(runtime->normal[i]);
        if(runtime->specular[i].id) UnloadTexture(runtime->specular[i]);
        if(runtime->gloss[i].id) UnloadTexture(runtime->gloss[i]);
    }
    if(runtime->emissive.id) UnloadTexture(runtime->emissive);
    if(runtime->orm.id) UnloadTexture(runtime->orm);
    for(int i=0;i<SWAT_MAX_ACTORS;i++) { free(runtime->actors[i].matrices); free(runtime->actors[i].first_person_matrices); }
    free(runtime);
}
