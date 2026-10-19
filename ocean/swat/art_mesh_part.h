#ifndef SWAT_ART_MESH_PART_H
#define SWAT_ART_MESH_PART_H
#include "raylib.h"
#include <stdbool.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
// A static source-triangle partition owns its compact geometry only. Materials
// and textures remain borrowed: close every partition before its source model.
// This follows the existing door/wall mesh cache ownership convention.
typedef struct SwatArtMeshPart {
    Model model;
    const Model* source;
    int source_mesh;
    bool uploaded;
} SwatArtMeshPart;
static void swat_art_mesh_part_close(SwatArtMeshPart* part) {
    if(!part)return;
    for(int i=0;i<part->model.meshCount;i++) {
        Mesh* m=&part->model.meshes[i];
        if(part->uploaded)UnloadMesh(*m);
        else {
            MemFree(m->vertices);MemFree(m->normals);MemFree(m->texcoords);
            MemFree(m->texcoords2);MemFree(m->tangents);MemFree(m->colors);
        }
    }
    MemFree(part->model.meshes);MemFree(part->model.meshMaterial);
    // Never release borrowed materials, their maps, or the source VAO/VBOs.
    memset(part,0,sizeof(*part));
}
static int swat_art_part_vertex(const Mesh* source,const int* triangles,int at) {
    int index=triangles[at/3]*3+at%3;
    return source->indices?source->indices[index]:index;
}
static void* swat_art_part_attribute(const void* source,unsigned int stride,
        const Mesh* mesh,const int* triangles,int vertices) {
    if(!source)return NULL;
    unsigned char* out=MemAlloc((unsigned int)vertices*stride);if(!out)return NULL;
    for(int i=0;i<vertices;i++)memcpy(out+(size_t)i*stride,
        (const unsigned char*)source+(size_t)swat_art_part_vertex(mesh,triangles,i)*stride,stride);
    return out;
}
static bool swat_art_mesh_part_build(SwatArtMeshPart* out,const Model* source,
        int mesh_id,const int* triangles,int count,bool upload) {
    if(!out || out->model.meshes || out->model.materials || out->source || !source ||
       !source->meshes || !source->materials || !source->meshMaterial || source->boneCount ||
       mesh_id<0 || mesh_id>=source->meshCount || !triangles || count<=0 ||
       count>INT_MAX/3 || (unsigned int)count>UINT_MAX/(3u*4u*sizeof(float)) ||
       (upload && !IsWindowReady()))return false;
    const Mesh* from=&source->meshes[mesh_id];int material=source->meshMaterial[mesh_id];
    if(!from->vertices || from->vertexCount<=0 || from->triangleCount<count || from->triangleCount>INT_MAX/3 ||
       material<0 || material>=source->materialCount || from->boneCount || from->boneIds ||
       from->boneWeights ||
       // Raylib allocates scratch animation arrays even for static glTF meshes.
       // They are inert only when they exactly retain the original attributes.
       (from->animVertices && memcmp(from->animVertices,from->vertices,(size_t)from->vertexCount*3*sizeof(float))) ||
       (from->animNormals && (!from->normals || memcmp(from->animNormals,from->normals,(size_t)from->vertexCount*3*sizeof(float)))) ||
       (!from->indices && from->triangleCount>from->vertexCount/3))return false;
    bool* selected=calloc((size_t)from->triangleCount,sizeof(*selected));if(!selected)return false;
    bool valid=true;
    for(int i=0;i<count && valid;i++) {
        int t=triangles[i];
        if(t<0 || t>=from->triangleCount || selected[t]){valid=false;break;}
        selected[t]=true;
        for(int j=0;j<3;j++) {
            int at=from->indices?from->indices[t*3+j]:t*3+j;
            if(at<0 || at>=from->vertexCount){valid=false;break;}
        }
    }
    free(selected);if(!valid)return false;
    SwatArtMeshPart part={0};part.source=source;part.source_mesh=mesh_id;
    part.model.transform=source->transform;part.model.materials=source->materials;
    part.model.materialCount=source->materialCount;
    part.model.meshes=MemAlloc(sizeof(Mesh));
    part.model.meshMaterial=MemAlloc(sizeof(int));
    if(!part.model.meshes || !part.model.meshMaterial) {
        MemFree(part.model.meshes);MemFree(part.model.meshMaterial);return false;
    }
    part.model.meshCount=1;memset(part.model.meshes,0,sizeof(Mesh));
    part.model.meshMaterial[0]=material;
    Mesh* mesh=part.model.meshes;mesh->vertexCount=count*3;mesh->triangleCount=count;
    mesh->vertices=swat_art_part_attribute(from->vertices,3*sizeof(float),from,triangles,mesh->vertexCount);
    mesh->normals=swat_art_part_attribute(from->normals,3*sizeof(float),from,triangles,mesh->vertexCount);
    mesh->texcoords=swat_art_part_attribute(from->texcoords,2*sizeof(float),from,triangles,mesh->vertexCount);
    mesh->texcoords2=swat_art_part_attribute(from->texcoords2,2*sizeof(float),from,triangles,mesh->vertexCount);
    mesh->tangents=swat_art_part_attribute(from->tangents,4*sizeof(float),from,triangles,mesh->vertexCount);
    mesh->colors=swat_art_part_attribute(from->colors,4*sizeof(unsigned char),from,triangles,mesh->vertexCount);
    if(!mesh->vertices || (from->normals&&!mesh->normals) || (from->texcoords&&!mesh->texcoords) ||
       (from->texcoords2&&!mesh->texcoords2) || (from->tangents&&!mesh->tangents) || (from->colors&&!mesh->colors)) {
        swat_art_mesh_part_close(&part);return false;
    }
    if(upload) {
        part.uploaded=true;UploadMesh(mesh,false);
        if(!mesh->vaoId){swat_art_mesh_part_close(&part);return false;}
    }
    *out=part;return true;
}
#endif
