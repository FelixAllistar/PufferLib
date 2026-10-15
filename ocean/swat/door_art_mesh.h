// Clips the original door art to the same retained bands as its colliders.
// Coordinates, materials, normals and UVs remain in the source model frame.
#ifndef SWAT_DOOR_ART_MESH_H
#define SWAT_DOOR_ART_MESH_H
static float door_vertex_z(SwatWallVertex v,int axis,float scale,float offset) {
    return (axis==0?v.p.x:v.p.z)*scale+offset;
}
static int door_clip(SwatWallVertex v[64],int n,float ay,float az,float by,float bz,
                     int axis,float sy,float sz,float oy,float oz) {
    SwatWallVertex out[64];int count=0;
    for(int i=0;i<n;i++) {
        SwatWallVertex p=v[i],q=v[(i+1)%n];
        float pv=(by-ay)*(door_vertex_z(p,axis,sz,oz)-az)-(bz-az)*(p.p.y*sy+oy-ay);
        float qv=(by-ay)*(door_vertex_z(q,axis,sz,oz)-az)-(bz-az)*(q.p.y*sy+oy-ay);
        if(pv>=0)out[count++]=p;
        if((pv>=0)!=(qv>=0))out[count++]=wall_lerp(p,q,pv/(pv-qv));
    }
    assert(count<=64);memcpy(v,out,(size_t)count*sizeof(*out));return count;
}
static int door_polygon(SwatWallVertex v[64],const Mesh* src,int triangle,
                        const SwatFracturePolygon* polygon,int axis,float sy,float sz,float oy,float oz) {
    for(int k=0;k<3;k++) {
        int at=src->indices?src->indices[3*triangle+k]:3*triangle+k;v[k]=(SwatWallVertex){0};
        memcpy(&v[k].p,src->vertices+3*at,3*sizeof(float));
        if(src->normals)memcpy(&v[k].n,src->normals+3*at,3*sizeof(float));
        if(src->texcoords)memcpy(&v[k].uv,src->texcoords+2*at,2*sizeof(float));
        if(src->texcoords2)memcpy(&v[k].uv2,src->texcoords2+2*at,2*sizeof(float));
    }
    int n=3;
    for(int e=0;e<polygon->count && n;e++) {int j=(e+1)%polygon->count;
        n=door_clip(v,n,polygon->vertices[e][0],polygon->vertices[e][1],polygon->vertices[j][0],polygon->vertices[j][1],axis,sy,sz,oy,oz);
    }
    return n;
}
static float door_core_half(const SwatDoorArt* art,const SwatObject* o,const Model* source) {
    for(int i=0;i<SWAT_DOOR_CACHE;i++)if(art->owners[i]==o->tag.index && art->meshes[i].source==source->meshes)return art->core_half[i];
    return o->half.x*.1f;
}
static const Model* door_mesh(SwatDoorArt* art,const Model* source,const SwatObject* o,const SwatMotelInstance* p) {
    uint64_t signature=((uint64_t)swat_fracture_hash(o)<<2)|(p?1:0)|(o->latch_broken?2:0);
    int slot=-1;
    for(int i=0;i<SWAT_DOOR_CACHE;i++)if(art->owners[i]==o->tag.index && art->meshes[i].source==source->meshes){slot=i;break;}
    if(slot<0)for(int i=0;i<SWAT_DOOR_CACHE;i++)if(art->owners[i]<0){slot=i;break;}
    if(slot<0)slot=o->tag.index%SWAT_DOOR_CACHE;
    SwatWallMeshCache* cache=&art->meshes[slot];
    if(art->owners[slot]==o->tag.index && cache->source==source->meshes &&
       !memcmp(&cache->half,&o->half,sizeof(o->half)) && cache->mask==signature)return &cache->model;
    uint64_t started=swat_profile_begin();SWAT_PROFILE_ADD(door_builds,1);
    wall_mesh_close(cache);art->owners[slot]=o->tag.index;cache->source=source->meshes;cache->half=o->half;cache->mask=signature;
    // Place backing below the shallowest source skin, including recessed panels.
    // Calculate once per rebuild, never while the unchanged leaf moves.
    float core=o->half.x;bool skin=false;int depth_axis=p?2:0;float depth_scale=p?p->scale.z:2*o->half.x;
    for(int m=0;m<source->meshCount;m++) {
        const Mesh* mesh=&source->meshes[m];if(!mesh->normals)continue;
        BoundingBox box=GetMeshBoundingBox(*mesh);
        float height=(box.max.y-box.min.y)*(p?p->scale.y:2*o->half.y);
        float width=(p?box.max.x-box.min.x:box.max.z-box.min.z)*(p?p->scale.x:2*o->half.z);
        if(height<o->half.y || width<o->half.z)continue; // Hardware does not define the leaf skins.
        for(int t=0;t<mesh->triangleCount;t++) {
            int v[3];for(int k=0;k<3;k++)v[k]=mesh->indices?mesh->indices[3*t+k]:3*t+k;
            int width_axis=p?0:2;const float* a=mesh->vertices+3*v[0],*b=mesh->vertices+3*v[1],*c=mesh->vertices+3*v[2];
            float area=.5f*fabsf((b[1]-a[1])*(c[width_axis]-a[width_axis])-(c[1]-a[1])*(b[width_axis]-a[width_axis]));
            area*=(p?p->scale.y:2*o->half.y)*(p?p->scale.x:2*o->half.z);
            if(area<.02f*o->half.y*o->half.z)continue; // Ignore small hardware faces sharing a material/mesh.
            for(int k=0;k<3;k++)if(fabsf(mesh->normals[3*v[k]+depth_axis])>.9f &&
                mesh->normals[3*v[k]+depth_axis]*mesh->vertices[3*v[k]+depth_axis]>0) {
                float depth=fabsf(mesh->vertices[3*v[k]+depth_axis]*depth_scale);
                if(depth>1e-5f){core=fminf(core,depth*.98f);skin=true;}
            }
        }
    }
    art->core_half[slot]=skin?core:o->half.x*.1f;
    art->builds++;
    int axis=p?0:2;float sy=p?p->scale.y:2*o->half.y,sz=p?p->scale.x:2*o->half.z;
    float oy=p?-o->half.y:0,oz=p?-o->half.z:0;
    SwatFracturePolygon original[SWAT_DOOR_SECTIONS]={0};int sections=1;
    if(o->latch_broken){sections=SWAT_DOOR_SECTIONS;for(int i=0;i<sections;i++){original[i].count=4;swat_door_section(o,i,original[i].vertices);}}
    else swat_source_polygon(o,&original[0]);
    const SwatFracturePolygon* polygons=o->cuts?o->cuts->sections:original;
    if(o->cuts)sections=o->cuts->section_count;
    SwatFaceBounds bounds[SWAT_CUT_SECTIONS];
    for(int s=0;s<sections;s++)bounds[s]=face_bounds(&polygons[s]);
    Model* model=&cache->model;model->transform=source->transform;model->materials=source->materials;model->materialCount=source->materialCount;
    model->meshes=MemAlloc(source->meshCount*sizeof(Mesh));memset(model->meshes,0,source->meshCount*sizeof(Mesh));
    model->meshMaterial=MemAlloc(source->meshCount*sizeof(int));
    for(int m=0;m<source->meshCount;m++) {
        const Mesh* src=&source->meshes[m];int capacity=0;Mesh mesh={0};
        for(int t=0;t<src->triangleCount;t++) {
          SwatFaceBounds triangle=face_triangle_bounds(src,t,axis,sy,sz,oy,oz);
          for(int band=0;band<sections;band++) {
            if(!face_bounds_overlap(triangle,bounds[band]))continue;
            cache->polygon_tests++;
            SwatWallVertex v[64];int n=door_polygon(v,src,t,&polygons[band],axis,sy,sz,oy,oz);
            for(int k=1;k<n-1;k++) {const int indices[]={0,k,k+1};
                for(int j=0;j<3;j++) {
                    if(mesh.vertexCount==capacity) {
                        capacity=capacity?capacity*2:256;
                        mesh.vertices=MemRealloc(mesh.vertices,capacity*3*sizeof(float));
                        mesh.normals=MemRealloc(mesh.normals,capacity*3*sizeof(float));
                        mesh.texcoords=MemRealloc(mesh.texcoords,capacity*2*sizeof(float));
                        mesh.colors=MemRealloc(mesh.colors,capacity*4);
                        if(src->texcoords2)mesh.texcoords2=MemRealloc(mesh.texcoords2,capacity*2*sizeof(float));
                    }
                    SwatWallVertex p=v[indices[j]];int at=mesh.vertexCount++;
                    memcpy(mesh.vertices+3*at,&p.p,3*sizeof(float));memcpy(mesh.normals+3*at,&p.n,3*sizeof(float));
                    memcpy(mesh.texcoords+2*at,&p.uv,2*sizeof(float));if(mesh.texcoords2)memcpy(mesh.texcoords2+2*at,&p.uv2,2*sizeof(float));
                    memset(mesh.colors+4*at,255,4); // Soot is evaluated per pixel.
                }
            }
          }
        }
        if(!mesh.vertexCount)continue;
        int count=mesh.vertexCount;mesh.triangleCount=count/3;
        uint64_t upload=swat_profile_begin();UploadMesh(&mesh,false);swat_profile_end(&swat_damage_profile.upload_ms,upload);
        SWAT_PROFILE_ADD(door_vertices,count);
        model->meshMaterial[model->meshCount]=source->meshMaterial[m];model->meshes[model->meshCount++]=mesh;
    }
    swat_profile_end(&swat_damage_profile.door_ms,started);return model;
}
#endif
