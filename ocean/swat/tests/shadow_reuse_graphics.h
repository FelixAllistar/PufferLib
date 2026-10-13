// Actual motel models and production skinned/procedural caster callbacks.
static void shadow_reuse_graphics(SwatView* view,const char* directory) {
    SwatConfig cfg=swat_default_config();cfg.mission=SWAT_MOTEL;cfg.hostile_fire=false;cfg.randomize=false;
    swat_sim_init(&sim,cfg,81);assert(view->characters);view->lighting.prepared=false;
    Camera3D cameras[]={
        {{-6.7f,1.6f,-9.5f},{-6.7f,1.1f,-5.5f},{0,1,0},65,CAMERA_PERSPECTIVE},
        {{-9.3f,1.5f,-7.5f},{-8.85f,1.25f,-6.18f},{0,1,0},55,CAMERA_PERSPECTIVE},
        {{-6.3f,1.6f,-2.6f},{-6.3f,.6f,-4.6f},{0,1,0},70,CAMERA_PERSPECTIVE},
        {{-6,1.6f,6},{-6,1.1f,-2},{0,1,0},65,CAMERA_PERSPECTIVE}
    };
    int door=-1;for(int i=0;i<sim.world.count;i++)if(sim.world.objects[i].door){door=i;break;}assert(door>=0);
    int comparisons=0;unsigned int static_faces=0,full_faces=0,changed_rooms=0;
    for(int stage=0;stage<12;stage++) {
        if(stage==2)sim.world.objects[door].door_open=true;
        if(stage==3){SwatHit hit=swat_world_ray(&sim.world,(b3Pos){-6.7f,1.1f,-7},swat_v(0,0,1),1.5f,b3_nullBodyId);assert(hit.hit&&swat_world_breach(&sim.world,hit.index,hit.point)>0);}
        if(stage==4)assert(swat_world_damage(&sim.world,SWAT_MOTEL_CABINET_FIRST,10000));
        if(stage==5){SwatController* c=&sim.actors[1].controller;b3Body_SetTransform(c->body.body,(b3Pos){-6.5,c->body.totalHeight*.5f,-3},b3Quat_identity);c->yaw=.7f;}
        if(stage==6)sim.actors[1].present=false;
        if(stage==7)sim.actors[1].present=true;
        if(stage==1 || stage==2 || (stage>=5 && stage<8)){SwatInput neutral=swat_neutral_input();for(int t=0;t<(stage<3?20:4);t++)swat_sim_step(&sim,&neutral);}
        if(stage==8) {
            SwatInput neutral=swat_neutral_input();for(int t=0;t<120;t++)swat_sim_step(&sim,&neutral);
            swat_view_prepare_shadows(view,&sim,cameras[0].position,false);
        }
        unsigned int partial=view->lighting.partial_shadow_updates;
        if(stage>=8) {
            int owner=door;
            if(!(stage&1))for(int i=SWAT_MOTEL_INSTANCES+1;i<sim.world.count;i++) {
                const SwatObject* o=&sim.world.objects[i];
                if(o->active && o->part==SWAT_PART_SKIN && o->material==SWAT_DRYWALL &&
                    o->half.y>.25f && o->half.z>.25f && swat_motel_wall_parent(&sim.world,o)>=0){owner=i;break;}
            }
            SwatObject* o=&sim.world.objects[owner];b3Quat q=swat_rotation(o->yaw,o->pitch);
            b3Vec3 normal=b3RotateVector(q,swat_v(1,0,0));
            b3Pos point=b3OffsetPos(o->center,b3RotateVector(q,swat_v(-o->half.x,stage<10?-.18f:.18f,-.08f)));
            b3Pos eye=b3OffsetPos(point,swat_mul(normal,-1.3f));
            cameras[1]=(Camera3D){{eye.x,eye.y+.2f,eye.z},{point.x,point.y,point.z},{0,1,0},55,CAMERA_PERSPECTIVE};
            assert(swat_world_impact_at(&sim.world,owner,point,34,false));
        }
        before=sim.world;unsigned int faces=view->lighting.room_face_updates;int rooms=view->lighting.room_updates;
        swat_view_prepare_shadows(view,&sim,cameras[0].position,false);
        static_faces+=view->lighting.room_face_updates-faces;
        changed_rooms+=(unsigned int)(view->lighting.room_updates-rooms);
        if(stage==1 || stage==2 || (stage>=5 && stage<8))assert(view->lighting.room_face_updates==faces);
        if(stage>=8)assert(view->lighting.partial_shadow_updates>partial);
        Image cached[4];for(int i=0;i<4;i++)cached[i]=room101_capture_prepared(view,cameras[i],true,640,480);
        view->lighting.prepared=false;faces=view->lighting.room_face_updates;
        swat_view_prepare_shadows(view,&sim,cameras[0].position,false);full_faces+=view->lighting.room_face_updates-faces;
        for(int i=0;i<4;i++) {
            Image full=room101_capture_prepared(view,cameras[i],true,640,480);Color* a=LoadImageColors(cached[i]),*b=LoadImageColors(full);int mismatch=0;
            for(int p=0;p<640*480;p++)mismatch+=abs(a[p].r-b[p].r)+abs(a[p].g-b[p].g)+abs(a[p].b-b[p].b)>3;
            if(mismatch>=10) {
                fprintf(stderr,"motel shadow stage=%d camera=%d mismatches=%d\n",stage,i,mismatch);
                char path[4096];snprintf(path,sizeof(path),"%s/shadow-region-failed.png",directory);assert(ExportImage(cached[i],path));
                snprintf(path,sizeof(path),"%s/shadow-region-reference.png",directory);assert(ExportImage(full,path));
            }
            assert(mismatch<10);comparisons++;
            if(stage==3 || stage==7){char path[4096];snprintf(path,sizeof(path),"%s/motel-shadow-%d-%d.png",directory,stage,i);assert(ExportImage(cached[i],path));}
            UnloadImageColors(a);UnloadImageColors(b);UnloadImage(cached[i]);UnloadImage(full);
        }
        assert(!memcmp(&before,&sim.world,sizeof(before)));
    }
    assert(static_faces<full_faces);swat_sim_close(&sim);
    printf("PASS production motel shadow reuse: %d full-redraw comparisons, original environment/skin/procedural casters, doors/C4/props/actors and regional bullet-hole updates, %u cached static faces / %u complete-changed-room faces / %u unconditional reference faces, immutable authority\n",comparisons,static_faces,changed_rooms*SWAT_LAMP_FACES,full_faces);
}
