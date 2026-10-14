#include "wall_support_fixture.h"
static Image wall_piece_image(SwatView* view,const SwatObject* piece,int side) {
    b3Quat q=swat_object_rotation(piece);b3Vec3 normal=b3RotateVector(q,swat_v(side,0,0)),up=b3RotateVector(q,swat_v(0,1,0));
    float y=0,z=0;int count=0;
    for(int s=0;s<piece->cuts->section_count;s++)for(int k=0;k<piece->cuts->sections[s].count;k++) {
        y+=piece->cuts->sections[s].vertices[k][0];z+=piece->cuts->sections[s].vertices[k][1];count++;
    }
    b3Pos p=b3OffsetPos(piece->center,b3RotateVector(q,swat_v(0,y/count,z/count)));
    Camera3D camera={{p.x+normal.x*3,p.y+normal.y*3,p.z+normal.z*3},{p.x,p.y,p.z},{up.x,up.y,up.z},1.3f,CAMERA_ORTHOGRAPHIC};
    RenderTexture2D target=LoadRenderTexture(600,600);assert(target.id);
    BeginTextureMode(target);ClearBackground(MAGENTA);BeginMode3D(camera);
    rlPushMatrix();rlTranslatef(piece->center.x,piece->center.y,piece->center.z);swat_environment_object_rotate(piece);
    assert(swat_environment_art_draw(&view->environment,piece));rlPopMatrix();
    EndMode3D();EndTextureMode();
    Image result=LoadImageFromTexture(target.texture);ImageFlipVertical(&result);UnloadRenderTexture(target);return result;
}
static void wall_piece_faces(SwatView* view,const SwatRubble* chunk,const char* directory,const char* prefix) {
    // Independently place the same retained source face back at its original
    // pose. A camera following the achieved body must see the same paint UVs.
    SwatObject canonical=chunk->object;canonical.rubble=NULL;canonical.center=chunk->origin;canonical.yaw=chunk->source_yaw;canonical.pitch=0;
    for(int side=-1;side<=1;side+=2) {
        Image source=wall_piece_image(view,&canonical,side),moving=wall_piece_image(view,&chunk->object,side);
        Color* a=LoadImageColors(source),*b=LoadImageColors(moving);int painted=0,bad=0,compared=0;
        for(int y=2;y<598;y++)for(int x=2;x<598;x++) {
            int at=y*600+x;bool edge=false;
            for(int dy=-2;dy<=2;dy++)for(int dx=-2;dx<=2;dx++) {
                Color c=a[at+dy*600+dx];edge|=c.r==MAGENTA.r && c.g==MAGENTA.g && c.b==MAGENTA.b;
            }
            if(edge)continue;
            painted++;compared++;bad+=abs(a[at].r-b[at].r)+abs(a[at].g-b[at].g)+abs(a[at].b-b[at].b)>6;
        }
        assert(painted>100 && bad<compared/100+10);
        sample_export(moving,directory,TextFormat("%s-source-face-%d",prefix,side));
        UnloadImageColors(a);UnloadImageColors(b);UnloadImage(source);UnloadImage(moving);
    }
}
static void wall_support_graphics_run(SwatView* view,const char* directory,int mission) {
    static SwatMap map;static SwatSnapshot state;
    SwatConfig cfg=swat_default_config();cfg.mission=mission;cfg.randomize=false;cfg.hostile_fire=false;cfg.max_ticks=30000;
    swat_sim_init(&sim,cfg,81);sim.extraction=(b3Pos){500,0,500};
    for(int i=1;i<sim.actor_count;i++)if(sim.actors[i].present){b3DestroyBody(sim.actors[i].controller.body.body);memset(&sim.actors[i],0,sizeof(sim.actors[i]));}
    int group=-1;float x=4,z=0;const char* name=mission==SWAT_MOTEL?"motel":"lab";
    if(mission==SWAT_MOTEL){group=sim.world.objects[106].wall_group;x=sim.world.objects[106].center.x;z=-3;}
    else for(int i=0;i<sim.world.count;i++)if(sim.world.objects[i].support_role==SWAT_SUPPORT_MEMBER){group=sim.world.objects[i].wall_group;break;}
    assert(group>0);swat_environment_art_prepare_location(&view->environment,&sim.world);
    Camera3D camera={{x-3.4f,2.05f,z+1.9f},{x,1.2f,z},{0,1,0},48,CAMERA_PERSPECTIVE};
    support_capture(view,camera,directory,TextFormat("%s-wall-0-intact",name));
    wall_support_ring(&sim.world,group,1.3f,z,.5f,.7f);assert(sim.world.rubble && sim.world.rubble->count);
    Image released=support_cached_image(view,camera);sample_export(released,directory,TextFormat("%s-wall-1-released",name));
    SwatRubblePool* authority=sim.world.rubble,*hidden=malloc(sizeof(*hidden));assert(hidden);*hidden=*authority;
    for(int i=0;i<hidden->count;i++)hidden->pieces[i].object.active=false;
    sim.world.rubble=hidden;Image absent=support_cached_image(view,camera);sim.world.rubble=authority;free(hidden);
    assert(impact_pixel_difference(released,absent)>10000);UnloadImage(released);UnloadImage(absent);
    support_ticks(15);support_capture(view,camera,directory,TextFormat("%s-wall-2-falling",name));
    int largest=-1;float area=0;
    for(int i=0;i<sim.world.rubble->count;i++) {
        SwatRubble* r=&sim.world.rubble->pieces[i];if(!r->object.active || r->object.material!=SWAT_DRYWALL)continue;
        float size=0;for(int s=0;s<r->state.section_count;s++)size+=swat_polygon_area(&r->state.sections[s]);
        if(size>area){area=size;largest=i;}
    }
    assert(largest>=0);wall_piece_faces(view,&sim.world.rubble->pieces[largest],directory,name);
    support_ticks(600);for(int i=0;i<sim.world.rubble->count;i++)if(sim.world.rubble->pieces[i].object.active)assert(!b3Body_IsAwake(sim.world.rubble->pieces[i].object.body));
    Image settled=support_cached_image(view,camera);sample_export(settled,directory,TextFormat("%s-wall-3-settled",name));
    swat_capture_map(&sim,1,&map);swat_capture_snapshot(&sim,1,&state);
    swat_sim_close(&sim);swat_apply_map(&sim,&map);assert(swat_apply_snapshot(&sim,&state));
    Image replica=support_cached_image(view,camera);assert(!impact_pixel_difference(settled,replica));
    sample_export(replica,directory,TextFormat("%s-wall-4-late-replica",name));UnloadImage(replica);UnloadImage(settled);
    swat_sim_close(&sim);
    printf("PASS wall support graphics mission%d: intact/released/falling/settled/late replica, cached/full shadows, original front/back paint follows achieved pose and immutable authority\n",mission);
}
static void wall_support_graphics(SwatView* view,const char* directory) {
    wall_support_graphics_run(view,directory,SWAT_BREACH_LAB);wall_support_graphics_run(view,directory,SWAT_MOTEL);
}
