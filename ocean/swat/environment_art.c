#include "environment_art.h"
#include "rlgl.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void close_model(Model model);

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

static Texture2D load_surface(const char* name) {
    char path[4096]; Texture2D texture={0};
    if(asset_path(path,sizeof(path),name)) texture=LoadTexture(path);
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
    art->plaster=load_surface("plaster_diffuse.png");
    art->wood=load_surface("wood_diffuse.png");
    char path[4096];
    if(asset_path(path,sizeof(path),"door_leaf.glb")) art->door=LoadModel(path);
    int missing_props=0;
    for(int i=0;i<SWAT_ENV_PROP_KINDS;i++) {
        if(asset_path(path,sizeof(path),swat_environment_prop_specs[i].file)) art->props[i]=LoadModel(path);
        if(!prop_bounds_fit(art->props[i],swat_environment_prop_specs[i].size)) {
            close_model(art->props[i]); art->props[i]=(Model){0};
        }
        if(!art->props[i].meshCount) missing_props++;
    }
    if(missing_props) TraceLog(LOG_WARNING,"SWAT: %d decorative props unavailable; omitted without affecting supports",missing_props);
    if(!art->plaster.id || !art->wood.id || !art->door.meshCount)
        TraceLog(LOG_WARNING,"SWAT: environment art incomplete; missing pieces use graybox rendering");
}

static void close_model(Model model) {
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
    close_model(art->door);
    for(int i=0;i<SWAT_ENV_PROP_KINDS;i++) close_model(art->props[i]);
    memset(art,0,sizeof(*art));
}

static Color wear_color(const SwatObject* o,float shade) {
    float wear=o->max_health>0 ? .5f+.5f*swat_clamp(o->health/o->max_health,0,1) : 1;
    unsigned char value=(unsigned char)(255*wear*shade);
    return (Color){value,value,value,255};
}

static void textured_box(Texture2D texture,const SwatObject* o) {
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
        Color color=wear_color(o,shade[face]);
        rlColor4ub(color.r,color.g,color.b,color.a);
        rlNormal3f(normals[face][0],normals[face][1],normals[face][2]);
        for(int v=0;v<4;v++) {
            float x=corners[face][v][0]*o->half.x;
            float y=corners[face][v][1]*o->half.y;
            float z=corners[face][v][2]*o->half.z;
            float u=face<2 ? oz+z : ox+x;
            float t=(face==2 || face==3) ? oz+z : (float)o->center.y+y;
            rlTexCoord2f(u,-t); rlVertex3f(x,y,z);
        }
    }
    rlEnd(); rlSetTexture(0);
}

bool swat_environment_art_draw(const SwatEnvironmentArt* art,const SwatObject* o) {
    SwatEnvironmentSurface surface=swat_environment_surface(o);
    if(surface==SWAT_ENV_DOOR && art->door.meshCount) {
        DrawModelEx(art->door,(Vector3){0},(Vector3){0,1,0},0,
            (Vector3){2*o->half.x,2*o->half.y,2*o->half.z},wear_color(o,1));
        return true;
    }
    Texture2D texture={0};
    if(surface==SWAT_ENV_PLASTER) texture=art->plaster;
    if(surface==SWAT_ENV_WOOD || surface==SWAT_ENV_DOOR) texture=art->wood;
    if(!texture.id) return false;
    textured_box(texture,o);
    return true;
}

bool swat_environment_art_has_floor(const SwatEnvironmentArt* art,SwatMaterial floor) {
    return floor==SWAT_WOOD && art->wood.id;
}

void swat_environment_art_draw_props(const SwatEnvironmentArt* art,const SwatWorld* world,
                                     const SwatLayout* layout) {
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
