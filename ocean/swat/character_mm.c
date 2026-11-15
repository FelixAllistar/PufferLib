#include "character_mm.h"
#include "raymath.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------- database
static char configured[1024];
static mm_db database;
static bool attempted,loaded;
void swat_mm_configure(const char* path) {
    if(attempted) return; // loaded once; changing the setting needs a restart
    snprintf(configured,sizeof(configured),"%s",path ? path : "");
}
const mm_db* swat_mm_db(void) {
    if(attempted) return loaded ? &database : NULL;
    attempted=true;
    const char* path=getenv("SWAT_MM_DB");
    if(!path || !*path) path=configured;
    if(!*path || !strcmp(path,"0")) return NULL;
    if(mm_db_load(&database,path)) { TraceLog(LOG_WARNING,"SWAT: motion matching database %s failed to load; clip locomotion",path); return NULL; }
    loaded=true;
    TraceLog(LOG_INFO,"SWAT: motion matching %s: %d joints, %d frames, %d clips, %d tags",path,database.joint_count,database.frame_count,database.clip_count,database.tag_count);
    return &database;
}
void swat_mm_shutdown(void) { if(loaded) mm_db_free(&database); loaded=false; }

// ---------------------------------------------------------------- helpers
static Matrix matrix(const float* f) {
    return (Matrix){f[0],f[4],f[8],f[12],f[1],f[5],f[9],f[13],f[2],f[6],f[10],f[14],f[3],f[7],f[11],f[15]};
}
// Column vectors: compose(a,b) applies b first (see character_runtime.c).
static Matrix compose(Matrix a,Matrix b) { return MatrixMultiply(b,a); }
static Vector3 origin(Matrix m) { return (Vector3){m.m12,m.m13,m.m14}; }
static Quaternion qm(mm_quat q) { return (Quaternion){q.x,q.y,q.z,q.w}; }
static Vector3 vm(mm_vec3 v) { return (Vector3){v.x,v.y,v.z}; }
// Rotation of a rigid (possibly scaled) transform.
static Quaternion rotation(Matrix m) {
    Vector3 t,s; Quaternion q; MatrixDecompose(m,&t,&q,&s); return QuaternionNormalize(q);
}
static Matrix trs(Vector3 t,Quaternion q,Vector3 s) {
    return compose(MatrixTranslate(t.x,t.y,t.z),compose(QuaternionToMatrix(q),MatrixScale(s.x,s.y,s.z)));
}
static Matrix node(const SwatCharacterAsset* asset,int i) { return matrix(swat_character_node_matrix(asset,i)); }
// Exact critically damped spring toward goal (D. Holden, MIT; see anim/mm.c).
static void spring(float* x,float* v,float goal,float halflife,float dt) {
    float y=(4*.69314718056f)/(halflife+1e-5f)/2,j0=*x-goal,j1=*v+j0*y,e=expf(-y*dt);
    *x=e*(j0+j1*dt)+goal; *v=e*(*v-j1*y*dt);
}

// ---------------------------------------------------------------- rig
struct SwatMMRig {
    const mm_db* db;
    int nodes;
    int* order;            // asset nodes, parents first
    int* joint_of_node;    // asset node -> database joint, -1 unmapped
    int* node_of_joint;    // database joint -> asset node, -1 unmapped
    Quaternion* post;      // per joint: W_body = W_db * post
    mm_vec3 *pos; mm_quat* rot; // scratch, database model space
    float* locals;         // scratch, nodes x 16
    Vector3* rest_offset;  // per asset node: rest local translation (bone offset)
    float hip_ratio,db_ankle[2],body_ankle[2],db_toe[2]; // rest heights above each floor
    int legs[2][3],head,neck,chest;
    Vector3 head_forward;  // head's local forward axis (model +Z at rest)
    bool identity,valid;
};
static const char* suffix(const char* name) { const char* c=name ? strrchr(name,':') : NULL; return c ? c+1 : name; }
// BC_CIV01 (stylized civilian candidate) names for Mixamo joints.
static const char* const civilian_alias[][2]={
    {"Hips","Hips"},{"Spine","Spine"},{"Spine2","Chest"},{"Neck","Neck"},{"Head","Head"},
    {"LeftShoulder","Clavicle_L"},{"LeftArm","UpperArm_L"},{"LeftForeArm","Forearm_L"},{"LeftHand","Hand_L"},
    {"RightShoulder","Clavicle_R"},{"RightArm","UpperArm_R"},{"RightForeArm","Forearm_R"},{"RightHand","Hand_R"},
    {"LeftUpLeg","Thigh_L"},{"LeftLeg","Shin_L"},{"LeftFoot","Foot_L"},{"LeftToeBase","Toe_L"},
    {"RightUpLeg","Thigh_R"},{"RightLeg","Shin_R"},{"RightFoot","Foot_R"},{"RightToeBase","Toe_R"},
    {"LeftHandIndex1","Index1_L"},{"LeftHandIndex2","Index2_L"},{"LeftHandMiddle1","Middle1_L"},{"LeftHandMiddle2","Middle2_L"},
    {"LeftHandRing1","Ring1_L"},{"LeftHandRing2","Ring2_L"},{"LeftHandPinky1","Pinky1_L"},{"LeftHandPinky2","Pinky2_L"},
    {"LeftHandThumb1","Thumb1_L"},{"LeftHandThumb2","Thumb2_L"},
    {"RightHandIndex1","Index1_R"},{"RightHandIndex2","Index2_R"},{"RightHandMiddle1","Middle1_R"},{"RightHandMiddle2","Middle2_R"},
    {"RightHandRing1","Ring1_R"},{"RightHandRing2","Ring2_R"},{"RightHandPinky1","Pinky1_R"},{"RightHandPinky2","Pinky2_R"},
    {"RightHandThumb1","Thumb1_R"},{"RightHandThumb2","Thumb2_R"},
};
static int find_body_joint(const SwatCharacterAsset* asset,const char* name) {
    int n=swat_character_find_node(asset,name); if(n>=0) return n;
    const char* bare=suffix(name); size_t len=strlen(bare);
    for(int i=0;i<swat_character_info(asset).nodes;i++) {
        const char* other=swat_character_node_name(asset,i); size_t m=other ? strlen(other) : 0;
        if(m>len && other[m-len-1]==':' && !strncmp(other,"mixamorig",9) && !strcmp(other+m-len,bare)) return i;
    }
    for(size_t k=0;k<sizeof(civilian_alias)/sizeof(*civilian_alias);k++)
        if(!strcmp(civilian_alias[k][0],bare)) return swat_character_find_node(asset,civilian_alias[k][1]);
    return -1;
}
static bool descends(const mm_db* db,int joint,int ancestor) {
    for(int p=db->parents[joint];p>=0;p=db->parents[p]) if(p==ancestor) return true;
    return false;
}
// The joint a bone points at, for rest-pose alignment: down the spine/neck,
// to the middle finger from the hand, else the first child; the first
// mapped joint along that chain.
static int bone_end(const SwatMMRig* rig,int joint) {
    const mm_db* db=rig->db; int at=joint;
    for(int depth=0;depth<8;depth++) {
        int first=-1,best=-1,rank=99;
        static const char* const prefer[]={"Spine","Neck","Head","Middle1"};
        for(int c=0;c<db->joint_count;c++) if(db->parents[c]==at) {
            if(first<0) first=c;
            for(int k=0;k<4;k++) if(strstr(suffix(db->joint_names[c]),prefer[k]) && k<rank) { rank=k; best=c; }
        }
        int next=best>=0 ? best : first; if(next<0) return -1;
        if(rig->node_of_joint[next]>=0) return next;
        at=next;
    }
    return -1;
}
void swat_mm_rig_free(SwatMMRig* r) {
    if(!r) return;
    free(r->order); free(r->joint_of_node); free(r->node_of_joint); free(r->post); free(r->rest_offset); free(r->pos); free(r->rot); free(r->locals); free(r);
}
SwatMMRig* swat_mm_rig_build(SwatCharacterAsset* asset,const mm_db* db,bool fingers) {
    if(!asset || !db) return NULL;
    SwatMMRig* r=calloc(1,sizeof(*r)); if(!r) return NULL;
    int n=swat_character_info(asset).nodes,J=db->joint_count;
    r->db=db; r->nodes=n;
    r->order=calloc((size_t)n,sizeof(int)); r->joint_of_node=malloc((size_t)n*sizeof(int)); r->node_of_joint=malloc((size_t)J*sizeof(int));
    r->post=calloc((size_t)J,sizeof(Quaternion)); r->pos=calloc((size_t)J,sizeof(mm_vec3)); r->rot=calloc((size_t)J,sizeof(mm_quat));
    r->locals=calloc((size_t)n*16,sizeof(float)); r->rest_offset=calloc((size_t)n,sizeof(Vector3));
    if(!r->rest_offset || !r->order || !r->joint_of_node || !r->node_of_joint || !r->post || !r->pos || !r->rot || !r->locals) { swat_mm_rig_free(r); return NULL; }
    // Parents first: order by depth (stable).
    int k=0;
    for(int depth=0;k<n && depth<n;depth++) for(int i=0;i<n;i++) {
        int d=0; for(int p=swat_character_parent_node(asset,i);p>=0 && d<=n;p=swat_character_parent_node(asset,p)) d++;
        if(d==depth) r->order[k++]=i;
    }
    if(k!=n) { swat_mm_rig_free(r); return NULL; }
    for(int i=0;i<n;i++) r->joint_of_node[i]=-1;
    int left_hand=mm_db_find_joint(db,"mixamorig:LeftHand"),right_hand=mm_db_find_joint(db,"mixamorig:RightHand");
    for(int j=0;j<J;j++) {
        r->node_of_joint[j]=-1;
        if(!fingers && ((left_hand>=0 && descends(db,j,left_hand)) || (right_hand>=0 && descends(db,j,right_hand)))) continue;
        int b=find_body_joint(asset,db->joint_names[j]);
        if(b<0 || r->joint_of_node[b]>=0) continue; // one database joint per body joint
        // The parent chain must agree: the body joint descends from the body
        // joint of the nearest mapped database ancestor.
        int p=db->parents[j]; while(p>=0 && r->node_of_joint[p]<0) p=db->parents[p];
        if(p>=0) { int q=swat_character_parent_node(asset,b); while(q>=0 && q!=r->node_of_joint[p]) q=swat_character_parent_node(asset,q); if(q<0) continue; }
        r->node_of_joint[j]=b; r->joint_of_node[b]=j;
    }
    static const int roles[2][3]={{MM_ROLE_LEFT_UPLEG,MM_ROLE_LEFT_LEG,MM_ROLE_LEFT_FOOT},{MM_ROLE_RIGHT_UPLEG,MM_ROLE_RIGHT_LEG,MM_ROLE_RIGHT_FOOT}};
    bool legs=true;
    for(int s=0;s<2;s++) for(int l=0;l<3;l++) { int j=db->roles[roles[s][l]]; r->legs[s][l]=j>=0 ? r->node_of_joint[j] : -1; legs=legs && r->legs[s][l]>=0; }
    if(db->hips<0 || r->node_of_joint[db->hips]<0 || !legs) { r->valid=false; return r; }
    // Rest poses: the database's from its own skeleton, the body's sampled.
    mm_forward_kinematics(db,db->rest_rot,db->rest_pos[db->hips],mm_vec3_make(0,0,0),(mm_quat){0,0,0,1},r->pos,r->rot);
    if(!swat_character_sample_pose(asset,NULL,0)) { swat_mm_rig_free(r); return NULL; }
    float db_ground=1e9f,body_ground=1e9f;
    for(int s=0;s<2;s++) {
        int foot=db->roles[s ? MM_ROLE_RIGHT_FOOT : MM_ROLE_LEFT_FOOT],toe=db->roles[s ? MM_ROLE_RIGHT_TOE : MM_ROLE_LEFT_TOE];
        int ends[2]={foot,toe};
        for(int e=0;e<2;e++) if(ends[e]>=0) {
            db_ground=fminf(db_ground,r->pos[ends[e]].y);
            if(r->node_of_joint[ends[e]]>=0) body_ground=fminf(body_ground,origin(node(asset,r->node_of_joint[ends[e]])).y);
        }
    }
    for(int i=0;i<n;i++) {
        int p=swat_character_parent_node(asset,i); Matrix local=p>=0 ? compose(MatrixInvert(node(asset,p)),node(asset,i)) : node(asset,i);
        r->rest_offset[i]=origin(local);
    }
    for(int s=0;s<2;s++) {
        int toe=db->roles[s ? MM_ROLE_RIGHT_TOE : MM_ROLE_LEFT_TOE];
        r->db_ankle[s]=r->pos[db->roles[s ? MM_ROLE_RIGHT_FOOT : MM_ROLE_LEFT_FOOT]].y; r->body_ankle[s]=origin(node(asset,r->legs[s][2])).y;
        r->db_toe[s]=toe>=0 ? r->pos[toe].y : -1;
    }
    float db_hip=r->pos[db->hips].y-db_ground,body_hip=origin(node(asset,r->node_of_joint[db->hips])).y-body_ground;
    r->hip_ratio=db_hip>1e-3f && body_hip>1e-3f ? body_hip/db_hip : 1;
    if(fabsf(r->hip_ratio-1)<1e-3f) r->hip_ratio=1;
    r->identity=r->hip_ratio==1 && fabsf(r->db_ankle[0]-r->body_ankle[0])<1e-3f && fabsf(r->db_ankle[1]-r->body_ankle[1])<1e-3f;
    for(int j=0;j<J;j++) {
        int b=r->node_of_joint[j]; r->post[j]=QuaternionIdentity(); if(b<0) continue;
        Quaternion db_rest=qm(r->rot[j]),body_rest=rotation(node(asset,b)),align=QuaternionIdentity();
        int e=bone_end(r,j);
        if(e>=0) {
            Vector3 a=Vector3Subtract(vm(r->pos[e]),vm(r->pos[j])),c=Vector3Subtract(origin(node(asset,r->node_of_joint[e])),origin(node(asset,b)));
            if(Vector3Length(a)>1e-4f && Vector3Length(c)>1e-4f) align=QuaternionFromVector3ToVector3(Vector3Normalize(c),Vector3Normalize(a));
        }
        Quaternion post=QuaternionNormalize(QuaternionMultiply(QuaternionInvert(db_rest),QuaternionMultiply(align,body_rest)));
        if(post.w<0) post=(Quaternion){-post.x,-post.y,-post.z,-post.w};
        if(post.w>1-1e-6f) post=QuaternionIdentity(); else r->identity=false;
        r->post[j]=post;
    }
    // Look-at joints and the head's forward axis (model +Z at rest).
    r->head=find_body_joint(asset,"mixamorig:Head"); r->neck=find_body_joint(asset,"mixamorig:Neck");
    r->chest=find_body_joint(asset,"mixamorig:Spine2");
    if(r->head>=0) r->head_forward=Vector3RotateByQuaternion((Vector3){0,0,1},QuaternionInvert(rotation(node(asset,r->head))));
    r->valid=true; return r;
}
bool swat_mm_rig_valid(const SwatMMRig* r) { return r && r->valid; }
bool swat_mm_rig_identity(const SwatMMRig* r) { return r && r->identity; }
float swat_mm_rig_hip_ratio(const SwatMMRig* r) { return r ? r->hip_ratio : 1; }
int swat_mm_rig_leg(const SwatMMRig* r,int side,int joint) { return r && r->valid && side>=0 && side<2 && joint>=0 && joint<3 ? r->legs[side][joint] : -1; }

// ---------------------------------------------------------------- body
SwatMMBody* swat_mm_body_new(void) { SwatMMBody* b=calloc(1,sizeof(*b)); if(b) b->look.target=b->look.commander=-1; return b; }
void swat_mm_body_free(SwatMMBody* b) { if(!b) return; if(b->ready) mm_controller_free(&b->controller); free(b); }
void swat_mm_release(SwatMMBody* b) { if(b) b->driven=false; }
static bool clip_has(const mm_db* db,uint32_t required) {
    for(int c=0;c<db->clip_count;c++) if((db->clips[c].tags&required)==required) return true;
    return false;
}
mm_filter swat_mm_filter(const mm_db* db,const SwatActor* a,bool fleeing) {
    const SwatBody* body=&a->controller.body;
    uint32_t stance=mm_db_tag_bit(db,body->totalHeight<body->standHeight-.01f ? "crouch" : "stand");
    const char* style=a->role==SWAT_SUSPECT ? "suspect" : a->role==SWAT_CIVILIAN ? (fleeing || a->gear.surrendered ? "scared" : "civilian") : "tactical";
    bool wounded=false; for(int r=0;r<SWAT_HIT_REGIONS;r++) wounded=wounded || a->gear.wounds[r]>0;
    uint32_t tag=wounded && mm_db_tag_bit(db,"injured") ? mm_db_tag_bit(db,"injured") : mm_db_tag_bit(db,style);
    mm_filter f={stance|tag,0};
    if(tag && !clip_has(db,f.required)) f.required=stance;
    if(f.required && !clip_has(db,f.required)) f.required=0;
    return f;
}
static mm_input input_of(const SwatMMBody* b,const SwatActor* a) {
    mm_input in={0};
    in.position=mm_vec3_make((float)b->feet.x,(float)b->feet.y,(float)b->feet.z);
    in.velocity=b->velocity; in.desired_velocity=b->velocity;
    in.yaw=in.desired_yaw=swat_mm_yaw(a->controller.yaw);
    in.filter=b->filter; return in;
}
bool swat_mm_update(SwatMMBody* b,const mm_db* db,const SwatSim* sim,int i) {
    if(!b || !db || i<0 || i>=sim->actor_count) return false;
    const SwatActor* a=&sim->actors[i];
    b3Pos feet=swat_body_feet_position(&a->controller.body);
    bool reset=!b->ready || !b->driven || b->episode!=sim->episode || sim->tick<b->tick;
    float dt=reset ? 0 : (sim->tick-b->tick)/60.0f;
    if(!reset && dt==0) return true;
    if(!reset && dt>.25f) reset=true;
    double travel=reset ? 0 : hypot(feet.x-b->feet.x,feet.z-b->feet.z);
    if(travel>6*dt+.25) reset=true; // teleport/restore: no invented travel
    if(reset) {
        b->velocity=mm_vec3_make(0,0,0); b->speed=0; b->fleeing=false;
    } else {
        // Velocity from replicated positions, smoothed (half-life 0.08 s).
        float vx=(float)((feet.x-b->feet.x)/dt),vz=(float)((feet.z-b->feet.z)/dt),k=1-expf(-.69314718056f*dt/.08f);
        b->velocity.x+=(vx-b->velocity.x)*k; b->velocity.z+=(vz-b->velocity.z)*k; b->velocity.y=0;
        b->speed=hypotf(b->velocity.x,b->velocity.z);
    }
    // Fleeing civilians run; hysteresis keeps the style from flickering.
    b->fleeing=a->role==SWAT_CIVILIAN && (b->fleeing ? b->speed>1.7f : b->speed>2.2f);
    b->feet=feet; b->tick=sim->tick; b->episode=sim->episode; b->driven=true;
    b->filter=swat_mm_filter(db,a,b->fleeing);
    mm_input in=input_of(b,a);
    if(reset) {
        if(b->ready) mm_controller_free(&b->controller);
        b->ready=false;
        int start=0;
        for(int c=0;c<db->clip_count;c++) if((db->clips[c].tags&in.filter.required)==in.filter.required) { start=db->clips[c].start; break; }
        mm_config config; mm_config_default(&config);
        if(mm_controller_init(&b->controller,db,&config,start,&in)) return false;
        b->ready=true;
        mm_controller_update(&b->controller,&in,1/60.0f); // first search: the matched pose, not the clip start
        return true;
    }
    mm_controller_update(&b->controller,&in,dt);
    return true;
}
bool swat_mm_apply(const SwatMMBody* b,SwatMMRig* r,SwatCharacterAsset* asset,Matrix* root,Matrix foot[2],bool solve[2]) {
    if(!b || !b->ready || !r || !r->valid || swat_character_info(asset).nodes!=r->nodes) return false;
    const mm_db* db=r->db; const mm_controller* c=&b->controller;
    // Model space: the controller's pose about its own root.
    mm_forward_kinematics(db,c->local_rot,c->hips_pos,mm_vec3_make(0,0,0),(mm_quat){0,0,0,1},r->pos,r->rot);
    Matrix* world=malloc((size_t)r->nodes*sizeof(Matrix)); if(!world) return false;
    for(int k=0;k<r->nodes;k++) {
        int i=r->order[k],p=swat_character_parent_node(asset,i),j=r->joint_of_node[i];
        Matrix sampled=node(asset,i),local=p>=0 ? compose(MatrixInvert(node(asset,p)),sampled) : sampled;
        Matrix parent=p>=0 ? world[p] : MatrixIdentity();
        if(j>=0) {
            Vector3 t,s; Quaternion q; MatrixDecompose(local,&t,&q,&s);
            Quaternion want=QuaternionNormalize(QuaternionMultiply(qm(r->rot[j]),r->post[j]));
            q=QuaternionNormalize(QuaternionMultiply(QuaternionInvert(p>=0 ? rotation(parent) : QuaternionIdentity()),want));
            // Mapped bones keep this body's rest lengths; the hips take the
            // database's root-relative translation, scaled to this body.
            if(j==db->hips) t=Vector3Transform(Vector3Scale(vm(r->pos[j]),r->hip_ratio),MatrixInvert(parent));
            else t=r->rest_offset[i];
            local=trs(t,q,s);
        }
        world[i]=compose(parent,local);
        memcpy(r->locals+i*16,MatrixToFloatV(local).v,16*sizeof(float));
    }
    free(world);
    if(!swat_character_compose_pose(asset,r->locals,(size_t)r->nodes*16)) return false;
    for(int s=0;s<2;s++) {
        int j=db->roles[s ? MM_ROLE_RIGHT_FOOT : MM_ROLE_LEFT_FOOT];
        // Ankles follow the database's (foot-locked) ankles on the ground
        // plane; their lift above the rest ankle height scales with the body.
        // A locked foot whose heel and toe are both off the floor (the
        // database marks contact up to 5 cm of ankle lift) comes down until
        // the lower of the two is at its rest height.
        int toe=db->roles[s ? MM_ROLE_RIGHT_TOE : MM_ROLE_LEFT_TOE];
        float lift=r->pos[j].y-r->db_ankle[s];
        if(toe>=0 && r->db_toe[s]>=0) lift=fminf(lift,r->pos[toe].y-r->db_toe[s]);
        float drop=c->feet[s].locked && lift>0 ? lift : 0;
        Vector3 p={r->pos[j].x,(r->pos[j].y-drop-r->db_ankle[s])*r->hip_ratio+r->body_ankle[s],r->pos[j].z};
        if(solve) solve[s]=!r->identity || drop>1e-4f;
        Matrix m=QuaternionToMatrix(QuaternionNormalize(QuaternionMultiply(qm(r->rot[j]),r->post[j])));
        m.m12=p.x; m.m13=p.y; m.m14=p.z; if(foot) foot[s]=m;
    }
    *root=compose(MatrixTranslate(c->root_pos.x,c->root_pos.y,c->root_pos.z),MatrixRotateY(c->root_yaw));
    return true;
}

// ---------------------------------------------------------------- look-at
static bool is_officer(const SwatActor* a) { return a->role==SWAT_OFFICER || a->role==SWAT_SNIPER; }
void swat_mm_look_update(SwatMMLook* l,const SwatSim* sim,int i,const b3Vec3* aim,float dt) {
    const SwatActor* a=&sim->actors[i];
    bool reset=!l->valid || l->episode!=sim->episode || sim->tick<l->tick;
    if(reset) { memset(l,0,sizeof(*l)); l->target=l->commander=-1; l->command_tick=-100000; }
    b3Pos eye=swat_controller_eye(&a->controller);
    // Heard commands: the latest officer command whose sound reached this eye.
    for(int k=0;k<sim->sounds.count;k++) {
        const SwatSoundEvent* e=&sim->sounds.events[(sim->sounds.head+k)%SWAT_SOUND_CAPACITY];
        if(e->kind!=SWAT_SOUND_COMMAND || e->source_actor<0 || e->source_actor>=sim->actor_count || e->source_actor==i) continue;
        if(!is_officer(&sim->actors[e->source_actor]) || e->tick<=l->command_tick || b3Distance(e->position,eye)>e->range) continue;
        l->commander=e->source_actor; l->command_tick=e->tick;
    }
    b3Vec3 d={0}; bool want=false;
    if(aim) { d=*aim; want=true; }
    else {
        float best=SWAT_LOOK_RANGE; int target=-1;
        for(int j=0;j<sim->actor_count;j++) {
            const SwatActor* o=&sim->actors[j];
            if(j==i || !o->present || !o->alive || !is_officer(o)) continue;
            b3Pos other=swat_controller_eye(&o->controller); float distance=b3Distance(other,eye);
            if(distance<best && swat_world_sight_clear(&sim->world,eye,other)) { best=distance; target=j; }
        }
        if(target<0 && l->commander>=0 && sim->tick-l->command_tick<600) {
            const SwatActor* o=&sim->actors[l->commander];
            if(o->present && o->alive && b3Distance(swat_controller_eye(&o->controller),eye)<22) target=l->commander;
        }
        l->target=target;
        if(target>=0) { d=b3SubPos(swat_controller_eye(&sim->actors[target].controller),eye); want=true; }
    }
    float yaw=l->yaw,pitch=l->pitch;
    if(want) {
        float c=cosf(a->controller.yaw),s=sinf(a->controller.yaw);
        float forward=d.x*c+d.z*s,right=-d.x*s+d.z*c,flat=hypotf(forward,right);
        if(flat>1e-4f || fabsf(d.y)>1e-4f) {
            yaw=Clamp(atan2f(right,forward),-SWAT_LOOK_YAW_LIMIT,SWAT_LOOK_YAW_LIMIT);
            pitch=Clamp(atan2f(d.y,flat),-SWAT_LOOK_PITCH_LIMIT,SWAT_LOOK_PITCH_LIMIT);
        } else want=false;
    }
    if(reset) { l->yaw=yaw; l->pitch=pitch; }
    else if(dt>0) {
        spring(&l->yaw,&l->yaw_velocity,yaw,SWAT_LOOK_HALFLIFE,dt);
        spring(&l->pitch,&l->pitch_velocity,pitch,SWAT_LOOK_HALFLIFE,dt);
        spring(&l->weight,&l->weight_velocity,want ? 1 : 0,SWAT_LOOK_HALFLIFE,dt);
    }
    l->weight=Clamp(l->weight,0,1);
    l->tick=sim->tick; l->episode=sim->episode; l->valid=true;
}
static bool rotate_about(SwatCharacterAsset* asset,int n,Quaternion q) {
    Vector3 p=origin(node(asset,n));
    Matrix delta=compose(MatrixTranslate(p.x,p.y,p.z),compose(QuaternionToMatrix(q),MatrixTranslate(-p.x,-p.y,-p.z)));
    return swat_character_transform_node(asset,n,MatrixToFloatV(delta).v);
}
static Vector3 head_forward(const SwatMMRig* r,const SwatCharacterAsset* asset) {
    return Vector3Normalize(Vector3RotateByQuaternion(r->head_forward,rotation(node(asset,r->head))));
}
bool swat_mm_look_apply(const SwatMMLook* l,const SwatMMRig* r,SwatCharacterAsset* asset,Matrix root,float facing,bool chest) {
    if(!l || !l->valid || !r || r->head<0 || r->neck<0 || l->weight<1e-4f) return true;
    // Engine-facing frame in model space.
    Matrix inverse=MatrixInvert(root); inverse.m12=inverse.m13=inverse.m14=0;
    float c=cosf(facing),s=sinf(facing);
    Vector3 forward=Vector3Transform((Vector3){c,0,s},inverse),right=Vector3Transform((Vector3){-s,0,c},inverse),up=Vector3Transform((Vector3){0,1,0},inverse);
    Vector3 h=head_forward(r,asset);
    float base_yaw=atan2f(Vector3DotProduct(h,right),Vector3DotProduct(h,forward));
    float base_pitch=atan2f(Vector3DotProduct(h,up),hypotf(Vector3DotProduct(h,right),Vector3DotProduct(h,forward)));
    float w=l->weight,yaw=base_yaw+w*(l->yaw-base_yaw),pitch=base_pitch+w*(l->pitch-base_pitch);
    Vector3 goal=Vector3Add(Vector3Scale(up,sinf(pitch)),Vector3Scale(Vector3Add(Vector3Scale(forward,cosf(yaw)),Vector3Scale(right,sinf(yaw))),cosf(pitch)));
    // Yaw about the vertical, over (chest,) neck and head; then the rest.
    // A positive engine yaw turns toward the right: about -up in this frame.
    bool use_chest=chest && r->chest>=0;
    const int joints[3]={use_chest ? r->chest : -1,r->neck,r->head};
    const float yaw_share[3]={use_chest ? .25f : 0,use_chest ? .35f : .45f,use_chest ? .40f : .55f};
    float turn=swat_angle(yaw-base_yaw);
    for(int k=0;k<3;k++) if(joints[k]>=0 && yaw_share[k]>0 && !rotate_about(asset,joints[k],QuaternionFromAxisAngle(Vector3Negate(up),turn*yaw_share[k]))) return false;
    h=head_forward(r,asset);
    Quaternion rest=QuaternionFromVector3ToVector3(h,Vector3Normalize(goal));
    if(!rotate_about(asset,r->neck,QuaternionSlerp(QuaternionIdentity(),rest,.4f))) return false;
    h=head_forward(r,asset);
    return rotate_about(asset,r->head,QuaternionFromVector3ToVector3(h,Vector3Normalize(goal))) && swat_character_finalize_pose(asset);
}
bool swat_mm_head_direction(const SwatMMRig* r,const SwatCharacterAsset* asset,const float* matrices,Matrix root,Vector3* position,Vector3* forward) {
    if(!r || r->head<0) return false;
    Matrix m=matrices ? matrix(matrices+r->head*16) : node(asset,r->head);
    Matrix w=compose(root,m);
    *position=origin(w);
    *forward=Vector3Normalize(Vector3RotateByQuaternion(r->head_forward,rotation(w)));
    return true;
}
