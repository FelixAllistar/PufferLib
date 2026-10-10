#include "protocol.h"
#include "motel.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static SwatSim sim,replica;
static bool same_triangle(const b3MeshData* a,int ai,const b3MeshData* b,int bi) {
    b3MeshTriangle x=b3GetMeshTriangles(a)[ai],y=b3GetMeshTriangles(b)[bi];
    const b3Vec3* av=b3GetMeshVertices(a),*bv=b3GetMeshVertices(b);
    return b3LengthSquared(b3Sub(av[x.index1],bv[y.index1]))<1e-14f &&
        b3LengthSquared(b3Sub(av[x.index2],bv[y.index2]))<1e-14f &&
        b3LengthSquared(b3Sub(av[x.index3],bv[y.index3]))<1e-14f;
}
static void check(SwatWorld* world) {
    bool tested[SWAT_MOTEL_ASSETS]={0};int assets=0,triangles=0,rays=0;
    for(int owner=1;owner<world->count;owner++) {
        SwatObject* o=&world->objects[owner];if(!o->active||!o->query_mesh.data)continue;
        SwatMotelInstance placement;
        int asset;
        if(owner<=SWAT_MOTEL_INSTANCES)asset=swat_motel_instance(owner-1)->asset;
        else if(swat_motel_mounted(world,owner,&placement)||swat_motel_prop(world,owner,&placement))asset=placement.asset;
        else continue;
        if(tested[asset])continue;
        tested[asset]=true;assets++;
        const b3MeshData* source=o->query_mesh.data;
        int count=b3Body_GetShapeCount(o->body);assert(count>1 && count<64);
        b3ShapeId shapes[64];assert(b3Body_GetShapes(o->body,shapes,64)==count);
        bool* seen=calloc((size_t)source->triangleCount,sizeof(bool));assert(seen);int sum=0;
        for(int part=0;part<count;part++) {
            b3Mesh mesh=b3Shape_GetMesh(shapes[part]);assert(mesh.data->triangleCount<=240);sum+=mesh.data->triangleCount;
            assert(!memcmp(&mesh.scale,&o->query_mesh.scale,sizeof(mesh.scale)));
            for(int t=0;t<mesh.data->triangleCount;t++) {
                int found=-1;for(int s=0;s<source->triangleCount;s++)if(!seen[s]&&same_triangle(mesh.data,t,source,s)){found=s;break;}
                assert(found>=0);seen[found]=true;
                // A partition must not turn a shared edge into
                // a false convex seam or lose concavity handling.
                assert(b3GetMeshFlags(mesh.data)[t]==b3GetMeshFlags(source)[found]);
            }
        }
        assert(sum==source->triangleCount);triangles+=sum;free(seen);
        b3BodyDef body_def=b3DefaultBodyDef();b3BodyId reference_body=b3CreateBody(world->id,&body_def);
        b3ShapeDef shape_def=b3DefaultShapeDef();SwatObject reference=*o;reference.query_mesh=(b3Mesh){0};
        reference.shape=b3CreateMeshShape(reference_body,&shape_def,source,o->query_mesh.scale);
        b3Vec3 lower=source->bounds.lowerBound,upper=source->bounds.upperBound;
        for(int axis=0;axis<3;axis++)for(int side=0;side<2;side++)for(int i=0;i<20;i++)for(int j=0;j<20;j++) {
            float lo[3]={lower.x,lower.y,lower.z},hi[3]={upper.x,upper.y,upper.z},origin[3],delta[3]={0};
            for(int a=0;a<3;a++)origin[a]=lo[a]+(hi[a]-lo[a])*.5f;
            origin[axis]=side?hi[axis]+.2f:lo[axis]-.2f;
            origin[(axis+1)%3]=lo[(axis+1)%3]+(hi[(axis+1)%3]-lo[(axis+1)%3])*(i+.37f)/20;
            origin[(axis+2)%3]=lo[(axis+2)%3]+(hi[(axis+2)%3]-lo[(axis+2)%3])*(j+.61f)/20;
            delta[axis]=(side?-1:1)*(hi[axis]-lo[axis]+.4f);
            b3RayCastInput in={.origin={origin[0],origin[1],origin[2]},.translation={delta[0],delta[1],delta[2]},.maxFraction=1};
            b3Mesh original={source,{1,1,1}};b3CastOutput a=b3RayCastMesh(&original,&in),b={0};b.fraction=1;
            for(int part=0;part<count;part++) {
                b3Mesh mesh=b3Shape_GetMesh(shapes[part]);mesh.scale=swat_v(1,1,1);
                b3CastOutput hit=b3RayCastMesh(&mesh,&in);if(hit.hit&&(!b.hit||hit.fraction<b.fraction))b=hit;
            }
            assert(a.hit==b.hit);if(a.hit) {
                assert(fabsf(a.fraction-b.fraction)<1e-5f);
                b3Vec3 local=b3Add(in.origin,swat_mul(in.translation,a.fraction));
                local=swat_v(local.x*o->query_mesh.scale.x,local.y*o->query_mesh.scale.y,local.z*o->query_mesh.scale.z);
                b3Quat rotation=b3MakeQuatFromAxisAngle(swat_v(0,1,0),o->yaw);
                b3Pos entry=b3OffsetPos(o->center,b3RotateVector(rotation,local));
                b3Vec3 direction=b3RotateVector(rotation,swat_normalize(in.translation));
                assert(fabsf(swat_world_exit_distance(o,entry,direction)-swat_world_exit_distance(&reference,entry,direction))<1e-5f);
            }rays++;
        }
        b3DestroyBody(reference_body);
    }
    assert(assets>=25 && tested[24] && tested[40] && tested[44] && tested[45] && tested[46] && tested[47]);
    printf("PASS motel contact partitions: %d assets, %d original triangles and edge flags preserved, %d matched full/partition rays and ballistic exit distances\n",assets,triangles,rays);
}
int main(void) {
    SwatConfig config=swat_default_config();config.mission=SWAT_MOTEL;config.randomize=false;config.hostile_fire=false;
    swat_sim_init(&sim,config,81);check(&sim.world);
    static SwatMap map,decoded;static unsigned char bytes[SWAT_NET_PACKET_MAX];
    swat_capture_map(&sim,1,&map);size_t size=swat_encode_map(bytes,sizeof(bytes),&map);assert(size&&swat_decode_map(&decoded,bytes,size));
    swat_apply_map(&replica,&decoded);check(&replica.world);
    swat_sim_close(&replica);assert(!replica.world.motel_contact_meshes&&!replica.world.motel_contact_mesh_count);
    int parts=sim.world.motel_contact_mesh_count;swat_sim_reset(&sim);assert(sim.world.motel_contact_mesh_count==parts);swat_sim_close(&sim);assert(!sim.world.motel_contact_meshes);
    puts("PASS motel contact partitions: exact encoded-map reconstruction and reset/close ownership");
}
