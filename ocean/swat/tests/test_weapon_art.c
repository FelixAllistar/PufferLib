// Explicit real-GPU check using the privately installed export, never a public fixture.
#include "weapon_art.h"
#include "rifle_geometry.h"
#include "character_asset.h"
#include "raymath.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void environment(const char* key,const char* value) {
#ifdef _WIN32
    assert(!_putenv_s(key,value ? value : ""));
#else
    if(value) assert(!setenv(key,value,1)); else assert(!unsetenv(key));
#endif
}
static void empty(const SwatSim* sim,bool cutaway) { (void)sim; (void)cutaway; }
static Image capture(SwatWeaponArt* art,SwatLighting* light,SwatArsenal* arsenal,SwatPose* pose) {
    static SwatWorld world; SwatEnvironmentArt environment_art={0};
    Camera3D camera={{.75f,.35f,-.92f},{.43f,0,0},{0,1,0},.75f,CAMERA_ORTHOGRAPHIC};
    RenderTexture2D target=LoadRenderTexture(1024,512); assert(target.id);
    BeginTextureMode(target); ClearBackground((Color){80,90,100,255}); BeginMode3D(camera);
    swat_lighting_begin(light,&environment_art,&world,camera.position);
    assert(swat_weapon_art_draw(art,light,arsenal,pose));
    swat_lighting_end(light,&environment_art); EndMode3D(); EndTextureMode();
    Image image=LoadImageFromTexture(target.texture); ImageFlipVertical(&image); UnloadRenderTexture(target); return image;
}
static int difference(Image a,Image b) {
    Color* x=LoadImageColors(a),*y=LoadImageColors(b); int different=0;
    for(int i=0;i<a.width*a.height;i++) if(abs(x[i].r-y[i].r)+abs(x[i].g-y[i].g)+abs(x[i].b-y[i].b)>6) different++;
    UnloadImageColors(x); UnloadImageColors(y); return different;
}
int main(int argc,char** argv) {
    assert(argc==3); // private assets directory, capture directory
    environment("SWAT_WEAPON_ART",NULL); environment("SWAT_WEAPON_ASSETS",argv[1]);
    environment("SWAT_LIGHTING",NULL); SetConfigFlags(FLAG_WINDOW_HIDDEN); InitWindow(640,480,"Rigid rifle regression");
    SwatWeaponArt art={0}; swat_weapon_art_init(&art); assert(art.carbine.meshCount==2);
    swat_weapon_art_init(&art); assert(art.carbine.meshCount==2);
    char source[4096]; snprintf(source,sizeof(source),"%s/rifle7_rigid_textured.glb",argv[1]);
    assert(art.normal_scale==swat_art_normal_scale(source));
    assert(swat_art_normal_scale("missing-private-material.glb")==1);
    for(int i=0;i<2;i++) {
        Mesh mesh=art.carbine.meshes[i]; assert(mesh.vertexCount>500 && mesh.tangents);
        Material mat=art.carbine.materials[art.carbine.meshMaterial[i]];
        assert(mat.maps[MATERIAL_MAP_ALBEDO].texture.width==4096);
        assert(mat.maps[MATERIAL_MAP_NORMAL].texture.width==4096);
        assert(mat.maps[MATERIAL_MAP_ROUGHNESS].texture.width==4096);
        assert(mat.maps[MATERIAL_MAP_ALBEDO].texture.mipmaps>1);
        assert(mat.maps[MATERIAL_MAP_ROUGHNESS].value==1);
        assert(mat.maps[MATERIAL_MAP_METALNESS].value==1);
    }
    SwatArsenal arsenal; swat_weapons_init(&arsenal,9); SwatArsenal before=arsenal;
    SwatPose pose={.weapon_forward={1,0,0},.weapon_up={0,1,0},.right={0,0,1}};
    for(int i=0;i<6;i++) {
        float angle=.3f*i; pose.weapon_forward=swat_v(cosf(angle),sinf(angle),0);
        pose.weapon_up=swat_v(-sinf(angle),cosf(angle),0); pose.shoulder=(b3Pos){2,3,4};
        Vector3 local={swat_carbine_muzzle.x,swat_carbine_muzzle.y,swat_carbine_muzzle.z};
        Vector3 visual=Vector3Transform(local,swat_weapon_art_transform(&pose));
        b3Pos canonical=swat_pose_weapon_point(&pose,swat_carbine_muzzle);
        assert(fabsf(visual.x-canonical.x)+fabsf(visual.y-canonical.y)+fabsf(visual.z-canonical.z)<.000001f);
    }
    pose=(SwatPose){.weapon_forward={1,0,0},.weapon_up={0,1,0},.right={0,0,1}};
    static SwatSim sim; SwatLighting light={0}; swat_lighting_init(&light); assert(light.enabled);
    swat_lighting_prepare(&light,&sim,(Vector3){0,1,0},false,empty);
    Image seated=capture(&art,&light,&arsenal,&pose); assert(!memcmp(&arsenal,&before,sizeof(before)));
    float authored_scale=art.normal_scale; art.normal_scale=0;
    Image zero_normal=capture(&art,&light,&arsenal,&pose); art.normal_scale=authored_scale;
    int scaled=difference(seated,zero_normal);assert(scaled>100);
    printf("authored normal scale=%.2f response pixels=%d\n",authored_scale,scaled);
    arsenal.slots[0].magazine_seated=false; Image removed=capture(&art,&light,&arsenal,&pose);
    int changed=difference(seated,removed); printf("removed magazine pixels=%d\n",changed); assert(changed>500);
    arsenal.slots[0].magazine_seated=true;
    Texture normal[16]; assert(art.carbine.materialCount<=16); for(int i=0;i<art.carbine.materialCount;i++) {
        Material* mat=&art.carbine.materials[i];
        normal[i]=mat->maps[MATERIAL_MAP_NORMAL].texture; mat->maps[MATERIAL_MAP_NORMAL].texture=(Texture){0};
    }
    Image flat=capture(&art,&light,&arsenal,&pose); changed=difference(seated,flat);
    assert(difference(flat,zero_normal)<100); UnloadImage(zero_normal);
    printf("normal map response pixels=%d\n",changed); fflush(stdout);
    char diagnostic[4096]; snprintf(diagnostic,sizeof(diagnostic),"%s/rifle-normal-debug.png",argv[2]); ExportImage(seated,diagnostic);
    snprintf(diagnostic,sizeof(diagnostic),"%s/rifle-flat-debug.png",argv[2]); ExportImage(flat,diagnostic); assert(changed>100);
    for(int i=0;i<art.carbine.materialCount;i++) art.carbine.materials[i].maps[MATERIAL_MAP_NORMAL].texture=normal[i];
    MaterialMap* saved=calloc((size_t)art.carbine.materialCount*(MATERIAL_MAP_BRDF+1),sizeof(*saved)); assert(saved);
    for(int i=0;i<art.carbine.materialCount;i++) {
        MaterialMap* maps=art.carbine.materials[i].maps;
        memcpy(saved+i*(MATERIAL_MAP_BRDF+1),maps,(MATERIAL_MAP_BRDF+1)*sizeof(*saved));
        maps[MATERIAL_MAP_ROUGHNESS].texture=(Texture){0};
        maps[MATERIAL_MAP_ROUGHNESS].value=.25f; maps[MATERIAL_MAP_METALNESS].value=1;
    }
    Image scalar_metal=capture(&art,&light,&arsenal,&pose);
    for(int i=0;i<art.carbine.materialCount;i++) art.carbine.materials[i].maps[MATERIAL_MAP_METALNESS].value=0;
    Image scalar_paint=capture(&art,&light,&arsenal,&pose); changed=difference(scalar_metal,scalar_paint);
    printf("scalar PBR without ORM metal/paint response pixels=%d\n",changed); assert(changed>100);
    for(int i=0;i<art.carbine.materialCount;i++) memcpy(art.carbine.materials[i].maps,saved+i*(MATERIAL_MAP_BRDF+1),(MATERIAL_MAP_BRDF+1)*sizeof(*saved));
    free(saved); UnloadImage(scalar_metal); UnloadImage(scalar_paint);
    char path[4096]; snprintf(path,sizeof(path),"%s/rifle-material.png",argv[2]); assert(ExportImage(seated,path));
    snprintf(path,sizeof(path),"%s/rifle-magazine-removed.png",argv[2]); assert(ExportImage(removed,path));
    UnloadImage(seated); UnloadImage(removed); UnloadImage(flat);
    arsenal.active=1; assert(!swat_weapon_art_draw(&art,&light,&arsenal,&pose));
    swat_weapon_art_close(&art); assert(!art.initialized && !art.carbine.meshCount); swat_weapon_art_close(&art);
    environment("SWAT_WEAPON_ART","0"); swat_weapon_art_init(&art); assert(!art.carbine.meshCount); swat_weapon_art_close(&art);
    environment("SWAT_WEAPON_ART",NULL); environment("SWAT_WEAPON_ASSETS","missing-private-rifle-fixture");
    swat_weapon_art_init(&art); assert(!art.carbine.meshCount); swat_weapon_art_close(&art);
    environment("SWAT_WEAPON_ASSETS",NULL); swat_lighting_close(&light); CloseWindow();
    puts("PASS rigid rifle: original maps/tangents, exact pose transform, reload magazine visibility, normal-map response, immutable arsenal, equipment and missing-asset fallback, opt-out, GPU lifecycle"); return 0;
}
