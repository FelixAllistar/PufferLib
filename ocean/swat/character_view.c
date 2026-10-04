#include "character_view.h"
#include "rlgl.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

void swat_character_view_close(SwatCharacterView* view) {
    // This view owns only default maps, so it can safely release a partially
    // initialized upload without dereferencing absent material maps.
    if(view->model.meshes) for(int i=0;i<view->model.meshCount;i++) UnloadMesh(view->model.meshes[i]);
    if(view->model.materials) for(int i=0;i<view->model.materialCount;i++) MemFree(view->model.materials[i].maps);
    MemFree(view->model.meshes); MemFree(view->model.materials); MemFree(view->model.meshMaterial); swat_character_free(view->asset); free(view->visible); memset(view,0,sizeof(*view));
}
bool swat_character_view_init(SwatCharacterView* view,const char* path,char* error,size_t capacity) {
    if(view->asset) return false;
    view->asset=swat_character_load(path,32,error,capacity); if(!view->asset) return false;
    SwatArtInfo info=swat_character_info(view->asset); Model* model=&view->model;
    model->transform=(Matrix){.m0=1,.m5=1,.m10=1,.m15=1}; model->meshCount=model->materialCount=0;
    model->meshes=MemAlloc((unsigned)(sizeof(Mesh)*(size_t)info.meshes)); model->materials=MemAlloc((unsigned)(sizeof(Material)*(size_t)info.meshes));
    model->meshMaterial=MemAlloc((unsigned)(sizeof(int)*(size_t)info.meshes)); view->visible=calloc((size_t)info.meshes,1);
    if(!model->meshes || !model->materials || !model->meshMaterial || !view->visible) goto failed;
    memset(model->meshes,0,sizeof(Mesh)*(size_t)info.meshes); memset(model->materials,0,sizeof(Material)*(size_t)info.meshes);
    for(int i=0;i<info.meshes;i++) {
        const SwatArtMesh* source=swat_character_mesh(view->asset,i); Mesh* mesh=&model->meshes[i]; model->meshCount=i+1;
        // Raylib's index buffer is uint16. Expand larger primitives instead of
        // truncating their indices; source bind/deformation data stays intact.
        bool indexed=source->vertices<=65535; mesh->vertexCount=indexed ? source->vertices : source->triangles*3; mesh->triangleCount=source->triangles;
        mesh->vertices=MemAlloc((unsigned)((size_t)mesh->vertexCount*3*sizeof(float))); mesh->normals=MemAlloc((unsigned)((size_t)mesh->vertexCount*3*sizeof(float)));
        mesh->texcoords=MemAlloc((unsigned)((size_t)mesh->vertexCount*2*sizeof(float)));
        if(indexed) mesh->indices=MemAlloc((unsigned)((size_t)source->triangles*3*sizeof(unsigned short)));
        if(!mesh->vertices || !mesh->normals || !mesh->texcoords || (indexed && !mesh->indices)) goto failed;
        for(int j=0;j<mesh->vertexCount;j++) {
            int from=indexed ? j : (int)source->indices[j];
            memcpy(mesh->vertices+j*3,source->positions+from*3,3*sizeof(float)); memcpy(mesh->normals+j*3,source->normals+from*3,3*sizeof(float)); memcpy(mesh->texcoords+j*2,source->texcoords+from*2,2*sizeof(float));
        }
        if(indexed) for(int j=0;j<source->triangles*3;j++) mesh->indices[j]=(unsigned short)source->indices[j];
        UploadMesh(mesh,true); if(!mesh->vaoId) goto failed;
        model->materials[i]=LoadMaterialDefault(); model->materialCount=i+1; model->meshMaterial[i]=i;
        if(!model->materials[i].maps) goto failed;
        model->materials[i].maps[MATERIAL_MAP_ALBEDO].color=(Color){(unsigned char)(powf(source->base_color[0],1.0f/2.2f)*255+.5f),(unsigned char)(powf(source->base_color[1],1.0f/2.2f)*255+.5f),(unsigned char)(powf(source->base_color[2],1.0f/2.2f)*255+.5f),(unsigned char)(source->base_color[3]*255)};
        model->materials[i].maps[MATERIAL_MAP_ROUGHNESS].value=source->roughness; model->materials[i].maps[MATERIAL_MAP_METALNESS].value=source->metalness;
        view->visible[i]=source->visible;
    }
    return true;
failed:
    if(error && capacity) snprintf(error,capacity,"Cannot allocate/upload character preview buffers.");
    swat_character_view_close(view); return false;
}
bool swat_character_view_sample(SwatCharacterView* view,const char* clip,double time) {
    if(!swat_character_sample(view->asset,clip,time)) return false;
    for(int i=0;i<view->model.meshCount;i++) {
        const SwatArtMesh* source=swat_character_mesh(view->asset,i); Mesh* mesh=&view->model.meshes[i]; view->visible[i]=source->visible;
        if(!source->visible) continue;
        for(int j=0;j<mesh->vertexCount;j++) {
            int from=mesh->indices ? j : (int)source->indices[j];
            memcpy(mesh->vertices+j*3,source->positions+from*3,3*sizeof(float)); memcpy(mesh->normals+j*3,source->normals+from*3,3*sizeof(float));
        }
        UpdateMeshBuffer(*mesh,0,mesh->vertices,mesh->vertexCount*3*(int)sizeof(float),0);
        UpdateMeshBuffer(*mesh,2,mesh->normals,mesh->vertexCount*3*(int)sizeof(float),0);
    }
    return true;
}
void swat_character_view_draw(const SwatCharacterView* view,const SwatLighting* lighting,Matrix root) {
    Shader shader=lighting && lighting->enabled && lighting->prepared ? lighting->mesh.shader : (Shader){rlGetShaderIdDefault(),rlGetShaderLocsDefault()};
    rlDrawRenderBatchActive(); rlDisableBackfaceCulling();
    if(lighting && lighting->enabled && lighting->prepared) {
        int disabled=0;
        SetShaderValue(shader,lighting->mesh.pbr,&disabled,SHADER_UNIFORM_INT);
        SetShaderValue(shader,lighting->mesh.normal_map,&disabled,SHADER_UNIFORM_INT);
    }
    for(int i=0;i<view->model.meshCount;i++) if(view->visible[i]) {
        Material material=view->model.materials[view->model.meshMaterial[i]]; material.shader=shader;
        if(!swat_character_mesh(view->asset,i)->double_sided) rlEnableBackfaceCulling(); else rlDisableBackfaceCulling();
        DrawMesh(view->model.meshes[i],material,root);
    }
    rlEnableBackfaceCulling();
}
