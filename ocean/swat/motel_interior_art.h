// Internal render-only Briar Court interior pass (recipe v1), compiled only
// in environment_art.c. Assets come from tools/blender/motel_interior_v1.py.
//
// Ownership rules (see ENVIRONMENT_ART.md, "Briar Court interior pass"):
// - Bed frame, mattress and folded-linen owners keep their colliders, damage
//   and support; only their look is replaced, inside the same bounds.
// - Every other piece is presentation-only clutter/fixture art inheriting an
//   existing support (floor, furniture, wall section). No colliders, no nav,
//   no AI occlusion, no wire state. It disappears with its support.
// - Wallpaper, carpet and tile are texture/UV swaps on derived copies of the
//   existing render meshes. Wall copies feed the unchanged breach clipper, so
//   holes, cores and sections render exactly as before.
#ifndef SWAT_MOTEL_INTERIOR_ART_H
#define SWAT_MOTEL_INTERIOR_ART_H

enum {
    INT_BED_FRAME,INT_BEDDING_ROSE,INT_BEDDING_TEAL,INT_TOWELS,INT_BOTTLES,INT_BIN,
    INT_CLOTHES,INT_TAKEOUT,INT_PIZZA,INT_PRINT,INT_MIRROR,INT_CURTAIN,INT_RAIL,
    INT_MAT,INT_COUNTER,INT_CLOCK,INT_BASKET,INT_DETERGENT,SWAT_INTERIOR_ASSETS
};
static const char* const interior_files[SWAT_INTERIOR_ASSETS]={
    "bed_frame.glb","bed_bedding_rose.glb","bed_bedding_teal.glb","towel_pair.glb",
    "desk_bottles.glb","wastebasket.glb","floor_clothes.glb","takeout.glb","pizza_box.glb",
    "wall_print_desert.glb","bath_mirror.glb","shower_curtain.glb","towel_rail.glb",
    "bath_mat.glb","counter_kit.glb","wall_clock.glb","laundry_basket.glb","detergent.glb"};
#define SWAT_INTERIOR_PIECES 64
// World-space mount (metres, engine yaw). `owner` is the motel object/tag that
// supports it; `wall` mounts follow the surviving section of that wall group.
typedef struct SwatInteriorPiece {int asset,owner;float x,y,z,yaw;bool wall;} SwatInteriorPiece;
typedef struct SwatMotelInteriorArt {
    Model models[SWAT_INTERIOR_ASSETS];
    float normal_scale[SWAT_INTERIOR_ASSETS][SWAT_ROOM101_MATERIALS];
    SwatInteriorPiece pieces[SWAT_INTERIOR_PIECES];int piece_count;
    Texture2D wallpaper,carpet,tile;
    Model floor;                       // Shallow copy of motel[4]: own materials, borrowed meshes.
    MaterialMap* floor_maps[2];        // Carpet/tile maps owned here.
    Model papered[SWAT_MOTEL_INSTANCES]; // Derived wall sources (own meshes), or empty.
    MaterialMap* papered_maps[SWAT_MOTEL_INSTANCES];
} SwatMotelInteriorArt;

// Guest rooms 102-104 and reception receive the new finishes. Room 101 keeps
// its accepted realism set; the laundry keeps its tile room.
static bool interior_papered_point(float x,float y,float z) {
    if(y<0 || y>2.8f || z<-5.88f || z>-.12f)return false;
    if(x>-11.88f && x<-8.12f)return true;   // Reception.
    return x>-3.88f && x<7.88f && fabsf(fmodf(x+8,4)-2)<1.88f; // 102..104.
}

static void interior_pieces_build(SwatMotelInteriorArt* a) {
    int n=0;
    #define PIECE(k,o,px,py,pz,w,wall_) do{ if(n<SWAT_INTERIOR_PIECES) a->pieces[n++]=(SwatInteriorPiece){k,o,px,py,pz,w,wall_}; }while(0)
    for(int r=0;r<4;r++) {
        float X=-6+4.f*r;int base=24*r;
        // Bathroom: mirror cabinet over the basin, towel rail, shower rod and
        // bunched curtain, bath mat and a small bin. Room 101 included: its
        // accepted v4 bathroom has no mirror/curtain either.
        PIECE(INT_MIRROR,14+base,X-.95f,1.50f,-5.898f,0,true);
        PIECE(INT_RAIL,20+base,X+.25f,1.25f,-4.085f,SWAT_PI,true);
        PIECE(INT_CURTAIN,31+base,X+.80f,1.95f,-4.9915f,0,false);
        PIECE(INT_MAT,9+base,X+.55f,.008f,-5.0f,SWAT_PI*.5f,false);
        PIECE(INT_BIN,9+base,X-.40f,.008f,-5.74f,.4f,false);
        // Bedroom wall art over the free end of the desk.
        PIECE(INT_PRINT,105+r,X-1.885f,1.62f,-1.98f,SWAT_PI*.5f,true);
        // Occupant clutter differs per room; Room 101 (civilian) stays tidy.
        if(r==1 || r==3) PIECE(INT_BOTTLES,24+base,X-1.22f,.7606f,-3.0f,.3f,false);
        if(r>=1) PIECE(INT_CLOTHES,9+base,X+.4f,.008f,-1.3f,1.3f+.6f*r,false);
        if(r==1) PIECE(INT_PIZZA,9+base,X+1.63f,.008f,-2.6f,.2f,false);
        if(r==2) PIECE(INT_TAKEOUT,23+base,X+1.0f,.768f,-2.15f,.4f,false);
        if(r==3) PIECE(INT_PIZZA,23+base,X+.8f,.768f,-2.45f,-.4f,false);
    }
    // Reception: bell, open register and brochure stand on the counter, a
    // wall clock on the east wall and a bin behind the counter.
    PIECE(INT_COUNTER,121,-9.25f,1.093f,-3.62f,0,false);
    PIECE(INT_CLOCK,105,-8.115f,2.05f,-2.0f,-SWAT_PI*.5f,true);
    PIECE(INT_BIN,2,-8.4f,.008f,-4.5f,0,false);
    // Laundry: detergent on the washer, a basket of linen by the desk.
    PIECE(INT_DETERGENT,133,-11.42f,.884f,-9.5f,.3f,false);
    PIECE(INT_BASKET,127,-11.5f,.008f,-8.75f,SWAT_PI*.5f,false);
    #undef PIECE
    a->piece_count=n;
}

static void interior_wall_close(SwatMotelInteriorArt* a,int i) {
    Model* m=&a->papered[i];
    for(int k=0;k<m->meshCount;k++)UnloadMesh(m->meshes[k]);
    MemFree(m->meshes);MemFree(m->meshMaterial);MemFree(m->materials);MemFree(a->papered_maps[i]);
    a->papered_maps[i]=NULL;*m=(Model){0};
}
static void motel_interior_close(SwatEnvironmentArt* art) {
    SwatMotelInteriorArt* a=art->motel_interior;if(!a)return;
    for(int i=0;i<SWAT_INTERIOR_ASSETS;i++)swat_art_model_close(a->models[i]);
    for(int i=0;i<SWAT_MOTEL_INSTANCES;i++)interior_wall_close(a,i);
    MemFree(a->floor.materials);MemFree(a->floor_maps[0]);MemFree(a->floor_maps[1]);
    if(a->wallpaper.id)UnloadTexture(a->wallpaper);
    if(a->carpet.id)UnloadTexture(a->carpet);
    if(a->tile.id)UnloadTexture(a->tile);
    free(a);art->motel_interior=NULL;
}

static MaterialMap* interior_maps(const Material* source,Texture2D albedo,float roughness) {
    // Raylib walks its internal MAX_MATERIAL_MAPS (12) slots; allocate a
    // zeroed superset and copy the public slots up to the BRDF map.
    MaterialMap* maps=MemAlloc(16*sizeof(MaterialMap));memset(maps,0,16*sizeof(MaterialMap));
    memcpy(maps,source->maps,(MATERIAL_MAP_BRDF+1)*sizeof(MaterialMap));
    maps[MATERIAL_MAP_ALBEDO].texture=albedo;maps[MATERIAL_MAP_ALBEDO].color=WHITE;
    // Flat finishes: the stucco/carpet relief maps do not belong to them.
    maps[MATERIAL_MAP_NORMAL].texture=(Texture2D){0};
    maps[MATERIAL_MAP_ROUGHNESS].texture=(Texture2D){0};maps[MATERIAL_MAP_ROUGHNESS].value=roughness;
    maps[MATERIAL_MAP_METALNESS].value=0;
    maps[MATERIAL_MAP_OCCLUSION].texture=(Texture2D){0};
    return maps;
}

// Split the instance's finish-plaster primitive (Raylib material slot 1) into
// exterior/other faces and room-facing faces that get wallpaper with metric
// UVs (0.6 m repeat). Geometry, winding and vertex order are unchanged.
static void interior_wall_build(SwatMotelInteriorArt* a,const Model* source,int owner) {
    const SwatMotelInstance* p=swat_motel_instance(owner-1);
    if(!source->meshCount || !a->wallpaper.id)return;
    float c=cosf(p->yaw),s=sinf(p->yaw);
    Model* out=&a->papered[owner-1];int papered=0;
    out->meshes=MemAlloc((source->meshCount+1)*sizeof(Mesh));memset(out->meshes,0,(source->meshCount+1)*sizeof(Mesh));
    out->meshMaterial=MemAlloc((source->meshCount+1)*sizeof(int));
    for(int m=0;m<source->meshCount;m++) {
        const Mesh* src=&source->meshes[m];
        bool finish=source->meshMaterial[m]==1 && src->vertices;
        Mesh part[2]={0};
        for(int k=0;k<2;k++) {
            int cap=src->triangleCount*3;
            part[k].vertices=MemAlloc(cap*3*sizeof(float));part[k].normals=MemAlloc(cap*3*sizeof(float));
            part[k].texcoords=MemAlloc(cap*2*sizeof(float));
            if(src->texcoords2)part[k].texcoords2=MemAlloc(cap*2*sizeof(float));
        }
        for(int t=0;t<src->triangleCount;t++) {
            Vector3 w[3],l[3];
            for(int k=0;k<3;k++) {
                int at=src->indices?src->indices[3*t+k]:3*t+k;
                l[k]=(Vector3){src->vertices[3*at],src->vertices[3*at+1],src->vertices[3*at+2]};
                float x=l[k].x*p->scale.x,y=l[k].y*p->scale.y,z=l[k].z*p->scale.z;
                w[k]=(Vector3){(float)p->origin.x+c*x+s*z,(float)p->origin.y+y,(float)p->origin.z-s*x+c*z};
            }
            Vector3 n=Vector3Normalize(Vector3CrossProduct(Vector3Subtract(w[1],w[0]),Vector3Subtract(w[2],w[0])));
            Vector3 mid=Vector3Scale(Vector3Add(Vector3Add(w[0],w[1]),w[2]),1/3.f);
            bool paper=finish && fabsf(n.y)<.2f && interior_papered_point(mid.x+.06f*n.x,mid.y,mid.z+.06f*n.z);
            Mesh* dst=&part[paper];
            for(int k=0;k<3;k++) {
                int at=src->indices?src->indices[3*t+k]:3*t+k,o=dst->vertexCount++;
                memcpy(dst->vertices+3*o,src->vertices+3*at,3*sizeof(float));
                if(src->normals)memcpy(dst->normals+3*o,src->normals+3*at,3*sizeof(float));
                else memset(dst->normals+3*o,0,3*sizeof(float));
                if(src->texcoords2)memcpy(dst->texcoords2+2*o,src->texcoords2+2*at,2*sizeof(float));
                if(paper) {
                    // Horizontal U along the face, V up; metric texture rows
                    // are flipped on upload so +V reads upward.
                    float u=fabsf(n.x)>fabsf(n.z)?(n.x>0?-w[k].z:w[k].z):(n.z>0?w[k].x:-w[k].x);
                    dst->texcoords[2*o]=u/.6f;dst->texcoords[2*o+1]=w[k].y/.6f;
                } else if(src->texcoords)memcpy(dst->texcoords+2*o,src->texcoords+2*at,2*sizeof(float));
                else memset(dst->texcoords+2*o,0,2*sizeof(float));
            }
        }
        for(int k=0;k<2;k++) {
            Mesh* mesh=&part[k];mesh->triangleCount=mesh->vertexCount/3;
            if(!mesh->triangleCount){MemFree(mesh->vertices);MemFree(mesh->normals);MemFree(mesh->texcoords);MemFree(mesh->texcoords2);continue;}
            UploadMesh(mesh,false);
            out->meshMaterial[out->meshCount]=k?source->materialCount:source->meshMaterial[m];
            out->meshes[out->meshCount++]=*mesh;papered+=k;
        }
    }
    if(!papered){interior_wall_close(a,owner-1);return;}
    out->transform=source->transform;out->materialCount=source->materialCount+1;
    out->materials=MemAlloc(out->materialCount*sizeof(Material));
    memcpy(out->materials,source->materials,source->materialCount*sizeof(Material));
    a->papered_maps[owner-1]=interior_maps(&source->materials[1],a->wallpaper,.82f);
    out->materials[source->materialCount]=source->materials[1];
    out->materials[source->materialCount].maps=a->papered_maps[owner-1];
}

static const Model* motel_wall_source_base(const SwatEnvironmentArt* art,int owner);
static void motel_interior_load(SwatEnvironmentArt* art) {
    const char* enabled=getenv("SWAT_MOTEL_INTERIOR");if(enabled && !strcmp(enabled,"0"))return;
    SwatMotelInteriorArt* a=calloc(1,sizeof(*a));if(!a)return;art->motel_interior=a;
    for(int i=0;i<SWAT_INTERIOR_ASSETS;i++) {
        char file[256];snprintf(file,sizeof(file),"motel_interior_v1/%s",interior_files[i]);
        if(!room101_model_load(&a->models[i],a->normal_scale[i],file)){swat_art_model_close(a->models[i]);a->models[i]=(Model){0};}
    }
    // Both halves of the bed swap together, or neither does.
    if(!a->models[INT_BED_FRAME].meshCount || !a->models[INT_BEDDING_ROSE].meshCount || !a->models[INT_BEDDING_TEAL].meshCount)
        for(int i=INT_BED_FRAME;i<=INT_BEDDING_TEAL;i++){swat_art_model_close(a->models[i]);a->models[i]=(Model){0};}
    interior_pieces_build(a);
    a->wallpaper=load_surface("motel_interior_v1/wallpaper_stripe_damask_512.png",true);
    a->carpet=load_surface("motel_interior_v1/carpet_burgundy_medallion_512.png",false);
    a->tile=load_surface("motel_interior_v1/bath_tile_grout_512.png",false);
    for(int owner=1;owner<=SWAT_MOTEL_INSTANCES;owner++) {
        int asset=swat_motel_instance(owner-1)->asset;
        if(asset==0 || asset==1 || asset==2 || asset==21 || asset==22)
            interior_wall_build(a,motel_wall_source_base(art,owner),owner);
    }
    // Floor module: carpet (slot 2) and bath tile (slot 3) swapped on a shallow
    // copy; meshes, bounds and the other slots stay the loaded originals.
    const Model* floor=&art->motel[4];
    if(floor->meshCount && floor->materialCount==4 && a->carpet.id && a->tile.id) {
        a->floor=*floor;a->floor.materials=MemAlloc(floor->materialCount*sizeof(Material));
        memcpy(a->floor.materials,floor->materials,floor->materialCount*sizeof(Material));
        a->floor_maps[0]=interior_maps(&floor->materials[2],a->carpet,.95f);
        a->floor_maps[1]=interior_maps(&floor->materials[3],a->tile,.45f);
        a->floor.materials[2].maps=a->floor_maps[0];a->floor.materials[3].maps=a->floor_maps[1];
    }
}

// Party-wall drywall skins (framed walls 106-108) facing a papered room.
static bool motel_interior_skin(const SwatEnvironmentArt* art,const SwatObject* o) {
    if(art->location!=1 || !art->motel_interior || !art->motel_interior->wallpaper.id)return false;
    if(o->material!=SWAT_DRYWALL || o->part!=SWAT_PART_SKIN)return false;
    // Skins sit on the 4 m party-wall line; test a point 0.1 m into their room.
    float x=(float)o->center.x,wall=4*roundf(x/4);
    return interior_papered_point(x+(x>wall?.1f:-.1f),(float)o->center.y,(float)o->center.z);
}

#endif
