// The Hollow woods v2 (presentation only). Included by hollow_art.h.
// See HOLLOW.md "Woods" and assets/environment/hollow_woods/README.md.
//
// Late-autumn Appalachian trees, the laurel thicket and forest-floor scatter,
// built by tools/blender/hollow_trees.py and placed by its seeded
// woods_placement.txt. Every mesh is drawn instanced: one DrawMeshInstanced
// per (mesh, part) per pass, with the ordinary lit mesh program
// (lighting.c useInstancing), so the woods get the dusk sun, fog, bulbs and
// grade like everything else.
//   - LOD by camera distance per instance: lod0 full branches + twig cards,
//     lod1 first two branch orders + whole-limb cards, lod2 a crossed-quad
//     impostor baked from lod0. Scatter and shrubs fade out by distance.
//   - Foliage cards are alpha tested (253/255 albedo alpha, lighting.c) and
//     double sided; the sun-shadow pass uses an instanced alpha-tested depth
//     program, for the trees flagged near the clearing only (lod1).
//   - The planning cutaway draws only the trunks of trees whose crowns could
//     hang over the clearing.
// No collision, navigation, protocol or simulation state is touched.
// SWAT_HOLLOW_WOODS=0 restores the previous low-poly woods GLB.
#define HW_MAX_MESHES 96
#define HW_MAX_SETS 64
#define HW_MAX_INSTANCES 6144
#define HW_TEXTURES 10
// LOD distances (metres from the eye, per unit instance scale).
#define HW_LOD0 14.0f
#define HW_LOD1 30.0f
enum { HW_FLAG_SHADOW=1, HW_FLAG_EDGE=2 };
static const char* const hw_texture_names[HW_TEXTURES]={"oak","poplar","sycamore","hemlock","pine","laurel","rock","log","impostor","wall"};
typedef struct HwMesh {
    Model model; int texture; int bark,card; // mesh indices in model, -1 none
    int band; // draw order: 0 lod0/scatter, 1 lod1, 2 impostor, 3 forest wall
    float radius,height; int tris;
} HwMesh;
typedef struct HwInstance { Matrix transform; Vector3 position; float scale,radius,height; short set; unsigned char flags; } HwInstance;
typedef struct SwatHollowWoods {
    HwMesh meshes[HW_MAX_MESHES]; char mesh_names[HW_MAX_MESHES][48]; int mesh_count;
    short sets[HW_MAX_SETS][3]; int set_count;
    HwInstance* instances; int instance_count;
    Texture2D color[HW_TEXTURES],normal[HW_TEXTURES],atlas;
    Shader depth; int depth_attrib;
    int shader_locs[RL_MAX_SHADER_LOCATIONS];
    Matrix* lists[HW_MAX_MESHES]; int list_count[HW_MAX_MESHES],trunk_count[HW_MAX_MESHES];
    float* depth_key[HW_MAX_MESHES]; int* sort_index; Matrix* sort_scratch;
    Vector3 eye; bool eye_known;
    int draws,tris; // last lit pass, for review tools
    float lod0,lod1;
    int layers; // SWAT_HOLLOW_WOODS_LAYERS diagnostic mask: 1 trees, 2 shrubs, 4 floor scatter (default 7)
} SwatHollowWoods;

static bool hollow_woods_enabled(void) {
    const char* e=getenv("SWAT_HOLLOW_WOODS");
    return !e || strcmp(e,"0");
}
static Texture2D hw_texture(const char* file,bool repeat) {
    char path[4096];Texture2D t={0};
    if(!asset_path(path,sizeof(path),file))return t;
    t=LoadTexture(path);
    if(t.id) {
        GenTextureMipmaps(&t);SetTextureFilter(t,TEXTURE_FILTER_TRILINEAR);
        SetTextureFilter(t,TEXTURE_FILTER_ANISOTROPIC_4X);SetTextureWrap(t,repeat?TEXTURE_WRAP_REPEAT:TEXTURE_WRAP_CLAMP);
    }
    return t;
}
static char* hw_text(const char* name) {
    char path[4096];if(!asset_path(path,sizeof(path),name))return NULL;
    return LoadFileText(path);
}
static int hw_mesh_find(const SwatHollowWoods* w,const char* name) {
    for(int i=0;i<w->mesh_count;i++) if(!strcmp(w->mesh_names[i],name)) return i;
    return -1;
}
static void hollow_woods_close(SwatHollowWoods* w) {
    if(!w)return;
    for(int i=0;i<w->mesh_count;i++) { UnloadModel(w->meshes[i].model); free(w->lists[i]); free(w->depth_key[i]); }
    free(w->sort_index); free(w->sort_scratch);
    for(int i=0;i<HW_TEXTURES;i++) { if(w->color[i].id)UnloadTexture(w->color[i]); if(w->normal[i].id)UnloadTexture(w->normal[i]); }
    if(w->atlas.id)UnloadTexture(w->atlas);
    if(w->depth.id && w->depth.id!=rlGetShaderIdDefault())UnloadShader(w->depth);
    free(w->instances); free(w);
}
// Instanced depth program for the sun-shadow pass: alpha-tested cards.
static const char* hw_depth_vs=
    "#version 330\n"
    "in vec3 vertexPosition; in vec2 vertexTexCoord; in mat4 instanceTransform;\n"
    "uniform mat4 mvp; out vec2 uv;\n"
    "void main(){ uv=vertexTexCoord; gl_Position=mvp*instanceTransform*vec4(vertexPosition,1.0); }\n";
static const char* hw_depth_fs=
    "#version 330\n"
    "in vec2 uv; uniform sampler2D texture0; uniform vec4 colDiffuse; out vec4 finalColor;\n"
    "void main(){ if(texture(texture0,uv).a*colDiffuse.a<0.5)discard; finalColor=vec4(1.0); }\n";

static SwatHollowWoods* hollow_woods_load(void) {
    if(!hollow_woods_enabled())return NULL;
    char* meshes=hw_text("hollow_woods/woods_meshes.txt");
    char* placement=meshes?hw_text("hollow_woods/woods_placement.txt"):NULL;
    SwatHollowWoods* w=calloc(1,sizeof(*w));
    if(!meshes || !placement || !w) { if(meshes)UnloadFileText(meshes); if(placement)UnloadFileText(placement); free(w); return NULL; }
    for(int i=0;i<HW_TEXTURES && hw_texture_names[i];i++) {
        char file[160];
        if(!strcmp(hw_texture_names[i],"impostor")) { w->color[i]=hw_texture("hollow_woods/impostor_atlas.png",false); continue; }
        if(!strcmp(hw_texture_names[i],"wall")) { w->color[i]=hw_texture("hollow_woods/forest_wall.png",false); SetTextureWrap(w->color[i],TEXTURE_WRAP_REPEAT); continue; }
        const char* name=strcmp(hw_texture_names[i],"log")?hw_texture_names[i]:"oak"; // logs reuse the oak bark
        snprintf(file,sizeof(file),"hollow_woods/textures/bark_%s_color.jpg",name); w->color[i]=hw_texture(file,true);
        snprintf(file,sizeof(file),"hollow_woods/textures/bark_%s_normal.jpg",name); w->normal[i]=hw_texture(file,true);
    }
    w->atlas=hw_texture("hollow_woods/foliage_atlas.png",false);
    bool ok=w->atlas.id!=0;
    for(char* line=strtok(meshes,"\n");line && ok;line=strtok(NULL,"\n")) {
        char path[64],tex[24],parts[8];float radius,height;int tris;
        if(line[0]=='#' || sscanf(line,"mesh %63s %23s %7s %f %f %d",path,tex,parts,&radius,&height,&tris)!=6)continue;
        if(w->mesh_count>=HW_MAX_MESHES){ok=false;break;}
        HwMesh* m=&w->meshes[w->mesh_count];
        char file[160];snprintf(file,sizeof(file),"hollow_woods/%s.glb",path);
        char full[4096];
        if(!asset_path(full,sizeof(full),file)){ok=false;break;}
        m->model=LoadModel(full);
        int expected=(strchr(parts,'b')?1:0)+(strchr(parts,'c')?1:0);
        if(m->model.meshCount!=expected){UnloadModel(m->model);ok=false;break;}
        m->bark=strchr(parts,'b')?0:-1; m->card=strchr(parts,'c')?(m->bark<0?0:1):-1;
        m->texture=-1;
        for(int t=0;t<HW_TEXTURES && hw_texture_names[t];t++) if(!strcmp(tex,hw_texture_names[t])) m->texture=t;
        if(m->texture<0 || !w->color[m->texture].id){UnloadModel(m->model);ok=false;break;}
        m->radius=radius;m->height=height;m->tris=tris;
        m->band=!strcmp(tex,"wall")?3:strstr(path,"_lod2")?2:strstr(path,"_lod1")?1:0;
        snprintf(w->mesh_names[w->mesh_count],sizeof(w->mesh_names[0]),"%s",path);
        w->mesh_count++;
    }
    UnloadFileText(meshes);
    w->instances=ok?calloc(HW_MAX_INSTANCES,sizeof(HwInstance)):NULL; ok=ok && w->instances;
    for(char* line=ok?strtok(placement,"\n"):NULL;line && ok;line=strtok(NULL,"\n")) {
        if(!strncmp(line,"set ",4)) {
            char name[48],l[3][64];
            if(sscanf(line,"set %47s %63s %63s %63s",name,l[0],l[1],l[2])!=4 || w->set_count>=HW_MAX_SETS){ok=false;break;}
            for(int k=0;k<3;k++) {
                w->sets[w->set_count][k]=(short)(strcmp(l[k],"-")?hw_mesh_find(w,l[k]):-1);
                if(strcmp(l[k],"-") && w->sets[w->set_count][k]<0) ok=false;
            }
            w->set_count++;
        } else if(line[0]=='i') {
            int set,flags;float x,y,z,yaw,s;
            if(sscanf(line,"i %d %f %f %f %f %f %d",&set,&x,&y,&z,&yaw,&s,&flags)!=7 || set<0 || set>=w->set_count || w->instance_count>=HW_MAX_INSTANCES)continue;
            HwInstance* in=&w->instances[w->instance_count++];
            in->set=(short)set;in->flags=(unsigned char)flags;in->position=(Vector3){x,y,z};in->scale=s;
            const HwMesh* m=&w->meshes[w->sets[set][0]];
            in->radius=m->radius*s;in->height=m->height*s;
            in->transform=MatrixMultiply(MatrixMultiply(MatrixScale(s,s,s),MatrixRotateY(yaw)),MatrixTranslate(x,y,z));
        }
    }
    UnloadFileText(placement);
    for(int i=0;i<w->mesh_count && ok;i++) {
        w->lists[i]=malloc(sizeof(Matrix)*HW_MAX_INSTANCES); w->depth_key[i]=malloc(sizeof(float)*HW_MAX_INSTANCES);
        ok=w->lists[i] && w->depth_key[i];
    }
    if(ok) { w->sort_index=malloc(sizeof(int)*HW_MAX_INSTANCES); w->sort_scratch=malloc(sizeof(Matrix)*HW_MAX_INSTANCES); ok=w->sort_index && w->sort_scratch; }
    if(ok) {
        w->depth=LoadShaderFromMemory(hw_depth_vs,hw_depth_fs);
        w->depth_attrib=GetShaderLocationAttrib(w->depth,"instanceTransform");
        if(w->depth.id==rlGetShaderIdDefault() || w->depth_attrib<0) ok=false;
    }
    if(!ok || !w->instance_count) { TraceLog(LOG_WARNING,"SWAT: hollow woods unavailable; using the old woods"); hollow_woods_close(w); return NULL; }
    { const char* e=getenv("SWAT_HOLLOW_WOODS_LAYERS"); w->layers=e?atoi(e):7; }
    w->lod0=HW_LOD0;w->lod1=HW_LOD1;
    TraceLog(LOG_INFO,"SWAT: hollow woods: %d meshes, %d sets, %d instances",w->mesh_count,w->set_count,w->instance_count);
    return w;
}

#define HW_SCATTER_FAR 42.0f
#define HW_SHRUB_FAR 45.0f
static const float* hw_sort_keys;
static int hw_sort_compare(const void* a,const void* b) {
    float x=hw_sort_keys[*(const int*)a],y=hw_sort_keys[*(const int*)b];
    return x<y?-1:x>y?1:(*(const int*)a-*(const int*)b);
}
static void hw_sort(SwatHollowWoods* w,int mesh,int n) {
    for(int k=0;k<n;k++) w->sort_index[k]=k;
    hw_sort_keys=w->depth_key[mesh]; qsort(w->sort_index,(size_t)n,sizeof(int),hw_sort_compare);
    for(int k=0;k<n;k++) w->sort_scratch[k]=w->lists[mesh][w->sort_index[k]];
    memcpy(w->lists[mesh],w->sort_scratch,sizeof(Matrix)*(size_t)n);
}
static void hw_lists_reset(SwatHollowWoods* w) { memset(w->list_count,0,sizeof(w->list_count)); memset(w->trunk_count,0,sizeof(w->trunk_count)); }

static void hollow_woods_draw(const SwatEnvironmentArt* art,SwatHollowWoods* w,bool shadow,bool lit,Matrix scene) {
    if(!w)return;
    Vector3 eye=w->eye;
    if(!shadow) {
        Matrix inv=MatrixInvert(rlGetMatrixModelview()); eye=(Vector3){inv.m12,inv.m13,inv.m14}; w->eye=eye; w->eye_known=true;
    } else if(!w->eye_known) return;
    bool cutaway=art->plan_cutaway && !shadow;
    hw_lists_reset(w);
    for(int i=0;i<w->instance_count;i++) {
        const HwInstance* in=&w->instances[i];
        if(shadow && !(in->flags&HW_FLAG_SHADOW)) continue;
        const short* set=w->sets[in->set];
        int layer=set[2]>=0?1:set[1]>=0?2:4;
        if(!(w->layers&layer)) continue;
        float d=Vector3Distance(eye,(Vector3){in->position.x,in->position.y+in->height*.5f,in->position.z});
        int lod;
        if(shadow) lod=set[1]>=0?1:0;
        else if(set[2]>=0 || set[1]>=0) {
            float s=fmaxf(in->scale,.5f);
            lod=d<w->lod0*s?0:(d<w->lod1*s || set[2]<0)?1:2;
            if(set[2]<0 && set[1]>=0 && d>HW_SHRUB_FAR) continue;  // understory shrubs fade into the fog
        } else { if(d>HW_SCATTER_FAR) continue; lod=0; }
        if(set[lod]<0) lod=0;
        if(cutaway && (in->flags&HW_FLAG_SHADOW) && set[2]>=0) lod=set[1]>=0?1:0; // trunk only (below)
        BoundingBox box={{in->position.x-in->radius,in->position.y-.3f,in->position.z-in->radius},
                         {in->position.x+in->radius,in->position.y+in->height+.3f,in->position.z+in->radius}};
        if(art->bounds && bounds_outside(art->bounds,box,scene)) continue;
        int mesh=set[lod];
        // Cutaway: trees that could hang over the clearing keep their trunk only.
        Matrix transform=in->transform;
        if(lod==2) { // impostor billboard: turned to the eye, mirrored on odd instances for variety
            float yaw=atan2f(eye.x-in->position.x,eye.z-in->position.z),sx=(i&1)?-in->scale:in->scale;
            transform=MatrixMultiply(MatrixMultiply(MatrixScale(sx,in->scale,in->scale),MatrixRotateY(yaw)),
                                     MatrixTranslate(in->position.x,in->position.y,in->position.z));
        }
        if(cutaway && (in->flags&HW_FLAG_SHADOW) && set[2]>=0) {
            if(w->list_count[mesh]+w->trunk_count[mesh]<HW_MAX_INSTANCES)
                w->lists[mesh][HW_MAX_INSTANCES-1-w->trunk_count[mesh]++]=transform;
        } else if(w->list_count[mesh]+w->trunk_count[mesh]<HW_MAX_INSTANCES) {
            w->depth_key[mesh][w->list_count[mesh]]=d; w->lists[mesh][w->list_count[mesh]++]=transform;
        }
    }
    SwatLightingProgram* p=lit?&art->lighting->mesh:NULL;
    Shader shader;
    if(shadow) {
        shader=w->depth; memcpy(w->shader_locs,w->depth.locs,sizeof(w->shader_locs));
        w->shader_locs[SHADER_LOC_MATRIX_MODEL]=w->depth_attrib;
    } else if(lit) {
        shader=p->shader; memcpy(w->shader_locs,p->shader.locs,sizeof(w->shader_locs));
        w->shader_locs[SHADER_LOC_MATRIX_MODEL]=p->instance_transform;
        Matrix identity=MatrixIdentity(); int one=1;
        rlEnableShader(p->shader.id);
        if(p->shader.locs[SHADER_LOC_MATRIX_MODEL]>=0) rlSetUniformMatrix(p->shader.locs[SHADER_LOC_MATRIX_MODEL],identity);
        if(p->instancing>=0) rlSetUniform(p->instancing,&one,SHADER_UNIFORM_INT,1);
        if(p->instance_transform<0) return;
    } else return; // unlit fallback: the woods need the lit program
    shader.locs=w->shader_locs;
    w->draws=w->tris=0;
    // Near to far: lod0 and scatter, lod1, impostors, then the forest walls;
    // each list front to back, so the software rasterizer's depth test
    // rejects hidden card fragments before shading them.
    int order[HW_MAX_MESHES],count=0;
    for(int band=0;band<4;band++) for(int i=0;i<w->mesh_count;i++) if(w->meshes[i].band==band) order[count++]=i;
    for(int o=0;o<count;o++) {
        int i=order[o];
        int n=w->list_count[i],trunks=w->trunk_count[i];
        if(n>1) hw_sort(w,i,n);
        if(!n && !trunks) continue;
        const HwMesh* m=&w->meshes[i];
        bool impostor=!strcmp(hw_texture_names[m->texture],"impostor") || !strcmp(hw_texture_names[m->texture],"wall");
        for(int part=0;part<2;part++) {
            int k=part?m->card:m->bark; if(k<0) continue;
            MaterialMap maps[SWAT_MATERIAL_MAPS]; memset(maps,0,sizeof(maps));
            Material material={.shader=shader,.maps=maps};
            if(part) {
                maps[MATERIAL_MAP_ALBEDO].texture=impostor?w->color[m->texture]:w->atlas;
                maps[MATERIAL_MAP_ALBEDO].color=(Color){255,255,255,253};
                maps[MATERIAL_MAP_ROUGHNESS].value=.9f;
            } else {
                maps[MATERIAL_MAP_ALBEDO].texture=shadow?(Texture2D){.id=rlGetTextureIdDefault()}:w->color[m->texture];
                maps[MATERIAL_MAP_ALBEDO].color=WHITE;
                maps[MATERIAL_MAP_NORMAL].texture=w->normal[m->texture]; maps[MATERIAL_MAP_NORMAL].value=2;
                maps[MATERIAL_MAP_ROUGHNESS].value=.92f;
            }
            if(lit) swat_lighting_material_uv(art->lighting,material,true,1,0);
            const Mesh mesh=m->model.meshes[k];
            if(n) { DrawMeshInstanced(mesh,material,w->lists[i],n); w->draws++; w->tris+=n*mesh.triangleCount; }
            if(!part && trunks) { // trunk-only instances (planning cutaway), stored from the top
                DrawMeshInstanced(mesh,material,&w->lists[i][HW_MAX_INSTANCES-trunks],trunks); w->draws++; w->tris+=trunks*mesh.triangleCount;
            }
        }
    }
    if(lit) { int zero=0; rlEnableShader(p->shader.id); if(p->instancing>=0) rlSetUniform(p->instancing,&zero,SHADER_UNIFORM_INT,1); }
}
