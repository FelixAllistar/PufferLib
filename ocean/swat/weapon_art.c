#include "weapon_art.h"
#include "rlgl.h"
#include "rifle_geometry.h"
#include "character_asset.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void swat_weapon_art_init(SwatWeaponArt* art) {
    if(art->initialized) return;
    art->initialized=true;
    art->normal_scale=1;
    const char* enabled=getenv("SWAT_WEAPON_ART"); if(enabled && !strcmp(enabled,"0")) return;
    const char* custom=getenv("SWAT_WEAPON_ASSETS"); char path[4096];
    if(custom && custom[0]) snprintf(path,sizeof(path),"%s/rifle7_rigid_textured.glb",custom);
    else {
        snprintf(path,sizeof(path),"%sassets/weapons/rifle7_rigid_textured.glb",GetApplicationDirectory());
        if(!FileExists(path)) snprintf(path,sizeof(path),"build/swat/assets/weapons/rifle7_rigid_textured.glb");
    }
    if(!FileExists(path)) { TraceLog(LOG_INFO,"SWAT: private rigid rifle absent; canonical procedural fallback"); return; }
    art->carbine=LoadModel(path); Model m=art->carbine;
    bool valid=m.meshCount==2 && m.boneCount==0 && m.meshMaterial;
    if(valid) {
        BoundingBox box=GetModelBoundingBox(m);
        valid=box.min.x>=-.01f && box.max.x>=.899f && box.max.x<=.901f &&
            box.min.y>=-.18f && box.max.y<=.14f && box.min.z>=-.04f && box.max.z<=.04f;
        for(int i=0;i<m.meshCount;i++) valid=valid && m.meshes[i].vertices && m.meshes[i].normals &&
            m.meshes[i].texcoords && m.meshes[i].tangents && m.meshMaterial[i]>=0 && m.meshMaterial[i]<m.materialCount;
    }
    if(!valid) { swat_art_model_close(m); art->carbine=(Model){0}; TraceLog(LOG_WARNING,"SWAT: rigid rifle violates measured interface; using fallback"); return; }
    art->normal_scale=swat_art_normal_scale(path);
    // Preserve 4K source maps, with mipmaps rather than shimmer at distance.
    unsigned int previous[32]; int count=0;
    for(int i=0;i<m.materialCount;i++) for(int k=0;k<=MATERIAL_MAP_BRDF;k++) {
        Texture2D* t=&m.materials[i].maps[k].texture;
        if(!t->id || t->id==rlGetTextureIdDefault()) continue;
        bool seen=false; for(int j=0;j<count;j++) if(previous[j]==t->id) seen=true;
        if(!seen && count<32) {
            previous[count++]=t->id; GenTextureMipmaps(t); SetTextureFilter(*t,TEXTURE_FILTER_TRILINEAR);
        }
    }
    TraceLog(LOG_INFO,"SWAT: private rigid rifle loaded, 0.90m reach, authored maps/normal scale %.2f, separate seated magazine",art->normal_scale);
}
void swat_weapon_art_close(SwatWeaponArt* art) { swat_art_model_close(art->carbine); memset(art,0,sizeof(*art)); }
Matrix swat_weapon_art_transform(const SwatPose* p) {
    Matrix m={0}; m.m0=p->weapon_forward.x; m.m1=p->weapon_forward.y; m.m2=p->weapon_forward.z;
    m.m4=p->weapon_up.x; m.m5=p->weapon_up.y; m.m6=p->weapon_up.z;
    m.m8=p->right.x; m.m9=p->right.y; m.m10=p->right.z;
    m.m12=(float)p->shoulder.x; m.m13=(float)p->shoulder.y; m.m14=(float)p->shoulder.z; m.m15=1; return m;
}
SwatPose swat_weapon_view_pose(const SwatPose* achieved,const SwatArsenal* arsenal,
    float ads,float horizontal,float vertical,float eye_relief) {
    SwatPose p=*achieved;
    b3Vec3 offset=swat_mul(b3Add(swat_mul(p.right,horizontal),swat_mul(p.up,vertical)),1-ads);
    if(!arsenal->active && !arsenal->primary) {
        // The measured rear aperture was 220 mm in front of the eye. That
        // distant eye point makes its 5.4 mm opening almost unreadable. Use
        // adjustable eye relief for the first-person carbine only. Computing
        // from the achieved rear point keeps this stable through lean/recoil.
        b3Pos rear=swat_pose_weapon_point(&p,swat_carbine_rear_sight);
        float distance=b3Dot(b3SubPos(rear,p.eye),p.forward);
        offset=b3Add(offset,swat_mul(p.forward,ads*(eye_relief-distance)));
    }
    p.shoulder=b3OffsetPos(p.shoulder,offset); p.muzzle=b3OffsetPos(p.muzzle,offset);
    p.sight=b3OffsetPos(p.sight,offset); p.left_hand=b3OffsetPos(p.left_hand,offset);
    p.right_hand=b3OffsetPos(p.right_hand,offset);
    return p;
}
bool swat_weapon_art_draw(SwatWeaponArt* art,SwatLighting* light,const SwatArsenal* a,const SwatPose* pose) {
    if(!art->carbine.meshCount || a->active || a->primary) return false;
    Shader shader=light->enabled && light->prepared ? light->mesh.shader :
        (Shader){rlGetShaderIdDefault(),rlGetShaderLocsDefault()};
    Matrix transform=swat_weapon_art_transform(pose);
    rlDrawRenderBatchActive(); rlDisableBackfaceCulling();
    for(int i=0;i<art->carbine.meshCount;i++) {
        if(i==1 && !a->slots[0].magazine_seated) continue;
        Material material=art->carbine.materials[art->carbine.meshMaterial[i]];
        material.shader=shader; swat_lighting_material_scaled(light,material,true,art->normal_scale);
        DrawMesh(art->carbine.meshes[i],material,transform);
    }
    swat_lighting_material(light,(Material){0},false);
    rlEnableBackfaceCulling(); return true;
}
