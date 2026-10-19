// Compile the actual candidate implementation so cache/resource assertions also
// cover its private borrowed-material lifecycle. Shipping unit is built too.
#ifndef SWAT_ENV_SOURCE
#define SWAT_ENV_SOURCE "../environment_art.c"
#endif
#include SWAT_ENV_SOURCE
#include "protocol.h"
#include "replay.h"
#include <assert.h>
static SwatSim sim;
static SwatEnvironmentArt art;
static SwatLighting light;
static SwatWorld before;
static void empty_shadow(const SwatSim*s,bool c){(void)s;(void)c;}
static int named(const char*n){for(int i=0;i<50;i++)if(!strcmp(swat_motel_picnic_part(i)->name,n))return SWAT_MOTEL_PICNIC_FIRST+i;assert(false);return-1;}
static void* clone(const void*p,size_t n){if(!p)return NULL;void*q=MemAlloc((unsigned int)n);assert(q);memcpy(q,p,n);return q;}
// Independent reference retains original indexed vertices and filters face IDs.
static Model reference(uint64_t mask){
 Model out=art.picnic_art->source;out.meshes=MemAlloc(8*sizeof(Mesh));out.meshMaterial=MemAlloc(8*sizeof(int));assert(out.meshes&&out.meshMaterial);out.meshCount=0;
 for(int mesh=0;mesh<8;mesh++){
  const Mesh*source=&art.picnic_art->source.meshes[mesh];bool*keep=calloc((size_t)source->triangleCount,1);assert(keep);
  for(int i=0;i<80;i++){const SwatPicnicComponent*p=swat_motel_picnic_component(i);if(p->mesh==mesh&&(mask&(UINT64_C(1)<<p->owner)))for(int t=0;t<p->triangles;t++)keep[p->source_triangles[t]]=true;}
  Mesh m={0};m.vertexCount=source->vertexCount;m.indices=MemAlloc((unsigned int)source->triangleCount*6);assert(m.indices);
  for(int t=0;t<source->triangleCount;t++){if(keep[t]){memcpy(m.indices+3*m.triangleCount,source->indices+3*t,6);m.triangleCount++;}}
  free(keep);
  if(!m.triangleCount){MemFree(m.indices);continue;}
  m.vertices=clone(source->vertices,(size_t)m.vertexCount*12);m.normals=clone(source->normals,(size_t)m.vertexCount*12);m.texcoords=clone(source->texcoords,(size_t)m.vertexCount*8);m.texcoords2=clone(source->texcoords2,(size_t)m.vertexCount*8);m.tangents=clone(source->tangents,(size_t)m.vertexCount*16);m.colors=clone(source->colors,(size_t)m.vertexCount*4);UploadMesh(&m,false);assert(m.vaoId);
  out.meshMaterial[out.meshCount]=art.picnic_art->source.meshMaterial[mesh];out.meshes[out.meshCount++]=m;
 }
 return out;
}
static Image frame(Camera3D camera,bool source,bool shadow,uint64_t submitted,Model*control){
 RenderTexture2D target=LoadRenderTexture(384,384);assert(target.id);before=sim.world;
 BeginTextureMode(target);ClearBackground(MAGENTA);BeginMode3D(camera);swat_lighting_begin(&light,&art,&sim.world,camera.position);
 if(source){SwatMotelInstance placement;int owner=SWAT_MOTEL_PICNIC_FIRST;while(owner<SWAT_MOTEL_OBJECTS&&!(submitted&(UINT64_C(1)<<(owner-SWAT_MOTEL_PICNIC_FIRST))))owner++;assert(owner<SWAT_MOTEL_OBJECTS&&swat_motel_picnic(&sim.world,owner,&placement));assert(location_mesh_draw(&art,&sim.world.objects[owner],&placement,control,&art.picnic_art->source,shadow,false,false,NULL));}
 else for(int i=0;i<50;i++)if(submitted&(UINT64_C(1)<<i))assert(swat_environment_motel_draw(&art,&sim.world,&sim.world.objects[SWAT_MOTEL_PICNIC_FIRST+i],shadow,false));
 swat_lighting_end(&light,&art);EndMode3D();EndTextureMode();Image image=LoadImageFromTexture(target.texture);ImageFlipVertical(&image);UnloadRenderTexture(target);assert(!memcmp(&before,&sim.world,sizeof(before)));return image;
}
static void compare(Image a,Image b,int cycle,int state,int view,bool shadow){
 Color*p=LoadImageColors(a),*q=LoadImageColors(b);int changed=0,filled=0;long sum=0;for(int i=0;i<384*384;i++){int delta=abs(p[i].r-q[i].r)+abs(p[i].g-q[i].g)+abs(p[i].b-q[i].b);changed+=delta>12;sum+=delta;filled+=p[i].r!=255||p[i].g!=0||p[i].b!=255;}printf("RUNTIME_RASTER cycle%d state%d view%d shadow%d changed%d mean%.9f filled%d\n",cycle,state,view,shadow,changed,(double)sum/(384*384*3),filled);assert(changed<32&&filled>100);UnloadImageColors(p);UnloadImageColors(q);
}
static void environment(const char*k,const char*v){
#ifdef _WIN32
 assert(!_putenv_s(k,v));
#else
 assert(!setenv(k,v,1));
#endif
}
int main(int argc,char**argv){
 assert(argc==3);setvbuf(stdout,NULL,_IONBF,0);environment("SWAT_ENVIRONMENT_ASSETS",argv[1]);environment("SWAT_IBL","0");environment("SWAT_CONTACT_SHADOWS","0");environment("SWAT_LIGHTING","1");SetConfigFlags(FLAG_WINDOW_HIDDEN);SetTraceLogLevel(LOG_INFO);InitWindow(384,384,"SWAT actual motel picnic art");assert(IsWindowReady());swat_lighting_init(&light);assert(light.enabled);
 Camera3D cameras[]={{{8.4f,1.82f,10},{6,.32f,7.5f},{0,1,0},2.6f,CAMERA_ORTHOGRAPHIC},{{6,3,7.5f},{6,.32f,7.5f},{0,0,-1},2.4f,CAMERA_ORTHOGRAPHIC},{{3.7f,.72f,10.2f},{6,.32f,7.5f},{0,1,0},2.6f,CAMERA_ORTHOGRAPHIC}};
 for(int cycle=0;cycle<2;cycle++){
  SwatConfig cfg=swat_default_config();cfg.mission=SWAT_MOTEL;cfg.randomize=cfg.hostile_fire=false;swat_sim_init(&sim,cfg,81);swat_environment_art_init(&art);swat_environment_art_prepare_location(&art,&sim.world);assert(art.picnic_art&&art.picnic_art->source.meshCount==8);swat_lighting_prepare(&light,&sim,(Vector3){6,1.7f,10},false,empty_shadow);art.lighting=&light;
  SwatArtTextureStats loaded=swat_art_texture_stats();assert(loaded.textures>0);MaterialMap maps[3][MATERIAL_MAP_BRDF+1];for(int i=0;i<3;i++)memcpy(maps[i],art.picnic_art->source.materials[i].maps,sizeof(maps[i]));
  for(int state=0;state<3;state++){
   if(state==1)assert(swat_world_damage(&sim.world,named("Tabletop_Plank_01"),100000));
   if(state==2){assert(swat_world_damage(&sim.world,named("Frame_Left_Leg_Front"),100000));assert(!sim.world.objects[named("Frame_Left_Leg_Rear")].active);}
   uint64_t mask=swat_motel_picnic_mask(&sim.world);Model control=reference(mask);
   for(int view=0;view<3;view++)for(int shadow=0;shadow<2;shadow++){
    Image a=frame(cameras[view],false,shadow,mask,&control),b=frame(cameras[view],true,shadow,mask,&control);compare(a,b,cycle,state,view,shadow);if(!cycle)assert(ExportImage(a,TextFormat("%s/state-%d-view-%d-shadow-%d.png",argv[2],state,view,shadow)));UnloadImage(a);UnloadImage(b);
   }
   for(int i=0;i<control.meshCount;i++){UnloadMesh(control.meshes[i]);}
   MemFree(control.meshes);MemFree(control.meshMaterial);
   if(state){int count=0;for(int i=0;i<8;i++)count+=!!art.picnic_art->meshes[i].model.meshCount;assert(count>0&&count<=8&&art.picnic_art->filtered&&art.picnic_art->mask==mask);printf("CACHE state%d filtered_meshes%d source_meshes%d\n",state,count,art.picnic_art->source.meshCount);}
  }
  // Submit a surviving tabletop alone, as a shadow/contact culler may do.
  swat_sim_reset(&sim);uint64_t submitted=UINT64_C(1)<<(named("Tabletop_Plank_03")-SWAT_MOTEL_PICNIC_FIRST);Model single=reference(submitted);Image a=frame(cameras[0],false,true,submitted,&single),b=frame(cameras[0],true,true,submitted,&single);compare(a,b,cycle,3,0,true);if(!cycle)assert(ExportImage(a,TextFormat("%s/culled-leader-shadow.png",argv[2])));UnloadImage(a);UnloadImage(b);for(int i=0;i<single.meshCount;i++)UnloadMesh(single.meshes[i]);MemFree(single.meshes);MemFree(single.meshMaterial);
  for(int i=0;i<3;i++){assert(!memcmp(maps[i],art.picnic_art->source.materials[i].maps,sizeof(maps[i])));}
  SwatArtTextureStats after=swat_art_texture_stats();assert(!memcmp(&loaded,&after,sizeof(loaded)));swat_environment_art_close(&art);assert(!art.picnic_art&&swat_art_texture_stats().textures==0);swat_sim_close(&sim);printf("PASS actual runtime cycle%d: exact original maps,<=8 visible material submissions,owner-specific culled shadows and all borrowed resources released\n",cycle);
 }
 swat_lighting_close(&light);CloseWindow();puts("PASS runtime picnic:38 actual renderer/indexed-control pairs; intact,board damage,leg cascade and independently culled shadow owners; no performance acceptance");return 0;
}
