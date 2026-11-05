// Skinned-mesh fit score for the third-person aim stance, plus a hill climber
// over SwatAimTuning. Presentation only.
//   officer_fit ASSET_DIR [--load FILE] [--climb ITERS] [--save FILE] [--seed N] [-v]
// Score terms (lower is better, per sample, summed over stance/move/pitch):
//   pad     stock butt pad to shoulder/chest surface gap (cm beyond 0.5)
//   inside  rifle penetration depth into head/torso/upper arms (cm, max)
//   cheek   head surface to stock comb gap (cm beyond 0.5)
//   eye     right eye distance from the sight line; behind-sight window
//   elbow   upper-arm drop vs tucked targets; outward flare beyond 10 cm
//   neck    skull axis vs chest beyond 30 degrees (contortion)
//   arms    locked-straight arms; support-hand slide along the handguard
#include "character_runtime.h"
#include "aim_tuning_io.h"
#include "raymath.h"
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
static Matrix nodem(SwatCharacterAsset* a,const char* n) { int i=swat_character_find_node(a,n); assert(i>=0); return fm(swat_character_node_matrix(a,i)); }
static void empty(const SwatSim* sim,bool cutaway) { (void)sim; (void)cutaway; }

enum { OTHER, HEAD, TORSO, UPPER_R, UPPER_L };
typedef struct { int count; unsigned char* label; } Labels;
static Labels labels[SWAT_CHARACTER_BANKS][SWAT_CHARACTER_MESHES];
static int classify(SwatCharacterAsset* a,const SwatArtMesh* s,int v) {
    float best=0; int node=-1;
    for(int w=0;w<s->influences;w++) { float wt=s->joint_weights[v*s->influences+w]; if(wt>best) { best=wt; node=s->joint_nodes[s->joint_ids[v*s->influences+w]]; } }
    if(node<0) return OTHER; const char* n=swat_character_node_name(a,node); if(!n) return OTHER;
    if(!strcmp(n,"mixamorig:Head")) return HEAD;
    if(!strcmp(n,"mixamorig:Spine") || !strcmp(n,"mixamorig:Spine1") || !strcmp(n,"mixamorig:Spine2") ||
       !strcmp(n,"mixamorig:RightShoulder") || !strcmp(n,"mixamorig:LeftShoulder") || !strcmp(n,"mixamorig:Neck")) return TORSO;
    if(!strcmp(n,"mixamorig:RightArm")) return UPPER_R;
    if(!strcmp(n,"mixamorig:LeftArm")) return UPPER_L;
    return OTHER;
}
// Uniform 2 cm hash grid over labelled body vertices.
#define CELL .02f
#define HASH 65536
typedef struct { Vector3 p,n; int label; int next; } BodyVert;
static BodyVert* verts; static int nverts,cap; static int heads[HASH];
static unsigned hash3(int x,int y,int z) { return ((unsigned)x*73856093u ^ (unsigned)y*19349663u ^ (unsigned)z*83492791u)&(HASH-1); }
static void grid_build(SwatCharacterAsset* a,int bank) {
    for(int i=0;i<HASH;i++) heads[i]=-1; nverts=0;
    SwatArtInfo info=swat_character_info(a);
    for(int m=0;m<info.meshes;m++) {
        const SwatArtMesh* s=swat_character_mesh(a,m); if(strncmp(s->node_name,"SWAT_Wearer",11)) continue;
        Labels* L=&labels[bank][m];
        if(!L->label) { L->count=s->vertices; L->label=malloc((size_t)s->vertices); for(int v=0;v<s->vertices;v++) L->label[v]=(unsigned char)classify(a,s,v); }
        for(int v=0;v<s->vertices;v++) {
            if(L->label[v]==OTHER) continue;
            if(nverts==cap) { cap=cap?cap*2:32768; verts=realloc(verts,(size_t)cap*sizeof(*verts)); }
            BodyVert* b=&verts[nverts]; b->p=(Vector3){s->positions[v*3],s->positions[v*3+1],s->positions[v*3+2]};
            b->n=Vector3Normalize((Vector3){s->normals[v*3],s->normals[v*3+1],s->normals[v*3+2]}); b->label=L->label[v];
            unsigned h=hash3((int)floorf(b->p.x/CELL),(int)floorf(b->p.y/CELL),(int)floorf(b->p.z/CELL)); b->next=heads[h]; heads[h]=nverts++;
        }
    }
}
// Nearest body vertex within 2 cells whose label is in `mask`.
static int nearest(Vector3 p,unsigned mask,float* distance) {
    int cx=(int)floorf(p.x/CELL),cy=(int)floorf(p.y/CELL),cz=(int)floorf(p.z/CELL),best=-1; float bd=1e9f;
    for(int x=-2;x<=2;x++) for(int y=-2;y<=2;y++) for(int z=-2;z<=2;z++)
        for(int i=heads[hash3(cx+x,cy+y,cz+z)];i>=0;i=verts[i].next) if(mask&(1u<<verts[i].label)) {
            float d=Vector3DistanceSqr(p,verts[i].p); if(d<bd) { bd=d; best=i; }
        }
    *distance=best>=0?sqrtf(bd):1e9f; return best;
}

typedef struct { float pad,inside,cheek,eye,elbow,neck,arms,total; } Terms;
static SwatCharacterRuntime* runtime; static SwatSim* sim; static Vector3 grip[2];
static const char* hands[2]={"mixamorig:LeftHand","mixamorig:RightHand"};
static const char* arms[2]={"mixamorig:LeftArm","mixamorig:RightArm"};
static const char* fores[2]={"mixamorig:LeftForeArm","mixamorig:RightForeArm"};

static void pose_sample(int row,float pitch) {
    SwatActor* actor=&sim->actors[0];
    bool crouch=row==2||row==3; b3Vec3 v={0,0,0};
    if(row==1||row==3) v=(b3Vec3){1,0,0}; if(row==4) v=(b3Vec3){0,0,-1};
    swat_body_set_crouch(&actor->controller.body,crouch); actor->controller.body.onGround=true;
    actor->controller.eye_height=actor->controller.body.totalHeight-.2032f;
    actor->controller.yaw=0; actor->controller.pitch=pitch*SWAT_RAD; actor->controller.ads=0; actor->controller.ready_blend=0;
    actor->controller.recoil_pitch=actor->controller.recoil_yaw=actor->controller.lean=0;
    b3Body_SetLinearVelocity(actor->controller.body.body,v);
    runtime->actors[0].valid=false; sim->tick++; swat_character_runtime_prepare(runtime,sim);
    runtime->actors[0].phase=.3; sim->tick+=14; swat_character_runtime_prepare(runtime,sim);
}
static Terms score_sample(int row,float pitch) {
    pose_sample(row,pitch);
    SwatCharacterActorPose* c=&runtime->actors[0]; assert(c->valid);
    SwatCharacterAsset* a=runtime->banks[c->bank].asset;
    assert(swat_character_restore_pose(a,c->matrices,c->count)); assert(swat_character_skin(a));
    grid_build(a,c->bank);
    Matrix gun=compose(nodem(a,"Prop_Rifle"),fm(rifle_joint)),inv=MatrixInvert(gun);
    Terms t={0};
    // Rifle surface: pad gap, penetration, comb-to-cheek gap.
    float pad=1e9f,cheek=1e9f,depth=0; SwatArtInfo info=swat_character_info(a);
    for(int m=0;m<info.meshes;m++) {
        const SwatArtMesh* s=swat_character_mesh(a,m); if(strcmp(s->node_name,"Rifle 7")) continue;
        for(int v=0;v<s->vertices;v+=3) {
            Vector3 p={s->positions[v*3],s->positions[v*3+1],s->positions[v*3+2]},q=Vector3Transform(p,inv); float d; int b;
            if(q.x<.02f) { nearest(p,(1u<<TORSO)|(1u<<UPPER_R),&d); pad=fminf(pad,d); }
            if(q.x<.30f && q.y>0) { nearest(p,1u<<HEAD,&d); cheek=fminf(cheek,d); }
            b=nearest(p,(1u<<HEAD)|(1u<<TORSO)|(1u<<UPPER_R)|(1u<<UPPER_L),&d);
            if(b>=0 && d<.03f) { float s2=Vector3DotProduct(Vector3Subtract(p,verts[b].p),verts[b].n); if(s2<0) depth=fmaxf(depth,-s2); }
        }
    }
    t.pad=fmaxf(0,100*pad-.5f); if(pad>1e8f) t.pad=8;
    t.cheek=fmaxf(0,100*cheek-.5f); if(cheek>1e8f) t.cheek=8;
    t.inside=2*100*depth;
    // Eye on the sight line, behind the rear sight.
    SwatPose pose=swat_pose(&sim->actors[0].controller,&sim->actors[0].arsenal);
    Matrix inverse_root=MatrixInvert(c->root); inverse_root.m12=inverse_root.m13=inverse_root.m14=0;
    Vector3 look=Vector3Transform((Vector3){pose.forward.x,pose.forward.y,pose.forward.z},inverse_root);
    Vector3 h=org(nodem(a,"mixamorig:Head")),up=Vector3Normalize(Vector3Subtract(org(nodem(a,"mixamorig:HeadTop_End")),h));
    Vector3 f=Vector3Normalize(Vector3Subtract(look,Vector3Scale(up,Vector3DotProduct(look,up)))),r=Vector3CrossProduct(f,up);
    Vector3 eye=Vector3Add(h,Vector3Add(Vector3Scale(up,.115f),Vector3Add(Vector3Scale(f,.085f),Vector3Scale(r,.03f))));
    Vector3 e=Vector3Transform(eye,inv);
    t.eye=.5f*100*hypotf(e.y-.1247f,e.z)+.3f*100*(fmaxf(0,.12f-e.x)+fmaxf(0,e.x-.30f));
    // Chest frame for elbows and neck.
    Vector3 s2=org(nodem(a,"mixamorig:Spine2"));
    Vector3 cu=Vector3Normalize(Vector3Subtract(org(nodem(a,"mixamorig:Neck")),s2));
    Vector3 cr=Vector3Normalize(Vector3Subtract(org(nodem(a,"mixamorig:RightArm")),org(nodem(a,"mixamorig:LeftArm"))));
    Vector3 cf=Vector3Normalize(Vector3CrossProduct(cu,cr)); cr=Vector3CrossProduct(cf,cu);
    const float drop_target[2]={.75f,.6f};
    for(int s=0;s<2;s++) {
        Vector3 sh=org(nodem(a,arms[s])),el=org(nodem(a,fores[s])),hd=org(nodem(a,hands[s]));
        Vector3 upper=Vector3Normalize(Vector3Subtract(el,sh));
        float drop=-Vector3DotProduct(upper,cu),out=(s?1:-1)*Vector3DotProduct(Vector3Subtract(el,sh),cr);
        t.elbow+=3*fabsf(drop-drop_target[s])+.3f*fmaxf(0,100*out-10);
        float reach=Vector3Distance(sh,hd)/(Vector3Distance(sh,el)+Vector3Distance(el,hd));
        if(reach>.98f) t.arms+=2;
        t.arms+=.1f*100*Vector3Distance(Vector3Transform(hd,inv),grip[s]);
    }
    float neck=acosf(Clamp(Vector3DotProduct(up,cu),-1,1))/SWAT_RAD; t.neck=.2f*fmaxf(0,neck-30);
    t.total=t.pad+t.inside+t.cheek+t.eye+t.elbow+t.neck+t.arms;
    return t;
}
static const int rows[]={0,0,0,1,1,1,2,2,2,3,3,4,0,0};
static const float pitches[]={-30,0,30,-30,0,30,-30,0,30,0,30,0,-60,60};
static const float weights[]={1,2,1,1,2,1,1,2,1,1,1,1,.5f,.5f};
enum { SAMPLES=sizeof(rows)/sizeof(*rows) };
static float score(bool verbose) {
    float total=0; Terms sum={0};
    for(int i=0;i<SAMPLES;i++) {
        Terms t=score_sample(rows[i],pitches[i]); total+=weights[i]*t.total;
        sum.pad+=t.pad; sum.inside+=t.inside; sum.cheek+=t.cheek; sum.eye+=t.eye; sum.elbow+=t.elbow; sum.neck+=t.neck; sum.arms+=t.arms;
        if(verbose) printf("row%d pitch%+4.0f total %6.2f  pad %5.2f inside %5.2f cheek %5.2f eye %5.2f elbow %5.2f neck %5.2f arms %5.2f\n",
            rows[i],pitches[i],t.total,t.pad,t.inside,t.cheek,t.eye,t.elbow,t.neck,t.arms);
    }
    if(verbose) printf("TERMS pad %.1f inside %.1f cheek %.1f eye %.1f elbow %.1f neck %.1f arms %.1f\n",sum.pad,sum.inside,sum.cheek,sum.eye,sum.elbow,sum.neck,sum.arms);
    return total;
}
static float gauss(void) { float u=(float)((rand()+1.0)/(RAND_MAX+2.0)),v=(float)((rand()+1.0)/(RAND_MAX+2.0)); return sqrtf(-2*logf(u))*cosf(6.2831853f*v); }

int main(int argc,char** argv) {
    if(argc<2) { fprintf(stderr,"usage: %s ASSET_DIR [--load FILE] [--climb N] [--save FILE] [--seed N] [-v]\n",argv[0]); return 2; }
    const char* load=NULL,*save=NULL; int climb=0,seed=1; bool verbose=false;
    for(int i=2;i<argc;i++) {
        if(!strcmp(argv[i],"--load") && i+1<argc) load=argv[++i];
        else if(!strcmp(argv[i],"--save") && i+1<argc) save=argv[++i];
        else if(!strcmp(argv[i],"--climb") && i+1<argc) climb=atoi(argv[++i]);
        else if(!strcmp(argv[i],"--seed") && i+1<argc) seed=atoi(argv[++i]);
        else if(!strcmp(argv[i],"-v")) verbose=true;
    }
    setenv("SWAT_CHARACTER_ASSETS",argv[1],1); SetTraceLogLevel(LOG_WARNING);
    SetConfigFlags(FLAG_WINDOW_HIDDEN); InitWindow(64,64,"officer fit");
    SwatLighting light={0}; swat_lighting_init(&light); static SwatSim lsim; swat_lighting_prepare(&light,&lsim,(Vector3){3,1.7f,3},false,empty);
    SwatWeaponArt weapons={0}; swat_weapon_art_init(&weapons);
    runtime=swat_character_runtime_open(&weapons); assert(runtime);
    for(int h=0;h<2;h++) grip[h]=org(runtime->grip_local[h]);
    sim=calloc(1,sizeof(*sim)); SwatConfig config=swat_default_config(); config.hostile_fire=false; config.mission=SWAT_ANNEX; swat_sim_init(sim,config,42);
    if(load && !aim_tuning_load(&swat_aim_tuning,load)) { fprintf(stderr,"cannot load %s\n",load); return 1; }
    float best=score(verbose); printf("START score %.3f\n",best); fflush(stdout);
    srand((unsigned)seed); float sigma[AIM_PARAMS];
    for(int i=0;i<AIM_PARAMS;i++) sigma[i]=.15f*(aim_param_hi[i]-aim_param_lo[i]);
    SwatAimTuning current=swat_aim_tuning; int accepted=0;
    for(int it=0;it<climb;it++) {
        SwatAimTuning trial=current; int moves=1+rand()%3; int which[3];
        for(int m=0;m<moves;m++) { int i=which[m]=rand()%AIM_PARAMS; float* p=aim_param(&trial,i); *p=Clamp(*p+sigma[i]*gauss(),aim_param_lo[i],aim_param_hi[i]); }
        swat_aim_tuning=trial; float s=score(false);
        // 1/5th success rule per touched parameter.
        for(int m=0;m<moves;m++) sigma[which[m]]*=s<best ? 1.5f : .92f;
        for(int m=0;m<moves;m++) { int i=which[m]; float span=aim_param_hi[i]-aim_param_lo[i]; sigma[i]=Clamp(sigma[i],.005f*span,.4f*span); }
        if(s<best) { best=s; current=trial; accepted++; printf("it %4d score %.3f\n",it,best); fflush(stdout); }
    }
    swat_aim_tuning=current;
    if(climb) { printf("BEST score %.3f after %d iterations (%d accepted)\n",best,climb,accepted); score(true); aim_tuning_print(stdout,&current); }
    if(save) { FILE* f=fopen(save,"w"); if(f) { aim_tuning_print(f,&current); fclose(f); } }
    swat_character_runtime_close(runtime); swat_weapon_art_close(&weapons); swat_lighting_close(&light); CloseWindow();
    return 0;
}
