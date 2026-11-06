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
static Matrix bank_fit(int bank) { return matrix(bank<2 ? fits[bank] : movement_fits[bank-2]); }
// Blend local TRS, not skin palettes or world matrices: quaternion interpolation
// preserves rigid rotations and bone lengths. Interrupted transitions start at
// the last displayed pose. Feet/yaw still follow authority immediately.
static bool transition(SwatCharacterRuntime* runtime,SwatCharacterActorPose* cache,
        SwatCharacterAsset* asset,int bank,bool restart,double dt) {
    size_t count=(size_t)swat_character_info(asset).nodes*16;
    if(restart) {
        SwatCharacterAsset* previous=runtime->banks[cache->bank].asset;
        if(!previous) {cache->transition_elapsed=.22;return true;}
        float aligned[count];
        Matrix bridge=compose(MatrixInvert(bank_fit(bank)),bank_fit(cache->bank));
        for(size_t i=0;i<count/16;i++) {
            int old=swat_character_find_node(previous,swat_character_node_name(asset,(int)i));
            Matrix world=old>=0 && (size_t)(old+1)*16<=cache->count ?
                compose(bridge,matrix(cache->matrices+old*16)) : matrix(swat_character_node_matrix(asset,(int)i));
            memcpy(aligned+i*16,MatrixToFloatV(world).v,16*sizeof(float));
        }
        if(cache->transition_count!=count) {
            free(cache->transition_locals);cache->transition_locals=calloc(count,sizeof(float));cache->transition_count=count;
        }
        if(!cache->transition_locals)return false;
        for(size_t i=0;i<count/16;i++) {
            int parent=swat_character_parent_node(asset,(int)i);
            Matrix local=matrix(aligned+i*16);
            if(parent>=0)local=compose(MatrixInvert(matrix(aligned+parent*16)),local);
            memcpy(cache->transition_locals+i*16,MatrixToFloatV(local).v,16*sizeof(float));
        }
        cache->transition_elapsed=0;
    }
    if(cache->transition_elapsed>=.22 || cache->transition_count!=count)return true;
    cache->transition_elapsed=fmin(.22,cache->transition_elapsed+dt);
    float t=(float)(cache->transition_elapsed/.22);t=t*t*(3-2*t);
    float locals[count];
    for(size_t i=0;i<count/16;i++) {
        int parent=swat_character_parent_node(asset,(int)i);
        Matrix target=matrix(swat_character_node_matrix(asset,(int)i));
        if(parent>=0)target=compose(MatrixInvert(matrix(swat_character_node_matrix(asset,parent))),target);
        Vector3 a,sa,b,sb;Quaternion qa,qb;
        MatrixDecompose(matrix(cache->transition_locals+i*16),&a,&qa,&sa);
        MatrixDecompose(target,&b,&qb,&sb);
        Vector3 p=Vector3Lerp(a,b,t),scale=Vector3Lerp(sa,sb,t);
        // A skeletal offset is a rigid bone. Blend its direction while
        // retaining the current source length; vector lerp alone shortens it.
        const char* name=swat_character_node_name(asset,(int)i);
        const char* parent_name=parent>=0?swat_character_node_name(asset,parent):NULL;
        if(name && parent_name && !strncmp(name,"mixamorig:",10) && !strncmp(parent_name,"mixamorig:",10) && Vector3LengthSqr(p)>1e-12f)
            p=Vector3Scale(p,Vector3Length(b)/Vector3Length(p));
        Matrix blended=compose(MatrixTranslate(p.x,p.y,p.z),compose(QuaternionToMatrix(QuaternionSlerp(qa,qb,t)),MatrixScale(scale.x,scale.y,scale.z)));
        memcpy(locals+i*16,MatrixToFloatV(blended).v,16*sizeof(float));
    }
    return swat_character_compose_pose(asset,locals,count);
}
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
static bool limb_pole(SwatCharacterAsset* asset,const char* upper,const char* lower,const char* end,Matrix target,const Vector3* hint);
static bool limb(SwatCharacterAsset* asset,const char* upper,const char* lower,const char* end,Matrix target) {
    return limb_pole(asset,upper,lower,end,target,NULL);
}
// An explicit elbow hint keeps the bend plane stable across clips. The
// sampled elbow decides the plane otherwise, and it flips between banks.
static bool limb_pole(SwatCharacterAsset* asset,const char* upper,const char* lower,const char* end,Matrix target,const Vector3* hint) {
    Vector3 a=origin(node(asset,upper)),b=origin(node(asset,lower)),c=origin(node(asset,end)),goal=origin(target);
    float ab=Vector3Distance(a,b),bc=Vector3Distance(b,c),distance=Vector3Distance(a,goal);
    if(ab<1e-5f || bc<1e-5f || distance<1e-5f) return true;
    Vector3 direction=Vector3Scale(Vector3Subtract(goal,a),1/distance);
    float reach=Clamp(distance,fabsf(ab-bc)+1e-5f,ab+bc-1e-5f);
    float along=(ab*ab-bc*bc+reach*reach)/(2*reach),height=sqrtf(fmaxf(0,ab*ab-along*along));
    Vector3 bend=hint ? *hint : Vector3Subtract(b,a);
    Vector3 pole=Vector3Subtract(bend,Vector3Scale(direction,Vector3DotProduct(bend,direction)));
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

// Measured chest frame: spine-to-neck up, left-to-right shoulder right.
// Columns are (forward, up, right), the same order as the rifle frame.
static Matrix chest_basis(SwatCharacterAsset* asset) {
    Vector3 up=Vector3Normalize(Vector3Subtract(origin(node(asset,"mixamorig:Neck")),origin(node(asset,"mixamorig:Spine2"))));
    Vector3 right=Vector3Normalize(Vector3Subtract(origin(node(asset,"mixamorig:RightArm")),origin(node(asset,"mixamorig:LeftArm"))));
    Vector3 forward=Vector3Normalize(Vector3CrossProduct(up,right)); right=Vector3CrossProduct(forward,up);
    Vector3 o=origin(node(asset,"mixamorig:Spine2"));
    return (Matrix){.m0=forward.x,.m1=forward.y,.m2=forward.z,.m4=up.x,.m5=up.y,.m6=up.z,
        .m8=right.x,.m9=right.y,.m10=right.z,.m12=o.x,.m13=o.y,.m14=o.z,.m15=1};
}
static Matrix rotation_only(Matrix m) { m.m12=m.m13=m.m14=0; return m; }
static bool rotate_about(SwatCharacterAsset* asset,const char* name,Quaternion q) {
    Vector3 p=origin(node(asset,name));
    return transform(asset,name,compose(MatrixTranslate(p.x,p.y,p.z),compose(QuaternionToMatrix(q),MatrixTranslate(-p.x,-p.y,-p.z))));
}
// Upper-body aim layer. The chest is turned to `target` (rotation columns
// forward/up/right) in three parts over Spine, Spine1 and Spine2, so the bend
// reads as a spine curve rather than a hinge at the sternum. The neck then
// finishes part of the remaining look toward `look`. Rotations only: the
// abdomen never stretches.
// Fitted by tools/officer_fit.c (score 514 -> 67: cheek weld, eye on the
// sight line, stock pad on the shoulder, tucked elbows). Re-fit with
// `make officer-fit` after rig or rifle changes.
SwatAimTuning swat_aim_tuning={
    .pocket={.1086f,.2000f,.1134f},.eye_weight=.697f,.eye_relief=.2516f,.blade_deg=39.47f,.torso_share=1,.torso_max_deg=50,
    .head_tip_deg=12.62f,.head_cant_deg=18.35f,.neck_share=.493f,.clavicle_share=.340f,
    .elbow_hint={{-.9955f,-1.5f,.1249f},{-.1275f,-.3003f,1.5f}},.slide_max=.1627f,
    .stride_scale=.72f,.walk_raise=.16f};
static Vector3 chest_vector(Matrix chest,const float* v) {
    return Vector3Add(Vector3Scale((Vector3){chest.m0,chest.m1,chest.m2},v[0]),
        Vector3Add(Vector3Scale((Vector3){chest.m4,chest.m5,chest.m6},v[1]),Vector3Scale((Vector3){chest.m8,chest.m9,chest.m10},v[2])));
}
// Right eye, measured on the F head mesh: 11.5 cm up the skull axis from the
// Head joint, 8.5 cm forward of it and 3 cm right (goggle lens inner face).
static Vector3 right_eye(SwatCharacterAsset* asset,Vector3 look) {
    Vector3 h=origin(node(asset,"mixamorig:Head"));
    Vector3 up=Vector3Normalize(Vector3Subtract(origin(node(asset,"mixamorig:HeadTop_End")),h));
    Vector3 f=Vector3Normalize(Vector3Subtract(look,Vector3Scale(up,Vector3DotProduct(look,up))));
    Vector3 r=Vector3CrossProduct(f,up);
    return Vector3Add(h,Vector3Add(Vector3Scale(up,.115f),Vector3Add(Vector3Scale(f,.085f),Vector3Scale(r,.03f))));
}
static bool aim_layer(SwatCharacterAsset* asset,Matrix target,Vector3 look,Vector3 world_up) {
    const SwatAimTuning* k=&swat_aim_tuning;
    const char* spine[]={"mixamorig:Spine","mixamorig:Spine1","mixamorig:Spine2"};
    const float share[]={1.f/3,.5f,1};
    for(int i=0;i<3;i++) {
        if(swat_character_find_node(asset,spine[i])<0) continue;
        Matrix delta=compose(rotation_only(target),MatrixTranspose(rotation_only(chest_basis(asset))));
        if(!rotate_about(asset,spine[i],QuaternionSlerp(QuaternionIdentity(),QuaternionFromMatrix(delta),share[i]))) return false;
    }
    // Head: skull axis (Head to HeadTop_End) stands perpendicular to the
    // look direction, tipped 10 degrees toward it for the sight picture.
    // Clips hunch and lift the head differently; aiming the skull itself,
    // not the chest-relative neck, removes that per-clip tilt. Split between
    // the neck and head joints.
    if(Vector3LengthSqr(look)<1e-10f || swat_character_find_node(asset,"mixamorig:HeadTop_End")<0) return true;
    look=Vector3Normalize(look);
    Vector3 up=Vector3Subtract(world_up,Vector3Scale(look,Vector3DotProduct(world_up,look)));
    if(Vector3LengthSqr(up)<1e-6f) return true;
    up=Vector3Normalize(up); Vector3 side=Vector3CrossProduct(look,up);
    float tip=k->head_tip_deg*SWAT_RAD,cant=k->head_cant_deg*SWAT_RAD;
    up=Vector3Add(Vector3Scale(up,cosf(tip)),Vector3Scale(look,sinf(tip)));
    up=Vector3Normalize(Vector3Add(Vector3Scale(up,cosf(cant)),Vector3Scale(side,sinf(cant))));
    const char* joints[]={"mixamorig:Neck","mixamorig:Head"}; const float part[]={k->neck_share,1};
    for(int i=0;i<2;i++) {
        Vector3 skull=Vector3Subtract(origin(node(asset,"mixamorig:HeadTop_End")),origin(node(asset,"mixamorig:Head")));
        if(Vector3LengthSqr(skull)<1e-10f) return false;
        Quaternion q=QuaternionFromVector3ToVector3(Vector3Normalize(skull),up);
        if(!rotate_about(asset,joints[i],QuaternionSlerp(QuaternionIdentity(),q,part[i]))) return false;
    }
    return true;
}

// Third-person rifle: the stock sits in the posed chest's pocket and the
// barrel follows the authoritative aim direction. The authoritative shoulder
// point pivots about the eye and drifts 30-45 cm from the mesh shoulder at
// steep pitch and in crouch; holding the rifle there forced the arms through
// the reach limit and took the support hand off the handguard.
// With eye_weight > 0 the rifle also moves toward the placement that puts the
// sight line through the mesh's right eye (cheek weld), and the right
// clavicle lifts the shoulder toward the resulting stock pad.
static Matrix pocket_rifle(SwatCharacterAsset* asset,Matrix aim,Vector3 look) {
    const SwatAimTuning* k=&swat_aim_tuning;
    Matrix chest=chest_basis(asset); Vector3 p=Vector3Add((Vector3){chest.m12,chest.m13,chest.m14},chest_vector(chest,k->pocket));
    Vector3 barrel={aim.m0,aim.m1,aim.m2},gun_up={aim.m4,aim.m5,aim.m6};
    Vector3 welded=Vector3Subtract(right_eye(asset,look),Vector3Add(Vector3Scale(barrel,k->eye_relief),Vector3Scale(gun_up,.12474874f)));
    p=Vector3Lerp(p,welded,k->eye_weight);
    aim=rotation_only(aim); aim.m12=p.x; aim.m13=p.y; aim.m14=p.z; return aim;
}
// Presentation-only recoil kick: the rifle drives straight back into the
// shoulder pocket (up to 2.5 cm) and recovers over 100 ms; the hands follow
// by IK. The authoritative recoil (view pitch/yaw) is already in the aim.
static Matrix kick_rifle(Matrix gun,int shot_age) {
    const int ticks=6; if(shot_age<0 || shot_age>=ticks) return gun;
    float t=(float)shot_age/ticks,back=.025f*(1-t)*(1-t);
    gun.m12-=back*gun.m0; gun.m13-=back*gun.m1; gun.m14-=back*gun.m2; return gun;
}
static bool lift_clavicle(SwatCharacterAsset* asset,Matrix gun) {
    float share=swat_aim_tuning.clavicle_share; if(share<=0 || swat_character_find_node(asset,"mixamorig:RightShoulder")<0) return true;
    Vector3 pivot=origin(node(asset,"mixamorig:RightShoulder")),joint=origin(node(asset,"mixamorig:RightArm"));
    Matrix chest=chest_basis(asset); Vector3 r={chest.m8,chest.m9,chest.m10};
    Vector3 goal=Vector3Add((Vector3){gun.m12,gun.m13,gun.m14},Vector3Scale(r,.05f));
    Vector3 from=Vector3Subtract(joint,pivot),to=Vector3Subtract(goal,pivot);
    if(Vector3LengthSqr(from)<1e-10f || Vector3LengthSqr(to)<1e-10f) return true;
    Quaternion q=QuaternionFromVector3ToVector3(Vector3Normalize(from),Vector3Normalize(to));
    return rotate_about(asset,"mixamorig:RightShoulder",QuaternionSlerp(QuaternionIdentity(),q,fminf(share,1)));
}
static Matrix blend_rigid(Matrix a,Matrix b,float t) {
    Vector3 pa,pb,sa,sb; Quaternion qa,qb; MatrixDecompose(a,&pa,&qa,&sa); MatrixDecompose(b,&pb,&qb,&sb);
    Vector3 p=Vector3Lerp(pa,pb,t); return compose(MatrixTranslate(p.x,p.y,p.z),QuaternionToMatrix(QuaternionSlerp(qa,qb,t)));
}
// Rifle and both hands. `grip` 1 holds the measured Ready grips on the rifle;
// 0 restores the clip's own hand motion relative to the rifle (magazine work).
// Elbows bend toward fixed chest-relative hints: down and out for the firing
// arm, down and slightly in for the support arm.
static bool rifle_and_hands(SwatCharacterRuntime* runtime,SwatCharacterAsset* asset,Matrix target_gun,
        const Matrix clip_hands[2],float grip) {
    Matrix source_gun=compose(node(asset,"Prop_Rifle"),matrix(rifle_joint));
    Matrix gun_delta=compose(target_gun,MatrixInvert(source_gun));
    const char* props[]={"Prop_Rifle","Prop_Magazine_A","Prop_Magazine_B"};
    bool ok=true; for(int p=0;p<3;p++) ok=transform(asset,props[p],gun_delta) && ok;
    ok=lift_clavicle(asset,target_gun) && ok;
    Matrix chest=chest_basis(asset);
    Vector3 hints[2]={chest_vector(chest,swat_aim_tuning.elbow_hint[0]),chest_vector(chest,swat_aim_tuning.elbow_hint[1])};
    const char* arm[2][3]={{"mixamorig:LeftArm","mixamorig:LeftForeArm","mixamorig:LeftHand"},
                           {"mixamorig:RightArm","mixamorig:RightForeArm","mixamorig:RightHand"}};
    Vector3 barrel={target_gun.m0,target_gun.m1,target_gun.m2};
    for(int h=0;h<2;h++) {
        Matrix held=compose(target_gun,runtime->grip_local[h]),authored=compose(target_gun,clip_hands[h]);
        if(h==0) {
            // The support hand slides back along the handguard, up to 16 cm,
            // until the arm reaches it without locking straight.
            Vector3 a=origin(node(asset,arm[0][0])),b=origin(node(asset,arm[0][1])),c=origin(node(asset,arm[0][2]));
            float reach=.97f*(Vector3Distance(a,b)+Vector3Distance(b,c));
            for(int cm=0;cm<(int)(100*swat_aim_tuning.slide_max+.5f) && Vector3Distance(a,origin(held))>reach;cm++) {
                held.m12-=.01f*barrel.x; held.m13-=.01f*barrel.y; held.m14-=.01f*barrel.z;
            }
        }
        ok=limb_pole(asset,arm[h][0],arm[h][1],arm[h][2],blend_rigid(authored,held,grip),&hints[h]) && ok;
    }
    return ok;
}

// Clamber overlay weights from the move's progress t in [0,1]: the support
// hand plants on the obstacle edge while rising and crossing, the chest leans
// over it, and the legs tuck while crossing.
static float ramp(float a,float b,float t){t=Clamp((t-a)/(b-a),0,1);return t*t*(3-2*t);}
typedef struct { float hand,lean,tuck; } MantleWeights;
static MantleWeights mantle_weights(const SwatMantle* m) {
    if(!m->active || m->total<=0) return (MantleWeights){0};
    float t=Clamp((float)m->tick/(float)m->total,0,1);
    return (MantleWeights){.hand=ramp(0,.15f,t)*(1-ramp(.62f,.80f,t)),.lean=ramp(.08f,.38f,t)*(1-ramp(.66f,.92f,t)),
        .tuck=ramp(.28f,.45f,t)*(1-ramp(.66f,.88f,t))};
}
// Left palm on the obstacle's near top edge, a little left of the line of travel.
static bool mantle_plant(SwatCharacterAsset* asset,Matrix inverse_root,const SwatMantle* m,float yaw,float radius,float weight) {
    if(weight<=0) return true;
    b3CosSin h=b3ComputeCosSin(yaw);b3Vec3 f=swat_v(h.cosine,0,h.sine),r=swat_v(-h.sine,0,h.cosine);
    b3Pos edge=b3OffsetPos(m->path[1],swat_add(swat_mul(f,radius+.04f),swat_mul(r,-.16f)));edge.y=m->path[1].y-.10f+.03f;
    Vector3 local=Vector3Transform((Vector3){(float)edge.x,(float)edge.y,(float)edge.z},inverse_root);
    Matrix current=node(asset,"mixamorig:LeftHand"),target=current;
    Vector3 p=Vector3Lerp(origin(current),local,weight);target.m12=p.x;target.m13=p.y;target.m14=p.z;
    const float hint_axes[3]={-.3f,-1,-.4f};Vector3 hint=chest_vector(chest_basis(asset),hint_axes);
    return limb_pole(asset,"mixamorig:LeftArm","mixamorig:LeftForeArm","mixamorig:LeftHand",target,&hint);
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
        direction=Vector3RotateByQuaternion(sampled[i],QuaternionSlerp(QuaternionIdentity(),rotation,amount*.3f));
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
static SwatCharacterRuntime* open_runtime(const SwatWeaponArt* weapons,const char* directory,bool allow_gear);
SwatCharacterRuntime* swat_character_runtime_open(const SwatWeaponArt* weapons) {
    const char* enabled=getenv("SWAT_CHARACTER_ART"); if(enabled && !strcmp(enabled,"0")) return NULL;
    char directory[2048]; const char* custom=getenv("SWAT_CHARACTER_ASSETS");
    if(custom && strlen(custom)>=sizeof(directory)) return NULL;
    if(custom && *custom) snprintf(directory,sizeof(directory),"%s",custom);
    else { snprintf(directory,sizeof(directory),"%sassets/characters",GetApplicationDirectory()); if(!DirectoryExists(directory)) snprintf(directory,sizeof(directory),"ocean/swat/assets/characters"); }
    return open_runtime(weapons,directory,true);
}
SwatCharacterRuntime* swat_character_runtime_open_body(const SwatWeaponArt* weapons,const char* directory,int variant,int variants) {
    const char* enabled=getenv("SWAT_CHARACTER_ART"); if(enabled && !strcmp(enabled,"0")) return NULL;
    SwatCharacterRuntime* runtime=open_runtime(weapons,directory,false);
    if(runtime) { runtime->suspect_variant=variant; runtime->suspect_variants=variants; }
    return runtime;
}
// Per-primitive body textures from body_textures.txt ("set diffuse normal",
// "-" for none) in a conformed body directory; false when absent.
static bool body_textures(SwatCharacterRuntime* runtime,const char* directory) {
    char path[4096]; snprintf(path,sizeof(path),"%s/body_textures.txt",directory);
    char* text=FileExists(path) ? LoadFileText(path) : NULL; if(!text) return false;
    for(char* line=strtok(text,"\n");line;line=strtok(NULL,"\n")) {
        int set; char diffuse[256],normal[256];
        if(sscanf(line,"%d %255s %255s",&set,diffuse,normal)!=3 || set<0 || set>=SWAT_BODY_SETS) continue;
        if(strcmp(diffuse,"-")) runtime->diffuse[set]=texture(directory,diffuse);
        if(strcmp(normal,"-")) runtime->normal[set]=texture(directory,normal);
    }
    UnloadFileText(text); return true;
}
static SwatCharacterRuntime* open_runtime(const SwatWeaponArt* weapons,const char* directory,bool allow_gear) {
    char path[4096],error[256];
    snprintf(path,sizeof(path),"%s/ready.glb",directory); if(!FileExists(path)) { TraceLog(LOG_INFO,"SWAT: private character absent; procedural actors"); return NULL; }
    SwatCharacterRuntime* runtime=calloc(1,sizeof(*runtime)); if(!runtime) return NULL;
    runtime->suspect_variant=-1;
    char geometry[4096]; snprintf(geometry,sizeof(geometry),"%s/upper_gear_f",directory);
    const char* gear=getenv("SWAT_CHARACTER_GEAR");
    snprintf(path,sizeof(path),"%s/ready.glb",geometry);
    bool use_gear=allow_gear && (!gear || strcmp(gear,"0")) && FileExists(path);
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
        const char* required[]={"mixamorig:Neck","mixamorig:Hips","mixamorig:LeftUpLeg","mixamorig:LeftLeg","mixamorig:LeftFoot","mixamorig:RightUpLeg","mixamorig:RightLeg","mixamorig:RightFoot","mixamorig:LeftArm","mixamorig:LeftForeArm","mixamorig:LeftHand","mixamorig:RightArm","mixamorig:RightForeArm","mixamorig:RightHand","Prop_Magazine_A","Prop_Magazine_B"};
        bool valid=true;
        if(use_gear && (info.joints!=72 || swat_character_find_node(runtime->banks[i].asset,"Gear_Elbow_L")<0 ||
                       swat_character_find_node(runtime->banks[i].asset,"Gear_Elbow_R")<0)) valid=false;
        for(size_t j=0;j<sizeof(required)/sizeof(*required);j++) if(swat_character_find_node(runtime->banks[i].asset,required[j])<0) valid=false;
        double duration=swat_character_clip_duration(runtime->banks[i].asset,0);
        valid=valid && (i==SWAT_CHARACTER_CROUCH_READY ? duration==0 : duration>0);
        if(!valid) { if(i<2) goto failed; swat_character_view_close(&runtime->banks[i]); }
    }
    // A fixed entry frame preserves the authored rifle turn/translation during
    // reload. Inverting the current frame would cancel that motion every draw.
    if(!swat_character_view_sample(&runtime->banks[0],clips[0],0)) goto failed;
    runtime->reload_inverse_gun=MatrixInvert(compose(node(runtime->banks[0].asset,"Prop_Rifle"),matrix(rifle_joint)));
    runtime->grip_local[0]=compose(runtime->reload_inverse_gun,node(runtime->banks[0].asset,"mixamorig:LeftHand"));
    runtime->grip_local[1]=compose(runtime->reload_inverse_gun,node(runtime->banks[0].asset,"mixamorig:RightHand"));
    bool conformed=body_textures(runtime,directory);
    if(!conformed) { runtime->diffuse[0]=texture(directory,"Ch15_1001_Diffuse.png"); runtime->diffuse[1]=texture(directory,"Ch15_1002_Diffuse.png"); }
    if(!conformed) runtime->emissive=texture(directory,"Ch15_1002_Emissive.png");
    const char* finish=getenv("SWAT_CHARACTER_FINISH");
    if(!conformed && (!finish || strcmp(finish,"0"))) for(int i=0;i<2;i++) {
        char file[128]; snprintf(file,sizeof(file),"Ch15_100%d_Specular.png",i+1); runtime->specular[i]=texture(directory,file);
        snprintf(file,sizeof(file),"Ch15_100%d_Glossiness.png",i+1); runtime->gloss[i]=texture(directory,file);
    }
    const char* normals=getenv("SWAT_CHARACTER_NORMALS");
    if(!conformed && (!normals || strcmp(normals,"0"))) { runtime->normal[0]=texture(directory,"Ch15_1001_Normal.png"); runtime->normal[1]=texture(directory,"Ch15_1002_Normal.png"); }
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
            int set=source->primitive; if(set>=(conformed ? SWAT_BODY_SETS : 2)) goto failed;
            if(runtime->diffuse[set].id) { material->maps[MATERIAL_MAP_ALBEDO].texture=runtime->diffuse[set]; material->maps[MATERIAL_MAP_ALBEDO].color=WHITE; }
            material->maps[MATERIAL_MAP_ROUGHNESS].texture=runtime->orm; material->maps[MATERIAL_MAP_ROUGHNESS].value=conformed ? .72f : .78f; material->maps[MATERIAL_MAP_METALNESS].value=0;
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
        bool role=runtime->suspect_variant<0 ? (actor->role==SWAT_OFFICER || actor->role==SWAT_SNIPER) :
            actor->role==SWAT_SUSPECT && !actor->gear.stunned_ticks && swat_body_variant(sim,i,runtime->suspect_variants)==runtime->suspect_variant;
        bool eligible=i<sim->actor_count && actor->present && actor->alive && role &&
            !actor->gear.surrendered && !actor->gear.restrained && actor->arsenal.active==0 && actor->arsenal.primary==0;
        if(!eligible) { cache->valid=false; continue; }
        b3Pos feet=swat_body_feet_position(&actor->controller.body);
        b3Vec3 velocity=b3Body_GetLinearVelocity(actor->controller.body.body); float speed=hypotf(velocity.x,velocity.z);
        uint64_t signature=fingerprint(UINT64_C(14695981039346656037),&actor->controller,sizeof(actor->controller));
        signature=fingerprint(signature,&actor->arsenal,sizeof(actor->arsenal)); signature=fingerprint(signature,&feet,sizeof(feet)); signature=fingerprint(signature,&velocity,sizeof(velocity));
        if(cache->valid && cache->tick==sim->tick && cache->episode==sim->episode && cache->signature==signature) continue;
        bool reset=!cache->valid || cache->episode!=sim->episode || sim->tick<cache->tick;
        double dt=reset ? 0 : (sim->tick-cache->tick)/60.0;
        double travel=reset ? 0 : hypot((double)feet.x-cache->feet.x,(double)feet.z-cache->feet.z);
        if(travel>6*dt+.25) {travel=0;reset=true;} // teleport/restore, no invented travel
        cache->distance=reset ? 0 : cache->distance+travel;
        const SwatWeapon* weapon=&actor->arsenal.slots[0]; bool reload=weapon->reload_remaining>0;
        bool moving=actor->controller.body.onGround && speed>.12f;
        bool crouch=actor->controller.body.totalHeight<actor->controller.body.standHeight-.01f;
        float direction=moving ? swat_angle(atan2f(velocity.z,velocity.x)-actor->controller.yaw) : 0;
        int locomotion=SWAT_CHARACTER_READY;
        if(crouch) locomotion=moving ? SWAT_CHARACTER_CROUCH_WALK : SWAT_CHARACTER_CROUCH_READY;
        else if(moving) locomotion=fabsf(sinf(direction))>fabsf(cosf(direction)) ? (direction<0 ? SWAT_CHARACTER_LEFT : SWAT_CHARACTER_RIGHT) :
            cosf(direction)<0 ? SWAT_CHARACTER_BACKWARD : SWAT_CHARACTER_WALK;
        if(!runtime->banks[locomotion].asset) locomotion=moving ? SWAT_CHARACTER_WALK : SWAT_CHARACTER_READY;
        // The reload clip is authored standing still. While moving, its upper
        // body is kept and the legs come from the locomotion bank at the
        // travelled phase, so the feet step instead of sliding.
        int bank=reload ? SWAT_CHARACTER_READY : locomotion,legs=reload && moving ? locomotion : bank;
        cache->phase=reset ? 0 : cache->phase;
        // Stride warping shortens each step; the cycle covers proportionally
        // less ground, so the phase advances faster and planted feet stay put.
        if(cycle_metres[legs]>0) cache->phase=fmod(cache->phase+travel/(cycle_metres[legs]*swat_aim_tuning.stride_scale),1.0);
        double time=reload ? swat_character_reload_time(weapon) : cycle_metres[bank]>0 ? cache->phase*swat_character_clip_duration(runtime->banks[bank].asset,0) : 0;
        SwatCharacterView* view=&runtime->banks[bank]; SwatCharacterAsset* asset=view->asset;
        bool changed=!reset && (bank!=cache->bank || crouch!=cache->crouching || reload!=cache->reloading);
        if(reset)cache->transition_elapsed=.22;
        double started=GetTime();
        // Release the holding correction for the authored magazine handling;
        // short endpoint fades avoid snapping between the source and hold pose.
        float grip=reload ? Clamp(1-(float)time/.12f,0,1)+Clamp(((float)time-5.82f)/.18f,0,1) : 1;
        cache->valid=false; if(!swat_character_view_sample(view,clips[bank],time) ||
            !swat_character_support_grip(asset,grip) || !elbow_carriers(asset)) continue;
        size_t count=(size_t)swat_character_info(asset).nodes*16;
        if(cache->count!=count) {
            free(cache->first_person_matrices);cache->first_person_matrices=calloc(count,sizeof(float));
        }
        if(!cache->first_person_matrices ||
           !swat_character_capture_pose(asset,cache->first_person_matrices,count)) continue;
        cache->first_person_inverse_gun=reload ? runtime->reload_inverse_gun :
            MatrixInvert(compose(node(asset,"Prop_Rifle"),matrix(rifle_joint)));
        Matrix heading=MatrixRotateY(-actor->controller.yaw);
        Matrix root=compose(MatrixTranslate(feet.x,feet.y,feet.z),compose(heading,bank_fit(bank)));
        float turn=0;
        if(cycle_metres[legs]>0 && moving) turn=swat_angle(direction-travel_yaw[legs]);
        // Rotate only the difference from each bank's actual source travel;
        // forward/backward/diagonal fallbacks still counter-aim the torso.
        Vector3 hip=origin(node(asset,"mixamorig:Hips"));
        Matrix leg_turn=compose(MatrixTranslate(hip.x,hip.y,hip.z),compose(MatrixRotateY(-turn),MatrixTranslate(-hip.x,-hip.y,-hip.z)));
        Matrix turn_delta=legs==bank ? leg_turn : MatrixIdentity();
        Matrix leg_targets[2]={compose(turn_delta,node(asset,"mixamorig:LeftFoot")),compose(turn_delta,node(asset,"mixamorig:RightFoot"))};
        if(legs!=bank) {
            // Locomotion feet, carried into this bank's model frame.
            SwatCharacterView* walk=&runtime->banks[legs];
            if(!swat_character_view_sample(walk,clips[legs],cache->phase*swat_character_clip_duration(walk->asset,0))) continue;
            Matrix bridge=compose(leg_turn,compose(MatrixInvert(bank_fit(bank)),bank_fit(legs)));
            leg_targets[0]=compose(bridge,node(walk->asset,"mixamorig:LeftFoot"));
            leg_targets[1]=compose(bridge,node(walk->asset,"mixamorig:RightFoot"));
        }
        // Phase-zero hip height relative to the fixed sole origin: Ready
        // .93581725 m, both authored crouches .6669743 m. Apply only the
        // remaining controller crouch drop, keeping the sampled foot targets.
        float authored_drop=bank==SWAT_CHARACTER_CROUCH_READY ? .268842936f : bank==SWAT_CHARACTER_CROUCH_WALK ? .268843055f : 0;
        float drop=fmaxf(0,.55f*(actor->controller.body.standHeight-actor->controller.body.totalHeight)-authored_drop);
        if(cycle_metres[legs]>0) {
            // Stride warping: the source walks lunge (0.96 m steps, deep knees,
            // hips low). Shorten foot travel along the walk direction about the
            // hips, then raise the hips as far as both legs still reach their
            // planted feet. Lateral foot spacing and foot rotation stay authored.
            Matrix fit=rotation_only(bank_fit(bank));
            Vector3 axis=Vector3Transform((Vector3){cosf(direction),0,sinf(direction)},MatrixTranspose(fit));
            axis.y=0; if(Vector3LengthSqr(axis)>1e-8f) axis=Vector3Normalize(axis);
            float k=swat_aim_tuning.stride_scale,raise=crouch ? 0 : swat_aim_tuning.walk_raise;
            const char* uplegs[2]={"mixamorig:LeftUpLeg","mixamorig:RightUpLeg"},*shins[2]={"mixamorig:LeftLeg","mixamorig:RightLeg"};
            for(int l=0;l<2;l++) {
                Vector3 f=origin(leg_targets[l]),offset=Vector3Subtract(f,hip);
                f=Vector3Subtract(f,Vector3Scale(axis,(1-k)*Vector3DotProduct(offset,axis)));
                leg_targets[l].m12=f.x; leg_targets[l].m13=f.y; leg_targets[l].m14=f.z;
                Vector3 up=Vector3Transform(origin(node(asset,uplegs[l])),turn_delta);
                float length=.985f*(Vector3Distance(origin(node(asset,uplegs[l])),origin(node(asset,shins[l])))+
                    Vector3Distance(origin(node(asset,shins[l])),origin(node(asset,l ? "mixamorig:RightFoot" : "mixamorig:LeftFoot"))));
                float flat=hypotf(up.x-f.x,up.z-f.z),height=up.y-f.y-drop;
                raise=fminf(raise,length>flat ? sqrtf(length*length-flat*flat)-height : 0);
            }
            drop-=fmaxf(0,raise);
        }
        // Clamber: tuck the feet up while crossing the obstacle.
        MantleWeights clamber=mantle_weights(&actor->controller.mantle);
        if(clamber.tuck>0) for(int l=0;l<2;l++) leg_targets[l].m13+=.24f*clamber.tuck;
        Matrix lowered=compose(MatrixTranslate(0,-drop,0),turn_delta);
        if(!transform(asset,"mixamorig:Hips",lowered)) continue;
        const char* props[]={"Prop_Rifle","Prop_Magazine_A","Prop_Magazine_B"};
        bool ok=true; for(int p=0;p<3;p++) ok=transform(asset,props[p],lowered) && ok;
        ok=limb(asset,"mixamorig:LeftUpLeg","mixamorig:LeftLeg","mixamorig:LeftFoot",leg_targets[0]) && ok;
        ok=limb(asset,"mixamorig:RightUpLeg","mixamorig:RightLeg","mixamorig:RightFoot",leg_targets[1]) && ok;
        SwatPose achieved=swat_pose(&actor->controller,&actor->arsenal);
        Matrix inverse_root=MatrixInvert(root),source_gun=compose(node(asset,"Prop_Rifle"),matrix(rifle_joint));
        Matrix aim_gun=compose(inverse_root,swat_weapon_art_transform(&achieved));
        // The clip's own hands relative to its rifle, for magazine handling.
        Matrix clip_hands[2]={compose(MatrixInvert(source_gun),node(asset,"mixamorig:LeftHand")),
                              compose(MatrixInvert(source_gun),node(asset,"mixamorig:RightHand"))};
        // The torso takes most of the weapon pitch (bounded so it never folds
        // through the floor); the rifle pivots in the pocket for the rest.
        // Carry clips lean over a lowered rifle. Aiming their torso using the
        // rifle's inverse carry rotation puts the head through the newly raised
        // receiver. Aim from the measured chest/shoulder frame instead. This
        // counter-aims the locomotion turn without importing the prop's
        // independent pitch/roll.
        const SwatAimTuning* tune=&swat_aim_tuning;
        float body_pitch=Clamp(tune->torso_share*asinf(Clamp(achieved.weapon_forward.y,-1,1)),-tune->torso_max_deg*SWAT_RAD,tune->torso_max_deg*SWAT_RAD);
        body_pitch-=25*SWAT_RAD*clamber.lean; // lean over the obstacle
        SwatPose bounded=achieved;bounded.weapon_forward=swat_direction(actor->controller.yaw,body_pitch);
        bounded.weapon_up=swat_normalize(b3Cross(achieved.right,bounded.weapon_forward));
        // Rebuild the remaining axis after bounding the torso heading.
        // Recoil makes the achieved right axis oblique to this new forward.
        bounded.right=swat_normalize(b3Cross(bounded.weapon_forward,bounded.weapon_up));
        Matrix target_chest=compose(inverse_root,swat_weapon_art_transform(&bounded));
        {
            // Bladed stance: the chest turns 20 degrees toward the firing
            // side, bringing the support shoulder forward to the handguard.
            Vector3 f={target_chest.m0,target_chest.m1,target_chest.m2},r={target_chest.m8,target_chest.m9,target_chest.m10};
            Vector3 bladed=Vector3Add(Vector3Scale(f,cosf(tune->blade_deg*SWAT_RAD)),Vector3Scale(r,sinf(tune->blade_deg*SWAT_RAD)));
            Matrix turn=QuaternionToMatrix(QuaternionFromVector3ToVector3(f,bladed)),p=target_chest;
            target_chest=compose(turn,rotation_only(target_chest)); target_chest.m12=p.m12; target_chest.m13=p.m13; target_chest.m14=p.m14;
        }
        Vector3 look=Vector3Transform((Vector3){achieved.forward.x,achieved.forward.y,achieved.forward.z},rotation_only(inverse_root));
        Vector3 world_up=Vector3Transform((Vector3){0,1,0},rotation_only(inverse_root));
        ok=aim_layer(asset,target_chest,look,world_up) && ok;
        int shot_age=(int)(sim->tick-actor->last_shot_tick);
        Matrix target_gun=kick_rifle(pocket_rifle(asset,aim_gun,look),shot_age);
        ok=rifle_and_hands(runtime,asset,target_gun,clip_hands,grip) && ok;
        if(!ok || !elbow_carriers(asset) || !swat_character_finalize_pose(asset)) continue;
        if(!transition(runtime,cache,asset,bank,changed,dt))continue;
        if(cache->transition_elapsed<.22) {
            // The bank blend interpolates local prop transforms along with
            // the skeleton. Re-seat the rifle in the blended chest pocket on
            // the aim line and solve the hands from the blended shoulders.
            // Reusing blended, reach-clamped wrists here would accumulate
            // their error across interrupted blends.
            ok=rifle_and_hands(runtime,asset,kick_rifle(pocket_rifle(asset,aim_gun,look),shot_age),clip_hands,grip) && ok;
        }
        ok=mantle_plant(asset,inverse_root,&actor->controller.mantle,actor->controller.yaw,actor->controller.body.bodyRadius,clamber.hand) && ok;
        if(!ok || !elbow_carriers(asset) || !swat_character_finalize_pose(asset))continue;
        if(cache->count!=count) {
            free(cache->matrices);cache->matrices=calloc(count,sizeof(float));cache->count=count;
        }
        if(!cache->matrices || !swat_character_capture_pose(asset,cache->matrices,count)) continue;
        {
            // Presented muzzle: the authoritative muzzle carried by the drawn
            // rifle (pocket, recoil, blends), for tracers and the flash.
            Matrix drawn=compose(root,compose(compose(node(asset,"Prop_Rifle"),matrix(rifle_joint)),MatrixInvert(swat_weapon_art_transform(&achieved))));
            cache->muzzle=Vector3Transform((Vector3){(float)achieved.muzzle.x,(float)achieved.muzzle.y,(float)achieved.muzzle.z},drawn);
        }
        for(int m=0;m<view->model.meshCount;m++) {
            const SwatArtMesh* source=swat_character_mesh(asset,m); bool visible=source->visible;
            if(!strcmp(source->node_name,"Removed magazine")) visible=weapon->magazine_seated && (!reload || time<.88);
            if(!strcmp(source->node_name,"Fresh magazine")) visible=reload && time>=1.4 && (time<3.65 || weapon->magazine_seated);
            // Authority retains rounds/magazines; the authored empty-mag fall
            // must not invent a persistent world drop or refill carrier slots.
            if(strstr(source->node_name,"sleeve")) visible=false;
            cache->visible[m]=(unsigned char)visible;
        }
        cache->root=root; cache->feet=feet; cache->source_time=time; cache->bank=bank; cache->tick=sim->tick; cache->episode=sim->episode; cache->signature=signature; cache->valid=true; cache->reloading=reload;cache->crouching=crouch;
        runtime->preparations++;
        double elapsed=GetTime()-started; runtime->prepare_seconds+=elapsed; runtime->prepare_max_seconds=fmax(runtime->prepare_max_seconds,elapsed);
    }
}
bool swat_character_runtime_muzzle(const SwatCharacterRuntime* runtime,int actor,Vector3* muzzle) {
    if(!runtime || actor<0 || actor>=SWAT_MAX_ACTORS || !runtime->actors[actor].valid) return false;
    *muzzle=runtime->actors[actor].muzzle; return true;
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
    SwatPose anchor=*presentation;
    if(cache->reloading) {
        // The generic busy pose points 28 degrees down and hides the whole
        // assembly at the weapon-view FOV. Let the source working carry supply
        // its own motion, relative to the camera's neutral holding frame.
        anchor.weapon_forward=anchor.forward; anchor.weapon_up=anchor.up;
        float enter=Clamp((float)cache->source_time/.7f,0,1),leave=Clamp((6-(float)cache->source_time)/1.05f,0,1);
        float carry=enter*enter*(3-2*enter)*leave*leave*(3-2*leave);
        // Frame the measured seated-magazine centre at 62% across / 65%
        // down, 0.60 m from the eye. Use the current projection so weapon-size
        // settings cannot push the magazine handling out of the viewport.
        // Retain the source rifle rotation and all relative hand/prop motion.
        Matrix source_gun=compose(node(view->asset,"Prop_Rifle"),matrix(rifle_joint));
        Vector3 centre=Vector3Transform((Vector3){.4214302f,-.0708817f,0},
            compose(cache->first_person_inverse_gun,source_gun));
        Matrix projection=rlGetMatrixProjection();
        float tangent=1/projection.m5,aspect=projection.m5/projection.m0;
        b3Vec3 screen=swat_add(swat_mul(anchor.forward,.60f),
            swat_add(swat_mul(anchor.right,.24f*.60f*tangent*aspect),swat_mul(anchor.up,-.30f*.60f*tangent)));
        b3Vec3 local=swat_add(swat_mul(anchor.forward,centre.x),
            swat_add(swat_mul(anchor.up,centre.y),swat_mul(anchor.right,centre.z)));
        b3Pos framed=b3OffsetPos(anchor.eye,b3Sub(screen,local));
        b3Pos neutral=b3OffsetPos(anchor.shoulder,swat_mul(anchor.up,.04f));
        anchor.shoulder=b3OffsetPos(neutral,swat_mul(b3SubPos(framed,neutral),carry));
    }
    Matrix root=compose(swat_weapon_art_transform(&anchor),cache->first_person_inverse_gun);
    swat_character_view_draw_first_person(view,lighting,root);
    // Shared banks may be used by a subsequent feed/world draw in this frame.
    bool restored=swat_character_restore_pose(view->asset,cache->matrices,cache->count);
    runtime->draws++; return restored;
}
void swat_character_runtime_close(SwatCharacterRuntime* runtime) {
    if(!runtime) return;
    for(int i=0;i<SWAT_CHARACTER_BANKS;i++) swat_character_view_close(&runtime->banks[i]);
    for(int i=0;i<SWAT_BODY_SETS;i++) {
        if(runtime->diffuse[i].id) UnloadTexture(runtime->diffuse[i]);
        if(runtime->normal[i].id) UnloadTexture(runtime->normal[i]);
        if(runtime->specular[i].id) UnloadTexture(runtime->specular[i]);
        if(runtime->gloss[i].id) UnloadTexture(runtime->gloss[i]);
    }
    if(runtime->emissive.id) UnloadTexture(runtime->emissive);
    if(runtime->orm.id) UnloadTexture(runtime->orm);
    for(int i=0;i<SWAT_MAX_ACTORS;i++) { free(runtime->actors[i].matrices); free(runtime->actors[i].first_person_matrices);free(runtime->actors[i].transition_locals); }
    free(runtime);
}

// ---------------------------------------------------------------- civilian
static const char* civilian_loops[SWAT_CIV_LOOPS]={"Civilian_StandingIdle","Civilian_Walk","Civilian_KneelingSurrender",
    "Civilian_RestrainedKneelingIdle","Civilian_RestrainedStandingIdle","Civilian_RestrainedEscortWalk"};
// Metres per loop for travelling loops (authored speed x duration).
static const double civilian_cycle_metres[SWAT_CIV_LOOPS]={0,.84,0,0,0,.51};
// Authored connecting clips: from loop -> to loop. Anything else switches at
// once. A transition in progress always finishes (no mid-clip reversal).
static const struct {int from,to;const char* clip;} civilian_transitions[]={
    {SWAT_CIV_IDLE,SWAT_CIV_SURRENDER,"Civilian_StandToKneelingSurrender"},
    {SWAT_CIV_WALK,SWAT_CIV_SURRENDER,"Civilian_StandToKneelingSurrender"},
    {SWAT_CIV_SURRENDER,SWAT_CIV_RESTRAINED_KNEEL,"Civilian_KneelingSurrenderToRestrained"},
    {SWAT_CIV_SURRENDER,SWAT_CIV_IDLE,"Civilian_KneelingSurrenderToStand"},
    {SWAT_CIV_RESTRAINED_KNEEL,SWAT_CIV_RESTRAINED_STAND,"Civilian_RestrainedKneelToStand"},
    {SWAT_CIV_RESTRAINED_KNEEL,SWAT_CIV_ESCORT,"Civilian_RestrainedKneelToStand"},
    {SWAT_CIV_RESTRAINED_STAND,SWAT_CIV_RESTRAINED_KNEEL,"Civilian_RestrainedStandToKneel"},
    {SWAT_CIV_ESCORT,SWAT_CIV_RESTRAINED_KNEEL,"Civilian_RestrainedStandToKneel"},
    {SWAT_CIV_RESTRAINED_KNEEL,SWAT_CIV_SURRENDER,"Civilian_RestrainedKneelToSurrender"},
};
static int civilian_clip(SwatCharacterAsset* a,const char* name) {
    for(int i=0;i<swat_character_info(a).clips;i++) if(!strcmp(swat_character_clip_name(a,i),name)) return i;
    return -1;
}
static int civilian_target(const SwatActor* a,float speed) {
    bool moving=speed>.12f;
    if(a->gear.restrained) return moving ? SWAT_CIV_ESCORT : a->controller.body.crouched ? SWAT_CIV_RESTRAINED_KNEEL : SWAT_CIV_RESTRAINED_STAND;
    if(a->gear.surrendered) return SWAT_CIV_SURRENDER;
    return moving ? SWAT_CIV_WALK : SWAT_CIV_IDLE;
}
// Armed suspect on the BC_CIV01 rig: chest turned to the aim, weapon on the
// authoritative aim axes with its stock in the chest pocket, both hands on it
// by two-bone IK (grip 12 cm along the barrel, support at 65% of its length,
// the same points the simulation's pose uses). Presentation only.
static Matrix civ_chest(SwatCharacterAsset* a) {
    Vector3 c=origin(node(a,"Chest"));
    Vector3 up=Vector3Normalize(Vector3Subtract(origin(node(a,"Neck")),c));
    Vector3 right=Vector3Normalize(Vector3Subtract(origin(node(a,"UpperArm_R")),origin(node(a,"UpperArm_L"))));
    Vector3 forward=Vector3Normalize(Vector3CrossProduct(up,right));right=Vector3CrossProduct(forward,up);
    return (Matrix){.m0=forward.x,.m1=forward.y,.m2=forward.z,.m4=up.x,.m5=up.y,.m6=up.z,
        .m8=right.x,.m9=right.y,.m10=right.z,.m12=c.x,.m13=c.y,.m14=c.z,.m15=1};
}
static bool suspect_hold(SwatCharacterAsset* asset,const SwatActor* a,SwatCivilianPose* p) {
    if(swat_character_find_node(asset,"Chest")<0 || swat_character_find_node(asset,"UpperArm_R")<0) return false;
    SwatPose achieved=swat_pose(&a->controller,&a->arsenal);
    Matrix inverse=MatrixInvert(p->root),turn=rotation_only(inverse);
    Matrix aim=compose(turn,rotation_only(swat_weapon_art_transform(&achieved)));
    Vector3 look=Vector3Transform((Vector3){achieved.weapon_forward.x,achieved.weapon_forward.y,achieved.weapon_forward.z},turn);
    // Chest toward the aim: 60% of its pitch, split over Spine and Chest.
    for(int k=0;k<2;k++) {
        Matrix chest=civ_chest(asset);Vector3 f={chest.m0,chest.m1,chest.m2};
        Vector3 target=Vector3Normalize((Vector3){look.x,look.y*.6f,look.z});
        Quaternion q=QuaternionFromVector3ToVector3(f,target);
        if(!rotate_about(asset,k?"Chest":"Spine",QuaternionSlerp(QuaternionIdentity(),q,k?1:.5f))) return false;
    }
    Matrix chest=civ_chest(asset);
    const float pocket[3]={.12f,.10f,.13f};
    Vector3 stock=Vector3Add((Vector3){chest.m12,chest.m13,chest.m14},chest_vector(chest,pocket));
    Matrix gun=aim;gun.m12=stock.x;gun.m13=stock.y;gun.m14=stock.z;
    const SwatWeaponDef* def=swat_arsenal_def(&a->arsenal,a->arsenal.active);
    Vector3 grips[2]={Vector3Transform((Vector3){def->barrel*.65f,-.02f,0},gun),Vector3Transform((Vector3){.12f,-.035f,0},gun)};
    const char* arm[2][3]={{"UpperArm_L","Forearm_L","Hand_L"},{"UpperArm_R","Forearm_R","Hand_R"}};
    const float hints[2][3]={{-.1f,-1,-.25f},{-.2f,-1,.6f}};
    for(int h=0;h<2;h++) {
        Matrix target=node(asset,arm[h][2]);target.m12=grips[h].x;target.m13=grips[h].y;target.m14=grips[h].z;
        Vector3 hint=chest_vector(chest,hints[h]);
        if(!limb_pole(asset,arm[h][0],arm[h][1],arm[h][2],target,&hint)) return false;
    }
    if(!swat_character_finalize_pose(asset)) return false;
    // Weapon drawn at the pocket, on the authoritative axes.
    Vector3 world=Vector3Transform(stock,p->root);
    p->gun=achieved;p->gun.shoulder=(b3Pos){world.x,world.y,world.z};
    return true;
}
SwatCivilianRuntime* swat_civilian_runtime_open(void) {
    const char* enabled=getenv("SWAT_CIVILIAN_ART"); if(enabled && !strcmp(enabled,"0")) return NULL;
    char directory[2048],path[4096],error[256];
    snprintf(directory,sizeof(directory),"%sassets/characters/civilian",GetApplicationDirectory());
    if(!DirectoryExists(directory)) snprintf(directory,sizeof(directory),"ocean/swat/assets/characters/civilian");
    snprintf(path,sizeof(path),"%s/runtime/BC_CIV01_candidate.glb",directory);
    if(!FileExists(path)) { TraceLog(LOG_INFO,"SWAT: civilian art absent; procedural civilians"); return NULL; }
    SwatCivilianRuntime* r=calloc(1,sizeof(*r)); if(!r) return NULL;
    if(!swat_character_view_init_gpu(&r->view,path,error,sizeof(error))) {
        TraceLog(LOG_WARNING,"SWAT: civilian art: %s",error); free(r); return NULL;
    }
    for(int i=0;i<SWAT_CIV_LOOPS;i++) if(civilian_clip(r->view.asset,civilian_loops[i])<0) {
        TraceLog(LOG_WARNING,"SWAT: civilian art lacks %s",civilian_loops[i]); swat_civilian_runtime_close(r); return NULL;
    }
    // Flat-colour palette: mesh_colors.txt resolves each mesh's bound material
    // (material_bindings.json) to its linear colour and roughness.
    snprintf(path,sizeof(path),"%s/runtime/mesh_colors.txt",directory);
    char* text=LoadFileText(path);
    SwatArtInfo info=swat_character_info(r->view.asset);
    for(int k=0;k<2;k++){r->palette[k]=calloc((size_t)info.meshes,sizeof(Color));if(!r->palette[k]){swat_civilian_runtime_close(r);return NULL;}}
    if(text) {
        // Suspects wear dark clothes (charcoal hoodie, black jeans and shoes)
        // over the same body, so they read apart from civilians at a glance.
        static const struct {const char* material;float c[3];} suspect[]={
            {"Jacket",{.030f,.031f,.034f}},{"JacketTrim",{.018f,.018f,.020f}},{"Denim",{.022f,.022f,.026f}},
            {"DenimSeam",{.035f,.035f,.040f}},{"Tee",{.045f,.045f,.045f}},{"Shoe",{.015f,.015f,.015f}},{"Sole",{.08f,.08f,.08f}}};
        for(char* line=strtok(text,"\n");line;line=strtok(NULL,"\n")) {
            char name[128],material[64];float c[3],rough;
            if(line[0]=='#' || sscanf(line,"%127s %63s %f %f %f %f",name,material,&c[0],&c[1],&c[2],&rough)!=6) continue;
            for(int m=0;m<info.meshes && m<r->view.model.materialCount;m++) {
                if(strcmp(swat_character_mesh(r->view.asset,m)->node_name,name)) continue;
                float d[3]={c[0],c[1],c[2]};
                for(size_t k=0;k<sizeof(suspect)/sizeof(*suspect);k++) if(!strcmp(suspect[k].material,material)) memcpy(d,suspect[k].c,sizeof(d));
                const float* cols[2]={c,d};
                for(int k=0;k<2;k++) r->palette[k][m]=(Color){(unsigned char)lrintf(powf(cols[k][0],1/2.2f)*255),
                    (unsigned char)lrintf(powf(cols[k][1],1/2.2f)*255),(unsigned char)lrintf(powf(cols[k][2],1/2.2f)*255),255};
                Material* mat=&r->view.model.materials[r->view.model.meshMaterial[m]];
                mat->maps[MATERIAL_MAP_ALBEDO].color=r->palette[0][m];
                mat->maps[MATERIAL_MAP_ROUGHNESS].value=rough;mat->maps[MATERIAL_MAP_METALNESS].value=0;
            }
        }
        UnloadFileText(text);
    }
    // Conformed Mixamo bodies sharing this skeleton and clip set.
    {
        char names[SWAT_CIV_BODIES][64],root[2048]; bool suspects[SWAT_CIV_BODIES];
        int n=swat_mixamo_index(names,suspects,SWAT_CIV_BODIES,root,sizeof(root));
        size_t nodes=(size_t)swat_character_info(r->view.asset).nodes;
        for(int pass=0;pass<2;pass++) for(int k=0;k<n;k++) {
            if(suspects[k]!=(pass==1)) continue; // civilians first, then suspects (variant order kept)
            // Bodies load on first use (swat_civilian_body_ready): a scene shows
            // a few of them, and each carries its own textures.
            SwatCivilianBody* b=&r->bodies[r->body_count];
            snprintf(b->dir,sizeof(b->dir),"%s/%s",root,names[k]);
            b->suspect=suspects[k]; snprintf(b->name,sizeof(b->name),"%s",names[k]); b->nodes=nodes;
            if(b->suspect) r->suspect_bodies++; else r->civilian_bodies++;
            r->body_count++;
        }
    }
    TraceLog(LOG_INFO,"SWAT: civilian BC_CIV01 rig, %d civilian and %d suspect bodies",r->civilian_bodies,r->suspect_bodies);
    return r;
}
int swat_mixamo_index(char names[][64],bool suspects[],int capacity,char* directory,size_t directory_size) {
    snprintf(directory,directory_size,"%sassets/characters/mixamo",GetApplicationDirectory());
    if(!DirectoryExists(directory)) snprintf(directory,directory_size,"ocean/swat/assets/characters/mixamo");
    const char* off=getenv("SWAT_MIXAMO_BODIES"); if(off && !strcmp(off,"0")) return 0;
    char path[4096]; snprintf(path,sizeof(path),"%s/index.txt",directory);
    char* text=FileExists(path) ? LoadFileText(path) : NULL; if(!text) return 0;
    int n=0;
    for(char* line=strtok(text,"\n");line && n<capacity;line=strtok(NULL,"\n")) {
        char name[64],role[32];
        if(line[0]=='#' || sscanf(line,"%63s %31s",name,role)!=2) continue;
        snprintf(names[n],64,"%s",name); suspects[n]=!strcmp(role,"suspect"); n++;
    }
    UnloadFileText(text); return n;
}
static bool swat_civilian_body_ready(SwatCivilianBody* b) {
    if(b->loaded) return b->view.asset!=NULL;
    b->loaded=true;
    char path[4096],error[256]; snprintf(path,sizeof(path),"%s/civ.glb",b->dir);
    if(!FileExists(path) || !swat_character_view_init_gpu(&b->view,path,error,sizeof(error))) {
        TraceLog(LOG_WARNING,"SWAT: body %s: %s",b->name,FileExists(path)?error:"missing civ.glb"); b->view=(SwatCharacterView){0}; return false;
    }
    // Bodies keep their own skeleton: every civilian clip must be present.
    for(int i=0;i<SWAT_CIV_LOOPS;i++) if(civilian_clip(b->view.asset,civilian_loops[i])<0) {
        TraceLog(LOG_WARNING,"SWAT: body %s lacks %s",b->name,civilian_loops[i]); swat_character_view_close(&b->view); b->view=(SwatCharacterView){0}; return false;
    }
    snprintf(path,sizeof(path),"%s/civ_textures.txt",b->dir); // civilian-rig body (retarget_clips.py)
    char* list=FileExists(path) ? LoadFileText(path) : NULL;
    if(list) {
        for(char* line=strtok(list,"\n");line;line=strtok(NULL,"\n")) {
            int set; char diffuse[256],normal[256];
            if(sscanf(line,"%d %255s %255s",&set,diffuse,normal)!=3 || set<0 || set>=SWAT_BODY_SETS) continue;
            if(strcmp(diffuse,"-")) b->diffuse[set]=texture(b->dir,diffuse);
            if(strcmp(normal,"-")) b->normal[set]=texture(b->dir,normal);
        }
        UnloadFileText(list);
    }
    for(int m=0;m<swat_character_info(b->view.asset).meshes && m<b->view.model.materialCount;m++) {
        int set=swat_character_mesh(b->view.asset,m)->primitive; if(set<0 || set>=SWAT_BODY_SETS) continue;
        Material* mat=&b->view.model.materials[b->view.model.meshMaterial[m]];
        if(b->diffuse[set].id) { mat->maps[MATERIAL_MAP_ALBEDO].texture=b->diffuse[set]; mat->maps[MATERIAL_MAP_ALBEDO].color=WHITE; }
        if(b->normal[set].id) { mat->maps[MATERIAL_MAP_NORMAL].texture=b->normal[set]; mat->maps[MATERIAL_MAP_NORMAL].value=2; }
        mat->maps[MATERIAL_MAP_ROUGHNESS].value=.72f; mat->maps[MATERIAL_MAP_METALNESS].value=0;
    }
    TraceLog(LOG_INFO,"SWAT: loaded body %s",b->name); return true;
}
void swat_civilian_runtime_prepare(SwatCivilianRuntime* r,const SwatSim* sim,double seconds) {
    if(!r) return;
    for(int i=0;i<SWAT_MAX_ACTORS;i++) {
        SwatCivilianPose* p=&r->actors[i]; const SwatActor* a=&sim->actors[i];
        SwatCharacterView* source=&r->view; SwatCharacterAsset* asset=source->asset;
        if(i>=sim->actor_count || !a->present || !a->alive || (a->role!=SWAT_CIVILIAN && a->role!=SWAT_SUSPECT)) { p->valid=false; continue; }
        p->suspect=a->role==SWAT_SUSPECT;
        p->armed=p->suspect && !a->gear.surrendered && !a->gear.restrained && !a->gear.stunned_ticks;
        {
            // Bodies are listed civilians first, then suspects.
            int count=p->suspect ? r->suspect_bodies : r->civilian_bodies,v=swat_body_variant(sim,i,count);
            p->body=v<0 ? -1 : (p->suspect ? r->civilian_bodies+v : v);
            if(p->body>=0 && !swat_civilian_body_ready(&r->bodies[p->body])) p->body=-1;
            // A conformed body carries its own bone lengths and rescaled clip
            // translations: sample it on its own skeleton.
            if(p->body>=0) { source=&r->bodies[p->body].view; asset=source->asset; }
        }
        size_t count=(size_t)swat_character_info(asset).nodes*16;
        b3Pos feet=swat_body_feet_position(&a->controller.body);
        b3Vec3 v=b3Body_GetLinearVelocity(a->controller.body.body); float speed=hypotf(v.x,v.z);
        bool reset=!p->valid || p->episode!=sim->episode || sim->tick<p->tick;
        double dt=reset?0:fmax(0,seconds-p->last_seconds);
        double travel=reset?0:hypot((double)feet.x-p->feet.x,(double)feet.z-p->feet.z);
        if(travel>6*dt+.25) {travel=0;reset=true;}
        int target=civilian_target(a,speed);
        if(reset) { p->loop=target; p->transition=-1; p->clip_time=0; p->distance=0; }
        else if(p->transition>=0) {
            p->clip_time+=dt;
            if(p->clip_time>=swat_character_clip_duration(asset,p->transition)) { p->loop=p->next; p->transition=-1; p->clip_time=0; }
        } else if(target!=p->loop) {
            p->transition=-1;
            for(size_t t=0;t<sizeof(civilian_transitions)/sizeof(*civilian_transitions);t++)
                if(civilian_transitions[t].from==p->loop && civilian_transitions[t].to==target) p->transition=civilian_clip(asset,civilian_transitions[t].clip);
            if(p->transition>=0) { p->next=target; p->clip_time=0; }
            else { p->loop=target; p->clip_time=0; }
        } else {
            // Travelling loops advance with the ground actually covered (no
            // foot sliding); standing loops with presentation time.
            double cycle=civilian_cycle_metres[p->loop];
            double length=swat_character_clip_duration(asset,civilian_clip(asset,civilian_loops[p->loop]));
            p->clip_time=cycle>0 ? fmod(p->clip_time+travel/cycle*length,length) : fmod(p->clip_time+dt,length>0?length:1);
        }
        const char* clip=p->transition>=0 ? swat_character_clip_name(asset,p->transition) : civilian_loops[p->loop];
        if(!swat_character_view_sample(source,clip,p->clip_time)) { p->valid=false; continue; }
        // Source front is +Z; engine heading zero is +X (+90 degrees about Y).
        p->root=compose(MatrixTranslate(feet.x,feet.y,feet.z),compose(MatrixRotateY(-a->controller.yaw),MatrixRotateY(SWAT_PI*.5f)));
        if(p->armed && !suspect_hold(asset,a,p)) p->armed=false;
        if(p->count!=count) { free(p->matrices); p->matrices=calloc(count,sizeof(float)); p->count=count; }
        if(!p->matrices || !swat_character_capture_pose(asset,p->matrices,count)) { p->valid=false; continue; }
        p->feet=feet; p->tick=sim->tick; p->episode=sim->episode; p->last_seconds=seconds; p->distance+=travel; p->valid=true;
    }
}
bool swat_civilian_runtime_draw(SwatCivilianRuntime* r,int actor,SwatLighting* lighting) {
    if(!r || actor<0 || actor>=SWAT_MAX_ACTORS || !r->actors[actor].valid) return false;
    SwatCivilianPose* p=&r->actors[actor];
    if(outside_camera(p->feet)) return true;
    if(p->body>=0 && p->body<r->body_count) {
        SwatCharacterView* body=&r->bodies[p->body].view;
        if(!swat_character_restore_pose(body->asset,p->matrices,p->count)) return false;
        swat_character_view_draw(body,lighting,p->root); return true;
    }
    if(!swat_character_restore_pose(r->view.asset,p->matrices,p->count)) return false;
    const Color* palette=r->palette[p->suspect?1:0];
    for(int m=0;m<swat_character_info(r->view.asset).meshes && m<r->view.model.materialCount;m++)
        if(palette[m].a) r->view.model.materials[r->view.model.meshMaterial[m]].maps[MATERIAL_MAP_ALBEDO].color=palette[m];
    swat_character_view_draw(&r->view,lighting,p->root); return true;
}
bool swat_civilian_runtime_weapon(const SwatCivilianRuntime* r,int actor,SwatPose* pose) {
    if(!r || actor<0 || actor>=SWAT_MAX_ACTORS || !r->actors[actor].valid || !r->actors[actor].armed) return false;
    *pose=r->actors[actor].gun; return true;
}
void swat_civilian_runtime_close(SwatCivilianRuntime* r) {
    if(!r) return;
    swat_character_view_close(&r->view);
    for(int b=0;b<r->body_count;b++) {
        if(r->bodies[b].view.asset) swat_character_view_close(&r->bodies[b].view);
        for(int s=0;s<SWAT_BODY_SETS;s++) { if(r->bodies[b].diffuse[s].id) UnloadTexture(r->bodies[b].diffuse[s]); if(r->bodies[b].normal[s].id) UnloadTexture(r->bodies[b].normal[s]); }
    }
    for(int i=0;i<SWAT_MAX_ACTORS;i++) free(r->actors[i].matrices);
    free(r->palette[0]);free(r->palette[1]);
    free(r);
}
