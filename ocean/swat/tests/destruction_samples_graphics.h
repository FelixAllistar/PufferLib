// Same material samples as the playable lab, through production light casters.
#include "fracture_geometry.h"
static uint32_t sample_geometry_hash(void) {
    uint32_t hash=2166136261u;
    for(int i=0;i<sim.world.count;i++) {
        const SwatFracture* f=sim.world.objects[i].cuts;if(!f)continue;
        const unsigned char* bytes=(const unsigned char*)f->sections;
        for(size_t j=0;j<(size_t)f->section_count*sizeof(*f->sections);j++){hash^=bytes[j];hash*=16777619u;}
    }
    return hash;
}
static Image sample_capture(SwatView* view,Camera3D camera) {
    static SwatSnapshot a,b;swat_capture_snapshot(&sim,1,&a);before=sim.world;uint32_t hash=sample_geometry_hash();
    view->lighting.prepared=false;swat_view_prepare_shadows(view,&sim,camera.position,false);
    Image image=room101_capture_prepared(view,camera,true,1200,720);
    swat_capture_snapshot(&sim,1,&b);
    assert(!memcmp(&a,&b,sizeof(a)) && !memcmp(&before,&sim.world,sizeof(before)) && hash==sample_geometry_hash());
    return image;
}
static void sample_export(Image image,const char* directory,const char* name) {
    char path[4096];snprintf(path,sizeof(path),"%s/%s.png",directory,name);assert(ExportImage(image,path));
}
static void door_damage_graphics(SwatView* view,const char* directory) {
    static SwatMap map;static SwatSnapshot state;
    SwatConfig cfg=swat_default_config();cfg.mission=SWAT_MOTEL;cfg.randomize=false;cfg.hostile_fire=false;
    swat_sim_init(&sim,cfg,81);swat_capture_map(&sim,1,&map);swat_environment_art_prepare_location(&view->environment,&sim.world);
    SwatObject* door=&sim.world.objects[16];assert(door->door && door->active);
    const Model* source=view->environment.room101_ready?&view->environment.room101[1]:&view->environment.motel[swat_motel_instance(15)->asset];
    BoundingBox bounds=GetModelBoundingBox(*source);
    printf("Door source: %d original primitives, depth %.4f..%.4f m\n",source->meshCount,bounds.min.z,bounds.max.z);
    Vector3 normal={cosf(door->yaw),0,-sinf(door->yaw)},tangent={sinf(door->yaw),0,cosf(door->yaw)};
    Vector3 center={door->center.x,door->center.y,door->center.z};
    Camera3D camera={Vector3Add(center,Vector3Add(Vector3Scale(normal,-2.5f),Vector3Scale(tangent,.18f))),
        center,{0,1,0},46,CAMERA_PERSPECTIVE};camera.position.y=1.48f;camera.target.y=1.15f;
    Image intact=sample_capture(view,camera);sample_export(intact,directory,"door-before");
    for(int shot=0;shot<3;shot++) {
        b3Pos p=b3OffsetPos(door->center,b3RotateVector(swat_rotation(door->yaw,door->pitch),swat_v(-door->half.x,-.35f+.2f*shot,-.1f)));
        assert(swat_world_impact_at(&sim.world,16,p,34,false));
    }
    b3Pos mount=b3OffsetPos(door->center,b3RotateVector(swat_rotation(door->yaw,door->pitch),swat_v(-door->half.x,.3f,-.2f)));
    assert(swat_world_blast(&sim.world,16,mount)>0);
    Image broken=sample_capture(view,camera);sample_export(broken,directory,"door-after");
    int pixels=impact_pixel_difference(intact,broken);assert(pixels>1000 && door->active && door->door_angle==0);
    swat_capture_snapshot(&sim,1,&state);swat_sim_close(&sim);swat_apply_map(&sim,&map);assert(swat_apply_snapshot(&sim,&state));
    Image replica=sample_capture(view,camera);assert(!impact_pixel_difference(broken,replica));UnloadImage(replica);
    door=&sim.world.objects[16];int count=sim.world.count;
    unsigned int builds=swat_environment_door_stats(&view->environment).builds;
    for(int i=0;i<90;i++) {
        swat_world_step_doors(&sim.world);
        if(i%10==0){Image motion=sample_capture(view,camera);UnloadImage(motion);}
    }
    assert(swat_environment_door_stats(&view->environment).builds==builds);
    assert(door->active && door->door_angle>1.4f && sim.world.count==count);
    Image open=sample_capture(view,camera);sample_export(open,directory,"door-open");UnloadImage(open);
    UnloadImage(intact);UnloadImage(broken);swat_sim_close(&sim);
    printf("PASS door damage graphics: %d changed pixels, original leaf/maps retained, local notch/scorch, physical hinge motion without mesh rebuild, pixel-identical replica and immutable authority\n",pixels);
}
static void destruction_samples_graphics(SwatView* view,const char* directory) {
    SwatConfig cfg=swat_default_config();cfg.mission=SWAT_BREACH_LAB;cfg.randomize=false;cfg.hostile_fire=false;
    swat_config_human(&cfg);swat_sim_init(&sim,cfg,81);swat_environment_art_prepare_location(&view->environment,&sim.world);
    const float z[]={-4.2f,0,4.2f};const char* names[]={"lab-door","lab-studs","lab-masonry"};
    int affected[3]={0};
    for(int sample=0;sample<3;sample++) {
        Camera3D camera={{.6f,1.65f,z[sample]+.8f},{4,1.3f,z[sample]},{0,1,0},50,CAMERA_PERSPECTIVE};
        Image intact=sample_capture(view,camera);char name[128];snprintf(name,sizeof(name),"%s-before",names[sample]);sample_export(intact,directory,name);
        SwatHit hit=swat_world_ray(&sim.world,(b3Pos){2.9f,1.12f,z[sample]},swat_v(1,0,0),1.5f,b3_nullBodyId);assert(hit.hit);
        if(!sample)assert(sim.world.objects[hit.index].door);
        affected[sample]=swat_world_blast(&sim.world,hit.index,hit.point);assert(affected[sample]>0);
        Image broken=sample_capture(view,camera);snprintf(name,sizeof(name),"%s-after",names[sample]);sample_export(broken,directory,name);
        assert(impact_pixel_difference(intact,broken)>1000);UnloadImage(intact);UnloadImage(broken);
        if(sample==1) {
            Image unlit=room101_capture_size(view,camera,false,1200,720);sample_export(unlit,directory,"lab-studs-unlit");
            // Render the original faces as a reference for unchanged UVs and
            // colour outside the opening. This is a temporary graphics-only
            // comparison; restore the exact cut pointers before simulation.
            SwatFracture* cuts[SWAT_MAX_OBJECTS];
            for(int i=0;i<sim.world.count;i++){cuts[i]=sim.world.objects[i].cuts;sim.world.objects[i].cuts=NULL;}
            Image reference=room101_capture_size(view,camera,false,1200,720);sample_export(reference,directory,"lab-studs-surfaces-reference");
            for(int i=0;i<sim.world.count;i++)sim.world.objects[i].cuts=cuts[i];
            int checked=0,mismatches=0;SwatBreachDetail details[SWAT_BREACH_DETAILS];
            int detail_count=swat_breach_details(&sim.world,(b3Pos){camera.position.x,camera.position.y,camera.position.z},details);
            for(int y=6;y<714;y+=6)for(int x=6;x<1194;x+=6) {
                Ray ray=GetScreenToWorldRayEx((Vector2){x+.5f,y+.5f},camera,1200,720);
                SwatHit hit=swat_world_ray(&sim.world,(b3Pos){ray.position.x,ray.position.y,ray.position.z},
                    swat_v(ray.direction.x,ray.direction.y,ray.direction.z),10,b3_nullBodyId);
                if(hit.kind!=SWAT_HIT_WORLD || hit.index<0)continue;
                const SwatObject* o=&sim.world.objects[hit.index];
                if(!o->cuts || o->material!=SWAT_DRYWALL || fabsf(hit.normal.x)<.999f)continue;
                b3Vec3 p=b3SubPos(hit.point,o->center);
                if(fabsf(p.y)>o->half.y-.03f || fabsf(p.z)>o->half.z-.03f)continue;
                bool near=false;
                for(int c=0;c<o->cuts->count;c++) {
                    SwatCutPlane planes[SWAT_CUT_SIDES];swat_cut_planes(&o->cuts->cuts[c],planes);
                    float outside=-1e9f;
                    for(int e=0;e<swat_cut_plane_count(&o->cuts->cuts[c]);e++)outside=fmaxf(outside,planes[e].y*p.y+planes[e].z*p.z-planes[e].offset);
                    near|=outside<.03f;
                }
                if(near)continue;
                // Original face colours remain exact outside the small new
                // attached pieces, which intentionally cover their seated roots.
                for(int d=0;d<detail_count;d++)near|=b3Distance(hit.point,details[d].position)<.10f;
                if(near)continue;
                Color a=GetImageColor(unlit,x,y),b=GetImageColor(reference,x,y);
                mismatches+=abs(a.r-b.r)>1 || abs(a.g-b.g)>1 || abs(a.b-b.b)>1;checked++;
            }
            printf("Retained painted faces: %d samples, %d colour/UV mismatches\n",checked,mismatches);
            assert(checked>1000 && !mismatches);UnloadImage(unlit);UnloadImage(reference);
        }
        if(sample) {
            Camera3D edge={{2.95f,1.48f,z[sample]},{4,1.15f,z[sample]+.93f},{0,1,0},50,CAMERA_PERSPECTIVE};
            Image close=sample_capture(view,edge);snprintf(name,sizeof(name),"%s-edge",names[sample]);sample_export(close,directory,name);UnloadImage(close);
        }
    }
    swat_sim_close(&sim);
    printf("PASS playable material samples: door %d / framed layers %d / masonry %d affected components; production shadows/contact, exposed depth and immutable authority\n",affected[0],affected[1],affected[2]);
    swat_sim_init(&sim,cfg,81);swat_environment_art_prepare_location(&view->environment,&sim.world);
    Camera3D edge_camera={{-1,2.4f,1.8f},{4,1.25f,1.8f},{0,1,0},51,CAMERA_PERSPECTIVE};
    Image edge_before=sample_capture(view,edge_camera);sample_export(edge_before,directory,"nearby-walls-before");
    SwatHit edge_hit=swat_world_ray(&sim.world,(b3Pos){2.9f,1.3f,2.6f},swat_v(1,0,0),1.5f,b3_nullBodyId);assert(edge_hit.hit);
    assert(swat_world_blast(&sim.world,edge_hit.index,edge_hit.point)>0);
    int neighbours=0;for(int i=0;i<sim.world.count;i++) {
        const SwatObject* o=&sim.world.objects[i];
        if(fabsf(o->center.z)<1.8f && o->cuts)neighbours++;
    }
    assert(neighbours>0);
    Image edge_after=sample_capture(view,edge_camera);sample_export(edge_after,directory,"nearby-walls-after");
    assert(impact_pixel_difference(edge_before,edge_after)>1000);UnloadImage(edge_before);UnloadImage(edge_after);
    printf("PASS visible cross-assembly blast: %d damaged middle-wall components\n",neighbours);swat_sim_close(&sim);
    door_damage_graphics(view,directory);
}
