#include "environment_art.h"
#include "lighting.h"
#include "rlgl.h"
#include "raymath.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


static bool prop_bounds_fit(Model model,b3Vec3 size) {
    if(!model.meshCount) return false;
    BoundingBox box=GetModelBoundingBox(model);
    // Fail closed if an explicit override is oversized or has a wrong pivot.
    return box.min.x>=-size.x*.5f && box.max.x<=size.x*.5f &&
        box.min.z>=-size.z*.5f && box.max.z<=size.z*.5f &&
        box.min.y>=-1e-5f && box.min.y<=1e-5f && box.max.y<=size.y && box.max.y>0;
}

static bool asset_path(char* path,size_t size,const char* name) {
    const char* custom=getenv("SWAT_ENVIRONMENT_ASSETS");
    if(custom && custom[0]) {
        snprintf(path,size,"%s/%s",custom,name);
        return FileExists(path); // Explicit override never mixes in default files.
    }
    snprintf(path,size,"%sassets/environment/%s",GetApplicationDirectory(),name);
    if(FileExists(path)) return true;
    snprintf(path,size,"ocean/swat/assets/environment/%s",name);
    return FileExists(path);
}

static Texture2D load_surface(const char* name,bool metric) {
    char path[4096]; Texture2D texture={0};
    if(asset_path(path,sizeof(path),name)) {
        if(metric) {
            // New source PNGs define top row as V=1. Reverse upload rows once;
            // positive metric V and OpenGL +Y normals then share one basis.
            Image image=LoadImage(path);
            if(image.data) { ImageFlipVertical(&image); texture=LoadTextureFromImage(image); UnloadImage(image); }
        } else texture=LoadTexture(path);
    }
    if(texture.id) {
        GenTextureMipmaps(&texture);
        SetTextureFilter(texture,TEXTURE_FILTER_TRILINEAR);
        SetTextureWrap(texture,TEXTURE_WRAP_REPEAT);
    }
    return texture;
}

void swat_environment_art_init(SwatEnvironmentArt* art) {
    if(art->initialized) return;
    art->initialized=true;
    const char* enabled=getenv("SWAT_ENVIRONMENT_ART");
    if(enabled && !strcmp(enabled,"0")) return;
    const char* plaster_style=getenv("SWAT_PLASTER_STYLE");
    if(!plaster_style || strcmp(plaster_style,"weathered")) {
        art->plaster=load_surface("painted_plaster_basecolor_v1.png",false);
        art->plaster_tile_metres=1;
    }
    if(!art->plaster.id) {
        art->plaster=load_surface("plaster_diffuse.png",false); art->plaster_tile_metres=1.8f;
    }
    art->wood=load_surface("wood_diffuse.png",false);
    art->legacy_plaster=plaster_style && !strcmp(plaster_style,"weathered");
    const char* style=getenv("SWAT_ENVIRONMENT_STYLE"),*pbr=getenv("SWAT_ENVIRONMENT_PBR");
    if(!style || strcmp(style,"legacy")) {
        static const char* names[SWAT_SURFACE_COUNT]={"plaster_painted","floor_pine","framing_pine","door_paint","door_wood","plaster_worn"};
        static const Vector2 tiles[SWAT_SURFACE_COUNT]={{1,1},{2,2},{.4f,2},{.6f,2},{.6f,2},{2,2}};
        for(int i=0;i<SWAT_SURFACE_COUNT;i++) {
            SwatSurfaceMaps* m=&art->surfaces[i]; char file[256]; m->tile=tiles[i];
            snprintf(file,sizeof(file),"materials_v1/%s_basecolor.png",names[i]); m->color=load_surface(file,true);
            if(m->color.id && (!pbr || strcmp(pbr,"0"))) {
                snprintf(file,sizeof(file),"materials_v1/%s_normal.png",names[i]); m->normal=load_surface(file,true);
                snprintf(file,sizeof(file),"materials_v1/%s_roughness.png",names[i]); m->roughness=load_surface(file,true);
            }
        }
    }
    char path[4096];
    if(asset_path(path,sizeof(path),"door_leaf.glb")) art->door=LoadModel(path);
    int missing_props=0;
    for(int i=0;i<SWAT_ENV_PROP_KINDS;i++) {
        if(asset_path(path,sizeof(path),swat_environment_prop_specs[i].file)) art->props[i]=LoadModel(path);
        if(!prop_bounds_fit(art->props[i],swat_environment_prop_specs[i].size)) {
            swat_art_model_close(art->props[i]); art->props[i]=(Model){0};
        }
        if(!art->props[i].meshCount) missing_props++;
    }
    if(missing_props) TraceLog(LOG_WARNING,"SWAT: %d decorative props unavailable; omitted without affecting supports",missing_props);
    if(!art->plaster.id || !art->wood.id || !art->door.meshCount)
        TraceLog(LOG_WARNING,"SWAT: environment art incomplete; missing pieces use graybox rendering");
}

void swat_environment_art_prepare_location(SwatEnvironmentArt* art,const SwatWorld* world) {
    const char* enabled=getenv("SWAT_ENVIRONMENT_ART");
    if(!art->initialized || (enabled && !strcmp(enabled,"0"))) return;
    int selected=world->storefront ? 2 : world->motel ? 1 : 0;
    if(art->location==selected) return;
    for(int i=0;i<SWAT_MOTEL_ASSETS;i++) { swat_art_model_close(art->motel[i]); art->motel[i]=(Model){0}; }
    for(int i=0;i<SWAT_STOREFRONT_ASSETS;i++) { swat_art_model_close(art->storefront[i]); art->storefront[i]=(Model){0}; }
    art->location=selected;
    if(!selected) return;
    char path[4096];
    int location=selected-1;
    int missing=0;
    for(int i=0;i<(location ? SWAT_STOREFRONT_ASSETS : SWAT_MOTEL_ASSETS);i++) {
        const SwatMotelAsset* a=location ? swat_storefront_asset(i) : swat_motel_asset(i);
        char file[256]; snprintf(file,sizeof(file),"%s/%s",location ? "storefront_v1" : "motel_v1",a->file);
        Model* model=location ? &art->storefront[i] : &art->motel[i];
        if(asset_path(path,sizeof(path),file)) *model=LoadModel(path);
        if(!model->meshCount || model->materialCount!=a->material_count+1) { swat_art_model_close(*model); *model=(Model){0}; missing++; continue; }
        // Raylib 5.5 reads scalar glTF PBR factors only when an ORM texture
        // exists. Restore the catalogued original factors for scalar materials.
        for(int m=0;m<a->material_count;m++) {
            Material* material=&model->materials[m+1];
            material->maps[MATERIAL_MAP_ROUGHNESS].value=a->materials[m].roughness;
            material->maps[MATERIAL_MAP_METALNESS].value=a->materials[m].metalness;
            material->maps[MATERIAL_MAP_NORMAL].value=2; // Signed UV derivatives.
            for(int k=0;k<=MATERIAL_MAP_BRDF;k++) {
                Texture2D* t=&material->maps[k].texture;
                if(t->id && t->id!=rlGetTextureIdDefault()) { GenTextureMipmaps(t); SetTextureFilter(*t,TEXTURE_FILTER_TRILINEAR); }
            }
        }
    }
    if(missing) TraceLog(LOG_WARNING,"SWAT: %d %s modules absent; matching colliders use graybox rendering",missing,location ? "storefront" : "motel");
}

void swat_art_model_close(Model model) {
    // Raylib UnloadModel owns meshes/material arrays but not their textures.
    // glTF materials can share one image: release every unique texture once.
    for(int m=0;m<model.materialCount;m++) for(int k=0;k<(MATERIAL_MAP_BRDF+1);k++) {
        unsigned int id=model.materials[m].maps[k].texture.id;
        if(!id || id==rlGetTextureIdDefault()) continue;
        bool earlier=false;
        for(int a=0;a<=m;a++) for(int b=0;b<(MATERIAL_MAP_BRDF+1);b++) {
            if(a==m && b>=k) break;
            if(model.materials[a].maps[b].texture.id==id) earlier=true;
        }
        if(!earlier) UnloadTexture(model.materials[m].maps[k].texture);
    }
    // Even a failed GLB load can own Raylib's fallback material allocation.
    if(model.meshCount || model.materialCount) UnloadModel(model);
}

void swat_environment_art_close(SwatEnvironmentArt* art) {
    if(art->plaster.id) UnloadTexture(art->plaster);
    if(art->wood.id) UnloadTexture(art->wood);
    for(int i=0;i<SWAT_SURFACE_COUNT;i++) {
        if(art->surfaces[i].color.id) UnloadTexture(art->surfaces[i].color);
        if(art->surfaces[i].normal.id) UnloadTexture(art->surfaces[i].normal);
        if(art->surfaces[i].roughness.id) UnloadTexture(art->surfaces[i].roughness);
    }
    swat_art_model_close(art->door);
    for(int i=0;i<SWAT_MOTEL_ASSETS;i++) swat_art_model_close(art->motel[i]);
    for(int i=0;i<SWAT_STOREFRONT_ASSETS;i++) swat_art_model_close(art->storefront[i]);
    for(int i=0;i<SWAT_ENV_PROP_KINDS;i++) swat_art_model_close(art->props[i]);
    memset(art,0,sizeof(*art));
}

static Color wear_color(const SwatObject* o,float shade) {
    // Lost wall cells expose framing; surviving cells need only restrained wear.
    float wear=o->max_health>0 ? .94f+.06f*swat_clamp(o->health/o->max_health,0,1) : 1;
    unsigned char value=(unsigned char)(255*wear*shade);
    return (Color){value,value,value,255};
}

static void textured_box(Texture2D texture,const SwatObject* o,bool lit,Vector2 tile,int grain,bool metric) {
    // Six independently UV-mapped faces. UVs are in metres, rather than stretched
    // once per damage cell. Translation in the wall basis keeps adjacent skins
    // continuous, including on cardinally rotated generated walls.
    static const float corners[6][4][3]={
        {{ 1,-1, 1},{ 1,-1,-1},{ 1, 1,-1},{ 1, 1, 1}},
        {{-1,-1,-1},{-1,-1, 1},{-1, 1, 1},{-1, 1,-1}},
        {{-1, 1, 1},{ 1, 1, 1},{ 1, 1,-1},{-1, 1,-1}},
        {{-1,-1,-1},{ 1,-1,-1},{ 1,-1, 1},{-1,-1, 1}},
        {{-1,-1, 1},{ 1,-1, 1},{ 1, 1, 1},{-1, 1, 1}},
        {{ 1,-1,-1},{-1,-1,-1},{-1, 1,-1},{ 1, 1,-1}}
    };
    static const float normals[6][3]={{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
    static const float shade[6]={.88f,.80f,1,.70f,.94f,.84f};
    float cy=cosf(o->yaw),sy=sinf(o->yaw);
    float ox=cy*(float)o->center.x-sy*(float)o->center.z;
    float oz=sy*(float)o->center.x+cy*(float)o->center.z;
    rlSetTexture(texture.id);
    rlBegin(RL_QUADS);
    for(int face=0;face<6;face++) {
        Color color=wear_color(o,lit ? 1 : shade[face]);
        rlColor4ub(color.r,color.g,color.b,color.a);
        rlNormal3f(normals[face][0],normals[face][1],normals[face][2]);
        for(int v=0;v<4;v++) {
            float x=corners[face][v][0]*o->half.x;
            float y=corners[face][v][1]*o->half.y;
            float z=corners[face][v][2]*o->half.z;
            float axes[3]={ox+x,(float)o->center.y+y,oz+z};
            int u_axis=face<2 ? 2 : 0,v_axis=(face==2 || face==3) ? 2 : 1;
            if(grain>=0 && u_axis==grain) { int old=u_axis; u_axis=v_axis; v_axis=old; }
            // End caps use the remaining planar axes rather than degenerate UVs.
            rlTexCoord2f(axes[u_axis]/tile.x,(metric ? 1 : -1)*axes[v_axis]/tile.y); rlVertex3f(x,y,z);
        }
    }
    rlEnd(); rlSetTexture(0);
}

static void clear_surface(const SwatEnvironmentArt* art) {
    swat_lighting_surface(art->lighting,(Texture2D){0},(Texture2D){0},(Vector3){0},(Vector2){0},false);
}

static bool material_door(const SwatEnvironmentArt* art,const SwatObject* o) {
    if(!art->door.meshCount || !art->surfaces[SWAT_SURFACE_DOOR_PAINT].color.id) return false;
    static const int material_slots[5]={2,2,1,3,4};
    if(art->door.meshCount!=5 || art->door.materialCount!=5) return false;
    for(int i=0;i<5;i++) if(art->door.meshMaterial[i]!=material_slots[i]) return false;
    // Preserve the original leaf/recess/hardware geometry; do not apply a box
    // texture to its non-metric source UVs. Mesh shader projects unit positions.
    // Without lighting, retain the original GLB until a valid projection exists.
    if(!art->lit || !art->lighting) return false;
    clear_surface(art);
    Model model=art->door; Vector3 size={2*o->half.x,2*o->half.y,2*o->half.z};
    Matrix transform=MatrixMultiply(model.transform,MatrixScale(size.x,size.y,size.z));
    for(int i=0;i<model.meshCount;i++) {
        int index=model.meshMaterial[i]; Material material=model.materials[index];
        // Raylib reserves slot 0; pinned GLB IDs + 1 are brass, paint,
        // recessed wood and exposed plaster. Hardware retains its source map.
        int kind=index==2 || index==3 ? SWAT_SURFACE_DOOR_PAINT : index==4 ? SWAT_SURFACE_WORN : -1;
        const SwatSurfaceMaps* maps=kind>=0 ? &art->surfaces[kind] : NULL;
        MaterialMap local[MATERIAL_MAP_BRDF+1]; memcpy(local,material.maps,sizeof(local)); material.maps=local;
        Vector2 tile={0}; Texture2D normal={0},roughness={0};
        if(maps && maps->color.id) {
            local[MATERIAL_MAP_ALBEDO].texture=maps->color;
            local[MATERIAL_MAP_ALBEDO].color=wear_color(o,1);
            tile=maps->tile; normal=maps->normal; roughness=maps->roughness;
        }
        swat_lighting_surface(art->lighting,normal,roughness,size,tile,true);
        DrawMesh(model.meshes[i],material,transform);
    }
    swat_lighting_surface(art->lighting,(Texture2D){0},(Texture2D){0},(Vector3){0},(Vector2){0},true);
    clear_surface(art);
    return true;
}

bool swat_environment_art_draw(const SwatEnvironmentArt* art,const SwatObject* o) {
    SwatEnvironmentSurface surface=swat_environment_surface(o);
    if(surface==SWAT_ENV_DOOR && material_door(art,o)) return true;
    if(surface==SWAT_ENV_DOOR && art->door.meshCount) {
        clear_surface(art);
        DrawModelEx(art->door,(Vector3){0},(Vector3){0,1,0},0,
            (Vector3){2*o->half.x,2*o->half.y,2*o->half.z},wear_color(o,1));
        return true;
    }
    Texture2D texture={0};
    int kind=surface==SWAT_ENV_PLASTER ? SWAT_SURFACE_PLASTER : surface==SWAT_ENV_DOOR ? SWAT_SURFACE_DOOR_PAINT : SWAT_SURFACE_DOOR_WOOD;
    int grain=surface==SWAT_ENV_DOOR ? 1 : -1;
    if(surface==SWAT_ENV_WOOD) {
        if(o->part==SWAT_PART_FRAME || o->part==SWAT_PART_SUPPORT) kind=SWAT_SURFACE_FRAME;
        else if(o->center.y+o->half.y<=.05f && o->half.y<.05f) kind=SWAT_SURFACE_FLOOR;
        grain=kind==SWAT_SURFACE_FLOOR ? 2 : o->half.y>=o->half.x && o->half.y>=o->half.z ? 1 : o->half.z>=o->half.x ? 2 : 0;
    }
    const SwatSurfaceMaps* maps=&art->surfaces[kind];
    if(surface!=SWAT_ENV_NONE && maps->color.id && !(surface==SWAT_ENV_PLASTER && art->legacy_plaster)) {
        swat_lighting_surface(art->lighting,maps->normal,maps->roughness,(Vector3){0},(Vector2){0},false);
        textured_box(maps->color,o,art->lit,maps->tile,grain,true);
        return true;
    }
    clear_surface(art);
    if(surface==SWAT_ENV_PLASTER) texture=art->plaster;
    if(surface==SWAT_ENV_WOOD || surface==SWAT_ENV_DOOR) texture=art->wood;
    if(!texture.id) return false;
    float tile=surface==SWAT_ENV_PLASTER ? art->plaster_tile_metres : 2;
    textured_box(texture,o,art->lit,(Vector2){tile,tile},-1,false);
    return true;
}

bool swat_environment_art_has_floor(const SwatEnvironmentArt* art,SwatMaterial floor) {
    return floor==SWAT_WOOD && (art->wood.id || art->surfaces[SWAT_SURFACE_FLOOR].color.id);
}

void swat_environment_art_draw_props(const SwatEnvironmentArt* art,const SwatWorld* world,
                                     const SwatLayout* layout) {
    clear_surface(art);
    bool loaded=false;
    for(int i=0;i<SWAT_ENV_PROP_KINDS;i++) if(art->props[i].meshCount) loaded=true;
    if(!loaded) return;
    SwatEnvironmentProp props[SWAT_ENV_PROP_MAX];
    int count=swat_environment_props(world,layout,props);
    // Raylib ignores glTF doubleSided; thin cloth/lens details need both faces.
    rlDrawRenderBatchActive();
    rlDisableBackfaceCulling();
    for(int i=0;i<count;i++) {
        const SwatEnvironmentProp* p=&props[i];
        Model model=art->props[p->kind]; if(!model.meshCount) continue;
        const SwatObject* support=&world->objects[p->support];
        rlPushMatrix();
        rlTranslatef((float)support->center.x,(float)support->center.y,(float)support->center.z);
        rlRotatef(support->yaw/SWAT_RAD,0,1,0);
        rlRotatef(support->pitch/SWAT_RAD,0,0,1);
        DrawModelEx(model,(Vector3){p->local.x,p->local.y,p->local.z},(Vector3){0,1,0},
            p->yaw/SWAT_RAD,(Vector3){1,1,1},wear_color(support,1));
        rlPopMatrix();
    }
    rlDrawRenderBatchActive();
    rlEnableBackfaceCulling();
}

static bool location_draw(const SwatEnvironmentArt* art,const SwatObject* o,const SwatMotelInstance* p,const Model* model,bool shadow,bool cutaway,bool transparent) {
    if(!o->active) return true;
    if(cutaway && p->roof) return true;
    if(!model->meshCount) return false;
    Vector3 origin={(float)p->origin.x,(float)p->origin.y,(float)p->origin.z}; float yaw=p->yaw;
    if(p->door) { origin=(Vector3){o->hinge.x,o->hinge.y-o->half.y,o->hinge.z}; yaw=o->yaw-SWAT_PI*.5f; }
    Matrix scale=MatrixScale(p->scale.x,p->scale.y,p->scale.z);
    Matrix transform=MatrixMultiply(MatrixMultiply(scale,MatrixRotateY(yaw)),MatrixTranslate(origin.x,origin.y,origin.z));
    bool lit=!shadow && art->lighting && art->lighting->enabled && art->lighting->prepared;
    rlDrawRenderBatchActive();
    for(int i=0;i<model->meshCount;i++) {
        Material material=model->materials[model->meshMaterial[i]];
        bool blend=material.maps[MATERIAL_MAP_ALBEDO].color.a<255;
        if(blend!=transparent || (shadow && blend)) continue;
        material.shader=lit?art->lighting->mesh.shader:(Shader){rlGetShaderIdDefault(),rlGetShaderLocsDefault()};
        if(lit) swat_lighting_material(art->lighting,material,true);
        DrawMesh(model->meshes[i],material,transform);
    }
    if(lit) swat_lighting_material(art->lighting,(Material){0},false);
    return true;
}
bool swat_environment_motel_draw(const SwatEnvironmentArt* art,const SwatWorld* world,const SwatObject* o,bool shadow,bool cutaway) {
    if(!world->motel || o->tag.index<1 || o->tag.index>SWAT_MOTEL_INSTANCES) return false;
    const SwatMotelInstance* p=swat_motel_instance(o->tag.index-1);
    return location_draw(art,o,p,&art->motel[p->asset],shadow,cutaway,false);
}
bool swat_environment_storefront_draw(const SwatEnvironmentArt* art,const SwatWorld* world,const SwatObject* o,bool shadow,bool cutaway) {
    if(!world->storefront || o->tag.index<1 || o->tag.index>SWAT_STOREFRONT_INSTANCES) return false;
    const SwatMotelInstance* p=swat_storefront_instance(o->tag.index-1);
    return location_draw(art,o,p,&art->storefront[p->asset],shadow,cutaway,false);
}
typedef struct TransparentObject { float distance; int index; } TransparentObject;
static int farthest_first(const void* a,const void* b) {
    float x=((const TransparentObject*)a)->distance,y=((const TransparentObject*)b)->distance;
    return x<y ? 1 : x>y ? -1 : 0;
}
void swat_environment_art_transparent(const SwatEnvironmentArt* art,const SwatWorld* world,Vector3 eye,bool cutaway) {
    if(!world->motel && !world->storefront) return;
    TransparentObject order[SWAT_MAX_OBJECTS]; int count=0;
    for(int i=1;i<world->count;i++) {
        const SwatObject* o=&world->objects[i]; if(!o->active) continue;
        const SwatMotelInstance* p=world->storefront ? swat_storefront_instance(i-1) : swat_motel_instance(i-1);
        if(!p || (cutaway && p->roof)) continue;
        const Model* model=world->storefront ? &art->storefront[p->asset] : &art->motel[p->asset];
        bool blend=false; for(int m=0;m<model->meshCount;m++) blend=blend || model->materials[model->meshMaterial[m]].maps[MATERIAL_MAP_ALBEDO].color.a<255;
        if(blend) order[count++]=(TransparentObject){Vector3DistanceSqr(eye,(Vector3){o->center.x,o->center.y,o->center.z}),i};
    }
    qsort(order,(size_t)count,sizeof(*order),farthest_first);
    rlDrawRenderBatchActive(); rlDisableDepthMask();
    for(int i=0;i<count;i++) {
        const SwatObject* o=&world->objects[order[i].index];
        const SwatMotelInstance* p=world->storefront ? swat_storefront_instance(o->tag.index-1) : swat_motel_instance(o->tag.index-1);
        const Model* model=world->storefront ? &art->storefront[p->asset] : &art->motel[p->asset];
        location_draw(art,o,p,model,false,cutaway,true);
    }
    rlDrawRenderBatchActive(); rlEnableDepthMask();
}
