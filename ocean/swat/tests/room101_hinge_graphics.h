// Native import/ownership acceptance for the delivered seated assembly.
#include "../motel_wall_art.h"
static Vector3 hinge_vertex(const Mesh* mesh,int triangle,int corner) {
    int at=mesh->indices?mesh->indices[3*triangle+corner]:3*triangle+corner;
    return (Vector3){mesh->vertices[3*at],mesh->vertices[3*at+1],mesh->vertices[3*at+2]};
}
static double hinge_dot(Vector3 a,Vector3 b) {return (double)a.x*b.x+(double)a.y*b.y+(double)a.z*b.z;}
static bool hinge_triangle_contains(Vector3 p,Vector3 a,Vector3 b,Vector3 c) {
    Vector3 u=Vector3Subtract(b,a),v=Vector3Subtract(c,a),w=Vector3Subtract(p,a),n=Vector3CrossProduct(u,v);
    float area=Vector3Length(n);if(area<1e-12f || fabsf(Vector3DotProduct(n,w))>area*2e-6f)return false;
    double uu=hinge_dot(u,u),vv=hinge_dot(v,v),uv=hinge_dot(u,v);
    double wu=hinge_dot(w,u),wv=hinge_dot(w,v),det=uu*vv-uv*uv;
    if(det<=0)return false;
    double x=(wu*vv-wv*uv)/det,y=(wv*uu-wu*uv)/det;
    return x>=-.0001f && y>=-.0001f && x+y<=1.0001f;
}
static void hinge_casing_area(const SwatEnvironmentArt* art) {
    const Mesh* original=&art->room101[0].meshes[3];double expected=0,actual=0;
    for(int t=0;t<original->triangleCount;t++) {
        Vector3 a=hinge_vertex(original,t,0),b=hinge_vertex(original,t,1),c=hinge_vertex(original,t,2);
        expected+=.5*Vector3Length(Vector3CrossProduct(Vector3Subtract(b,a),Vector3Subtract(c,a)));
    }
    int group=sim.world.objects[13].wall_group;
    for(int i=group-1;i<sim.world.count && sim.world.objects[i].wall_group==group;i++) {
        const Model* clipped=&art->motel_wall_art->pieces[i][0].model;
        for(int m=0;m<clipped->meshCount;m++)for(int t=0;t<clipped->meshes[m].triangleCount;t++) {
            Vector3 v[3];for(int k=0;k<3;k++)v[k]=hinge_vertex(&clipped->meshes[m],t,k);
            bool match=false;
            for(int s=0;s<original->triangleCount && !match;s++) {
                Vector3 a=hinge_vertex(original,s,0),b=hinge_vertex(original,s,1),c=hinge_vertex(original,s,2);
                match=hinge_triangle_contains(v[0],a,b,c) && hinge_triangle_contains(v[1],a,b,c) && hinge_triangle_contains(v[2],a,b,c);
            }
            if(match)actual+=.5*Vector3Length(Vector3CrossProduct(Vector3Subtract(v[1],v[0]),Vector3Subtract(v[2],v[0])));
        }
    }
    fprintf(stderr,"seated casing source_area=%.9f rendered_piece_area=%.9f\n",expected,actual);
    assert(expected>.01 && fabs(actual-expected)<.00001);
}
static void room101_hinge_graphics(SwatView* view,const char* directory) {
    SwatConfig cfg=swat_default_config();cfg.mission=SWAT_MOTEL;cfg.randomize=false;cfg.hostile_fire=false;
    swat_sim_init(&sim,cfg,81);sim.extraction=(b3Pos){500,0,500};
    swat_environment_art_prepare_location(&view->environment,&sim.world);
    SwatEnvironmentArt* art=&view->environment;assert(art->room101_hinges_ready);
    assert(art->room101[0].meshCount==7 && art->room101[1].meshCount==8);
    const int triangles[]={3198,504};
    for(int k=0;k<2;k++) {
        int count=0;for(int m=0;m<art->room101_hinges[k].meshCount;m++)count+=art->room101_hinges[k].meshes[m].triangleCount;
        assert(count==triangles[k]);
    }
    const float heights[]={.23f,1.10f,1.92f};
    int frames[3];uint32_t fixed=0,remnants=0;
    for(int h=0;h<3;h++) {
        BoundingBox pin=GetMeshBoundingBox(art->room101_hinges[0].meshes[3*h+2]);
        assert(fabsf(pin.min.x+.003f)<1e-6f && fabsf(pin.max.x-.003f)<1e-6f);
        assert(fabsf(pin.min.z+.003f)<1e-6f && fabsf(pin.max.z-.003f)<1e-6f);
        assert(fabsf((pin.min.y+pin.max.y)*.5f-heights[h])<1e-6f); // Node translation exactly once.
        frames[h]=swat_motel_hinge_frame_owner(&sim.world,h);assert(frames[h]>SWAT_MOTEL_INSTANCES);
        fixed|=swat_environment_motel_hinge_mesh_mask(&sim.world,frames[h],false);
        remnants|=swat_environment_motel_hinge_mesh_mask(&sim.world,frames[h],true);
    }
    assert(fixed==438u && !remnants);
    assert(swat_environment_motel_hinge_mesh_mask(&sim.world,16,false)==73u);
    assert(!swat_environment_motel_hinge_mesh_mask(&sim.world,16,true));
    SwatObject* door=&sim.world.objects[16];
    for(int pose=0;pose<3;pose++) {
        door->door_angle=pose*SWAT_PI*.25f;door->yaw=door->closed_yaw+door->door_angle;
        door->center=swat_door_center(door,door->yaw);
        b3Body_SetTransform(door->body,door->center,swat_object_rotation(door));
        Vector3 target={door->hinge.x,door->hinge.y-door->half.y+1.10f,door->hinge.z+.008f};
        Camera3D close={Vector3Add(target,(Vector3){.12f,.08f,.20f}),target,{0,1,0},.25f,CAMERA_ORTHOGRAPHIC};
        char name[64];snprintf(name,sizeof(name),"seated-hinge-%d",pose*45);
        support_capture(view,close,directory,name);
    }
    hinge_casing_area(art);
    door->door_angle=0;door->yaw=door->closed_yaw;door->center=swat_door_center(door,door->yaw);
    b3Body_SetTransform(door->body,door->center,swat_object_rotation(door));
    support_hinge_shots(16,0);
    assert(swat_environment_motel_hinge_mesh_mask(&sim.world,16,false)==72u);
    assert(swat_environment_motel_hinge_mesh_mask(&sim.world,16,true)==15u);
    fixed=remnants=0;
    for(int h=0;h<3;h++) {
        fixed|=swat_environment_motel_hinge_mesh_mask(&sim.world,frames[h],false);
        remnants|=swat_environment_motel_hinge_mesh_mask(&sim.world,frames[h],true);
    }
    assert(fixed==432u && remnants==240u);
    // Removing the slab keeps the fixed remnants; it cannot leave intact pins
    // connecting to an absent leaf. A removed masonry support hides its pieces.
    door->active=false;
    assert(!swat_environment_motel_hinge_mesh_mask(&sim.world,16,true));
    fixed=0;for(int h=0;h<3;h++)fixed|=swat_environment_motel_hinge_mesh_mask(&sim.world,frames[h],false);
    assert(!fixed);
    int owner=frames[1];sim.world.objects[owner].active=false;
    assert(swat_motel_hinge_frame_owner(&sim.world,1)<0);
    assert(!swat_environment_motel_hinge_mesh_mask(&sim.world,owner,true));
    sim.world.objects[owner].active=true;
    puts("PASS seated hinge import: all source triangles, three coaxial pins with node transforms exactly once, hidden initial remnants, separate moving/fixed ownership, independent first-hinge failure and slab/frame removal");
    swat_sim_close(&sim);
    // Actual shots, last-hinge physics, falling, ray silhouettes, seven cached/
    // full depth comparisons and a pixel-identical late replica of this art.
    door_support_graphics(view,directory);
}
