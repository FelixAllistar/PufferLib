// Grazing rays must see material across damaged leaf thickness.
static void door_solid_case(SwatView* view,const char* directory,int mission) {
    SwatConfig cfg=swat_default_config();cfg.mission=mission;cfg.randomize=false;cfg.hostile_fire=false;
    swat_sim_init(&sim,cfg,81);swat_environment_art_prepare_location(&view->environment,&sim.world);
    int owner=16;
    if(mission==SWAT_BREACH_LAB) {
        SwatHit hit=swat_world_ray(&sim.world,(b3Pos){2.9f,1.12f,-4.2f},swat_v(1,0,0),1.5f,b3_nullBodyId);
        assert(hit.hit);owner=hit.index;
    }
    SwatObject* o=&sim.world.objects[owner];assert(o->door);
    for(int shot=0;shot<20;shot++) {
        b3Pos p=b3OffsetPos(o->center,b3RotateVector(swat_object_rotation(o),swat_v(-o->half.x,-.45f+(shot/5)*.3f,-.34f+(shot%5)*.17f)));
        assert(swat_world_impact_at(&sim.world,owner,p,34,false));
    }
    b3Pos mount=b3OffsetPos(o->center,b3RotateVector(swat_object_rotation(o),swat_v(-o->half.x,.3f,-.2f)));
    assert(swat_world_blast(&sim.world,owner,mount)>0);
    b3Quat q=swat_object_rotation(o);b3Vec3 normal=b3RotateVector(q,swat_v(1,0,0)),tangent=b3RotateVector(q,swat_v(0,0,1));
    Vector3 center={o->center.x,o->center.y,o->center.z};int total=0,missing=0;
    for(int side=-1;side<=1;side+=2)for(int angle=0;angle<3;angle++) {
        float across=angle==0?.8f:angle==1?.2f:.04f;
        Camera3D camera={Vector3Add(center,(Vector3){side*across*normal.x+2*tangent.x,0,side*across*normal.z+2*tangent.z}),center,{0,1,0},2*o->half.y+.15f,CAMERA_ORTHOGRAPHIC};
        Image image=support_leaf_image(view,camera,o);Color* pixels=LoadImageColors(image);int tested=0,holes=0;
        for(int y=30;y<870;y+=3)for(int x=2;x<598;x+=3) {
            bool solid=true;
            // A full two-pixel footprint excludes raster-edge and corner coverage.
            const Vector2 offsets[]={{0,0},{-2,0},{2,0},{0,-2},{0,2},{-2,-2},{-2,2},{2,-2},{2,2}};
            for(int i=0;i<9 && solid;i++) {
                Ray ray=GetScreenToWorldRayEx((Vector2){x+.5f+offsets[i].x,y+.5f+offsets[i].y},camera,600,900);
                solid=swat_world_object_ray(o,(b3Pos){ray.position.x,ray.position.y,ray.position.z},swat_v(ray.direction.x*5,ray.direction.y*5,ray.direction.z*5));
            }
            if(!solid)continue;
            tested++;Color c=pixels[y*600+x];holes+=c.r==255 && c.g==0 && c.b==255;
        }
        char name[80];snprintf(name,sizeof(name),"door-solid-%d-%d-%d",mission,side,angle);sample_export(image,directory,name);
        printf("door grazing mission=%d side=%d angle=%d solid=%d missing=%d\n",mission,side,angle,tested,holes);total+=tested;missing+=holes;
        UnloadImageColors(pixels);UnloadImage(image);
    }
    assert(total>10000 && missing==0);printf("PASS door grazing mission=%d total=%d missing=%d\n",mission,total,missing);
    swat_sim_close(&sim);
}
static void door_solid_graphics(SwatView* view,const char* directory) {
    door_solid_case(view,directory,SWAT_MOTEL);door_solid_case(view,directory,SWAT_BREACH_LAB);
}
