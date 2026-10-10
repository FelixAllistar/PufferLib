// Internal render cache: clip authored wall triangles to physical sections.
// Materials/textures are borrowed from the original bank, never duplicated.
#ifndef SWAT_MOTEL_WALL_ART_H
#define SWAT_MOTEL_WALL_ART_H
typedef struct SwatWallVertex {Vector3 p,n;Vector2 uv,uv2;} SwatWallVertex;
typedef struct SwatWallMeshCache {
    Model model;const Mesh* source;b3Pos center;b3Vec3 half;bool fractured;float corners[4][2];uint64_t mask;
} SwatWallMeshCache;
typedef struct SwatMotelWallArt {
    SwatWallMeshCache pieces[SWAT_MAX_OBJECTS][2],fences[3],banks[5],windows[5];
    bool core_batch_disabled,core_batch_requested,cores_drawn;
} SwatMotelWallArt;
static void wall_mesh_close(SwatWallMeshCache* cache) {
    for(int i=0;i<cache->model.meshCount;i++)UnloadMesh(cache->model.meshes[i]);
    MemFree(cache->model.meshes);MemFree(cache->model.meshMaterial);memset(cache,0,sizeof(*cache));
}
static void wall_art_close(SwatEnvironmentArt* art) {
    if(!art->motel_wall_art)return;
    for(int i=0;i<SWAT_MAX_OBJECTS;i++)for(int j=0;j<2;j++)wall_mesh_close(&art->motel_wall_art->pieces[i][j]);
    for(int i=0;i<3;i++)wall_mesh_close(&art->motel_wall_art->fences[i]);
    for(int i=0;i<5;i++)wall_mesh_close(&art->motel_wall_art->banks[i]);
    for(int i=0;i<5;i++)wall_mesh_close(&art->motel_wall_art->windows[i]);
    free(art->motel_wall_art);art->motel_wall_art=NULL;
}
static const Model* filtered_mesh(SwatWallMeshCache* cache,const Model* source,uint64_t mask,int (*triangle_part)(int),int asset) {
    if(cache->source==source->meshes && cache->mask==mask)return &cache->model;
    wall_mesh_close(cache);cache->source=source->meshes;cache->mask=mask;
    Model* model=&cache->model;model->transform=source->transform;model->materials=source->materials;model->materialCount=source->materialCount;
    model->meshes=MemAlloc(source->meshCount*sizeof(Mesh));memset(model->meshes,0,source->meshCount*sizeof(Mesh));
    model->meshMaterial=MemAlloc(source->meshCount*sizeof(int));int triangle=0;
    for(int m=0;m<source->meshCount;m++) {
        const Mesh* src=&source->meshes[m];Mesh mesh={0};int capacity=src->triangleCount*3;
        mesh.vertices=MemAlloc(capacity*3*sizeof(float));mesh.normals=MemAlloc(capacity*3*sizeof(float));mesh.texcoords=MemAlloc(capacity*2*sizeof(float));
        if(src->texcoords2)mesh.texcoords2=MemAlloc(capacity*2*sizeof(float));
        for(int t=0;t<src->triangleCount;t++,triangle++) {
            int part;
            if(triangle_part)part=triangle_part(triangle);
            else {
                b3Vec3 v[3];
                for(int k=0;k<3;k++){int at=src->indices?src->indices[t*3+k]:t*3+k;v[k]=swat_v(src->vertices[at*3],src->vertices[at*3+1],src->vertices[at*3+2]);}
                part=1+swat_motel_pane_triangle(asset,v[0],v[1],v[2]);
            }
            if(part<0 || !(mask&(UINT64_C(1)<<part)))continue;
            for(int j=0;j<3;j++) {
                int at=src->indices?src->indices[t*3+j]:t*3+j,out=mesh.vertexCount++;
                memcpy(mesh.vertices+out*3,src->vertices+at*3,3*sizeof(float));
                if(src->normals)memcpy(mesh.normals+out*3,src->normals+at*3,3*sizeof(float));else memset(mesh.normals+out*3,0,3*sizeof(float));
                if(src->texcoords)memcpy(mesh.texcoords+out*2,src->texcoords+at*2,2*sizeof(float));else memset(mesh.texcoords+out*2,0,2*sizeof(float));
                if(src->texcoords2)memcpy(mesh.texcoords2+out*2,src->texcoords2+at*2,2*sizeof(float));
            }
        }
        mesh.triangleCount=mesh.vertexCount/3;
        if(!mesh.triangleCount){MemFree(mesh.vertices);MemFree(mesh.normals);MemFree(mesh.texcoords);MemFree(mesh.texcoords2);continue;}
        UploadMesh(&mesh,false);model->meshMaterial[model->meshCount]=source->meshMaterial[m];model->meshes[model->meshCount++]=mesh;
    }
    return model;
}
static const Model* fence_mesh(SwatMotelWallArt* art,const Model* source,const SwatWorld* world,int owner) {
    int count=swat_motel_fence_part_count(),first=world->objects[owner].wall_group-1;uint64_t mask=0;
    for(int i=0;i<count;i++)if(world->objects[first+i].active)mask|=UINT64_C(1)<<i;
    if(mask==(count==64?UINT64_MAX:(UINT64_C(1)<<count)-1))return source;
    return filtered_mesh(&art->fences[owner-155],source,mask,swat_motel_fence_triangle_part,0);
}
static const Model* bank_mesh(SwatMotelWallArt* art,const Model* source,const SwatWorld* world,int owner) {
    uint64_t mask=0;for(int part=0;part<14;part++)if(world->objects[owner+part].active)mask|=UINT64_C(1)<<part;
    if(mask==16383)return source;
    return filtered_mesh(&art->banks[(owner-SWAT_MOTEL_SURROUNDINGS_FIRST)/SWAT_SURROUNDINGS_PARTS],source,mask,swat_motel_bank_triangle_part,0);
}
static const Model* window_mesh_art(SwatMotelWallArt* art,const Model* source,const SwatWorld* world,int owner) {
    int mask=swat_motel_pane_mask(world,owner);if(mask==3)return source;
    return filtered_mesh(&art->windows[swat_motel_window_index(owner)],source,1u|((unsigned)mask<<1),NULL,swat_motel_instance(owner-1)->asset);
}
static int window_room_triangle(int triangle){return swat_motel_window_model(0)->triangle_parts[triangle];}
static int window_lobby_triangle(int triangle){return swat_motel_window_model(1)->triangle_parts[triangle];}
static const Model* window_r1_mesh_art(SwatMotelWallArt* art,const Model* source,const SwatWorld* world,int owner){
    int bay=swat_motel_window_index(owner),kind=bay?0:1;
    const SwatWindowModel* m=swat_motel_window_model(kind);uint64_t mask=swat_motel_window_mask(world,owner);
    if(mask==((UINT64_C(1)<<m->part_count)-1))return source;
    return filtered_mesh(&art->windows[bay],source,mask,kind?window_lobby_triangle:window_room_triangle,0);
}
static SwatWallVertex wall_lerp(SwatWallVertex a,SwatWallVertex b,float t) {
    return (SwatWallVertex){Vector3Lerp(a.p,b.p,t),Vector3Lerp(a.n,b.n,t),Vector2Lerp(a.uv,b.uv,t),Vector2Lerp(a.uv2,b.uv2,t)};
}
static int wall_clip(SwatWallVertex* vertices,int count,int axis,float edge,float sign) {
    SwatWallVertex out[12];int n=0;
    for(int i=0;i<count;i++) {
        SwatWallVertex a=vertices[i],b=vertices[(i+1)%count];
        float av=axis?a.p.y:a.p.x,bv=axis?b.p.y:b.p.x;
        bool ain=sign*(av-edge)>=0,bin=sign*(bv-edge)>=0;
        if(ain)out[n++]=a;
        if(ain!=bin)out[n++]=wall_lerp(a,b,(edge-av)/(bv-av));
    }
    memcpy(vertices,out,n*sizeof(*out));return n;
}
static int wall_clip_edge(SwatWallVertex* vertices,int count,Vector2 a,Vector2 b) {
    SwatWallVertex out[12];int n=0;
    for(int i=0;i<count;i++) {
        SwatWallVertex p=vertices[i],q=vertices[(i+1)%count];
        float pv=(b.y-a.y)*(p.p.x-a.x)-(b.x-a.x)*(p.p.y-a.y);
        float qv=(b.y-a.y)*(q.p.x-a.x)-(b.x-a.x)*(q.p.y-a.y);
        if(pv>=0)out[n++]=p;
        if((pv>=0)!=(qv>=0))out[n++]=wall_lerp(p,q,pv/(pv-qv));
    }
    memcpy(vertices,out,n*sizeof(*out));return n;
}
static const Model* wall_mesh(SwatWallMeshCache* cache,const Model* source,const SwatObject* o,const SwatMotelInstance* p) {
    if(cache->source==source->meshes && b3Distance(cache->center,o->center)<1e-6f && b3Length(b3Sub(cache->half,o->half))<1e-6f &&
       cache->fractured==o->fractured && !memcmp(cache->corners,o->corners,sizeof(o->corners)))return &cache->model;
    wall_mesh_close(cache);cache->source=source->meshes;cache->center=o->center;cache->half=o->half;
    cache->fractured=o->fractured;memcpy(cache->corners,o->corners,sizeof(o->corners));
    b3Vec3 d=b3SubPos(o->center,p->origin);float cx=(cosf(p->yaw)*d.x-sinf(p->yaw)*d.z)/p->scale.x;
    float cy=d.y/p->scale.y,hx=o->half.z/p->scale.x,hy=o->half.y/p->scale.y;
    Model* model=&cache->model;model->transform=MatrixIdentity();
    model->materials=source->materials;model->materialCount=source->materialCount;
    model->meshes=MemAlloc(source->meshCount*sizeof(Mesh));memset(model->meshes,0,source->meshCount*sizeof(Mesh));
    model->meshMaterial=MemAlloc(source->meshCount*sizeof(int));
    for(int m=0;m<source->meshCount;m++) {
        const Mesh* src=&source->meshes[m];Mesh mesh={0};int capacity=src->triangleCount*21;
        mesh.vertices=MemAlloc(capacity*3*sizeof(float));mesh.normals=MemAlloc(capacity*3*sizeof(float));
        mesh.texcoords=MemAlloc(capacity*2*sizeof(float));if(src->texcoords2)mesh.texcoords2=MemAlloc(capacity*2*sizeof(float));
        for(int t=0;t<src->triangleCount;t++) {
            SwatWallVertex v[12];int n=3;
            for(int k=0;k<3;k++) {
                int at=src->indices?src->indices[3*t+k]:3*t+k;
                v[k]=(SwatWallVertex){0};memcpy(&v[k].p,src->vertices+3*at,3*sizeof(float));
                if(src->normals)memcpy(&v[k].n,src->normals+3*at,3*sizeof(float));
                if(src->texcoords)memcpy(&v[k].uv,src->texcoords+2*at,2*sizeof(float));
                if(src->texcoords2)memcpy(&v[k].uv2,src->texcoords2+2*at,2*sizeof(float));
            }
            if(o->fractured)for(int e=0;e<4;e++) {
                int next=(e+1)%4;
                Vector2 a={cx+o->corners[e][1]/p->scale.x,cy+o->corners[e][0]/p->scale.y};
                Vector2 b={cx+o->corners[next][1]/p->scale.x,cy+o->corners[next][0]/p->scale.y};
                n=wall_clip_edge(v,n,a,b);
            } else {
                n=wall_clip(v,n,0,cx-hx,1);n=wall_clip(v,n,0,cx+hx,-1);
                n=wall_clip(v,n,1,cy-hy,1);n=wall_clip(v,n,1,cy+hy,-1);
            }
            for(int k=1;k<n-1;k++) {
                int corner[]={0,k,k+1};
                if(Vector3LengthSqr(Vector3CrossProduct(Vector3Subtract(v[k].p,v[0].p),Vector3Subtract(v[k+1].p,v[0].p)))<1e-14f)continue;
                for(int j=0;j<3;j++) {SwatWallVertex a=v[corner[j]];int at=mesh.vertexCount++;
                    memcpy(mesh.vertices+3*at,&a.p,3*sizeof(float));memcpy(mesh.normals+3*at,&a.n,3*sizeof(float));
                    memcpy(mesh.texcoords+2*at,&a.uv,2*sizeof(float));if(mesh.texcoords2)memcpy(mesh.texcoords2+2*at,&a.uv2,2*sizeof(float));}
            }
        }
        mesh.triangleCount=mesh.vertexCount/3;
        if(!mesh.triangleCount) {MemFree(mesh.vertices);MemFree(mesh.normals);MemFree(mesh.texcoords);MemFree(mesh.texcoords2);continue;}
        UploadMesh(&mesh,false);model->meshMaterial[model->meshCount]=source->meshMaterial[m];model->meshes[model->meshCount++]=mesh;
    }
    return model;
}
#endif
