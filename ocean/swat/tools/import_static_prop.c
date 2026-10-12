// Native C full-scene importer. Usage: importer runtime.glb symbol_prefix
#include "../vendor/cgltf_private.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(int argc,char** argv) {
    assert(argc==3);const char* symbol=argv[2];
    for(const char* s=symbol;*s;s++)assert((*s>='a'&&*s<='z')||(*s>='0'&&*s<='9')||*s=='_');
    cgltf_options options={0};cgltf_data* data=NULL;
    assert(cgltf_parse_file(&options,argv[1],&data)==cgltf_result_success);
    assert(cgltf_load_buffers(&options,data,argv[1])==cgltf_result_success&&cgltf_validate(data)==cgltf_result_success);
    assert(!data->skins_count&&!data->animations_count&&data->materials_count==1);
    size_t count=0,total=0;float lo[]={INFINITY,INFINITY,INFINITY},hi[]={-INFINITY,-INFINITY,-INFINITY};
    for(size_t n=0;n<data->nodes_count;n++)if(data->nodes[n].mesh){
        cgltf_mesh* m=data->nodes[n].mesh;assert(m->primitives_count==1);cgltf_primitive* p=m->primitives;
        assert(p->type==cgltf_primitive_type_triangles&&p->indices&&p->indices->count%3==0);
        count+=cgltf_find_accessor(p,cgltf_attribute_type_position,0)->count;total+=p->indices->count;
    }
    float (*vertices)[3]=malloc(count*sizeof(*vertices));size_t* indices=malloc(total*sizeof(*indices));assert(vertices&&indices);
    size_t base=0,at=0;
    for(size_t n=0;n<data->nodes_count;n++)if(data->nodes[n].mesh){
        cgltf_primitive* p=data->nodes[n].mesh->primitives;const cgltf_accessor* positions=cgltf_find_accessor(p,cgltf_attribute_type_position,0);
        float transform[16];cgltf_node_transform_world(&data->nodes[n],transform);
        for(size_t i=0;i<positions->count;i++){
            float v[3];assert(cgltf_accessor_read_float(positions,i,v,3));
            for(int k=0;k<3;k++){float x=transform[k]*v[0]+transform[k+4]*v[1]+transform[k+8]*v[2]+transform[k+12];assert(isfinite(x));vertices[base+i][k]=x;lo[k]=fminf(lo[k],x);hi[k]=fmaxf(hi[k],x);}
        }
        for(size_t i=0;i<p->indices->count;i++){size_t ix=cgltf_accessor_read_index(p->indices,i);assert(ix<positions->count);indices[at++]=base+ix;}base+=positions->count;
    }
    assert(base==count&&at==total);float center[3],half[3];for(int k=0;k<3;k++){center[k]=(lo[k]+hi[k])*.5f;half[k]=(hi[k]-lo[k])*.5f;}
    printf("// Native C import of original scene transforms; metres.\nstatic b3Vec3 %s_vertices[]={\n",symbol);
    for(size_t i=0;i<count;i++)printf("{%.9ef,%.9ef,%.9ef},\n",vertices[i][0]-center[0],vertices[i][1]-center[1],vertices[i][2]-center[2]);
    printf("};\nstatic int32_t %s_indices[]={\n",symbol);
    for(size_t i=0;i<total;i++){printf("%zu,",indices[i]);if(i%24==23)puts("");}puts("};");
    const cgltf_pbr_metallic_roughness* m=&data->materials[0].pbr_metallic_roughness;
    printf("static const SwatMotelMaterial %s_materials[]={{%.9ef,%.9ef}};\n",symbol,m->roughness_factor,m->metallic_factor);
    const char* file=strrchr(argv[1],'/');file=file?file+1:argv[1];
    printf("static const SwatMotelAsset %s_asset={\"%s\",{%.9ef,%.9ef,%.9ef},{%.9ef,%.9ef,%.9ef},%s_vertices,%zu,%s_indices,%zu,%s_materials,1};\n",symbol,file,center[0],center[1],center[2],half[0],half[1],half[2],symbol,count,symbol,total/3,symbol);
    free(indices);free(vertices);cgltf_free(data);
}
