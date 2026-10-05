#include "character_view.h"
#include "rlgl.h"
#include "raymath.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

void swat_character_view_close(SwatCharacterView* view) {
    for(int i=0;i<view->model.meshCount;i++) {
        if(view->arms) UnloadMesh(view->arms[i]);
        if(view->influence_maps && view->influence_maps[i].id) UnloadTexture(view->influence_maps[i]);
        if(view->palette_maps && view->palette_maps[i].id) UnloadTexture(view->palette_maps[i]);
        if(view->palette_pixels) free(view->palette_pixels[i]);
    }
    free(view->influence_maps); free(view->palette_maps); free(view->palette_pixels);
    free(view->arms);
    if(view->fallback.id && view->fallback.id!=rlGetShaderIdDefault()) UnloadShader(view->fallback);
    // This view owns only default maps, so it can safely release a partially
    // initialized upload without dereferencing absent material maps.
    if(view->model.meshes) for(int i=0;i<view->model.meshCount;i++) UnloadMesh(view->model.meshes[i]);
    if(view->model.materials) for(int i=0;i<view->model.materialCount;i++) MemFree(view->model.materials[i].maps);
    MemFree(view->model.meshes); MemFree(view->model.materials); MemFree(view->model.meshMaterial); swat_character_free(view->asset); free(view->visible); memset(view,0,sizeof(*view));
}
bool swat_character_view_init_gpu(SwatCharacterView* view,const char* path,char* error,size_t capacity) {
    if(!swat_character_view_init(view,path,error,capacity)) return false;
    int count=view->model.meshCount;
    view->influence_maps=calloc((size_t)count,sizeof(Texture2D)); view->palette_maps=calloc((size_t)count,sizeof(Texture2D));
    view->palette_pixels=calloc((size_t)count,sizeof(float*)); view->fallback=swat_lighting_skin_shader();
    view->arms=calloc((size_t)count,sizeof(Mesh));
    if(!view->influence_maps || !view->palette_maps || !view->palette_pixels || !view->arms || view->fallback.id==rlGetShaderIdDefault()) goto failed;
    for(int i=0;i<count;i++) {
        Mesh* mesh=&view->model.meshes[i]; const SwatArtMesh* source=swat_character_mesh(view->asset,i);
        int sets=source->influences/4,texels=mesh->vertexCount*sets*2,width=512,height=texels ? (texels+width-1)/width : 1;
        if(height>8192) goto failed;
        float* pixels=calloc((size_t)width*height*4,sizeof(float)); if(!pixels) goto failed;
        for(int j=0;j<mesh->vertexCount;j++) {
            int from=mesh->indices ? j : (int)source->indices[j];
            memcpy(mesh->vertices+j*3,source->bind_positions+from*3,3*sizeof(float)); memcpy(mesh->normals+j*3,source->bind_normals+from*3,3*sizeof(float));
            for(int s=0;s<sets;s++) for(int k=0;k<4;k++) {
                size_t at=(size_t)from*source->influences+s*4+k,texel=((size_t)j*sets+s)*8;
                pixels[texel+k]=source->joint_ids[at]; pixels[texel+4+k]=source->joint_weights[at];
            }
        }
        Image image={pixels,width,height,1,PIXELFORMAT_UNCOMPRESSED_R32G32B32A32};
        view->influence_maps[i]=LoadTextureFromImage(image); free(pixels);
        width=256; height=(source->palette_count*4+width-1)/width;
        view->palette_pixels[i]=calloc((size_t)width*height*4,sizeof(float)); if(!view->palette_pixels[i]) goto failed;
        image=(Image){view->palette_pixels[i],width,height,1,PIXELFORMAT_UNCOMPRESSED_R32G32B32A32};
        view->palette_maps[i]=LoadTextureFromImage(image);
        if(!view->influence_maps[i].id || !view->palette_maps[i].id) goto failed;
        SetTextureFilter(view->influence_maps[i],TEXTURE_FILTER_POINT); SetTextureFilter(view->palette_maps[i],TEXTURE_FILTER_POINT);
        UpdateMeshBuffer(*mesh,0,mesh->vertices,mesh->vertexCount*3*(int)sizeof(float),0);
        UpdateMeshBuffer(*mesh,2,mesh->normals,mesh->vertexCount*3*(int)sizeof(float),0);
        if(swat_character_body_mesh(source->node_name) && mesh->indices && source->influences) {
            // First person keeps forearms/gloves; upper shoulders otherwise
            // sweep through the camera in the source working-carry reload.
            float* arm_weights=calloc((size_t)mesh->vertexCount,sizeof(float)); if(!arm_weights) goto failed;
            for(int j=0;j<mesh->vertexCount;j++) for(int w=0;w<source->influences;w++) {
                size_t at=(size_t)j*source->influences+w; if(!source->joint_weights[at]) continue;
                const char* name=swat_character_node_name(view->asset,source->joint_nodes[source->joint_ids[at]]);
                if(name && (strstr(name,"ForeArm") || strstr(name,"Hand"))) arm_weights[j]+=source->joint_weights[at];
            }
            int triangles=0;
            for(int t=0;t<mesh->triangleCount;t++) if(arm_weights[mesh->indices[t*3]]>.5f && arm_weights[mesh->indices[t*3+1]]>.5f && arm_weights[mesh->indices[t*3+2]]>.5f) triangles++;
            if(triangles) {
                Mesh* arms=&view->arms[i]; arms->vertexCount=mesh->vertexCount; arms->triangleCount=triangles;
                arms->vertices=MemAlloc((unsigned)mesh->vertexCount*3*sizeof(float)); arms->normals=MemAlloc((unsigned)mesh->vertexCount*3*sizeof(float)); arms->texcoords=MemAlloc((unsigned)mesh->vertexCount*2*sizeof(float));
                arms->indices=MemAlloc((unsigned)triangles*3*sizeof(unsigned short));
                if(!arms->vertices || !arms->normals || !arms->texcoords || !arms->indices) { free(arm_weights); goto failed; }
                memcpy(arms->vertices,mesh->vertices,(size_t)mesh->vertexCount*3*sizeof(float)); memcpy(arms->normals,mesh->normals,(size_t)mesh->vertexCount*3*sizeof(float)); memcpy(arms->texcoords,mesh->texcoords,(size_t)mesh->vertexCount*2*sizeof(float));
                int at=0; for(int t=0;t<mesh->triangleCount;t++) if(arm_weights[mesh->indices[t*3]]>.5f && arm_weights[mesh->indices[t*3+1]]>.5f && arm_weights[mesh->indices[t*3+2]]>.5f) { memcpy(arms->indices+at*3,mesh->indices+t*3,3*sizeof(unsigned short)); at++; }
                UploadMesh(arms,false); if(!arms->vaoId) { free(arm_weights); goto failed; }
            }
            free(arm_weights);
        }
    }
    view->gpu=true; return true;
failed:
    if(error && capacity) snprintf(error,capacity,"Full-influence GPU buffers/shader unavailable.");
    swat_character_view_close(view); return false;
}
bool swat_character_view_init(SwatCharacterView* view,const char* path,char* error,size_t capacity) {
    if(view->asset) return false;
    view->weapon_normal_scale=1;
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
    if(view->gpu) {
        if(!swat_character_sample_pose(view->asset,clip,time)) return false;
        for(int i=0;i<view->model.meshCount;i++) view->visible[i]=swat_character_mesh(view->asset,i)->visible;
        return true;
    }
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
static void draw(const SwatCharacterView* view,SwatLighting* lighting,Matrix root,bool first_person) {
    if(!swat_character_finalize_pose(view->asset)) return;
    Shader shader=lighting && lighting->enabled && lighting->prepared ? lighting->mesh.shader : view->gpu ? view->fallback : (Shader){rlGetShaderIdDefault(),rlGetShaderLocsDefault()};
    rlDrawRenderBatchActive(); rlDisableBackfaceCulling();
    bool rigid_body=false;
    for(int i=0;i<view->model.meshCount;i++) if(view->visible[i]) {
        const char* name=swat_character_mesh(view->asset,i)->node_name;
        if(view->rigid_rifle && view->rigid_rifle->meshCount && !strcmp(name,"Rifle 7")) {
            rigid_body=true; continue; // Avoid drawing the old body over its revision.
        }
        Mesh mesh=view->model.meshes[i];
        if(first_person) {
            if(swat_character_body_mesh(name)) { if(!view->arms || !view->arms[i].vaoId) continue; mesh=view->arms[i]; }
            else if(strcmp(name,"Rifle 7") && strcmp(name,"Removed magazine") && strcmp(name,"Fresh magazine") &&
                    strncmp(name,"SWAT_ElbowCap_",14)) continue;
        }
        Material material=view->model.materials[view->model.meshMaterial[i]]; material.shader=shader;
        float scale=(!strcmp(name,"Rifle 7") || (strstr(name,"magazine") && !strstr(name,"sleeve"))) ? view->weapon_normal_scale : 1;
        if(lighting) swat_lighting_material_scaled(lighting,material,true,scale);
        if(view->gpu) {
            const SwatArtMesh* source=swat_character_mesh(view->asset,i);
            memcpy(view->palette_pixels[i],source->palette,(size_t)source->palette_count*16*sizeof(float));
            UpdateTexture(view->palette_maps[i],view->palette_pixels[i]);
            int enabled=1,sets=source->influences/4,palette=11,influences=12;
            SetShaderValue(shader,GetShaderLocation(shader,"useSkinning"),&enabled,SHADER_UNIFORM_INT);
            SetShaderValue(shader,GetShaderLocation(shader,"skinSets"),&sets,SHADER_UNIFORM_INT);
            SetShaderValue(shader,GetShaderLocation(shader,"skinPalette"),&palette,SHADER_UNIFORM_INT);
            SetShaderValue(shader,GetShaderLocation(shader,"skinInfluences"),&influences,SHADER_UNIFORM_INT);
            rlActiveTextureSlot(11); rlEnableTexture(view->palette_maps[i].id);
            rlActiveTextureSlot(12); rlEnableTexture(view->influence_maps[i].id); rlActiveTextureSlot(0);
        }
        if(!swat_character_mesh(view->asset,i)->double_sided) rlEnableBackfaceCulling(); else rlDisableBackfaceCulling();
        DrawMesh(mesh,material,root);
    }
    if(view->gpu) { int zero=0; SetShaderValue(shader,GetShaderLocation(shader,"useSkinning"),&zero,SHADER_UNIFORM_INT); rlActiveTextureSlot(11); rlDisableTexture(); rlActiveTextureSlot(12); rlDisableTexture(); rlActiveTextureSlot(0); }
    if(lighting) swat_lighting_material(lighting,(Material){0},false);
    if(lighting) { lighting->surface_normal=~0u; lighting->surface_roughness=~0u; }
    if(rigid_body) {
        int prop=swat_character_find_node(view->asset,"Prop_Rifle");
        const float* f=swat_character_node_matrix(view->asset,prop);
        Matrix gun={f[0],f[4],f[8],f[12],f[1],f[5],f[9],f[13],f[2],f[6],f[10],f[14],f[3],f[7],f[11],f[15]};
        // Column vectors: external root * sampled Prop_Rifle * measured bridge.
        const Model* rifle=view->rigid_rifle;
        Material material=rifle->materials[rifle->meshMaterial[0]];
        material.shader=lighting && lighting->enabled && lighting->prepared ? lighting->mesh.shader :
            (Shader){rlGetShaderIdDefault(),rlGetShaderLocsDefault()};
        rlDrawRenderBatchActive(); rlDisableBackfaceCulling();
        swat_lighting_material_scaled(lighting,material,true,view->weapon_normal_scale);
        DrawMesh(rifle->meshes[0],material,MatrixMultiply(MatrixMultiply(view->rifle_bridge,gun),root));
        swat_lighting_material(lighting,(Material){0},false);
    }
    rlEnableBackfaceCulling();
}
void swat_character_view_draw(const SwatCharacterView* view,SwatLighting* lighting,Matrix root) { draw(view,lighting,root,false); }
void swat_character_view_draw_first_person(const SwatCharacterView* view,SwatLighting* lighting,Matrix root) { draw(view,lighting,root,true); }
