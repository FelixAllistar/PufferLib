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
static int door_polygon(SwatWallVertex v[64],const Mesh* src,int triangle,const SwatObject* o,
                        const SwatFracturePolygon* polygon,int strip,int axis,float sy,float sz,float oy,float oz) {
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
    // Small extra subdivisions localize the soot gradient without changing
    // either the original surface or the collision silhouette.
    if(!o->latch_broken)return n;
    float cuts[]={-o->half.z,o->half.z-.45f,o->half.z-.32f,o->half.z-.20f,o->half.z-.08f,o->half.z};
    float lo=fmaxf(cuts[strip],-o->half.z),hi=fmaxf(cuts[strip+1],-o->half.z);
    if(hi<=lo)return 0;
    if(n)n=door_clip(v,n,-o->half.y,lo,o->half.y,lo,axis,sy,sz,oy,oz);
    if(n)n=door_clip(v,n,o->half.y,hi,-o->half.y,hi,axis,sy,sz,oy,oz);
    return n;
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
    wall_mesh_close(cache);art->owners[slot]=o->tag.index;cache->source=source->meshes;cache->half=o->half;cache->mask=signature;
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
    int strips=o->latch_broken?5:1;
    Model* model=&cache->model;model->transform=source->transform;model->materials=source->materials;model->materialCount=source->materialCount;
    model->meshes=MemAlloc(source->meshCount*sizeof(Mesh));memset(model->meshes,0,source->meshCount*sizeof(Mesh));
    model->meshMaterial=MemAlloc(source->meshCount*sizeof(int));
    for(int m=0;m<source->meshCount;m++) {
        const Mesh* src=&source->meshes[m];int count=0;
        for(int t=0;t<src->triangleCount;t++) {
          SwatFaceBounds triangle=face_triangle_bounds(src,t,axis,sy,sz,oy,oz);
          for(int band=0;band<sections;band++) {
            if(!face_bounds_overlap(triangle,bounds[band]))continue;
            for(int strip=0;strip<strips;strip++) {
            cache->polygon_tests++;
            SwatWallVertex v[64];int n=door_polygon(v,src,t,o,&polygons[band],strip,axis,sy,sz,oy,oz);if(n>=3)count+=3*(n-2);
            }
          }
        }
        if(!count)continue;
        Mesh mesh={0};mesh.vertices=MemAlloc(count*3*sizeof(float));mesh.normals=MemAlloc(count*3*sizeof(float));
        mesh.texcoords=MemAlloc(count*2*sizeof(float));mesh.colors=MemAlloc(count*4);
        if(src->texcoords2)mesh.texcoords2=MemAlloc(count*2*sizeof(float));
        for(int t=0;t<src->triangleCount;t++) {
          SwatFaceBounds triangle=face_triangle_bounds(src,t,axis,sy,sz,oy,oz);
          for(int band=0;band<sections;band++) {
            if(!face_bounds_overlap(triangle,bounds[band]))continue;
            for(int strip=0;strip<strips;strip++) {
            cache->polygon_tests++;
            SwatWallVertex v[64];int n=door_polygon(v,src,t,o,&polygons[band],strip,axis,sy,sz,oy,oz);
            for(int k=1;k<n-1;k++) {const int indices[]={0,k,k+1};
                for(int j=0;j<3;j++) {
                    SwatWallVertex p=v[indices[j]];int at=mesh.vertexCount++;
                    memcpy(mesh.vertices+3*at,&p.p,3*sizeof(float));memcpy(mesh.normals+3*at,&p.n,3*sizeof(float));
                    memcpy(mesh.texcoords+2*at,&p.uv,2*sizeof(float));if(mesh.texcoords2)memcpy(mesh.texcoords2+2*at,&p.uv2,2*sizeof(float));
                    float y=p.p.y*sy+oy,z=door_vertex_z(p,axis,sz,oz),soot=0;
                    if(o->latch_broken) {
                        float r=hypotf((y-.06f)/.43f,(o->half.z-z)/.37f);
                        soot=swat_clamp((1.07f+.04f*sinf(31*y+17*z)-r)*1.2f,0,.91f);
                    }
                    if(o->cuts)for(int c=0;c<o->cuts->count;c++) {
                        const SwatCut* cut=&o->cuts->cuts[c];if(cut->kind!=SWAT_CUT_BREACH)continue;
                        float r=hypotf((y-cut->y)/(cut->height*2.2f+.03f),(z-cut->z)/(cut->width*2.2f+.03f));
                        soot=fmaxf(soot,swat_clamp((1.1f-r)*1.3f,0,.94f));
                    }
                    mesh.colors[4*at]=(unsigned char)(255*(1-soot));mesh.colors[4*at+1]=(unsigned char)(255*(1-.99f*soot));
                    mesh.colors[4*at+2]=(unsigned char)(255*(1-.96f*soot));mesh.colors[4*at+3]=255;
                }
            }
            }
          }
        }
        assert(mesh.vertexCount==count);mesh.triangleCount=count/3;UploadMesh(&mesh,false);
        model->meshMaterial[model->meshCount]=source->meshMaterial[m];model->meshes[model->meshCount++]=mesh;
    }
    return model;
}
#endif
