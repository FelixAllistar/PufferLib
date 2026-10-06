// Collision/solver benchmark, not a replacement movement controller or trainer.
// Copies the real curriculum's static geometry and uses matched character
// proxies, CCD grenade spheres, eighteen rays per decision and two clearance
// sweeps per tick. Full controller logic, rewards and resets are excluded.
#include "PxPhysicsAPI.h"
#include "sim.h"
#include "locomotion.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <ctime>
#include <omp.h>
using namespace physx;

static double wall_now() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
static double cpu_now() { timespec t; clock_gettime(CLOCK_PROCESS_CPUTIME_ID,&t); return t.tv_sec+t.tv_nsec*1e-9; }
static PxVec3 px(b3Vec3 v) { return PxVec3(v.x,v.y,v.z); }
static PxVec3 pxpos(b3Pos p) { return PxVec3(float(p.x),float(p.y),float(p.z)); }
static PxFilterFlags filter(PxFilterObjectAttributes,PxFilterData,PxFilterObjectAttributes,PxFilterData,PxPairFlags& flags,const void*,PxU32) {
    flags=PxPairFlag::eCONTACT_DEFAULT|PxPairFlag::eDETECT_CCD_CONTACT;
    return PxFilterFlag::eDEFAULT;
}
struct Errors : PxErrorCallback {
    int failures=0;
    void reportError(PxErrorCode::Enum code,const char* message,const char* file,int line) override {
        if(code!=PxErrorCode::eDEBUG_INFO && code!=PxErrorCode::eDEBUG_WARNING) failures++;
        fprintf(stderr,"PhysX %d: %s (%s:%d)\n",int(code),message,file,line);
    }
};
struct Ignore : PxQueryFilterCallback {
    const PxRigidActor* actor;
    explicit Ignore(const PxRigidActor* a) : actor(a) {}
    PxQueryHitType::Enum preFilter(const PxFilterData&,const PxShape*,const PxRigidActor* a,PxHitFlags&) override {
        return a==actor ? PxQueryHitType::eNONE : PxQueryHitType::eBLOCK;
    }
    PxQueryHitType::Enum postFilter(const PxFilterData&,const PxQueryHit&,const PxShape*,const PxRigidActor*) override { return PxQueryHitType::eBLOCK; }
};
struct BWorld { void* course; SwatBody character; b3BodyId grenade; };
static b3BodyId grenade(SwatWorld* w,b3Pos pos) {
    b3BodyDef bd=b3DefaultBodyDef(); bd.type=b3_dynamicBody; bd.position=pos;
    bd.angularDamping=.4f; bd.isBullet=true;
    b3BodyId body=b3CreateBody(w->id,&bd);
    b3ShapeDef sd=b3DefaultShapeDef(); sd.baseMaterial=swat_physics_material(SWAT_STEEL);
    sd.density=.42f/(4.f/3*SWAT_PI*.045f*.045f*.045f);
    b3Sphere sphere={{0,0,0},.045f}; b3CreateSphereShape(body,&sd,&sphere);
    return body;
}
static void box_queries(BWorld& w,bool sensors) {
    SwatSim* sim=swat_training_sim(w.course); b3Pos feet=swat_body_feet_position(&w.character);
    if(sensors) for(int row=0;row<3;row++) for(int col=0;col<5;col++)
        swat_world_ray(&sim->world,b3OffsetPos(feet,swat_v(0,.3f+row*.65f,0)),swat_direction((col-2)*30*SWAT_RAD,0),3,w.character.body);
    if(sensors) for(int i=0;i<3;i++) swat_world_ray(&sim->world,b3OffsetPos(feet,swat_v(.6f*(i+1),.6f,0)),swat_v(0,-1,0),1.2f,w.character.body);
    b3Pos up=b3OffsetPos(feet,swat_v(0,.1016f,0)),down=b3OffsetPos(feet,swat_v(0,-.0508f,0));
    swat_body_trace_body(&w.character,up,down,1,.5f);
    swat_body_trace_body(&w.character,feet,b3OffsetPos(feet,swat_v(.3f,.3f,0)),1,1);
}
static void physx_queries(PxScene& scene,PxRigidDynamic& character,float height,float radius,bool sensors) {
    PxVec3 feet=character.getGlobalPose().p-PxVec3(0,height*.5f,0);
    Ignore ignore(&character); PxQueryFilterData query; query.flags|=PxQueryFlag::ePREFILTER;
    PxRaycastBuffer ray;
    if(sensors) for(int row=0;row<3;row++) for(int col=0;col<5;col++)
        scene.raycast(feet+PxVec3(0,.3f+row*.65f,0),px(swat_direction((col-2)*30*SWAT_RAD,0)),3,ray,PxHitFlag::eDEFAULT,query,&ignore);
    if(sensors) for(int i=0;i<3;i++) scene.raycast(feet+PxVec3(.6f*(i+1),.6f,0),PxVec3(0,-1,0),1.2f,ray,PxHitFlag::eDEFAULT,query,&ignore);
    PxSweepBuffer sweep;
    scene.sweep(PxBoxGeometry(radius*.5f,height*.25f,radius*.5f),PxTransform(feet+PxVec3(0,.1016f+height*.25f,0)),PxVec3(0,-1,0),.1524f,sweep,PxHitFlag::eDEFAULT,query,&ignore);
    PxVec3 delta(.3f,.3f,0);
    scene.sweep(PxBoxGeometry(radius*.5f,height*.5f,radius*.5f),PxTransform(feet+PxVec3(0,height*.5f,0)),delta.getNormalized(),delta.magnitude(),sweep,PxHitFlag::eDEFAULT,query,&ignore);
}
static PxShape* box_shape(PxPhysics& physics,PxRigidActor& actor,const PxVec3& half,PxMaterial& material,const PxTransform& local=PxTransform(PxIdentity)) {
    PxShape* shape=physics.createShape(PxBoxGeometry(half),material,true);
    if(!shape) std::abort(); shape->setLocalPose(local); shape->setContactOffset(.005f);
    actor.attachShape(*shape); shape->release(); return shape;
}
static void static_geometry(PxPhysics& physics,PxScene& scene,const SwatWorld& world,const PxVec3& offset,PxMaterial** materials) {
    for(int i=0;i<world.count;i++) {
        const SwatObject& o=world.objects[i]; if(!o.active) continue;
        PxQuat rotation(o.yaw,PxVec3(0,1,0));
        PxRigidStatic* actor=physics.createRigidStatic(PxTransform(pxpos(o.center)+offset,rotation));
        if(o.fractured) {
            PxVec3 points[8];
            for(int j=0;j<4;j++) { points[j]=PxVec3(-o.half.x,o.corners[j][0],o.corners[j][1]); points[j+4]=PxVec3(o.half.x,o.corners[j][0],o.corners[j][1]); }
            PxConvexMeshDesc desc; desc.points.count=8; desc.points.stride=sizeof(PxVec3); desc.points.data=points; desc.flags=PxConvexFlag::eCOMPUTE_CONVEX;
            PxCookingParams params(physics.getTolerancesScale());
            PxConvexMesh* mesh=PxCreateConvexMesh(params,desc,physics.getPhysicsInsertionCallback());
            if(!mesh) std::abort();
            PxShape* shape=physics.createShape(PxConvexMeshGeometry(mesh),*materials[o.material],true);
            shape->setContactOffset(.005f); actor->attachShape(*shape); shape->release(); mesh->release();
        } else box_shape(physics,*actor,px(o.half),*materials[o.material]);
        scene.addActor(*actor);
    }
}
int main(int argc,char** argv) {
    const char* backend="box3d",*trace_path=nullptr; int envs=32,steps=300,threads=4,substeps=1;
    for(int i=1;i<argc;i++) {
        if(i+1<argc && !strcmp(argv[i],"--backend")) backend=argv[++i];
        else if(i+1<argc && !strcmp(argv[i],"--envs")) envs=atoi(argv[++i]);
        else if(i+1<argc && !strcmp(argv[i],"--steps")) steps=atoi(argv[++i]);
        else if(i+1<argc && !strcmp(argv[i],"--threads")) threads=atoi(argv[++i]);
        else if(i+1<argc && !strcmp(argv[i],"--physx-substeps")) substeps=atoi(argv[++i]);
        else if(i+1<argc && !strcmp(argv[i],"--trace")) trace_path=argv[++i];
        else return 2;
    }
    bool box=!strcmp(backend,"box3d");
    if((!box && strcmp(backend,"physx")) || envs<1 || envs>1024 || steps<1 || threads<1 || threads>32 || (substeps!=1 && substeps!=4)) return 2;
    omp_set_num_threads(threads);
    FILE* trace=trace_path ? fopen(trace_path,"w") : nullptr; if(trace_path && !trace) return 2;
    if(trace) fprintf(trace,"stage,tick,x,y,z,vx,vy,vz\n");
    PxDefaultAllocator allocator; Errors errors;
    PxFoundation* foundation=box ? nullptr : PxCreateFoundation(PX_PHYSICS_VERSION,allocator,errors);
    PxPhysics* physics=box ? nullptr : PxCreatePhysics(PX_PHYSICS_VERSION,*foundation,PxTolerancesScale(),false);
    PxDefaultCpuDispatcher* dispatcher=box ? nullptr : PxDefaultCpuDispatcherCreate(threads);
    if(!box && (!foundation || !physics || !dispatcher)) return 1;
    PxMaterial* materials[SWAT_MATERIAL_COUNT]={}; PxMaterial* character_material=nullptr;
    if(!box) {
        for(int i=0;i<SWAT_MATERIAL_COUNT;i++) {
            const SwatMaterialDef* m=swat_material(SwatMaterial(i));
            // Box3D combines coefficients geometrically; multiply their roots
            // in PhysX. Rolling resistance still differs between the engines.
            materials[i]=physics->createMaterial(sqrtf(m->friction),sqrtf(m->friction),sqrtf(m->restitution));
            materials[i]->setFrictionCombineMode(PxCombineMode::eMULTIPLY); materials[i]->setRestitutionCombineMode(PxCombineMode::eMULTIPLY);
        }
        character_material=physics->createMaterial(0,0,0);
        character_material->setFrictionCombineMode(PxCombineMode::eMULTIPLY); character_material->setRestitutionCombineMode(PxCombineMode::eMULTIPLY);
    }
    for(int stage=0;stage<5;stage++) {
        void* prototype=swat_training_create(2718,0,stage); SwatSim* source=swat_training_sim(prototype);
        std::vector<BWorld> worlds;
        std::vector<PxRigidDynamic*> characters,grenades;
        std::vector<PxVec3> offsets;
        PxScene* scene=nullptr;
        float height=stage==2 ? 1.016f : 1.8288f,radius=.4064f;
        if(!box) {
            PxSceneDesc desc(physics->getTolerancesScale()); desc.gravity=PxVec3(0,-10,0);
            desc.cpuDispatcher=dispatcher; desc.filterShader=filter; desc.flags|=PxSceneFlag::eENABLE_CCD;
            desc.solverType=PxSolverType::eTGS; desc.bounceThresholdVelocity=1;
            scene=physics->createScene(desc); if(!scene) return 1;
        }
        for(int i=0;i<envs;i++) {
            if(box) {
                BWorld w={}; w.course=swat_training_create(2718,0,stage); SwatSim* sim=swat_training_sim(w.course);
                b3Body_Disable(sim->actors[0].controller.body.body);
                swat_body_init_category(&w.character,sim->world.id,(b3Pos){-4,1.8288f*.5f+.02f,0},UINT64_C(1)<<49);
                if(stage==2) swat_body_set_crouch(&w.character,true);
                b3Body_SetGravityScale(w.character.body,1);
                w.grenade=grenade(&sim->world,(b3Pos){-3.3f,1.6f,0}); worlds.push_back(w);
            } else {
                PxVec3 offset(float((i%32)*32),0,float((i/32)*24)); offsets.push_back(offset);
                static_geometry(*physics,*scene,source->world,offset,materials);
                PxRigidDynamic* character=physics->createRigidDynamic(PxTransform(offset+PxVec3(-4,height*.5f+.02f,0)));
                box_shape(*physics,*character,PxVec3(radius*.5f,height*.25f,radius*.5f),*character_material,PxTransform(PxVec3(0,-height*.25f,0)));
                float cr=radius*.707f,lo=cr*.5f,hi=height*.5f-cr; if(hi<=lo) hi=lo+.01f;
                PxShape* capsule=physics->createShape(PxCapsuleGeometry(cr,(hi-lo)*.5f),*character_material,true);
                capsule->setLocalPose(PxTransform(PxVec3(0,(hi+lo)*.5f,0),PxQuat(PxHalfPi,PxVec3(0,0,1))));
                capsule->setContactOffset(.005f); character->attachShape(*capsule); capsule->release();
                PxRigidBodyExt::setMassAndUpdateInertia(*character,500.f);
                character->setRigidDynamicLockFlags(PxRigidDynamicLockFlag::eLOCK_ANGULAR_X|PxRigidDynamicLockFlag::eLOCK_ANGULAR_Y|PxRigidDynamicLockFlag::eLOCK_ANGULAR_Z);
                character->setSolverIterationCounts(4,1); character->setSleepThreshold(0); scene->addActor(*character); characters.push_back(character);
                PxRigidDynamic* frag=physics->createRigidDynamic(PxTransform(offset+PxVec3(-3.3f,1.6f,0)));
                PxShape* sphere=physics->createShape(PxSphereGeometry(.045f),*materials[SWAT_STEEL],true);
                sphere->setContactOffset(.005f); frag->attachShape(*sphere); sphere->release();
                PxRigidBodyExt::setMassAndUpdateInertia(*frag,.42f); frag->setAngularDamping(.4f);
                frag->setRigidBodyFlag(PxRigidBodyFlag::eENABLE_CCD,true); frag->setSolverIterationCounts(4,1);
                scene->addActor(*frag); grenades.push_back(frag);
            }
        }
        double start=wall_now(),cpu_start=cpu_now();
        for(int tick=0;tick<steps*4;tick++) {
            if(box) {
#pragma omp parallel for
                for(int i=0;i<envs;i++) {
                    BWorld& w=worlds[i]; SwatWorld* world=&swat_training_sim(w.course)->world;
                    if(tick%120==0) {
                        b3Body_SetTransform(w.character.body,(b3Pos){-4,height*.5f+.02f,0},b3Quat_identity);
                        b3Body_SetLinearVelocity(w.character.body,swat_v(0,0,0));
                        b3Body_SetTransform(w.grenade,(b3Pos){-3.3f,1.6f,0},b3Quat_identity);
                        b3Body_SetLinearVelocity(w.grenade,swat_v(10,2.2f,0)); b3Body_SetAngularVelocity(w.grenade,swat_v(0,0,12));
                    }
                    b3Vec3 velocity=b3Body_GetLinearVelocity(w.character.body); velocity.x=2.8f; velocity.z=0; b3Body_SetLinearVelocity(w.character.body,velocity);
                    b3World_Step(world->id,SWAT_DT,4); box_queries(w,tick%4==3);
                }
            } else {
                for(int i=0;i<envs;i++) {
                    if(tick%120==0) {
                        characters[i]->setGlobalPose(PxTransform(offsets[i]+PxVec3(-4,height*.5f+.02f,0)));
                        characters[i]->setLinearVelocity(PxVec3(0));
                        grenades[i]->setGlobalPose(PxTransform(offsets[i]+PxVec3(-3.3f,1.6f,0)));
                        grenades[i]->setLinearVelocity(PxVec3(10,2.2f,0)); grenades[i]->setAngularVelocity(PxVec3(0,0,12));
                    }
                    PxVec3 velocity=characters[i]->getLinearVelocity(); velocity.x=2.8f; velocity.z=0; characters[i]->setLinearVelocity(velocity);
                }
                for(int sub=0;sub<substeps;sub++) { scene->simulate(SWAT_DT/substeps); if(!scene->fetchResults(true)) return 1; }
#pragma omp parallel for
                for(int i=0;i<envs;i++) physx_queries(*scene,*characters[i],height,radius,tick%4==3);
            }
            if(trace) {
                PxVec3 p=box ? pxpos(b3Body_GetPosition(worlds[0].grenade)) : grenades[0]->getGlobalPose().p;
                PxVec3 v=box ? px(b3Body_GetLinearVelocity(worlds[0].grenade)) : grenades[0]->getLinearVelocity();
                fprintf(trace,"%d,%d,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g\n",stage,tick,p.x,p.y,p.z,v.x,v.y,v.z);
            }
        }
        double elapsed=wall_now()-start,cpu=cpu_now()-cpu_start;
        printf("backend=%s stage=%d envs=%d threads=%d decisions=%d calls_per_tick=%d solver_substeps=%d seconds=%.6f cpu_seconds=%.6f decisions_per_second=%.0f\n",backend,stage,envs,threads,steps*envs,box ? 1 : substeps,box ? 4 : 4*substeps,elapsed,cpu,steps*envs/elapsed); fflush(stdout);
        for(BWorld& w:worlds) swat_training_close(w.course);
        if(scene) scene->release(); swat_training_close(prototype);
    }
    if(!box) {
        character_material->release(); for(PxMaterial* m:materials) m->release();
        physics->release(); dispatcher->release(); foundation->release();
    }
    if(trace) fclose(trace);
    return errors.failures ? 1 : 0;
}
