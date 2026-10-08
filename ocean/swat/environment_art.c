#include "environment_art.h"
#include "lighting.h"
#include "character_asset.h"
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

static void room101_close(SwatEnvironmentArt* art) {
    swat_art_model_close(art->room101_desk);art->room101_desk=(Model){0};
    for(int i=0;i<SWAT_MOTEL_DRESSING_ASSETS;i++) {swat_art_model_close(art->motel_dressing[i]);art->motel_dressing[i]=(Model){0};}
    for(int i=0;i<SWAT_ROOM101_ASSETS;i++) { swat_art_model_close(art->room101[i]); art->room101[i]=(Model){0}; }
    art->room101_ready=false;
    for(int i=0;i<SWAT_ROOM101_V3_ASSETS;i++) {swat_art_model_close(art->room101_v3[i]);art->room101_v3[i]=(Model){0};}
    art->room101_v3_ready=false;
    for(int i=0;i<SWAT_ROOM101_V4_ASSETS;i++) {swat_art_model_close(art->room101_v4[i]);art->room101_v4[i]=(Model){0};}
    art->room101_v4_ready=false;
}
static bool room101_model_load(Model* model,float* scales,const char* file) {
    char path[4096];if(!asset_path(path,sizeof(path),file))return false;
    *model=LoadModel(path);
    SwatArtMaterialFactors factors[SWAT_ROOM101_MATERIALS-1];
    int count=swat_art_material_factors(path,factors,SWAT_ROOM101_MATERIALS-1);
    if(!model->meshCount || count<1 || model->materialCount!=count+1)return false;
    for(int m=0;m<count;m++) {
        Material* material=&model->materials[m+1];const SwatArtMaterialFactors* f=&factors[m];
        material->maps[MATERIAL_MAP_ROUGHNESS].value=f->roughness;
        material->maps[MATERIAL_MAP_METALNESS].value=f->metalness;
        material->maps[MATERIAL_MAP_OCCLUSION].value=f->occlusion_strength;
        material->maps[MATERIAL_MAP_NORMAL].value=2;scales[m+1]=f->normal_scale;
        material->maps[MATERIAL_MAP_ALBEDO].color=(Color){
            (unsigned char)lroundf(255*powf(swat_clamp(f->base_color[0],0,1),1/2.2f)),
            (unsigned char)lroundf(255*powf(swat_clamp(f->base_color[1],0,1),1/2.2f)),
            (unsigned char)lroundf(255*powf(swat_clamp(f->base_color[2],0,1),1/2.2f)),
            (unsigned char)lroundf(255*swat_clamp(f->base_color[3],0,1))};
        for(int k=0;k<=MATERIAL_MAP_BRDF;k++) {
            Texture2D* t=&material->maps[k].texture;
            if(t->id && t->id!=rlGetTextureIdDefault()) {GenTextureMipmaps(t);SetTextureFilter(*t,TEXTURE_FILTER_TRILINEAR);SetTextureWrap(*t,TEXTURE_WRAP_REPEAT);}
        }
    }
    return true;
}
static void room101_load(SwatEnvironmentArt* art) {
    const char* enabled=getenv("SWAT_MOTEL_ROOM101"); if(enabled && !strcmp(enabled,"0")) return;
    static const char* files[SWAT_ROOM101_ASSETS]={
        "room_facade_4m_candidate.glb","room_door_leaf_candidate.glb","gallery_walkway_4m_candidate.glb",
        "room_number_plaque_candidate.glb","ribbed_glass_wall_sconce_candidate.glb",
        "facade_optional_detail_overlay.glb","door_optional_detail_overlay.glb","plaque_optional_detail_overlay.glb"};
    for(int i=0;i<SWAT_ROOM101_ASSETS;i++) {
        char file[256];snprintf(file,sizeof(file),"motel_room101_v2/%s",files[i]);
        if(!room101_model_load(&art->room101[i],art->room101_normal_scale[i],file)) {room101_close(art);return;}
    }
    art->room101_ready=true;
    if(enabled && !strcmp(enabled,"2"))return;
    static const char* v3[SWAT_ROOM101_V3_ASSETS]={
        "room_window_insert_016_v3.glb","through_wall_ac_017_v3.glb","room_number_plaque_018_v3.glb",
        "room_floor_4x6m_008_v3.glb","end_wall_6m_104_v3.glb","end_wall_6m_105_v3.glb",
        "bathroom_partition_4m_019_v3.glb","flat_roof_4x6m_009_v3.glb","mattress_dirty_022_v3.glb",
        "folded_bedsheet_stack_027_v3.glb","facade_optional_detail_overlay_v3.glb","door_optional_detail_overlay_v3.glb"};
    for(int i=0;i<SWAT_ROOM101_V3_ASSETS;i++) {
        char file[256];snprintf(file,sizeof(file),"motel_room101_v3/%s",v3[i]);
        if(!room101_model_load(&art->room101_v3[i],art->room101_v3_normal_scale[i],file)) {
            for(int j=0;j<SWAT_ROOM101_V3_ASSETS;j++) {swat_art_model_close(art->room101_v3[j]);art->room101_v3[j]=(Model){0};}
            return; // Atomic v3 fallback retains the entire v2 set.
        }
    }
    art->room101_v3_ready=true;
    if(enabled && !strcmp(enabled,"3"))return;
    static const char* v4[SWAT_ROOM101_V4_ASSETS]={
        "mattress_dirty_022_v4.glb","folded_bedsheet_stack_027_v4.glb","bedframe_single_institutional_021_v4.glb",
        "basin_pedestal_028_v4.glb","solid_wall_4m_013_v4.glb","bathroom_partition_4m_019_v4.glb",
        "room_floor_4x6m_008_v4.glb","room_window_insert_016_v4.glb"};
    for(int i=0;i<SWAT_ROOM101_V4_ASSETS;i++) {
        char file[256];snprintf(file,sizeof(file),"motel_room101_v4/%s",v4[i]);
        if(!room101_model_load(&art->room101_v4[i],art->room101_v4_normal_scale[i],file)) {
            for(int j=0;j<SWAT_ROOM101_V4_ASSETS;j++) {swat_art_model_close(art->room101_v4[j]);art->room101_v4[j]=(Model){0};}
            return; // Atomic v4 fallback retains the complete v3 room.
        }
    }
    art->room101_v4_ready=true;
    // Only Room 101's desktop material/UVs change. The original mesh bank and
    // physical desk remain the fallback and authority for this instance.
    if(!room101_model_load(&art->room101_desk,art->room101_desk_normal_scale,
            "motel_desk_w1/desk_023_oak_veneer01_w1.glb")) {
        swat_art_model_close(art->room101_desk);art->room101_desk=(Model){0};
    }
}

static void motel_dressing_load(SwatEnvironmentArt* art) {
    static const char* files[SWAT_MOTEL_DRESSING_ASSETS]={
        "mb01_wall_toilet_roll_holder_lod0.glb","mb01_double_robe_hook_lod0.glb",
        "mg01_lidded_ice_bucket_lod0.glb","mg01_hospitality_service_tray_lod0.glb",
        "mw01_framed_woodland_print_lod0.glb","me01_door_viewer_face_lod0.glb",
        "me01_concave_wall_bumper_lod0.glb"};
    for(int i=0;i<SWAT_MOTEL_DRESSING_ASSETS;i++) {
        char file[256],path[4096];snprintf(file,sizeof(file),"motel_dressing_v1/%s",files[i]);
        Model* model=&art->motel_dressing[i];
        if(!room101_model_load(model,art->motel_dressing_normal_scale[i],file)) {
            swat_art_model_close(*model);*model=(Model){0};continue;
        }
        SwatArtMaterialFactors factors[SWAT_ROOM101_MATERIALS-1];
        if(!asset_path(path,sizeof(path),file))continue;
        int count=swat_art_material_factors(path,factors,SWAT_ROOM101_MATERIALS-1);
        for(int m=0;m<count;m++) {
            art->motel_dressing_occlusion_uv[i][m+1]=factors[m].occlusion_texcoord;
            // Separate AO atlas has no tiling, unlike the UV0 surface maps.
            Texture2D ao=model->materials[m+1].maps[MATERIAL_MAP_OCCLUSION].texture;
            if(ao.id)SetTextureWrap(ao,TEXTURE_WRAP_CLAMP);
        }
    }
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
    room101_close(art);
    for(int i=0;i<SWAT_MOTEL_ASSETS;i++) { swat_art_model_close(art->motel[i]); art->motel[i]=(Model){0}; }
    for(int i=0;i<SWAT_STOREFRONT_ASSETS;i++) { swat_art_model_close(art->storefront[i]); art->storefront[i]=(Model){0}; }
    art->location=selected;
    if(!selected) return;
    char path[4096];
    int location=selected-1;
    int missing=0;
    for(int i=0;i<(location ? SWAT_STOREFRONT_ASSETS : SWAT_MOTEL_ASSETS);i++) {
        const SwatMotelAsset* a=location ? swat_storefront_asset(i) : swat_motel_asset(i);
        char file[256]; snprintf(file,sizeof(file),"%s/%s",location ? "storefront_v1" : i<SWAT_MOTEL_BASE_ASSETS?"motel_v1":"motel_utility_v1",a->file);
        Model* model=location ? &art->storefront[i] : &art->motel[i];
        if(asset_path(path,sizeof(path),file)) *model=LoadModel(path);
        if(!model->meshCount || model->materialCount!=a->material_count+1) { swat_art_model_close(*model); *model=(Model){0}; missing++; continue; }
        SwatArtMaterialFactors factors[SWAT_ROOM101_MATERIALS-1];
        int factors_count=swat_art_material_factors(path,factors,SWAT_ROOM101_MATERIALS-1);
        // Raylib 5.5 reads scalar glTF PBR factors only when an ORM texture
        // exists. Restore the catalogued original factors for scalar materials.
        for(int m=0;m<a->material_count;m++) {
            Material* material=&model->materials[m+1];
            material->maps[MATERIAL_MAP_ROUGHNESS].value=a->materials[m].roughness;
            material->maps[MATERIAL_MAP_METALNESS].value=a->materials[m].metalness;
            material->maps[MATERIAL_MAP_NORMAL].value=2; // Signed UV derivatives.
            material->maps[MATERIAL_MAP_OCCLUSION].value=m<factors_count?factors[m].occlusion_strength:1;
            if(!location)art->motel_occlusion_uv[i][m+1]=m<factors_count?factors[m].occlusion_texcoord:0;
            for(int k=0;k<=MATERIAL_MAP_BRDF;k++) {
                Texture2D* t=&material->maps[k].texture;
                if(t->id && t->id!=rlGetTextureIdDefault()) { GenTextureMipmaps(t); SetTextureFilter(*t,TEXTURE_FILTER_TRILINEAR); }
            }
        }
    }
    if(missing) TraceLog(LOG_WARNING,"SWAT: %d %s modules absent; matching colliders use graybox rendering",missing,location ? "storefront" : "motel");
    if(selected==1) {room101_load(art);motel_dressing_load(art);}
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
    room101_close(art);
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

static void fragment_mesh(Texture2D texture,const SwatObject* o,bool lit,Vector2 tile,bool metric) {
    b3Vec3 vertices[8];
    for(int i=0;i<8;i++) vertices[i]=swat_v(i<4 ? o->half.x : -o->half.x,o->corners[i%4][0],o->corners[i%4][1]);
    const int faces[6][4]={{0,1,2,3},{7,6,5,4},{0,4,5,1},{1,5,6,2},{2,6,7,3},{3,7,4,0}};
    float oz=sinf(o->yaw)*(float)o->center.x+cosf(o->yaw)*(float)o->center.z;
    rlSetTexture(texture.id); rlBegin(RL_QUADS);
    for(int f=0;f<6;f++) {
        b3Vec3 normal=swat_normalize(b3Cross(b3Sub(vertices[faces[f][1]],vertices[faces[f][0]]),b3Sub(vertices[faces[f][2]],vertices[faces[f][0]])));
        Color color=f<2 ? wear_color(o,lit ? 1 : .9f) : (Color){178,169,148,255};
        rlColor4ub(color.r,color.g,color.b,255); rlNormal3f(normal.x,normal.y,normal.z);
        for(int j=0;j<4;j++) {
            b3Vec3 v=vertices[faces[f][j]];
            rlTexCoord2f((oz+v.z)/tile.x,(metric ? 1 : -1)*((float)o->center.y+v.y)/tile.y);
            rlVertex3f(v.x,v.y,v.z);
        }
    }
    rlEnd(); rlSetTexture(0);
}
void swat_environment_fragment_draw(const SwatObject* o) {
    fragment_mesh((Texture2D){rlGetTextureIdDefault(),1,1,1,PIXELFORMAT_UNCOMPRESSED_R8G8B8A8},o,true,(Vector2){1,1},true);
}
static void textured_box(Texture2D texture,const SwatObject* o,bool lit,Vector2 tile,int grain,bool metric) {
    if(o->fractured) { fragment_mesh(texture,o,lit,tile,metric); return; }
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
    if(!o->active) return false;
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
        else if(o->half.y<.11f && o->half.x>.5f && o->half.z>.5f) kind=SWAT_SURFACE_FLOOR;
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
    if(!texture.id) { if(o->fractured) { swat_environment_fragment_draw(o); return true; } return false; }
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
        if(p->door && art->motel_dressing[5].meshCount) {
            BoundingBox box=GetMeshBoundingBox(model->meshes[i]);
            if(box.min.x>.50f && box.max.x<.58f && box.min.y>1.58f && box.max.y<1.62f)continue;
        }
        Material material=model->materials[model->meshMaterial[i]];
        bool blend=material.maps[MATERIAL_MAP_ALBEDO].color.a<255;
        if(blend!=transparent || (shadow && blend)) continue;
        // Opaque depth passes need geometry only. Binding every full-resolution
        // material map here wastes driver work for both shadows and contact AO.
        MaterialMap depth_maps[MATERIAL_MAP_BRDF+1]={0};
        if(shadow) {
            depth_maps[MATERIAL_MAP_ALBEDO].texture.id=rlGetTextureIdDefault();
            depth_maps[MATERIAL_MAP_ALBEDO].color=WHITE;
            material.maps=depth_maps;
        }
        material.shader=lit?art->lighting->mesh.shader:(Shader){rlGetShaderIdDefault(),rlGetShaderLocsDefault()};
        if(lit) {
            float normal_scale=1;
            if(model==&art->room101_desk)normal_scale=art->room101_desk_normal_scale[model->meshMaterial[i]];
            for(int r=0;r<SWAT_ROOM101_ASSETS;r++) if(model==&art->room101[r]) normal_scale=art->room101_normal_scale[r][model->meshMaterial[i]];
            for(int r=0;r<SWAT_ROOM101_V3_ASSETS;r++) if(model==&art->room101_v3[r]) normal_scale=art->room101_v3_normal_scale[r][model->meshMaterial[i]];
            for(int r=0;r<SWAT_ROOM101_V4_ASSETS;r++) if(model==&art->room101_v4[r]) normal_scale=art->room101_v4_normal_scale[r][model->meshMaterial[i]];
            int ao_uv=0;
            for(int r=0;r<SWAT_MOTEL_ASSETS;r++)if(model==&art->motel[r])ao_uv=art->motel_occlusion_uv[r][model->meshMaterial[i]];
            for(int r=0;r<SWAT_MOTEL_DRESSING_ASSETS;r++)if(model==&art->motel_dressing[r]) {
                ao_uv=art->motel_dressing_occlusion_uv[r][model->meshMaterial[i]];
                normal_scale=art->motel_dressing_normal_scale[r][model->meshMaterial[i]];
            }
            swat_lighting_material_uv(art->lighting,material,true,normal_scale,ao_uv);
        }
        DrawMesh(model->meshes[i],material,transform);
    }
    if(lit) swat_lighting_material(art->lighting,(Material){0},false);
    return true;
}
bool swat_environment_motel_draw(const SwatEnvironmentArt* art,const SwatWorld* world,const SwatObject* o,bool shadow,bool cutaway) {
    if(!world->motel || o->tag.index<1)return false;
    if(o->active)for(int i=0;i<SWAT_MOTEL_DRESSING_INSTANCES;i++) {
        int parent=swat_motel_dressing_parent(i);
        if(parent!=o->tag.index && (o->tag.index<=SWAT_MOTEL_INSTANCES ||
            i%SWAT_MOTEL_DRESSING_ASSETS!=6 || o->part!=SWAT_PART_SKIN ||
            o->wall_group!=world->objects[parent].wall_group))continue;
        SwatMotelInstance mount;
        if(swat_motel_dressing(world,i,&mount)==o->tag.index)
            location_draw(art,o,&mount,&art->motel_dressing[mount.asset],shadow,cutaway,false);
    }
    if(o->tag.index>SWAT_MOTEL_INSTANCES)return false;
    const SwatMotelInstance* p=swat_motel_instance(o->tag.index-1);
    if(o->tag.index==24 && art->room101_v4_ready && art->room101_desk.meshCount)
        return location_draw(art,o,p,&art->room101_desk,shadow,cutaway,false);
    if(art->room101_ready) {
        if(art->room101_v4_ready) {
            static const int tags[SWAT_ROOM101_V4_ASSETS]={23,28,22,29,14,20,9,17};
            for(int r=0;r<SWAT_ROOM101_V4_ASSETS;r++)if(o->tag.index==tags[r])return location_draw(art,o,p,&art->room101_v4[r],shadow,cutaway,false);
        }
        if(art->room101_v3_ready) {
            static const int tags[10]={17,18,19,9,105,106,20,10,23,28};
            for(int r=0;r<10;r++)if(o->tag.index==tags[r])return location_draw(art,o,p,&art->room101_v3[r],shadow,cutaway,false);
        }
        // Source assembly indices; replace only these five Room 101 instances.
        static const int instances[5]={12,15,10,18,20},overlays[5]={5,6,-1,7,-1};
        for(int r=0;r<5;r++) if(o->tag.index==instances[r]+1) {
            bool drawn=location_draw(art,o,p,&art->room101[r],shadow,cutaway,false);
            if(overlays[r]>=0) {
                const Model* overlay=art->room101_v3_ready && r<2?&art->room101_v3[10+r]:&art->room101[overlays[r]];
                location_draw(art,o,p,overlay,shadow,cutaway,false);
            }
            return drawn;
        }
    }
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
